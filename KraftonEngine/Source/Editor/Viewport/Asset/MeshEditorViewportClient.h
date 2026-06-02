#pragma once

#include "Viewport/EditorPreviewViewportClient.h"
#include "Viewport/ViewportClient.h"
#include "Editor/Viewport/ViewportCameraTransform.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Editor/Slate/SWindow.h"
#include "Core/Types/RayTypes.h"
#include "Gizmo/BoneTransformGizmoTarget.h"
#include "Gizmo/GizmoTransformTarget.h"
#include "Component/Debug/BoneDebugComponent.h"

#include <d3d11.h>
#include <functional>

class UGizmoComponent;
class FWindowsWindow;
class UWorld;
class AActor;
class USkeletalMesh;
class USkeletalMeshComponent;
class UBodySetup;
class UPhysicsAsset;

class FPhysicsBodyTransformGizmoTarget : public IGizmoTransformTarget
{
public:
	void SetBody(USkeletalMeshComponent* InMeshComp, UBodySetup* InBodySetup);
	void Clear();
	void SetOnModified(std::function<void()> InCallback) { OnModified = std::move(InCallback); }

	bool IsValid() const override;
	UWorld* GetWorld() const override;

	FVector GetWorldLocation() const override;
	FRotator GetWorldRotation() const override;
	FQuat GetWorldQuat() const override;
	FVector GetWorldScale() const override;

	void SetWorldLocation(const FVector& NewLocation) override;
	void SetWorldRotation(const FRotator& NewRotation) override;
	void SetWorldRotation(const FQuat& NewQuat) override;
	void SetWorldScale(const FVector& NewScale) override;

	void AddWorldOffset(const FVector& Delta) override;
	void AddWorldRotation(const FQuat& Delta, bool bWorldSpace) override;
	void AddScaleDelta(const FVector& Delta) override;

private:
	bool GetShapeWorldTransform(FTransform& OutTransform) const;
	void SetShapeWorldTransform(const FTransform& WorldTransform);

	USkeletalMeshComponent* MeshComponent = nullptr;
	UBodySetup* BodySetup = nullptr;
	std::function<void()> OnModified;
};

class FPhysicsConstraintTransformGizmoTarget : public IGizmoTransformTarget
{
public:
	void SetConstraint(USkeletalMeshComponent* InMeshComp, UPhysicsAsset* InPhysicsAsset, int32 InConstraintIndex);
	void Clear();
	void SetOnModified(std::function<void()> InCallback) { OnModified = std::move(InCallback); }

	bool IsValid() const override;
	UWorld* GetWorld() const override;

	FVector GetWorldLocation() const override;
	FRotator GetWorldRotation() const override;
	FQuat GetWorldQuat() const override;
	FVector GetWorldScale() const override;

	void SetWorldLocation(const FVector& NewLocation) override;
	void SetWorldRotation(const FRotator& NewRotation) override;
	void SetWorldRotation(const FQuat& NewQuat) override;
	void SetWorldScale(const FVector& NewScale) override;

	void AddWorldOffset(const FVector& Delta) override;
	void AddWorldRotation(const FQuat& Delta, bool bWorldSpace) override;
	void AddScaleDelta(const FVector& Delta) override;

private:
	bool GetConstraintWorldTransform(FTransform& OutTransform) const;
	void SetConstraintWorldTransform(const FTransform& WorldTransform);

	USkeletalMeshComponent* MeshComponent = nullptr;
	UPhysicsAsset* PhysicsAsset = nullptr;
	int32 ConstraintIndex = -1;
	std::function<void()> OnModified;
};

class FMeshEditorViewportClient : public FViewportClient, public IEditorPreviewViewportClient
{
public:
	void Initialize(ID3D11Device* Device, uint32 Width, uint32 Height);
	void Release();

	void CreatePreviewGizmo();
	void CreateBoneDebugComponent();
	void ResetCameraToPreviousBounds();

	void SetPreviewWorld(UWorld* InWorld) { PreviewWorld = InWorld; }
	void SetPreviewActor(AActor* InActor) { PreviewActor = InActor; }
	void SetPreviewMeshComponent(USkeletalMeshComponent* InComp) { PreviewMeshComponent = InComp; }
	void SetViewportRect(float X, float Y, float Width, float Height) { ViewportScreenRect = { X, Y, Width, Height }; }

	bool IsRenderable() const override { return bIsRenderable; }
	bool IsMouseOverViewport() const override;

	bool IsGizmoHolding() const;

	FViewport* GetViewport() const override { return Viewport; }
	UWorld* GetPreviewWorld() const override { return PreviewWorld; }

