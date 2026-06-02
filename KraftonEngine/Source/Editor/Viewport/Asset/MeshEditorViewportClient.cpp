#include "MeshEditorViewportClient.h"

#include "Render/Types/MinimalViewInfo.h"
#include "Viewport/Viewport.h"
#include "Math/MathUtils.h"
#include "Input/InputSystem.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Component/Debug/GizmoComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Debug/BoneDebugComponent.h"
#include "Collision/Ray/RayUtils.h"
#include "Settings/EditorSettings.h"
#include "Slate/SlateApplication.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsGeometry.h"
#include "Physics/PhysicsAsset.h"

#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace
{
	constexpr float MatrixDecomposeTolerance = 1.0e-6f;
	constexpr float MinPhysicsBodyGizmoScale = 0.01f;
	constexpr float MinPhysicsConstraintGizmoScale = 0.01f;

	FKShapeElem* GetFirstPhysicsShapeElem(UBodySetup* BodySetup)
	{
		if (!BodySetup)
		{
			return nullptr;
		}

		FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
		if (!AggGeom.SphereElems.empty()) return &AggGeom.SphereElems[0];
		if (!AggGeom.BoxElems.empty()) return &AggGeom.BoxElems[0];
		if (!AggGeom.SphylElems.empty()) return &AggGeom.SphylElems[0];
		return nullptr;
	}

	FTransform MatrixToGizmoTransform(const FMatrix& Matrix)
	{
		FTransform Result;
		Result.Location = Matrix.GetLocation();
		Result.Scale = Matrix.GetScale();

		FMatrix RotationMatrix = Matrix;
		RotationMatrix.M[3][0] = 0.0f;
		RotationMatrix.M[3][1] = 0.0f;
		RotationMatrix.M[3][2] = 0.0f;
		RotationMatrix.M[3][3] = 1.0f;

		if (std::fabs(Result.Scale.X) > MatrixDecomposeTolerance)
		{
			RotationMatrix.M[0][0] /= Result.Scale.X;
			RotationMatrix.M[0][1] /= Result.Scale.X;
			RotationMatrix.M[0][2] /= Result.Scale.X;
		}

		if (std::fabs(Result.Scale.Y) > MatrixDecomposeTolerance)
		{
			RotationMatrix.M[1][0] /= Result.Scale.Y;
			RotationMatrix.M[1][1] /= Result.Scale.Y;
			RotationMatrix.M[1][2] /= Result.Scale.Y;
		}

		if (std::fabs(Result.Scale.Z) > MatrixDecomposeTolerance)
		{
			RotationMatrix.M[2][0] /= Result.Scale.Z;
			RotationMatrix.M[2][1] /= Result.Scale.Z;
			RotationMatrix.M[2][2] /= Result.Scale.Z;
		}

		Result.Rotation = RotationMatrix.ToQuat().GetNormalized();
		return Result;
	}

	FVector ClampBodyScale(const FVector& Scale)
	{
		return FVector(
			std::max(MinPhysicsBodyGizmoScale, Scale.X),
			std::max(MinPhysicsBodyGizmoScale, Scale.Y),
			std::max(MinPhysicsBodyGizmoScale, Scale.Z));
	}

	FVector ClampConstraintScale(const FVector& Scale)
	{
		return FVector(
			std::max(MinPhysicsConstraintGizmoScale, Scale.X),
			std::max(MinPhysicsConstraintGizmoScale, Scale.Y),
			std::max(MinPhysicsConstraintGizmoScale, Scale.Z));
	}

	bool GetBoneWorldTransformByName(USkeletalMeshComponent* MeshComponent, const FName& BoneName, FTransform& OutTransform)
	{
		if (!MeshComponent)
		{
			return false;
		}

		const int32 BoneIndex = MeshComponent->FindBoneIndex(BoneName.ToString());
		return BoneIndex >= 0 && MeshComponent->GetBoneWorldTransformByIndex(BoneIndex, OutTransform);
	}

	FTransform BuildConstraintDisplayWorldTransform(
		const FConstraintSetup& Constraint,
		const FTransform& ParentBoneWorldTransform,
		const FTransform& ChildBoneWorldTransform)
	{
		const FTransform ParentFrameWorld = MatrixToGizmoTransform(Constraint.ParentFrame.ToMatrix() * ParentBoneWorldTransform.ToMatrix());
		const FTransform ChildFrameWorld = MatrixToGizmoTransform(Constraint.ChildFrame.ToMatrix() * ChildBoneWorldTransform.ToMatrix());

		FTransform Result;
		Result.Location = FVector::Lerp(ParentFrameWorld.Location, ChildFrameWorld.Location, 0.5f);
		Result.Rotation = FQuat::Slerp(ParentFrameWorld.Rotation.GetNormalized(), ChildFrameWorld.Rotation.GetNormalized(), 0.5f).GetNormalized();
		Result.Scale = ClampConstraintScale(FVector::Lerp(ParentFrameWorld.Scale, ChildFrameWorld.Scale, 0.5f));
		return Result;
	}

	bool IntersectShapeLocalAABB(const FRay& WorldRay, const FMatrix& ShapeWorldMatrix, const FVector& LocalExtent, float& OutDistance)
	{
		const FMatrix Inverse = ShapeWorldMatrix.GetInverse();
		FRay LocalRay;
		LocalRay.Origin = Inverse.TransformPositionWithW(WorldRay.Origin);
		LocalRay.Direction = Inverse.TransformVector(WorldRay.Direction);
		LocalRay.Direction.Normalize();

		float LocalTMin = 0.0f;
		float LocalTMax = 0.0f;
		if (!FRayUtils::IntersectRayAABB(LocalRay, LocalExtent * -1.0f, LocalExtent, LocalTMin, LocalTMax))
		{
			return false;
		}

		const float HitT = LocalTMin >= 0.0f ? LocalTMin : LocalTMax;
		if (HitT < 0.0f)
		{
			return false;
		}

		const FVector LocalHit = LocalRay.Origin + LocalRay.Direction * HitT;
		const FVector WorldHit = ShapeWorldMatrix.TransformPositionWithW(LocalHit);
		OutDistance = FVector::Distance(WorldRay.Origin, WorldHit);
		return true;
	}

	bool IntersectRaySphere(const FRay& Ray, const FVector& Center, float Radius, float& OutDistance)
	{
		if (Radius <= 0.0f)
		{
			return false;
		}

		const FVector ToCenter = Center - Ray.Origin;
		const float AlongRay = ToCenter.Dot(Ray.Direction);
		if (AlongRay < 0.0f)
		{
			return false;
		}

		const FVector ClosestPoint = Ray.Origin + Ray.Direction * AlongRay;
		if (FVector::DistSquared(ClosestPoint, Center) > Radius * Radius)
		{
			return false;
		}

		OutDistance = AlongRay;
		return true;
	}
}

