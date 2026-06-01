#include "Component/Movement/WheeledVehicleMovementComponent.h"

#include "Component/PrimitiveComponent.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Input/InputSystem.h"
#include "Math/MathUtils.h"
#include "Physics/Vehicle/Vehicle4WPhysics.h"

namespace
{
	int32 ToVirtualKey(EVehicleInputKey Key)
	{
		switch (Key)
		{
		case EVehicleInputKey::W: return 'W';
		case EVehicleInputKey::A: return 'A';
		case EVehicleInputKey::S: return 'S';
		case EVehicleInputKey::D: return 'D';
		case EVehicleInputKey::Space: return 0x20;
		case EVehicleInputKey::Up: return 0x26;
		case EVehicleInputKey::Down: return 0x28;
		case EVehicleInputKey::Left: return 0x25;
		case EVehicleInputKey::Right: return 0x27;
		case EVehicleInputKey::Q: return 'Q';
		case EVehicleInputKey::E: return 'E';
		case EVehicleInputKey::None:
		default:
			return 0;
		}
	}

	bool IsVehicleKeyDown(InputSystem& Input, EVehicleInputKey Key)
	{
		const int32 KeyCode = ToVirtualKey(Key);
		return KeyCode > 0 && Input.GetKey(KeyCode);
	}
}

UWheeledVehicleMovementComponent::UWheeledVehicleMovementComponent()
{
	PrimaryComponentTick.SetTickGroup(TG_PrePhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PrePhysics);
	InitializeWheelDefaults();
}

UWheeledVehicleMovementComponent::~UWheeledVehicleMovementComponent() = default;

void UWheeledVehicleMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureVehiclePhysics();
}

void UWheeledVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	EnsureVehiclePhysics();
	if (!VehiclePhysics)
	{
		return;
	}

	if (bUseKeyboardInput)
	{
		InputSystem& Input = InputSystem::Get();
		const float Forward = IsVehicleKeyDown(Input, ThrottleKey) ? 1.0f : 0.0f;
		const float Reverse = IsVehicleKeyDown(Input, ReverseKey) ? 1.0f : 0.0f;
		const float Left = IsVehicleKeyDown(Input, SteerLeftKey) ? 1.0f : 0.0f;
		const float Right = IsVehicleKeyDown(Input, SteerRightKey) ? 1.0f : 0.0f;
		ThrottleInput = Forward - Reverse;
		SteeringInput = Right - Left;
		BrakeInput = IsVehicleKeyDown(Input, BrakeKey) ? 1.0f : 0.0f;
		HandbrakeInput = BrakeInput;
	}

	VehiclePhysics->SetThrottle(ThrottleInput);
	VehiclePhysics->SetBrake(BrakeInput);
	VehiclePhysics->SetSteer(SteeringInput);
	VehiclePhysics->SetHandbrake(HandbrakeInput);
	VehiclePhysics->TickVehicle(DeltaTime);

	if (bDrawWheelColliderDebug)
	{
		DrawWheelDebug();
	}
}

