#include "Component/Primitive/ClothComponent.h"

#include "Cloth/ClothInstance.h"
#include "Cloth/ClothScene.h"
#include "Core/Logging/Log.h"
#include "GameFramework/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Object/ReferenceCollector.h"
#include "Render/Proxy/ClothSceneProxy.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"

#include <algorithm>
#include <cstring>

UClothComponent::~UClothComponent()
{
	UnregisterClothInstance();
}

void UClothComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveMaterial();
	RebuildCloth();
	RegisterClothInstance();
}

void UClothComponent::EndPlay()
{
	UnregisterClothInstance();
	Super::EndPlay();
}

void UClothComponent::PostEditProperty(const char* PropertyName)
{
	Super::PostEditProperty(PropertyName);

	if (!PropertyName || std::strcmp(PropertyName, "MaterialPath") == 0)
	{
		ResolveMaterial();
		MarkProxyDirty(EDirtyFlag::Material);
	}

	if (!PropertyName ||
		std::strcmp(PropertyName, "Columns") == 0 ||
		std::strcmp(PropertyName, "Rows") == 0 ||
		std::strcmp(PropertyName, "Width") == 0 ||
		std::strcmp(PropertyName, "Height") == 0 ||
		std::strcmp(PropertyName, "PinMode") == 0 ||
		std::strcmp(PropertyName, "bDoubleSided") == 0)
	{
		RebuildCloth();
	}

	if (!PropertyName ||
		std::strcmp(PropertyName, "bSimulateCloth") == 0 ||
		std::strcmp(PropertyName, "Gravity") == 0 ||
		std::strcmp(PropertyName, "SolverFrequency") == 0 ||
		std::strcmp(PropertyName, "StiffnessFrequency") == 0 ||
		std::strcmp(PropertyName, "Damping") == 0 ||
		std::strcmp(PropertyName, "LinearDrag") == 0 ||
		std::strcmp(PropertyName, "AngularDrag") == 0 ||
		std::strcmp(PropertyName, "DragCoefficient") == 0 ||
		std::strcmp(PropertyName, "LiftCoefficient") == 0 ||
		std::strcmp(PropertyName, "ConstraintStiffness") == 0 ||
		std::strcmp(PropertyName, "ConstraintStiffnessMultiplier") == 0 ||
		std::strcmp(PropertyName, "CompressionLimit") == 0 ||
		std::strcmp(PropertyName, "StretchLimit") == 0 ||
		std::strcmp(PropertyName, "TetherConstraintScale") == 0 ||
		std::strcmp(PropertyName, "TetherConstraintStiffness") == 0 ||
		std::strcmp(PropertyName, "bUseGeodesicTether") == 0 ||
		std::strcmp(PropertyName, "CollisionMode") == 0 ||
		std::strcmp(PropertyName, "Friction") == 0 ||
		std::strcmp(PropertyName, "CollisionMassScale") == 0 ||
		std::strcmp(PropertyName, "CollisionThickness") == 0 ||
		std::strcmp(PropertyName, "bEnableContinuousCollision") == 0)
	{
		if (bComponentHasBegunPlay)
		{
			UnregisterClothInstance();
			RegisterClothInstance();
		}
	}
}

void UClothComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(ClothMesh.Get());
	Collector.AddReferencedObject(Material);
}

FMeshDataView UClothComponent::GetMeshDataView() const
{
	if (!ClothMesh || ClothMesh->GetRenderVertices().empty())
	{
		return {};
	}

	FMeshDataView View;
	View.VertexData = ClothMesh->GetRenderVertices().data();
	View.VertexCount = static_cast<uint32>(ClothMesh->GetRenderVertices().size());
	View.Stride = sizeof(FVertexPNCTT);
	View.IndexData = ClothMesh->GetRenderIndices().data();
	View.IndexCount = static_cast<uint32>(ClothMesh->GetRenderIndices().size());
	return View;
}

