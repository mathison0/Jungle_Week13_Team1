#include "MeshEditorWidget.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Runtime/Engine.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Viewport/Viewport.h"
#include "GameFramework/World.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "Settings/EditorSettings.h"
#include "UI/Toolbar/ViewportToolbar.h"
#include "Slate/SlateApplication.h"
#include "Input/InputSystem.h"
#include "Render/Shader/ShaderManager.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Montage/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/Instance/AnimSingleNodeInstance.h"
#include "Animation/AnimationManager.h"
#include "Animation/Sequence/AnimDataModel.h"
#include "Asset/AssetRegistry.h"
#include "UI/Asset/Animation/AnimationTransportBar.h"
#include "UI/Asset/Animation/AnimationTimelinePanel.h"
#include "UI/Asset/Animation/AnimSequencePropertyPanel.h"
#include "UI/Asset/Animation/AnimMontagePropertyPanel.h"
#include "UI/Panel/EditorPropertyRenderer.h"
#include "UI/Util/EditorFileUtils.h"
#include "Editor/UI/Util/EditorTextureManager.h"
#include "Platform/Paths.h"
#include "Object/Object.h"
#include "Object/Reflection/UStruct.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetManager.h"
#include "Physics/PhysicsGeometry.h"
#include "Physics/IPhysicsScene.h"
#include "Mesh/MeshManager.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

// Paths.h가 끌어오는 Windows.h는 GetCurrentTime을 GetTickCount로 치환한다.
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

namespace
{
ID3D11ShaderResourceView* LoadEditorIcon(const wchar_t* FileName)
{
	const FString Path = FPaths::ToUtf8(
		FPaths::Combine(FPaths::AssetDir(), L"Editor/Icons/", FileName));
	return FEditorTextureManager::Get().GetOrLoadIcon(Path);
}

FKShapeElem* GetFirstPhysicsShapeElem(UBodySetup* BodySetup, const char** OutShapeType = nullptr)
{
	if (OutShapeType)
		{
			*OutShapeType = "None";
		}

		if (!BodySetup)
		{
			return nullptr;
		}

		FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
		if (!AggGeom.SphereElems.empty())
		{
			if (OutShapeType) *OutShapeType = "Sphere";
			return &AggGeom.SphereElems[0];
		}
		if (!AggGeom.BoxElems.empty())
		{
			if (OutShapeType) *OutShapeType = "Box";
			return &AggGeom.BoxElems[0];
		}
		if (!AggGeom.SphylElems.empty())
		{
			if (OutShapeType) *OutShapeType = "Capsule";
			return &AggGeom.SphylElems[0];
		}
		return nullptr;
	}

	struct FPhysicsConstraintCandidate
	{
		UBodySetup* BodySetup = nullptr;
		FName ParentBoneName = FName::None;
		FName ChildBoneName = FName::None;
	};

	enum class EManualPhysicsBodyShape : uint8
	{
		Sphere,
		Capsule,
		Box
	};

	FString MakePhysicsGraphNodeKey(const UBodySetup* BodySetup)
	{
		if (!BodySetup)
		{
			return "None";
		}
		return BodySetup->GetBoneName().ToString() + "#" + std::to_string(BodySetup->GetUUID());
	}

	FString MakePhysicsGraphConstraintNodeKey(int32 ConstraintIndex)
	{
		return "Constraint#" + std::to_string(ConstraintIndex);
	}

	FString TruncatePhysicsGraphTextToWidth(const FString& Text, float MaxWidth)
	{
		if (ImGui::CalcTextSize(Text.c_str()).x <= MaxWidth)
		{
			return Text;
		}

		const FString Ellipsis = "...";
		FString Result = Text;
		while (!Result.empty() && ImGui::CalcTextSize((Result + Ellipsis).c_str()).x > MaxWidth)
		{
			Result.pop_back();
		}
		return Result.empty() ? Ellipsis : Result + Ellipsis;
	}

	TArray<FString> WrapPhysicsGraphText(const FString& Text, float MaxWidth, int32 MaxLines)
	{
		TArray<FString> Lines;
		FString Remaining = Text;
		while (!Remaining.empty() && static_cast<int32>(Lines.size()) < MaxLines)
		{
			if (static_cast<int32>(Lines.size()) == MaxLines - 1)
			{
				Lines.push_back(TruncatePhysicsGraphTextToWidth(Remaining, MaxWidth));
				break;
			}

			FString Candidate = Remaining;
			while (!Candidate.empty() && ImGui::CalcTextSize(Candidate.c_str()).x > MaxWidth)
			{
				Candidate.pop_back();
			}
			if (Candidate.empty())
			{
				Candidate = Remaining.substr(0, 1);
			}

			int32 BreakIndex = static_cast<int32>(Candidate.size());
			for (int32 i = BreakIndex - 1; i > 0; --i)
			{
				const char Ch = Candidate[i];
				if (Ch == '_' || Ch == '-' || Ch == ' ')
				{
					BreakIndex = i + 1;
					break;
				}
			}

			Lines.push_back(Remaining.substr(0, BreakIndex));
			Remaining.erase(0, BreakIndex);
			while (!Remaining.empty() && Remaining.front() == ' ')
			{
				Remaining.erase(0, 1);
			}
		}

		if (Lines.empty())
		{
			Lines.push_back("");
		}
		return Lines;
	}

	struct FPhysicsGraphNodeVisual
	{
		ImVec2 Size = ImVec2(0.0f, 0.0f);
		TArray<FString> Lines;
	};

	FPhysicsGraphNodeVisual MakePhysicsGraphNodeVisual(const FString& PrimaryText, const FString& SecondaryText, float Width, float MinHeight, float TextScale = 1.0f)
	{
		const float TextPaddingX = 12.0f;
		const float TextPaddingY = 7.0f;
		const float LineGap = 2.0f;
		const float TextWidth = std::max(16.0f, Width - TextPaddingX * 2.0f);
		const float SafeTextScale = std::max(0.1f, TextScale);

		FPhysicsGraphNodeVisual Visual;
		TArray<FString> PrimaryLines = WrapPhysicsGraphText(PrimaryText, TextWidth / SafeTextScale, 2);
		for (const FString& Line : PrimaryLines)
		{
			Visual.Lines.push_back(Line);
		}
		if (!SecondaryText.empty())
		{
			Visual.Lines.push_back(TruncatePhysicsGraphTextToWidth(SecondaryText, TextWidth / SafeTextScale));
		}

		const float LineHeight = ImGui::GetTextLineHeight() * SafeTextScale;
		const float ScaledLineGap = LineGap * SafeTextScale;
		const float TextHeight = static_cast<float>(Visual.Lines.size()) * LineHeight
			+ std::max(0, static_cast<int32>(Visual.Lines.size()) - 1) * ScaledLineGap;
		Visual.Size = ImVec2(Width, std::max(MinHeight, TextHeight + TextPaddingY * SafeTextScale * 2.0f));
		return Visual;
	}

	void DrawCenteredPhysicsGraphText(ImDrawList* DrawList, const ImVec2& NodePos, const FPhysicsGraphNodeVisual& Visual, float Zoom, float TextScale, ImU32 PrimaryColor, ImU32 SecondaryColor)
	{
		if (!DrawList || Visual.Lines.empty())
		{
			return;
		}

		ImFont* Font = ImGui::GetFont();
		const float EffectiveTextScale = Zoom * std::max(0.1f, TextScale);
		const float FontSize = ImGui::GetFontSize() * EffectiveTextScale;
		const float LineHeight = ImGui::GetTextLineHeight() * EffectiveTextScale;
		const float LineGap = 2.0f * EffectiveTextScale;
		const ImVec2 ScaledNodeSize(Visual.Size.x * Zoom, Visual.Size.y * Zoom);
		const float TextHeight = static_cast<float>(Visual.Lines.size()) * LineHeight
			+ std::max(0, static_cast<int32>(Visual.Lines.size()) - 1) * LineGap;
		float Y = NodePos.y + (ScaledNodeSize.y - TextHeight) * 0.5f;
		for (int32 LineIndex = 0; LineIndex < static_cast<int32>(Visual.Lines.size()); ++LineIndex)
		{
			const FString& Line = Visual.Lines[LineIndex];
			const ImVec2 TextSize = ImGui::CalcTextSize(Line.c_str());
			const float X = NodePos.x + (ScaledNodeSize.x - TextSize.x * EffectiveTextScale) * 0.5f;
			DrawList->AddText(Font, FontSize, ImVec2(X, Y), LineIndex == 0 ? PrimaryColor : SecondaryColor, Line.c_str());
			Y += LineHeight + LineGap;
		}
	}

	int32 FindBoneIndexByName(const FSkeletalMesh* Asset, const FName& BoneName)
	{
		if (!Asset)
		{
			return -1;
		}

		const FString BoneNameString = BoneName.ToString();
		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
		{
			if (Asset->Bones[BoneIndex].Name == BoneNameString)
			{
				return BoneIndex;
			}
		}
		return -1;
	}

	struct FManualPhysicsBodyMetrics
	{
		FVector Center = FVector::ZeroVector;
		FVector CapsuleCenter = FVector::ZeroVector;
		FVector BoxExtent = FVector(0.1f, 0.1f, 0.1f);
		float SphereRadius = 0.1f;
		float CapsuleRadius = 0.05f;
		float CapsuleLength = 0.2f;
		FQuat CapsuleRotation = FQuat::Identity;
	};

	struct FManualBoneVertexSample
	{
		FVector LocalPosition = FVector::ZeroVector;
	};

	bool IsManualBodyFiniteVector(const FVector& Value)
	{
		return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
	}

	float ManualBodyPercentile(TArray<float> Values, float Alpha)
	{
		if (Values.empty())
		{
			return 0.0f;
		}

		std::sort(Values.begin(), Values.end());

		const float ClampedAlpha = std::clamp(Alpha, 0.0f, 1.0f);
		const float ScaledIndex = ClampedAlpha * static_cast<float>(Values.size() - 1);
		const int32 LowerIndex = static_cast<int32>(std::floor(ScaledIndex));
		const int32 UpperIndex = static_cast<int32>(std::ceil(ScaledIndex));
		const float Blend = ScaledIndex - static_cast<float>(LowerIndex);

		if (LowerIndex == UpperIndex)
		{
			return Values[LowerIndex];
		}
		return Values[LowerIndex] + (Values[UpperIndex] - Values[LowerIndex]) * Blend;
	}

	FVector ManualBodyMaxVector(const FVector& Value, float MinValue)
	{
		return FVector(
			std::max(Value.X, MinValue),
			std::max(Value.Y, MinValue),
			std::max(Value.Z, MinValue));
	}

	FVector ComputeManualBodySampleExtent(const TArray<FVector>& Points)
	{
		if (Points.empty())
		{
			return FVector::ZeroVector;
		}

		FVector Min = Points[0];
		FVector Max = Points[0];
		for (const FVector& Point : Points)
		{
			Min.X = std::min(Min.X, Point.X);
			Min.Y = std::min(Min.Y, Point.Y);
			Min.Z = std::min(Min.Z, Point.Z);
			Max.X = std::max(Max.X, Point.X);
			Max.Y = std::max(Max.Y, Point.Y);
			Max.Z = std::max(Max.Z, Point.Z);
		}
		return Max - Min;
	}