void FPhysicsBodyTransformGizmoTarget::SetBody(USkeletalMeshComponent* InMeshComp, UBodySetup* InBodySetup)
{
	MeshComponent = InMeshComp;
	BodySetup = InBodySetup;
}

void FPhysicsBodyTransformGizmoTarget::Clear()
{
	MeshComponent = nullptr;
	BodySetup = nullptr;
}

bool FPhysicsBodyTransformGizmoTarget::IsValid() const
{
	if (!MeshComponent || !BodySetup || !GetFirstPhysicsShapeElem(BodySetup))
	{
		return false;
	}

	return MeshComponent->FindBoneIndex(BodySetup->GetBoneName().ToString()) >= 0;
}

UWorld* FPhysicsBodyTransformGizmoTarget::GetWorld() const
{
	return MeshComponent ? MeshComponent->GetWorld() : nullptr;
}

FVector FPhysicsBodyTransformGizmoTarget::GetWorldLocation() const
{
	FTransform WorldTransform;
	return GetShapeWorldTransform(WorldTransform) ? WorldTransform.Location : FVector::ZeroVector;
}

FRotator FPhysicsBodyTransformGizmoTarget::GetWorldRotation() const
{
	FTransform WorldTransform;
	return GetShapeWorldTransform(WorldTransform) ? WorldTransform.GetRotator() : FRotator::ZeroRotator;
}

FQuat FPhysicsBodyTransformGizmoTarget::GetWorldQuat() const
{
	FTransform WorldTransform;
	return GetShapeWorldTransform(WorldTransform) ? WorldTransform.Rotation : FQuat::Identity;
}

FVector FPhysicsBodyTransformGizmoTarget::GetWorldScale() const
{
	FTransform WorldTransform;
	return GetShapeWorldTransform(WorldTransform) ? WorldTransform.Scale : FVector::OneVector;
}

void FPhysicsBodyTransformGizmoTarget::SetWorldLocation(const FVector& NewLocation)
{
	FTransform WorldTransform;
	if (GetShapeWorldTransform(WorldTransform))
	{
		WorldTransform.Location = NewLocation;
		SetShapeWorldTransform(WorldTransform);
	}
}

void FPhysicsBodyTransformGizmoTarget::SetWorldRotation(const FRotator& NewRotation)
{
	FTransform WorldTransform;
	if (GetShapeWorldTransform(WorldTransform))
	{
		WorldTransform.SetRotation(NewRotation);
		SetShapeWorldTransform(WorldTransform);
	}
}

void FPhysicsBodyTransformGizmoTarget::SetWorldRotation(const FQuat& NewQuat)
{
	FTransform WorldTransform;
	if (GetShapeWorldTransform(WorldTransform))
	{
		WorldTransform.SetRotation(NewQuat);
		SetShapeWorldTransform(WorldTransform);
	}
}

void FPhysicsBodyTransformGizmoTarget::SetWorldScale(const FVector& NewScale)
{
	FTransform WorldTransform;
	if (GetShapeWorldTransform(WorldTransform))
	{
		WorldTransform.Scale = ClampBodyScale(NewScale);
		SetShapeWorldTransform(WorldTransform);
	}
}

void FPhysicsBodyTransformGizmoTarget::AddWorldOffset(const FVector& Delta)
{
	SetWorldLocation(GetWorldLocation() + Delta);
}

void FPhysicsBodyTransformGizmoTarget::AddWorldRotation(const FQuat& Delta, bool bWorldSpace)
{
	FTransform WorldTransform;
	if (GetShapeWorldTransform(WorldTransform))
	{
		WorldTransform.Rotation = bWorldSpace
			? (Delta * WorldTransform.Rotation).GetNormalized()
			: (WorldTransform.Rotation * Delta).GetNormalized();
		SetShapeWorldTransform(WorldTransform);
	}
}

void FPhysicsBodyTransformGizmoTarget::AddScaleDelta(const FVector& Delta)
{
	SetWorldScale(GetWorldScale() + Delta);
}

bool FPhysicsBodyTransformGizmoTarget::GetShapeWorldTransform(FTransform& OutTransform) const
{
	FKShapeElem* ShapeElem = GetFirstPhysicsShapeElem(BodySetup);
	if (!MeshComponent || !BodySetup || !ShapeElem)
	{
		return false;
	}

	const int32 BoneIndex = MeshComponent->FindBoneIndex(BodySetup->GetBoneName().ToString());
	if (BoneIndex < 0)
	{
		return false;
	}

	FTransform BoneWorldTransform;
	if (!MeshComponent->GetBoneWorldTransformByIndex(BoneIndex, BoneWorldTransform))
	{
		return false;
	}

	OutTransform = MatrixToGizmoTransform(ShapeElem->Transform.ToMatrix() * BoneWorldTransform.ToMatrix());
	return true;
}

