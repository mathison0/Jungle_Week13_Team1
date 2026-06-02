#include "Component/Physics/WindDirectionalSourceComponent.h"

#include "Cloth/ClothScene.h"
#include "Component/Primitive/BillboardComponent.h"
#include "GameFramework/World.h"
#include "Materials/MaterialManager.h"
#include "Render/Scene/FScene.h"

#include <algorithm>
#include <cmath>

static void AddWindArrow(FScene& Scene, const FVector& Origin, const FVector& Direction, float Strength)
{
	const FVector Forward = Direction.Normalized();
	if (Forward.Length() <= 0.001f)
	{
		return;
	}

	const float ArrowLength = 1.5f + (std::min)(Strength, 20.0f) * 0.08f;
	const float HeadLength = 0.35f;
	const float HeadRadius = 0.14f;
	const int32 RingSegments = 10;

	FVector ReferenceUp(0.0f, 0.0f, 1.0f);
	if (std::abs(Forward.Dot(ReferenceUp)) > 0.98f)
	{
		ReferenceUp = FVector(0.0f, 1.0f, 0.0f);
	}

	const FVector Right = Forward.Cross(ReferenceUp).Normalized();
	const FVector Up = Right.Cross(Forward).Normalized();
	const FVector Tip = Origin + Forward * ArrowLength;
	const FVector HeadBase = Tip - Forward * HeadLength;
	const FColor ShaftColor = FColor(100, 180, 255);
	const FColor HeadColor = FColor(180, 230, 255);

	Scene.AddDebugLine(Origin, Tip, ShaftColor);

	FVector PreviousRingPoint = HeadBase + Right * HeadRadius;
	for (int32 Index = 1; Index <= RingSegments; ++Index)
	{
		const float Angle = (static_cast<float>(Index) / static_cast<float>(RingSegments)) * 2.0f * 3.1415926535f;
		const FVector RingOffset = Right * std::cos(Angle) * HeadRadius + Up * std::sin(Angle) * HeadRadius;
		const FVector RingPoint = HeadBase + RingOffset;

		Scene.AddDebugLine(PreviousRingPoint, RingPoint, HeadColor);
		Scene.AddDebugLine(Tip, RingPoint, HeadColor);
		PreviousRingPoint = RingPoint;
	}
}

static void AddWindRadiusCircle(FScene& Scene, const FVector& Origin, float Radius)
{
	if (Radius <= 0.0f)
	{
		return;
	}

	constexpr int32 SegmentCount = 48;
	const FColor CircleColor(80, 210, 255);
	FVector Previous = Origin + FVector(Radius, 0.0f, 0.0f);
	for (int32 Index = 1; Index <= SegmentCount; ++Index)
	{
		const float Angle = (static_cast<float>(Index) / static_cast<float>(SegmentCount)) * 2.0f * 3.1415926535f;
		const FVector Current = Origin + FVector(std::cos(Angle) * Radius, std::sin(Angle) * Radius, 0.0f);
		Scene.AddDebugLine(Previous, Current, CircleColor);
		Previous = Current;
	}
}

UWindDirectionalSourceComponent::~UWindDirectionalSourceComponent()
{
	UnregisterFromClothScene();
}

void UWindDirectionalSourceComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterWithClothScene();
}

void UWindDirectionalSourceComponent::EndPlay()
{
	UnregisterFromClothScene();
	Super::EndPlay();
}

void UWindDirectionalSourceComponent::PostEditProperty(const char* PropertyName)
{
	Super::PostEditProperty(PropertyName);
	(void)PropertyName;
}

void UWindDirectionalSourceComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	AddWindArrow(Scene, GetWorldLocation(), GetWindDirection(), Strength);
	AddWindRadiusCircle(Scene, GetWorldLocation(), Radius);
}

FVector UWindDirectionalSourceComponent::GetWindDirection() const
{
	FVector Direction = GetForwardVector();
	if (Direction.IsNearlyZero())
	{
		return FVector::ForwardVector;
	}

	Direction.Normalize();
	return Direction;
}

FVector UWindDirectionalSourceComponent::GetWindVelocity() const
{
	return IsWindEnabled() ? GetWindDirection() * Strength : FVector::ZeroVector;
}

FVector UWindDirectionalSourceComponent::GetWindVelocityAt(const FVector& WorldPosition) const
{
	if (!IsWindEnabled())
	{
		return FVector::ZeroVector;
	}

	float Influence = 1.0f;
	if (Radius > 0.0f)
	{
		const float Distance = (WorldPosition - GetWorldLocation()).Length();
		if (Distance >= Radius)
		{
			return FVector::ZeroVector;
		}

		const float Alpha = (std::max)(0.0f, 1.0f - Distance / Radius);
		Influence = std::pow(Alpha, (std::max)(0.1f, FalloffExponent));
	}

	return GetWindDirection() * Strength * Influence;
}

UBillboardComponent* UWindDirectionalSourceComponent::EnsureEditorBillboard()
{
	if (!Owner)
	{
		return nullptr;
	}

	for (USceneComponent* Child : GetChildren())
	{
		UBillboardComponent* Billboard = Cast<UBillboardComponent>(Child);
		if (Billboard && Billboard->IsEditorOnlyComponent())
		{
			Billboard->SetAbsoluteScale(true);
			Billboard->SetHiddenInComponentTree(true);
			return Billboard;
		}
	}

	UBillboardComponent* Billboard = Owner->AddComponent<UBillboardComponent>();
	if (Billboard)
	{
		Billboard->AttachToComponent(this);
		Billboard->SetAbsoluteScale(true);
		Billboard->SetEditorOnlyComponent(true);
		Billboard->SetHiddenInComponentTree(true);
		Billboard->SetMaterial(FMaterialManager::Get().GetOrCreateMaterial("Content/Material/Editor/WindDirectional.mat"));
	}

	return Billboard;
}

void UWindDirectionalSourceComponent::RegisterWithClothScene()
{
	UWorld* World = GetWorld();
	FClothScene* ClothScene = World ? World->GetClothScene() : nullptr;
	if (ClothScene)
	{
		ClothScene->RegisterWindSource(this);
	}
}

void UWindDirectionalSourceComponent::UnregisterFromClothScene()
{
	UWorld* World = GetWorld();
	FClothScene* ClothScene = World ? World->GetClothScene() : nullptr;
	if (ClothScene)
	{
		ClothScene->UnregisterWindSource(this);
	}
}
