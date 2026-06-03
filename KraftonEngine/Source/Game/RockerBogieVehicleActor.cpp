#include "Game/RockerBogieVehicleActor.h"

#include "Component/Camera/CameraComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Component/Movement/RockerBogieVehicleMovementComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Object/FName.h"

namespace
{
	const char* GWheelNames[6] =
	{
		"RBWheel_FL", "RBWheel_FR", "RBWheel_ML", "RBWheel_MR", "RBWheel_RL", "RBWheel_RR"
	};

	const char* GLinkNames[4] =
	{
		"RBRocker_L", "RBRocker_R", "RBBogie_L", "RBBogie_R"
	};

	const FVector GWheelLocations[6] =
	{
		FVector(0.90f, -0.85f, -0.55f),
		FVector(0.90f,  0.85f, -0.55f),
		FVector(-0.25f, -0.85f, -0.55f),
		FVector(-0.25f,  0.85f, -0.55f),
		FVector(-1.15f, -0.85f, -0.55f),
		FVector(-1.15f,  0.85f, -0.55f)
	};
}

void ARockerBogieVehicleActor::InitDefaultComponents()
{
	ChassisComponent = AddComponent<UBoxComponent>();
	ChassisComponent->SetFName(FName("RockerBogieChassis"));
	SetRootComponent(ChassisComponent);
	ChassisComponent->SetBoxExtent(FVector(0.85f, 0.55f, 0.25f));
	ChassisComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ChassisComponent->SetCollisionObjectType(ECollisionChannel::WorldDynamic);
	ChassisComponent->SetSimulatePhysics(true);
	ChassisComponent->SetMass(260.0f);
	ChassisComponent->SetCenterOfMass(FVector(0.0f, 0.0f, -0.20f));

	SpringArm = AddComponent<USpringArmComponent>();
	SpringArm->AttachToComponent(ChassisComponent);
	SpringArm->TargetArmLength = 7.0f;
	SpringArm->SocketOffset = FVector(0.0f, 0.0f, 2.2f);
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bInheritPitch = true;
	SpringArm->bInheritYaw = true;
	SpringArm->bInheritRoll = false;

	Camera = AddComponent<UCameraComponent>();
	Camera->AttachToComponent(SpringArm);

	for (int32 WheelIndex = 0; WheelIndex < 6; ++WheelIndex)
	{
		UStaticMeshComponent* Wheel = AddComponent<UStaticMeshComponent>();
		Wheel->SetFName(FName(GWheelNames[WheelIndex]));
		Wheel->AttachToComponent(ChassisComponent);
		Wheel->SetRelativeLocation(GWheelLocations[WheelIndex]);
		Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WheelMeshes[WheelIndex] = Wheel;
	}

	for (int32 LinkIndex = 0; LinkIndex < 4; ++LinkIndex)
	{
		UStaticMeshComponent* Link = AddComponent<UStaticMeshComponent>();
		Link->SetFName(FName(GLinkNames[LinkIndex]));
		Link->AttachToComponent(ChassisComponent);
		Link->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		LinkMeshes[LinkIndex] = Link;
	}

	BodyMesh = AddComponent<UStaticMeshComponent>();
	BodyMesh->SetFName(FName("RockerBogieBodyMesh"));
	BodyMesh->AttachToComponent(ChassisComponent);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	VehicleMovementComponent = AddComponent<URockerBogieVehicleMovementComponent>();
	VehicleMovementComponent->SetUpdatedComponent(ChassisComponent);
	for (int32 WheelIndex = 0; WheelIndex < 6; ++WheelIndex)
	{
		VehicleMovementComponent->SetWheelVisualComponent(WheelIndex, WheelMeshes[WheelIndex]);
	}
	for (int32 LinkIndex = 0; LinkIndex < 4; ++LinkIndex)
	{
		VehicleMovementComponent->SetLinkVisualComponent(LinkIndex, LinkMeshes[LinkIndex]);
	}
}

void ARockerBogieVehicleActor::PostDuplicate()
{
	RebindComponents();
}

void ARockerBogieVehicleActor::RebindComponents()
{
	ChassisComponent = Cast<UBoxComponent>(GetRootComponent());
	VehicleMovementComponent = GetComponentByClass<URockerBogieVehicleMovementComponent>();

	for (int32 WheelIndex = 0; WheelIndex < 6; ++WheelIndex)
	{
		WheelMeshes[WheelIndex] = nullptr;
	}
	for (int32 LinkIndex = 0; LinkIndex < 4; ++LinkIndex)
	{
		LinkMeshes[LinkIndex] = nullptr;
	}
	BodyMesh = nullptr;

	for (UActorComponent* Component : GetComponents())
	{
		UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component);
		if (!Mesh)
		{
			continue;
		}

		const FName MeshName = Mesh->GetFName();
		if (MeshName == FName("RockerBogieBodyMesh"))
		{
			BodyMesh = Mesh;
			continue;
		}

		for (int32 WheelIndex = 0; WheelIndex < 6; ++WheelIndex)
		{
			if (MeshName == FName(GWheelNames[WheelIndex]))
			{
				WheelMeshes[WheelIndex] = Mesh;
				break;
			}
		}

		for (int32 LinkIndex = 0; LinkIndex < 4; ++LinkIndex)
		{
			if (MeshName == FName(GLinkNames[LinkIndex]))
			{
				LinkMeshes[LinkIndex] = Mesh;
				break;
			}
		}
	}

	if (VehicleMovementComponent)
	{
		VehicleMovementComponent->SetUpdatedComponent(ChassisComponent);
		for (int32 WheelIndex = 0; WheelIndex < 6; ++WheelIndex)
		{
			if (WheelMeshes[WheelIndex])
			{
				VehicleMovementComponent->SetWheelVisualComponent(WheelIndex, WheelMeshes[WheelIndex]);
			}
		}
		for (int32 LinkIndex = 0; LinkIndex < 4; ++LinkIndex)
		{
			if (LinkMeshes[LinkIndex])
			{
				VehicleMovementComponent->SetLinkVisualComponent(LinkIndex, LinkMeshes[LinkIndex]);
			}
		}
	}
}
