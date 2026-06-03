#pragma once

#include "Cloth/ClothCollisionTypes.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"

class UClothComponent;
class FNvClothContext;
class UClothMesh;

namespace nv
{
namespace cloth
{
	class Cloth;
	class Fabric;
	class Solver;
}
}

struct FClothInstanceDesc
{
	UClothComponent* OwnerComponent = nullptr;
	FVector Gravity = FVector(0.0f, 0.0f, -9.81f);
	FVector WindVelocity = FVector::ZeroVector;
	float SolverFrequency = 120.0f;
	float StiffnessFrequency = 60.0f;
	float Damping = 0.35f;
	float LinearDrag = 0.2f;
	float AngularDrag = 0.45f;
	float DragCoefficient = 0.2f;
	float LiftCoefficient = 0.05f;
	float ConstraintStiffness = 1.0f;
	float ConstraintStiffnessMultiplier = 1.0f;
	float CompressionLimit = 1.0f;
	float StretchLimit = 1.0f;
	float TetherConstraintScale = 1.0f;
	float TetherConstraintStiffness = 1.0f;
	float Friction = 0.45f;
	float CollisionMassScale = 2.0f;
	bool bEnableContinuousCollision = true;
	bool bUseGeodesicTether = false;
};

class FClothInstance
{
public:
	FClothInstance() = default;
	~FClothInstance();

	bool Initialize(FNvClothContext& InContext, UClothMesh* InMesh, const FClothInstanceDesc& InDesc = FClothInstanceDesc());
	void Shutdown();
	bool Simulate(float DeltaTime);

	bool IsInitialized() const { return Cloth != nullptr && Solver != nullptr; }
	UClothMesh* GetMesh() const { return Mesh; }
	UClothComponent* GetOwnerComponent() const { return Desc.OwnerComponent; }
	const FClothInstanceDesc& GetDesc() const { return Desc; }

	void SetGravity(const FVector& InGravity);
	void SetWindVelocity(const FVector& InWindVelocity);
	void SetCollisionData(const FClothCollisionData& CollisionData);
	void SetCollisionDataForSubstep(const FClothCollisionData& CollisionData, uint32 SubstepIndex, uint32 SubstepCount);

private:
	bool CookFabricAndCreateCloth();
	bool WriteBackParticles();
	void ApplySettings();
	void ApplyCollisionData(const FClothCollisionData& StartCollisionData, const FClothCollisionData& TargetCollisionData);
	void CommitCollisionDataFrame(const FClothCollisionData& CollisionData);

	FNvClothContext* Context = nullptr;
	UClothMesh* Mesh = nullptr;
	FClothInstanceDesc Desc;
	FClothCollisionData PreviousCollisionData;
	FMatrix PreviousCollisionOwnerWorldMatrix;
	bool bHasPreviousCollisionData = false;

	nv::cloth::Fabric* Fabric = nullptr;
	nv::cloth::Cloth* Cloth = nullptr;
	nv::cloth::Solver* Solver = nullptr;
};
