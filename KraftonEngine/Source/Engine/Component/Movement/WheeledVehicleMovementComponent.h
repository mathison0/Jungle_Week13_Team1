#pragma once

#include "MovementComponent.h"
#include "Physics/Vehicle/VehiclePhysicsInterface.h"

#include <memory>

#include "Source/Engine/Component/Movement/WheeledVehicleMovementComponent.generated.h"

class IVehiclePhysics;
class UPrimitiveComponent;

UENUM()
enum class EVehicleInputKey : uint8
{
	None = 0,
	W,
	A,
	S,
	D,
	Space,
	Up,
	Down,
	Left,
	Right,
	Q,
	E
};

UCLASS()
class UWheeledVehicleMovementComponent : public UMovementComponent
{
public:
	GENERATED_BODY()

	UWheeledVehicleMovementComponent();
	~UWheeledVehicleMovementComponent() override;

	void BeginPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	void SetThrottleInput(float Value);
	void SetBrakeInput(float Value);
	void SetSteeringInput(float Value);
	void SetHandbrakeInput(float Value);
	void SetWheelColliderComponent(int32 WheelIndex, UPrimitiveComponent* WheelCollider);

private:
	void InitializeWheelDefaults();
	void EnsureVehiclePhysics();
	FVehiclePhysicsConfig BuildPhysicsConfig() const;
	UPrimitiveComponent* GetChassisComponent() const;
	void DrawWheelDebug() const;

private:
	FVehicleWheelConfig WheelSetups[VehicleWheelCount4W];
	UPrimitiveComponent* WheelColliderComponents[VehicleWheelCount4W] = {};
	std::unique_ptr<IVehiclePhysics> VehiclePhysics;

	UPROPERTY(Edit, Save, Category = "Vehicle|Input", DisplayName = "Throttle Input")
	float ThrottleInput = 0.0f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Input", DisplayName = "Brake Input")
	float BrakeInput = 0.0f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Input", DisplayName = "Steering Input")
	float SteeringInput = 0.0f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Input", DisplayName = "Handbrake Input")
	float HandbrakeInput = 0.0f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Input", DisplayName = "Use Keyboard Input")
	bool bUseKeyboardInput = true;

	UPROPERTY(Edit, Save, Category = "Vehicle|Input", DisplayName = "Throttle Key", Enum = EVehicleInputKey)
	EVehicleInputKey ThrottleKey = EVehicleInputKey::W;

	UPROPERTY(Edit, Save, Category = "Vehicle|Input", DisplayName = "Reverse Key", Enum = EVehicleInputKey)
	EVehicleInputKey ReverseKey = EVehicleInputKey::S;

	UPROPERTY(Edit, Save, Category = "Vehicle|Input", DisplayName = "Steer Left Key", Enum = EVehicleInputKey)
	EVehicleInputKey SteerLeftKey = EVehicleInputKey::A;

	UPROPERTY(Edit, Save, Category = "Vehicle|Input", DisplayName = "Steer Right Key", Enum = EVehicleInputKey)
	EVehicleInputKey SteerRightKey = EVehicleInputKey::D;

	UPROPERTY(Edit, Save, Category = "Vehicle|Input", DisplayName = "Brake Key", Enum = EVehicleInputKey)
	EVehicleInputKey BrakeKey = EVehicleInputKey::Space;

	UPROPERTY(Edit, Save, Category = "Vehicle|Debug", DisplayName = "Draw Wheel Collider Debug")
	bool bDrawWheelColliderDebug = true;

	UPROPERTY(Edit, Save, Category = "Vehicle|Wheel", DisplayName = "Wheel Radius")
	float WheelRadius = 0.35f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Wheel", DisplayName = "Wheel Width")
	float WheelWidth = 0.25f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Wheel", DisplayName = "Front Axle X")
	float FrontAxleX = 1.45f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Wheel", DisplayName = "Rear Axle X")
	float RearAxleX = -1.35f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Wheel", DisplayName = "Half Track Width")
	float HalfTrackWidth = 0.85f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Wheel", DisplayName = "Wheel Local Z")
	float WheelLocalZ = -0.45f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Suspension", DisplayName = "Suspension Max Raise")
	float SuspensionMaxRaise = 0.15f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Suspension", DisplayName = "Suspension Max Drop")
	float SuspensionMaxDrop = 0.25f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Suspension", DisplayName = "Spring Rate")
	float SpringRate = 35000.0f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Suspension", DisplayName = "Spring Damping")
	float SpringDamping = 4500.0f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Traction", DisplayName = "Max Steer Angle")
	float MaxSteerAngle = 35.0f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Traction", DisplayName = "Max Brake Torque")
	float MaxBrakeTorque = 4500.0f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Traction", DisplayName = "Max Drive Force")
	float MaxDriveForce = 8500.0f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Traction", DisplayName = "Tire Friction")
	float TireFriction = 1.6f;

	UPROPERTY(Edit, Save, Category = "Vehicle|Traction", DisplayName = "Tire Lateral Stiffness")
	float TireLateralStiffness = 12000.0f;
};