	void BuildManualBodyPercentileBounds(
		const TArray<FManualBoneVertexSample>& Samples,
		float LowerPercentile,
		float UpperPercentile,
		FVector& OutMin,
		FVector& OutMax)
	{
		TArray<float> Xs;
		TArray<float> Ys;
		TArray<float> Zs;
		Xs.reserve(Samples.size());
		Ys.reserve(Samples.size());
		Zs.reserve(Samples.size());

		for (const FManualBoneVertexSample& Sample : Samples)
		{
			Xs.push_back(Sample.LocalPosition.X);
			Ys.push_back(Sample.LocalPosition.Y);
			Zs.push_back(Sample.LocalPosition.Z);
		}

		OutMin = FVector(
			ManualBodyPercentile(Xs, LowerPercentile),
			ManualBodyPercentile(Ys, LowerPercentile),
			ManualBodyPercentile(Zs, LowerPercentile));
		OutMax = FVector(
			ManualBodyPercentile(Xs, UpperPercentile),
			ManualBodyPercentile(Ys, UpperPercentile),
			ManualBodyPercentile(Zs, UpperPercentile));
	}

	FVector GetManualBodyLongestLocalAxis(const FVector& Extent)
	{
		if (Extent.X >= Extent.Y && Extent.X >= Extent.Z)
		{
			return FVector::XAxisVector;
		}
		if (Extent.Y >= Extent.X && Extent.Y >= Extent.Z)
		{
			return FVector::YAxisVector;
		}
		return FVector::ZAxisVector;
	}

	FQuat MakeManualBodyQuatFromZAxis(const FVector& InAxis)
	{
		FVector Axis = InAxis;
		if (Axis.IsNearlyZero())
		{
			return FQuat::Identity;
		}
		Axis.Normalize();

		const FVector From = FVector::ZAxisVector;
		const float Dot = std::clamp(From.Dot(Axis), -1.0f, 1.0f);
		if (Dot > 0.9999f)
		{
			return FQuat::Identity;
		}
		if (Dot < -0.9999f)
		{
			return FQuat::FromAxisAngle(FVector::XAxisVector, 3.14159265358979323846f);
		}

		FVector RotationAxis = From.Cross(Axis);
		RotationAxis.Normalize();

		FQuat Result = FQuat::FromAxisAngle(RotationAxis, std::acos(Dot));
		Result.Normalize();
		return Result;
	}

	int32 FindManualBodyBestChildBone(
		const FSkeletalMesh& Mesh,
		const TArray<TArray<int32>>& ChildrenByBone,
		int32 BoneIndex)
	{
		int32 BestChild = -1;
		float BestDistanceSq = 0.0f;

		const FMatrix BoneBindInverse = Mesh.Bones[BoneIndex].GetInverseBindPose();
		for (const int32 ChildIndex : ChildrenByBone[BoneIndex])
		{
			if (ChildIndex < 0 || ChildIndex >= static_cast<int32>(Mesh.Bones.size()))
			{
				continue;
			}

			const FVector ChildLocal = Mesh.Bones[ChildIndex].GetSkinBindGlobalPose().GetLocation() * BoneBindInverse;
			const float DistanceSq = ChildLocal.LengthSquared();
			if (DistanceSq > BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				BestChild = ChildIndex;
			}
		}

		return BestChild;
	}

	float GetFallbackManualBodySize(const FSkeletalMesh* Asset, int32 BoneIndex)
	{
		if (!Asset || BoneIndex < 0 || BoneIndex >= static_cast<int32>(Asset->Bones.size()))
		{
			return 0.1f;
		}

		const FVector BoneLocation = Asset->Bones[BoneIndex].GetReferenceGlobalPose().GetLocation();
		float BestChildDistance = 0.0f;
		for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(Asset->Bones.size()); ++ChildIndex)
		{
			if (Asset->Bones[ChildIndex].ParentIndex != BoneIndex)
			{
				continue;
			}

			const float Distance = FVector::Distance(BoneLocation, Asset->Bones[ChildIndex].GetReferenceGlobalPose().GetLocation());
			if (Distance > BestChildDistance)
			{
				BestChildDistance = Distance;
			}
		}

