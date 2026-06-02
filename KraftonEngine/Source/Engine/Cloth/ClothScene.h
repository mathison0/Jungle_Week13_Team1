#pragma once

#include "Cloth/ClothCollisionTypes.h"
#include "Cloth/ClothInstance.h"
#include "Cloth/NvClothContext.h"
#include "Core/Types/CoreTypes.h"

#include <memory>

class UClothMesh;
class UWindDirectionalSourceComponent;
class UWorld;

class FClothScene
{
public:
	~FClothScene();

	void Initialize(UWorld* InWorld);
	void Shutdown();
	void Tick(float DeltaTime);

	FClothInstance* CreateInstance(UClothMesh* Mesh, const FClothInstanceDesc& Desc);
	void DestroyInstance(FClothInstance* Instance);
	void RegisterWindSource(UWindDirectionalSourceComponent* WindSource);
	void UnregisterWindSource(UWindDirectionalSourceComponent* WindSource);

	UWorld* GetWorld() const { return World; }
	FNvClothContext& GetNvClothContext() { return NvClothContext; }
	const FNvClothContext& GetNvClothContext() const { return NvClothContext; }
	uint32 GetInstanceCount() const { return static_cast<uint32>(Instances.size()); }
	uint32 GetWindSourceCount() const { return static_cast<uint32>(WindSources.size()); }
	uint32 GetLastActiveInstanceCount() const { return LastActiveInstanceCount; }
	uint32 GetLastParticleCount() const { return LastParticleCount; }
	uint32 GetLastCollisionPrimitiveCount() const { return LastCollisionPrimitiveCount; }
	double GetLastSimulationTimeMs() const { return LastSimulationTimeMs; }
	double GetAverageSimulationTimeMs() const { return AverageSimulationTimeMs; }

private:
	FVector ComputeWindVelocityAt(const FVector& WorldPosition) const;

	UWorld* World = nullptr;
	FNvClothContext NvClothContext;
	TArray<std::unique_ptr<FClothInstance>> Instances;
	TArray<UWindDirectionalSourceComponent*> WindSources;
	FClothCollisionData CollisionScratch;
	uint32 LastActiveInstanceCount = 0;
	uint32 LastParticleCount = 0;
	uint32 LastCollisionPrimitiveCount = 0;
	double LastSimulationTimeMs = 0.0;
	double AverageSimulationTimeMs = 0.0;
};
