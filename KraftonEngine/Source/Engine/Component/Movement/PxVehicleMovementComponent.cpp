#include "Component/Movement/PxVehicleMovementComponent.h"

#include "Physics/PhysX/Vehicle/PhysXVehicle4W.h"
#include "Physics/PhysX/Vehicle/PhysXVehicleConfig.h"
#include "Physics/PhysX/PhysXPhysicsScene.h"
#include "Physics/PhysX/PhysXHelper.h"
#include "Core/ProjectSettings.h"
#include "Component/PrimitiveComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"
#include "Input/InputSystem.h"

#include <algorithm>

using namespace physx;

namespace
{
	// 현재 월드의 PhysX 씬. 백엔드가 PhysX가 아니면 nullptr.
	// (호출 측은 IPhysicsScene을 FPhysXPhysicsScene으로 다운캐스트 + 백엔드 가드 후 사용)
	FPhysXPhysicsScene* GetPhysXScene(UWorld* World)
	{
		if (!World) return nullptr;
		if (FProjectSettings::Get().Physics.Backend != EPhysicsBackend::PhysX) return nullptr;
		return static_cast<FPhysXPhysicsScene*>(World->GetPhysicsScene());
	}
}

UPxVehicleMovementComponent::UPxVehicleMovementComponent()
{
	// 입력은 물리 시뮬레이션 전에 준비돼야 하므로 PrePhysics 그룹.
	PrimaryComponentTick.SetTickGroup(TG_PrePhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PrePhysics);

	// 4륜 고정 슬롯. 액터가 인덱스로 꽂고, 직렬화/로드 시에도 4칸으로 라운드트립.
	WheelVisuals.resize(4);
}

UPxVehicleMovementComponent::~UPxVehicleMovementComponent() = default;

void UPxVehicleMovementComponent::EndPlay()
{
	DestroyVehicle();
	Super::EndPlay();
}

void UPxVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	EnsureVehicle();
	if (!Vehicle)
	{
		return;
	}

	if (bUseKeyboardInput)
	{
		InputSystem& Input = InputSystem::Get();
		Vehicle->SetThrottle(Input.GetKey('W'));   // 전진
		Vehicle->SetReverse(Input.GetKey('S'));    // 후진
		Vehicle->SetBrake(Input.GetKey(0x20));     // Space = 브레이크
		Vehicle->SetSteerLeft(Input.GetKey('A'));
		Vehicle->SetSteerRight(Input.GetKey('D'));
	}

	// 마우스 룩: 차량 pawn 은 ACharacter 가 아니라 자동 mouse look 이 없으므로 여기서 직접
	// ControlRotation 을 누적한다. SpringArm 이 bUsePawnControlRotation 으로 이 값을 읽어
	// 카메라만 회전시킨다(차체는 물리로만 움직임). pitch 는 상하 한도로 클램프.
	if (bUseMouseLook)
	{
		if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
		{
			const InputSystem& Input = InputSystem::Get();
			const int DX = Input.MouseDeltaX();
			const int DY = Input.MouseDeltaY();
			if (DX != 0 || DY != 0)
			{
				FRotator Rot = OwnerPawn->GetControlRotation();
				Rot.Yaw   += static_cast<float>(DX) * MouseSensitivity;
				Rot.Pitch += static_cast<float>(DY) * MouseSensitivity;
				Rot.Pitch  = std::clamp(Rot.Pitch, MinCameraPitch, MaxCameraPitch);
				OwnerPawn->SetControlRotation(Rot);
			}
		}
	}

	// 직전 프레임 Simulate 결과(바퀴 자세)를 시각 컴포넌트에 반영.
	UpdateWheelVisuals();
}

void UPxVehicleMovementComponent::SetWheelVisualComponent(int32 WheelIndex, USceneComponent* Visual)
{
	if (WheelIndex >= 0 && WheelIndex < static_cast<int32>(WheelVisuals.size()))
	{
		WheelVisuals[WheelIndex] = Visual;
	}
}

