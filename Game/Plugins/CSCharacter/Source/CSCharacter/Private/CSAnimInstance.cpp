#include "CSAnimInstance.h"

#include "CSPhysicsBodyComponent.h"

#include "CSCharacter.h"
#include "CSMovementTuning.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

ECSLocomotionPose UCSAnimInstance::PoseForState(ECSLocomotionState InState)
{
	switch (InState)
	{
	case ECSLocomotionState::CrouchIdle:
	case ECSLocomotionState::CrouchWalk:
		return ECSLocomotionPose::Crouch;
	case ECSLocomotionState::JumpStart:
		return ECSLocomotionPose::JumpStart;
	case ECSLocomotionState::Fall:
		return ECSLocomotionPose::Fall;
	case ECSLocomotionState::Land:
		return ECSLocomotionPose::Land;
	case ECSLocomotionState::Idle:
	case ECSLocomotionState::Walk:
	case ECSLocomotionState::Jog:
	case ECSLocomotionState::Sprint:
	default:
		return ECSLocomotionPose::Ground;
	}
}

ECSLocomotionState UCSAnimInstance::SelectGroundState(float Speed, bool bCrouching, float WalkSpeed, float JogSpeed, float SprintSpeed, float CrouchSpeed)
{
	if (bCrouching)
	{
		return Speed < 0.5f * CrouchSpeed ? ECSLocomotionState::CrouchIdle : ECSLocomotionState::CrouchWalk;
	}
	if (Speed < 0.5f * WalkSpeed)
	{
		return ECSLocomotionState::Idle;
	}
	if (Speed < 0.5f * (WalkSpeed + JogSpeed))
	{
		return ECSLocomotionState::Walk;
	}
	if (Speed < 0.5f * (JogSpeed + SprintSpeed))
	{
		return ECSLocomotionState::Jog;
	}
	return ECSLocomotionState::Sprint;
}

float UCSAnimInstance::ComputeLocomotion(float InGroundSpeed, TConstArrayView<float> GaitSpeeds, TConstArrayView<FCSStrideSample> Stride,
	float RateMin, float RateMax, float& OutAxis)
{
	const float Speed = FMath::IsFinite(InGroundSpeed) ? FMath::Max(InGroundSpeed, 0.f) : 0.f;

	// Axis: piecewise linear through (0, 0), (GaitSpeeds[0], 1), (GaitSpeeds[1], 2), ...
	OutAxis = float(GaitSpeeds.Num());
	float Low = 0.f;
	for (int32 Index = 0; Index < GaitSpeeds.Num(); ++Index)
	{
		const float High = GaitSpeeds[Index];
		if (Speed <= High)
		{
			// Two gaits tuned to the same speed leave no span to blend across: take the upper one.
			OutAxis = High - Low > KINDA_SMALL_NUMBER ? float(Index) + FMath::Clamp((Speed - Low) / (High - Low), 0.f, 1.f) : float(Index + 1);
			break;
		}
		Low = High;
	}

	if (Stride.Num() < 2)
	{
		return 1.f;
	}
	const float Axis = FMath::Clamp(OutAxis, 0.f, float(Stride.Num() - 1));
	const int32 Lower = FMath::Min(FMath::FloorToInt32(Axis), Stride.Num() - 2);
	const float Upper = Axis - float(Lower);
	const FCSStrideSample& A = Stride[Lower];
	const FCSStrideSample& B = Stride[Lower + 1];
	const float Seconds = (1.f - Upper) * A.Seconds + Upper * B.Seconds;
	const float Natural = Seconds > KINDA_SMALL_NUMBER
		? ((1.f - Upper) * A.StrideSpeed * A.Seconds + Upper * B.StrideSpeed * B.Seconds) / Seconds
		: FMath::Lerp(A.StrideSpeed, B.StrideSpeed, Upper);
	if (Natural <= 1.f)
	{
		return 1.f;
	}
	const float Lowest = FMath::Max(RateMin, 0.01f);
	return FMath::Clamp(Speed / Natural, Lowest, FMath::Max(RateMax, Lowest));
}

void UCSAnimInstance::SetState(ECSLocomotionState NewState)
{
	if (NewState != State)
	{
		State = NewState;
		StateTime = 0.f;
	}
	PoseIndex = int32(PoseForState(State));
}