void UClothComponent::UpdateWorldAABB() const
{
	if (!ClothMesh || ClothMesh->GetRenderVertices().empty())
	{
		UPrimitiveComponent::UpdateWorldAABB();
		return;
	}

	const FMatrix& WorldMatrix = GetWorldMatrix();
	FVector MinLocation = WorldMatrix.TransformPositionWithW(ClothMesh->GetRenderVertices()[0].Position);
	FVector MaxLocation = MinLocation;

	for (const FVertexPNCTT& Vertex : ClothMesh->GetRenderVertices())
	{
		const FVector WorldPosition = WorldMatrix.TransformPositionWithW(Vertex.Position);
		MinLocation.X = (std::min)(MinLocation.X, WorldPosition.X);
		MinLocation.Y = (std::min)(MinLocation.Y, WorldPosition.Y);
		MinLocation.Z = (std::min)(MinLocation.Z, WorldPosition.Z);
		MaxLocation.X = (std::max)(MaxLocation.X, WorldPosition.X);
		MaxLocation.Y = (std::max)(MaxLocation.Y, WorldPosition.Y);
		MaxLocation.Z = (std::max)(MaxLocation.Z, WorldPosition.Z);
	}

	WorldAABBMinLocation = MinLocation;
	WorldAABBMaxLocation = MaxLocation;
	bWorldAABBDirty = false;
	bHasValidWorldAABB = true;
}

FPrimitiveSceneProxy* UClothComponent::CreateSceneProxy()
{
	EnsureClothMesh();
	if (!ClothMesh || ClothMesh->GetRenderVertices().empty())
	{
		if (ClothMesh)
		{
			ClothMesh->SetDoubleSided(bDoubleSided);
			ClothMesh->BuildGrid(Columns, Rows, Width, Height, PinMode);
			MarkWorldBoundsDirty();
		}
	}

	return new FClothSceneProxy(this);
}

void UClothComponent::RebuildCloth()
{
	EnsureClothMesh();
	if (!ClothMesh)
	{
		return;
	}

	const bool bWasRegistered = ClothInstance != nullptr;
	if (bWasRegistered)
	{
		UnregisterClothInstance();
	}

	ClothMesh->SetDoubleSided(bDoubleSided);
	ClothMesh->BuildGrid(Columns, Rows, Width, Height, PinMode);

	MarkWorldBoundsDirty();
	MarkRenderStateDirty();
	MarkRenderTransformDirty();

	if (bWasRegistered)
	{
		RegisterClothInstance();
	}
}

void UClothComponent::RegisterClothInstance()
{
	if (ClothInstance || !bSimulateCloth)
	{
		return;
	}

	EnsureClothMesh();
	UWorld* World = GetWorld();
	FClothScene* ClothScene = World ? World->GetClothScene() : nullptr;
	if (!ClothScene || !ClothMesh)
	{
		return;
	}

	FClothInstanceDesc Desc;
	Desc.OwnerComponent = this;
	Desc.Gravity = Gravity;
	Desc.SolverFrequency = SolverFrequency;
	Desc.StiffnessFrequency = StiffnessFrequency;
	Desc.Damping = Damping;
	Desc.LinearDrag = LinearDrag;
	Desc.AngularDrag = AngularDrag;
	Desc.DragCoefficient = DragCoefficient;
	Desc.LiftCoefficient = LiftCoefficient;
	Desc.ConstraintStiffness = ConstraintStiffness;
	Desc.ConstraintStiffnessMultiplier = ConstraintStiffnessMultiplier;
	Desc.CompressionLimit = CompressionLimit;
	Desc.StretchLimit = StretchLimit;
	Desc.TetherConstraintScale = TetherConstraintScale;
	Desc.TetherConstraintStiffness = TetherConstraintStiffness;
	Desc.Friction = Friction;
	Desc.CollisionMassScale = CollisionMassScale;
	Desc.bEnableContinuousCollision = bEnableContinuousCollision;
	Desc.bUseGeodesicTether = bUseGeodesicTether;

	ClothInstance = ClothScene->CreateInstance(ClothMesh.Get(), Desc);
	if (!ClothInstance)
	{
		UE_LOG("[Cloth] Failed to register cloth instance for component %s.", GetName().c_str());
	}
}

void UClothComponent::UnregisterClothInstance()
{
	if (!ClothInstance)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (FClothScene* ClothScene = World->GetClothScene())
		{
			ClothScene->DestroyInstance(ClothInstance);
		}
	}

	ClothInstance = nullptr;
}

void UClothComponent::SetMaterial(UMaterialInterface* InMaterial)
{
	Material = InMaterial;
	MaterialPath = InMaterial ? InMaterial->GetAssetPathFileName() : "None";
	MarkProxyDirty(EDirtyFlag::Material);
}

void UClothComponent::EnsureClothMesh()
{
	if (!ClothMesh)
	{
		ClothMesh = UObjectManager::Get().CreateObject<UClothMesh>(this);
	}
}

void UClothComponent::ResolveMaterial()
{
	if (MaterialPath.IsNull())
	{
		Material = nullptr;
		return;
	}

	Material = FMaterialManager::Get().GetOrCreateMaterialInterface(MaterialPath.ToString());
	MaterialPath.SetCachedObject(Material);
}
