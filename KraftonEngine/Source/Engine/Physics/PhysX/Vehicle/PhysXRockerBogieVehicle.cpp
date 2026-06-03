#include "Physics/PhysX/Vehicle/PhysXRockerBogieVehicle.h"

#include "Core/Types/CollisionTypes.h"

#include <algorithm>
#include <cmath>

using namespace physx;

namespace
{
	PxQuat JointXToVehicleY()
	{
		return PxQuat(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f));
	}

	PxTransform LocalToWorld(const PxTransform& Root, const PxVec3& Local, const PxQuat& LocalRot = PxQuat(PxIdentity))
	{
		return PxTransform(Root.transform(Local), Root.q * LocalRot);
	}

	float ClampInput(float Value)
	{
		return std::max(-1.0f, std::min(1.0f, Value));
	}

	float ClampRange(float Value, float MinValue, float MaxValue)
	{
		return std::max(MinValue, std::min(MaxValue, Value));
	}

	PxVec3 GetJointWorldPivot(const PxRevoluteJoint* Joint)
	{
		if (!Joint)
		{
			return PxVec3(0.0f);
		}

		PxRigidActor* Actor0 = nullptr;
		PxRigidActor* Actor1 = nullptr;
		Joint->getActors(Actor0, Actor1);
		if (!Actor0)
		{
			return PxVec3(0.0f);
		}

		return Actor0->getGlobalPose().transform(Joint->getLocalPose(PxJointActorIndex::eACTOR0).p);
	}
}

FPhysXRockerBogieVehicle::~FPhysXRockerBogieVehicle()
{
	Release();
}

bool FPhysXRockerBogieVehicle::Build(PxScene* Scene, PxPhysics* Physics, PxRigidDynamic* ChassisActor, const FPhysXRockerBogieSetup& Setup)
{
	if (!Scene || !Physics || !ChassisActor || Chassis)
	{
		return false;
	}

	SceneHandle = Scene;
	PhysicsHandle = Physics;
	Chassis = ChassisActor;
	Config = Setup;

	uint32 OwnerFilterId = 0;
	if (Chassis->getNbShapes() > 0)
	{
		PxShape* Shape = nullptr;
		Chassis->getShapes(&Shape, 1);
		if (Shape)
		{
			OwnerFilterId = Shape->getSimulationFilterData().word3;
		}
	}

	CollisionFilter.word0 = static_cast<PxU32>(ECollisionChannel::WorldDynamic);
	CollisionFilter.word1 = 0;
	CollisionFilter.word2 = 0;
	CollisionFilter.word3 = OwnerFilterId;
	for (uint32 Ch = 0; Ch < static_cast<uint32>(ECollisionChannel::ActiveCount); ++Ch)
	{
		CollisionFilter.word1 |= (1u << Ch);
	}

	TireMaterial = Physics->createMaterial(Setup.TireStaticFriction, Setup.TireDynamicFriction, 0.02f);
	BodyMaterial = Physics->createMaterial(0.55f, 0.45f, 0.05f);
	if (!TireMaterial || !BodyMaterial)
	{
		Release();
		return false;
	}

	BuildSide(0);
	BuildSide(1);

	Chassis->setAngularDamping(Config.ChassisAngularDamping);
	Chassis->setSolverIterationCounts(16, 4);

	return IsValid();
}

void FPhysXRockerBogieVehicle::Release()
{
	ReleaseSide(Sides[0]);
	ReleaseSide(Sides[1]);

	if (TireMaterial)
	{
		TireMaterial->release();
		TireMaterial = nullptr;
	}
	if (BodyMaterial)
	{
		BodyMaterial->release();
		BodyMaterial = nullptr;
	}

	Chassis = nullptr;
	SceneHandle = nullptr;
	PhysicsHandle = nullptr;
	ThrottleInput = 0.0f;
	SteerInput = 0.0f;
}

void FPhysXRockerBogieVehicle::SetInput(float Throttle, float Steer)
{
	ThrottleInput = ClampInput(Throttle);
	SteerInput = ClampInput(Steer);
}

