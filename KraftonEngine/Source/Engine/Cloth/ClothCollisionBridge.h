#pragma once

#include "Cloth/ClothCollisionTypes.h"

class UWorld;
class UPrimitiveComponent;

class FClothCollisionBridge
{
public:
	static void BuildWorldShapeCollision(const UWorld* World, const UClothComponent* ClothComponent, float CollisionThickness, FClothCollisionData& OutData);

private:
	static bool IsShapeUsableForCloth(const UPrimitiveComponent* Component, const UClothComponent* ClothComponent);
};