		return std::max(0.04f, BestChildDistance > 0.0f ? BestChildDistance * 0.25f : 0.1f);
	}

	bool CollectDirectManualBodySamples(
		const FSkeletalMesh& Mesh,
		int32 BoneIndex,
		const FPhysicsAssetAutoGenerateSettings& Settings,
		TArray<FManualBoneVertexSample>& OutSamples)
	{
		const int32 BoneCount = static_cast<int32>(Mesh.Bones.size());
		const FMatrix& BoneBindInverse = Mesh.Bones[BoneIndex].GetInverseBindPose();
		OutSamples.clear();
		OutSamples.reserve(Mesh.Vertices.size());

		for (const FVertexPNCTBW& Vertex : Mesh.Vertices)
		{
			if (!IsManualBodyFiniteVector(Vertex.Position))
			{
				continue;
			}

			int32 BestBoneIndex = -1;
			float BestWeight = 0.0f;
			for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
			{
				const int32 InfluencedBoneIndex = Vertex.BoneIndices[InfluenceIndex];
				const float Weight = Vertex.BoneWeights[InfluenceIndex];
				if (InfluencedBoneIndex >= 0 && InfluencedBoneIndex < BoneCount && Weight > BestWeight)
				{
					BestBoneIndex = InfluencedBoneIndex;
					BestWeight = Weight;
				}
			}

			if (BestBoneIndex == BoneIndex && BestWeight >= Settings.MinBoneWeight)
			{
				OutSamples.push_back({ Vertex.Position * BoneBindInverse });
			}
		}

		const int32 RequiredMinVertexCount = std::max(Settings.MinVertexCount, 1);
		if (static_cast<int32>(OutSamples.size()) >= RequiredMinVertexCount)
		{
			return true;
		}

		OutSamples.clear();
		for (const FVertexPNCTBW& Vertex : Mesh.Vertices)
		{
			if (!IsManualBodyFiniteVector(Vertex.Position))
			{
				continue;
			}

			for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
			{
				if (Vertex.BoneIndices[InfluenceIndex] == BoneIndex
					&& Vertex.BoneWeights[InfluenceIndex] >= Settings.MinBoneWeight)
				{
					OutSamples.push_back({ Vertex.Position * BoneBindInverse });
					break;
				}
			}
		}

		return static_cast<int32>(OutSamples.size()) >= RequiredMinVertexCount;
	}

	bool CollectAutoGenerateLikeManualBodySamples(
		const FSkeletalMesh& Mesh,
		int32 TargetBoneIndex,
		const FPhysicsAssetAutoGenerateSettings& Settings,
		TArray<FManualBoneVertexSample>& OutSamples,
		TArray<TArray<int32>>& OutChildrenByBone)
	{
		const int32 BoneCount = static_cast<int32>(Mesh.Bones.size());
		if (BoneCount <= 0 || Mesh.Vertices.empty())
		{
			return false;
		}

		const float MinBoneSizeRatio = std::max(Settings.MinBoneSizeRatio, 0.0f);
		const int32 RequiredMinVertexCount = std::max(Settings.MinVertexCount, 1);

		TArray<FMatrix> BoneBindInverse;
		BoneBindInverse.resize(BoneCount);
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			BoneBindInverse[BoneIndex] = Mesh.Bones[BoneIndex].GetInverseBindPose();
		}

		OutChildrenByBone.clear();
		OutChildrenByBone.resize(BoneCount);
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			const int32 ParentIndex = Mesh.Bones[BoneIndex].ParentIndex;
			if (ParentIndex >= 0 && ParentIndex < BoneCount)
			{
				OutChildrenByBone[ParentIndex].push_back(BoneIndex);
			}
		}

		TArray<TArray<FVector>> CompSamplesByBone;
		CompSamplesByBone.resize(BoneCount);

		FVector MeshMin = FVector::ZeroVector;
		FVector MeshMax = FVector::ZeroVector;
		bool bMeshBoundsValid = false;

		for (const FVertexPNCTBW& Vertex : Mesh.Vertices)
		{
			if (!IsManualBodyFiniteVector(Vertex.Position))
			{
				continue;
			}

			if (!bMeshBoundsValid)
			{
				MeshMin = Vertex.Position;
				MeshMax = Vertex.Position;
				bMeshBoundsValid = true;
			}
			else
			{
				MeshMin.X = std::min(MeshMin.X, Vertex.Position.X);
				MeshMin.Y = std::min(MeshMin.Y, Vertex.Position.Y);
				MeshMin.Z = std::min(MeshMin.Z, Vertex.Position.Z);
				MeshMax.X = std::max(MeshMax.X, Vertex.Position.X);
				MeshMax.Y = std::max(MeshMax.Y, Vertex.Position.Y);
				MeshMax.Z = std::max(MeshMax.Z, Vertex.Position.Z);
			}

			if (Settings.bUseDominantBoneOnly)
			{
				int32 BestBoneIndex = -1;
				float BestWeight = 0.0f;
				for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
				{
					const int32 InfluencedBoneIndex = Vertex.BoneIndices[InfluenceIndex];
					const float Weight = Vertex.BoneWeights[InfluenceIndex];
					if (InfluencedBoneIndex >= 0 && InfluencedBoneIndex < BoneCount && Weight > BestWeight)
					{
						BestBoneIndex = InfluencedBoneIndex;
						BestWeight = Weight;
					}
				}

				if (BestBoneIndex >= 0 && BestWeight >= Settings.MinBoneWeight)
				{
					CompSamplesByBone[BestBoneIndex].push_back(Vertex.Position);
				}
			}
			else
			{
				for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
				{
					const int32 InfluencedBoneIndex = Vertex.BoneIndices[InfluenceIndex];
					const float Weight = Vertex.BoneWeights[InfluenceIndex];
					if (InfluencedBoneIndex >= 0 && InfluencedBoneIndex < BoneCount && Weight >= Settings.MinBoneWeight)
					{
						CompSamplesByBone[InfluencedBoneIndex].push_back(Vertex.Position);
					}
				}
			}
		}

		const FVector MeshSize = bMeshBoundsValid ? (MeshMax - MeshMin) : FVector::ZeroVector;
		const float MeshExtentMax = std::max({ MeshSize.X, MeshSize.Y, MeshSize.Z });

		TArray<int32> BoneDepth;
		BoneDepth.resize(BoneCount, 0);
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			const int32 Parent = Mesh.Bones[BoneIndex].ParentIndex;
			BoneDepth[BoneIndex] = (Parent >= 0 && Parent < BoneIndex) ? BoneDepth[Parent] + 1 : 0;
		}

		TArray<bool> bGetsBody;
		bGetsBody.resize(BoneCount, false);
		for (int32 BoneIndex = BoneCount - 1; BoneIndex >= 0; --BoneIndex)
		{
			const FBone& Bone = Mesh.Bones[BoneIndex];
			const int32 Parent = Bone.ParentIndex;

			const FVector SampleSize = ComputeManualBodySampleExtent(CompSamplesByBone[BoneIndex]);
			const float SampleExtentMax = std::max({ SampleSize.X, SampleSize.Y, SampleSize.Z });
			const float SizeRatio = (MeshExtentMax > 1.0e-6f) ? (SampleExtentMax / MeshExtentMax) : 0.0f;

			const bool bTooSmall = SizeRatio < MinBoneSizeRatio;
			const bool bTooDeep = Settings.MaxBoneDepth > 0 && BoneDepth[BoneIndex] > Settings.MaxBoneDepth;
			const bool bTooFew = CompSamplesByBone[BoneIndex].size() < static_cast<size_t>(RequiredMinVertexCount);

			if (Parent >= 0 && (bTooSmall || bTooDeep || bTooFew))
			{
				TArray<FVector>& ParentSamples = CompSamplesByBone[Parent];
				ParentSamples.insert(ParentSamples.end(), CompSamplesByBone[BoneIndex].begin(), CompSamplesByBone[BoneIndex].end());
				continue;
			}

			bGetsBody[BoneIndex] = true;
		}

		if (Settings.MaxBodyCount > 0)
		{
			struct FBodyCandidate { int32 BoneIndex; float Volume; };
			TArray<FBodyCandidate> Candidates;
			for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
			{
				if (!bGetsBody[BoneIndex])
				{
					continue;
				}
				const FVector Size = ComputeManualBodySampleExtent(CompSamplesByBone[BoneIndex]);
				Candidates.push_back({ BoneIndex, Size.X * Size.Y * Size.Z });
			}

			if (static_cast<int32>(Candidates.size()) > Settings.MaxBodyCount)
			{
				std::sort(Candidates.begin(), Candidates.end(),
					[](const FBodyCandidate& A, const FBodyCandidate& B) { return A.Volume < B.Volume; });

				const int32 DemoteCount = static_cast<int32>(Candidates.size()) - Settings.MaxBodyCount;
				for (int32 i = 0; i < DemoteCount; ++i)
				{
					const int32 BoneIndex = Candidates[i].BoneIndex;
					int32 Ancestor = Mesh.Bones[BoneIndex].ParentIndex;
					while (Ancestor >= 0 && !bGetsBody[Ancestor])
					{
						Ancestor = Mesh.Bones[Ancestor].ParentIndex;
					}

					bGetsBody[BoneIndex] = false;
					if (Ancestor >= 0)
					{
						TArray<FVector>& Dst = CompSamplesByBone[Ancestor];
						Dst.insert(Dst.end(), CompSamplesByBone[BoneIndex].begin(), CompSamplesByBone[BoneIndex].end());
					}
				}
			}
		}

		if (TargetBoneIndex < 0 || TargetBoneIndex >= BoneCount || !bGetsBody[TargetBoneIndex])
		{
			return false;
		}

		OutSamples.clear();
		OutSamples.reserve(CompSamplesByBone[TargetBoneIndex].size());
		const FMatrix& InvBind = BoneBindInverse[TargetBoneIndex];
		for (const FVector& Position : CompSamplesByBone[TargetBoneIndex])
		{
			OutSamples.push_back({ Position * InvBind });
		}
		return static_cast<int32>(OutSamples.size()) >= RequiredMinVertexCount;
	}

	FManualPhysicsBodyMetrics BuildManualPhysicsBodyMetricsFromSamples(
		const FSkeletalMesh& Mesh,
		int32 BoneIndex,
		EManualPhysicsBodyShape Shape,
		const TArray<FManualBoneVertexSample>& Samples,
		const TArray<TArray<int32>>& ChildrenByBone,
		const FPhysicsAssetAutoGenerateSettings& Settings)
	{
		FManualPhysicsBodyMetrics Metrics;
		const float LowerPercentile = std::clamp(Settings.LowerPercentile, 0.0f, 0.49f);
		const float UpperPercentile = std::clamp(Settings.UpperPercentile, LowerPercentile + 0.01f, 1.0f);
		const float MinShapeSize = std::max(Settings.MinShapeSize, 0.001f);
		const float ShapePadding = std::max(Settings.ShapePadding, 1.0f);

		FVector LocalMin;
		FVector LocalMax;
		BuildManualBodyPercentileBounds(Samples, LowerPercentile, UpperPercentile, LocalMin, LocalMax);

		const FVector Center = (LocalMin + LocalMax) * 0.5f;
		const FVector Extent = ManualBodyMaxVector((LocalMax - LocalMin) * (0.5f * ShapePadding), MinShapeSize);
		if (!IsManualBodyFiniteVector(Center) || !IsManualBodyFiniteVector(Extent))
		{
			return Metrics;
		}

		Metrics.Center = Center;
		Metrics.BoxExtent = Extent;

		if (Shape == EManualPhysicsBodyShape::Sphere)
		{
			TArray<float> Distances;
			Distances.reserve(Samples.size());
			for (const FManualBoneVertexSample& Sample : Samples)
			{
				Distances.push_back((Sample.LocalPosition - Center).Length());
			}
			Metrics.SphereRadius = std::max(ManualBodyPercentile(Distances, UpperPercentile) * ShapePadding, MinShapeSize);
			return Metrics;
		}

		if (Shape != EManualPhysicsBodyShape::Capsule)
		{
			return Metrics;
		}

		const FMatrix& BoneBindInverse = Mesh.Bones[BoneIndex].GetInverseBindPose();
		const int32 ChildIndex = FindManualBodyBestChildBone(Mesh, ChildrenByBone, BoneIndex);
		FVector CapsuleAxis = GetManualBodyLongestLocalAxis(Extent);
		bool bHasUsableAxis = false;
		if (ChildIndex >= 0)
		{
			CapsuleAxis = Mesh.Bones[ChildIndex].GetSkinBindGlobalPose().GetLocation() * BoneBindInverse;
			bHasUsableAxis = !CapsuleAxis.IsNearlyZero(MinShapeSize);
			if (bHasUsableAxis)
			{
				CapsuleAxis.Normalize();
			}
		}
		else
		{
			bHasUsableAxis = !CapsuleAxis.IsNearlyZero();
			if (bHasUsableAxis)
			{
				CapsuleAxis.Normalize();
			}
		}

		if (!bHasUsableAxis)
		{
			CapsuleAxis = FVector::ZAxisVector;
		}

		TArray<float> Projections;
		Projections.reserve(Samples.size());
		for (const FManualBoneVertexSample& Sample : Samples)
		{
			Projections.push_back(Sample.LocalPosition.Dot(CapsuleAxis));
		}

		const float MinProjection = ManualBodyPercentile(Projections, LowerPercentile);
		const float MaxProjection = ManualBodyPercentile(Projections, UpperPercentile);
		const float CenterProjection = (MinProjection + MaxProjection) * 0.5f;

		FVector PerpCenter = FVector::ZeroVector;
		int32 PerpCount = 0;
		for (const FManualBoneVertexSample& Sample : Samples)
		{
			const float Projection = Sample.LocalPosition.Dot(CapsuleAxis);
			if (Projection < MinProjection || Projection > MaxProjection)
			{
				continue;
			}
			PerpCenter += Sample.LocalPosition - CapsuleAxis * Projection;
			++PerpCount;
		}
		if (PerpCount > 0)
		{
			PerpCenter /= static_cast<float>(PerpCount);
		}

		TArray<float> Radii;
		Radii.reserve(Samples.size());
		for (const FManualBoneVertexSample& Sample : Samples)
		{
			const float Projection = Sample.LocalPosition.Dot(CapsuleAxis);
			if (Projection < MinProjection || Projection > MaxProjection)
			{
				continue;
			}
			const FVector AxisPoint = PerpCenter + CapsuleAxis * Projection;
			Radii.push_back((Sample.LocalPosition - AxisPoint).Length());
		}

		const float Radius = std::max(ManualBodyPercentile(Radii, UpperPercentile) * ShapePadding, MinShapeSize);
		const float ProjectionSpan = std::max(MaxProjection - MinProjection, MinShapeSize);
		Metrics.CapsuleRadius = Radius;
		Metrics.CapsuleLength = std::max(ProjectionSpan - Radius * 2.0f, MinShapeSize);
		Metrics.CapsuleCenter = PerpCenter + CapsuleAxis * CenterProjection;
		Metrics.CapsuleRotation = MakeManualBodyQuatFromZAxis(CapsuleAxis);
		return Metrics;
	}

	FManualPhysicsBodyMetrics BuildManualPhysicsBodyMetricsFromAutoGenerateFormula(
		const FSkeletalMesh* Asset,
		int32 BoneIndex,
		EManualPhysicsBodyShape Shape,
		const FPhysicsAssetAutoGenerateSettings& Settings)
	{
		FManualPhysicsBodyMetrics Metrics;
		if (!Asset || BoneIndex < 0 || BoneIndex >= static_cast<int32>(Asset->Bones.size()) || Asset->Vertices.empty())
		{
			const float FallbackSize = GetFallbackManualBodySize(Asset, BoneIndex);
			Metrics.BoxExtent = FVector(FallbackSize, FallbackSize, FallbackSize);
			Metrics.SphereRadius = FallbackSize;
			Metrics.CapsuleRadius = FallbackSize * 0.5f;
			Metrics.CapsuleLength = FallbackSize * 2.0f;
			return Metrics;
		}

		TArray<TArray<int32>> ChildrenByBone;
		TArray<FManualBoneVertexSample> Samples;
		const bool bHasAutoGenerateSamples = CollectAutoGenerateLikeManualBodySamples(*Asset, BoneIndex, Settings, Samples, ChildrenByBone);
		if (!bHasAutoGenerateSamples)
		{
			ChildrenByBone.clear();
			ChildrenByBone.resize(Asset->Bones.size());
			for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(Asset->Bones.size()); ++ChildIndex)
			{
				const int32 ParentIndex = Asset->Bones[ChildIndex].ParentIndex;
				if (ParentIndex >= 0 && ParentIndex < static_cast<int32>(Asset->Bones.size()))
				{
					ChildrenByBone[ParentIndex].push_back(ChildIndex);
				}
			}
			CollectDirectManualBodySamples(*Asset, BoneIndex, Settings, Samples);
		}

		if (static_cast<int32>(Samples.size()) < std::max(Settings.MinVertexCount, 1))
		{
			const float FallbackSize = GetFallbackManualBodySize(Asset, BoneIndex);
			Metrics.BoxExtent = FVector(FallbackSize, FallbackSize, FallbackSize);
			Metrics.SphereRadius = FallbackSize;
			Metrics.CapsuleRadius = FallbackSize * 0.5f;
			Metrics.CapsuleLength = FallbackSize * 2.0f;
			return Metrics;
		}

		return BuildManualPhysicsBodyMetricsFromSamples(*Asset, BoneIndex, Shape, Samples, ChildrenByBone, Settings);
	}

	UBodySetup* AddManualPhysicsBodyForBone(
		USkeletalMesh* SkeletalMesh,
		int32 BoneIndex,
		EManualPhysicsBodyShape Shape,
		const FPhysicsAssetAutoGenerateSettings& Settings)
	{
		FSkeletalMesh* Asset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
		UPhysicsAsset* PhysAsset = SkeletalMesh ? SkeletalMesh->EnsurePhysicsAsset() : nullptr;
		if (!SkeletalMesh || !Asset || !PhysAsset || BoneIndex < 0 || BoneIndex >= static_cast<int32>(Asset->Bones.size()))
		{
			return nullptr;
		}

		const FBone& Bone = Asset->Bones[BoneIndex];
		const FName BoneName(Bone.Name);

		UBodySetup* BodySetup = UObjectManager::Get().CreateObject<UBodySetup>(PhysAsset);
		if (!BodySetup)
		{
			return nullptr;
		}

		const FManualPhysicsBodyMetrics Metrics = BuildManualPhysicsBodyMetricsFromAutoGenerateFormula(Asset, BoneIndex, Shape, Settings);
		const int32 BodyOrdinal = static_cast<int32>(PhysAsset->BodySetups.size());
		BodySetup->SetBoneName(BoneName);
		BodySetup->SetFName(FName(Bone.Name + "_BodySetup_" + std::to_string(BodyOrdinal)));

		FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
		if (Shape == EManualPhysicsBodyShape::Sphere)
		{
			FKSphereElem Sphere;
			Sphere.Name = Bone.Name + "_SphereBody_" + std::to_string(BodyOrdinal);
			Sphere.Radius = Metrics.SphereRadius;
			Sphere.Transform.Location = Metrics.Center;
			Sphere.Transform.Rotation = FQuat::Identity;
			Sphere.Transform.Scale = FVector::OneVector;
			AggGeom.SphereElems.push_back(Sphere);
		}
		else if (Shape == EManualPhysicsBodyShape::Capsule)
		{
			FKSphylElem Capsule;
			Capsule.Name = Bone.Name + "_CapsuleBody_" + std::to_string(BodyOrdinal);
			Capsule.Radius = Metrics.CapsuleRadius;
			Capsule.Length = Metrics.CapsuleLength;
			Capsule.Transform.Location = Metrics.CapsuleCenter;
			Capsule.Transform.Rotation = Metrics.CapsuleRotation;
			Capsule.Transform.Scale = FVector::OneVector;
			AggGeom.SphylElems.push_back(Capsule);
		}
		else
		{
			FKBoxElem Box;
			Box.Name = Bone.Name + "_BoxBody_" + std::to_string(BodyOrdinal);
			Box.Extent = Metrics.BoxExtent;
			Box.Transform.Location = Metrics.Center;
			Box.Transform.Rotation = FQuat::Identity;
			Box.Transform.Scale = FVector::OneVector;
			AggGeom.BoxElems.push_back(Box);
		}

		PhysAsset->BodySetups.push_back(BodySetup);
		return BodySetup;
	}

	bool RemovePhysicsBodyAndConstraints(UPhysicsAsset* PhysAsset, UBodySetup* BodySetup)
	{
		if (!PhysAsset || !BodySetup)
		{
			return false;
		}

		const FName BoneName = BodySetup->GetBoneName();
		bool bRemoved = false;
		bool bHasOtherBodyForBone = false;
		for (UBodySetup* OtherBodySetup : PhysAsset->BodySetups)
		{
			if (OtherBodySetup && OtherBodySetup != BodySetup && OtherBodySetup->GetBoneName() == BoneName)
			{
				bHasOtherBodyForBone = true;
				break;
			}
		}

		if (!bHasOtherBodyForBone)
		{
			for (int32 ConstraintIndex = static_cast<int32>(PhysAsset->ConstraintSetups.size()) - 1; ConstraintIndex >= 0; --ConstraintIndex)
			{
				const FConstraintSetup& Constraint = PhysAsset->ConstraintSetups[ConstraintIndex];
				if (Constraint.ParentBoneName == BoneName || Constraint.ChildBoneName == BoneName)
				{
					PhysAsset->ConstraintSetups.erase(PhysAsset->ConstraintSetups.begin() + ConstraintIndex);
					bRemoved = true;
				}
			}
		}

		for (int32 BodyIndex = static_cast<int32>(PhysAsset->BodySetups.size()) - 1; BodyIndex >= 0; --BodyIndex)
		{
			if (PhysAsset->BodySetups[BodyIndex] == BodySetup)
			{
				PhysAsset->BodySetups.erase(PhysAsset->BodySetups.begin() + BodyIndex);
				UObjectManager::Get().DestroyObject(BodySetup);
				bRemoved = true;
				break;
			}
		}
		return bRemoved;
	}

	TArray<FPhysicsConstraintCandidate> BuildStrictConstraintCandidates(
		USkeletalMesh* SkeletalMesh,
		UPhysicsAsset* PhysAsset,
		UBodySetup* SourceBody)
	{
		TArray<FPhysicsConstraintCandidate> Candidates;
		FSkeletalMesh* Asset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
		if (!SkeletalMesh || !PhysAsset || !SourceBody || !Asset)
		{
			return Candidates;
		}

		const int32 SourceBoneIndex = FindBoneIndexByName(Asset, SourceBody->GetBoneName());
		if (SourceBoneIndex < 0 || SourceBoneIndex >= static_cast<int32>(Asset->Bones.size()))
		{
			return Candidates;
		}

		auto TryAddCandidate = [&](int32 CandidateBoneIndex)
		{
			if (CandidateBoneIndex < 0 || CandidateBoneIndex >= static_cast<int32>(Asset->Bones.size()))
			{
				return;
			}

			const FName CandidateBoneName(Asset->Bones[CandidateBoneIndex].Name);
			UBodySetup* CandidateBody = PhysAsset->FindBodySetupByBoneName(CandidateBoneName);
			if (!CandidateBody || SkeletalMesh->HasPhysicsConstraintBetweenBodies(SourceBody->GetBoneName(), CandidateBoneName))
			{
				return;
			}

			FPhysicsConstraintCandidate Candidate;
			Candidate.BodySetup = CandidateBody;
			if (Asset->Bones[SourceBoneIndex].ParentIndex == CandidateBoneIndex)
			{
				Candidate.ParentBoneName = CandidateBoneName;
				Candidate.ChildBoneName = SourceBody->GetBoneName();
			}
			else if (Asset->Bones[CandidateBoneIndex].ParentIndex == SourceBoneIndex)
			{
				Candidate.ParentBoneName = SourceBody->GetBoneName();
				Candidate.ChildBoneName = CandidateBoneName;
			}
			else
			{
				return;
			}
			Candidates.push_back(Candidate);
		};

		TryAddCandidate(Asset->Bones[SourceBoneIndex].ParentIndex);
		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
		{
			if (Asset->Bones[BoneIndex].ParentIndex == SourceBoneIndex)
			{
				TryAddCandidate(BoneIndex);
			}
		}
		return Candidates;
	}