void UWheeledVehicleMovementComponent::SetThrottleInput(float Value)
{
	ThrottleInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetBrakeInput(float Value)
{
	BrakeInput = FMath::Clamp(Value, 0.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetSteeringInput(float Value)
{
	SteeringInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetHandbrakeInput(float Value)
{
	HandbrakeInput = FMath::Clamp(Value, 0.0f, 1.0f);
}

void UWheeledVehicleMovementComponent::SetWheelColliderComponent(int32 WheelIndex, UPrimitiveComponent* WheelCollider)
{
	if (WheelIndex < 0 || WheelIndex >= VehicleWheelCount4W)
	{
		return;
	}

	WheelColliderComponents[WheelIndex] = WheelCollider;
	if (VehiclePhysics)
	{
		VehiclePhysics->SetWheelCollider(WheelIndex, WheelCollider);
	}
}

void UWheeledVehicleMovementComponent::InitializeWheelDefaults()
{
	WheelSetups[0].BoneName = FName("Wheel_FL");
	WheelSetups[0].LocalLocation = FVector(FrontAxleX, -HalfTrackWidth, WheelLocalZ);
	WheelSetups[0].bAffectedBySteering = true;
	WheelSetups[0].bAffectedByEngine = true;

	WheelSetups[1].BoneName = FName("Wheel_FR");
	WheelSetups[1].LocalLocation = FVector(FrontAxleX, HalfTrackWidth, WheelLocalZ);
	WheelSetups[1].bAffectedBySteering = true;
	WheelSetups[1].bAffectedByEngine = true;

	WheelSetups[2].BoneName = FName("Wheel_RL");
	WheelSetups[2].LocalLocation = FVector(RearAxleX, -HalfTrackWidth, WheelLocalZ);
	WheelSetups[2].bAffectedByEngine = true;
	WheelSetups[2].bAffectedByHandbrake = true;

	WheelSetups[3].BoneName = FName("Wheel_RR");
	WheelSetups[3].LocalLocation = FVector(RearAxleX, HalfTrackWidth, WheelLocalZ);
	WheelSetups[3].bAffectedByEngine = true;
	WheelSetups[3].bAffectedByHandbrake = true;
}

void UWheeledVehicleMovementComponent::EnsureVehiclePhysics()
{
	if (VehiclePhysics)
	{
		return;
	}

	UPrimitiveComponent* ChassisComponent = GetChassisComponent();
	UWorld* World = GetWorld();
	if (!World || !ChassisComponent)
	{
		return;
	}

	VehiclePhysics = std::make_unique<FPhysXVehicle4WPhysics>();
	VehiclePhysics->Initialize(World, ChassisComponent, BuildPhysicsConfig());
	for (int32 WheelIndex = 0; WheelIndex < VehicleWheelCount4W; ++WheelIndex)
	{
		VehiclePhysics->SetWheelCollider(WheelIndex, WheelColliderComponents[WheelIndex]);
	}
}

FVehiclePhysicsConfig UWheeledVehicleMovementComponent::BuildPhysicsConfig() const
{
	FVehiclePhysicsConfig Config;
	for (int32 WheelIndex = 0; WheelIndex < VehicleWheelCount4W; ++WheelIndex)
	{
		Config.Wheels[WheelIndex] = WheelSetups[WheelIndex];
		const bool bFrontWheel = WheelIndex < 2;
		const bool bRightWheel = WheelIndex == 1 || WheelIndex == 3;
		Config.Wheels[WheelIndex].LocalLocation = FVector(
			bFrontWheel ? FrontAxleX : RearAxleX,
			bRightWheel ? HalfTrackWidth : -HalfTrackWidth,
			WheelLocalZ);
		Config.Wheels[WheelIndex].WheelRadius = WheelRadius;
		Config.Wheels[WheelIndex].WheelWidth = WheelWidth;
		Config.Wheels[WheelIndex].SuspensionMaxRaise = SuspensionMaxRaise;
		Config.Wheels[WheelIndex].SuspensionMaxDrop = SuspensionMaxDrop;
		Config.Wheels[WheelIndex].SpringRate = SpringRate;
		Config.Wheels[WheelIndex].SpringDamping = SpringDamping;
		Config.Wheels[WheelIndex].MaxSteerAngle = MaxSteerAngle;
		Config.Wheels[WheelIndex].MaxBrakeTorque = MaxBrakeTorque;
		Config.WheelColliders[WheelIndex] = WheelColliderComponents[WheelIndex];
	}

	Config.MaxDriveForce = MaxDriveForce;
	Config.TireFriction = TireFriction;
	Config.TireLateralStiffness = TireLateralStiffness;
	return Config;
}

UPrimitiveComponent* UWheeledVehicleMovementComponent::GetChassisComponent() const
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

void UWheeledVehicleMovementComponent::DrawWheelDebug() const
{
	if (!VehiclePhysics)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (int32 WheelIndex = 0; WheelIndex < VehiclePhysics->GetWheelCount(); ++WheelIndex)
	{
		const FVehicleWheelState& State = VehiclePhysics->GetWheelState(WheelIndex);
		const FColor TraceColor = State.bGrounded ? FColor::Green() : FColor::Red();
		DrawDebugLine(World, State.TraceStart, State.TraceEnd, TraceColor, 0.0f);
		DrawDebugSphere(World, State.WheelCenter, WheelRadius, 16, TraceColor, 0.0f);

		if (State.bGrounded)
		{
			DrawDebugPoint(World, State.ContactPoint, 0.08f, FColor::Yellow(), 0.0f);
			DrawDebugLine(World, State.ContactPoint, State.ContactPoint + State.ContactNormal * 0.35f, FColor::Blue(), 0.0f);
			DrawDebugLine(World, State.ContactPoint, State.ContactPoint + State.SuspensionForce * 0.00005f, FColor(0, 180, 255), 0.0f);
			DrawDebugLine(World, State.ContactPoint, State.ContactPoint + State.TireForce * 0.00005f, FColor(255, 120, 0), 0.0f);
		}
	}
}
