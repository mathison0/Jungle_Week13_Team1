#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

// 에셋 측 충돌 기하 데이터.
// PhysX 타입으로의 변환은 런타임 body 생성 경로에서 처리한다.

struct FKShapeElem
{
    FString Name;

    // StaticMesh BodySetup: 컴포넌트 로컬 기준.
    // SkeletalMesh PhysicsAsset BodySetup: 소유 본 로컬 기준.
    FTransform Transform;
};

struct FKSphereElem : public FKShapeElem
{
    float Radius = 50.0f;
};

struct FKBoxElem : public FKShapeElem
{
    FVector Extent = FVector(50.0f, 50.0f, 50.0f);
};

struct FKSphylElem : public FKShapeElem
{
    float Radius = 25.0f;

    // 캡슐 전체 높이가 아니라 가운데 원통 구간의 길이.
    // PhysX 변환: PxCapsuleGeometry(Radius, Length * 0.5f).
    float Length = 50.0f;
};

struct FKConvexElem : public FKShapeElem
{
    TArray<FVector> VertexData;
    TArray<uint32> IndexData;
};

struct FKAggregateGeom
{
    TArray<FKSphereElem> SphereElems;
    TArray<FKBoxElem> BoxElems;
    TArray<FKSphylElem> SphylElems;
    TArray<FKConvexElem> ConvexElems;

    bool IsEmpty() const;
    int32 GetElementCount() const;
};
