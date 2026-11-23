#include "CSMovementTuning.h"

namespace
{
	// Each check sets OutReason and returns false on the first failure.
	struct FTuningChecker
	{
		FString& Reason;

		bool Finite(const TCHAR* Name, float Value)
		{
			if (!FMath::IsFinite(Value))
			{
				Reason = FString::Printf(TEXT("%s is not a number"), Name);
				return false;
			}
			return true;
		}

		bool Positive(const TCHAR* Name, float Value)
		{
			if (!Finite(Name, Value)) { return false; }
			if (Value <= 0.f)
			{
				Reason = FString::Printf(TEXT("%s must be greater than 0 (is %g)"), Name, Value);
				return false;
			}
			return true;
		}

		bool NotNegative(const TCHAR* Name, float Value)
		{
			if (!Finite(Name, Value)) { return false; }
			if (Value < 0.f)
			{
				Reason = FString::Printf(TEXT("%s must not be negative (is %g)"), Name, Value);
				return false;
			}
			return true;
		}

		bool Closed(const TCHAR* Name, float Value, float Min, float Max)
		{
			if (!Finite(Name, Value)) { return false; }
			if (Value < Min || Value > Max)
			{
				Reason = FString::Printf(TEXT("%s must be within [%g, %g] (is %g)"), Name, Min, Max, Value);
				return false;
			}
			return true;
		}

		bool Open(const TCHAR* Name, float Value, float Min, float Max)
		{
			if (!Finite(Name, Value)) { return false; }
			if (Value <= Min || Value >= Max)
			{
				Reason = FString::Printf(TEXT("%s must be inside (%g, %g) (is %g)"), Name, Min, Max, Value);
				return false;
			}
			return true;
		}

		bool Profile(const TCHAR* Name, const FPhysicalAnimationData& Data)
		{
			const float Values[] = {Data.OrientationStrength, Data.AngularVelocityStrength, Data.PositionStrength,
				Data.VelocityStrength, Data.MaxLinearForce, Data.MaxAngularForce};
			const TCHAR* Fields[] = {TEXT("OrientationStrength"), TEXT("AngularVelocityStrength"), TEXT("PositionStrength"),
				TEXT("VelocityStrength"), TEXT("MaxLinearForce"), TEXT("MaxAngularForce")};
			for (int32 i = 0; i < UE_ARRAY_COUNT(Values); ++i)
			{
				if (!NotNegative(*FString::Printf(TEXT("%s.%s"), Name, Fields[i]), Values[i]))
				{
					return false;
				}
			}
			return true;
		}

		bool Order(const TCHAR* LowName, float Low, const TCHAR* HighName, float High)
		{
			if (Low > High)
			{
				Reason = FString::Printf(TEXT("%s (%g) must not be above %s (%g)"), LowName, Low, HighName, High);
				return false;
			}
			return true;
		}
	};
}

namespace
{
	FPhysicalAnimationData MakeDrive(bool bLocal, float Orientation, float AngularVelocity, float Position, float Velocity)
	{
		FPhysicalAnimationData Data;
		Data.bIsLocalSimulation = bLocal;
		Data.OrientationStrength = Orientation;
		Data.AngularVelocityStrength = AngularVelocity;
		Data.PositionStrength = Position;
		Data.VelocityStrength = Velocity;
		// 0: no force limit.
		Data.MaxLinearForce = 0.f;
		Data.MaxAngularForce = 0.f;
		return Data;
	}
}

UCSMovementTuning::UCSMovementTuning()
{
	// Starting values (no spec numbers). World-space motors: Normal holds the pose closely, Hit lets
	// the limb go loose enough to swing visibly before the ramp back.
	NormalProfile = MakeDrive(false, 1000.f, 100.f, 1000.f, 100.f);
	HitProfile = MakeDrive(false, 60.f, 10.f, 60.f, 10.f);
}

