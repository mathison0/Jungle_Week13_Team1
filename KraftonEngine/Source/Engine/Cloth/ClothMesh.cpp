#include "Cloth/ClothMesh.h"

#include <algorithm>
#include <cmath>

void UClothMesh::RebuildGrid()
{
	BuildGrid(Columns, Rows, Width, Height, PinMode);
}

void UClothMesh::BuildGrid(int32 InColumns, int32 InRows, float InWidth, float InHeight, EClothPinMode InPinMode)
{
	Columns = (std::max)(2, InColumns);
	Rows = (std::max)(2, InRows);
	Width = (std::max)(1.0f, InWidth);
	Height = (std::max)(1.0f, InHeight);
	PinMode = InPinMode;

	const uint32 VertexCount = static_cast<uint32>(Columns * Rows);
	RenderVertices.clear();
	RenderVertices.reserve(bDoubleSided ? VertexCount * 2 : VertexCount);
	Particles.clear();
	Particles.reserve(VertexCount);
	SimulationIndices.clear();
	SimulationIndices.reserve(static_cast<uint32>((Columns - 1) * (Rows - 1) * 6));
	RenderIndices.clear();

	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const float V = Rows > 1 ? static_cast<float>(Row) / static_cast<float>(Rows - 1) : 0.0f;
		const float Z = Height * 0.5f - V * Height;

		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const float U = Columns > 1 ? static_cast<float>(Column) / static_cast<float>(Columns - 1) : 0.0f;
			const float Y = (U - 0.5f) * Width;
			const FVector Position(0.0f, Y, Z);

			FClothParticle Particle;
			Particle.Position = Position;
			Particle.InvMass = IsPinnedIndex(GetParticleIndex(Column, Row)) ? 0.0f : 1.0f;
			Particles.push_back(Particle);

			FVertexPNCTT Vertex;
			Vertex.Position = Position;
			Vertex.Normal = FVector(1.0f, 0.0f, 0.0f);
			Vertex.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			Vertex.UV = FVector2(U, V);
			Vertex.Tangent = FVector4(0.0f, 1.0f, 0.0f, 1.0f);
			RenderVertices.push_back(Vertex);
		}
	}

	for (int32 Row = 0; Row < Rows - 1; ++Row)
	{
		for (int32 Column = 0; Column < Columns - 1; ++Column)
		{
			const uint32 I0 = GetParticleIndex(Column, Row);
			const uint32 I1 = GetParticleIndex(Column, Row + 1);
			const uint32 I2 = GetParticleIndex(Column + 1, Row);
			const uint32 I3 = GetParticleIndex(Column + 1, Row + 1);

			SimulationIndices.push_back(I0);
			SimulationIndices.push_back(I1);
			SimulationIndices.push_back(I2);

			SimulationIndices.push_back(I2);
			SimulationIndices.push_back(I1);
			SimulationIndices.push_back(I3);
		}
	}

	BuildRenderIndicesFromSimulation();
	RecalculateNormals();
}

void UClothMesh::UpdateParticlePositions(const TArray<FVector>& InPositions)
{
	const uint32 Count = (std::min)(static_cast<uint32>(InPositions.size()), static_cast<uint32>(Particles.size()));
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		Particles[Index].Position = InPositions[Index];
		RenderVertices[Index].Position = InPositions[Index];
	}

	RecalculateNormals();
}

void UClothMesh::RecalculateNormals()
{
	const uint32 BaseVertexCount = static_cast<uint32>(Particles.size());
	for (uint32 Index = 0; Index < BaseVertexCount && Index < RenderVertices.size(); ++Index)
	{
		RenderVertices[Index].Normal = FVector::ZeroVector;
	}

	for (uint32 Index = 0; Index + 2 < SimulationIndices.size(); Index += 3)
	{
		const uint32 I0 = SimulationIndices[Index];
		const uint32 I1 = SimulationIndices[Index + 1];
		const uint32 I2 = SimulationIndices[Index + 2];
		if (I0 >= BaseVertexCount || I1 >= BaseVertexCount || I2 >= BaseVertexCount)
		{
			continue;
		}

		const FVector& P0 = RenderVertices[I0].Position;
		const FVector& P1 = RenderVertices[I1].Position;
		const FVector& P2 = RenderVertices[I2].Position;
		FVector Normal = FVector::Cross(P1 - P0, P2 - P0);
		Normal.Normalize();

		RenderVertices[I0].Normal += Normal;
		RenderVertices[I1].Normal += Normal;
		RenderVertices[I2].Normal += Normal;
	}

	for (uint32 Index = 0; Index < BaseVertexCount && Index < RenderVertices.size(); ++Index)
	{
		FVertexPNCTT& Vertex = RenderVertices[Index];
		if (Vertex.Normal.IsNearlyZero())
		{
			Vertex.Normal = FVector(1.0f, 0.0f, 0.0f);
		}
		else
		{
			Vertex.Normal.Normalize();
		}
	}

	RecalculateTangents();
}

