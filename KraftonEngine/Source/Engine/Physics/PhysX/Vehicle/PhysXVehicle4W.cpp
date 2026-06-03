#include "Physics/PhysX/Vehicle/PhysXVehicle4W.h"

#include "Physics/PhysX/Vehicle/PhysXVehicleConfig.h"
#include "Physics/PhysX/PhysXHelper.h"

using namespace physx;

namespace
{
	// 디지털(키보드) 입력 → 아날로그 보간 속도. 인덱스 순서는
	// PxVehicleDrive4WControl: accel, brake, handbrake, steerLeft, steerRight.
	// (배열 길이는 eMAX_NB_ANALOG_INPUTS=16이고 나머지는 0으로 둔다.)
	const PxVehicleKeySmoothingData GKeySmoothing =
	{
		{ 3.0f, 3.0f, 10.0f, 2.5f, 2.5f },
		{ 5.0f, 5.0f, 10.0f, 5.0f, 5.0f }
	};

	// 서스펜션 raycast 프리필터.
	// 자기 차량(같은 owner UUID)의 shape는 무시하고, 그 외 모든 shape는
	// drivable ground로 본다. 엔진 FilterShader의 same-owner 가드와 같은 원리
	// (filter word3 = 소유 액터 UUID)를 raycast에 그대로 적용한 것.
	PxQueryHitType::Enum WheelSuspensionPreFilter(
		PxFilterData QueryFilterData, PxFilterData ObjectFilterData,
		const void* /*constantBlock*/, PxU32 /*constantBlockSize*/, PxHitFlags& /*hitFlags*/)
	{
		if (QueryFilterData.word3 != 0 && QueryFilterData.word3 == ObjectFilterData.word3)
		{
			return PxQueryHitType::eNONE;
		}
		return PxQueryHitType::eBLOCK;
	}
}

FPhysXVehicle4W::~FPhysXVehicle4W()
{
	Release();
}