void FPhysXRockerBogieVehicle::Simulate(float)
{
	const float LeftOmega = (ThrottleInput + SteerInput) * Config.MaxDriveSpeed;
	const float RightOmega = (ThrottleInput - SteerInput) * Config.MaxDriveSpeed;
	const float PerWheelDriveTorque = Config.MaxDriveTorque / static_cast<float>(WheelCount);

	if (Chassis)
	{
		const PxTransform ChassisPose = Chassis->getGlobalPose();
		const PxVec3 Forward = ChassisPose.q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
		const PxVec3 Right = ChassisPose.q.rotate(PxVec3(0.0f, 1.0f, 0.0f));
		const float HorizontalForwardLength = std::sqrt(Forward.x * Forward.x + Forward.y * Forward.y);
		const float PitchError = std::atan2(-Forward.z, HorizontalForwardLength);
		const float PitchRate = Chassis->getAngularVelocity().dot(Right);
		const float PitchTorque = ClampRange(
			-(PitchError * Config.ChassisPitchStiffness + PitchRate * Config.ChassisPitchDamping),
			-Config.MaxChassisPitchTorque,
			Config.MaxChassisPitchTorque);
		Chassis->addTorque(Right * PitchTorque);
	}

	for (uint32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		const float Omega = (SideIndex == 0) ? LeftOmega : RightOmega;
		for (uint32 Wheel = 0; Wheel < 3; ++Wheel)
		{
			FJointedBody& WheelBody = Sides[SideIndex].Wheels[Wheel];
			if (!WheelBody.Body)
			{
				continue;
			}

			if (WheelBody.Joint)
			{
				WheelBody.Joint->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_ENABLED, false);
			}

			if (std::fabs(Omega) <= 0.01f)
			{
				continue;
			}

			const PxVec3 WheelAxis = WheelBody.Body->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
			const float CurrentOmega = WheelBody.Body->getAngularVelocity().dot(WheelAxis);
			const float SpeedError = Omega - CurrentOmega;
			const float TorqueScale = ClampInput(SpeedError / Config.MaxDriveSpeed);
			WheelBody.Body->addTorque(WheelAxis * (PerWheelDriveTorque * TorqueScale));
		}
	}
}

bool FPhysXRockerBogieVehicle::GetWheelLocalPose(uint32 WheelIndex, PxTransform& OutPose) const
{
	if (WheelIndex >= WheelCount)
	{
		return false;
	}

	const uint32 SideIndex = (WheelIndex % 2 == 0) ? 0 : 1;
	const uint32 LocalWheel = WheelIndex / 2;
	const FJointedBody& Wheel = Sides[SideIndex].Wheels[LocalWheel];
	if (!Wheel.Body)
	{
		return false;
	}

	OutPose = GetChassisRelativePose(Wheel.Body);
	// Match PxVehicle4W visual convention: a wheel at rest reports identity
	// rotation, even though the physics capsule body is pre-rotated so its
	// local X axis becomes the engine's wheel/right axis (+Y).
	OutPose.q = OutPose.q * JointXToVehicleY().getConjugate();
	return true;
}

bool FPhysXRockerBogieVehicle::GetLinkLocalPose(uint32 LinkIndex, PxTransform& OutPose) const
{
	if (LinkIndex >= LinkCount)
	{
		return false;
	}

	const bool bRight = (LinkIndex == RightRocker || LinkIndex == RightBogie);
	const bool bBogie = (LinkIndex == LeftBogie || LinkIndex == RightBogie);
	const FJointedBody& Link = bBogie ? Sides[bRight ? 1 : 0].Bogie : Sides[bRight ? 1 : 0].Rocker;
	if (!Link.Body)
	{
		return false;
	}

	OutPose = GetChassisRelativePose(Link.Body);
	return true;
}

bool FPhysXRockerBogieVehicle::GetDebugGeometry(FPhysXRockerBogieDebugGeometry& OutGeometry) const
{
	if (!IsValid())
	{
		return false;
	}

	for (uint32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		const FSide& Side = Sides[SideIndex];
		OutGeometry.RockerPivots[SideIndex] = GetJointWorldPivot(Side.Rocker.Joint);
		OutGeometry.BogiePivots[SideIndex] = GetJointWorldPivot(Side.Bogie.Joint);

		for (uint32 LocalWheel = 0; LocalWheel < 3; ++LocalWheel)
		{
			const uint32 WheelIndex = LocalWheel * 2 + SideIndex;
			const FJointedBody& Wheel = Side.Wheels[LocalWheel];
			OutGeometry.WheelCenters[WheelIndex] = Wheel.Body ? Wheel.Body->getGlobalPose().p : PxVec3(0.0f);
			OutGeometry.WheelPivots[WheelIndex] = GetJointWorldPivot(Wheel.Joint);
		}
	}

	return true;
}

PxRigidDynamic* FPhysXRockerBogieVehicle::CreateBox(const PxTransform& Pose, const PxVec3& HalfExtents, float Mass, PxMaterial* Material)
{
	PxRigidDynamic* Body = PhysicsHandle->createRigidDynamic(Pose);
	if (!Body)
	{
		return nullptr;
	}

	PxShape* Shape = PhysicsHandle->createShape(PxBoxGeometry(HalfExtents), *Material, true);
	if (!Shape)
	{
		Body->release();
		return nullptr;
	}

	Body->attachShape(*Shape);
	PxRigidBodyExt::setMassAndUpdateInertia(*Body, Mass);
	Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
	Shape->release();
	Body->setLinearDamping(0.12f);
	Body->setAngularDamping(0.18f);
	Body->setSolverIterationCounts(16, 4);
	SetCollisionFilter(Body);
	SceneHandle->addActor(*Body);
	return Body;
}

