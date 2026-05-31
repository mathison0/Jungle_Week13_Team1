#pragma once

#include "Object/Object.h"
#include "Core/Types/CoreTypes.h"

#include "Source/Engine/Physics/PhysicsMaterial/PhysicalMaterial.generated.h"

class FArchive;

namespace physx
{
	class PxPhysics;
	class PxMaterial;
}

// 두 물리 재질이 만났을 때 마찰/탄성 값을 합치는 방식
UENUM()
enum class EPMCombineMode : uint8
{
	Average = 0,		// (a + b) / 2
	Min,				// min(a, b)
	Multiply,			// a * b
	Max,				// max(a, b)

	COUNT
};

UCLASS()
class UPhysicalMaterial : public UObject
{
public:
	GENERATED_BODY()

	~UPhysicalMaterial() override;

	// ---- PxMaterial Section -----
	// 재질에 대응되는 PxMaterial을 반환 -> 없으면 PhysX를 통해 새로 생성
	physx::PxMaterial* GetOrCreatePxMaterial(physx::PxPhysics* Physics);

	// 이미 만들어진 PxMaterial에 현재 값 다시 반영
	void UpdatePxMaterial();

	// 이 객체가 소유한 PxMaterial Cache 해제
	void ReleasePxMaterial();

	// --- Physical Material Section --- 
	float GetStaticFriction() const { return StaticFriction; }
	float GetDynamicFriction() const { return DynamicFriction; }
	float GetRestitution() const { return Restitution; }
	float GetDensity() const { return Density; }
	float GetRaiseMassToPower() const { return RaiseMassToPower; }

	bool GetOverrideFrictionCombineMode() const { return bOverrideFrictionCombineMode; }
	bool GetOverrideRestitutionCombineMode() const { return bOverrideRestitutionCombineMode; }

	EPMCombineMode GetFrictionCombineMode() const { return FrictionCombineMode; }
	EPMCombineMode GetRestitutionCombineMode() const { return RestitutionCombineMode; }

	void SetStaticFriction(float InValue);
	void SetDynamicFriction(float InValue);
	void SetRestitution(float InValue);
	void SetDensity(float InValue);
	void SetRaiseMassToPower(float InValue);

	void SetFrictionCombineMode(EPMCombineMode InMode, bool bOverride = true);
	void SetRestitutionCombineMode(EPMCombineMode InMode, bool bOverride = true);

	// --- Asset Section ---
	// 에셋 파일로 저장/로드되는 값들. PxMaterialHandle은 런타임 캐시라 제외한다.
	void Serialize(FArchive& Ar) override;

	// 매니저가 저장 경로로 사용하는 런타임 정보 (직렬화 대상 아님)
	void SetSourcePath(const FString& InPath) { SourcePath = InPath; }
	const FString& GetSourcePath() const { return SourcePath; }

private:
	// 정지 마찰 계수
	UPROPERTY(Edit, Save, Category = "PhysicalMaterial", DisplayName = "Static Friction", Min = 0.0f, Max = 100.0f, Speed = 0.05f)
	float StaticFriction = 0.5f;

	// 동적 마찰
	UPROPERTY(Edit, Save, Category = "PhysicalMaterial", DisplayName = "Dynamic Friction", Min = 0.0f, Max = 100.0f, Speed = 0.05f)
	float DynamicFriction = 0.5f;

	// 반발 계수. 0->완전 비탄성 충돌 / 1 -> 완전 탄성 충돌
	UPROPERTY(Edit, Save, Category = "PhysicalMaterial", DisplayName = "Restitution", Min = 0.0f, Max = 1.0f, Speed = 0.05f)
	float Restitution = 0.3f;

	// 밀도 이후 BodySetup 기반 mass 계산에 사용
	UPROPERTY(Edit, Save, Category = "PhysicalMaterial", DisplayName = "Density", Min = 0.0f, Max = 10000.0f, Speed = 0.1f)
	float Density = 1.0f;

	// RaiseMassToPower. 큰 물체의 질량 증가를 완화할 때 사용. Mass = pow(ComputedMass, RaiseMassToPower);
	UPROPERTY(Edit, Save, Category = "PhysicalMaterial", DisplayName = "Raise Mass To Power", Min = 0.1f, Max = 1.0f, Speed = 0.01f)
	float RaiseMassToPower = 0.75f;
	
	UPROPERTY(Edit, Save, Category = "PhysicalMaterial", DisplayName = "Override Friction Combine Mode")
	bool bOverrideFrictionCombineMode = false;

	UPROPERTY(Edit, Save, Category = "PhysicalMaterial", DisplayName = "Friction Combine Mode", Enum = EPMCombineMode)
	EPMCombineMode FrictionCombineMode = EPMCombineMode::Average;

	UPROPERTY(Edit, Save, Category = "PhysicalMaterial", DisplayName = "Override Restitution Combine Mode")
	bool bOverrideRestitutionCombineMode = false;

	UPROPERTY(Edit, Save, Category = "PhysicalMaterial", DisplayName = "Restitution Combine Mode", Enum = EPMCombineMode)
	EPMCombineMode RestitutionCombineMode = EPMCombineMode::Average;

	// PxMaterial은 runtime Object -> 저장 대상X
	// UPhysicalMaterial 내부 runtime Cache로 취급
	physx::PxMaterial* PxMaterialHandle = nullptr;

	// 에셋 파일 경로 (런타임 전용, 직렬화 안 함)
	FString SourcePath;
};
