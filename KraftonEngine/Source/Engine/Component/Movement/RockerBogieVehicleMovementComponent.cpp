#include "Component/Movement/RockerBogieVehicleMovementComponent.h"

#include "Physics/PhysX/Vehicle/PhysXRockerBogieVehicle.h"
#include "Physics/PhysX/PhysXPhysicsScene.h"
#include "Physics/PhysX/PhysXHelper.h"
#include "Core/ProjectSettings.h"
#include "Component/PrimitiveComponent.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Input/InputSystem.h"

#include <cmath>

using namespace physx;

namespace
{
	FPhysXPhysicsScene* GetPhysXScene(UWorld* World)
	{
		if (!World) return nullptr;
		if (FProjectSettings::Get().Physics.Backend != EPhysicsBackend::PhysX) return nullptr;
		return static_cast<FPhysXPhysicsScene*>(World->GetPhysicsScene());
	}

	uint32 GetWheelIndex(uint32 SideIndex, uint32 LocalWheelIndex)
	{
		return LocalWheelIndex * 2 + SideIndex;
	}
}

URockerBogieVehicleMovementComponent::URockerBogieVehicleMovementComponent()
{
	PrimaryComponentTick.SetTickGroup(TG_PrePhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PrePhysics);

	WheelVisuals.resize(FPhysXRockerBogieVehicle::WheelCount);
	LinkVisuals.resize(FPhysXRockerBogieVehicle::LinkCount);
}

URockerBogieVehicleMovementComponent::~URockerBogieVehicleMovementComponent() = default;

void URockerBogieVehicleMovementComponent::EndPlay()
{
	DestroyVehicle();
	Super::EndPlay();
}

void URockerBogieVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
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
		const float Throttle = (Input.GetKey('W') ? 1.0f : 0.0f) - (Input.GetKey('S') ? 1.0f : 0.0f);
		const float Steer = (Input.GetKey('D') ? 1.0f : 0.0f) - (Input.GetKey('A') ? 1.0f : 0.0f);
		Vehicle->SetInput(Throttle, Steer);
	}

	UpdateVisuals();
	if (bDrawDebugGeometry)
	{
		DrawDebugGeometry();
	}
}

void URockerBogieVehicleMovementComponent::SetWheelVisualComponent(int32 WheelIndex, USceneComponent* Visual)
{
	if (WheelIndex >= 0 && WheelIndex < static_cast<int32>(WheelVisuals.size()))
	{
		WheelVisuals[WheelIndex] = Visual;
	}
}

void URockerBogieVehicleMovementComponent::SetLinkVisualComponent(int32 LinkIndex, USceneComponent* Visual)
{
	if (LinkIndex >= 0 && LinkIndex < static_cast<int32>(LinkVisuals.size()))
	{
		LinkVisuals[LinkIndex] = Visual;
	}
}

void URockerBogieVehicleMovementComponent::EnsureVehicle()
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

	PxRigidActor* RigidActor = PxScene->GetComponentRigidActor(Chassis);
	PxRigidDynamic* ChassisDynamic = RigidActor ? RigidActor->is<PxRigidDynamic>() : nullptr;
	if (!ChassisDynamic)
	{
		return;
	}

	FPhysXRockerBogieSetup Setup;
	Setup.WheelRadius = WheelRadius;
	Setup.WheelHalfWidth = WheelHalfWidth;
	Setup.WheelMass = WheelMass;
	Setup.HalfTrackWidth = HalfTrackWidth;
	Setup.WheelLocalZ = WheelLocalZ;
	Setup.RockerPivotX = RockerPivotX;
	Setup.RockerPivotZ = RockerPivotZ;
	Setup.RockerFrontLength = RockerFrontLength;
	Setup.RockerRearLength = RockerRearLength;
	Setup.BogieHalfLength = BogieHalfLength;
	Setup.MaxDriveTorque = MaxDriveTorque;
	Setup.MaxDriveSpeed = MaxDriveSpeed;
	Setup.WheelContactProbeExtra = WheelContactProbeExtra;
	Setup.WheelSlipSpeed = WheelSlipSpeed;
	Setup.MinGroundedTorqueScale = MinGroundedTorqueScale;
	Setup.ChassisAngularDamping = ChassisAngularDamping;
	Setup.ChassisPitchStiffness = ChassisPitchStiffness;
	Setup.ChassisPitchDamping = ChassisPitchDamping;
	Setup.MaxChassisPitchTorque = MaxChassisPitchTorque;

	auto NewVehicle = std::make_unique<FPhysXRockerBogieVehicle>();
	if (!NewVehicle->Build(PxScene->GetPxScene(), PxScene->GetPhysics(), ChassisDynamic, Setup))
	{
		return;
	}

	Vehicle = std::move(NewVehicle);
	PxScene->RegisterRockerBogieVehicle(Vehicle.get());
}

void URockerBogieVehicleMovementComponent::DestroyVehicle()
{
	if (!Vehicle)
	{
		return;
	}

	if (FPhysXPhysicsScene* PxScene = GetPhysXScene(GetWorld()))
	{
		PxScene->UnregisterRockerBogieVehicle(Vehicle.get());
	}

	Vehicle->Release();
	Vehicle.reset();
}

