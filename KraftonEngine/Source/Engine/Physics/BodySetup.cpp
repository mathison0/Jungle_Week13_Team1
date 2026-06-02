#include "Physics/BodySetup.h"

#include <utility>

bool UBodySetup::HasGeometry() const
{
    // simple collision(AggGeom)과 StaticMesh triangle collision(cooked binary) 중
    // 하나라도 있으면 PhysX actor 등록 시 shape 생성 대상으로 본다.
    return !AggGeom.IsEmpty() || HasCookedTriangleMeshPhysXData();
}

bool UBodySetup::IsCookedTriangleMeshPhysXDataCompatible(int32 InPhysXVersion, int32 InCookingVersion) const
{
    // vertex/index 변경 여부는 import/reimport 단계에서 source stale 검사로 처리한다.
    // 런타임에서는 저장된 cooked binary가 현재 PhysX와 cooking option에 맞는지만 본다.
    return HasCookedTriangleMeshPhysXData()
        && TriangleMeshPhysXVersion == InPhysXVersion
        && TriangleMeshCookingVersion == InCookingVersion;
}

void UBodySetup::SetCookedTriangleMeshPhysXData(TArray<uint8>&& InCookedData, int32 InPhysXVersion, int32 InCookingVersion)
{
    // StaticMesh import/reimport 중 cook이 성공한 결과를 BodySetup이 소유한다.
    // 이 값은 StaticMesh package에 저장되고, 런타임 등록 시 createTriangleMesh() 입력으로만 사용된다.
    CookedTriangleMeshPhysXData = std::move(InCookedData);
    TriangleMeshCookingVersion = InCookingVersion;
    TriangleMeshPhysXVersion = InPhysXVersion;
}

void UBodySetup::ClearCookedTriangleMeshPhysXData()
{
    // cooking 실패나 version 불일치가 확인되면 기존 cooked byte를 폐기한다.
    // 잘못된 cooked data가 남으면 현재 StaticMesh와 다른 충돌면을 생성할 수 있다.
    CookedTriangleMeshPhysXData.clear();
    TriangleMeshCookingVersion = 0;
    TriangleMeshPhysXVersion = 0;
}
