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
struct FKAggregateGeom
{
    GENERATED_BODY()

    UPROPERTY(Edit, Save, Category="Physics|Geometry", DisplayName="Spheres", Type=Array, Struct=FKSphereElem)
    TArray<FKSphereElem> SphereElems;

    UPROPERTY(Edit, Save, Category="Physics|Geometry", DisplayName="Boxes", Type=Array, Struct=FKBoxElem)
    TArray<FKBoxElem> BoxElems;

    UPROPERTY(Edit, Save, Category="Physics|Geometry", DisplayName="Capsules", Type=Array, Struct=FKSphylElem)
    TArray<FKSphylElem> SphylElems;

    bool IsEmpty() const;
    int32 GetElementCount() const;
};