void UClothMesh::RecalculateTangents()
{
	const uint32 BaseVertexCount = static_cast<uint32>(Particles.size());
	for (uint32 Index = 0; Index < BaseVertexCount && Index < RenderVertices.size(); ++Index)
	{
		RenderVertices[Index].Tangent = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	for (uint32 Index = 0; Index + 2 < SimulationIndices.size(); Index += 3)
	{
		const uint32 I0 = SimulationIndices[Index];
		const uint32 I1 = SimulationIndices[Index + 1];
		const uint32 I2 = SimulationIndices[Index + 2];
		if (I0 >= BaseVertexCount || I1 >= BaseVertexCount || I2 >= BaseVertexCount)
		{
			continue;
		}

		const FVector& P0 = RenderVertices[I0].Position;
		const FVector& P1 = RenderVertices[I1].Position;
		const FVector& P2 = RenderVertices[I2].Position;
		const FVector2& UV0 = RenderVertices[I0].UV;
		const FVector2& UV1 = RenderVertices[I1].UV;
		const FVector2& UV2 = RenderVertices[I2].UV;

		const float DeltaU1 = UV1.X - UV0.X;
		const float DeltaV1 = UV1.Y - UV0.Y;
		const float DeltaU2 = UV2.X - UV0.X;
		const float DeltaV2 = UV2.Y - UV0.Y;
		const float Determinant = DeltaU1 * DeltaV2 - DeltaU2 * DeltaV1;
		if (std::fabs(Determinant) <= 1.0e-6f)
		{
			continue;
		}

		FVector Tangent = ((P1 - P0) * DeltaV2 - (P2 - P0) * DeltaV1) / Determinant;
		if (Tangent.IsNearlyZero())
		{
			continue;
		}
		Tangent.Normalize();

		RenderVertices[I0].Tangent.X += Tangent.X;
		RenderVertices[I0].Tangent.Y += Tangent.Y;
		RenderVertices[I0].Tangent.Z += Tangent.Z;
		RenderVertices[I1].Tangent.X += Tangent.X;
		RenderVertices[I1].Tangent.Y += Tangent.Y;
		RenderVertices[I1].Tangent.Z += Tangent.Z;
		RenderVertices[I2].Tangent.X += Tangent.X;
		RenderVertices[I2].Tangent.Y += Tangent.Y;
		RenderVertices[I2].Tangent.Z += Tangent.Z;
	}

	for (uint32 Index = 0; Index < BaseVertexCount && Index < RenderVertices.size(); ++Index)
	{
		FVertexPNCTT& Vertex = RenderVertices[Index];
		FVector Tangent(Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z);
		if (Tangent.IsNearlyZero())
		{
			Tangent = FVector(0.0f, 1.0f, 0.0f);
		}

		Tangent = Tangent - Vertex.Normal * Tangent.Dot(Vertex.Normal);
		if (Tangent.IsNearlyZero())
		{
			Tangent = std::fabs(Vertex.Normal.X) < 0.9f
				? Vertex.Normal.Cross(FVector(1.0f, 0.0f, 0.0f))
				: Vertex.Normal.Cross(FVector(0.0f, 1.0f, 0.0f));
		}
		Tangent.Normalize();
		Vertex.Tangent = FVector4(Tangent.X, Tangent.Y, Tangent.Z, 1.0f);
	}

	SyncDoubleSidedRenderVertices();
}

bool UClothMesh::IsPinnedIndex(uint32 ParticleIndex) const
{
	if (Columns <= 0 || Rows <= 0) return false;

	const int32 Column = static_cast<int32>(ParticleIndex % static_cast<uint32>(Columns));
	const int32 Row = static_cast<int32>(ParticleIndex / static_cast<uint32>(Columns));

	switch (PinMode)
	{
	case EClothPinMode::TopRow:
		return Row == 0;
	case EClothPinMode::Corners:
		return Row == 0 && (Column == 0 || Column == Columns - 1);
	case EClothPinMode::BottomRow:
		return Row == Rows - 1;
	case EClothPinMode::LeftColumn:
		return Column == 0;
	case EClothPinMode::RightColumn:
		return Column == Columns - 1;
	case EClothPinMode::TopCorners:
		return Row == 0 && (Column == 0 || Column == Columns - 1);
	case EClothPinMode::FourCorners:
		return (Column == 0 || Column == Columns - 1) && (Row == 0 || Row == Rows - 1);
	case EClothPinMode::Edges:
		return Column == 0 || Column == Columns - 1 || Row == 0 || Row == Rows - 1;
	case EClothPinMode::GoalFrame:
		return Row == 0 || Column == 0 || Column == Columns - 1;
	default:
		return false;
	}
}

uint32 UClothMesh::GetParticleIndex(int32 Column, int32 Row) const
{
	return static_cast<uint32>(Row * Columns + Column);
}

void UClothMesh::BuildRenderIndicesFromSimulation()
{
	RenderIndices = SimulationIndices;
	if (!bDoubleSided)
	{
		return;
	}

	const uint32 BaseVertexCount = static_cast<uint32>(Particles.size());
	RenderIndices.reserve(SimulationIndices.size() * 2);
	for (uint32 Index = 0; Index + 2 < SimulationIndices.size(); Index += 3)
	{
		RenderIndices.push_back(BaseVertexCount + SimulationIndices[Index + 2]);
		RenderIndices.push_back(BaseVertexCount + SimulationIndices[Index + 1]);
		RenderIndices.push_back(BaseVertexCount + SimulationIndices[Index]);
	}
}

void UClothMesh::SyncDoubleSidedRenderVertices()
{
	const uint32 BaseVertexCount = static_cast<uint32>(Particles.size());
	if (!bDoubleSided)
	{
		if (RenderVertices.size() > BaseVertexCount)
		{
			RenderVertices.resize(BaseVertexCount);
		}
		return;
	}

	RenderVertices.resize(BaseVertexCount * 2);
	for (uint32 Index = 0; Index < BaseVertexCount; ++Index)
	{
		FVertexPNCTT BackVertex = RenderVertices[Index];
		BackVertex.Normal = BackVertex.Normal * -1.0f;
		BackVertex.Tangent.W *= -1.0f;
		RenderVertices[BaseVertexCount + Index] = BackVertex;
	}
}
