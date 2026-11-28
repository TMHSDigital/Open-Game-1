#include "CSTuningRanges.h"

#include "CSMovementTuning.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FRowSpec
	{
		const TCHAR* Path;
		const TCHAR* Group;
		const TCHAR* Unit;
		float Min;
		float Max;
		float Step;
	};

	// Where a field has UIMin and UIMax in CSMovementTuning.h they equal Min and Max here
	// (test CombatSimulator.CS.Tuning.EditorRangesAgree).
	const FRowSpec Specs[] = {
		{TEXT("WalkSpeed"), TEXT("Speeds"), TEXT("cm/s"), 50.f, 400.f, 5.f},
		{TEXT("JogSpeed"), TEXT("Speeds"), TEXT("cm/s"), 100.f, 700.f, 5.f},
		{TEXT("SprintSpeed"), TEXT("Speeds"), TEXT("cm/s"), 200.f, 1200.f, 5.f},
		{TEXT("CrouchSpeed"), TEXT("Speeds"), TEXT("cm/s"), 50.f, 400.f, 5.f},
		{TEXT("SprintRampSeconds"), TEXT("Speeds"), TEXT("s"), 0.1f, 3.f, 0.05f},
		{TEXT("Acceleration"), TEXT("Speeds"), TEXT("cm/s^2"), 200.f, 6000.f, 50.f},

		{TEXT("GroundFriction"), TEXT("Turning"), TEXT(""), 1.f, 20.f, 0.5f},
		{TEXT("SprintGripScale"), TEXT("Turning"), TEXT(""), 0.1f, 1.f, 0.05f},
		{TEXT("RotationRateYaw"), TEXT("Turning"), TEXT("deg/s"), 90.f, 1440.f, 10.f},

		{TEXT("BrakingFriction"), TEXT("Braking and slopes"), TEXT(""), 0.f, 10.f, 0.1f},
		{TEXT("BrakingDeceleration"), TEXT("Braking and slopes"), TEXT("cm/s^2"), 0.f, 4000.f, 50.f},
		{TEXT("MaxWalkableSlopeDeg"), TEXT("Braking and slopes"), TEXT("deg"), 5.f, 85.f, 1.f},
		{TEXT("UphillSpeedLoss"), TEXT("Braking and slopes"), TEXT(""), 0.f, 0.99f, 0.01f},
		{TEXT("DownhillSpeedGain"), TEXT("Braking and slopes"), TEXT(""), 0.f, 1.f, 0.01f},

		{TEXT("JumpHeightCm"), TEXT("Air"), TEXT("cm"), 10.f, 150.f, 1.f},
		{TEXT("AirControl"), TEXT("Air"), TEXT(""), 0.f, 1.f, 0.01f},
		{TEXT("LandingSpeedLossPerMps"), TEXT("Air"), TEXT(""), 0.f, 0.5f, 0.01f},

		{TEXT("ThirdPersonFOV"), TEXT("Camera"), TEXT("deg"), 60.f, 120.f, 1.f},
		{TEXT("FirstPersonFOV"), TEXT("Camera"), TEXT("deg"), 60.f, 120.f, 1.f},
		{TEXT("CameraBlendSeconds"), TEXT("Camera"), TEXT("s"), 0.05f, 1.f, 0.05f},
		{TEXT("ThirdPersonArmLength"), TEXT("Camera"), TEXT("cm"), 50.f, 1000.f, 10.f},
		{TEXT("ThirdPersonShoulderOffset.X"), TEXT("Camera"), TEXT("cm"), -200.f, 200.f, 1.f},
		{TEXT("ThirdPersonShoulderOffset.Y"), TEXT("Camera"), TEXT("cm"), -200.f, 200.f, 1.f},
		{TEXT("ThirdPersonShoulderOffset.Z"), TEXT("Camera"), TEXT("cm"), -200.f, 200.f, 1.f},
		{TEXT("ThirdPersonLagSpeed"), TEXT("Camera"), TEXT(""), 2.f, 30.f, 0.5f},
		{TEXT("ThirdPersonLagMaxDistance"), TEXT("Camera"), TEXT("cm"), 0.f, 200.f, 5.f},
		{TEXT("FirstPersonSmoothSeconds"), TEXT("Camera"), TEXT("s"), 0.f, 1.f, 0.01f},
		{TEXT("FirstPersonEyeHeightCm"), TEXT("Camera"), TEXT("cm"), 100.f, 300.f, 1.f},
		{TEXT("FirstPersonCrouchedEyeHeightCm"), TEXT("Camera"), TEXT("cm"), 50.f, 160.f, 1.f},
		{TEXT("FirstPersonHeightSmoothSeconds"), TEXT("Camera"), TEXT("s"), 0.f, 2.f, 0.01f},

		{TEXT("RagdollRecoverSeconds"), TEXT("Ragdoll"), TEXT("s"), 0.1f, 3.f, 0.05f},
		{TEXT("GetUpStrength"), TEXT("Ragdoll"), TEXT("x"), 0.f, 20.f, 0.1f},
		{TEXT("GetUpCrouchSeconds"), TEXT("Ragdoll"), TEXT("s"), 0.f, 3.f, 0.05f},
		{TEXT("GetUpStandSeconds"), TEXT("Ragdoll"), TEXT("s"), 0.f, 3.f, 0.05f},
		{TEXT("RagdollImpulseThreshold"), TEXT("Ragdoll"), TEXT("cm/s"), 50.f, 5000.f, 10.f},
		{TEXT("RagdollLandingSpeedThreshold"), TEXT("Ragdoll"), TEXT("cm/s"), 300.f, 5000.f, 10.f},
		{TEXT("RagdollSettleSpeed"), TEXT("Ragdoll"), TEXT("cm/s"), 5.f, 200.f, 1.f},
		{TEXT("RagdollSettleSeconds"), TEXT("Ragdoll"), TEXT("s"), 0.1f, 10.f, 0.05f},
		{TEXT("RagdollMaxSeconds"), TEXT("Ragdoll"), TEXT("s"), 1.f, 30.f, 0.5f},
		{TEXT("RagdollLinearDamping"), TEXT("Ragdoll"), TEXT(""), 0.f, 10.f, 0.1f},
		{TEXT("RagdollAngularDamping"), TEXT("Ragdoll"), TEXT(""), 0.f, 20.f, 0.1f},
		{TEXT("RagdollFriction"), TEXT("Ragdoll"), TEXT(""), 0.f, 4.f, 0.05f},

		{TEXT("NormalProfile.OrientationStrength"), TEXT("Body"), TEXT(""), 0.f, 5000.f, 10.f},
		{TEXT("NormalProfile.AngularVelocityStrength"), TEXT("Body"), TEXT(""), 0.f, 1000.f, 5.f},
		{TEXT("NormalProfile.PositionStrength"), TEXT("Body"), TEXT(""), 0.f, 5000.f, 10.f},
		{TEXT("NormalProfile.VelocityStrength"), TEXT("Body"), TEXT(""), 0.f, 1000.f, 5.f},
		{TEXT("NormalProfile.MaxLinearForce"), TEXT("Body"), TEXT(""), 0.f, 100000.f, 100.f},
		{TEXT("NormalProfile.MaxAngularForce"), TEXT("Body"), TEXT(""), 0.f, 100000.f, 100.f},
		{TEXT("NormalLimbStrengthScale"), TEXT("Body"), TEXT(""), 0.f, 1.f, 0.01f},
		{TEXT("HitProfile.OrientationStrength"), TEXT("Body"), TEXT(""), 0.f, 5000.f, 10.f},
		{TEXT("HitProfile.AngularVelocityStrength"), TEXT("Body"), TEXT(""), 0.f, 1000.f, 5.f},
		{TEXT("HitProfile.PositionStrength"), TEXT("Body"), TEXT(""), 0.f, 5000.f, 10.f},
		{TEXT("HitProfile.VelocityStrength"), TEXT("Body"), TEXT(""), 0.f, 1000.f, 5.f},
		{TEXT("HitProfile.MaxLinearForce"), TEXT("Body"), TEXT(""), 0.f, 100000.f, 100.f},
		{TEXT("HitProfile.MaxAngularForce"), TEXT("Body"), TEXT(""), 0.f, 100000.f, 100.f},
		{TEXT("HitHoldSeconds"), TEXT("Body"), TEXT("s"), 0.f, 1.f, 0.01f},
		{TEXT("HitRecoverSeconds"), TEXT("Body"), TEXT("s"), 0.05f, 3.f, 0.05f},
		{TEXT("HitCapsuleVelocityShare"), TEXT("Body"), TEXT(""), 0.f, 1.f, 0.01f},
		{TEXT("DevPushSpeed"), TEXT("Body"), TEXT("cm/s"), 50.f, 3000.f, 10.f},
		{TEXT("LocomotionRateMin"), TEXT("Body"), TEXT("x"), 0.25f, 1.f, 0.05f},
		{TEXT("LocomotionRateMax"), TEXT("Body"), TEXT("x"), 1.f, 4.f, 0.05f},
		{TEXT("StrideRateMin"), TEXT("Body"), TEXT("x"), 0.5f, 1.f, 0.01f},
		{TEXT("StrideRateMax"), TEXT("Body"), TEXT("x"), 1.f, 2.f, 0.01f},
		{TEXT("StrideScaleMin"), TEXT("Body"), TEXT("x"), 0.5f, 1.f, 0.01f},
		{TEXT("StrideScaleMax"), TEXT("Body"), TEXT("x"), 1.f, 2.f, 0.01f},
		{TEXT("MinContactSpeed"), TEXT("Body"), TEXT("cm/s"), 0.f, 2000.f, 10.f},
		{TEXT("ContactImpulseScale"), TEXT("Body"), TEXT(""), 0.f, 5.f, 0.05f},
		{TEXT("CharacterContactScale"), TEXT("Body"), TEXT(""), 0.f, 2.f, 0.05f},
		{TEXT("ContactImpulseMax"), TEXT("Body"), TEXT("cm/s"), 100.f, 10000.f, 50.f},
		{TEXT("ContactRepeatSeconds"), TEXT("Body"), TEXT("s"), 0.05f, 3.f, 0.05f},
		{TEXT("DevBallMass"), TEXT("Body"), TEXT("kg"), 0.5f, 100.f, 0.5f},
		{TEXT("DevBallSpeed"), TEXT("Body"), TEXT("cm/s"), 100.f, 5000.f, 50.f},
		{TEXT("PunchWindupSeconds"), TEXT("Punch"), TEXT("s"), 0.f, 0.5f, 0.01f},
		{TEXT("PunchStrikeSeconds"), TEXT("Punch"), TEXT("s"), 0.05f, 0.6f, 0.01f},
		{TEXT("PunchRecoverSeconds"), TEXT("Punch"), TEXT("s"), 0.05f, 1.f, 0.01f},
		{TEXT("PunchReach"), TEXT("Punch"), TEXT("cm"), 30.f, 120.f, 1.f},
		{TEXT("PunchSpeed"), TEXT("Punch"), TEXT("cm/s"), 200.f, 4000.f, 50.f},
		{TEXT("PunchLean"), TEXT("Punch"), TEXT("cm/s"), 0.f, 600.f, 10.f},
		{TEXT("PunchAssistRange"), TEXT("Punch"), TEXT("cm"), 0.f, 500.f, 5.f},
		{TEXT("PunchAssistAngle"), TEXT("Punch"), TEXT("deg"), 0.f, 90.f, 1.f},
		{TEXT("PunchLungeSpeed"), TEXT("Punch"), TEXT("cm/s"), 0.f, 800.f, 10.f},
		{TEXT("CombatFacingRange"), TEXT("Combat"), TEXT("cm"), 0.f, 2000.f, 10.f},
		{TEXT("CombatFacingSeconds"), TEXT("Combat"), TEXT("s"), 0.f, 10.f, 0.1f},
		{TEXT("CombatBackSpeedScale"), TEXT("Combat"), TEXT("x"), 0.3f, 1.f, 0.01f},
		{TEXT("CombatStanceMaxSpeed"), TEXT("Combat"), TEXT("cm/s"), 0.f, 700.f, 10.f},
		{TEXT("CombatStanceSpeedRange"), TEXT("Combat"), TEXT("cm"), 0.f, 2000.f, 10.f},
		{TEXT("CombatStanceAnimationShare"), TEXT("Combat"), TEXT("x"), 0.f, 1.f, 0.01f},
		{TEXT("LegYawMax"), TEXT("Combat"), TEXT("deg"), 0.f, 90.f, 1.f},
		{TEXT("BlockAbsorb"), TEXT("Defence"), TEXT("x"), 0.f, 1.f, 0.01f},
		{TEXT("BlockAngle"), TEXT("Defence"), TEXT("deg"), 0.f, 180.f, 1.f},
		{TEXT("BlockSpeedScale"), TEXT("Defence"), TEXT("x"), 0.1f, 1.f, 0.01f},
		{TEXT("DodgeSpeed"), TEXT("Defence"), TEXT("cm/s"), 0.f, 1500.f, 10.f},
		{TEXT("DodgeSeconds"), TEXT("Defence"), TEXT("s"), 0.f, 1.f, 0.01f},
		{TEXT("DodgeCooldownSeconds"), TEXT("Defence"), TEXT("s"), 0.f, 3.f, 0.05f},
		{TEXT("StaminaMax"), TEXT("Stamina"), TEXT(""), 10.f, 300.f, 5.f},
		// At least 1 a second: at 0 an exhausted character would never get its guard back.
		{TEXT("StaminaRegen"), TEXT("Stamina"), TEXT("/s"), 1.f, 100.f, 1.f},
		{TEXT("StaminaRegenDelay"), TEXT("Stamina"), TEXT("s"), 0.f, 3.f, 0.05f},
		{TEXT("TiredStrikeScale"), TEXT("Stamina"), TEXT("x"), 0.2f, 1.f, 0.01f},
		{TEXT("BlockStaminaCost"), TEXT("Stamina"), TEXT("per cm/s"), 0.f, 0.5f, 0.005f},
		{TEXT("DodgeStaminaCost"), TEXT("Stamina"), TEXT(""), 0.f, 50.f, 1.f},
		{TEXT("GuardReturnShare"), TEXT("Stamina"), TEXT("x"), 0.f, 1.f, 0.01f},
		{TEXT("PunchMass"), TEXT("Punch"), TEXT("kg"), 1.f, 100.f, 0.5f},
	};

	/** "HitProfile.PositionStrength" -> "Hit Profile: Position Strength". */
	FString MakeLabel(const FString& Path)
	{
		FString Head, Tail;
		if (Path.Split(TEXT("."), &Head, &Tail))
		{
			return FName::NameToDisplayString(Head, false) + TEXT(": ") + FName::NameToDisplayString(Tail, false);
		}
		return FName::NameToDisplayString(Path, false);
	}

	/** The leaf property of a row path and the address of its value inside Tuning; null when there is none. */
	const FProperty* Resolve(const UCSMovementTuning& Tuning, const FString& Path, const void*& OutAddress)
	{
		OutAddress = nullptr;
		FString Head = Path, Tail;
		Path.Split(TEXT("."), &Head, &Tail);
		const FProperty* Property = FindFProperty<FProperty>(UCSMovementTuning::StaticClass(), *Head);
		if (!Property)
		{
			return nullptr;
		}
		const void* Address = Property->ContainerPtrToValuePtr<void>(&Tuning);
		if (!Tail.IsEmpty())
		{
			const FStructProperty* Struct = CastField<FStructProperty>(Property);
			Property = Struct ? FindFProperty<FProperty>(Struct->Struct, *Tail) : nullptr;
			if (!Property)
			{
				return nullptr;
			}
			Address = Property->ContainerPtrToValuePtr<void>(Address);
		}
		if (!Property->IsA<FFloatProperty>() && !Property->IsA<FDoubleProperty>())
		{
			return nullptr;
		}
		OutAddress = Address;
		return Property;
	}
}