PxRigidDynamic* FPhysXRockerBogieVehicle::CreateWheel(const PxTransform& Pose, float Radius, float HalfWidth, float Mass, PxMaterial* Material)
{
	PxRigidDynamic* Body = PhysicsHandle->createRigidDynamic(Pose);
	if (!Body)
	{
		return nullptr;
	}

	PxShape* Shape = PhysicsHandle->createShape(PxCapsuleGeometry(Radius, HalfWidth), *Material, true);
	if (!Shape)
	{
		Body->release();
		return nullptr;
	}

	Body->attachShape(*Shape);
	Shape->release();
	PxRigidBodyExt::setMassAndUpdateInertia(*Body, Mass);
	Body->setLinearDamping(0.04f);
	Body->setAngularDamping(0.08f);
	Body->setSolverIterationCounts(16, 4);
	SetCollisionFilter(Body);
	SceneHandle->addActor(*Body);
	return Body;
}

PxRevoluteJoint* FPhysXRockerBogieVehicle::CreateHinge(PxRigidActor* Parent, PxRigidDynamic* Child, const PxVec3& WorldPivot, const PxQuat& WorldAxisFrame)
{
	if (!Parent || !Child)
	{
		return nullptr;
	}

	const PxTransform ParentPose = Parent->getGlobalPose();
	const PxTransform ChildPose = Child->getGlobalPose();
	PxTransform ParentFrame(ParentPose.transformInv(WorldPivot), ParentPose.q.getConjugate() * WorldAxisFrame);
	PxTransform ChildFrame(ChildPose.transformInv(WorldPivot), ChildPose.q.getConjugate() * WorldAxisFrame);

	PxRevoluteJoint* Joint = PxRevoluteJointCreate(*PhysicsHandle, Parent, ParentFrame, Child, ChildFrame);
	if (Joint)
	{
		Joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, false);
		Joint->setConstraintFlag(PxConstraintFlag::ePROJECTION, true);
		Joint->setProjectionLinearTolerance(0.005f);
		Joint->setProjectionAngularTolerance(0.05f);
	}
	return Joint;
}

