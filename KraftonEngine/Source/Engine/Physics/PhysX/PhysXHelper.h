#pragma once

#include "Component/PrimitiveComponent.h"
#include "Physics/BodyInstance.h"
#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

#include <PxPhysicsAPI.h>


// ===============================================================
//    PhysX <-> Engine Data Structure를 변환하는 헬퍼 클래스
// ================================================================
class FPhysXHelper final
{
public:
	// ----- Vector -------
	static physx::PxVec3 ToPxVec3(const FVector& V)
	{
		return physx::PxVec3(V.X, V.Y, V.Z);
	}

	static FVector ToFVector(const physx::PxVec3& V)
	{
		return FVector(V.x, V.y, V.z);
	}

	// ---- Quat -----
	static physx::PxQuat ToPxQuat(const FQuat& Q)
	{
		return physx::PxQuat(Q.X, Q.Y, Q.Z, Q.W);
	}


	static FQuat ToFQuat(const physx::PxQuat& Q)
	{
		return FQuat(Q.x, Q.y, Q.z, Q.w);
	}

	// ---- Transform ----
	static physx::PxTransform ToPxTransform(const FVector& Position, const FQuat& Rotation)
	{
		return physx::PxTransform(ToPxVec3(Position), ToPxQuat(Rotation));
	}

	static physx::PxTransform ToPxTransform(const FTransform& Transform)
	{
		return ToPxTransform(Transform.Location, Transform.Rotation);
	}

	static physx::PxTransform ToPxTransform(const UPrimitiveComponent* Comp)
	{
		if (!Comp)
		{
			return physx::PxTransform(physx::PxIdentity);
		}

		return ToPxTransform(Comp->GetWorldLocation(), Comp->GetWorldMatrix().ToQuat());
	}

	static FTransform ToFTransform(const physx::PxTransform& Transform)
	{
		return FTransform(ToFVector(Transform.p), ToFQuat(Transform.q), FVector::OneVector);
	}

	// ---- User Data Get, Set, Has ----
	template <typename T, typename TPhysXObject>
	static T* GetUserData(const TPhysXObject* Object)
	{
		return Object ? static_cast<T*>(Object->userData) : nullptr;
	}

	template <typename TPhysXObject, typename T>
	static void SetUserData(TPhysXObject* Object, T* UserData)
	{
		if (Object)
		{
			Object->userData = UserData;
		}
	}

	template <typename TPhysXObject, typename T>
	static bool HasUserData(const TPhysXObject* Object, const T* Expected)
	{
		return GetUserData<T>(Object) == Expected;
	}

	// --- PhysX userData policy ---
	// PxRigidActor::userData = 대표 FBodyInstance*.
	// PxShape::userData = 해당 shape를 소유한 component의 FBodyInstance*.
	// PxJoint::userData는 이번 구현에서 쓰지 않는다.
	static void SetActorBodyRecord(physx::PxRigidActor* Actor, FBodyInstance* BodyInstance)
	{
		FPhysXHelper::SetUserData(Actor, BodyInstance);
	}

	static void SetShapeBodyRecord(physx::PxShape* Shape, FBodyInstance* BodyInstance)
	{
		FPhysXHelper::SetUserData(Shape, BodyInstance);
	}

	static FBodyInstance* GetBodyInstanceFromPxActor(const physx::PxRigidActor* Actor)
	{
		return FPhysXHelper::GetUserData<FBodyInstance>(Actor);
	}

	static FBodyInstance* GetBodyInstanceFromPxShape(const physx::PxShape* Shape)
	{
		return FPhysXHelper::GetUserData<FBodyInstance>(Shape);
	}

	static bool IsShapeBodyRecord(const physx::PxShape* Shape, const FBodyInstance* BodyInstance)
	{
		return FPhysXHelper::HasUserData(Shape, BodyInstance);
	}

	static physx::PxRigidActor* GetRigidActor(const FBodyInstance* BodyInstance)
	{
		return BodyInstance ? BodyInstance->GetPxRigidActor() : nullptr;
	}

	static physx::PxRigidDynamic* GetRigidDynamic(const FBodyInstance* BodyInstance)
	{
		return BodyInstance ? BodyInstance->GetPxRigidDynamic() : nullptr;
	}

	static AActor* GetOwnerActorFromPxActor(const physx::PxRigidActor* Actor)
	{
		FBodyInstance* BodyInstance = GetBodyInstanceFromPxActor(Actor);
		return BodyInstance ? BodyInstance->GetOwnerActor() : nullptr;
	}

	static UPrimitiveComponent* GetOwnerComponentFromPxActor(const physx::PxRigidActor* Actor)
	{
		FBodyInstance* BodyInstance = GetBodyInstanceFromPxActor(Actor);
		return BodyInstance ? BodyInstance->GetOwnerComponent() : nullptr;
	}

	static AActor* GetOwnerActorFromPxShape(const physx::PxShape* Shape)
	{
		FBodyInstance* BodyInstance = GetBodyInstanceFromPxShape(Shape);
		return BodyInstance ? BodyInstance->GetOwnerActor() : nullptr;
	}

	static UPrimitiveComponent* GetOwnerComponentFromPxShape(const physx::PxShape* Shape)
	{
		FBodyInstance* BodyInstance = GetBodyInstanceFromPxShape(Shape);
		return BodyInstance ? BodyInstance->GetOwnerComponent() : nullptr;
	}
};
