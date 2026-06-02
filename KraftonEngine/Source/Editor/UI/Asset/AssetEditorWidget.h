#pragma once
#include "UI/EditorWidget.h"

class UObject;
class IEditorPreviewViewportClient;

class FAssetEditorWidget : public FEditorWidget
{
public:
	virtual ~FAssetEditorWidget() = default;

	virtual bool CanEdit(UObject* Object) const = 0;

	virtual void Open(UObject* Object);
	virtual void Close();
	virtual void Tick(float DeltaTime) {}
	// 기존 에디터 재사용 시 PhysicsAsset 요청이면 PhysicalAsset 탭으로 전환하기 위한 hook.
	virtual void FocusObject(UObject* Object) { (void)Object; RequestFocus(); }

	virtual void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const {}

	virtual bool AllowsMultipleInstances() const { return false; }
	virtual bool IsEditingObject(UObject* Object) const { return IsOpen() && EditedObject == Object; }

	UObject* GetEditedObject() const { return EditedObject; }
	bool IsOpen() const { return bOpen; }
	void RequestFocus() { bFocusRequested = true; }

protected:
	void MarkDirty() { bDirty = true; }
	void ClearDirty() { bDirty = false; }
	bool IsDirty() const { return bDirty; }
	bool ConsumeFocusRequest()
	{
		const bool bWasRequested = bFocusRequested;
		bFocusRequested = false;
		return bWasRequested;
	}

protected:
	UObject* EditedObject = nullptr;
	bool bOpen = false;
	bool bDirty = false;
	bool bFocusRequested = false;
};
