#pragma once
#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Editor/UI/Panel/EditorPropertyRenderer.h"

// ===========================
// FPhysicalMaterialEditorWidget
// - UPhysicalMaterial(.uasset)의 friction/restitution 등을 편집하는 에셋 에디터.
// - 리플렉션 기반 EditorPropertyRenderer로 UPROPERTY를 그대로 그린다(Min/Max/Enum 메타 반영).
//   값 변경은 in-place로 쓰이고, Save 버튼을 눌러야 디스크에 기록된다.
// ===========================
class FPhysicalMaterialEditorWidget : public FAssetEditorWidget
{
public:
	FPhysicalMaterialEditorWidget() = default;

	bool CanEdit(UObject* Object) const override;
	void Open(UObject* Object) override;
	void Render(float DeltaTime) override;

private:
	FEditorPropertyRenderer PropertyRenderer;
};
