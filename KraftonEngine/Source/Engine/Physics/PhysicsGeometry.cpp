#include "Physics/PhysicsGeometry.h"

bool FKAggregateGeom::IsEmpty() const
{
    return GetElementCount() == 0;
}

int32 FKAggregateGeom::GetElementCount() const
{
    return static_cast<int32>(
        SphereElems.size() +
        BoxElems.size() +
        SphylElems.size());
}