void FPhysXRockerBogieVehicle::BuildSide(uint32 SideIndex)
{
	const float SideSign = (SideIndex == 0) ? -1.0f : 1.0f;
	const float Y = SideSign * Config.HalfTrackWidth;
	const PxTransform ChassisPose = Chassis->getGlobalPose();
	const PxQuat HingeWorldFrame = ChassisPose.q * JointXToVehicleY();

	const PxVec3 RockerPivotLocal(Config.RockerPivotX, Y, Config.RockerPivotZ);
	const PxVec3 FrontWheelLocal(Config.RockerPivotX + Config.RockerFrontLength, Y, Config.WheelLocalZ);
	const PxVec3 BogiePivotLocal(Config.RockerPivotX - Config.RockerRearLength, Y, Config.RockerPivotZ);
	const PxVec3 MiddleWheelLocal(BogiePivotLocal.x + Config.BogieHalfLength, Y, Config.WheelLocalZ);
	const PxVec3 RearWheelLocal(BogiePivotLocal.x - Config.BogieHalfLength, Y, Config.WheelLocalZ);

	const PxVec3 RockerCenterLocal = (FrontWheelLocal + BogiePivotLocal) * 0.5f;
	const PxVec3 BogieCenterLocal = (MiddleWheelLocal + RearWheelLocal) * 0.5f;

	FSide& Side = Sides[SideIndex];
	Side.Rocker.Body = CreateBox(LocalToWorld(ChassisPose, RockerCenterLocal), PxVec3((Config.RockerFrontLength + Config.RockerRearLength) * 0.5f, 0.035f, 0.035f), Config.ArmMass, BodyMaterial);
	Side.Rocker.Joint = CreateHinge(Chassis, Side.Rocker.Body, ChassisPose.transform(RockerPivotLocal), HingeWorldFrame);
	if (Side.Rocker.Joint)
	{
		Side.Rocker.Joint->setLimit(PxJointAngularLimitPair(-Config.RockerAngleLimit, Config.RockerAngleLimit, PxSpring(Config.JointSpring, Config.JointDamping)));
		Side.Rocker.Joint->setRevoluteJointFlag(PxRevoluteJointFlag::eLIMIT_ENABLED, true);
	}

	Side.Wheels[0].Body = CreateWheel(LocalToWorld(ChassisPose, FrontWheelLocal, JointXToVehicleY()), Config.WheelRadius, Config.WheelHalfWidth, Config.WheelMass, TireMaterial);
	Side.Wheels[0].Joint = CreateHinge(Side.Rocker.Body, Side.Wheels[0].Body, ChassisPose.transform(FrontWheelLocal), HingeWorldFrame);
	if (Side.Wheels[0].Joint)
	{
		Side.Wheels[0].Joint->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_ENABLED, false);
	}

	Side.Bogie.Body = CreateBox(LocalToWorld(ChassisPose, BogieCenterLocal), PxVec3(Config.BogieHalfLength, 0.035f, 0.035f), Config.ArmMass, BodyMaterial);
	Side.Bogie.Joint = CreateHinge(Side.Rocker.Body, Side.Bogie.Body, ChassisPose.transform(BogiePivotLocal), HingeWorldFrame);
	if (Side.Bogie.Joint)
	{
		Side.Bogie.Joint->setLimit(PxJointAngularLimitPair(-Config.BogieAngleLimit, Config.BogieAngleLimit, PxSpring(Config.JointSpring, Config.JointDamping)));
		Side.Bogie.Joint->setRevoluteJointFlag(PxRevoluteJointFlag::eLIMIT_ENABLED, true);
	}

	Side.Wheels[1].Body = CreateWheel(LocalToWorld(ChassisPose, MiddleWheelLocal, JointXToVehicleY()), Config.WheelRadius, Config.WheelHalfWidth, Config.WheelMass, TireMaterial);
	Side.Wheels[1].Joint = CreateHinge(Side.Bogie.Body, Side.Wheels[1].Body, ChassisPose.transform(MiddleWheelLocal), HingeWorldFrame);
	if (Side.Wheels[1].Joint)
	{
		Side.Wheels[1].Joint->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_ENABLED, false);
	}

	Side.Wheels[2].Body = CreateWheel(LocalToWorld(ChassisPose, RearWheelLocal, JointXToVehicleY()), Config.WheelRadius, Config.WheelHalfWidth, Config.WheelMass, TireMaterial);
	Side.Wheels[2].Joint = CreateHinge(Side.Bogie.Body, Side.Wheels[2].Body, ChassisPose.transform(RearWheelLocal), HingeWorldFrame);
	if (Side.Wheels[2].Joint)
	{
		Side.Wheels[2].Joint->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_ENABLED, false);
	}
}

void FPhysXRockerBogieVehicle::ReleaseSide(FSide& Side)
{
	for (FJointedBody& Wheel : Side.Wheels)
	{
		if (Wheel.Joint)
		{
			Wheel.Joint->release();
			Wheel.Joint = nullptr;
		}
		if (Wheel.Body)
		{
			if (SceneHandle)
			{
				SceneHandle->removeActor(*Wheel.Body);
			}
			Wheel.Body->release();
			Wheel.Body = nullptr;
		}
	}

	if (Side.Bogie.Joint)
	{
		Side.Bogie.Joint->release();
		Side.Bogie.Joint = nullptr;
	}
	if (Side.Bogie.Body)
	{
		if (SceneHandle)
		{
			SceneHandle->removeActor(*Side.Bogie.Body);
		}
		Side.Bogie.Body->release();
		Side.Bogie.Body = nullptr;
	}

	if (Side.Rocker.Joint)
	{
		Side.Rocker.Joint->release();
		Side.Rocker.Joint = nullptr;
	}
	if (Side.Rocker.Body)
	{
		if (SceneHandle)
		{
			SceneHandle->removeActor(*Side.Rocker.Body);
		}
		Side.Rocker.Body->release();
		Side.Rocker.Body = nullptr;
	}
}

void FPhysXRockerBogieVehicle::SetCollisionFilter(PxRigidActor* Actor) const
{
	if (!Actor)
	{
		return;
	}

	const PxU32 ShapeCount = Actor->getNbShapes();
	for (PxU32 Index = 0; Index < ShapeCount; ++Index)
	{
		PxShape* Shape = nullptr;
		Actor->getShapes(&Shape, 1, Index);
		if (Shape)
		{
			Shape->setSimulationFilterData(CollisionFilter);
			Shape->setQueryFilterData(CollisionFilter);
		}
	}
}

PxTransform FPhysXRockerBogieVehicle::GetChassisRelativePose(const PxRigidActor* Actor) const
{
	if (!Chassis || !Actor)
	{
		return PxTransform(PxIdentity);
	}

	const PxTransform ChassisPose = Chassis->getGlobalPose();
	const PxTransform ActorPose = Actor->getGlobalPose();
	return PxTransform(
		ChassisPose.transformInv(ActorPose.p),
		ChassisPose.q.getConjugate() * ActorPose.q);
}