void FPhysicsBodyTransformGizmoTarget::SetShapeWorldTransform(const FTransform& WorldTransform)
{
	FKShapeElem* ShapeElem = GetFirstPhysicsShapeElem(BodySetup);
	if (!MeshComponent || !BodySetup || !ShapeElem)
	{
		return;
	}

	const int32 BoneIndex = MeshComponent->FindBoneIndex(BodySetup->GetBoneName().ToString());
	if (BoneIndex < 0)
	{
		return;
	}

	FTransform BoneWorldTransform;
	if (!MeshComponent->GetBoneWorldTransformByIndex(BoneIndex, BoneWorldTransform))
	{
		return;
	}

	const FMatrix LocalMatrix = WorldTransform.ToMatrix() * BoneWorldTransform.ToMatrix().GetInverse();
	ShapeElem->Transform = MatrixToGizmoTransform(LocalMatrix);
	ShapeElem->Transform.Scale = ClampBodyScale(ShapeElem->Transform.Scale);
	if (OnModified)
	{
		OnModified();
	}
}

void FPhysicsConstraintTransformGizmoTarget::SetConstraint(USkeletalMeshComponent* InMeshComp, UPhysicsAsset* InPhysicsAsset, int32 InConstraintIndex)
{
	MeshComponent = InMeshComp;
	PhysicsAsset = InPhysicsAsset;
	ConstraintIndex = InConstraintIndex;
}

void FPhysicsConstraintTransformGizmoTarget::Clear()
{
	MeshComponent = nullptr;
	PhysicsAsset = nullptr;
	ConstraintIndex = -1;
}

bool FPhysicsConstraintTransformGizmoTarget::IsValid() const
{
	FTransform WorldTransform;
	return GetConstraintWorldTransform(WorldTransform);
}

UWorld* FPhysicsConstraintTransformGizmoTarget::GetWorld() const
{
	return MeshComponent ? MeshComponent->GetWorld() : nullptr;
}

FVector FPhysicsConstraintTransformGizmoTarget::GetWorldLocation() const
{
	FTransform WorldTransform;
	return GetConstraintWorldTransform(WorldTransform) ? WorldTransform.Location : FVector::ZeroVector;
}

FRotator FPhysicsConstraintTransformGizmoTarget::GetWorldRotation() const
{
	FTransform WorldTransform;
	return GetConstraintWorldTransform(WorldTransform) ? WorldTransform.GetRotator() : FRotator::ZeroRotator;
}

FQuat FPhysicsConstraintTransformGizmoTarget::GetWorldQuat() const
{
	FTransform WorldTransform;
	return GetConstraintWorldTransform(WorldTransform) ? WorldTransform.Rotation : FQuat::Identity;
}

FVector FPhysicsConstraintTransformGizmoTarget::GetWorldScale() const
{
	FTransform WorldTransform;
	return GetConstraintWorldTransform(WorldTransform) ? WorldTransform.Scale : FVector::OneVector;
}

void FPhysicsConstraintTransformGizmoTarget::SetWorldLocation(const FVector& NewLocation)
{
	FTransform WorldTransform;
	if (GetConstraintWorldTransform(WorldTransform))
	{
		WorldTransform.Location = NewLocation;
		SetConstraintWorldTransform(WorldTransform);
	}
}

void FPhysicsConstraintTransformGizmoTarget::SetWorldRotation(const FRotator& NewRotation)
{
	FTransform WorldTransform;
	if (GetConstraintWorldTransform(WorldTransform))
	{
		WorldTransform.SetRotation(NewRotation);
		SetConstraintWorldTransform(WorldTransform);
	}
}

void FPhysicsConstraintTransformGizmoTarget::SetWorldRotation(const FQuat& NewQuat)
{
	FTransform WorldTransform;
	if (GetConstraintWorldTransform(WorldTransform))
	{
		WorldTransform.SetRotation(NewQuat);
		SetConstraintWorldTransform(WorldTransform);
	}
}

void FPhysicsConstraintTransformGizmoTarget::SetWorldScale(const FVector& NewScale)
{
	FTransform WorldTransform;
	if (GetConstraintWorldTransform(WorldTransform))
	{
		WorldTransform.Scale = ClampConstraintScale(NewScale);
		SetConstraintWorldTransform(WorldTransform);
	}
}

void FPhysicsConstraintTransformGizmoTarget::AddWorldOffset(const FVector& Delta)
{
	SetWorldLocation(GetWorldLocation() + Delta);
}

void FPhysicsConstraintTransformGizmoTarget::AddWorldRotation(const FQuat& Delta, bool bWorldSpace)
{
	FTransform WorldTransform;
	if (GetConstraintWorldTransform(WorldTransform))
	{
		WorldTransform.Rotation = bWorldSpace
			? (Delta * WorldTransform.Rotation).GetNormalized()
			: (WorldTransform.Rotation * Delta).GetNormalized();
		SetConstraintWorldTransform(WorldTransform);
	}
}

void FPhysicsConstraintTransformGizmoTarget::AddScaleDelta(const FVector& Delta)
{
	SetWorldScale(GetWorldScale() + Delta);
}

bool FPhysicsConstraintTransformGizmoTarget::GetConstraintWorldTransform(FTransform& OutTransform) const
{
	if (!MeshComponent || !PhysicsAsset || ConstraintIndex < 0 || ConstraintIndex >= static_cast<int32>(PhysicsAsset->ConstraintSetups.size()))
	{
		return false;
	}

	const FConstraintSetup& Constraint = PhysicsAsset->ConstraintSetups[ConstraintIndex];
	FTransform ParentBoneWorldTransform;
	FTransform ChildBoneWorldTransform;
	if (!GetBoneWorldTransformByName(MeshComponent, Constraint.ParentBoneName, ParentBoneWorldTransform)
		|| !GetBoneWorldTransformByName(MeshComponent, Constraint.ChildBoneName, ChildBoneWorldTransform))
	{
		return false;
	}

	OutTransform = BuildConstraintDisplayWorldTransform(Constraint, ParentBoneWorldTransform, ChildBoneWorldTransform);
	return true;
}