float UCSAnimInstance::SplitStride(float NaturalRate, float RateMin, float RateMax, float ScaleMin, float ScaleMax, float& OutStrideScale)
{
	OutStrideScale = 1.f;
	if (NaturalRate <= 0.f)
	{
		return NaturalRate;
	}
	const float Rate = FMath::Clamp(NaturalRate, FMath::Min(RateMin, RateMax), FMath::Max(RateMin, RateMax));
	OutStrideScale = FMath::Clamp(NaturalRate / Rate, FMath::Min(ScaleMin, ScaleMax), FMath::Max(ScaleMin, ScaleMax));
	return NaturalRate / OutStrideScale;
}

void UCSAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		// Editor preview or a non-character owner: hold the idle pose.
		GroundSpeed = VerticalSpeed = 0.f;
		LocomotionAxis = 0.f;
		LocomotionPlayRate = 1.f;
		StrideScale = 1.f;
		bIsCrouching = bIsInAir = false;
		SetState(ECSLocomotionState::Idle);
		return;
	}

	const FVector Velocity = Move->Velocity;
	GroundSpeed = Velocity.Size2D();
	VerticalSpeed = Velocity.Z;
	bIsCrouching = Character->bIsCrouched;
	if (const ACSCharacter* GettingUp = Cast<ACSCharacter>(Character))
	{
		// Getting up after a ragdoll goes by way of the crouch: the body is pulled toward this pose.
		const UCSPhysicsBodyComponent* PhysicsBody = GettingUp->GetPhysicsBody();
		bIsCrouching |= PhysicsBody && PhysicsBody->IsGetUpCrouch();
	}
	const bool bWasInAir = bIsInAir;
	bIsInAir = Move->IsFalling();
	StateTime += DeltaSeconds;

	const UCSMovementTuning* Tuning = GetDefault<UCSMovementTuning>();
	float Walk = Tuning->WalkSpeed, Jog = Tuning->JogSpeed, Sprint = Tuning->SprintSpeed, Crouch = Tuning->CrouchSpeed;
	if (const ACSCharacter* CS = Cast<ACSCharacter>(Character))
	{
		Walk = CS->GetGaitSpeed(ECSGait::Walk);
		Jog = CS->GetGaitSpeed(ECSGait::Jog);
		Sprint = CS->GetGaitSpeed(ECSGait::Sprint);
		Crouch = CS->GetGaitSpeed(ECSGait::Crouch);
		Tuning = &CS->GetActiveTuning();
	}

	// Blend position and play rate from the speed actually moved, so the feet match the ground.
	const float StandingGaits[] = {Walk, Jog, Sprint};
	const float CrouchGaits[] = {Crouch};
	// The rate the feet need, unclamped, then split between rate and the length of the steps.
	const float NaturalRate = bIsCrouching
		? ComputeLocomotion(GroundSpeed, CrouchGaits, CrouchStride, 0.01f, 100.f, LocomotionAxis)
		: ComputeLocomotion(GroundSpeed, StandingGaits, GroundStride, 0.01f, 100.f, LocomotionAxis);
	LocomotionPlayRate = SplitStride(NaturalRate, Tuning->StrideRateMin, Tuning->StrideRateMax, Tuning->StrideScaleMin, Tuning->StrideScaleMax, StrideScale);
	LocomotionPlayRate = FMath::Clamp(LocomotionPlayRate, Tuning->LocomotionRateMin, Tuning->LocomotionRateMax);
	if (bIsInAir || GroundSpeed < 1.f)
	{
		// Standing or off the ground there are no steps to change.
		StrideScale = 1.f;
	}

	if (bIsInAir)
	{
		if (!bWasInAir)
		{
			SetState(VerticalSpeed > 0.f ? ECSLocomotionState::JumpStart : ECSLocomotionState::Fall);
		}
		else if (State == ECSLocomotionState::JumpStart && (VerticalSpeed <= 0.f || StateTime >= JumpStartSeconds))
		{
			SetState(ECSLocomotionState::Fall);
		}
		else if (State != ECSLocomotionState::JumpStart)
		{
			SetState(ECSLocomotionState::Fall);
		}
		return;
	}

	// Land only when touching down slow enough to stop; a running landing goes straight on running.
	const bool bSlow = GroundSpeed < 0.5f * Walk;
	if (bWasInAir && bSlow)
	{
		SetState(ECSLocomotionState::Land);
		return;
	}
	if (State == ECSLocomotionState::Land && StateTime < LandSeconds && bSlow)
	{
		return;
	}
	SetState(SelectGroundState(GroundSpeed, bIsCrouching, Walk, Jog, Sprint, Crouch));
}
