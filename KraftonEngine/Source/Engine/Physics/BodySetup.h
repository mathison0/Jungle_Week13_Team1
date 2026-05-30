#pragma once

#include "Physics/BodySetupCore.h"
#include "Physics/PhysicsGeometry.h"

#include "Source/Engine/Physics/BodySetup.generated.h"

UCLASS()
class UBodySetup : public UBodySetupCore
{
public:
    GENERATED_BODY()

    UBodySetup() = default;
    ~UBodySetup() override = default;

    FKAggregateGeom& GetAggGeom() { return AggGeom; }
    const FKAggregateGeom& GetAggGeom() const { return AggGeom; }

    bool HasGeometry() const;

public:
    UPROPERTY(Edit, Save, Category="Physics", DisplayName="Aggregate Geometry", Type=Struct, Struct=FKAggregateGeom)
    FKAggregateGeom AggGeom;
};