void CollectEditablePropertiesFromStruct(UStruct* Struct, void* Container, UObject* OwnerObject, TArray<FPropertyValue>& OutProps)
	{
		if (!Struct || !Container)
		{
			return;
		}

		TArray<const FProperty*> Properties;
		Struct->GetPropertyRefs(Properties);
		for (const FProperty* Property : Properties)
		{
			if (!Property || (Property->Flags & PF_Edit) == 0 || !Property->GetValuePtrFor(Container))
			{
				continue;
			}

			FPropertyValue Value = Property->ToValue(Container, OwnerObject);
			if (Value.PassesEditCondition())
			{
				OutProps.push_back(Value);
			}
		}
	}

	bool RenderReflectedPropertyTable(const char* TableId, TArray<FPropertyValue>& Props, bool bDispatchChange)
	{
		bool bChanged = false;
		if (Props.empty())
		{
			ImGui::TextDisabled("No editable reflected properties.");
			return false;
		}

		const float TableWidth = ImGui::GetContentRegionAvail().x;
		const float NameColumnWeight = TableWidth < 420.0f ? 0.5f : 0.42f;
		const float ValueColumnWeight = 1.0f - NameColumnWeight;
		const ImGuiTableFlags TableFlags =
			ImGuiTableFlags_SizingStretchProp |
			ImGuiTableFlags_Resizable |
			ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_PadOuterX |
			ImGuiTableFlags_RowBg;

		if (ImGui::BeginTable(TableId, 2, TableFlags))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, NameColumnWeight);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, ValueColumnWeight);

			ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.145f, 0.145f, 0.145f, 1.0f));

			FEditorPropertyRenderer PropertyRenderer;
			for (int32 i = 0; i < static_cast<int32>(Props.size()); ++i)
			{
				ImGui::TableNextRow();
				ImGui::PushID(i);

				ImGui::TableSetColumnIndex(0);
				const bool bPropertyOpen = FEditorPropertyRenderer::DrawPropertyLabel(Props[i]);

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);

				FEditorPropertyRenderOptions Options;
				Options.bDispatchChange = bDispatchChange;
				Options.bUseExternalExpansion = true;
				Options.bParentExpanded = bPropertyOpen;
				if (PropertyRenderer.RenderPropertyWidget(Props, i, Options))
				{
					bChanged = true;
				}
				ImGui::PopID();
			}

			ImGui::PopStyleColor(2);
			ImGui::EndTable();
		}

		return bChanged;
	}

	void RegisterPreviewWorldCollisionComponents(UWorld* World, USkeletalMeshComponent* PreviewMeshComponent)
	{
		IPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
		if (!World || !PhysicsScene)
		{
			return;
		}

		for (AActor* Actor : World->GetActors())
		{
			if (!Actor)
			{
				continue;
			}

			for (UActorComponent* Component : Actor->GetComponents())
			{
				UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
				if (!Primitive || Primitive == PreviewMeshComponent || !Primitive->IsQueryCollisionEnabled())
				{
					continue;
				}
				PhysicsScene->RegisterComponent(Primitive);
			}
		}
	}

}

void FMeshEditorWidget::StartPhysicsAssetSimulation()
{
	USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
	USkeletalMesh* Mesh = Comp ? Comp->GetSkeletalMesh() : nullptr;
	UPhysicsAsset* PhysAsset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!Comp || !PhysAsset || !PhysAsset->HasAnyBodySetup())
	{
		return;
	}

	if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
	{
		if (IPhysicsScene* PhysicsScene = PreviewWorld->EnsurePhysicsScene())
		{
			RegisterPreviewWorldCollisionComponents(PreviewWorld, Comp);
			if (Comp->IsRagdollSimulating())
			{
				Comp->SetSimulateRagdoll(false);
			}
			PhysicsScene->DestroyPhysicsAssetBodies(Comp);
			PhysicsScene->InstantiatePhysicsAssetBodies(Comp);
			PhysicsScene->SyncPhysicsAssetBodiesToComponentPose(Comp, true);
		}
	}

	Comp->SetSimulateRagdoll(true);
	bPhysicsAssetSimulationRunning = Comp->IsRagdollSimulating();
}

//Ragdoll 시뮬레이션을 멈추고, 필요하면 포즈를 리셋한다.
void FMeshEditorWidget::StopPhysicsAssetSimulation(bool bResetPose)
{
	USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
	if (Comp)
	{
		Comp->SetSimulateRagdoll(false);
		if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
		{
			if (IPhysicsScene* PhysicsScene = PreviewWorld->GetPhysicsScene())
			{
				PhysicsScene->DestroyPhysicsAssetBodies(Comp);
			}
		}

		if (bResetPose)
		{
			Comp->ApplyBoneEditBasePose();
		}
	}

	bPhysicsAssetSimulationRunning = false;
}

void FMeshEditorWidget::RenderPhysicsSimulationControls(USkeletalMesh* SkeletalMesh, UPhysicsAsset* PhysAsset)
{
	const bool bHasSimulatableAsset = SkeletalMesh && PhysAsset && PhysAsset->HasAnyBodySetup();
	constexpr float IconSize = 16.0f;
	constexpr float ButtonSize = 24.0f;

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.3f));

	auto DrawIconButton = [&](const char* Id, const wchar_t* IconFile, const char* FallbackLabel, bool bDisabled) -> bool
		{
			if (bDisabled)
			{
				ImGui::BeginDisabled();
			}

			bool bClicked = false;
			if (ID3D11ShaderResourceView* Icon = LoadEditorIcon(IconFile))
			{
				const ImVec2 ButtonPos = ImGui::GetCursorScreenPos();
				bClicked = ImGui::InvisibleButton(Id, ImVec2(ButtonSize, ButtonSize));
				const float IconOffset = (ButtonSize - IconSize) * 0.5f;
				ImGui::GetWindowDrawList()->AddImage(
					reinterpret_cast<ImTextureID>(Icon),
					ImVec2(ButtonPos.x + IconOffset, ButtonPos.y + IconOffset),
					ImVec2(ButtonPos.x + IconOffset + IconSize, ButtonPos.y + IconOffset + IconSize));
			}
			else
			{
				bClicked = ImGui::Button(FallbackLabel, ImVec2(ButtonSize, ButtonSize));
			}

			if (bDisabled)
			{
				ImGui::EndDisabled();
				bClicked = false;
			}
			return bClicked;
		};

	if (DrawIconButton("##PhysicsAssetSimPlay", L"icon_playInSelectedViewport_16x.png", ">", !bHasSimulatableAsset || bPhysicsAssetSimulationRunning))
	{
		StartPhysicsAssetSimulation();
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Start PhysX simulation");
	}

	ImGui::SameLine(0.0f, 4.0f);

	if (DrawIconButton("##PhysicsAssetSimStop", L"generic_stop_16x.png", "[]", !bPhysicsAssetSimulationRunning))
	{
		StopPhysicsAssetSimulation(true);
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Stop PhysX simulation");
	}

	ImGui::SameLine();
	ImGui::TextDisabled("%s", bPhysicsAssetSimulationRunning ? "Simulating" : "Stopped");

	ImGui::PopStyleColor(3);

	ImGui::Text("Simulation Rate");
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::SliderFloat("##PhysicsAssetSimulationRate", &PhysicsAssetSimulationTimeScale, 0.0f, 2.0f, "%.2fx");
}