void FPhysicsConstraintTransformGizmoTarget::SetConstraintWorldTransform(const FTransform& WorldTransform)
{
	if (!MeshComponent || !PhysicsAsset || ConstraintIndex < 0 || ConstraintIndex >= static_cast<int32>(PhysicsAsset->ConstraintSetups.size()))
	{
		return;
	}

	FConstraintSetup& Constraint = PhysicsAsset->ConstraintSetups[ConstraintIndex];
	FTransform ParentBoneWorldTransform;
	FTransform ChildBoneWorldTransform;
	if (!GetBoneWorldTransformByName(MeshComponent, Constraint.ParentBoneName, ParentBoneWorldTransform)
		|| !GetBoneWorldTransformByName(MeshComponent, Constraint.ChildBoneName, ChildBoneWorldTransform))
	{
		return;
	}

	Constraint.ParentFrame = MatrixToGizmoTransform(WorldTransform.ToMatrix() * ParentBoneWorldTransform.ToMatrix().GetInverse());
	Constraint.ChildFrame = MatrixToGizmoTransform(WorldTransform.ToMatrix() * ChildBoneWorldTransform.ToMatrix().GetInverse());
	Constraint.ParentFrame.Scale = ClampConstraintScale(Constraint.ParentFrame.Scale);
	Constraint.ChildFrame.Scale = ClampConstraintScale(Constraint.ChildFrame.Scale);
	if (OnModified)
	{
		OnModified();
	}
}

void FMeshEditorViewportClient::Initialize(ID3D11Device* Device, uint32 Width, uint32 Height)
{
	Viewport = new FViewport();
	Viewport->Initialize(Device, Width, Height);
	Viewport->SetClient(this);

	bIsRenderable = true;
}

void FMeshEditorViewportClient::Release()
{
	if (Viewport)
	{
		Viewport->Release();
		delete Viewport;
		Viewport = nullptr;
	}

	PreviewWorld = nullptr;
	PreviewActor = nullptr;
	OnPhysicsBodyPicked = nullptr;
	OnPhysicsConstraintPicked = nullptr;
	OnPhysicsAssetModified = nullptr;

	UObjectManager::Get().DestroyObject(Gizmo);
	Gizmo = nullptr;
	BoneDebugComponent = nullptr;

	bIsRenderable = false;

	SetSelectedBone(nullptr, -1);
}

void FMeshEditorViewportClient::CreatePreviewGizmo()
{
	Gizmo = UObjectManager::Get().CreateObject<UGizmoComponent>();
	Gizmo->SetScene(&PreviewWorld->GetScene());
	Gizmo->CreateRenderState();
	Gizmo->Deactivate();
}

void FMeshEditorViewportClient::CreateBoneDebugComponent()
{
	BoneDebugComponent = PreviewActor->AddComponent<UBoneDebugComponent>();
	BoneDebugComponent->SetTargetMeshComponent(PreviewMeshComponent);
	BoneDebugComponent->SetSelectedBoneIndex(SelectedBoneIndex);
	BoneDebugComponent->CreateRenderState();
}

void FMeshEditorViewportClient::ResetCameraToPreviousBounds()
{
	if (!PreviewActor)
	{
		ViewTransform.ViewLocation = FVector(-5.0f, -5.0f, 3.0f);
		ViewTransform.LookAt(FVector::ZeroVector);
		TargetLocation = ViewTransform.ViewLocation;
		return;
	}

	FBoundingBox Bounds = PreviewMeshComponent->GetWorldBoundingBox();
	FVector Center = Bounds.GetCenter();
	float Radius = Bounds.GetExtent().Length();

	if (Radius < 0.1f)
	{
		Radius = 1.0f;
	}

	const float FovRadians = ViewTransform.FOV;
	const float Distance = Radius / std::tan(FovRadians * 0.5f) * 1.25f;

	const FVector ViewDir = FVector(-1.0f, -1.0f, -0.6f).Normalized();
	
	ViewTransform.ViewLocation = Center - ViewDir * Distance;
	ViewTransform.LookAt(Center);

	TargetLocation = ViewTransform.ViewLocation;
	LastAppliedCameraLocation = ViewTransform.ViewLocation;
	bTargetLocationInitialized = true;
	bLastAppliedCameraLocationInitialized = true;
}

bool FMeshEditorViewportClient::IsMouseOverViewport() const
{
	if (!bIsRenderable || ViewportScreenRect.Width <= 0.0f || ViewportScreenRect.Height <= 0.0f) return false;

	ImVec2 MousePos = ImGui::GetMousePos();
	return MousePos.x >= ViewportScreenRect.X && MousePos.x <= (ViewportScreenRect.X + ViewportScreenRect.Width) &&
		MousePos.y >= ViewportScreenRect.Y && MousePos.y <= (ViewportScreenRect.Y + ViewportScreenRect.Height);
}

bool FMeshEditorViewportClient::IsGizmoHolding() const
{
	return Gizmo && Gizmo->IsHolding();
}

void FMeshEditorViewportClient::NotifyViewportResized(int32 NewWidth, int32 NewHeight)
{
	if (Viewport)
	{
		ViewTransform.AspectRatio = static_cast<float>(NewWidth) / static_cast<float>(NewHeight);
	}
}

bool FMeshEditorViewportClient::GetCameraView(FMinimalViewInfo& OutPOV) const
{
	OutPOV.Location = ViewTransform.ViewLocation;
	OutPOV.Rotation = ViewTransform.ViewRotation;
	OutPOV.FOV = ViewTransform.FOV;
	OutPOV.AspectRatio = ViewTransform.AspectRatio;
	return true;
}

