#include "Cloth/ClothInstance.h"

#include "Cloth/ClothCollisionTypes.h"
#include "Cloth/ClothMesh.h"
#include "Cloth/NvClothContext.h"
#include "Component/Primitive/ClothComponent.h"
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

static physx::PxVec4 ToPxVec4(const FVector4& Vector)
{
	return physx::PxVec4(Vector.X, Vector.Y, Vector.Z, Vector.W);
}

static TArray<physx::PxVec4> ToPxVec4Array(const TArray<FVector4>& Source)
{
	TArray<physx::PxVec4> Result;
	Result.reserve(Source.size());
	for (const FVector4& Value : Source)
	{
		Result.push_back(ToPxVec4(Value));
	}
	return Result;
}

static nv::cloth::Range<const physx::PxVec4> MakePxVec4Range(const TArray<physx::PxVec4>& Values)
{
	return Values.empty()
		? nv::cloth::Range<const physx::PxVec4>()
		: nv::cloth::Range<const physx::PxVec4>(Values.data(), Values.data() + Values.size());
}

static nv::cloth::Range<const uint32_t> MakeUint32Range(const TArray<uint32>& Values)
{
	return Values.empty()
		? nv::cloth::Range<const uint32_t>()
		: nv::cloth::Range<const uint32_t>(Values.data(), Values.data() + Values.size());
}

static bool CanUsePreviousSphereCollision(const FClothCollisionData& Previous, const FClothCollisionData& Current)
{
	return Previous.Spheres.size() == Current.Spheres.size()
		&& Previous.Capsules == Current.Capsules;
}

static bool CanUsePreviousPlaneCollision(const FClothCollisionData& Previous, const FClothCollisionData& Current)
{
	return Previous.Planes.size() == Current.Planes.size()
		&& Previous.ConvexMasks == Current.ConvexMasks;
}

static FVector4 LerpVector4(const FVector4& A, const FVector4& B, float Alpha)
{
	return FVector4(
		A.X + (B.X - A.X) * Alpha,
		A.Y + (B.Y - A.Y) * Alpha,
		A.Z + (B.Z - A.Z) * Alpha,
		A.W + (B.W - A.W) * Alpha);
}

static FVector4 LerpPlane(const FVector4& A, const FVector4& B, float Alpha)
{
	FVector4 Plane = LerpVector4(A, B, Alpha);
	const float NormalLength = FVector(Plane.X, Plane.Y, Plane.Z).Length();
	if (NormalLength <= 0.0001f)
	{
		return B;
	}

	const float InvLength = 1.0f / NormalLength;
	return FVector4(
		Plane.X * InvLength,
		Plane.Y * InvLength,
		Plane.Z * InvLength,
		Plane.W * InvLength);
}

static void BuildSubstepSpheres(
	const FClothCollisionData& Previous,
	const FClothCollisionData& Current,
	bool bUsePrevious,
	float Alpha,
	TArray<FVector4>& OutSpheres)
{
	OutSpheres.clear();
	OutSpheres.reserve(Current.Spheres.size());

	for (size_t Index = 0; Index < Current.Spheres.size(); ++Index)
	{
		OutSpheres.push_back(bUsePrevious
			? LerpVector4(Previous.Spheres[Index], Current.Spheres[Index], Alpha)
			: Current.Spheres[Index]);
	}
}

static void BuildSubstepPlanes(
	const FClothCollisionData& Previous,
	const FClothCollisionData& Current,
	bool bUsePrevious,
	float Alpha,
	TArray<FVector4>& OutPlanes)
{
	OutPlanes.clear();
	OutPlanes.reserve(Current.Planes.size());

	for (size_t Index = 0; Index < Current.Planes.size(); ++Index)
	{
		OutPlanes.push_back(bUsePrevious
			? LerpPlane(Previous.Planes[Index], Current.Planes[Index], Alpha)
			: Current.Planes[Index]);
	}
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
	PreviousCollisionData.Reset();
	PreviousCollisionOwnerWorldMatrix = FMatrix::Identity;
	bHasPreviousCollisionData = false;
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
	SetCollisionDataForSubstep(CollisionData, 0, 1);
}