//PhysicsAsset 탭의 본 계층 구조 + 시뮬레이션 컨트롤 + 기타 편집 UI 렌더링. 선택된 본/바디/제약 조건에 따라 세부 패널도 함께 렌더링.
void FMeshEditorWidget::RenderPhysicalAssetLayout()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	UPhysicsAsset* PhysAsset = SkeletalMesh ? SkeletalMesh->GetPhysicsAsset() : nullptr;
	if (PhysAsset && SelectedConstraintIndex >= static_cast<int32>(PhysAsset->ConstraintSetups.size()))
	{
		SelectedConstraintIndex = -1;
	}
	auto BodyBelongsToPhysicsAsset = [](UPhysicsAsset* InPhysAsset, UBodySetup* BodySetup) -> bool
	{
		if (!InPhysAsset || !BodySetup)
		{
			return false;
		}

		for (UBodySetup* Candidate : InPhysAsset->BodySetups)
		{
			if (Candidate == BodySetup)
			{
				return true;
			}
		}
		return false;
	};
	if (!PhysAsset)
	{
		SelectedBodySetup = nullptr;
		PhysicsGraphFocusBodySetup = nullptr;
		SelectedConstraintIndex = -1;
	}
	else
	{
		if (SelectedBodySetup && !BodyBelongsToPhysicsAsset(PhysAsset, SelectedBodySetup))
		{
			SelectedBodySetup = nullptr;
		}
		if (PhysicsGraphFocusBodySetup && !BodyBelongsToPhysicsAsset(PhysAsset, PhysicsGraphFocusBodySetup))
		{
			PhysicsGraphFocusBodySetup = nullptr;
		}
	}

	// Left: bone hierarchy
	const float LeftPanelHeight = ImGui::GetContentRegionAvail().y;
	const float GraphHeight = std::min(260.0f, std::max(160.0f, LeftPanelHeight * 0.32f));
	ImGui::BeginGroup();
	ImGui::BeginChild("BoneHierarchy", ImVec2(HierarchyWidth, std::max(120.0f, LeftPanelHeight - GraphHeight - ImGui::GetStyle().ItemSpacing.y)), true);
	ImGui::Text("Physics Asset");
	ImGui::Separator();

	//Auto generate bodies 버튼 + 시뮬레이션 컨트롤
	if (SkeletalMesh)
	{
		ImGui::Text("Bodies: %d", PhysAsset ? static_cast<int32>(PhysAsset->BodySetups.size()) : 0);
		ImGui::Text("Constraints: %d", PhysAsset ? static_cast<int32>(PhysAsset->ConstraintSetups.size()) : 0);
		RenderPhysicsSimulationControls(SkeletalMesh, PhysAsset);

		// 자동생성 merge-up 임계값(0이면 해당 캡 비활성). 작은/깊은 본은 드롭 대신 부모 바디로 병합된다.
		ImGui::SliderFloat("Min Bone Size Ratio", &AutoGenMinBoneSizeRatio, 0.0f, 0.25f, "%.3f");
		ImGui::SliderInt("Max Bone Depth (0=off)", &AutoGenMaxBoneDepth, 0, 16);
		ImGui::SliderInt("Max Body Count (0=off)", &AutoGenMaxBodyCount, 0, 128);

		// Auto generate bodies 버튼
		if (ImGui::Button("Generate Bodies", ImVec2(-1.0f, 0.0f)))
		{
			StopPhysicsAssetSimulation(true);
			PhysAsset = SkeletalMesh->EnsurePhysicsAsset();

			FPhysicsAssetAutoGenerateSettings Settings;
			Settings.bReplaceExisting = true;
			Settings.bCreateConstraints = true;
			Settings.bUseDominantBoneOnly = true;
			Settings.bUseDefaultNameFilters = true;
			Settings.MinBoneWeight = 0.25f;
			Settings.LowerPercentile = 0.05f;
			Settings.UpperPercentile = 0.95f;
			Settings.ShapePadding = 1.10f;
			Settings.MinShapeSize = 0.01f;
			Settings.MinVertexCount = 12;
			Settings.MinBoneSizeRatio = AutoGenMinBoneSizeRatio;
			Settings.MaxBoneDepth = AutoGenMaxBoneDepth;
			Settings.MaxBodyCount = AutoGenMaxBodyCount;

			if (PhysAsset->AutoGeneratePrimitiveBodiesFromSkeletalMesh(*(SkeletalMesh->GetSkeletalMeshAsset()), Settings))
			{
				PhysAsset = SkeletalMesh->GetPhysicsAsset();
				SelectedBodySetup = nullptr;
				PhysicsGraphFocusBodySetup = nullptr;
				SelectedConstraintIndex = -1;
				PhysicsGraphNodePositions.clear();
				ViewportClient.SetSelectedBone(SkeletalMesh, SelectedBoneIndex);
				MarkDirty();
			}
		}
		//Save Physics Asset 버튼
		if (PhysAsset)
		{
			const char* SaveLabel = IsDirty() ? "Save Physics Asset*" : "Save Physics Asset";
			if (ImGui::Button(SaveLabel, ImVec2(-1.0f, 0.0f)))
			{
				SaveCurrentPhysicsAsset();
			}
		}
	}

	//좌측 패널 : 본 계층 구조 트리. 각 본 옆에 해당 본에 바디가 있으면 바디 아이콘 표시. 본/바디 선택 가능. 선택된 본/바디는 우측 세부 패널과 좌측 하단 제약 조건 그래프에 정보 전달. 물리 자산이 있을 때는 제약 조건도 함께 렌더링 (제약 조건 선택 시 세부 패널에 정보 전달). 물리 시뮬레이션이 실행 중일 때는 트리에 시뮬레이션 상태 반영 (예: 아이콘 색상 변경).
	ImGui::Dummy(ImVec2(0.0f, 6.0f));
	ImGui::Text("Bone Hierarchy");
	ImGui::Separator();
	if (SkeletalMesh)
	{
		const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
		const  TArray<UBodySetup*> Bodies = PhysAsset ? PhysAsset->BodySetups : TArray<UBodySetup*>();
		if (Asset)
		{
			for (int32 i = 0; i < static_cast<int32>(Asset->Bones.size()); ++i)
			{
				if (Asset->Bones[i].ParentIndex == -1)
				{
					RenderBoneTreeWithPhysicsAsset(Asset, Bodies, i);
				}
			}
		}
	}
	ImGui::EndChild();

	//좌측 하단 : 선택된 본/바디에 대한 제약 조건 그래프. 본과 바디가 모두 선택되지 않았거나 물리 자산이 없으면 빈 패널.
	ImGui::BeginChild("BodyConstraintGraph", ImVec2(HierarchyWidth, GraphHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove);
	RenderPhysicsAssetGraph(SkeletalMesh, PhysAsset);
	ImGui::EndChild();
	ImGui::EndGroup();

	ImGui::SameLine();

	// Splitter
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::Button("##skelSplitter", ImVec2(4.0f, -1.0f));
	if (ImGui::IsItemActive())
	{
		HierarchyWidth += ImGui::GetIO().MouseDelta.x;
		HierarchyWidth = std::max(100.0f, std::min(HierarchyWidth, ImGui::GetWindowWidth() - DetailsWidth - 100.0f));
	}
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}
	ImGui::PopStyleColor(3);

	ImGui::SameLine();

	// Center: viewport
	ImGui::BeginGroup();
	{
		constexpr float RightSplitterWidth = 4.0f;
		const float AvailableWidth = ImGui::GetContentRegionAvail().x;
		float ViewportWidth = AvailableWidth - DetailsWidth - RightSplitterWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f;
		ViewportWidth = std::max(120.0f, ViewportWidth);
		ImVec2 Size = ImVec2(ViewportWidth, ImGui::GetContentRegionAvail().y);
		RenderViewportPanel(Size);
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	// Right splitter
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::Button("##physicsDetailsSplitter", ImVec2(4.0f, -1.0f));
	if (ImGui::IsItemActive())
	{
		DetailsWidth -= ImGui::GetIO().MouseDelta.x;
		DetailsWidth = std::max(220.0f, std::min(DetailsWidth, ImGui::GetWindowWidth() - HierarchyWidth - 180.0f));
	}
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}
	ImGui::PopStyleColor(3);

	ImGui::SameLine();

	// Right: bone details(본 트리에서 본을 선택했을 때 해당 본의 정보 + 편집 UI, 바디를 선택했을 때는 바디 정보 + 편집 UI, 제약 조건을 선택했을 때는 제약 조건 정보 + 편집 UI). 본/바디/제약 조건이 모두 선택되지 않았을 때는 빈 패널.
	ImGui::BeginChild("BoneDetails", ImVec2(DetailsWidth, 0), true);
	ImGui::Text(SelectedConstraintIndex >= 0 ? "Constraint Details" : (SelectedBodySetup ? "Body Details" : "Bone Details"));
	ImGui::Separator();

	//Constraint Details
	if (SkeletalMesh && PhysAsset && SelectedConstraintIndex >= 0 && SelectedConstraintIndex < static_cast<int32>(PhysAsset->ConstraintSetups.size()))
	{
		FConstraintSetup& Constraint = PhysAsset->ConstraintSetups[SelectedConstraintIndex];
		ImGui::Text("Name: %s", Constraint.ConstraintName.ToString().c_str());
		ImGui::Text("Parent: %s", Constraint.ParentBoneName.ToString().c_str());
		ImGui::Text("Child: %s", Constraint.ChildBoneName.ToString().c_str());
		ImGui::Dummy(ImVec2(0, 8));

		TArray<FPropertyValue> Props;
		CollectEditablePropertiesFromStruct(FConstraintSetup::StaticStruct(), &Constraint, nullptr, Props);
		if (RenderReflectedPropertyTable("##ConstraintReflectionTable", Props, false))
		{
			MarkDirty();
			ViewportClient.RefreshPhysicsAssetDebugDraw();
		}

		ImGui::Dummy(ImVec2(0, 8));
		if (ImGui::Button("Delete Constraint", ImVec2(-1.0f, 0.0f)))
		{
			PhysAsset->ConstraintSetups.erase(PhysAsset->ConstraintSetups.begin() + SelectedConstraintIndex);
			SelectedConstraintIndex = -1;
			ViewportClient.SetSelectedPhysicsConstraint(SkeletalMesh, -1);
			MarkDirty();
		}
	}
	//Body Details
	else if (SkeletalMesh && SelectedBoneIndex != -1 && SelectedBodySetup)
	{
		FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
		FBone& Bone = Asset->Bones[SelectedBoneIndex];
		const char* ShapeType = nullptr;
		GetFirstPhysicsShapeElem(SelectedBodySetup, &ShapeType);

		ImGui::Text("Body: %s", SelectedBodySetup->GetName().c_str());
		ImGui::Text("Bone: %s", Bone.Name.c_str());
		ImGui::Text("Shape: %s", ShapeType ? ShapeType : "None");
		ImGui::Dummy(ImVec2(0, 10));

		TArray<FPropertyValue> Props;
		SelectedBodySetup->GetEditableProperties(Props);
		if (RenderReflectedPropertyTable("##BodySetupReflectionTable", Props, true))
		{
			ViewportClient.SetSelectedPhysicsBody(SkeletalMesh, SelectedBoneIndex, SelectedBodySetup);
			MarkDirty();
		}
	}
	//Bone Details
	else if (SkeletalMesh && SelectedBoneIndex != -1)
	{
		FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
		FBone& Bone = Asset->Bones[SelectedBoneIndex];

		ImGui::Text("Name: %s", Bone.Name.c_str());
		ImGui::Text("Index: %d", SelectedBoneIndex);
		ImGui::Dummy(ImVec2(0, 10));

		USkeletalMeshComponent* PreviewMeshComponent = ViewportClient.GetPreviewMeshComponent();
		FTransform LocalTransform = PreviewMeshComponent
			? PreviewMeshComponent->GetBoneEditBaseLocalTransformByIndex(SelectedBoneIndex)
			: FTransform(Bone.GetReferenceLocalPose());

		FVector Location = LocalTransform.Location;
		if (ImGui::DragFloat3("Location", &Location.X, 0.1f))
		{
			LocalTransform.Location = Location;
			if (PreviewMeshComponent)
				PreviewMeshComponent->SetBoneEditBaseLocalTransformByIndex(SelectedBoneIndex, LocalTransform);
			else
			{
				Bone.ReferenceLocalPose = LocalTransform.ToMatrix();
				Bone.SyncLegacyPoseDataFromSeparated();
			}
		}

		FVector Rotation = LocalTransform.GetRotator().ToVector();
		if (ImGui::DragFloat3("Rotation", &Rotation.X, 0.1f))
		{
			LocalTransform.SetRotation(FRotator(Rotation));
			if (PreviewMeshComponent)
				PreviewMeshComponent->SetBoneEditBaseLocalTransformByIndex(SelectedBoneIndex, LocalTransform);
			else
			{
				Bone.ReferenceLocalPose = LocalTransform.ToMatrix();
				Bone.SyncLegacyPoseDataFromSeparated();
			}
		}

		FVector Scale = LocalTransform.Scale;
		if (ImGui::DragFloat3("Scale", &Scale.X, 0.1f, 0.01f))
		{
			LocalTransform.Scale = Scale;
			if (PreviewMeshComponent)
				PreviewMeshComponent->SetBoneEditBaseLocalTransformByIndex(SelectedBoneIndex, LocalTransform);
			else
			{
				Bone.ReferenceLocalPose = LocalTransform.ToMatrix();
				Bone.SyncLegacyPoseDataFromSeparated();
			}
		}
	}
	else
	{
		ImGui::TextDisabled("Select a bone to edit.");
	}

	ImGui::EndChild();
}

