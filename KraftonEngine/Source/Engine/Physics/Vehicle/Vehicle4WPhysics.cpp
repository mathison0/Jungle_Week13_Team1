#include "Physics/Vehicle/Vehicle4WPhysics.h"

#include "Component/PrimitiveComponent.h"
#include "GameFramework/World.h"
#include "Math/MathUtils.h"

#include <algorithm>
#include <cmath>

namespace
{
	float SignNonZero(float Value)
	{
		return Value < 0.0f ? -1.0f : 1.0f;
	}

	FVector SafeNormal(const FVector& Value, const FVector& Fallback)
	{
		const float SizeSq = Value.LengthSquared();
		if (SizeSq <= 1.0e-8f)
		{
			return Fallback;
		}
		return Value / std::sqrt(SizeSq);
	}

	FVector ClampVectorMagnitude(const FVector& Value, float MaxMagnitude)
	{
		if (MaxMagnitude <= 0.0f)
		{
			return FVector::ZeroVector;
		}

		const float SizeSq = Value.LengthSquared();
		const float MaxSq = MaxMagnitude * MaxMagnitude;
		if (SizeSq <= MaxSq || SizeSq <= 1.0e-8f)
		{
			return Value;
		}

		return Value * (MaxMagnitude / std::sqrt(SizeSq));
	}
}

FPhysXVehicle4WPhysics::FPhysXVehicle4WPhysics() = default;

FPhysXVehicle4WPhysics::~FPhysXVehicle4WPhysics()
{
	DestroyVehicle();
}

void FPhysXVehicle4WPhysics::Initialize(UWorld* InWorld, UPrimitiveComponent* InChassisComponent, const FVehiclePhysicsConfig& InConfig)
{
	World = InWorld;
	ChassisComponent = InChassisComponent;
	Config = InConfig;
	for (int32 WheelIndex = 0; WheelIndex < VehicleWheelCount4W; ++WheelIndex)
	{
		WheelColliders[WheelIndex] = Config.WheelColliders[WheelIndex];
	}
	ResetRuntimeState();
}

void FPhysXVehicle4WPhysics::DestroyVehicle()
{
	World = nullptr;
	ChassisComponent = nullptr;
	for (int32 WheelIndex = 0; WheelIndex < VehicleWheelCount4W; ++WheelIndex)
	{
		WheelColliders[WheelIndex] = nullptr;
	}
	ResetRuntimeState();
}

