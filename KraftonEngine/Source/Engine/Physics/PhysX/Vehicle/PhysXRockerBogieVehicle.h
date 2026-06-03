#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"

#include <PxPhysicsAPI.h>

struct FPhysXRockerBogieSetup
{
	float WheelRadius = 0.28f;
	float WheelHalfWidth = 0.10f;
	float WheelMass = 18.0f;
	float ArmMass = 12.0f;

	float HalfTrackWidth = 0.85f;
	float WheelLocalZ = -0.55f;
	float RockerPivotX = 0.0f;
	float RockerPivotZ = -0.20f;
	float RockerFrontLength = 0.90f;
	float RockerRearLength = 0.70f;
	float BogieHalfLength = 0.45f;

	float MaxDriveTorque = 850.0f;
	float MaxDriveSpeed = 18.0f;
	float WheelContactProbeExtra = 0.18f;
	float WheelSlipSpeed = 3.0f;
	float MinGroundedTorqueScale = 0.15f;
	float ChassisAngularDamping = 1.8f;
	float ChassisPitchStiffness = 900.0f;
	float ChassisPitchDamping = 180.0f;
	float MaxChassisPitchTorque = 2200.0f;
	float RockerAngleLimit = physx::PxPi * 0.35f;
	float BogieAngleLimit = physx::PxPi * 0.18f;
	float JointSpring = 450.0f;
	float JointDamping = 90.0f;
	float TireStaticFriction = 1.2f;
	float TireDynamicFriction = 1.0f;
};

struct FPhysXRockerBogieDebugGeometry
{
	physx::PxVec3 WheelCenters[6];
	physx::PxVec3 WheelPivots[6];
	physx::PxVec3 RockerPivots[2];
	physx::PxVec3 BogiePivots[2];
};

class FPhysXRockerBogieVehicle
{
public:
	enum EWheelIndex : uint32
	{
		FrontLeft = 0,
		FrontRight,
		MiddleLeft,
		MiddleRight,
		RearLeft,
		RearRight,
		WheelCount
	};

	enum ELinkIndex : uint32
	{
		LeftRocker = 0,
		RightRocker,
		LeftBogie,
		RightBogie,
		LinkCount
	};

	FPhysXRockerBogieVehicle() = default;
	~FPhysXRockerBogieVehicle();

	FPhysXRockerBogieVehicle(const FPhysXRockerBogieVehicle&) = delete;
	FPhysXRockerBogieVehicle& operator=(const FPhysXRockerBogieVehicle&) = delete;

	bool Build(physx::PxScene* Scene, physx::PxPhysics* Physics, physx::PxRigidDynamic* ChassisActor, const FPhysXRockerBogieSetup& Setup);
	void Release();
	bool IsValid() const { return Chassis != nullptr; }

	void SetInput(float Throttle, float Steer);
	void Simulate(float DeltaTime);

	bool GetWheelLocalPose(uint32 WheelIndex, physx::PxTransform& OutPose) const;
	bool GetLinkLocalPose(uint32 LinkIndex, physx::PxTransform& OutPose) const;
	bool GetDebugGeometry(FPhysXRockerBogieDebugGeometry& OutGeometry) const;

private:
	struct FJointedBody
	{
		physx::PxRigidDynamic* Body = nullptr;
		physx::PxRevoluteJoint* Joint = nullptr;
	};

	struct FSide
	{
		FJointedBody Rocker;
		FJointedBody Bogie;
		FJointedBody Wheels[3];
	};

	physx::PxRigidDynamic* CreateBox(const physx::PxTransform& Pose, const physx::PxVec3& HalfExtents, float Mass, physx::PxMaterial* Material);
	physx::PxRigidDynamic* CreateWheel(const physx::PxTransform& Pose, float Radius, float HalfWidth, float Mass, physx::PxMaterial* Material);
	physx::PxConvexMesh* CreateWheelConvexMesh(float Radius, float HalfWidth) const;
	physx::PxRevoluteJoint* CreateHinge(physx::PxRigidActor* Parent, physx::PxRigidDynamic* Child, const physx::PxVec3& WorldPivot, const physx::PxQuat& WorldAxisFrame);
	bool QueryWheelContact(const FJointedBody& Wheel, float& OutLoadScale, float& OutSlipScale) const;
	void BuildSide(uint32 SideIndex);
	void ReleaseSide(FSide& Side);
	void SetCollisionFilter(physx::PxRigidActor* Actor) const;
	physx::PxTransform GetChassisRelativePose(const physx::PxRigidActor* Actor) const;

	FPhysXRockerBogieSetup Config;
	physx::PxScene* SceneHandle = nullptr;
	physx::PxPhysics* PhysicsHandle = nullptr;
	physx::PxRigidDynamic* Chassis = nullptr;
	physx::PxMaterial* TireMaterial = nullptr;
	physx::PxMaterial* BodyMaterial = nullptr;
	physx::PxConvexMesh* WheelConvexMesh = nullptr;
	FSide Sides[2];
	physx::PxFilterData CollisionFilter;
	float ThrottleInput = 0.0f;
	float SteerInput = 0.0f;
};