void FMeshEditorViewportClient::Tick(float DeltaTime)
{
	SyncCameraSmoothingTarget();

	if (bIsFocusAnimating)
	{
		FocusAnimTimer += DeltaTime;
		float Alpha = Clamp(FocusAnimTimer / FocusAnimDuration, 0.0f, 1.0f);
		if (Alpha >= 1.0f)
		{
			Alpha = 1.0f;
			bIsFocusAnimating = false;
		}

		float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);

		FVector NewLoc = FocusStartLoc * (1.0f - SmoothAlpha) + FocusEndLoc * SmoothAlpha;

		FQuat StartQuat = FocusStartRot.ToQuaternion();
		FQuat EndQuat = FocusEndRot.ToQuaternion();
		FQuat BlendedQuat = FQuat::Slerp(StartQuat, EndQuat, SmoothAlpha);

		ViewTransform.ViewLocation = NewLoc;
		ViewTransform.ViewRotation = FRotator::FromQuaternion(BlendedQuat);

		TargetLocation = NewLoc;
		LastAppliedCameraLocation = NewLoc;
		bLastAppliedCameraLocationInitialized = true;
	}
	else
	{
		ApplySmoothedCameraLocation(DeltaTime);
	}

	TickShortcuts();
	TickInput(DeltaTime);
	TickInteraction(DeltaTime);
}

void FMeshEditorViewportClient::SetSelectedBone(USkeletalMesh* Mesh, int32 BoneIndex)
{
	SelectedMesh = Mesh;
	SelectedBoneIndex = BoneIndex;
	SelectedPhysicsBodySetup = nullptr;
	SelectedPhysicsConstraintIndex = -1;
	RenderOptions.WeightBoneHeatMapBoneIndex = BoneIndex;
	PhysicsBodyTarget.Clear();
	PhysicsConstraintTarget.Clear();

	if (Gizmo && PreviewMeshComponent && BoneIndex >= 0)
	{
		BoneTarget.SetBone(PreviewMeshComponent, BoneIndex);
		Gizmo->SetTarget(&BoneTarget);
	}
	else if (Gizmo)
	{
		Gizmo->Deactivate();
	}

	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetTargetMeshComponent(PreviewMeshComponent);
		BoneDebugComponent->SetSelectedBoneIndex(BoneIndex);
		BoneDebugComponent->SetSelectedPhysicsBodySetup(nullptr);
		BoneDebugComponent->SetSelectedPhysicsConstraintIndex(-1);
	}
}

void FMeshEditorViewportClient::SetSelectedPhysicsBody(USkeletalMesh* Mesh, int32 BoneIndex, UBodySetup* BodySetup)
{
	SelectedMesh = Mesh;
	SelectedBoneIndex = BoneIndex;
	SelectedPhysicsBodySetup = BodySetup;
	SelectedPhysicsConstraintIndex = -1;
	RenderOptions.WeightBoneHeatMapBoneIndex = BoneIndex;

	PhysicsBodyTarget.SetBody(PreviewMeshComponent, BodySetup);
	PhysicsConstraintTarget.Clear();

	if (Gizmo && PhysicsBodyTarget.IsValid())
	{
		Gizmo->SetTarget(&PhysicsBodyTarget);
	}
	else if (Gizmo)
	{
		Gizmo->Deactivate();
	}

	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetTargetMeshComponent(PreviewMeshComponent);
		BoneDebugComponent->SetSelectedBoneIndex(BoneIndex);
		BoneDebugComponent->SetSelectedPhysicsBodySetup(BodySetup);
		BoneDebugComponent->SetSelectedPhysicsConstraintIndex(-1);
	}
}

void FMeshEditorViewportClient::SetSelectedPhysicsConstraint(USkeletalMesh* Mesh, int32 ConstraintIndex)
{
	SelectedMesh = Mesh;
	SelectedPhysicsBodySetup = nullptr;
	SelectedPhysicsConstraintIndex = ConstraintIndex;

	UPhysicsAsset* PhysicsAsset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!PhysicsAsset || ConstraintIndex < 0 || ConstraintIndex >= static_cast<int32>(PhysicsAsset->ConstraintSetups.size()))
	{
		SelectedBoneIndex = -1;
		PhysicsBodyTarget.Clear();
		PhysicsConstraintTarget.Clear();
		if (Gizmo)
		{
			Gizmo->Deactivate();
		}
		if (BoneDebugComponent)
		{
			BoneDebugComponent->SetSelectedPhysicsBodySetup(nullptr);
			BoneDebugComponent->SetSelectedPhysicsConstraintIndex(-1);
		}
		return;
	}

	const FConstraintSetup& Constraint = PhysicsAsset->ConstraintSetups[ConstraintIndex];
	const int32 ChildBoneIndex = PreviewMeshComponent ? PreviewMeshComponent->FindBoneIndex(Constraint.ChildBoneName.ToString()) : -1;
	SelectedBoneIndex = ChildBoneIndex;
	RenderOptions.WeightBoneHeatMapBoneIndex = ChildBoneIndex;

	PhysicsBodyTarget.Clear();
	PhysicsConstraintTarget.SetConstraint(PreviewMeshComponent, PhysicsAsset, ConstraintIndex);

	if (Gizmo && PhysicsConstraintTarget.IsValid())
	{
		Gizmo->SetTarget(&PhysicsConstraintTarget);
	}
	else if (Gizmo)
	{
		Gizmo->Deactivate();
	}

	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetTargetMeshComponent(PreviewMeshComponent);
		BoneDebugComponent->SetSelectedBoneIndex(ChildBoneIndex);
		BoneDebugComponent->SetSelectedPhysicsBodySetup(nullptr);
		BoneDebugComponent->SetSelectedPhysicsConstraintIndex(ConstraintIndex);
	}
}

const FBone* FMeshEditorViewportClient::GetSelectedBone() const
{
	if (!SelectedMesh) return nullptr;

	FSkeletalMesh* Asset = SelectedMesh->GetSkeletalMeshAsset();
	if (!Asset) return nullptr;

	if (SelectedBoneIndex < 0 || SelectedBoneIndex >= static_cast<int32>(Asset->Bones.size())) return nullptr;

	return &Asset->Bones[SelectedBoneIndex];
}

EBoneDebugDrawMode FMeshEditorViewportClient::GetBoneDebugDrawMode() const
{
	return BoneDebugComponent ? BoneDebugComponent->GetDrawMode() : EBoneDebugDrawMode::SelectedOnly;
}

