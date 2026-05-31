#include "PhysicalMaterial.h"

#include <PxPhysicsAPI.h>
#include "Core/Logging/Log.h"
#include "Math/MathUtils.h"
#include "Serialization/Archive.h"

using namespace physx;

namespace
{
	PxCombineMode::Enum ToPxCombineMode(EPMCombineMode Mode)
	{
		switch (Mode)
		{
		case EPMCombineMode::Average:
			return PxCombineMode::eAVERAGE;
		case EPMCombineMode::Min:
			return PxCombineMode::eMIN;
		case EPMCombineMode::Multiply:
			return PxCombineMode::eMULTIPLY;
		case EPMCombineMode::Max:
			return PxCombineMode::eMAX;
		default:
			return PxCombineMode::eAVERAGE;
		}
	}
}

UPhysicalMaterial::~UPhysicalMaterial()
{
	ReleasePxMaterial();
}

physx::PxMaterial* UPhysicalMaterial::GetOrCreatePxMaterial(physx::PxPhysics* Physics)
{
	// Physics가 없으면 애초에 Material 생성 불가.
	if (!Physics)
	{
		UE_LOG("[PhysicalMateiral] Invalid PxPhysics");
		return nullptr;
	}

	if (PxMaterialHandle)
	{
		return PxMaterialHandle;
	}

	PxMaterialHandle = Physics->createMaterial(
		FMath::ClampMin(StaticFriction, 0.f),
		FMath::ClampMin(DynamicFriction, 0.f),
		FMath::Clamp(Restitution, 0.f, 1.f)
	);

	if (!PxMaterialHandle)
	{
		UE_LOG("[Physical Material] Failed to Create Material!");
		PxMaterialHandle = nullptr;
		return nullptr;
	}

	UpdatePxMaterial();

	UE_LOG("[Physical Material] Success Create Physical Material!");
	return PxMaterialHandle;
}

void UPhysicalMaterial::UpdatePxMaterial()
{
	// 핸들이 비어있는 건 정상 상태(= PxMaterial을 만든 적 없음)다
	if (!PxMaterialHandle)
	{
		return;
	}

	PxMaterialHandle->setStaticFriction(FMath::ClampMin(StaticFriction, 0.f));
	PxMaterialHandle->setDynamicFriction(FMath::ClampMin(DynamicFriction, 0.f));
	PxMaterialHandle->setRestitution(FMath::Clamp(Restitution, 0.f, 1.f));

	if (bOverrideFrictionCombineMode)
	{
		PxMaterialHandle->setFrictionCombineMode(ToPxCombineMode(FrictionCombineMode));
	}

	if (bOverrideRestitutionCombineMode)
	{
		PxMaterialHandle->setRestitutionCombineMode(ToPxCombineMode(RestitutionCombineMode));
	}
}

void UPhysicalMaterial::ReleasePxMaterial()
{
	// 핸들이 비어있는 건 정상 상태(= PxMaterial을 만든 적 없음)다
	if (!PxMaterialHandle)
	{
		return;
	}

	PxMaterialHandle->release();
	PxMaterialHandle = nullptr;
}

void UPhysicalMaterial::SetStaticFriction(float InValue)
{
	StaticFriction = FMath::ClampMin(InValue, 0.f);
	UpdatePxMaterial();
}

void UPhysicalMaterial::SetDynamicFriction(float InValue)
{
	DynamicFriction = FMath::ClampMin(InValue, 0.f);
	UpdatePxMaterial();
}

void UPhysicalMaterial::SetRestitution(float InValue)
{
	Restitution = FMath::Clamp(InValue, 0.f, 1.f);
	UpdatePxMaterial();
}

void UPhysicalMaterial::SetDensity(float InValue)
{
	Density = FMath::ClampMin(InValue, 0.f);
}

void UPhysicalMaterial::SetRaiseMassToPower(float InValue)
{
	RaiseMassToPower = FMath::Clamp(InValue, 0.1f, 1.0f);
}

void UPhysicalMaterial::SetFrictionCombineMode(EPMCombineMode InMode, bool bOverride /*= true*/)
{
	FrictionCombineMode = InMode;
	bOverrideFrictionCombineMode = bOverride;
	UpdatePxMaterial();
}

void UPhysicalMaterial::SetRestitutionCombineMode(EPMCombineMode InMode, bool bOverride /*= true*/)
{
	RestitutionCombineMode = InMode;
	bOverrideRestitutionCombineMode = bOverride;
	UpdatePxMaterial();
}

// --- Asset 직렬화 ---
// Manager가 헤더/메타데이터를 쓴 뒤 이 함수로 값만 직렬화한다.
// enum class:uint8 은 trivially-copyable이라 Archive의 템플릿 operator<< 로 1바이트 왕복.
void UPhysicalMaterial::Serialize(FArchive& Ar)
{
	Ar << StaticFriction;
	Ar << DynamicFriction;
	Ar << Restitution;
	Ar << Density;
	Ar << RaiseMassToPower;
	Ar << bOverrideFrictionCombineMode;
	Ar << FrictionCombineMode;
	Ar << bOverrideRestitutionCombineMode;
	Ar << RestitutionCombineMode;
}
