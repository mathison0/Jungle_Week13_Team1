#include "PhysicalMaterialEditorWidget.h"

#include "Physics/PhysicsMaterial/PhysicalMaterial.h"
#include "Physics/PhysicsMaterial/PhysicalMaterialManager.h"
#include "Object/Object.h"

#include <imgui.h>
#include <cstring>

bool FPhysicalMaterialEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UPhysicalMaterial>();
}

void FPhysicalMaterialEditorWidget::Open(UObject* Object)
{
	if (!CanEdit(Object))
	{
		return;
	}

	EditedObject = Object;
	bOpen = true;
	ClearDirty();
}

void FPhysicalMaterialEditorWidget::Render(float DeltaTime)
{
	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	UPhysicalMaterial* Material = static_cast<UPhysicalMaterial*>(EditedObject);

	bool bWindowOpen = true;
	FString VisibleTitle = "Physical Material Editor";
	if (!Material->GetSourcePath().empty())
	{
		VisibleTitle += " - ";
		VisibleTitle += Material->GetSourcePath();
	}
	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	ImGui::SetNextWindowSize(ImVec2(440.0f, 400.0f), ImGuiCond_Once);

	FString WindowTitle = VisibleTitle + "###PhysicalMaterialEditor";
	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen))
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			Close();
		}
		return;
	}

	if (ImGui::Button("Save"))
	{
		if (FPhysicalMaterialManager::Get().Save(Material))
		{
			ClearDirty();
		}
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s", Material->GetSourcePath().empty() ? "Unsaved asset" : Material->GetSourcePath().c_str());
	ImGui::Separator();

	// 리플렉션 프로퍼티를 디테일 패널과 동일한 위젯으로 렌더. (offset 직접 write -> setter 우회)
	TArray<FPropertyValue> Props;
	Material->GetEditableProperties(Props);

	// Resizable: 이름 칼럼이 길어 잘릴 때 가운데 경계를 드래그해 넓힐 수 있게 한다.
	if (ImGui::BeginTable("##PhysMatSettings", 2,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
	{
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.45f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.55f);

		for (int32 Index = 0; Index < (int32)Props.size(); ++Index)
		{
			FPropertyValue& Prop = Props[Index];

			ImGui::PushID(Index);
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(FEditorPropertyRenderer::GetPropertyDisplayName(Prop));

			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);

			// Combine Mode 콤보는 대응되는 Override 체크박스가 켜졌을 때만 활성화.
			const char* PropName = Prop.GetName();
			bool bDisabled = false;
			if (std::strcmp(PropName, "FrictionCombineMode") == 0)
			{
				bDisabled = !Material->GetOverrideFrictionCombineMode();
			}
			else if (std::strcmp(PropName, "RestitutionCombineMode") == 0)
			{
				bDisabled = !Material->GetOverrideRestitutionCombineMode();
			}

			if (bDisabled)
			{
				ImGui::BeginDisabled();
			}

			FEditorPropertyRenderOptions Options;
			const bool bChanged = PropertyRenderer.RenderPropertyWidget(Props, Index, Options);

			if (bDisabled)
			{
				ImGui::EndDisabled();
			}

			if (bChanged)
			{
				Material->PostEditProperty(Prop.GetName());
				MarkDirty();
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	ImGui::End();
	if (!bWindowOpen)
	{
		Close();
	}
}