void FMeshEditorViewportClient::SetBoneDebugDrawMode(EBoneDebugDrawMode InDrawMode)
{
	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetDrawMode(InDrawMode);
	}
}

void FMeshEditorViewportClient::SetPhysicsAssetDebugDrawEnabled(bool bEnabled)
{
	bPhysicsAssetDebugDrawEnabled = bEnabled;
	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetDrawPhysicsAsset(bEnabled);
	}
}

void FMeshEditorViewportClient::SetPhysicsAssetSolidDebugDrawEnabled(bool bEnabled)
{
	SetPhysicsAssetBodyShowMode(bEnabled ? EPhysicsAssetBodyShowMode::Solid : EPhysicsAssetBodyShowMode::Wireframe);
}

void FMeshEditorViewportClient::SetPhysicsAssetBodyShowMode(EPhysicsAssetBodyShowMode InMode)
{
	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetPhysicsAssetBodyShowMode(InMode);
	}
}

void FMeshEditorViewportClient::SetPhysicsAssetConstraintShowMode(EPhysicsAssetConstraintShowMode InMode)
{
	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetPhysicsAssetConstraintShowMode(InMode);
	}
}

void FMeshEditorViewportClient::SetSelectedPhysicsConstraintIndex(int32 ConstraintIndex)
{
	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetSelectedPhysicsConstraintIndex(ConstraintIndex);
	}
}

void FMeshEditorViewportClient::RefreshPhysicsAssetDebugDraw()
{
	if (BoneDebugComponent)
	{
		BoneDebugComponent->MarkRenderStateDirty();
	}
	if (Gizmo && Gizmo->HasTarget())
	{
		Gizmo->UpdateGizmoTransform();
	}
}

void FMeshEditorViewportClient::SetOnPhysicsAssetModified(std::function<void()> InCallback)
{
	OnPhysicsAssetModified = std::move(InCallback);
	PhysicsBodyTarget.SetOnModified(OnPhysicsAssetModified);
	PhysicsConstraintTarget.SetOnModified(OnPhysicsAssetModified);
}

void FMeshEditorViewportClient::TickShortcuts()
{
	if (!FSlateApplication::Get().DoesClientOwnKeyboardInput(this)) return;

	if (InputSystem::Get().GetKeyDown('F'))
	{
		if (const FBone* SelectedBone = GetSelectedBone())
		{
			FVector TargetLoc = PreviewMeshComponent->GetBoneLocationByIndex(SelectedBoneIndex);

			FVector OriginalLoc = ViewTransform.ViewLocation;
			FRotator OriginalRot = ViewTransform.ViewRotation;

			FBoundingBox Bounds = PreviewMeshComponent->GetWorldBoundingBox();
			FVector Center = Bounds.GetCenter();
			float Radius = Bounds.GetExtent().Length();

			if (Radius < 0.1f)
			{
				Radius = 1.0f;
			}

			float FocusDistance = Radius;
			FVector CameraForward = ViewTransform.ViewRotation.GetForwardVector();
			FVector NewCameraLoc = TargetLoc - CameraForward * FocusDistance;

			ViewTransform.ViewLocation = NewCameraLoc;
			ViewTransform.LookAt(TargetLoc);
			FRotator TargetRot = ViewTransform.ViewRotation;

			ViewTransform.ViewLocation = OriginalLoc;
			ViewTransform.ViewRotation = OriginalRot;

			bIsFocusAnimating = true;
			FocusAnimTimer = 0.0f;
			FocusStartLoc = OriginalLoc;
			FocusStartRot = OriginalRot;
			FocusEndLoc = NewCameraLoc;
			FocusEndRot = TargetRot;
		}
	}
}

void FMeshEditorViewportClient::TickInput(float DeltaTime)
{
	if (!FSlateApplication::Get().DoesClientOwnMouseInput(this)) return;

	// 텍스트 입력 중에는 카메라 키/마우스 조작을 가로채지 않는다.
	if (ImGui::GetIO().WantTextInput) return;

	FViewportCameraControlSettings& ControlSettings = FEditorSettings::Get().MeshEditorViewportSettings.CameraControls;

	InputSystem& Input = InputSystem::Get();
	
	FVector LocalMove = FVector::ZeroVector;
	float WorldVerticalMove = 0.0f;
	float CameraSpeed = ControlSettings.MoveSpeed;

	if (Input.GetKey('W')) LocalMove.X += CameraSpeed;
	if (Input.GetKey('S')) LocalMove.X -= CameraSpeed;
	if (Input.GetKey('D')) LocalMove.Y += CameraSpeed;
	if (Input.GetKey('A')) LocalMove.Y -= CameraSpeed;
	if (Input.GetKey('Q')) WorldVerticalMove -= CameraSpeed;
	if (Input.GetKey('E')) WorldVerticalMove += CameraSpeed;

	const FVector Forward = ViewTransform.ViewRotation.GetForwardVector();
	const FVector Right = ViewTransform.ViewRotation.GetRightVector();
	const FVector Up = ViewTransform.ViewRotation.GetUpVector();

	FVector DeltaMove = (Forward * LocalMove.X + Right * LocalMove.Y) * DeltaTime;
	DeltaMove.Z += WorldVerticalMove * DeltaTime;
	TargetLocation += DeltaMove;

	FVector Rotation = FVector::ZeroVector;

	FVector MouseRotation = FVector::ZeroVector;
	float MouseRotationSpeed = 0.15f * ControlSettings.RotationSpeed;

	if (Input.GetKey(VK_RBUTTON))
	{
		float DeltaX = static_cast<float>(Input.MouseDeltaX());
		float DeltaY = static_cast<float>(Input.MouseDeltaY());

		MouseRotation.Y += DeltaX * MouseRotationSpeed;
		MouseRotation.Z += DeltaY * MouseRotationSpeed;
	}

	Rotation *= DeltaTime;
	ViewTransform.Rotate(Rotation.Y + MouseRotation.Y, Rotation.Z + MouseRotation.Z);

	if (Input.GetKeyUp(VK_SPACE))
	{
		Gizmo->SetNextMode();
	}
}

