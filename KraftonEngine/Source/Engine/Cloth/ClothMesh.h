#pragma once

#include "Object/Object.h"
#include "Render/Types/VertexTypes.h"

#include "Source/Engine/Cloth/ClothMesh.generated.h"

UENUM()
enum class EClothPinMode : uint8
{
	None = 0,
	TopRow = 1,
	Corners = 2,
	BottomRow = 3,
	LeftColumn = 4,
	RightColumn = 5,
	TopCorners = 6,
	FourCorners = 7,
	Edges = 8,
	GoalFrame = 9
};

struct FClothParticle
{
	FVector Position;
	float InvMass = 1.0f;
};

UCLASS()
class UClothMesh : public UObject
{
public:
	GENERATED_BODY()

	void RebuildGrid();
	void BuildGrid(int32 InColumns, int32 InRows, float InWidth, float InHeight, EClothPinMode InPinMode);
	void UpdateParticlePositions(const TArray<FVector>& InPositions);
	void RecalculateNormals();
	void RecalculateTangents();

	const TArray<FVertexPNCTT>& GetRenderVertices() const { return RenderVertices; }
	const TArray<uint32>& GetRenderIndices() const { return RenderIndices; }
	const TArray<FClothParticle>& GetParticles() const { return Particles; }
	const TArray<uint32>& GetSimulationIndices() const { return SimulationIndices; }

	int32 GetColumnCount() const { return Columns; }
	int32 GetRowCount() const { return Rows; }
	uint32 GetParticleCount() const { return static_cast<uint32>(Particles.size()); }
	bool IsDoubleSided() const { return bDoubleSided; }
	void SetDoubleSided(bool bInDoubleSided) { bDoubleSided = bInDoubleSided; }
	bool IsPinnedIndex(uint32 ParticleIndex) const;

	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Columns", Min=2)
	int32 Columns = 16;
	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Rows", Min=2)
	int32 Rows = 16;
	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Width", Min=1.0f, Speed=1.0f)
	float Width = 300.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Height", Min=1.0f, Speed=1.0f)
	float Height = 300.0f;
	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Pin Mode", Enum=EClothPinMode)
	EClothPinMode PinMode = EClothPinMode::TopRow;
	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Double Sided")
	bool bDoubleSided = true;

private:
	uint32 GetParticleIndex(int32 Column, int32 Row) const;
	void BuildRenderIndicesFromSimulation();
	void SyncDoubleSidedRenderVertices();

	TArray<FVertexPNCTT> RenderVertices;
	TArray<uint32> RenderIndices;
	TArray<FClothParticle> Particles;
	TArray<uint32> SimulationIndices;
};
