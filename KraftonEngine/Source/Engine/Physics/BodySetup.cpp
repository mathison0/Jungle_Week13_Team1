#include "Physics/BodySetup.h"

bool UBodySetup::HasGeometry() const
{
    return !AggGeom.IsEmpty();
}
