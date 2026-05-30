#include "PhysXCollision.h"

#include "PhysXShapeDesc.h"

using namespace physx;

// filterData 레이아웃:
//   word0 = 자신의 ObjectType (ECollisionChannel)
//   word1 = Block 비트마스크 (해당 채널에 Block 응답인 비트)
//   word2 = Overlap 비트마스크 (해당 채널에 Overlap 응답인 비트)
//   word3 = 소유 액터 UUID - 같은 액터의 두 컴포넌트끼리 충돌을 무시하기 위함
//           Native 측 O(N^2) 루프의 same-owner 가드와 동일 의미.
//           Owner가 없거나 UUID가 0이면 가드 미적용.
static PxFilterData BuildFilterData(const FPhysXShapeCollisionDesc& Collision)
{
	PxFilterData Filter;
	Filter.word0 = static_cast<PxU32>(Collision.ObjectType);
	Filter.word1 = 0;
	Filter.word2 = 0;
	Filter.word3 = Collision.OwnerActorId;

	for (int32 Ch = 0; Ch < static_cast<int32>(ECollisionChannel::ActiveCount); ++Ch)
	{
		ECollisionResponse R = Collision.Responses.GetResponse(static_cast<ECollisionChannel>(Ch));
		if (R == ECollisionResponse::Block)   Filter.word1 |= (1u << Ch);
		if (R == ECollisionResponse::Overlap) Filter.word2 |= (1u << Ch);
	}

	return Filter;
}

void FPhysXCollision::SetupFilterData(PxShape* Shape, const FPhysXShapeCollisionDesc& Collision)
{
	if (!Shape) return;

	PxFilterData Filter = BuildFilterData(Collision);
	Shape->setSimulationFilterData(Filter);
	Shape->setQueryFilterData(Filter);
}

// 엔진의 채널/응답 매트릭스를 PhysX에서 처리한다.
// 양쪽 모두 상대 채널에 대해 Block이면 물리 충돌, 한쪽이라도 Overlap이면 트리거, 그 외 무시.
PxFilterFlags FPhysXCollision::FilterShader(
	PxFilterObjectAttributes Attributes0, PxFilterData FilterData0,
	PxFilterObjectAttributes Attributes1, PxFilterData FilterData1,
	PxPairFlags& PairFlags, const void*, PxU32)
{
	// 같은 액터(같은 owner UUID)의 두 컴포넌트끼리는 충돌 무시.
	// Native 측 O(N^2) 루프의 same-owner 가드와 동일 의미. 차량 차체-바퀴처럼
	// 한 액터가 여러 콜라이더를 가질 때 자기끼리 충돌 시뮬레이션되는 문제를 막는다.
	if (FilterData0.word3 != 0 && FilterData0.word3 == FilterData1.word3)
	{
		return PxFilterFlag::eKILL;
	}

	// 트리거 처리 - 한쪽이라도 트리거면 오버랩 통지만.
	if (PxFilterObjectIsTrigger(Attributes0) || PxFilterObjectIsTrigger(Attributes1))
	{
		PairFlags = PxPairFlag::eTRIGGER_DEFAULT;
		return PxFilterFlag::eDEFAULT;
	}

	PxU32 ChannelA = FilterData0.word0; // A의 ObjectType
	PxU32 ChannelB = FilterData1.word0; // B의 ObjectType

	// A가 B의 채널에 대해 Block인지, B가 A의 채널에 대해 Block인지.
	bool bABlocksB = (FilterData0.word1 & (1u << ChannelB)) != 0;
	bool bBBlocksA = (FilterData1.word1 & (1u << ChannelA)) != 0;

	// 양쪽 모두 Block -> 물리 충돌 + contact 콜백.
	if (bABlocksB && bBBlocksA)
	{
		PairFlags = PxPairFlag::eCONTACT_DEFAULT
			| PxPairFlag::eNOTIFY_TOUCH_FOUND
			| PxPairFlag::eNOTIFY_TOUCH_LOST
			| PxPairFlag::eNOTIFY_CONTACT_POINTS;
		return PxFilterFlag::eDEFAULT;
	}

	// 한쪽이라도 Overlap -> 겹침 감지만 (물리적 밀어내기 없음).
	// 일반적으로 이 케이스는 trigger shape 분기에서 이미 처리되지만, 등록 시점에
	// trigger flag로 분류되지 않은 simulation shape pair인데 응답이 Overlap인 경우의
	// 안전망. eSOLVE_CONTACT 명시 제외 + eDETECT_DISCRETE_CONTACT + NOTIFY로 detection만.
	bool bAOverlapsB = (FilterData0.word2 & (1u << ChannelB)) != 0;
	bool bBOverlapsA = (FilterData1.word2 & (1u << ChannelA)) != 0;

	if (bAOverlapsB || bBOverlapsA)
	{
		PairFlags = PxPairFlag::eDETECT_DISCRETE_CONTACT
			| PxPairFlag::eNOTIFY_TOUCH_FOUND
			| PxPairFlag::eNOTIFY_TOUCH_LOST;
		return PxFilterFlag::eDEFAULT;
	}

	// Ignore - 쌍 완전히 제거.
	return PxFilterFlag::eKILL;
}
