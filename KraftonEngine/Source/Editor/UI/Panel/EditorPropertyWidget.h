#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Editor/UI/Panel/EditorPropertyRenderer.h"
#include "Object/Object.h"

class UActorComponent;
class AActor;

class FEditorPropertyWidget : public FEditorWidget
{
public:
	virtual void Render(float DeltaTime) override;
	void SetShowEditorOnlyComponents(bool bEnable) { bShowEditorOnlyComponents = bEnable; }
	bool IsShowingEditorOnlyComponents() const { return bShowEditorOnlyComponents; }

private:
	void RenameActor(AActor* PrimaryActor);
	void RenderComponentTree(AActor* Actor);
	void RenderSceneComponentNode(class USceneComponent* Comp);
	void RenderDetails(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);
	void RenderComponentProperties(AActor* Actor, const TArray<AActor*>& SelectedActors);
	void RenderActorProperties(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);

	void PropagatePropertyChange(const FString& PropName, const TArray<AActor*>& SelectedActors);

	void AddComponentToActor(AActor* Actor, UClass* ComponentClass);

	UActorComponent* SelectedComponent = nullptr;
	AActor* LastSelectedActor = nullptr;
	UObject* LastDetailsTarget = nullptr;
	bool bActorSelected = true; // true: Actor details, false: Component details
	bool bShowEditorOnlyComponents = false;
	bool bRestoreDetailsScroll = false;
	bool bDetailsContentInvalidatedThisFrame = false;
	float DetailsScrollY = 0.0f;

	char RenameBuffer[256] = {};
	bool bShowDuplicateWarning = false;

	FEditorPropertyRenderer PropertyRenderer;
};
