#pragma once

#include "Cloth/ClothCollisionTypes.h"

class UWorld;

class FClothCollisionBridge
{
public:
	static void BuildWorldShapeCollision(const UWorld* World, const UClothComponent* ClothComponent, float CollisionThickness, FClothCollisionData& OutData);
};