void FPhysXVehicle4WPhysics::SetThrottle(float Value)
{
	ThrottleInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void FPhysXVehicle4WPhysics::SetBrake(float Value)
{
	BrakeInput = FMath::Clamp(Value, 0.0f, 1.0f);
}

void FPhysXVehicle4WPhysics::SetSteer(float Value)
{
	SteerInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void FPhysXVehicle4WPhysics::SetHandbrake(float Value)
{
	HandbrakeInput = FMath::Clamp(Value, 0.0f, 1.0f);
}

void FPhysXVehicle4WPhysics::SetWheelCollider(int32 WheelIndex, UPrimitiveComponent* WheelCollider)
{
	if (WheelIndex < 0 || WheelIndex >= VehicleWheelCount4W)
	{
		return;
	}

	WheelColliders[WheelIndex] = WheelCollider;
	Config.WheelColliders[WheelIndex] = WheelCollider;
}

void FPhysXVehicle4WPhysics::TickVehicle(float DeltaTime)
{
	if (!World || !ChassisComponent || DeltaTime <= 0.0f)
	{
		return;
	}

	for (int32 WheelIndex = 0; WheelIndex < VehicleWheelCount4W; ++WheelIndex)
	{
		SimulateWheel(WheelIndex, DeltaTime);
	}
}

const FVehicleWheelState& FPhysXVehicle4WPhysics::GetWheelState(int32 WheelIndex) const
{
	static FVehicleWheelState InvalidState;
	if (WheelIndex < 0 || WheelIndex >= VehicleWheelCount4W)
	{
		return InvalidState;
	}
	return WheelStates[WheelIndex];
}

FVector FPhysXVehicle4WPhysics::TransformLocalToWorld(const FVector& Local) const
{
	if (!ChassisComponent)
	{
		return Local;
	}

	const FVector Origin = ChassisComponent->GetWorldLocation();
	const FVector Forward = SafeNormal(ChassisComponent->GetForwardVector(), FVector::ForwardVector);
	const FVector Right = SafeNormal(ChassisComponent->GetRightVector(), FVector::RightVector);
	const FVector Up = SafeNormal(ChassisComponent->GetUpVector(), FVector::UpVector);
	return Origin + Forward * Local.X + Right * Local.Y + Up * Local.Z;
}

FVector FPhysXVehicle4WPhysics::RotateAroundUp(const FVector& Vector, float AngleRadians) const
{
	const FVector Up = SafeNormal(ChassisComponent ? ChassisComponent->GetUpVector() : FVector::UpVector, FVector::UpVector);
	const float CosA = std::cos(AngleRadians);
	const float SinA = std::sin(AngleRadians);
	return Vector * CosA + Up.Cross(Vector) * SinA + Up * (Up.Dot(Vector) * (1.0f - CosA));
}

void FPhysXVehicle4WPhysics::SimulateWheel(int32 WheelIndex, float DeltaTime)
{
	const FVehicleWheelConfig& Wheel = Config.Wheels[WheelIndex];
	FVehicleWheelState& State = WheelStates[WheelIndex];

	const FVector Up = SafeNormal(ChassisComponent->GetUpVector(), FVector::UpVector);
	const FVector Down = Up * -1.0f;
	const FVector ChassisLocation = ChassisComponent->GetWorldLocation();
	const FVector WheelMount = TransformLocalToWorld(Wheel.LocalLocation);
	const float SuspensionTravel = (std::max)(0.01f, Wheel.SuspensionMaxRaise + Wheel.SuspensionMaxDrop);
	const float TraceDistance = SuspensionTravel + Wheel.WheelRadius;

	State.TraceStart = WheelMount + Up * Wheel.SuspensionMaxRaise;
	State.TraceEnd = State.TraceStart + Down * TraceDistance;
	State.bGrounded = false;
	State.ContactNormal = Up;
	State.SuspensionForce = FVector::ZeroVector;
	State.TireForce = FVector::ZeroVector;
	State.Compression = 0.0f;

	FHitResult Hit;
	const bool bHit = World->PhysicsSphereSweepShapeComponents(
		State.TraceStart,
		Down,
		TraceDistance,
		Wheel.WheelRadius,
		Hit,
		Config.GroundTraceChannel,
		ChassisComponent->GetOwner());

	if (!bHit)
	{
		PreviousCompression[WheelIndex] = 0.0f;
		State.WheelCenter = WheelMount - Up * Wheel.SuspensionMaxDrop;
		State.ContactPoint = State.WheelCenter - Up * Wheel.WheelRadius;
		State.RotationAngle += State.AngularVelocity * DeltaTime;
		if (WheelColliders[WheelIndex])
		{
			WheelColliders[WheelIndex]->SetWorldLocation(State.WheelCenter);
		}
		return;
	}

	State.bGrounded = true;
	State.WheelCenter = State.TraceStart + Down * Hit.Distance;
	State.ContactPoint = Hit.WorldHitLocation;
	State.ContactNormal = SafeNormal(Hit.ImpactNormal, Up);

	const float CompressionDistance = FMath::Clamp(SuspensionTravel - Hit.Distance, 0.0f, SuspensionTravel);
	const float CompressionVelocity = (CompressionDistance - PreviousCompression[WheelIndex]) / DeltaTime;
	PreviousCompression[WheelIndex] = CompressionDistance;
	State.Compression = CompressionDistance / SuspensionTravel;

	const float SuspensionForceMagnitude =
		(std::max)(0.0f, CompressionDistance * Wheel.SpringRate + CompressionVelocity * Wheel.SpringDamping);
	State.SuspensionForce = Up * SuspensionForceMagnitude;

	const FVector LinearVelocity = ChassisComponent->GetLinearVelocity();
	const FVector AngularVelocity = ChassisComponent->GetAngularVelocity();
	const FVector ContactOffset = State.ContactPoint - ChassisLocation;
	const FVector ContactVelocity = LinearVelocity + AngularVelocity.Cross(ContactOffset);

	const FVector BaseForward = SafeNormal(ChassisComponent->GetForwardVector(), FVector::ForwardVector);
	const float MaxSteerRadians = Wheel.MaxSteerAngle * FMath::DegToRad;
	State.SteerAngle = Wheel.bAffectedBySteering ? SteerInput * MaxSteerRadians : 0.0f;

	const FVector WheelForward = SafeNormal(RotateAroundUp(BaseForward, State.SteerAngle), BaseForward);
	const FVector WheelRight = SafeNormal(Up.Cross(WheelForward), ChassisComponent->GetRightVector());
	const float LongitudinalSpeed = ContactVelocity.Dot(WheelForward);
	const float LateralSpeed = ContactVelocity.Dot(WheelRight);

	const int32 DrivenWheelCount =
		(Config.Wheels[0].bAffectedByEngine ? 1 : 0) +
		(Config.Wheels[1].bAffectedByEngine ? 1 : 0) +
		(Config.Wheels[2].bAffectedByEngine ? 1 : 0) +
		(Config.Wheels[3].bAffectedByEngine ? 1 : 0);
	const float DriveForceMagnitude = Wheel.bAffectedByEngine && DrivenWheelCount > 0
		? (ThrottleInput * Config.MaxDriveForce) / static_cast<float>(DrivenWheelCount)
		: 0.0f;

	const float BrakeAlpha = (std::max)(BrakeInput, Wheel.bAffectedByHandbrake ? HandbrakeInput : 0.0f);
	const float BrakeForceMagnitude = BrakeAlpha * Wheel.MaxBrakeTorque / (std::max)(0.01f, Wheel.WheelRadius);
	const float BrakeDirection = std::abs(LongitudinalSpeed) > 0.05f ? -SignNonZero(LongitudinalSpeed) : 0.0f;
	const FVector BrakeForce = WheelForward * (BrakeForceMagnitude * BrakeDirection);

	const FVector DriveForce = WheelForward * DriveForceMagnitude;
	const FVector RollingForce = WheelForward * (-LongitudinalSpeed * Config.RollingResistance);
	const FVector LateralForce = WheelRight * (-LateralSpeed * Config.TireLateralStiffness);

	const float MaxTireForce = SuspensionForceMagnitude * Config.TireFriction;
	const FVector TireForce = ClampVectorMagnitude(DriveForce + BrakeForce + RollingForce + LateralForce, MaxTireForce);
	State.TireForce = TireForce;

	ChassisComponent->AddForceAtLocation(State.SuspensionForce + State.TireForce, State.ContactPoint);

	State.AngularVelocity = LongitudinalSpeed / (std::max)(0.01f, Wheel.WheelRadius);
	State.RotationAngle += State.AngularVelocity * DeltaTime;

	if (WheelColliders[WheelIndex])
	{
		WheelColliders[WheelIndex]->SetWorldLocation(State.WheelCenter);
	}
}

void FPhysXVehicle4WPhysics::ResetRuntimeState()
{
	for (int32 WheelIndex = 0; WheelIndex < VehicleWheelCount4W; ++WheelIndex)
	{
		WheelStates[WheelIndex] = FVehicleWheelState();
		PreviousCompression[WheelIndex] = 0.0f;
	}
}
