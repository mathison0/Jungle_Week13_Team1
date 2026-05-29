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

	// --- PhysX userData helpers ---
	// PxActor::userData는 AActor*가 아니라 FBodyInstance*
	// PxShape::userData는 기존처럼 UPrimitiveComponent*를 유지
	static FBodyInstance* GetBodyInstanceFromPxActor(const physx::PxRigidActor* Actor)
	{
		return FPhysXHelper::GetUserData<FBodyInstance>(Actor);
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
};
