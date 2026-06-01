#include "Game/Vehicle4WActor.h"

#include "Component/Movement/WheeledVehicleMovementComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Object/FName.h"

void AVehicle4WActor::InitDefaultComponents()
{
	ChassisComponent = AddComponent<UBoxComponent>();
	ChassisComponent->SetFName(FName("VehicleChassis"));
	SetRootComponent(ChassisComponent);
	ChassisComponent->SetBoxExtent(FVector(1.4f, 0.75f, 0.35f));
	ChassisComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ChassisComponent->SetCollisionObjectType(ECollisionChannel::WorldDynamic);
	ChassisComponent->SetSimulatePhysics(true);
	ChassisComponent->SetMass(1200.0f);
	ChassisComponent->SetCenterOfMass(FVector(0.0f, 0.0f, -0.25f));

	static const char* WheelNames[4] =
	{
		"WheelCollider_FL",
		"WheelCollider_FR",
		"WheelCollider_RL",
		"WheelCollider_RR"
	};

	static const FVector WheelLocations[4] =
	{
		FVector(1.45f, -0.85f, -0.45f),
		FVector(1.45f, 0.85f, -0.45f),
		FVector(-1.35f, -0.85f, -0.45f),
		FVector(-1.35f, 0.85f, -0.45f)
	};

	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		USphereComponent* WheelCollider = AddComponent<USphereComponent>();
		WheelCollider->SetFName(FName(WheelNames[WheelIndex]));
		WheelCollider->AttachToComponent(ChassisComponent);
		WheelCollider->SetRelativeLocation(WheelLocations[WheelIndex]);
		WheelCollider->SetSphereRadius(0.35f);
		WheelCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WheelColliders[WheelIndex] = WheelCollider;
	}

	VehicleMovementComponent = AddComponent<UWheeledVehicleMovementComponent>();
	VehicleMovementComponent->SetUpdatedComponent(ChassisComponent);
	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		VehicleMovementComponent->SetWheelColliderComponent(WheelIndex, WheelColliders[WheelIndex]);
	}
}

void AVehicle4WActor::PostDuplicate()
{
	RebindComponents();
}

void AVehicle4WActor::RebindComponents()
{
	ChassisComponent = Cast<UBoxComponent>(GetRootComponent());
	VehicleMovementComponent = GetComponentByClass<UWheeledVehicleMovementComponent>();

	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		WheelColliders[WheelIndex] = nullptr;
	}

	int32 FoundWheelIndex = 0;
	for (UActorComponent* Component : GetComponents())
	{
		if (USphereComponent* Sphere = Cast<USphereComponent>(Component))
		{
			if (FoundWheelIndex < 4)
			{
				WheelColliders[FoundWheelIndex++] = Sphere;
			}
		}
	}

	if (VehicleMovementComponent)
	{
		VehicleMovementComponent->SetUpdatedComponent(ChassisComponent);
		for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
		{
			VehicleMovementComponent->SetWheelColliderComponent(WheelIndex, WheelColliders[WheelIndex]);
		}
	}
}
