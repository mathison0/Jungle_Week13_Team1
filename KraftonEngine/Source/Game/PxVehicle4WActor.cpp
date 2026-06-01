#include "Game/PxVehicle4WActor.h"

#include "Component/Movement/PxVehicleMovementComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Engine/Runtime/Engine.h"
#include "Mesh/MeshManager.h"
#include "Object/FName.h"

namespace
{
	// 시각 전용 바퀴 메시(실린더). 충돌은 없고, 회전/조향은 컴포넌트가 PxVehicle 결과로 맞춘다.
	const char* const GWheelMeshPath = "Content/Data/BasicShape/Cylinder.obj";

	const char* GWheelNames[4] =
	{
		"WheelMesh_FL", "WheelMesh_FR", "WheelMesh_RL", "WheelMesh_RR"
	};

	const FVector GWheelLocations[4] =
	{
		FVector(1.45f, -0.85f, -0.45f),
		FVector(1.45f,  0.85f, -0.45f),
		FVector(-1.35f, -0.85f, -0.45f),
		FVector(-1.35f,  0.85f, -0.45f)
	};
}

void APxVehicle4WActor::InitDefaultComponents()
{
	// 섀시: 물리 강체. Box 콜리전 + simulate. (PxVehicle은 이 강체에 드라이브를 붙인다.)
	ChassisComponent = AddComponent<UBoxComponent>();
	ChassisComponent->SetFName(FName("PxVehicleChassis"));
	SetRootComponent(ChassisComponent);
	ChassisComponent->SetBoxExtent(FVector(1.4f, 0.75f, 0.35f));
	ChassisComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ChassisComponent->SetCollisionObjectType(ECollisionChannel::WorldDynamic);
	ChassisComponent->SetSimulatePhysics(true);
	ChassisComponent->SetMass(1200.0f);
	ChassisComponent->SetCenterOfMass(FVector(0.0f, 0.0f, -0.25f));

	// 바퀴: 시각 전용 StaticMesh. NoCollision이라 물리 씬에 등록되지 않아(섀시에 충돌도형이
	// 붙지 않음) PxVehicle raycast 서스펜션과 충돌하지 않는다.
	auto* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	UStaticMesh* WheelMesh = FMeshManager::LoadStaticMesh(GWheelMeshPath, Device);

	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		UStaticMeshComponent* Wheel = AddComponent<UStaticMeshComponent>();
		Wheel->SetFName(FName(GWheelNames[WheelIndex]));
		Wheel->AttachToComponent(ChassisComponent);
		Wheel->SetRelativeLocation(GWheelLocations[WheelIndex]);
		Wheel->SetStaticMesh(WheelMesh);
		Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WheelMeshes[WheelIndex] = Wheel;
	}

	VehicleMovementComponent = AddComponent<UPxVehicleMovementComponent>();
	VehicleMovementComponent->SetUpdatedComponent(ChassisComponent);
	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		VehicleMovementComponent->SetWheelVisualComponent(WheelIndex, WheelMeshes[WheelIndex]);
	}
}

void APxVehicle4WActor::PostDuplicate()
{
	RebindComponents();
}

void APxVehicle4WActor::RebindComponents()
{
	ChassisComponent = Cast<UBoxComponent>(GetRootComponent());
	VehicleMovementComponent = GetComponentByClass<UPxVehicleMovementComponent>();

	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		WheelMeshes[WheelIndex] = nullptr;
	}

	int32 FoundWheelIndex = 0;
	for (UActorComponent* Component : GetComponents())
	{
		if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component))
		{
			if (FoundWheelIndex < 4)
			{
				WheelMeshes[FoundWheelIndex++] = Mesh;
			}
		}
	}

	if (VehicleMovementComponent)
	{
		VehicleMovementComponent->SetUpdatedComponent(ChassisComponent);
		for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
		{
			VehicleMovementComponent->SetWheelVisualComponent(WheelIndex, WheelMeshes[WheelIndex]);
		}
	}
}
