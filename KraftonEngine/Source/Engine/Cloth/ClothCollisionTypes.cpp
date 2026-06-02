#include "Cloth/ClothCollisionTypes.h"

void FClothCollisionData::Reset()
{
	Spheres.clear();
	Capsules.clear();
	Planes.clear();
	ConvexMasks.clear();
}

uint32 FClothCollisionData::GetPrimitiveCount() const
{
	uint32 StandaloneSphereCount = 0;
	for (uint32 SphereIndex = 0; SphereIndex < static_cast<uint32>(Spheres.size()); ++SphereIndex)
	{
		bool bReferencedByCapsule = false;
		for (uint32 CapsuleSphereIndex : Capsules)
		{
			if (CapsuleSphereIndex == SphereIndex)
			{
				bReferencedByCapsule = true;
				break;
			}
		}

		if (!bReferencedByCapsule)
		{
			++StandaloneSphereCount;
		}
	}

	return StandaloneSphereCount + static_cast<uint32>(Capsules.size() / 2) + static_cast<uint32>(ConvexMasks.size());
}
