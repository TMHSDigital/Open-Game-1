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
		&& C.Positive(TEXT("RagdollRecoverSeconds"), RagdollRecoverSeconds)
		&& C.Positive(TEXT("RagdollImpulseThreshold"), RagdollImpulseThreshold)
		&& C.Positive(TEXT("RagdollLandingSpeedThreshold"), RagdollLandingSpeedThreshold);
}