void FMeshEditorViewportClient::TickInteraction(float DeltaTime)
{
	if (!FSlateApplication::Get().DoesClientOwnMouseInput(this)) return;

	if (!Gizmo || !PreviewWorld) return;

	FViewportCameraControlSettings& ControlSettings = FEditorSettings::Get().MeshEditorViewportSettings.CameraControls;

	Gizmo->ApplyScreenSpaceScaling(ViewTransform.ViewLocation, ViewTransform.bIsOrtho, ViewTransform.OrthoZoom);
	Gizmo->SetAxisMask(UGizmoComponent::ComputeAxisMask(RenderOptions.ViewportType, Gizmo->GetMode()));

	const float ZoomSpeed = ControlSettings.ZoomSpeed;

	float ScrollNotches = InputSystem::Get().GetScrollNotches();
	if (ScrollNotches != 0.0f)
	{
		if (bool bIsRightButtonDown = InputSystem::Get().GetKey(VK_RBUTTON))
		{
			float& MoveSpeed = FEditorSettings::Get().MeshEditorViewportSettings.CameraControls.MoveSpeed;
			if (ScrollNotches < 0.0f)
			{
				MoveSpeed = MoveSpeed * 0.9f;
			}
			else
			{
				MoveSpeed = MoveSpeed * 1.1f;
			}

			if (MoveSpeed > 1000.0f)
			{
				MoveSpeed = 1000.0f;
			}
			if (MoveSpeed < 0.001f)
			{
				MoveSpeed = 0.001f;
			}
		}
		else
		{
			if (ViewTransform.bIsOrtho)
			{
				// D.2: ViewTransform 직접 갱신.
				float NewWidth = ViewTransform.OrthoZoom - ScrollNotches * ZoomSpeed * DeltaTime;
				ViewTransform.OrthoZoom = Clamp(NewWidth, 0.1f, 1000.0f);
			}
			else
			{
				//foot zoom 발줌은 절대 delta time를 곱하지 않음. 노치당 이동 거리가 일정해야 하기 때문.
				// Instead of moving directly, update TargetLocation for smooth zoom
				TargetLocation += ViewTransform.ViewRotation.GetForwardVector() * (ScrollNotches * ZoomSpeed * 0.015f);
				// UnrealEngine의 Mouse Scroll Camera Speed는 노치당 5
			}
		}
	}


	ImVec2 MousePos = ImGui::GetIO().MousePos;
	float LocalMouseX = MousePos.x - ViewportScreenRect.X;
	float LocalMouseY = MousePos.y - ViewportScreenRect.Y;

	float VPWidth = Viewport ? static_cast<float>(Viewport->GetWidth()) : 1.0f;
	float VPHeight = Viewport ? static_cast<float>(Viewport->GetHeight()) : 1.0f;

	FMinimalViewInfo POV;
	GetCameraView(POV);
	FRay Ray = POV.DeprojectScreenToWorld(LocalMouseX, LocalMouseY, VPWidth, VPHeight);
	FHitResult HitResult;

	FRayUtils::RaycastComponent(Gizmo, Ray, HitResult);

	InputSystem& Input = InputSystem::Get();

	if (Input.GetKeyDown(VK_LBUTTON))
	{
		HandleDragStart(Ray);
	}
	else if (Input.GetLeftDragging())
	{
		if (Gizmo->IsPressedOnHandle() && !Gizmo->IsHolding())
		{
			Gizmo->SetHolding(true);
		}

		if (Gizmo->IsHolding())
		{
			Gizmo->UpdateDrag(Ray);
		}
	}
	else if (Input.GetLeftDragEnd())
	{
		if (Gizmo->IsHolding())
		{
			Gizmo->DragEnd();
		}
	}
	else if (Input.GetKeyUp(VK_LBUTTON))
	{
		Gizmo->SetPressedOnHandle(false);
	}
}

void FMeshEditorViewportClient::SyncCameraSmoothingTarget()
{
	const FVector CurrentLocation = ViewTransform.ViewLocation;
	const bool bCameraMovedExternally = bLastAppliedCameraLocationInitialized
		&& FVector::DistSquared(CurrentLocation, LastAppliedCameraLocation) > 0.0001f;

	if (!bTargetLocationInitialized || bCameraMovedExternally)
	{
		TargetLocation = CurrentLocation;
		bTargetLocationInitialized = true;
	}

	LastAppliedCameraLocation = CurrentLocation;
	bLastAppliedCameraLocationInitialized = true;
}

void FMeshEditorViewportClient::ApplySmoothedCameraLocation(float DeltaTime)
{
	const FVector CurrentLocation = ViewTransform.ViewLocation;
	const float LerpAlpha = Clamp(DeltaTime * SmoothLocationSpeed, 0.0f, 1.0f);
	const FVector NewLocation = CurrentLocation + (TargetLocation - CurrentLocation) * LerpAlpha;
	ViewTransform.ViewLocation = NewLocation;

	LastAppliedCameraLocation = NewLocation;
	bLastAppliedCameraLocationInitialized = true;
}

void FMeshEditorViewportClient::SyncGizmo()
{
	if (!Gizmo || !PreviewActor) return;

	if (const FBone* SelectedBone = GetSelectedBone())
	{
	}
	else
	{
		Gizmo->Deactivate();
	}
}

void FMeshEditorViewportClient::ApplyTransformSettingsToGizmo()
{
	if (!Gizmo) return;

	const FGizmoToolSettings& Settings = FEditorSettings::Get().MeshEditorViewportSettings.Gizmo;
	const bool bForceLocalForScale = Gizmo->GetMode() == EGizmoMode::Scale;

	Gizmo->SetWorldSpace(bForceLocalForScale ? false : Settings.CoordSystem == EEditorCoordSystem::World);
	Gizmo->SetSnapSettings(
		Settings.bEnableTranslationSnap, Settings.TranslationSnapSize,
		Settings.bEnableRotationSnap, Settings.RotationSnapSize,
		Settings.bEnableScaleSnap, Settings.ScaleSnapSize
	);
}

