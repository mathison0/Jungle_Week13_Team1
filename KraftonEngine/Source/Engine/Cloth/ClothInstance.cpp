#include "Cloth/ClothInstance.h"

#include "Cloth/ClothCollisionTypes.h"
#include "Cloth/ClothMesh.h"
#include "Cloth/NvClothContext.h"
#include "Core/Logging/Log.h"

#include <algorithm>

#if WITH_NVCLOTH
#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/Factory.h>
#include <NvCloth/Range.h>
#include <NvCloth/Solver.h>
#include <NvClothExt/ClothFabricCooker.h>
#include <NvClothExt/ClothMeshDesc.h>
#include <foundation/PxVec3.h>
#include <foundation/PxVec4.h>
#endif

#if WITH_NVCLOTH
static physx::PxVec3 ToPxVec3(const FVector& Vector)
{
	return physx::PxVec3(Vector.X, Vector.Y, Vector.Z);
}
#endif

static float ClampClothDeltaTime(float DeltaTime)
{
	return (std::max)(0.0f, (std::min)(DeltaTime, 1.0f / 30.0f));
}

static float ClampClothSetting(float Value, float MinValue, float MaxValue)
{
	return (std::max)(MinValue, (std::min)(Value, MaxValue));
}

FClothInstance::~FClothInstance()
{
	Shutdown();
}

bool FClothInstance::Initialize(FNvClothContext& InContext, UClothMesh* InMesh, const FClothInstanceDesc& InDesc)
{
	Shutdown();

	Context = &InContext;
	Mesh = InMesh;
	Desc = InDesc;

	if (!Context || !Context->IsInitialized())
	{
		UE_LOG("[Cloth] NvCloth context is not initialized.");
		return false;
	}

	if (!Mesh)
	{
		UE_LOG("[Cloth] Cloth mesh is null.");
		return false;
	}

	if (Mesh->GetParticles().empty())
	{
		Mesh->RebuildGrid();
	}

	if (Mesh->GetParticles().size() < 3 || Mesh->GetSimulationIndices().size() < 3)
	{
		UE_LOG("[Cloth] Cloth mesh does not have enough particles or triangles.");
		return false;
	}

	if (!CookFabricAndCreateCloth())
	{
		Shutdown();
		return false;
	}

	return true;
}

void FClothInstance::Shutdown()
{
#if WITH_NVCLOTH
	if (Solver && Cloth)
	{
		Solver->removeCloth(Cloth);
	}

	delete Solver;
	Solver = nullptr;

	delete Cloth;
	Cloth = nullptr;

	if (Fabric)
	{
		Fabric->decRefCount();
		Fabric = nullptr;
	}
#endif

	Context = nullptr;
	Mesh = nullptr;
}