bool UCSMovementTuning::IsValid(FString& OutReason) const
{
	OutReason.Reset();
	FTuningChecker C{OutReason};
	return C.Positive(TEXT("WalkSpeed"), WalkSpeed)
		&& C.Positive(TEXT("JogSpeed"), JogSpeed)
		&& C.Positive(TEXT("SprintSpeed"), SprintSpeed)
		&& C.Positive(TEXT("CrouchSpeed"), CrouchSpeed)
		&& C.Order(TEXT("WalkSpeed"), WalkSpeed, TEXT("JogSpeed"), JogSpeed)
		&& C.Order(TEXT("JogSpeed"), JogSpeed, TEXT("SprintSpeed"), SprintSpeed)
		&& C.Positive(TEXT("SprintRampSeconds"), SprintRampSeconds)
		&& C.Positive(TEXT("Acceleration"), Acceleration)
		&& C.Positive(TEXT("GroundFriction"), GroundFriction)
		&& C.Positive(TEXT("SprintGripScale"), SprintGripScale)
		&& C.Closed(TEXT("SprintGripScale"), SprintGripScale, 0.f, 1.f)
		&& C.Positive(TEXT("RotationRateYaw"), RotationRateYaw)
		&& C.NotNegative(TEXT("BrakingFriction"), BrakingFriction)
		&& C.NotNegative(TEXT("BrakingDeceleration"), BrakingDeceleration)
		&& C.Open(TEXT("MaxWalkableSlopeDeg"), MaxWalkableSlopeDeg, 0.f, 90.f)
		&& C.Closed(TEXT("UphillSpeedLoss"), UphillSpeedLoss, 0.f, 0.99f)
		&& C.Closed(TEXT("DownhillSpeedGain"), DownhillSpeedGain, 0.f, 1.f)
		&& C.Positive(TEXT("JumpHeightCm"), JumpHeightCm)
		&& C.Closed(TEXT("AirControl"), AirControl, 0.f, 1.f)
		&& C.NotNegative(TEXT("LandingSpeedLossPerMps"), LandingSpeedLossPerMps)
		&& C.Closed(TEXT("ThirdPersonFOV"), ThirdPersonFOV, 60.f, 120.f)
		&& C.Closed(TEXT("FirstPersonFOV"), FirstPersonFOV, 60.f, 120.f)
		&& C.Positive(TEXT("CameraBlendSeconds"), CameraBlendSeconds)
		&& C.Positive(TEXT("ThirdPersonArmLength"), ThirdPersonArmLength)
		&& C.Closed(TEXT("ThirdPersonArmLength"), ThirdPersonArmLength, 0.f, 1000.f)
		&& C.Closed(TEXT("ThirdPersonShoulderOffset.X"), float(ThirdPersonShoulderOffset.X), -200.f, 200.f)
		&& C.Closed(TEXT("ThirdPersonShoulderOffset.Y"), float(ThirdPersonShoulderOffset.Y), -200.f, 200.f)
		&& C.Closed(TEXT("ThirdPersonShoulderOffset.Z"), float(ThirdPersonShoulderOffset.Z), -200.f, 200.f)
		&& C.Positive(TEXT("ThirdPersonLagSpeed"), ThirdPersonLagSpeed)
		&& C.NotNegative(TEXT("ThirdPersonLagMaxDistance"), ThirdPersonLagMaxDistance)
		&& C.Closed(TEXT("FirstPersonSmoothSeconds"), FirstPersonSmoothSeconds, 0.f, 1.f)
		&& C.Positive(TEXT("FirstPersonEyeHeightCm"), FirstPersonEyeHeightCm)
		&& C.Closed(TEXT("FirstPersonEyeHeightCm"), FirstPersonEyeHeightCm, 0.f, 300.f)
		&& C.Positive(TEXT("FirstPersonCrouchedEyeHeightCm"), FirstPersonCrouchedEyeHeightCm)
		&& C.Order(TEXT("FirstPersonCrouchedEyeHeightCm"), FirstPersonCrouchedEyeHeightCm, TEXT("FirstPersonEyeHeightCm"), FirstPersonEyeHeightCm)
		&& C.Closed(TEXT("FirstPersonHeightSmoothSeconds"), FirstPersonHeightSmoothSeconds, 0.f, 2.f)
		&& C.Positive(TEXT("RagdollRecoverSeconds"), RagdollRecoverSeconds)
		&& C.Positive(TEXT("RagdollImpulseThreshold"), RagdollImpulseThreshold)
		&& C.Positive(TEXT("RagdollLandingSpeedThreshold"), RagdollLandingSpeedThreshold)
		&& C.Positive(TEXT("RagdollSettleSpeed"), RagdollSettleSpeed)
		&& C.Positive(TEXT("RagdollSettleSeconds"), RagdollSettleSeconds)
		&& C.Positive(TEXT("RagdollMaxSeconds"), RagdollMaxSeconds)
		&& C.Order(TEXT("RagdollSettleSeconds"), RagdollSettleSeconds, TEXT("RagdollMaxSeconds"), RagdollMaxSeconds)
		&& C.Closed(TEXT("RagdollLinearDamping"), RagdollLinearDamping, 0.f, 10.f)
		&& C.Closed(TEXT("RagdollAngularDamping"), RagdollAngularDamping, 0.f, 20.f)
		&& C.Closed(TEXT("RagdollFriction"), RagdollFriction, 0.f, 4.f)
		&& C.Profile(TEXT("NormalProfile"), NormalProfile)
		&& C.Closed(TEXT("NormalLimbStrengthScale"), NormalLimbStrengthScale, 0.f, 1.f)
		&& C.Profile(TEXT("HitProfile"), HitProfile)
		&& C.NotNegative(TEXT("HitHoldSeconds"), HitHoldSeconds)
		&& C.Positive(TEXT("HitRecoverSeconds"), HitRecoverSeconds)
		&& C.Closed(TEXT("HitCapsuleVelocityShare"), HitCapsuleVelocityShare, 0.f, 1.f)
		&& C.Positive(TEXT("DevPushSpeed"), DevPushSpeed);
}