	UGizmoComponent* GetGizmo() const { return Gizmo; }
	USkeletalMeshComponent* GetPreviewMeshComponent() const { return PreviewMeshComponent; }

	FViewportRenderOptions& GetRenderOptions() override { return RenderOptions; }
	const FViewportRenderOptions& GetRenderOptions() const override { return RenderOptions; }

	void NotifyViewportResized(int32 NewWidth, int32 NewHeight) override;

	bool GetCameraView(FMinimalViewInfo& OutPOV) const override;

	void Tick(float DeltaTime);

	void SetSelectedBone(USkeletalMesh* Mesh, int32 BoneIndex);
	void SetSelectedPhysicsBody(USkeletalMesh* Mesh, int32 BoneIndex, UBodySetup* BodySetup);
	void SetSelectedPhysicsConstraint(USkeletalMesh* Mesh, int32 ConstraintIndex);
	const FBone* GetSelectedBone() const;
	void SetOnPhysicsBodyPicked(std::function<void(int32, UBodySetup*)> InCallback) { OnPhysicsBodyPicked = std::move(InCallback); }
	void SetOnPhysicsConstraintPicked(std::function<void(int32)> InCallback) { OnPhysicsConstraintPicked = std::move(InCallback); }
	void SetOnPhysicsAssetPickMissed(std::function<void()> InCallback) { OnPhysicsAssetPickMissed = std::move(InCallback); }
	void SetOnPhysicsAssetModified(std::function<void()> InCallback);

	EBoneDebugDrawMode GetBoneDebugDrawMode() const;
	void SetBoneDebugDrawMode(EBoneDebugDrawMode InDrawMode);
	void SetPhysicsAssetDebugDrawEnabled(bool bEnabled);
	void SetPhysicsAssetSolidDebugDrawEnabled(bool bEnabled);
	void SetPhysicsAssetBodyShowMode(EPhysicsAssetBodyShowMode InMode);
	void SetPhysicsAssetConstraintShowMode(EPhysicsAssetConstraintShowMode InMode);
	void SetSelectedPhysicsConstraintIndex(int32 ConstraintIndex);
	void RefreshPhysicsAssetDebugDraw();

	void ApplyTransformSettingsToGizmo();

private:
	void TickShortcuts();
	void TickInput(float DeltaTime);
	void TickInteraction(float DeltaTime);
	void SyncCameraSmoothingTarget();
	void ApplySmoothedCameraLocation(float DeltaTime);

	void SyncGizmo();

	void HandleDragStart(const FRay& Ray);
	bool TryPickPhysicsAssetConstraint(const FRay& Ray);
	bool TryPickPhysicsAssetBody(const FRay& Ray);

private:
	USkeletalMesh* SelectedMesh = nullptr;
	int32 SelectedBoneIndex = -1;
	UBodySetup* SelectedPhysicsBodySetup = nullptr;
	int32 SelectedPhysicsConstraintIndex = -1;

	FViewport* Viewport = nullptr;
	FWindowsWindow* Window = nullptr;
	FViewportRenderOptions RenderOptions;

	FBoneTransformGizmoTarget BoneTarget;
	FPhysicsBodyTransformGizmoTarget PhysicsBodyTarget;
	FPhysicsConstraintTransformGizmoTarget PhysicsConstraintTarget;
	UGizmoComponent* Gizmo = nullptr;
	USkeletalMeshComponent* PreviewMeshComponent = nullptr;
	UBoneDebugComponent* BoneDebugComponent = nullptr;
	std::function<void(int32, UBodySetup*)> OnPhysicsBodyPicked;
	std::function<void(int32)> OnPhysicsConstraintPicked;
	std::function<void()> OnPhysicsAssetPickMissed;
	std::function<void()> OnPhysicsAssetModified;

	UWorld* PreviewWorld = nullptr;
	AActor* PreviewActor = nullptr;

	bool bIsRenderable = false;
	bool bPhysicsAssetDebugDrawEnabled = false;

	FViewportCameraTransform ViewTransform;

	FRect ViewportScreenRect;

	// Camera Focus Animation
	bool bIsFocusAnimating = false;
	FVector FocusStartLoc;
	FRotator FocusStartRot;
	FVector FocusEndLoc;
	FRotator FocusEndRot;
	float FocusAnimTimer = 0.0f;
	const float FocusAnimDuration = 0.5f;

	// Camera Smoothing
	FVector TargetLocation;
	bool bTargetLocationInitialized = false;
	FVector LastAppliedCameraLocation;
	bool bLastAppliedCameraLocationInitialized = false;
	const float SmoothLocationSpeed = 10.0f;
};