void URockerBogieVehicleMovementComponent::UpdateVisuals()
{
	if (!Vehicle)
	{
		return;
	}

	const FQuat WheelOffset = WheelMeshRotationOffset.ToQuaternion();
	for (int32 Index = 0; Index < static_cast<int32>(WheelVisuals.size()); ++Index)
	{
		USceneComponent* Visual = WheelVisuals[Index];
		if (!Visual)
		{
			continue;
		}

		PxTransform Pose;
		if (Vehicle->GetWheelLocalPose(static_cast<uint32>(Index), Pose))
		{
			const FTransform Local = FPhysXHelper::ToFTransform(Pose);
			Visual->SetRelativeLocation(Local.Location);
			Visual->SetRelativeRotation(Local.Rotation * WheelOffset);
		}
	}

	for (int32 Index = 0; Index < static_cast<int32>(LinkVisuals.size()); ++Index)
	{
		USceneComponent* Visual = LinkVisuals[Index];
		if (!Visual)
		{
			continue;
		}

		PxTransform Pose;
		if (Vehicle->GetLinkLocalPose(static_cast<uint32>(Index), Pose))
		{
			const FTransform Local = FPhysXHelper::ToFTransform(Pose);
			Visual->SetRelativeLocation(Local.Location);
			Visual->SetRelativeRotation(Local.Rotation);
		}
	}
}

void URockerBogieVehicleMovementComponent::DrawDebugGeometry() const
{
	if (!Vehicle)
	{
		return;
	}

	UWorld* World = GetWorld();
	UPrimitiveComponent* Chassis = GetChassisComponent();
	if (!World || !Chassis)
	{
		return;
	}

	FPhysXRockerBogieDebugGeometry Geometry;
	if (!Vehicle->GetDebugGeometry(Geometry))
	{
		return;
	}

	const FQuat ChassisRotation = Chassis->GetWorldMatrix().ToQuat();
	const FColor WheelColor(150, 85, 85);
	const FColor AxleColor(70, 150, 255);
	const FColor RadiusColor(215, 120, 120);
	const FColor RockerColor(255, 225, 40);
	const FColor BogieColor(255, 150, 40);
	const FColor PivotColor(255, 255, 255);

	for (uint32 WheelIndex = 0; WheelIndex < FPhysXRockerBogieVehicle::WheelCount; ++WheelIndex)
	{
		const FVector Center = FPhysXHelper::ToFVector(Geometry.WheelCenters[WheelIndex]);
		FQuat WheelRotation = ChassisRotation;

		PxTransform LocalPose;
		if (Vehicle->GetWheelLocalPose(WheelIndex, LocalPose))
		{
			WheelRotation = ChassisRotation * FPhysXHelper::ToFQuat(LocalPose.q);
		}

		DrawWheelRadius(World, Center, WheelRotation, WheelColor);

		const FVector Axle = WheelRotation.GetRightVector();
		const float AxleHalfLength = WheelHalfWidth + 0.08f;
		DrawDebugLine(World, Center - Axle * AxleHalfLength, Center + Axle * AxleHalfLength, AxleColor, DebugDrawDuration);
		DrawDebugLine(World, Center, Center - FVector::UpVector * WheelRadius, RadiusColor, DebugDrawDuration);
		DrawDebugPoint(World, Center, 0.045f, PivotColor, DebugDrawDuration);
	}

	for (uint32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		const FVector RockerPivot = FPhysXHelper::ToFVector(Geometry.RockerPivots[SideIndex]);
		const FVector BogiePivot = FPhysXHelper::ToFVector(Geometry.BogiePivots[SideIndex]);
		const FVector FrontWheelPivot = FPhysXHelper::ToFVector(Geometry.WheelPivots[GetWheelIndex(SideIndex, 0)]);
		const FVector MiddleWheelPivot = FPhysXHelper::ToFVector(Geometry.WheelPivots[GetWheelIndex(SideIndex, 1)]);
		const FVector RearWheelPivot = FPhysXHelper::ToFVector(Geometry.WheelPivots[GetWheelIndex(SideIndex, 2)]);

		DrawDebugLine(World, RockerPivot, FrontWheelPivot, RockerColor, DebugDrawDuration);
		DrawDebugLine(World, RockerPivot, BogiePivot, RockerColor, DebugDrawDuration);
		DrawDebugLine(World, BogiePivot, MiddleWheelPivot, BogieColor, DebugDrawDuration);
		DrawDebugLine(World, BogiePivot, RearWheelPivot, BogieColor, DebugDrawDuration);

		DrawDebugPoint(World, RockerPivot, 0.06f, PivotColor, DebugDrawDuration);
		DrawDebugPoint(World, BogiePivot, 0.06f, PivotColor, DebugDrawDuration);
		DrawDebugPoint(World, FrontWheelPivot, 0.05f, AxleColor, DebugDrawDuration);
		DrawDebugPoint(World, MiddleWheelPivot, 0.05f, AxleColor, DebugDrawDuration);
		DrawDebugPoint(World, RearWheelPivot, 0.05f, AxleColor, DebugDrawDuration);
	}
}

void URockerBogieVehicleMovementComponent::DrawWheelRadius(UWorld* World, const FVector& Center, const FQuat& Rotation, const FColor& Color) const
{
	constexpr int32 SegmentCount = 32;
	constexpr float TwoPi = 6.28318530717958647692f;

	const FVector AxisX = Rotation.GetForwardVector();
	const FVector AxisZ = Rotation.GetUpVector();

	FVector PreviousPoint = Center + AxisX * WheelRadius;
	for (int32 Segment = 1; Segment <= SegmentCount; ++Segment)
	{
		const float Angle = (static_cast<float>(Segment) / static_cast<float>(SegmentCount)) * TwoPi;
		const FVector Point = Center + AxisX * (std::cos(Angle) * WheelRadius) + AxisZ * (std::sin(Angle) * WheelRadius);
		DrawDebugLine(World, PreviousPoint, Point, Color, DebugDrawDuration);
		PreviousPoint = Point;
	}
}

UPrimitiveComponent* URockerBogieVehicleMovementComponent::GetChassisComponent() const
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