bool FPhysXVehicle4W::Build(PxScene* Scene, PxPhysics* Physics, PxRigidDynamic* ChassisActor,
	PxMaterial* DefaultMaterial, const FPxVehicleSetup& Setup)
{
	if (!Scene || !Physics || !ChassisActor || Drive)
	{
		return false;
	}

	// 섀시 shape의 query filter word3 = owner actor UUID. 서스펜션 raycast가
	// 자기 차량 shape를 무시하도록 동일 값을 query filter로 쓴다(프리필터 참조).
	uint32 OwnerFilterId = 0;
	if (ChassisActor->getNbShapes() > 0)
	{
		PxShape* FirstShape = nullptr;
		ChassisActor->getShapes(&FirstShape, 1);
		if (FirstShape)
		{
			OwnerFilterId = FirstShape->getQueryFilterData().word3;
		}
	}

	// ── Wheels sim data ──
	PxVehicleWheelsSimData* WheelsSimData = PxVehicleWheelsSimData::allocate(NbWheels);

	PxFilterData SuspQueryFilter;
	SuspQueryFilter.word3 = OwnerFilterId;

	// 관성 모먼트: 1/2 * m * r^2
	const PxReal WheelMOI = 0.5f * Setup.WheelMass * Setup.WheelRadius * Setup.WheelRadius;

	for (uint32 i = 0; i < NbWheels; ++i)
	{
		const bool bFront = (i < 2);

		PxVehicleWheelData Wheel;
		Wheel.mRadius = Setup.WheelRadius;
		Wheel.mWidth = Setup.WheelWidth;
		Wheel.mMass = Setup.WheelMass;
		Wheel.mMOI = WheelMOI;
		Wheel.mMaxBrakeTorque = Setup.MaxBrakeTorque;
		Wheel.mMaxHandBrakeTorque = bFront ? 0.0f : Setup.MaxHandBrakeTorque;
		Wheel.mMaxSteer = bFront ? (PxPi * Setup.MaxSteerAngleDeg / 180.0f) : 0.0f;

		PxVehicleSuspensionData Susp;
		Susp.mSpringStrength = Setup.SpringStrength;
		Susp.mSpringDamperRate = Setup.SpringDamperRate;
		Susp.mMaxCompression = Setup.SuspensionMaxRaise;
		Susp.mMaxDroop = Setup.SuspensionMaxDrop;
		Susp.mSprungMass = Setup.ChassisMass * 0.25f;

		PxVehicleTireData Tire;
		Tire.mType = Setup.TireType;

		const PxVec3 Offset = FPhysXHelper::ToPxVec3(Setup.WheelLocalLocation[i]);

		WheelsSimData->setWheelData(i, Wheel);
		WheelsSimData->setSuspensionData(i, Susp);
		WheelsSimData->setTireData(i, Tire);
		WheelsSimData->setWheelCentreOffset(i, Offset);
		WheelsSimData->setSuspTravelDirection(i, PxVec3(0.0f, 0.0f, -1.0f)); // Z-up: 아래로
		WheelsSimData->setSuspForceAppPointOffset(i, Offset);
		WheelsSimData->setTireForceAppPointOffset(i, Offset);
		WheelsSimData->setSceneQueryFilterData(i, SuspQueryFilter);
		WheelsSimData->setWheelShapeMapping(i, -1); // PhysX wheel shape 없음(시각화는 컴포넌트)
	}

	// ── Drive sim data (엔진/클러치/기어/디퍼런셜/Ackermann) ──
	PxVehicleDriveSimData4W DriveSimData;

	PxVehicleEngineData Engine;
	Engine.mPeakTorque = Setup.EnginePeakTorque;
	Engine.mMaxOmega = Setup.EngineMaxOmega;
	DriveSimData.setEngineData(Engine);

	PxVehicleClutchData Clutch;
	Clutch.mStrength = Setup.ClutchStrength;
	DriveSimData.setClutchData(Clutch);

	PxVehicleGearsData Gears;
	Gears.mSwitchTime = Setup.GearSwitchTime;
	DriveSimData.setGearsData(Gears);

	PxVehicleAutoBoxData AutoBox;
	DriveSimData.setAutoBoxData(AutoBox);

	PxVehicleDifferential4WData Diff;
	Diff.mType = PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;
	DriveSimData.setDiffData(Diff);

	PxVehicleAckermannGeometryData Ackermann;
	Ackermann.mAccuracy = 1.0f;
	Ackermann.mAxleSeparation = Setup.WheelBase;
	Ackermann.mFrontWidth = Setup.TrackWidth;
	Ackermann.mRearWidth = Setup.TrackWidth;
	DriveSimData.setAckermannGeometryData(Ackermann);

	// 모든 바퀴 구동(nbNonDriven=0). WheelsSimData는 create가 복사하므로 즉시 free.
	Drive = PxVehicleDrive4W::create(Physics, ChassisActor, *WheelsSimData, DriveSimData, 0);
	WheelsSimData->free();

	if (!Drive)
	{
		return false;
	}

	Drive->setToRestState();
	Drive->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
	Drive->mDriveDynData.setUseAutoGears(true);

	// 속도-조향 제한 테이블.
	const int32 PairCount = (Setup.SteerVsSpeedCount < 8) ? Setup.SteerVsSpeedCount : 8;
	for (int32 i = 0; i < PairCount; ++i)
	{
		SteerVsSpeed.addPair(Setup.SteerVsSpeed[i].Speed, Setup.SteerVsSpeed[i].Scale);
	}

	// ── 자기 1대용 노면-타이어 마찰 페어 ──
	// 노면/타이어 타입 각 1종(기본 노면 = DefaultMaterial). 다른 PxMaterial 지면도
	// surface type 0으로 폴백되어 동일 마찰을 받는다.
	PxVehicleDrivableSurfaceType SurfaceTypes[1];
	SurfaceTypes[0].mType = 0;
	const PxMaterial* SurfaceMaterials[1] = { DefaultMaterial };

	FrictionPairs = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(1, 1);
	FrictionPairs->setup(1, 1, SurfaceMaterials, SurfaceTypes);
	FrictionPairs->setTypePairFriction(0, Setup.TireType, Setup.TireFriction);

	// ── 자기 1대용 서스펜션 raycast BatchQuery (고정 크기 = 바퀴수) ──
	// 결과는 멤버 버퍼(SqResults/SqHits)에 담는다 → 객체 수명 동안 주소 고정.
	PxBatchQueryDesc Desc(NbWheels, 0, 0);
	Desc.queryMemory.userRaycastResultBuffer = SqResults;
	Desc.queryMemory.userRaycastTouchBuffer = SqHits;
	Desc.queryMemory.raycastTouchBufferSize = NbWheels;
	Desc.preFilterShader = WheelSuspensionPreFilter;
	BatchQuery = Scene->createBatchQuery(Desc);

	// 바퀴 결과 버퍼(멤버) 연결. PxVehicleUpdates가 여기 결과를 채운다.
	WheelQuery.wheelQueryResults = WheelResults;
	WheelQuery.nbWheelQueryResults = NbWheels;

	return true;
}