//Bone + Body 트리 렌더링. 본과 해당 본에 연결된 바디가 모두 트리 구조로 표현됨. 본 또는 바디를 클릭하면 선택 상태가 업데이트되고 세부 패널이 해당 선택에 맞게 갱신됨. 본을 우클릭하면 해당 본에 새 물리 바디를 추가할 수 있는 컨텍스트 메뉴가 나타남.
void FMeshEditorWidget::RenderBoneTreeWithPhysicsAsset(const FSkeletalMesh* Asset, const TArray<UBodySetup*>& Bodies, int32 Index)
{
	const FBone& Bone = Asset->Bones[Index];
	const FName BoneName(Bone.Name);

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;

	if (Index == SelectedBoneIndex && SelectedBodySetup == nullptr)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	bool bHasChildren = false;
	for (int32 i = Index + 1; i < static_cast<int32>(Asset->Bones.size()); ++i)
	{
		if (Asset->Bones[i].ParentIndex == Index)
		{
			bHasChildren = true;
			break;
		}
	}

	TArray<UBodySetup*> BoneBodies;
	for (UBodySetup* Body : Bodies)
	{
		if (Body && Body->GetBoneName() == BoneName)
		{
			BoneBodies.push_back(Body);
		}
	}

	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	UPhysicsAsset* PhysAsset = SkeletalMesh ? SkeletalMesh->GetPhysicsAsset() : nullptr;
	TArray<int32> BoneConstraintIndices;
	if (PhysAsset)
	{
		for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(PhysAsset->ConstraintSetups.size()); ++ConstraintIndex)
		{
			const FConstraintSetup& Constraint = PhysAsset->ConstraintSetups[ConstraintIndex];
			if (Constraint.ParentBoneName == BoneName || Constraint.ChildBoneName == BoneName)
			{
				BoneConstraintIndices.push_back(ConstraintIndex);
			}
		}
	}

	const bool bHasBodyChildren = !BoneBodies.empty();
	if (!bHasChildren && !bHasBodyChildren)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	ImGui::PushID(Index);
	if (ID3D11ShaderResourceView* BoneIcon = LoadEditorIcon(L"Bone.png"))
	{
		ImGui::Image(reinterpret_cast<ImTextureID>(BoneIcon), ImVec2(14.0f, 14.0f));
		ImGui::SameLine(0.0f, 4.0f);
	}
	bool bOpen = ImGui::TreeNodeEx("Bone", Flags, "%s", Bone.Name.c_str());

	if (ImGui::IsItemClicked())
	{
		SelectedBoneIndex = Index;
		SelectedBodySetup = nullptr;
		PhysicsGraphFocusBodySetup = nullptr;
		SelectedConstraintIndex = -1;
		ViewportClient.SetSelectedBone(Cast<USkeletalMesh>(EditedObject), Index);
	}
	if (ImGui::BeginPopupContextItem("##BonePhysicsContext"))
	{
		const bool bCanAddBody = SkeletalMesh != nullptr;
		if (ImGui::BeginMenu("Add Body", bCanAddBody))
		{
			auto AddBodyMenuItem = [&](const char* Label, EManualPhysicsBodyShape Shape)
			{
				if (ImGui::MenuItem(Label))
				{
					FPhysicsAssetAutoGenerateSettings Settings;
					Settings.bReplaceExisting = false;
					Settings.bCreateConstraints = false;
					Settings.bUseDominantBoneOnly = true;
					Settings.bUseDefaultNameFilters = true;
					Settings.MinBoneWeight = 0.25f;
					Settings.LowerPercentile = 0.05f;
					Settings.UpperPercentile = 0.95f;
					Settings.ShapePadding = 1.10f;
					Settings.MinShapeSize = 0.01f;
					Settings.MinVertexCount = 12;
					Settings.MinBoneSizeRatio = AutoGenMinBoneSizeRatio;
					Settings.MaxBoneDepth = AutoGenMaxBoneDepth;
					Settings.MaxBodyCount = AutoGenMaxBodyCount;

					if (UBodySetup* NewBody = AddManualPhysicsBodyForBone(SkeletalMesh, Index, Shape, Settings))
					{
						SelectedBoneIndex = Index;
						SelectedBodySetup = NewBody;
						PhysicsGraphFocusBodySetup = NewBody;
						SelectedConstraintIndex = -1;
						ViewportClient.SetSelectedPhysicsBody(SkeletalMesh, Index, NewBody);
						MarkDirty();
					}
				}
			};

			AddBodyMenuItem("Sphere", EManualPhysicsBodyShape::Sphere);
			AddBodyMenuItem("Capsule", EManualPhysicsBodyShape::Capsule);
			AddBodyMenuItem("Cube", EManualPhysicsBodyShape::Box);
			ImGui::EndMenu();
		}
		if (!bCanAddBody)
		{
			ImGui::TextDisabled("No skeletal mesh");
		}
		ImGui::EndPopup();
	}

	if (bOpen && (bHasChildren || bHasBodyChildren))
	{
		for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BoneBodies.size()); ++BodyIndex)
		{
			UBodySetup* Body = BoneBodies[BodyIndex];
			const FKAggregateGeom& AggGeom = Body->GetAggGeom();
			const int32 ShapeCount = AggGeom.GetElementCount();

			ImGuiTreeNodeFlags BodyFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
			if (BoneConstraintIndices.empty())
			{
				BodyFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			}
			if (SelectedBodySetup == Body)
			{
				BodyFlags |= ImGuiTreeNodeFlags_Selected;
			}

			ImGui::PushID(BodyIndex);
			const bool bBodyOpen = ImGui::TreeNodeEx("Body", BodyFlags, "%s (%d shapes)", Body->GetName().c_str(), ShapeCount);
			if (ImGui::IsItemClicked())
			{
				SelectedBoneIndex = Index;
				SelectedBodySetup = Body;
				PhysicsGraphFocusBodySetup = Body;
				SelectedConstraintIndex = -1;
				ViewportClient.SetSelectedPhysicsBody(Cast<USkeletalMesh>(EditedObject), Index, Body);
			}
			if (ImGui::BeginPopupContextItem("##BodyPhysicsContext"))
			{
				if (RenderConstraintCandidateMenu(SkeletalMesh, PhysAsset, Body))
				{
					PhysicsGraphFocusBodySetup = Body;
					SelectedConstraintIndex = PhysAsset
						? static_cast<int32>(PhysAsset->ConstraintSetups.size()) - 1
						: -1;
					SelectedBodySetup = nullptr;
					ViewportClient.SetSelectedPhysicsConstraint(SkeletalMesh, SelectedConstraintIndex);
					MarkDirty();
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Delete Body"))
				{
					if (RemovePhysicsBodyAndConstraints(PhysAsset, Body))
					{
						if (PhysicsGraphFocusBodySetup == Body)
						{
							PhysicsGraphFocusBodySetup = nullptr;
						}
						SelectedBodySetup = nullptr;
						SelectedConstraintIndex = -1;
						ViewportClient.SetSelectedBone(SkeletalMesh, Index);
						MarkDirty();
						ImGui::EndPopup();
						ImGui::PopID();
						ImGui::TreePop();
						ImGui::PopID();
						return;
					}
				}
				ImGui::EndPopup();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Spheres: %d\nBoxes: %d\nCapsules: %d",
					static_cast<int32>(AggGeom.SphereElems.size()),
					static_cast<int32>(AggGeom.BoxElems.size()),
					static_cast<int32>(AggGeom.SphylElems.size()));
			}
			if (bBodyOpen && !BoneConstraintIndices.empty())
			{
				for (int32 ConstraintListIndex = 0; ConstraintListIndex < static_cast<int32>(BoneConstraintIndices.size()); ++ConstraintListIndex)
				{
					const int32 ConstraintIndex = BoneConstraintIndices[ConstraintListIndex];
					if (!PhysAsset || ConstraintIndex < 0 || ConstraintIndex >= static_cast<int32>(PhysAsset->ConstraintSetups.size()))
					{
						continue;
					}

					const FConstraintSetup& Constraint = PhysAsset->ConstraintSetups[ConstraintIndex];
					ImGui::PushID(("Constraint" + std::to_string(ConstraintIndex)).c_str());
					const FString ConstraintLabel = "Constraint: "
						+ Constraint.ParentBoneName.ToString()
						+ " -> "
						+ Constraint.ChildBoneName.ToString();
					if (ID3D11ShaderResourceView* ConstraintIcon = LoadEditorIcon(L"PhysicsConstraintComponent_64x.png"))
					{
						ImGui::Image(reinterpret_cast<ImTextureID>(ConstraintIcon), ImVec2(14.0f, 14.0f));
						ImGui::SameLine(0.0f, 4.0f);
					}
					if (ImGui::Selectable(ConstraintLabel.c_str(), SelectedConstraintIndex == ConstraintIndex))
					{
						SelectedBoneIndex = FindBoneIndexByName(Asset, Constraint.ChildBoneName);
						SelectedBodySetup = nullptr;
						PhysicsGraphFocusBodySetup = PhysAsset->FindBodySetupByBoneName(Constraint.ChildBoneName);
						if (!PhysicsGraphFocusBodySetup)
						{
							PhysicsGraphFocusBodySetup = Body;
						}
						SelectedConstraintIndex = ConstraintIndex;
						ViewportClient.SetSelectedPhysicsConstraint(SkeletalMesh, ConstraintIndex);
					}
					if (ImGui::BeginPopupContextItem("##ConstraintPhysicsContext"))
					{
						if (ImGui::MenuItem("Delete Constraint"))
						{
							PhysAsset->ConstraintSetups.erase(PhysAsset->ConstraintSetups.begin() + ConstraintIndex);
							SelectedConstraintIndex = -1;
							ViewportClient.SetSelectedPhysicsConstraint(SkeletalMesh, -1);
							MarkDirty();
							ImGui::EndPopup();
							ImGui::PopID();
							ImGui::TreePop();
							ImGui::PopID();
							ImGui::TreePop();
							ImGui::PopID();
							return;
						}
						ImGui::EndPopup();
					}
					ImGui::PopID();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		for (int32 i = Index + 1; i < static_cast<int32>(Asset->Bones.size()); ++i)
		{
			if (Asset->Bones[i].ParentIndex == Index)
			{
				RenderBoneTreeWithPhysicsAsset(Asset, Bodies, i);
			}
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

bool FMeshEditorWidget::RenderConstraintCandidateMenu(USkeletalMesh* SkeletalMesh, UPhysicsAsset* PhysAsset, UBodySetup* SourceBody)
{
	if (!SkeletalMesh || !PhysAsset || !SourceBody)
	{
		ImGui::MenuItem("Add Constraint", nullptr, false, false);
		return false;
	}

	bool bAdded = false;
	const TArray<FPhysicsConstraintCandidate> Candidates = BuildStrictConstraintCandidates(SkeletalMesh, PhysAsset, SourceBody);
	if (ImGui::BeginMenu("Add Constraint", !Candidates.empty()))
	{
		for (const FPhysicsConstraintCandidate& Candidate : Candidates)
		{
			if (!Candidate.BodySetup)
			{
				continue;
			}

			const FString Label = Candidate.BodySetup->GetBoneName().ToString()
				+ " (" + Candidate.ParentBoneName.ToString() + " -> " + Candidate.ChildBoneName.ToString() + ")";
			if (ImGui::MenuItem(Label.c_str()))
			{
				bAdded = SkeletalMesh->AddPhysicsConstraintBetweenBodies(Candidate.ParentBoneName, Candidate.ChildBoneName);
			}
		}
		ImGui::EndMenu();
	}
	else if (Candidates.empty())
	{
		ImGui::TextDisabled("No strict candidates");
	}

	return bAdded;
}

void FMeshEditorWidget::RenderPhysicsAssetGraph(USkeletalMesh* SkeletalMesh, UPhysicsAsset* PhysAsset)
{
	ImGui::Text("Body-Constraint Graph");
	ImGui::Separator();

	if (!SkeletalMesh || !PhysAsset || PhysAsset->BodySetups.empty())
	{
		ImGui::TextDisabled("No physics bodies.");
		return;
	}

	if (!PhysicsGraphFocusBodySetup && SelectedBodySetup)
	{
		PhysicsGraphFocusBodySetup = SelectedBodySetup;
	}

	UBodySetup* GraphRootBody = PhysicsGraphFocusBodySetup;
	if (!GraphRootBody)
	{
		ImGui::TextDisabled("Select a body to edit its constraint graph.");
		return;
	}

	FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Asset)
	{
		ImGui::TextDisabled("No skeleton.");
		return;
	}

	const int32 SelectedGraphBoneIndex = FindBoneIndexByName(Asset, GraphRootBody->GetBoneName());
	if (SelectedGraphBoneIndex < 0)
	{
		ImGui::TextDisabled("Selected body bone is missing.");
		return;
	}

	TArray<UBodySetup*> VisibleBodies;
	TArray<int32> VisibleConstraintIndices;
	VisibleBodies.push_back(GraphRootBody);

	auto AddVisibleBody = [&](UBodySetup* BodySetup)
	{
		if (!BodySetup)
		{
			return;
		}

		for (UBodySetup* ExistingBody : VisibleBodies)
		{
			if (ExistingBody == BodySetup)
			{
				return;
			}
		}
		VisibleBodies.push_back(BodySetup);
	};

	const FName SelectedBoneName = GraphRootBody->GetBoneName();
	for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(PhysAsset->ConstraintSetups.size()); ++ConstraintIndex)
	{
		const FConstraintSetup& Constraint = PhysAsset->ConstraintSetups[ConstraintIndex];
		if (Constraint.ChildBoneName != SelectedBoneName)
		{
			continue;
		}

		const FName OtherBoneName = Constraint.ParentBoneName;
		const int32 OtherBoneIndex = FindBoneIndexByName(Asset, OtherBoneName);
		if (OtherBoneIndex < 0 || Asset->Bones[SelectedGraphBoneIndex].ParentIndex != OtherBoneIndex)
		{
			continue;
		}

		AddVisibleBody(PhysAsset->FindBodySetupByBoneName(OtherBoneName));
		VisibleConstraintIndices.push_back(ConstraintIndex);
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 CanvasPos = ImGui::GetCursorScreenPos();
	ImVec2 CanvasSize = ImGui::GetContentRegionAvail();
	CanvasSize.x = std::max(CanvasSize.x, 80.0f);
	CanvasSize.y = std::max(CanvasSize.y, 80.0f);

	ImGui::Dummy(CanvasSize);
	const ImVec2 CanvasMax(CanvasPos.x + CanvasSize.x, CanvasPos.y + CanvasSize.y);
	const ImVec2 MousePos = ImGui::GetMousePos();
	const bool bMouseInCanvas =
		MousePos.x >= CanvasPos.x && MousePos.x <= CanvasMax.x &&
		MousePos.y >= CanvasPos.y && MousePos.y <= CanvasMax.y;
	if (bMouseInCanvas && ImGui::GetIO().MouseWheel != 0.0f)
	{
		bPhysicsGraphCapturingMouse = true;
		ImGui::SetNextFrameWantCaptureMouse(true);
		InputSystem::Get().SetGuiMouseCapture(true);
	}
	if (bPhysicsGraphPanning && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		bPhysicsGraphCapturingMouse = true;
		ImGui::SetNextFrameWantCaptureMouse(true);
		InputSystem::Get().SetGuiMouseCapture(true);
		const ImVec2 Delta = ImGui::GetIO().MouseDelta;
		PhysicsGraphPanX += Delta.x;
		PhysicsGraphPanY += Delta.y;
	}
	else if (bPhysicsGraphPanning)
	{
		bPhysicsGraphPanning = false;
	}
	if (bMouseInCanvas && ImGui::GetIO().MouseWheel != 0.0f)
	{
		const float OldZoom = std::clamp(PhysicsGraphZoom, 0.35f, 2.5f);
		const float ZoomStep = ImGui::GetIO().MouseWheel > 0.0f ? 1.12f : 1.0f / 1.12f;
		const float NewZoom = std::clamp(OldZoom * ZoomStep, 0.35f, 2.5f);
		const ImVec2 MouseGraph(
			(MousePos.x - CanvasPos.x - PhysicsGraphPanX) / OldZoom,
			(MousePos.y - CanvasPos.y - PhysicsGraphPanY) / OldZoom);
		PhysicsGraphPanX = MousePos.x - CanvasPos.x - MouseGraph.x * NewZoom;
		PhysicsGraphPanY = MousePos.y - CanvasPos.y - MouseGraph.y * NewZoom;
		PhysicsGraphZoom = NewZoom;
	}
	PhysicsGraphZoom = std::clamp(PhysicsGraphZoom, 0.35f, 2.5f);

	auto GraphToScreen = [&](const ImVec2& GraphPos)
	{
		return ImVec2(
			CanvasPos.x + PhysicsGraphPanX + GraphPos.x * PhysicsGraphZoom,
			CanvasPos.y + PhysicsGraphPanY + GraphPos.y * PhysicsGraphZoom);
	};
	auto GraphSizeToScreen = [&](const ImVec2& GraphSize)
	{
		return ImVec2(GraphSize.x * PhysicsGraphZoom, GraphSize.y * PhysicsGraphZoom);
	};
	const FString GraphScopeKey = MakePhysicsGraphNodeKey(GraphRootBody) + "::";
	auto MakeScopedBodyNodeKey = [&](const UBodySetup* BodySetup)
	{
		return GraphScopeKey + MakePhysicsGraphNodeKey(BodySetup);
	};
	auto MakeScopedConstraintNodeKey = [&](int32 ConstraintIndex)
	{
		return GraphScopeKey + MakePhysicsGraphConstraintNodeKey(ConstraintIndex);
	};

	DrawList->AddRectFilled(CanvasPos, CanvasMax, IM_COL32(28, 30, 34, 255));
	DrawList->AddRect(CanvasPos, CanvasMax, IM_COL32(68, 72, 82, 255));
	DrawList->PushClipRect(CanvasPos, CanvasMax, true);

	const float GridStep = std::max(8.0f, 32.0f * PhysicsGraphZoom);
	const float GridOffsetX = std::fmod(PhysicsGraphPanX, GridStep);
	const float GridOffsetY = std::fmod(PhysicsGraphPanY, GridStep);
	for (float X = CanvasPos.x + GridOffsetX; X < CanvasMax.x; X += GridStep)
	{
		DrawList->AddLine(ImVec2(X, CanvasPos.y), ImVec2(X, CanvasMax.y), IM_COL32(42, 45, 52, 255));
	}
	for (float Y = CanvasPos.y + GridOffsetY; Y < CanvasMax.y; Y += GridStep)
	{
		DrawList->AddLine(ImVec2(CanvasPos.x, Y), ImVec2(CanvasMax.x, Y), IM_COL32(42, 45, 52, 255));
	}

	constexpr float GraphNodeTextScale = 0.5f;
	constexpr float BodyNodeWidth = 64.0f;
	constexpr float ConstraintNodeWidth = 66.0f;
	TMap<FString, ImVec2> NodeCenters;
	TMap<FString, FPhysicsGraphNodeVisual> NodeVisuals;
	bool bAnyGraphNodeHovered = false;
	bool bAnyGraphNodeActive = false;

	for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(VisibleBodies.size()); ++BodyIndex)
	{
		UBodySetup* BodySetup = VisibleBodies[BodyIndex];
		if (!BodySetup)
		{
			continue;
		}

		const FString BoneName = BodySetup->GetBoneName().ToString();
		const FString NodeKey = MakeScopedBodyNodeKey(BodySetup);
		const FPhysicsGraphNodeVisual NodeVisual = MakePhysicsGraphNodeVisual(BoneName, "Body", BodyNodeWidth, 21.0f, GraphNodeTextScale);
		NodeVisuals[NodeKey] = NodeVisual;
		if (PhysicsGraphNodePositions.find(NodeKey) == PhysicsGraphNodePositions.end())
		{
			FPhysicsGraphNodePosition Position;
			const bool bRootBody = BodySetup == GraphRootBody;
			const float DesiredScreenX = BodySetup == GraphRootBody
				? std::max(12.0f, CanvasSize.x - NodeVisual.Size.x * PhysicsGraphZoom - 12.0f)
				: 12.0f;
			const float LeftSideIndex = static_cast<float>(std::max(0, BodyIndex - 1));
			const float DesiredScreenY = std::max(12.0f, (CanvasSize.y - NodeVisual.Size.y * PhysicsGraphZoom) * 0.5f
				+ (bRootBody ? 0.0f : LeftSideIndex * (NodeVisual.Size.y * PhysicsGraphZoom + 12.0f)));
			Position.X = (DesiredScreenX - PhysicsGraphPanX) / PhysicsGraphZoom;
			Position.Y = (DesiredScreenY - PhysicsGraphPanY) / PhysicsGraphZoom;
			PhysicsGraphNodePositions[NodeKey] = Position;
		}

		FPhysicsGraphNodePosition& Position = PhysicsGraphNodePositions[NodeKey];
		const ImVec2 NodePos = GraphToScreen(ImVec2(Position.X, Position.Y));
		const ImVec2 NodeSize = GraphSizeToScreen(NodeVisual.Size);
		NodeCenters[BoneName] = ImVec2(NodePos.x + NodeSize.x * 0.5f, NodePos.y + NodeSize.y * 0.5f);
	}

	for (int32 ConstraintListIndex = 0; ConstraintListIndex < static_cast<int32>(VisibleConstraintIndices.size()); ++ConstraintListIndex)
	{
		const int32 ConstraintIndex = VisibleConstraintIndices[ConstraintListIndex];
		const FConstraintSetup& Constraint = PhysAsset->ConstraintSetups[ConstraintIndex];
		const FString ParentName = Constraint.ParentBoneName.ToString();
		const FString ChildName = Constraint.ChildBoneName.ToString();
		auto ParentIt = NodeCenters.find(ParentName);
		auto ChildIt = NodeCenters.find(ChildName);
		if (ParentIt == NodeCenters.end() || ChildIt == NodeCenters.end())
		{
			continue;
		}

		const FString ConstraintNodeKey = MakeScopedConstraintNodeKey(ConstraintIndex);
		const FPhysicsGraphNodeVisual ConstraintVisual = MakePhysicsGraphNodeVisual("Constraint", ParentName + " -> " + ChildName, ConstraintNodeWidth, 23.0f, GraphNodeTextScale);
		const ImVec2 Mid((ParentIt->second.x + ChildIt->second.x) * 0.5f, (ParentIt->second.y + ChildIt->second.y) * 0.5f);
		if (PhysicsGraphNodePositions.find(ConstraintNodeKey) == PhysicsGraphNodePositions.end())
		{
			FPhysicsGraphNodePosition Position;
			Position.X = (Mid.x - CanvasPos.x - PhysicsGraphPanX) / PhysicsGraphZoom - ConstraintVisual.Size.x * 0.5f;
			Position.Y = (Mid.y - CanvasPos.y - PhysicsGraphPanY) / PhysicsGraphZoom - ConstraintVisual.Size.y * 0.5f
				+ static_cast<float>(ConstraintListIndex) * (ConstraintVisual.Size.y + 10.0f);
			PhysicsGraphNodePositions[ConstraintNodeKey] = Position;
		}

		FPhysicsGraphNodePosition& ConstraintPosition = PhysicsGraphNodePositions[ConstraintNodeKey];
		const ImVec2 ConstraintNodePos = GraphToScreen(ImVec2(ConstraintPosition.X, ConstraintPosition.Y));
		const ImVec2 ConstraintNodeSize = GraphSizeToScreen(ConstraintVisual.Size);
		const ImVec2 ConstraintNodeMax(ConstraintNodePos.x + ConstraintNodeSize.x, ConstraintNodePos.y + ConstraintNodeSize.y);
		const ImVec2 ConstraintCenter(ConstraintNodePos.x + ConstraintNodeSize.x * 0.5f, ConstraintNodePos.y + ConstraintNodeSize.y * 0.5f);

		const ImU32 LineColor = ConstraintIndex == SelectedConstraintIndex
			? IM_COL32(120, 210, 255, 255)
			: IM_COL32(180, 185, 195, 210);
		DrawList->AddLine(ParentIt->second, ConstraintCenter, LineColor, ConstraintIndex == SelectedConstraintIndex ? 3.0f : 2.0f);
		DrawList->AddLine(ConstraintCenter, ChildIt->second, LineColor, ConstraintIndex == SelectedConstraintIndex ? 3.0f : 2.0f);

		ImGui::SetCursorScreenPos(ConstraintNodePos);
		ImGui::PushID(("Constraint" + std::to_string(ConstraintIndex)).c_str());
		ImGui::InvisibleButton("##ConstraintNode", ConstraintNodeSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const bool bConstraintNodeHovered = ImGui::IsItemHovered();
		const bool bConstraintNodeActive = ImGui::IsItemActive();
		bAnyGraphNodeHovered |= bConstraintNodeHovered;
		bAnyGraphNodeActive |= bConstraintNodeActive;
		if (bConstraintNodeHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		{
			bPhysicsGraphCapturingMouse = true;
			ImGui::SetNextFrameWantCaptureMouse(true);
			InputSystem::Get().SetGuiMouseCapture(true);
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			if (!PhysicsGraphFocusBodySetup)
			{
				PhysicsGraphFocusBodySetup = GraphRootBody;
			}
			SelectedConstraintIndex = ConstraintIndex;
			SelectedBodySetup = nullptr;
			const int32 ChildBoneIndex = FindBoneIndexByName(Asset, Constraint.ChildBoneName);
			if (ChildBoneIndex >= 0)
			{
				SelectedBoneIndex = ChildBoneIndex;
			}
			ViewportClient.SetSelectedPhysicsConstraint(SkeletalMesh, ConstraintIndex);
		}
		if (bConstraintNodeActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			bPhysicsGraphCapturingMouse = true;
			ImGui::SetNextFrameWantCaptureMouse(true);
			InputSystem::Get().SetGuiMouseCapture(true);
			const ImVec2 Delta = ImGui::GetIO().MouseDelta;
			ConstraintPosition.X += Delta.x / PhysicsGraphZoom;
			ConstraintPosition.Y += Delta.y / PhysicsGraphZoom;
		}
		if (ImGui::BeginPopupContextItem("##ConstraintContext"))
		{
			if (ImGui::MenuItem("Delete Constraint"))
			{
				PhysAsset->ConstraintSetups.erase(PhysAsset->ConstraintSetups.begin() + ConstraintIndex);
				SelectedConstraintIndex = -1;
				ViewportClient.SetSelectedPhysicsConstraint(SkeletalMesh, -1);
				MarkDirty();
				ImGui::EndPopup();
				ImGui::PopID();
				DrawList->PopClipRect();
				return;
			}
			ImGui::EndPopup();
		}
		DrawList->AddRectFilled(ConstraintNodePos, ConstraintNodeMax, IM_COL32(144, 161, 83, 245), 4.0f);
		DrawList->AddRect(ConstraintNodePos, ConstraintNodeMax,
			ConstraintIndex == SelectedConstraintIndex ? IM_COL32(220, 235, 150, 255) : IM_COL32(185, 196, 130, 255), 4.0f);
		DrawCenteredPhysicsGraphText(DrawList, ConstraintNodePos, ConstraintVisual, PhysicsGraphZoom, GraphNodeTextScale, IM_COL32(245, 248, 230, 255), IM_COL32(218, 226, 186, 255));
		ImGui::PopID();
	}

	for (UBodySetup* BodySetup : VisibleBodies)
	{
		if (!BodySetup)
		{
			continue;
		}

		const FString NodeKey = MakeScopedBodyNodeKey(BodySetup);
		auto VisualIt = NodeVisuals.find(NodeKey);
		const FPhysicsGraphNodeVisual NodeVisual = VisualIt != NodeVisuals.end()
			? VisualIt->second
			: MakePhysicsGraphNodeVisual(BodySetup->GetBoneName().ToString(), "Body", BodyNodeWidth, 21.0f, GraphNodeTextScale);
		FPhysicsGraphNodePosition& Position = PhysicsGraphNodePositions[NodeKey];
		const ImVec2 NodePos = GraphToScreen(ImVec2(Position.X, Position.Y));
		const ImVec2 NodeSize = GraphSizeToScreen(NodeVisual.Size);
		const ImVec2 NodeMax(NodePos.x + NodeSize.x, NodePos.y + NodeSize.y);
		const bool bSelected = SelectedBodySetup == BodySetup;
		const ImU32 NodeColor = bSelected ? IM_COL32(64, 150, 190, 245) : IM_COL32(70, 74, 82, 245);
		const ImU32 BorderColor = bSelected ? IM_COL32(145, 225, 255, 255) : IM_COL32(115, 120, 132, 255);

		ImGui::SetCursorScreenPos(NodePos);
		ImGui::PushID(BodySetup);
		ImGui::InvisibleButton("##BodyGraphNode", NodeSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const bool bBodyNodeHovered = ImGui::IsItemHovered();
		const bool bBodyNodeActive = ImGui::IsItemActive();
		bAnyGraphNodeHovered |= bBodyNodeHovered;
		bAnyGraphNodeActive |= bBodyNodeActive;
		if (bBodyNodeHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			bPhysicsGraphCapturingMouse = true;
			ImGui::SetNextFrameWantCaptureMouse(true);
			InputSystem::Get().SetGuiMouseCapture(true);
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			SelectedBodySetup = BodySetup;
			SelectedConstraintIndex = -1;
			const int32 BoneIndex = FindBoneIndexByName(Asset, BodySetup->GetBoneName());
			if (BoneIndex >= 0)
			{
				SelectedBoneIndex = BoneIndex;
				ViewportClient.SetSelectedPhysicsBody(SkeletalMesh, BoneIndex, BodySetup);
			}
		}
		if (bBodyNodeActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			bPhysicsGraphCapturingMouse = true;
			ImGui::SetNextFrameWantCaptureMouse(true);
			InputSystem::Get().SetGuiMouseCapture(true);
			const ImVec2 Delta = ImGui::GetIO().MouseDelta;
			Position.X += Delta.x / PhysicsGraphZoom;
			Position.Y += Delta.y / PhysicsGraphZoom;
		}
		if (ImGui::BeginPopupContextItem("##BodyGraphContext"))
		{
			if (RenderConstraintCandidateMenu(SkeletalMesh, PhysAsset, BodySetup))
			{
				if (!PhysicsGraphFocusBodySetup)
				{
					PhysicsGraphFocusBodySetup = GraphRootBody;
				}
				SelectedConstraintIndex = static_cast<int32>(PhysAsset->ConstraintSetups.size()) - 1;
				SelectedBodySetup = nullptr;
				ViewportClient.SetSelectedPhysicsConstraint(SkeletalMesh, SelectedConstraintIndex);
				MarkDirty();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Delete Body"))
			{
				if (RemovePhysicsBodyAndConstraints(PhysAsset, BodySetup))
				{
					if (PhysicsGraphFocusBodySetup == BodySetup)
					{
						PhysicsGraphFocusBodySetup = nullptr;
					}
					SelectedBodySetup = nullptr;
					SelectedConstraintIndex = -1;
					ViewportClient.SetSelectedBone(SkeletalMesh, SelectedBoneIndex);
					MarkDirty();
					ImGui::EndPopup();
					ImGui::PopID();
					DrawList->PopClipRect();
					return;
				}
			}
			ImGui::EndPopup();
		}

		DrawList->AddRectFilled(NodePos, NodeMax, NodeColor, 4.0f);
		DrawList->AddRect(NodePos, NodeMax, BorderColor, 4.0f);
		DrawCenteredPhysicsGraphText(DrawList, NodePos, NodeVisual, PhysicsGraphZoom, GraphNodeTextScale, IM_COL32(238, 240, 244, 255), IM_COL32(190, 196, 205, 255));
		ImGui::PopID();
	}

	const bool bBackgroundClicked = bMouseInCanvas
		&& !bAnyGraphNodeHovered
		&& !bAnyGraphNodeActive
		&& ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	if (bBackgroundClicked)
	{
		bPhysicsGraphPanning = true;
		bPhysicsGraphCapturingMouse = true;
		ImGui::SetNextFrameWantCaptureMouse(true);
		InputSystem::Get().SetGuiMouseCapture(true);
	}

	DrawList->PopClipRect();
	ImGui::SetCursorScreenPos(ImVec2(CanvasPos.x, CanvasMax.y));
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh stats overlay
// ─────────────────────────────────────────────────────────────────────────────

