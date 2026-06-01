#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"

// 에셋 측 충돌 기하 데이터.
// PhysX 타입으로의 변환은 런타임 body 생성 경로에서 처리한다.
#include "Source/Engine/Physics/PhysicsGeometry.generated.h"


USTRUCT()
struct FKShapeElem
{
    GENERATED_BODY()

    UPROPERTY(Edit, Save, Category="Physics|Shape", DisplayName="Name")
    FString Name;

    // StaticMesh BodySetup에서는 mesh component 로컬 기준이다.
    // PhysicsAsset BodySetup에서는 해당 BodySetup이 소유한 bone 로컬 기준이다.
    UPROPERTY(Edit, Save, Category="Physics|Shape", DisplayName="Location", Member=Transform.Location, Type=Vec3, Speed=0.1f);
    UPROPERTY(Edit, Save, Category="Physics|Shape", DisplayName="Rotation", Member=Transform.Rotation, Type=Vec4, Speed=0.01f);
    UPROPERTY(Edit, Save, Category="Physics|Shape", DisplayName="Scale", Member=Transform.Scale, Type=Vec3, Speed=0.1f);

    FTransform Transform;
};

USTRUCT()
struct FKSphereElem : public FKShapeElem
{
    GENERATED_BODY()

    UPROPERTY(Edit, Save, Category="Physics|Shape", DisplayName="Radius", Min=0.0f, Speed=0.5f)
    float Radius = 50.0f;
};

USTRUCT()
struct FKBoxElem : public FKShapeElem
{
    GENERATED_BODY()

    UPROPERTY(Edit, Save, Category="Physics|Shape", DisplayName="Extent", Type=Vec3, Min=0.0f, Speed=0.5f)
    FVector Extent = FVector(50.0f, 50.0f, 50.0f);
};

USTRUCT()
struct FKSphylElem : public FKShapeElem
{
    GENERATED_BODY()

    UPROPERTY(Edit, Save, Category="Physics|Shape", DisplayName="Radius", Min=0.0f, Speed=0.5f)
    float Radius = 25.0f;

    // 캡슐 전체 높이가 아니라 가운데 원통 구간의 길이.
    // PhysX 변환: PxCapsuleGeometry(Radius, Length * 0.5f).
    UPROPERTY(Edit, Save, Category="Physics|Shape", DisplayName="Length", Min=0.0f, Speed=0.5f)
    float Length = 50.0f;
};

USTRUCT()
struct FKConvexElem : public FKShapeElem
{
    GENERATED_BODY()

    UPROPERTY(Edit, Save, Category="Physics|Shape", DisplayName="Vertices", Type=Array)
    TArray<FVector> VertexData;

    UPROPERTY(Edit, Save, Category="Physics|Shape", DisplayName="Indices", Type=Array)
    TArray<uint32> IndexData;

    UPROPERTY(Save, Category="Physics|Shape", DisplayName="Cooked PhysX Data", Type=Array)
    TArray<uint8> CookedPhysXData;

    UPROPERTY(Save, Category="Physics|Shape", DisplayName="Geometry Hash")
    uint64 GeometryHash = 0;

    UPROPERTY(Save, Category="Physics|Shape", DisplayName="Cooking Version")
    uint32 CookingVersion = 1;

    UPROPERTY(Save, Category="Physics|Shape", DisplayName="PhysX Version")
    uint32 PhysXVersion = 0;

    UPROPERTY(Save, Category="Physics|Shape", DisplayName="Cooking Params Hash")
    uint32 CookingParamsHash = 0;

    UPROPERTY(Save, Category="Physics|Shape", DisplayName="Cooked Data Hash")
    uint64 CookedDataHash = 0;

    UPROPERTY(Save, Category="Physics|Shape", DisplayName="Local Bounds Min", Type=Vec3)
    FVector LocalBoundsMin = FVector(0.0f, 0.0f, 0.0f);

    UPROPERTY(Save, Category="Physics|Shape", DisplayName="Local Bounds Max", Type=Vec3)
    FVector LocalBoundsMax = FVector(0.0f, 0.0f, 0.0f);

    bool bCookedDataDirty = true;
};

USTRUCT()
struct FKAggregateGeom
{
    GENERATED_BODY()

    UPROPERTY(Edit, Save, Category="Physics|Geometry", DisplayName="Spheres", Type=Array, Struct=FKSphereElem)
    TArray<FKSphereElem> SphereElems;

    UPROPERTY(Edit, Save, Category="Physics|Geometry", DisplayName="Boxes", Type=Array, Struct=FKBoxElem)
    TArray<FKBoxElem> BoxElems;

    UPROPERTY(Edit, Save, Category="Physics|Geometry", DisplayName="Capsules", Type=Array, Struct=FKSphylElem)
    TArray<FKSphylElem> SphylElems;

    // 데이터 저장 구조는 준비되어 있지만 PhysX 변환은 아직 지원하지 않는다.
    // PhysicsAsset Editor 초기 버전에서는 생성 UI를 숨기거나 비활성화해야 한다.
    UPROPERTY(Edit, Save, Category="Physics|Geometry", DisplayName="Convexes", Type=Array, Struct=FKConvexElem)
    TArray<FKConvexElem> ConvexElems;

    bool IsEmpty() const;
    int32 GetElementCount() const;
};
