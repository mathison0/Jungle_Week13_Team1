#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"

// ======================================================
// FPxVehicleSetup
//
// PxVehicleDrive4W 한 대를 만들 때 필요한 입력값을 모은 plain 데이터.
// PhysX 타입을 쓰지 않으므로 reflection/컴포넌트 쪽에서 자유롭게 채우고,
// FPhysXVehicle4W::Build 안에서만 PhysX 구조체로 변환한다.
//
// 차종(스포츠카/SUV/트럭...)의 차이는 이 값들의 조합으로 표현한다.
// 길이=m, 질량=kg, 토크=N·m, 각속도=rad/s (엔진 SI 단위계와 동일).
// ======================================================
struct FPxVehicleSetup
{
	// [0]=FL [1]=FR [2]=RL [3]=RR. 앞 2개(0,1)가 조향륜.
	FVector WheelLocalLocation[4] = {};

	// 섀시
	float ChassisMass = 1200.0f;

	// 바퀴 (4륜 공통)
	float WheelRadius = 0.35f;
	float WheelWidth = 0.25f;
	float WheelMass = 20.0f;

	// 서스펜션 (F = k·x + c·v)
	float SpringStrength = 35000.0f;    // k
	float SpringDamperRate = 4500.0f;   // c
	float SuspensionMaxRaise = 0.15f;   // maxCompression
	float SuspensionMaxDrop = 0.25f;    // maxDroop

	// 타이어
	uint32 TireType = 0;
	float TireFriction = 1.5f;          // 노면 대비 마찰 계수

	// 조향 / 제동
	float MaxSteerAngleDeg = 35.0f;     // 전륜 최대 조향각
	float MaxBrakeTorque = 4500.0f;
	float MaxHandBrakeTorque = 6000.0f; // 후륜 핸드브레이크

	// 엔진 / 드라이브트레인
	float EnginePeakTorque = 500.0f;
	float EngineMaxOmega = 600.0f;      // 레드라인 (rad/s)
	float ClutchStrength = 10.0f;
	float GearSwitchTime = 0.5f;

	// Ackermann 조향 형상
	float TrackWidth = 1.7f;            // 좌우 바퀴 간격
	float WheelBase = 2.8f;             // 앞뒤 차축 간격

	// 속도에 따른 조향 제한 (x=전진속도 m/s, y=조향 스케일 0~1). 최대 8쌍.
	struct FSteerSpeedPair { float Speed; float Scale; };
	FSteerSpeedPair SteerVsSpeed[8] =
	{
		{ 0.0f, 1.0f }, { 20.0f, 0.8f }, { 40.0f, 0.5f }, { 60.0f, 0.35f }
	};
	int32 SteerVsSpeedCount = 4;
};