void FPhysXVehicle4W::Release()
{
	if (BatchQuery)
	{
		BatchQuery->release();
		BatchQuery = nullptr;
	}
	if (FrictionPairs)
	{
		FrictionPairs->release();
		FrictionPairs = nullptr;
	}
	if (Drive)
	{
		Drive->free();
		Drive = nullptr;
	}
}

void FPhysXVehicle4W::Simulate(float DeltaTime)
{
	if (!Drive || !BatchQuery || !FrictionPairs)
	{
		return;
	}

	ResolveDriveInputs();
	SmoothInputs(DeltaTime);

	// 자기 1대만 배열에 담아 일괄 함수 호출. 순서 고정(반드시 simulate() 직전).
	PxVehicleWheels* Vehicles[1] = { Drive };
	PxVehicleSuspensionRaycasts(BatchQuery, 1, Vehicles, NbWheels, SqResults);
	PxVehicleUpdates(DeltaTime, PxVec3(0.0f, 0.0f, -9.81f), *FrictionPairs, 1, Vehicles, &WheelQuery);
}

void FPhysXVehicle4W::SmoothInputs(float DeltaTime)
{
	if (!Drive)
	{
		return;
	}

	PxVehicleDrive4WSmoothDigitalRawInputsAndSetAnalogInputs(
		GKeySmoothing, SteerVsSpeed, RawInput, DeltaTime, IsInAir(), *Drive);
}

void FPhysXVehicle4W::ResolveDriveInputs()
{
	// 전진과 후진은 기어가 다르다. auto gearbox 는 reverse 로는 절대 안 가므로 후진은 직접
	// reverse 기어로 바꾼다(이땐 auto off). 진행 방향과 반대 입력이 들어오면 곧장 기어를
	// 바꾸지 않고 먼저 제동해 정지에 가까워진 뒤 전환한다(고속에서 기어 역전 방지).
	const float ForwardSpeed = Drive->computeForwardSpeed(); // +면 전진, -면 후진 (m/s)
	constexpr float StopSpeed = 1.0f;                        // 방향 전환 전 정지로 간주할 임계

	const bool bInReverse = (Drive->mDriveDynData.getCurrentGear() == PxVehicleGearsData::eREVERSE);

	bool bAccel = false;
	bool bBrake = bBrakeInput; // Space = 풋 브레이크 (항상 우선)

	if (bThrottleInput && !bReverseInput)
	{
		if (ForwardSpeed < -StopSpeed)
		{
			bBrake = true; // 아직 뒤로 가는 중 → 먼저 정지
		}
		else
		{
			if (bInReverse)
			{
				Drive->mDriveDynData.setUseAutoGears(true);
				Drive->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
			}
			bAccel = true;
		}
	}
	else if (bReverseInput && !bThrottleInput)
	{
		if (ForwardSpeed > StopSpeed)
		{
			bBrake = true; // 아직 앞으로 가는 중 → 먼저 정지
		}
		else
		{
			if (!bInReverse)
			{
				Drive->mDriveDynData.setUseAutoGears(false);
				Drive->mDriveDynData.forceGearChange(PxVehicleGearsData::eREVERSE);
			}
			bAccel = true;
		}
	}

	RawInput.setDigitalAccel(bAccel);
	RawInput.setDigitalBrake(bBrake);
}

bool FPhysXVehicle4W::IsInAir() const
{
	for (uint32 i = 0; i < NbWheels; ++i)
	{
		if (!WheelResults[i].isInAir)
		{
			return false;
		}
	}
	return true;
}

bool FPhysXVehicle4W::GetWheelLocalPose(uint32 WheelIndex, PxTransform& OutPose) const
{
	if (WheelIndex >= NbWheels)
	{
		return false;
	}

	OutPose = WheelResults[WheelIndex].localPose;
	return true;
}
