#include "Cloth/ClothScene.h"

#include "Cloth/ClothCollisionBridge.h"
#include "Cloth/ClothInstance.h"
#include "Cloth/ClothMesh.h"
#include "Component/Primitive/ClothComponent.h"
#include "Component/Physics/WindDirectionalSourceComponent.h"
#include "Core/Logging/Log.h"
#include "Engine/Profiling/Stats/Stats.h"
#include "GameFramework/World.h"
#include "Physics/IPhysicsScene.h"
#include "Render/Device/D3DDevice.h"
#include "Runtime/Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

static constexpr float MaxClothSubstepTime = 1.0f / 60.0f;
static constexpr uint32 MaxClothSubstepCount = 4;

static uint32 CalculateClothSubstepCount(float DeltaTime)
{
	const float SafeDeltaTime = (std::max)(0.0f, (std::min)(DeltaTime, MaxClothSubstepTime * MaxClothSubstepCount));
	return (std::max)(1u, static_cast<uint32>(std::ceil(SafeDeltaTime / MaxClothSubstepTime)));
}

FClothScene::~FClothScene()
{
	Shutdown();
}

void FClothScene::Initialize(UWorld* InWorld)
{
	World = InWorld;

	FNvClothInitializeDesc Desc;
	Desc.BackendPreference = EClothBackendPreference::Auto;
	Desc.D3DDevice = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;

	NvClothContext.Initialize(Desc);

	UE_LOG("[Cloth] Scene initialized. Backend=%s Fallback=%s",
		NvClothContext.GetActiveBackendName(),
		NvClothContext.GetFallbackStatus().c_str());
}

void FClothScene::Shutdown()
{
	if (!World && !NvClothContext.IsInitialized() && Instances.empty() && WindSources.empty())
	{
		return;
	}

	Instances.clear();
	WindSources.clear();
	CollisionScratch.Reset();
	LastActiveInstanceCount = 0;
	LastParticleCount = 0;
	LastCollisionPrimitiveCount = 0;
	LastSimulationTimeMs = 0.0;
	AverageSimulationTimeMs = 0.0;
	NvClothContext.Shutdown();
	World = nullptr;

	UE_LOG("[Cloth] Scene shutdown complete.");
}

void FClothScene::Tick(float DeltaTime)
{
	if (DeltaTime <= 0.0f) return;
	if (!NvClothContext.IsInitialized()) return;

	SCOPE_STAT_CAT("ClothSimulation", "Cloth");
	const auto StartTime = std::chrono::high_resolution_clock::now();
	const uint32 SubstepCount = CalculateClothSubstepCount(DeltaTime);
	const float ClampedDeltaTime = (std::min)(DeltaTime, MaxClothSubstepTime * MaxClothSubstepCount);
	const float SubstepDeltaTime = ClampedDeltaTime / static_cast<float>(SubstepCount);
	LastActiveInstanceCount = 0;
	LastParticleCount = 0;
	LastCollisionPrimitiveCount = 0;
	for (std::unique_ptr<FClothInstance>& Instance : Instances)
	{
		if (Instance && Instance->IsInitialized())
		{
			++LastActiveInstanceCount;
			if (UClothMesh* Mesh = Instance->GetMesh())
			{
				LastParticleCount += static_cast<uint32>(Mesh->GetParticles().size());
			}

			UClothComponent* OwnerComponent = Instance->GetOwnerComponent();
			const FVector WindVelocity = OwnerComponent
				? ComputeWindVelocityAt(OwnerComponent->GetWorldLocation())
				: FVector::ZeroVector;
			Instance->SetWindVelocity(WindVelocity);
			if (OwnerComponent && OwnerComponent->GetClothCollisionMode() == EClothCollisionMode::WorldShapes)
			{
				CollisionScratch.Reset();
				if (IPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr)
				{
					FClothCollisionGatherDesc GatherDesc;
					GatherDesc.ClothComponent = OwnerComponent;
					GatherDesc.CollisionThickness = OwnerComponent->GetCollisionThickness();
					PhysicsScene->GatherClothCollision(GatherDesc, CollisionScratch);
				}
				else
				{
					FClothCollisionBridge::BuildWorldShapeCollision(World, OwnerComponent, OwnerComponent->GetCollisionThickness(), CollisionScratch);
				}
				LastCollisionPrimitiveCount += CollisionScratch.GetPrimitiveCount();
			}
			else
			{
				CollisionScratch.Reset();
			}

			bool bSimulated = false;
			for (uint32 SubstepIndex = 0; SubstepIndex < SubstepCount; ++SubstepIndex)
			{
				Instance->SetCollisionDataForSubstep(CollisionScratch, SubstepIndex, SubstepCount);
				if (!Instance->Simulate(SubstepDeltaTime))
				{
					break;
				}

				bSimulated = true;
			}

			if (bSimulated && OwnerComponent)
			{
				OwnerComponent->MarkWorldBoundsDirty();
				OwnerComponent->MarkRenderTransformDirty();
			}
		}
	}

	const auto EndTime = std::chrono::high_resolution_clock::now();
	LastSimulationTimeMs = std::chrono::duration<double, std::milli>(EndTime - StartTime).count();
	AverageSimulationTimeMs = AverageSimulationTimeMs <= 0.0
		? LastSimulationTimeMs
		: AverageSimulationTimeMs * 0.95 + LastSimulationTimeMs * 0.05;
}

FClothInstance* FClothScene::CreateInstance(UClothMesh* Mesh, const FClothInstanceDesc& Desc)
{
	if (!NvClothContext.IsInitialized())
	{
		UE_LOG("[Cloth] Cannot create cloth instance before NvCloth context initialization.");
		return nullptr;
	}

	std::unique_ptr<FClothInstance> Instance = std::make_unique<FClothInstance>();
	if (!Instance->Initialize(NvClothContext, Mesh, Desc))
	{
		return nullptr;
	}

	FClothInstance* RawInstance = Instance.get();
	Instances.push_back(std::move(Instance));
	return RawInstance;
}

void FClothScene::DestroyInstance(FClothInstance* Instance)
{
	if (!Instance)
	{
		return;
	}

	auto It = std::remove_if(Instances.begin(), Instances.end(),
		[Instance](const std::unique_ptr<FClothInstance>& Candidate)
		{
			return Candidate.get() == Instance;
		});

	Instances.erase(It, Instances.end());
}

void FClothScene::RegisterWindSource(UWindDirectionalSourceComponent* WindSource)
{
	if (!WindSource)
	{
		return;
	}

	if (std::find(WindSources.begin(), WindSources.end(), WindSource) == WindSources.end())
	{
		WindSources.push_back(WindSource);
	}
}

void FClothScene::UnregisterWindSource(UWindDirectionalSourceComponent* WindSource)
{
	if (!WindSource)
	{
		return;
	}

	WindSources.erase(std::remove(WindSources.begin(), WindSources.end(), WindSource), WindSources.end());
}

FVector FClothScene::ComputeWindVelocityAt(const FVector& WorldPosition) const
{
	FVector Result = FVector::ZeroVector;

	for (UWindDirectionalSourceComponent* WindSource : WindSources)
	{
		if (WindSource && WindSource->IsWindEnabled())
		{
			Result += WindSource->GetWindVelocityAt(WorldPosition);
		}
	}

	return Result;
}
