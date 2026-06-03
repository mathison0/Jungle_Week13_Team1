#include "Game/Lua/GameLuaBindings.h"

#include "sol/sol.hpp"

#include "Component/Particle/ParticleSystemComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/World.h"
#include "Materials/MaterialManager.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemManager.h"
#include "Engine/Runtime/Engine.h"
#include "Engine/Runtime/EngineInitHooks.h"
#include "Lua/LuaScriptManager.h"

// ============================================================
// 게임-특화 Lua 바인딩 등록 위치 — 현재는 비어 있음.
//
// Engine 의 FLuaScriptManager 가 등록하는 일반 binding (AActor / APawn / FVector /
// UWorld / Anim 등) 만으로 동작하지 않는 game-specific usertype (ACarPawn /
// AGameStateXxx / 전용 enum 등) 이 도입되면 여기에 new_usertype 으로 추가한다.
//
// 호출 시점: UEngine::Init() 이 FLuaScriptManager::Initialize() 를 끝낸 직후.
// 등록은 EngineInitHooks 에 자동으로 걸려 GameEngine / EditorEngine 두 엔트리 모두
// 같은 바인딩이 적용된다 (PIE 호환).
// ============================================================
void RegisterGameLuaBindings(sol::state& Lua)
{
	sol::object ExistingWorld = Lua["World"];
	sol::table World = ExistingWorld.is<sol::table>()
		? ExistingWorld.as<sol::table>()
		: Lua.create_named_table("World");

	World.set_function("SpawnFireworkActor", []() -> AActor*
	{
		if (!GEngine || !GEngine->GetWorld())
		{
			return nullptr;
		}

		UWorld* World = GEngine->GetWorld();
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
		if (!Actor)
		{
			return nullptr;
		}

		const FVector FireworkLocation(-35.101353f, -21.828775f, -108.804749f);
		Actor->InitDefaultComponents("Content/Data/BasicShape/Cube_StaticMesh.uasset");
		Actor->SetActorLocation(FireworkLocation);
		Actor->SetActorRotation(FVector(0.0f, 0.0f, 0.0f));
		Actor->SetActorScale(FVector(1.0f, 1.0f, 1.0f));
		Actor->SetVisible(true);
		Actor->bNeedsTick = true;

		if (UStaticMeshComponent* RootMesh = Cast<UStaticMeshComponent>(Actor->GetRootComponent()))
		{
			RootMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			RootMesh->SetSimulatePhysics(false);
			RootMesh->SetGenerateOverlapEvents(false);
			RootMesh->SetVisibility(true);

			if (UMaterialInterface* Material = FMaterialManager::Get().GetOrCreateMaterialInterface("Content/Material/Auto/BasicShapeMaterial.mat"))
			{
				RootMesh->SetMaterial(0, Material);
			}

			UParticleSystemComponent* Particle = Actor->AddComponent<UParticleSystemComponent>();
			if (Particle)
			{
				Particle->AttachToComponent(RootMesh);
				Particle->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
				Particle->SetRelativeRotation(FVector(0.0f, 0.0f, 0.0f));
				Particle->SetRelativeScale(FVector(1.0f, 1.0f, 1.0f));
				Particle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Particle->SetSimulatePhysics(false);
				Particle->SetGenerateOverlapEvents(false);
				Particle->SetVisibility(true);

				if (UParticleSystem* Template = FParticleSystemManager::Get().Load("Content/Particle/Firework.uasset"))
				{
					Particle->SetTemplate(Template);
				}

				Particle->BeginPlay();
				Particle->ResetSystem();
				Particle->Activate();
				Particle->SetEmitterSpawningEnabled(true);
			}
		}

		return Actor;
	});

	World.set_function("DispatchOverlapToActorScript", [](AActor* TargetActor, AActor* OtherActor) -> bool
	{
		if (!TargetActor || !OtherActor || !IsAliveObject(TargetActor) || !IsAliveObject(OtherActor))
		{
			return false;
		}

		if (ULuaScriptComponent* LuaScript = TargetActor->GetComponentByClass<ULuaScriptComponent>())
		{
			LuaScript->DispatchOverlap(OtherActor);
			return true;
		}

		return false;
	});
}

// 자기-등록 — Editor / Game 측이 RegisterGameLuaBindings 함수명을 모르고도
// FEngineInitHooks::RunAll() 한 번이면 호출되도록 static initializer 로 등록.
namespace
{
	void RunRegisterGameLuaBindings()
	{
		RegisterGameLuaBindings(FLuaScriptManager::GetState());
	}

	struct GameLuaBindingsAutoReg
	{
		GameLuaBindingsAutoReg() { FEngineInitHooks::Register(&RunRegisterGameLuaBindings); }
	};

	static GameLuaBindingsAutoReg gAutoReg;
}
