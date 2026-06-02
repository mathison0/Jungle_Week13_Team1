#pragma once

#include "GameFramework/AActor.h"

#include "Source/Engine/GameFramework/Actor/WindDirectionalSourceActor.generated.h"

class UWindDirectionalSourceComponent;
class UBillboardComponent;

UCLASS()
class AWindDirectionalSourceActor : public AActor
{
public:
	GENERATED_BODY()

	AWindDirectionalSourceActor() = default;

	void InitDefaultComponents();
	void PostDuplicate() override;

	UWindDirectionalSourceComponent* GetWindComponent() const { return WindComponent; }

private:
	UWindDirectionalSourceComponent* WindComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
