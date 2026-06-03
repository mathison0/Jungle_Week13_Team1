#pragma once

#include "Core/Types/CoreTypes.h"

#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleUtilControl.h>
#include <vehicle/PxVehicleComponents.h>
#include <vehicle/PxVehicleUpdate.h>

struct FPxVehicleSetup;

// ======================================================
// FPhysXVehicle4W
//
// PxVehicleDrive4W 한 대를 "자기 완결형"으로 감싼다. 이미 등록된 섀시
// PxRigidDynamic에 4륜 드라이브 데이터(엔진/기어/디퍼런셜/Ackermann)를 붙이고,
// 자기를 굴리는 데 필요한 PhysX 자원(BatchQuery, 노면-타이어 마찰 페어, 결과
// 버퍼)을 직접 소유한다.
// ======================================================
class FPhysXVehicle4W
{
public:
	FPhysXVehicle4W() = default;
	~FPhysXVehicle4W();

	// 내부 결과 버퍼를 BatchQuery/Updates가 직접 가리키므로 복사/이동 금지.
	FPhysXVehicle4W(const FPhysXVehicle4W&) = delete;
	FPhysXVehicle4W& operator=(const FPhysXVehicle4W&) = delete;

	// 섀시 actor에 드라이브 데이터를 붙여 차량을 만든다. 실패 시 false.
	bool Build(physx::PxScene* Scene, physx::PxPhysics* Physics, physx::PxRigidDynamic* ChassisActor, physx::PxMaterial* DefaultMaterial, const FPxVehicleSetup& Setup);
	void Release();

	bool IsValid() const { return Drive != nullptr; }
	uint32 GetNbWheels() const { return NbWheels; }

	// --- 입력 (키보드 on/off) ---
	// 전진/후진/풋브레이크는 의도만 저장한다. 후진은 reverse 기어가 필요하고 진행 방향과
	// 반대 입력은 먼저 제동해야 하므로, 실제 기어 전환 + accel/brake 페달은 Simulate 안의
	// ResolveDriveInputs 가 속도/기어를 보고 결정한다.
	void SetThrottle(bool b)   { bThrottleInput = b; }
	void SetReverse(bool b)    { bReverseInput = b; }
	void SetBrake(bool b)      { bBrakeInput = b; }
	void SetSteerLeft(bool b)  { RawInput.setDigitalSteerLeft(b); }
	void SetSteerRight(bool b) { RawInput.setDigitalSteerRight(b); }

	// --- 매 프레임 구동 (반드시 Scene->simulate() 직전) ---
	// 입력 보간 → 서스펜션 raycast → 힘 적용을 자기 1대에 대해 수행.
	void Simulate(float DeltaTime);

	// --- 결과 조회 ---
	bool IsInAir() const;
	// 바퀴 i의 섀시-로컬 포즈(조향/구름/서스펜션 압축 포함). 범위 밖이면 false.
	bool GetWheelLocalPose(uint32 WheelIndex, physx::PxTransform& OutPose) const;

private:
	void SmoothInputs(float DeltaTime);
	// 전진/후진/브레이크 의도를 기어 전환 + accel/brake 페달로 변환해 RawInput 에 기록.
	void ResolveDriveInputs();

	static constexpr uint32 NbWheels = 4;

	// 키보드 의도 (W/S/Space). ResolveDriveInputs 가 매 프레임 페달/기어로 변환.
	bool bThrottleInput = false;
	bool bReverseInput = false;
	bool bBrakeInput = false;

	physx::PxVehicleDrive4W* Drive = nullptr;
	physx::PxBatchQuery* BatchQuery = nullptr;
	physx::PxVehicleDrivableSurfaceToTireFrictionPairs* FrictionPairs = nullptr;

	physx::PxVehicleDrive4WRawInputData RawInput;
	physx::PxFixedSizeLookupTable<8> SteerVsSpeed;

	// 자기 1대용 고정 크기 버퍼. BatchQuery/Updates가 이 멤버 주소를 직접
	// 가리키므로 객체를 복사/이동하면 안 된다(위 = delete).
	physx::PxRaycastQueryResult SqResults[NbWheels];
	physx::PxRaycastHit SqHits[NbWheels];
	physx::PxWheelQueryResult WheelResults[NbWheels];
	physx::PxVehicleWheelQueryResult WheelQuery;
};
