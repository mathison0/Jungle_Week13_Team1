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

    static constexpr int32 StaticMeshTriangleCollisionCookingVersion = 1;

    FKAggregateGeom& GetAggGeom() { return AggGeom; }
    const FKAggregateGeom& GetAggGeom() const { return AggGeom; }

    // PhysX 등록 단계에서 이 BodySetup이 만들 수 있는 collision을 갖고 있는지 확인한다.
    // AggGeom 기반 simple shape(Box/Sphere/Capsule)뿐 아니라 StaticMesh triangle mesh의
    // cooked binary도 실제 collision geometry로 취급한다.
    bool HasGeometry() const;

    // StaticMesh triangle collision용 PhysX cooked binary.
    // 이 byte array는 createTriangleMesh()가 바로 읽을 수 있는 PhysX 전용 데이터이며,
    // 원본 vertex/index 배열과 달리 BVH 등 simulation/query에 필요한 내부 구조가 포함되어 있다.
    const TArray<uint8>& GetCookedTriangleMeshPhysXData() const { return CookedTriangleMeshPhysXData; }
    bool HasCookedTriangleMeshPhysXData() const { return !CookedTriangleMeshPhysXData.empty(); }

    // Cooked data가 현재 PhysX SDK / cooking option으로 만들어졌는지만 확인한다.
    // 원본 vertex/index 변경 여부는 StaticMesh import/reimport 단계의 package stale 검사에서 처리한다.
    int32 GetTriangleMeshPhysXVersion() const { return TriangleMeshPhysXVersion; }
    int32 GetTriangleMeshCookingVersion() const { return TriangleMeshCookingVersion; }
    bool IsCookedTriangleMeshPhysXDataCompatible(int32 InPhysXVersion, int32 InCookingVersion) const;

    // StaticMesh import/reimport 중 cooking이 성공했을 때 binary 저장용 데이터를 교체한다.
    // InCookedData는 move로 받아 BodySetup이 소유하고, StaticMesh package에 같이 저장된다.
    void SetCookedTriangleMeshPhysXData(TArray<uint8>&& InCookedData, int32 InPhysXVersion,
        int32 InCookingVersion = StaticMeshTriangleCollisionCookingVersion);

    // StaticMesh geometry가 유효하지 않거나 cooking 실패/버전 불일치가 발생하면 저장된 cooked data를 제거한다.
    // 잘못된 cooked byte가 남아 있으면 PhysX shape 생성 실패나 충돌 불일치로 이어질 수 있다.
    void ClearCookedTriangleMeshPhysXData();

public:
    // 하나의 StaticMesh 또는 하나의 skeleton bone에 붙는 collision shape 묶음.
    // PhysicsAsset Editor에서는 bone마다 UBodySetup 하나를 만들고 AggGeom을 채운다.
    UPROPERTY(Edit, Save, Category="Physics", DisplayName="Aggregate Geometry", Type=Struct, Struct=FKAggregateGeom)
    FKAggregateGeom AggGeom;

    // StaticMesh triangle collision cooked binary.
    //
    // AggGeom은 에디터에서 만든 simple collision primitive를 저장한다. 반면 StaticMesh triangle collision은
    // 렌더 메시의 vertex/index를 PhysX cooked binary로 변환한 뒤 PxTriangleMesh shape로 붙인다.
    //
    // 이 데이터는 StaticMesh import/reimport, 즉 원본 vertex/index가 바뀌었다고 판단되는 시점에만 다시 만든다.
    // 런타임 PhysX 등록 단계에서는 vertex/index를 다시 훑거나 recook하지 않고 이 byte를 createTriangleMesh()에 넘긴다.
    UPROPERTY(Save, Category="Physics", DisplayName="Cooked Triangle Mesh PhysX Data")
    TArray<uint8> CookedTriangleMeshPhysXData;

    // cooking parameter나 input layout이 바뀌면 값을 올려 기존 런타임 캐시를 무효화한다.
    // 값이 맞지 않는 asset은 import/reimport로 package를 다시 저장해야 한다.
    UPROPERTY(Save, Category="Physics", DisplayName="Triangle Mesh Cooking Version")
    int32 TriangleMeshCookingVersion = 0;

    // PhysX cooked binary는 SDK version에 의존하므로 PX_PHYSICS_VERSION이 달라지면 재사용하지 않는다.
    UPROPERTY(Save, Category="Physics", DisplayName="Triangle Mesh PhysX Version")
    int32 TriangleMeshPhysXVersion = 0;
};
