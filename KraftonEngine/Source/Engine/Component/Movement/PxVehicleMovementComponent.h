#pragma once

#include "MovementComponent.h"
#include "Math/Rotator.h"

#include <memory>

#include "Source/Engine/Component/Movement/PxVehicleMovementComponent.generated.h"

class FPhysXVehicle4W;
class UPrimitiveComponent;
class USceneComponent;
struct FPxVehicleSetup;

// ===========================================================================
// UPxVehicleMovementComponent
//
// PxVehicleDrive4W 기반 4륜 차량을 구동하는 무브먼트 컴포넌트.
// 에디터 튜닝값(UPROPERTY)으로 FPhysXVehicle4W를 lazy 빌드해 PhysX 씬에 1대로
// 등록하고, 매 프레임 키보드 입력을 차에 넣는다. PhysX가 돌려준 바퀴 자세
// (localPose)는 액터가 꽂아준 바퀴 시각 컴포넌트에 반영한다.
//
// 실제 raycast/힘적용은 Scene이 simulate() 직전에 차의 Simulate()를 불러서 한다
// (즉 이 컴포넌트는 입력 주입 + 결과 시각화만 담당).
// ===========================================================================
UCLASS()
class UPxVehicleMovementComponent : public UMovementComponent
{
public:
	GENERATED_BODY()

	UPxVehicleMovementComponent();
	~UPxVehicleMovementComponent() override;

	void EndPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	// 액터가 바퀴 시각 컴포넌트를 꽂아준다. [0]=FL [1]=FR [2]=RL [3]=RR.
	void SetWheelVisualComponent(int32 WheelIndex, USceneComponent* Visual);

private:
	void EnsureVehicle();   // 섀시 강체가 준비되면 차를 만들어 씬에 등록(lazy)
	void DestroyVehicle();  // 씬에서 등록 해제 + PhysX 자원 해제
	void UpdateWheelVisuals();
	FPxVehicleSetup BuildSetup() const;
	UPrimitiveComponent* GetChassisComponent() const;

	std::unique_ptr<FPhysXVehicle4W> Vehicle;

	// 바퀴 시각 컴포넌트. [0]=FL [1]=FR [2]=RL [3]=RR. 액터가 SetWheelVisualComponent 로 꽂아준다.
	// 오브젝트 참조로 직렬화(Save)해야 씬 저장/로드 후에도 바인딩이 살아남는다 — 비-리플렉션
	// 멤버나 FName 매칭(FName 은 씬에 저장 안 됨)에 의존하면 로드 시 끊겨 바퀴가 안 돈다.
	UPROPERTY(Save, Category="Vehicle|Visual")
	TArray<USceneComponent*> WheelVisuals;

	// --- 입력 ---
	UPROPERTY(Edit, Save, Category="Vehicle|Input", DisplayName="Use Keyboard Input")
	bool bUseKeyboardInput = true;

	// --- 카메라 마우스 룩 ---
	// 차량 pawn 은 ACharacter 가 아니라 자동 mouse look 이 없어 여기서 ControlRotation 을 누적한다.
	UPROPERTY(Edit, Save, Category="Vehicle|Input", DisplayName="Use Mouse Look")
	bool bUseMouseLook = true;
	UPROPERTY(Edit, Save, Category="Vehicle|Input", DisplayName="Mouse Sensitivity")
	float MouseSensitivity = 0.2f;     // deg / pixel — yaw/pitch 공통
	UPROPERTY(Edit, Save, Category="Vehicle|Input", DisplayName="Min Camera Pitch")
	float MinCameraPitch = -80.0f;     // 위 한도
	UPROPERTY(Edit, Save, Category="Vehicle|Input", DisplayName="Max Camera Pitch")
	float MaxCameraPitch = 60.0f;      // 아래 한도

	// --- 바퀴 배치/제원 ---
	UPROPERTY(Edit, Save, Category="Vehicle|Wheel", DisplayName="Wheel Radius")
	float WheelRadius = 0.35f;
	UPROPERTY(Edit, Save, Category="Vehicle|Wheel", DisplayName="Wheel Width")
	float WheelWidth = 0.25f;
	UPROPERTY(Edit, Save, Category="Vehicle|Wheel", DisplayName="Front Axle X")
	float FrontAxleX = 1.45f;
	UPROPERTY(Edit, Save, Category="Vehicle|Wheel", DisplayName="Rear Axle X")
	float RearAxleX = -1.35f;
	UPROPERTY(Edit, Save, Category="Vehicle|Wheel", DisplayName="Half Track Width")
	float HalfTrackWidth = 0.85f;
	UPROPERTY(Edit, Save, Category="Vehicle|Wheel", DisplayName="Wheel Local Z")
	float WheelLocalZ = -0.45f;

	// --- 서스펜션 ---
	UPROPERTY(Edit, Save, Category="Vehicle|Suspension", DisplayName="Suspension Max Raise")
	float SuspensionMaxRaise = 0.15f;
	UPROPERTY(Edit, Save, Category="Vehicle|Suspension", DisplayName="Suspension Max Drop")
	float SuspensionMaxDrop = 0.25f;
	UPROPERTY(Edit, Save, Category="Vehicle|Suspension", DisplayName="Spring Rate")
	float SpringRate = 35000.0f;
	UPROPERTY(Edit, Save, Category="Vehicle|Suspension", DisplayName="Spring Damping")
	float SpringDamping = 4500.0f;

	// --- 조향 / 제동 ---
	UPROPERTY(Edit, Save, Category="Vehicle|Steering", DisplayName="Max Steer Angle")
	float MaxSteerAngle = 35.0f;
	UPROPERTY(Edit, Save, Category="Vehicle|Brake", DisplayName="Max Brake Torque")
	float MaxBrakeTorque = 4500.0f;
	UPROPERTY(Edit, Save, Category="Vehicle|Brake", DisplayName="Max Handbrake Torque")
	float MaxHandBrakeTorque = 6000.0f;

	// --- 엔진 / 드라이브트레인 ---
	UPROPERTY(Edit, Save, Category="Vehicle|Engine", DisplayName="Engine Peak Torque")
	float EnginePeakTorque = 500.0f;
	UPROPERTY(Edit, Save, Category="Vehicle|Engine", DisplayName="Engine Max Omega")
	float EngineMaxOmega = 600.0f;
	UPROPERTY(Edit, Save, Category="Vehicle|Engine", DisplayName="Clutch Strength")
	float ClutchStrength = 10.0f;
	UPROPERTY(Edit, Save, Category="Vehicle|Engine", DisplayName="Gear Switch Time")
	float GearSwitchTime = 0.5f;

	// --- 타이어 ---
	UPROPERTY(Edit, Save, Category="Vehicle|Tire", DisplayName="Tire Friction")
	float TireFriction = 1.5f;

	// --- 시각화 ---
	// 바퀴 메시 기본 회전 보정. 실린더 모델 축을 차축(Y)에 맞추려면 에디터에서 조정.
	UPROPERTY(Edit, Save, Category="Vehicle|Visual", DisplayName="Wheel Mesh Rotation Offset")
	FRotator WheelMeshRotationOffset = FRotator();
};
