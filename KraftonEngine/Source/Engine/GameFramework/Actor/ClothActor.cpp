#include "GameFramework/Actor/ClothActor.h"

#include "Component/Primitive/ClothComponent.h"

void AClothActor::InitDefaultComponents()
{
	ClothComponent = AddComponent<UClothComponent>();
	SetRootComponent(ClothComponent);
}

void AClothActor::PostDuplicate()
{
	Super::PostDuplicate();

	if (!GetRootComponent())
	{
		InitDefaultComponents();
		return;
	}

	ClothComponent = Cast<UClothComponent>(GetRootComponent());
}