void FClothInstance::SetCollisionDataForSubstep(const FClothCollisionData& CollisionData, uint32 SubstepIndex, uint32 SubstepCount)
{
#if WITH_NVCLOTH
	if (!Cloth)
	{
		return;
	}

	const uint32 SafeSubstepCount = (std::max)(1u, SubstepCount);
	const uint32 SafeSubstepIndex = (std::min)(SubstepIndex, SafeSubstepCount - 1);
	const float StartAlpha = static_cast<float>(SafeSubstepIndex) / static_cast<float>(SafeSubstepCount);
	const float EndAlpha = static_cast<float>(SafeSubstepIndex + 1) / static_cast<float>(SafeSubstepCount);

	const FMatrix CurrentOwnerWorldMatrix = Desc.OwnerComponent
		? Desc.OwnerComponent->GetWorldMatrix()
		: FMatrix::Identity;
	const bool bCanUsePreviousOwnerSpace = bHasPreviousCollisionData
		&& PreviousCollisionOwnerWorldMatrix.Equals(CurrentOwnerWorldMatrix);
	const bool bUsePreviousSpheres = bCanUsePreviousOwnerSpace
		&& CanUsePreviousSphereCollision(PreviousCollisionData, CollisionData);
	const bool bUsePreviousPlanes = bCanUsePreviousOwnerSpace
		&& CanUsePreviousPlaneCollision(PreviousCollisionData, CollisionData);

	FClothCollisionData StartCollisionData;
	FClothCollisionData TargetCollisionData;
	BuildSubstepSpheres(PreviousCollisionData, CollisionData, bUsePreviousSpheres, StartAlpha, StartCollisionData.Spheres);
	BuildSubstepSpheres(PreviousCollisionData, CollisionData, bUsePreviousSpheres, EndAlpha, TargetCollisionData.Spheres);
	StartCollisionData.Capsules = CollisionData.Capsules;
	TargetCollisionData.Capsules = CollisionData.Capsules;

	BuildSubstepPlanes(PreviousCollisionData, CollisionData, bUsePreviousPlanes, StartAlpha, StartCollisionData.Planes);
	BuildSubstepPlanes(PreviousCollisionData, CollisionData, bUsePreviousPlanes, EndAlpha, TargetCollisionData.Planes);
	StartCollisionData.ConvexMasks = CollisionData.ConvexMasks;
	TargetCollisionData.ConvexMasks = CollisionData.ConvexMasks;

	ApplyCollisionData(StartCollisionData, TargetCollisionData);

	if (SafeSubstepIndex + 1 >= SafeSubstepCount)
	{
		CommitCollisionDataFrame(CollisionData);
	}
#else
	(void)CollisionData;
	(void)SubstepIndex;
	(void)SubstepCount;
#endif
}

