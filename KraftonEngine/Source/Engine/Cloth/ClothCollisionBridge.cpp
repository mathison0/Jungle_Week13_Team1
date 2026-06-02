#include "Cloth/ClothCollisionBridge.h"

#include "Cloth/ClothCollisionBuilder.h"
#include "Component/Primitive/ClothComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include <algorithm>

bool FClothCollisionBridge::IsShapeUsableForCloth(const UPrimitiveComponent* Component, const UClothComponent* ClothComponent)
{
	if (!Component || Component == ClothComponent)
	{
		return false;
	}

	if (!Component->IsCollisionEnabled())
	{
		return false;
	}

	return true;
}

void FClothCollisionBridge::BuildWorldShapeCollision(const UWorld* World, const UClothComponent* ClothComponent, float CollisionThickness, FClothCollisionData& OutData)
{
	OutData.Reset();
	if (!World || !ClothComponent)
	{
		return;
	}

	const FMatrix& ClothWorldInverse = ClothComponent->GetWorldInverseMatrix();
	for (AActor* Actor : World->GetActors())
	{
		if (!Actor)
		{
			continue;
		}

		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (!IsShapeUsableForCloth(Primitive, ClothComponent))
			{
				continue;
			}

			if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Primitive))
			{
				const float WorldRadius = Capsule->GetScaledCapsuleRadius();
				const float WorldSegmentHalfLength = (std::max)(0.0f, Capsule->GetScaledCapsuleHalfHeight() - WorldRadius);
				FClothCollisionBuilder::AppendCapsuleFromWorldShape(
					Capsule->GetWorldLocation(),
					Capsule->GetUpVector(),
					WorldRadius,
					WorldSegmentHalfLength,
					CollisionThickness,
					ClothWorldInverse,
					OutData);
				continue;
			}

			if (const USphereComponent* Sphere = Cast<USphereComponent>(Primitive))
			{
				FClothCollisionBuilder::AppendSphereFromWorldShape(
					Sphere->GetWorldLocation(),
					Sphere->GetScaledSphereRadius(),
					CollisionThickness,
					ClothWorldInverse,
					OutData);
				continue;
			}

			if (const UBoxComponent* Box = Cast<UBoxComponent>(Primitive))
			{
				FClothCollisionBuilder::AppendBoxFromWorldShape(
					Box->GetWorldLocation(),
					Box->GetForwardVector(),
					Box->GetRightVector(),
					Box->GetUpVector(),
					Box->GetScaledBoxExtent(),
					CollisionThickness,
					ClothWorldInverse,
					OutData);
			}
		}
	}
}