const TArray<FCSTuningRow>& CSTuning::Rows()
{
	static const TArray<FCSTuningRow> Table = []
	{
		TArray<FCSTuningRow> Out;
		Out.Reserve(UE_ARRAY_COUNT(Specs));
		for (const FRowSpec& Spec : Specs)
		{
			FCSTuningRow Row;
			Row.Path = Spec.Path;
			Row.Group = Spec.Group;
			Row.Label = MakeLabel(Row.Path);
			Row.Unit = Spec.Unit;
			Row.Min = Spec.Min;
			Row.Max = Spec.Max;
			Row.Step = Spec.Step;
			Out.Add(MoveTemp(Row));
		}
		return Out;
	}();
	return Table;
}

const FCSTuningRow* CSTuning::FindRow(const FString& Path)
{
	return Rows().FindByPredicate([&Path](const FCSTuningRow& Row) { return Row.Path == Path; });
}

bool CSTuning::GetValue(const UCSMovementTuning& Tuning, const FString& Path, float& OutValue)
{
	const void* Address = nullptr;
	const FProperty* Property = Resolve(Tuning, Path, Address);
	if (!Property)
	{
		return false;
	}
	if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
	{
		OutValue = Float->GetPropertyValue(Address);
	}
	else
	{
		OutValue = float(CastFieldChecked<FDoubleProperty>(Property)->GetPropertyValue(Address));
	}
	return true;
}

bool CSTuning::SetValue(UCSMovementTuning& Tuning, const FString& Path, float Value)
{
	const void* Address = nullptr;
	const FProperty* Property = Resolve(Tuning, Path, Address);
	if (!Property)
	{
		return false;
	}
	void* Target = const_cast<void*>(Address);
	if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
	{
		Float->SetPropertyValue(Target, Value);
	}
	else
	{
		CastFieldChecked<FDoubleProperty>(Property)->SetPropertyValue(Target, double(Value));
	}
	return true;
}
