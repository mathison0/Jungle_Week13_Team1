#pragma once

#include "Cloth/ClothCollisionTypes.h"
#include "Cloth/ClothMesh.h"
#include "Component/MeshComponent.h"
#include "Object/Ptr/ObjectPtr.h"
#include "Object/Ptr/SoftObjectPtr.h"

#include "Source/Engine/Component/Primitive/ClothComponent.generated.h"

class FClothInstance;
class FReferenceCollector;
class UMaterialInterface;

UCLASS()
class UClothComponent : public UMeshComponent
{
public:
	GENERATED_BODY()

	UClothComponent() = default;
	~UClothComponent() override;

	void BeginPlay() override;
	void EndPlay() override;
	void PostEditProperty(const char* PropertyName) override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	FMeshDataView GetMeshDataView() const override;
	void UpdateWorldAABB() const override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void RebuildCloth();
	void RegisterClothInstance();
	void UnregisterClothInstance();

	UClothMesh* GetClothMesh() const { return ClothMesh.Get(); }
	FClothInstance* GetClothInstance() const { return ClothInstance; }
	EClothCollisionMode GetClothCollisionMode() const { return CollisionMode; }
	float GetCollisionThickness() const { return CollisionThickness; }

	void SetMaterial(UMaterialInterface* InMaterial);
	UMaterialInterface* GetMaterial() const { return Material; }

private:
	void EnsureClothMesh();
	void ResolveMaterial();

	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Columns", Min=2)
	int32 Columns = 16;
	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Rows", Min=2)
	int32 Rows = 16;
	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Width", Min=1.0f, Speed=1.0f)
	float Width = 7.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Height", Min=1.0f, Speed=1.0f)
	float Height = 7.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Pin Mode", Enum=EClothPinMode)
	EClothPinMode PinMode = EClothPinMode::TopRow;
	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Double Sided")
	bool bDoubleSided = true;

	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Simulate")
	bool bSimulateCloth = true;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Gravity")
	FVector Gravity = FVector(0.0f, 0.0f, -9.81f);
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Solver Frequency", Min=1.0f)
	float SolverFrequency = 120.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Stiffness Frequency", Min=1.0f)
	float StiffnessFrequency = 60.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Damping", Min=0.0f, Max=1.0f)
	float Damping = 0.35f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Linear Drag", Min=0.0f, Max=1.0f)
	float LinearDrag = 0.2f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Angular Drag", Min=0.0f, Max=1.0f)
	float AngularDrag = 0.45f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Drag Coefficient", Min=0.0f, Max=2.0f)
	float DragCoefficient = 0.2f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Lift Coefficient", Min=0.0f, Max=2.0f)
	float LiftCoefficient = 0.05f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Constraint Stiffness", Min=0.0f, Max=1.0f)
	float ConstraintStiffness = 1.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Constraint Stiffness Multiplier", Min=0.0f, Max=2.0f)
	float ConstraintStiffnessMultiplier = 1.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Compression Limit", Min=0.0f, Max=2.0f)
	float CompressionLimit = 1.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Stretch Limit", Min=0.0f, Max=2.0f)
	float StretchLimit = 1.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Tether Scale", Min=0.0f, Max=2.0f)
	float TetherConstraintScale = 1.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Tether Stiffness", Min=0.0f, Max=1.0f)
	float TetherConstraintStiffness = 1.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Simulation", DisplayName="Use Geodesic Tether")
	bool bUseGeodesicTether = false;
	UPROPERTY(Edit, Save, Category="Cloth|Collision", DisplayName="Collision Mode", Enum=EClothCollisionMode)
	EClothCollisionMode CollisionMode = EClothCollisionMode::WorldShapes;
	UPROPERTY(Edit, Save, Category="Cloth|Collision", DisplayName="Friction", Min=0.0f, Max=1.0f)
	float Friction = 0.45f;
	UPROPERTY(Edit, Save, Category="Cloth|Collision", DisplayName="Collision Mass Scale", Min=0.0f, Max=10.0f)
	float CollisionMassScale = 2.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Collision", DisplayName="Collision Thickness", Min=0.0f, Max=0.25f, Speed=0.01f)
	float CollisionThickness = 0.03f;
	UPROPERTY(Edit, Save, Category="Cloth|Collision", DisplayName="Continuous Collision")
	bool bEnableContinuousCollision = true;

	UPROPERTY(Edit, Save, Category="Materials", DisplayName="Material", AssetType="Material")
	FSoftObjectPtr MaterialPath = "None";

	TObjectPtr<UClothMesh> ClothMesh;
	UMaterialInterface* Material = nullptr;
	FClothInstance* ClothInstance = nullptr;
};
