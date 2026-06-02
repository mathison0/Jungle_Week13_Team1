#include "GameFramework/Actor/WindDirectionalSourceActor.h"

#include "Component/Physics/WindDirectionalSourceComponent.h"
#include "Component/Primitive/BillboardComponent.h"

void AWindDirectionalSourceActor::InitDefaultComponents()
{
	WindComponent = AddComponent<UWindDirectionalSourceComponent>();
	SetRootComponent(WindComponent);
	BillboardComponent = WindComponent->EnsureEditorBillboard();
}

void AWindDirectionalSourceActor::PostDuplicate()
{
	Super::PostDuplicate();

	if (!GetRootComponent())
	{
		InitDefaultComponents();
		return;
	}

	WindComponent = Cast<UWindDirectionalSourceComponent>(GetRootComponent());
	BillboardComponent = WindComponent ? WindComponent->EnsureEditorBillboard() : nullptr;
}
