#pragma once

#include "Component/SceneComponent.h"

#include "Source/Engine/Component/Physics/WindDirectionalSourceComponent.generated.h"

class FScene;
class UBillboardComponent;

UCLASS()
class UWindDirectionalSourceComponent : public USceneComponent
{
public:
	GENERATED_BODY()

	~UWindDirectionalSourceComponent() override;

	void BeginPlay() override;
	void EndPlay() override;
	void PostEditProperty(const char* PropertyName) override;
	void ContributeSelectedVisuals(FScene& Scene) const override;

	bool IsWindEnabled() const { return bEnabled && Strength != 0.0f; }
	float GetStrength() const { return Strength; }
	float GetRadius() const { return Radius; }
	FVector GetWindDirection() const;
	FVector GetWindVelocity() const;
	FVector GetWindVelocityAt(const FVector& WorldPosition) const;
	UBillboardComponent* EnsureEditorBillboard();

private:
	void RegisterWithClothScene();
	void UnregisterFromClothScene();

	UPROPERTY(Edit, Save, Category="Wind", DisplayName="Enabled")
	bool bEnabled = true;
	UPROPERTY(Edit, Save, Category="Wind", DisplayName="Strength", Min=0.0f, Speed=0.1f)
	float Strength = 6.0f;
	UPROPERTY(Edit, Save, Category="Wind", DisplayName="Radius", Min=0.0f, Speed=0.1f)
	float Radius = 0.0f;
	UPROPERTY(Edit, Save, Category="Wind", DisplayName="Falloff Exponent", Min=0.1f, Max=8.0f, Speed=0.1f)
	float FalloffExponent = 1.0f;
};
