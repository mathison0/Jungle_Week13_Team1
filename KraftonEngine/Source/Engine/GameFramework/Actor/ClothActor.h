#pragma once

#include "GameFramework/AActor.h"

#include "Source/Engine/GameFramework/Actor/ClothActor.generated.h"

class UClothComponent;

UCLASS()
class AClothActor : public AActor
{
public:
	GENERATED_BODY()

	AClothActor() = default;

	void InitDefaultComponents();
	void PostDuplicate() override;

	UClothComponent* GetClothComponent() const { return ClothComponent; }

private:
	UClothComponent* ClothComponent = nullptr;
};
