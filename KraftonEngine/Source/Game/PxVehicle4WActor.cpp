#include "Game/PxVehicle4WActor.h"

#include "Component/Movement/PxVehicleMovementComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Engine/Runtime/Engine.h"
#include "Mesh/MeshManager.h"
#include "Object/FName.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Math/Rotator.h"

namespace
{
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
	ChassisComponent->SetCenterOfMass(FVector(0.0f, 0.0f, -0.32f));

	// 3인칭 카메라 체인 — Capsule → SpringArm → Camera. lag 적용해 부드럽게 따라옴.
	SpringArm = AddComponent<USpringArmComponent>();
	SpringArm->AttachToComponent(ChassisComponent);
	SpringArm->TargetArmLength = 10.0f;
	SpringArm->SocketOffset = FVector(0.0f, 0.0f, 3.0f);
	SpringArm->bEnableCameraLag = false;
	SpringArm->bEnableCameraRotationLag = false;

	// mouse look 이 capsule rotation 안 건드리고 카메라만 회전 — UE ThirdPerson 패턴.
	// ACharacter::Tick 이 APawn::ControlRotation 누적 → SpringArm 이 이걸 inherit.
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = true;
	SpringArm->bInheritYaw = true;
	SpringArm->bInheritRoll = false;

	Camera = AddComponent<UCameraComponent>();
	Camera->AttachToComponent(SpringArm);

	// 메시 로드(첫 로드 시 임포트+쿠킹+_StaticMesh.uasset 저장). 바퀴는 한 에셋을 4개가 공유.
	auto* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();

	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		UStaticMeshComponent* Wheel = AddComponent<UStaticMeshComponent>();
		Wheel->SetFName(FName(GWheelNames[WheelIndex]));
		Wheel->AttachToComponent(ChassisComponent);
		Wheel->SetRelativeLocation(GWheelLocations[WheelIndex]);
		Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WheelMeshes[WheelIndex] = Wheel;
	}

	// 차체 시각 메시: 섀시에 붙는 NoCollision StaticMesh. 물리엔 영향 없음.
	// 메시 native 스케일/회전이 박스(2.8×1.5×0.7m)와 안 맞으면 RelativeScale/Rotation으로 보정.
	BodyMesh = AddComponent<UStaticMeshComponent>();
	BodyMesh->SetFName(FName("BodyMesh"));
	BodyMesh->AttachToComponent(ChassisComponent);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, -1.f));

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
	BodyMesh = nullptr;

	// 바퀴/차체를 "이름"으로 정확한 슬롯에 매칭한다. GetComponents() 순회 순서에 의존하면
	// PIE 복제(Duplicate) 후 순서가 바뀌어 바퀴가 한 칸씩 밀리는 문제가 생긴다.
	const FName BodyName("BodyMesh");
	for (UActorComponent* Component : GetComponents())
	{
		UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component);
		if (!Mesh)
		{
			continue;
		}

		const FName MeshName = Mesh->GetFName();
		if (MeshName == BodyName)
		{
			BodyMesh = Mesh;
			continue;
		}

		for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
		{
			if (MeshName == FName(GWheelNames[WheelIndex]))
			{
				WheelMeshes[WheelIndex] = Mesh;
				break;
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