bool FClothInstance::Simulate(float DeltaTime)
{
#if WITH_NVCLOTH
	if (!IsInitialized())
	{
		return false;
	}

	const float ClampedDeltaTime = ClampClothDeltaTime(DeltaTime);
	if (ClampedDeltaTime <= 0.0f)
	{
		return true;
	}

	if (Solver->beginSimulation(ClampedDeltaTime))
	{
		const int ChunkCount = Solver->getSimulationChunkCount();
		for (int ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
		{
			Solver->simulateChunk(ChunkIndex);
		}

		Solver->endSimulation();
	}

	if (Solver->hasError())
	{
		UE_LOG("[Cloth] NvCloth solver reported an unrecoverable error.");
		return false;
	}

	return WriteBackParticles();
#else
	(void)DeltaTime;
	return false;
#endif
}

void FClothInstance::SetGravity(const FVector& InGravity)
{
	Desc.Gravity = InGravity;
#if WITH_NVCLOTH
	if (Cloth)
	{
		Cloth->setGravity(ToPxVec3(Desc.Gravity));
	}
#endif
}

void FClothInstance::SetWindVelocity(const FVector& InWindVelocity)
{
	Desc.WindVelocity = InWindVelocity;
#if WITH_NVCLOTH
	if (Cloth)
	{
		Cloth->setWindVelocity(ToPxVec3(Desc.WindVelocity));
	}
#endif
}

void FClothInstance::SetCollisionData(const FClothCollisionData& CollisionData)
{
#if WITH_NVCLOTH
	if (!Cloth)
	{
		return;
	}

	TArray<physx::PxVec4> Spheres;
	Spheres.reserve(CollisionData.Spheres.size());
	for (const FVector4& Sphere : CollisionData.Spheres)
	{
		Spheres.push_back(physx::PxVec4(Sphere.X, Sphere.Y, Sphere.Z, Sphere.W));
	}

	const uint32_t ExistingCapsuleCount = Cloth->getNumCapsules();
	if (ExistingCapsuleCount > 0)
	{
		Cloth->setCapsules(nv::cloth::Range<const uint32_t>(), 0, ExistingCapsuleCount);
	}

	const uint32_t ExistingSphereCount = Cloth->getNumSpheres();
	if (ExistingSphereCount > 0)
	{
		Cloth->setSpheres(nv::cloth::Range<const physx::PxVec4>(), 0, ExistingSphereCount);
	}

	if (!Spheres.empty())
	{
		Cloth->setSpheres(
			nv::cloth::Range<const physx::PxVec4>(Spheres.data(), Spheres.data() + Spheres.size()),
			0,
			0);
	}

	const uint32_t NewCapsuleCount = static_cast<uint32_t>(CollisionData.Capsules.size() / 2);
	if (NewCapsuleCount > 0)
	{
		Cloth->setCapsules(
			nv::cloth::Range<const uint32_t>(CollisionData.Capsules.data(), CollisionData.Capsules.data() + CollisionData.Capsules.size()),
			0,
			0);
	}

	TArray<physx::PxVec4> Planes;
	Planes.reserve(CollisionData.Planes.size());
	for (const FVector4& Plane : CollisionData.Planes)
	{
		Planes.push_back(physx::PxVec4(Plane.X, Plane.Y, Plane.Z, Plane.W));
	}

	const uint32_t ExistingConvexCount = Cloth->getNumConvexes();
	if (ExistingConvexCount > 0)
	{
		Cloth->setConvexes(nv::cloth::Range<const uint32_t>(), 0, ExistingConvexCount);
	}

	const uint32_t ExistingPlaneCount = Cloth->getNumPlanes();
	if (ExistingPlaneCount > 0)
	{
		Cloth->setPlanes(nv::cloth::Range<const physx::PxVec4>(), 0, ExistingPlaneCount);
	}

	if (!Planes.empty())
	{
		Cloth->setPlanes(
			nv::cloth::Range<const physx::PxVec4>(Planes.data(), Planes.data() + Planes.size()),
			0,
			0);
	}

	if (!CollisionData.ConvexMasks.empty())
	{
		Cloth->setConvexes(
			nv::cloth::Range<const uint32_t>(CollisionData.ConvexMasks.data(), CollisionData.ConvexMasks.data() + CollisionData.ConvexMasks.size()),
			0,
			0);
	}

	static uint32 ClothCollisionSetLogCounter = 0;
	if ((++ClothCollisionSetLogCounter % 30) == 1)
	{
		UE_LOG("[ClothCollision][NvCloth] set spheres=%u capsules=%u planes=%u convexes=%u primitives=%u previousSpheres=%u previousCapsules=%u previousPlanes=%u previousConvexes=%u",
			static_cast<uint32>(CollisionData.Spheres.size()),
			NewCapsuleCount,
			static_cast<uint32>(CollisionData.Planes.size()),
			static_cast<uint32>(CollisionData.ConvexMasks.size()),
			CollisionData.GetPrimitiveCount(),
			ExistingSphereCount,
			ExistingCapsuleCount,
			ExistingPlaneCount,
			ExistingConvexCount);
	}
#else
	(void)CollisionData;
#endif
}

bool FClothInstance::CookFabricAndCreateCloth()
{
#if WITH_NVCLOTH
	nv::cloth::Factory* Factory = Context ? Context->GetFactory() : nullptr;
	if (!Factory || !Mesh)
	{
		return false;
	}

	const TArray<FClothParticle>& SourceParticles = Mesh->GetParticles();
	const TArray<uint32>& SourceIndices = Mesh->GetSimulationIndices();
	if (SourceParticles.empty() || SourceIndices.empty())
	{
		return false;
	}

	TArray<physx::PxVec3> CookPositions;
	TArray<float> CookInvMasses;
	TArray<physx::PxU32> CookIndices;
	TArray<physx::PxVec4> InitialParticles;

	CookPositions.reserve(SourceParticles.size());
	CookInvMasses.reserve(SourceParticles.size());
	InitialParticles.reserve(SourceParticles.size());

	for (const FClothParticle& Particle : SourceParticles)
	{
		CookPositions.push_back(ToPxVec3(Particle.Position));
		CookInvMasses.push_back(Particle.InvMass);
		InitialParticles.push_back(physx::PxVec4(Particle.Position.X, Particle.Position.Y, Particle.Position.Z, Particle.InvMass));
	}

	CookIndices.reserve(SourceIndices.size());
	for (uint32 Index : SourceIndices)
	{
		CookIndices.push_back(static_cast<physx::PxU32>(Index));
	}

	nv::cloth::ClothMeshDesc MeshDesc;
	MeshDesc.setToDefault();
	MeshDesc.points.data = CookPositions.data();
	MeshDesc.points.count = static_cast<physx::PxU32>(CookPositions.size());
	MeshDesc.points.stride = sizeof(physx::PxVec3);
	MeshDesc.invMasses.data = CookInvMasses.data();
	MeshDesc.invMasses.count = static_cast<physx::PxU32>(CookInvMasses.size());
	MeshDesc.invMasses.stride = sizeof(float);
	MeshDesc.triangles.data = CookIndices.data();
	MeshDesc.triangles.count = static_cast<physx::PxU32>(CookIndices.size() / 3);
	MeshDesc.triangles.stride = sizeof(physx::PxU32) * 3;

	if (!MeshDesc.isValid())
	{
		UE_LOG("[Cloth] NvCloth mesh descriptor is invalid.");
		return false;
	}

	nv::cloth::Vector<int32_t>::Type PhaseTypes;
	Fabric = NvClothCookFabricFromMesh(Factory, MeshDesc, ToPxVec3(Desc.Gravity), &PhaseTypes, Desc.bUseGeodesicTether);
	if (!Fabric)
	{
		UE_LOG("[Cloth] NvCloth fabric cooking failed.");
		return false;
	}

	Cloth = Factory->createCloth(
		nv::cloth::Range<const physx::PxVec4>(InitialParticles.data(), InitialParticles.data() + InitialParticles.size()),
		*Fabric);
	if (!Cloth)
	{
		UE_LOG("[Cloth] NvCloth cloth creation failed.");
		return false;
	}

	Solver = Factory->createSolver();
	if (!Solver)
	{
		UE_LOG("[Cloth] NvCloth solver creation failed.");
		return false;
	}

	ApplySettings();
	Solver->addCloth(Cloth);
	return true;
#else
	UE_LOG("[Cloth] WITH_NVCLOTH is disabled.");
	return false;
#endif
}

bool FClothInstance::WriteBackParticles()
{
#if WITH_NVCLOTH
	if (!Cloth || !Mesh)
	{
		return false;
	}

	nv::cloth::MappedRange<physx::PxVec4> CurrentParticles = Cloth->getCurrentParticles();
	TArray<FVector> Positions;
	Positions.reserve(CurrentParticles.size());

	for (uint32 Index = 0; Index < CurrentParticles.size(); ++Index)
	{
		const physx::PxVec4& Particle = CurrentParticles[Index];
		Positions.push_back(FVector(Particle.x, Particle.y, Particle.z));
	}

	Mesh->UpdateParticlePositions(Positions);
	return true;
#else
	return false;
#endif
}

void FClothInstance::ApplySettings()
{
#if WITH_NVCLOTH
	if (!Cloth || !Fabric)
	{
		return;
	}

	Cloth->setGravity(ToPxVec3(Desc.Gravity));
	Cloth->setWindVelocity(ToPxVec3(Desc.WindVelocity));
	const float Damping = ClampClothSetting(Desc.Damping, 0.0f, 1.0f);
	const float LinearDrag = ClampClothSetting(Desc.LinearDrag, 0.0f, 1.0f);
	const float AngularDrag = ClampClothSetting(Desc.AngularDrag, 0.0f, 1.0f);

	Cloth->setSolverFrequency((std::max)(1.0f, Desc.SolverFrequency));
	Cloth->setStiffnessFrequency((std::max)(1.0f, Desc.StiffnessFrequency));
	Cloth->setDamping(physx::PxVec3(Damping, Damping, Damping));
	Cloth->setLinearDrag(physx::PxVec3(LinearDrag, LinearDrag, LinearDrag));
	Cloth->setAngularDrag(physx::PxVec3(AngularDrag, AngularDrag, AngularDrag));
	Cloth->setDragCoefficient(ClampClothSetting(Desc.DragCoefficient, 0.0f, 2.0f));
	Cloth->setLiftCoefficient(ClampClothSetting(Desc.LiftCoefficient, 0.0f, 2.0f));
	Cloth->setFriction(ClampClothSetting(Desc.Friction, 0.0f, 1.0f));
	Cloth->setCollisionMassScale(ClampClothSetting(Desc.CollisionMassScale, 0.0f, 10.0f));
	Cloth->enableContinuousCollision(Desc.bEnableContinuousCollision);

	TArray<nv::cloth::PhaseConfig> PhaseConfigs;
	PhaseConfigs.reserve(Fabric->getNumPhases());
	for (uint32 Index = 0; Index < Fabric->getNumPhases(); ++Index)
	{
		nv::cloth::PhaseConfig PhaseConfig(static_cast<uint16_t>(Index));
		PhaseConfig.mStiffness = 1.0f;
		PhaseConfig.mStiffnessMultiplier = 1.0f;
		PhaseConfig.mCompressionLimit = 1.0f;
		PhaseConfig.mStretchLimit = 1.0f;
		PhaseConfigs.push_back(PhaseConfig);
	}

	if (!PhaseConfigs.empty())
	{
		Cloth->setPhaseConfig(nv::cloth::Range<const nv::cloth::PhaseConfig>(PhaseConfigs.data(), PhaseConfigs.data() + PhaseConfigs.size()));
	}
#endif
}
