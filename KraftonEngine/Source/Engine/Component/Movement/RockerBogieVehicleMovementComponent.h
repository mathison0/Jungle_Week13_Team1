#pragma once

#include "MovementComponent.h"
#include "Math/Rotator.h"

#include <memory>

#include "Source/Engine/Component/Movement/RockerBogieVehicleMovementComponent.generated.h"

class FPhysXRockerBogieVehicle;
class UPrimitiveComponent;
class USceneComponent;
class UWorld;
struct FColor;
struct FQuat;
struct FVector;

UCLASS()
class URockerBogieVehicleMovementComponent : public UMovementComponent
{
public:
	GENERATED_BODY()

	URockerBogieVehicleMovementComponent();
	~URockerBogieVehicleMovementComponent() override;

	void EndPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	void SetWheelVisualComponent(int32 WheelIndex, USceneComponent* Visual);
	void SetLinkVisualComponent(int32 LinkIndex, USceneComponent* Visual);

private:
	void EnsureVehicle();
	void DestroyVehicle();
	void UpdateVisuals();
	void DrawDebugGeometry() const;
	void DrawWheelRadius(UWorld* World, const FVector& Center, const FQuat& Rotation, const FColor& Color) const;
	UPrimitiveComponent* GetChassisComponent() const;

	std::unique_ptr<FPhysXRockerBogieVehicle> Vehicle;

	UPROPERTY(Save, Category="Vehicle|Visual")
	TArray<USceneComponent*> WheelVisuals;
	UPROPERTY(Save, Category="Vehicle|Visual")
	TArray<USceneComponent*> LinkVisuals;

	UPROPERTY(Edit, Save, Category="Vehicle|Input", DisplayName="Use Keyboard Input")
	bool bUseKeyboardInput = true;

	UPROPERTY(Edit, Save, Category="RockerBogie|Wheel", DisplayName="Wheel Radius")
	float WheelRadius = 0.28f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Wheel", DisplayName="Wheel Half Width")
	float WheelHalfWidth = 0.10f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Wheel", DisplayName="Wheel Mass")
	float WheelMass = 18.0f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Wheel", DisplayName="Wheel Local Z")
	float WheelLocalZ = -0.55f;

	UPROPERTY(Edit, Save, Category="RockerBogie|Layout", DisplayName="Half Track Width")
	float HalfTrackWidth = 0.85f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Layout", DisplayName="Rocker Pivot X")
	float RockerPivotX = 0.0f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Layout", DisplayName="Rocker Pivot Z")
	float RockerPivotZ = -0.20f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Layout", DisplayName="Rocker Front Length")
	float RockerFrontLength = 0.90f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Layout", DisplayName="Rocker Rear Length")
	float RockerRearLength = 0.70f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Layout", DisplayName="Bogie Half Length")
	float BogieHalfLength = 0.45f;

	UPROPERTY(Edit, Save, Category="RockerBogie|Drive", DisplayName="Max Drive Torque")
	float MaxDriveTorque = 850.0f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Drive", DisplayName="Max Drive Speed")
	float MaxDriveSpeed = 18.0f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Drive", DisplayName="Wheel Contact Probe Extra")
	float WheelContactProbeExtra = 0.18f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Drive", DisplayName="Wheel Slip Speed")
	float WheelSlipSpeed = 3.0f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Drive", DisplayName="Min Grounded Torque Scale")
	float MinGroundedTorqueScale = 0.15f;

	UPROPERTY(Edit, Save, Category="RockerBogie|Stability", DisplayName="Chassis Angular Damping")
	float ChassisAngularDamping = 1.8f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Stability", DisplayName="Chassis Pitch Stiffness")
	float ChassisPitchStiffness = 900.0f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Stability", DisplayName="Chassis Pitch Damping")
	float ChassisPitchDamping = 180.0f;
	UPROPERTY(Edit, Save, Category="RockerBogie|Stability", DisplayName="Max Chassis Pitch Torque")
	float MaxChassisPitchTorque = 2200.0f;

	UPROPERTY(Edit, Save, Category="RockerBogie|Visual", DisplayName="Wheel Mesh Rotation Offset")
	FRotator WheelMeshRotationOffset = FRotator();

	UPROPERTY(Edit, Save, Category="RockerBogie|Debug", DisplayName="Draw Debug Geometry")
	bool bDrawDebugGeometry = true;
	UPROPERTY(Edit, Save, Category="RockerBogie|Debug", DisplayName="Debug Draw Duration")
	float DebugDrawDuration = 0.0f;
};
