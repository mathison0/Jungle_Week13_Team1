#pragma once
#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"


struct FSuspensionSetup
{
	float SuspensionMaxRaise;
	float SuspensionMaxDrop;
	float SpringRate;
	float SpringDamping;

};

struct FSuspensionSystem
{
	FSuspensionSetup Setup;
	// SuspensionSystem의 현재 압축량 (0.0f = 완전히 풀림, 1.0f = 완전히 압축)
	float CompressionRatio;
	// SuspensionSystem이 현재 지면과 접촉 중인지 여부
	bool bIsGrounded;
	// SuspensionSystem이 지면과 접촉 중일 때의 접촉점 위치
	FVector ContactPoint;
	// SuspensionSystem이 지면과 접촉 중일 때의 접촉점 법선 벡터
	FVector ContactNormal;
	// SuspensionSystem이 지면과 접촉 중일 때의 접촉점에서 바퀴 중심까지의 벡터
	FVector WheelToContactPoint;

	static float CalculateSuspensionForce(const FSuspensionSetup& Setup, float CompressionRatio, const FVector& WheelVelocity, const FVector& ContactNormal)
	{
		// 스프링 힘 계산
		float SpringForce = Setup.SpringRate * CompressionRatio;
		// 감쇠력 계산 (압축 방향으로만 작용)
		float DampingForce = 0.0f;
		if (CompressionRatio > 0.0f)
		{
			float RelativeVelocity = WheelVelocity.Dot(ContactNormal);
			DampingForce = Setup.SpringDamping * RelativeVelocity;
		}
		return SpringForce + DampingForce;
	}
};