void FMeshEditorViewportClient::HandleDragStart(const FRay& Ray)
{
	FHitResult Hit;
	if (FRayUtils::RaycastComponent(Gizmo, Ray, Hit))
	{
		Gizmo->SetPressedOnHandle(true);
		return;
	}

	if (TryPickPhysicsAssetConstraint(Ray))
	{
		return;
	}

	if (TryPickPhysicsAssetBody(Ray))
	{
		return;
	}

	if (bPhysicsAssetDebugDrawEnabled)
	{
		USkeletalMesh* Mesh = PreviewMeshComponent ? PreviewMeshComponent->GetSkeletalMesh() : nullptr;
		SetSelectedBone(Mesh, -1);
		if (OnPhysicsAssetPickMissed)
		{
			OnPhysicsAssetPickMissed();
		}
	}
}

bool FMeshEditorViewportClient::TryPickPhysicsAssetConstraint(const FRay& Ray)
{
	if (!PreviewMeshComponent)
	{
		return false;
	}

	USkeletalMesh* Mesh = PreviewMeshComponent->GetSkeletalMesh();
	UPhysicsAsset* PhysicsAsset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!PhysicsAsset || PhysicsAsset->ConstraintSetups.empty())
	{
		return false;
	}

	int32 BestConstraintIndex = -1;
	float BestDistance = FLT_MAX;

	for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(PhysicsAsset->ConstraintSetups.size()); ++ConstraintIndex)
	{
		const FConstraintSetup& Constraint = PhysicsAsset->ConstraintSetups[ConstraintIndex];

		FTransform ParentBoneWorldTransform;
		FTransform ChildBoneWorldTransform;
		if (!GetBoneWorldTransformByName(PreviewMeshComponent, Constraint.ParentBoneName, ParentBoneWorldTransform)
			|| !GetBoneWorldTransformByName(PreviewMeshComponent, Constraint.ChildBoneName, ChildBoneWorldTransform))
		{
			continue;
		}

		const FTransform ConstraintWorldTransform = BuildConstraintDisplayWorldTransform(
			Constraint,
			ParentBoneWorldTransform,
			ChildBoneWorldTransform);
		const float BoneDistance = FVector::Distance(ParentBoneWorldTransform.Location, ChildBoneWorldTransform.Location);
		const float AutoRadius = FMath::Clamp(BoneDistance * 0.35f, 0.025f, 0.35f);
		const float PickRadius = std::max(0.025f, AutoRadius * 0.3f * 1.6f);

		float Distance = 0.0f;
		if (IntersectRaySphere(Ray, ConstraintWorldTransform.Location, PickRadius, Distance) && Distance < BestDistance)
		{
			BestDistance = Distance;
			BestConstraintIndex = ConstraintIndex;
		}
	}

	if (BestConstraintIndex < 0)
	{
		return false;
	}

	SetSelectedPhysicsConstraint(Mesh, BestConstraintIndex);
	if (OnPhysicsConstraintPicked)
	{
		OnPhysicsConstraintPicked(BestConstraintIndex);
	}
	return true;
}

bool FMeshEditorViewportClient::TryPickPhysicsAssetBody(const FRay& Ray)
{
	if (!PreviewMeshComponent)
	{
		return false;
	}

	USkeletalMesh* Mesh = PreviewMeshComponent->GetSkeletalMesh();
	UPhysicsAsset* PhysicsAsset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!PhysicsAsset || !PhysicsAsset->HasAnyBodySetup())
	{
		return false;
	}

	UBodySetup* BestBody = nullptr;
	int32 BestBoneIndex = -1;
	float BestDistance = FLT_MAX;

	for (UBodySetup* BodySetup : PhysicsAsset->BodySetups)
	{
		if (!BodySetup || !BodySetup->HasGeometry())
		{
			continue;
		}

		const int32 BoneIndex = PreviewMeshComponent->FindBoneIndex(BodySetup->GetBoneName().ToString());
		if (BoneIndex < 0)
		{
			continue;
		}

		FTransform BoneWorldTransform;
		if (!PreviewMeshComponent->GetBoneWorldTransformByIndex(BoneIndex, BoneWorldTransform))
		{
			continue;
		}

		const FMatrix BoneWorldMatrix = BoneWorldTransform.ToMatrix();
		const FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();

		auto TestShape = [&](const FMatrix& LocalShapeMatrix, const FVector& LocalExtent)
		{
			float Distance = 0.0f;
			const FMatrix ShapeWorldMatrix = LocalShapeMatrix * BoneWorldMatrix;
			if (IntersectShapeLocalAABB(Ray, ShapeWorldMatrix, LocalExtent, Distance) && Distance < BestDistance)
			{
				BestDistance = Distance;
				BestBody = BodySetup;
				BestBoneIndex = BoneIndex;
			}
		};

		for (const FKSphereElem& Sphere : AggGeom.SphereElems)
		{
			TestShape(Sphere.Transform.ToMatrix(), FVector(Sphere.Radius, Sphere.Radius, Sphere.Radius));
		}
		for (const FKBoxElem& Box : AggGeom.BoxElems)
		{
			TestShape(Box.Transform.ToMatrix(), Box.Extent);
		}
		for (const FKSphylElem& Sphyl : AggGeom.SphylElems)
		{
			const float HalfHeight = Sphyl.Length * 0.5f + Sphyl.Radius;
			TestShape(Sphyl.Transform.ToMatrix(), FVector(Sphyl.Radius, Sphyl.Radius, HalfHeight));
		}
	}

	if (!BestBody)
	{
		return false;
	}

	SetSelectedPhysicsBody(Mesh, BestBoneIndex, BestBody);
	if (OnPhysicsBodyPicked)
	{
		OnPhysicsBodyPicked(BestBoneIndex, BestBody);
	}
	return true;
}