void FClothInstance::ApplyCollisionData(const FClothCollisionData& StartCollisionData, const FClothCollisionData& TargetCollisionData)
{
#if WITH_NVCLOTH
	if (!Cloth)
	{
		return;
	}

	const TArray<physx::PxVec4> StartSpheres = ToPxVec4Array(StartCollisionData.Spheres);
	const TArray<physx::PxVec4> TargetSpheres = ToPxVec4Array(TargetCollisionData.Spheres);

	const uint32_t ExistingCapsuleCount = Cloth->getNumCapsules();
	if (ExistingCapsuleCount > 0)
	{
		Cloth->setCapsules(nv::cloth::Range<const uint32_t>(), 0, ExistingCapsuleCount);
	}

	const uint32_t ExistingSphereCount = Cloth->getNumSpheres();
	if (ExistingSphereCount != static_cast<uint32_t>(TargetSpheres.size()))
	{
		Cloth->setSpheres(nv::cloth::Range<const physx::PxVec4>(), 0, ExistingSphereCount);
	}

	Cloth->setSpheres(MakePxVec4Range(StartSpheres), MakePxVec4Range(TargetSpheres));

	const uint32_t NewCapsuleCount = static_cast<uint32_t>(TargetCollisionData.Capsules.size() / 2);
	if (NewCapsuleCount > 0)
	{
		Cloth->setCapsules(
			MakeUint32Range(TargetCollisionData.Capsules),
			0,
			0);
	}

	const TArray<physx::PxVec4> StartPlanes = ToPxVec4Array(StartCollisionData.Planes);
	const TArray<physx::PxVec4> TargetPlanes = ToPxVec4Array(TargetCollisionData.Planes);

	const uint32_t ExistingConvexCount = Cloth->getNumConvexes();
	if (ExistingConvexCount > 0)
	{
		Cloth->setConvexes(nv::cloth::Range<const uint32_t>(), 0, ExistingConvexCount);
	}

	const uint32_t ExistingPlaneCount = Cloth->getNumPlanes();
	if (ExistingPlaneCount != static_cast<uint32_t>(TargetPlanes.size()))
	{
		Cloth->setPlanes(nv::cloth::Range<const physx::PxVec4>(), 0, ExistingPlaneCount);
	}

	Cloth->setPlanes(MakePxVec4Range(StartPlanes), MakePxVec4Range(TargetPlanes));

	if (!TargetCollisionData.ConvexMasks.empty())
	{
		Cloth->setConvexes(
			MakeUint32Range(TargetCollisionData.ConvexMasks),
			0,
			0);
	}
#else
	(void)StartCollisionData;
	(void)TargetCollisionData;
#endif
}

void FClothInstance::CommitCollisionDataFrame(const FClothCollisionData& CollisionData)
{
	PreviousCollisionData = CollisionData;
	PreviousCollisionOwnerWorldMatrix = Desc.OwnerComponent
		? Desc.OwnerComponent->GetWorldMatrix()
		: FMatrix::Identity;
	bHasPreviousCollisionData = true;
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
	Cloth->setTetherConstraintScale(ClampClothSetting(Desc.TetherConstraintScale, 0.0f, 2.0f));
	Cloth->setTetherConstraintStiffness(ClampClothSetting(Desc.TetherConstraintStiffness, 0.0f, 1.0f));

	const float ConstraintStiffness = ClampClothSetting(Desc.ConstraintStiffness, 0.0f, 1.0f);
	const float ConstraintStiffnessMultiplier = ClampClothSetting(Desc.ConstraintStiffnessMultiplier, 0.0f, 2.0f);
	const float CompressionLimit = ClampClothSetting(Desc.CompressionLimit, 0.0f, 2.0f);
	const float StretchLimit = ClampClothSetting(Desc.StretchLimit, 0.0f, 2.0f);
	TArray<nv::cloth::PhaseConfig> PhaseConfigs;
	PhaseConfigs.reserve(Fabric->getNumPhases());
	for (uint32 Index = 0; Index < Fabric->getNumPhases(); ++Index)
	{
		nv::cloth::PhaseConfig PhaseConfig(static_cast<uint16_t>(Index));
		PhaseConfig.mStiffness = ConstraintStiffness;
		PhaseConfig.mStiffnessMultiplier = ConstraintStiffnessMultiplier;
		PhaseConfig.mCompressionLimit = CompressionLimit;
		PhaseConfig.mStretchLimit = StretchLimit;
		PhaseConfigs.push_back(PhaseConfig);
	}

	if (!PhaseConfigs.empty())
	{
		Cloth->setPhaseConfig(nv::cloth::Range<const nv::cloth::PhaseConfig>(PhaseConfigs.data(), PhaseConfigs.data() + PhaseConfigs.size()));
	}
#endif
}
