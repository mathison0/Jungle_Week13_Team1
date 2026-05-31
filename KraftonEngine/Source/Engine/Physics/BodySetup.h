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
    // 하나의 StaticMesh 또는 하나의 skeleton bone에 붙는 collision shape 묶음.
    // PhysicsAsset Editor에서는 bone마다 UBodySetup 하나를 만들고 AggGeom을 채운다.
    UPROPERTY(Edit, Save, Category="Physics", DisplayName="Aggregate Geometry", Type=Struct, Struct=FKAggregateGeom)
    FKAggregateGeom AggGeom;
};
