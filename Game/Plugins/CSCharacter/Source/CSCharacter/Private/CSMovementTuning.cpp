#include "CSMovementTuning.h"

#include "CSTuningRanges.h"

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
	// Single-field bounds: every row of the range table (CSTuningRanges.cpp).
	for (const FCSTuningRow& Row : CSTuning::Rows())
	{
		float Value = 0.f;
		if (!CSTuning::GetValue(*this, Row.Path, Value))
		{
			OutReason = FString::Printf(TEXT("%s is in the range table but is not a numeric tuning field"), *Row.Path);
			return false;
		}
		if (!C.Closed(*Row.Path, Value, Row.Min, Row.Max))
		{
			return false;
		}
	}
	// Rules between fields.
	return C.Order(TEXT("WalkSpeed"), WalkSpeed, TEXT("JogSpeed"), JogSpeed)
		&& C.Order(TEXT("JogSpeed"), JogSpeed, TEXT("SprintSpeed"), SprintSpeed)
		&& C.Order(TEXT("FirstPersonCrouchedEyeHeightCm"), FirstPersonCrouchedEyeHeightCm, TEXT("FirstPersonEyeHeightCm"), FirstPersonEyeHeightCm)
		&& C.Order(TEXT("RagdollSettleSeconds"), RagdollSettleSeconds, TEXT("RagdollMaxSeconds"), RagdollMaxSeconds);
}