void UPxVehicleMovementComponent::EnsureVehicle()
{
	if (Vehicle)
	{
		return;
	}

	FPhysXPhysicsScene* PxScene = GetPhysXScene(GetWorld());
	if (!PxScene)
	{
		return;
	}

	UPrimitiveComponent* Chassis = GetChassisComponent();
	if (!Chassis)
	{
		return;
	}

	// 섀시가 simulate되는 PxRigidDynamic으로 등록돼 있어야 차를 붙일 수 있다.
	// 등록 타이밍 때문에 BeginPlay 시점엔 아직 없을 수 있어 매 tick 재시도.
	PxRigidActor* RigidActor = PxScene->GetComponentRigidActor(Chassis);
	PxRigidDynamic* ChassisDynamic = RigidActor ? RigidActor->is<PxRigidDynamic>() : nullptr;
	if (!ChassisDynamic)
	{
		return;
	}

	auto NewVehicle = std::make_unique<FPhysXVehicle4W>();
	if (!NewVehicle->Build(PxScene->GetPxScene(), PxScene->GetPhysics(), ChassisDynamic,
		PxScene->GetDefaultMaterial(), BuildSetup()))
	{
		return;
	}

	Vehicle = std::move(NewVehicle);
	PxScene->RegisterVehicle(Vehicle.get());
}

void UPxVehicleMovementComponent::DestroyVehicle()
{
	if (!Vehicle)
	{
		return;
	}

	if (FPhysXPhysicsScene* PxScene = GetPhysXScene(GetWorld()))
	{
		PxScene->UnregisterVehicle(Vehicle.get());
	}

	Vehicle->Release();
	Vehicle.reset();
}

void UPxVehicleMovementComponent::UpdateWheelVisuals()
{
	if (!Vehicle)
	{
		return;
	}

	const FQuat BaseQuat = WheelMeshRotationOffset.ToQuaternion();
	const int32 WheelCount = static_cast<int32>(WheelVisuals.size());
	for (int32 i = 0; i < 4 && i < WheelCount; ++i)
	{
		if (!WheelVisuals[i])
		{
			continue;
		}

		PxTransform Pose;
		if (Vehicle->GetWheelLocalPose(static_cast<uint32>(i), Pose))
		{
			const FTransform Local = FPhysXHelper::ToFTransform(Pose);
			WheelVisuals[i]->SetRelativeLocation(Local.Location);
			WheelVisuals[i]->SetRelativeRotation(Local.Rotation * BaseQuat);
		}
	}
}

FPxVehicleSetup UPxVehicleMovementComponent::BuildSetup() const
{
	FPxVehicleSetup Setup;

	Setup.WheelLocalLocation[0] = FVector(FrontAxleX, -HalfTrackWidth, WheelLocalZ); // FL
	Setup.WheelLocalLocation[1] = FVector(FrontAxleX,  HalfTrackWidth, WheelLocalZ); // FR
	Setup.WheelLocalLocation[2] = FVector(RearAxleX,  -HalfTrackWidth, WheelLocalZ); // RL
	Setup.WheelLocalLocation[3] = FVector(RearAxleX,   HalfTrackWidth, WheelLocalZ); // RR

	// 섀시 질량은 실제 강체 값을 따른다(액터가 박스에 설정한 값과 어긋나지 않게).
	if (UPrimitiveComponent* Chassis = GetChassisComponent())
	{
		const float Mass = Chassis->GetMass();
		if (Mass > 0.0f)
		{
			Setup.ChassisMass = Mass;
		}
	}

	Setup.WheelRadius = WheelRadius;
	Setup.WheelWidth = WheelWidth;
	Setup.SpringStrength = SpringRate;
	Setup.SpringDamperRate = SpringDamping;
	Setup.SuspensionMaxRaise = SuspensionMaxRaise;
	Setup.SuspensionMaxDrop = SuspensionMaxDrop;
	Setup.TireFriction = TireFriction;
	Setup.MaxSteerAngleDeg = MaxSteerAngle;
	Setup.MaxBrakeTorque = MaxBrakeTorque;
	Setup.MaxHandBrakeTorque = MaxHandBrakeTorque;
	Setup.EnginePeakTorque = EnginePeakTorque;
	Setup.EngineMaxOmega = EngineMaxOmega;
	Setup.ClutchStrength = ClutchStrength;
	Setup.GearSwitchTime = GearSwitchTime;
	Setup.TrackWidth = 2.0f * HalfTrackWidth;
	Setup.WheelBase = FrontAxleX - RearAxleX;

	return Setup;
}

UPrimitiveComponent* UPxVehicleMovementComponent::GetChassisComponent() const
{
	if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(GetUpdatedComponent()))
	{
		return Primitive;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	return Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent());
}
