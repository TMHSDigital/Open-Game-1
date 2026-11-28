#include "CSAnimInstance.h"

#include "CSPhysicsBodyComponent.h"

#include "CSCharacter.h"
#include "CSGuardComponent.h"
#include "CSMovementTuning.h"
#include "CSStrikeComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
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
	const ECSLocomotionPose Pose = PoseForState(State);
	PoseIndex = int32(bStanceLegs && Pose == ECSLocomotionPose::Ground
		? (bSideStep ? ECSLocomotionPose::StanceSide : ECSLocomotionPose::Stance) : Pose);
}

bool UCSAnimInstance::IsSideStep(float MoveAngle, float Angle, float Hysteresis, bool bWasSide)
{
	// Measured from the nearer of ahead and behind: 90 is straight sideways.
	const float FromAxis = 90.f - FMath::Abs(90.f - FMath::Abs(FRotator::NormalizeAxis(MoveAngle)));
	return FromAxis >= (bWasSide ? Angle - Hysteresis : Angle);
}

float UCSAnimInstance::ComputeLegYaw(float MoveAngle, float MaxYaw, bool bWasBackward, bool& bOutBackward)
{
	const float Angle = FRotator::NormalizeAxis(MoveAngle);
	bOutBackward = FMath::Abs(Angle) > (bWasBackward ? 90.f : 110.f);
	const float FromFacing = bOutBackward ? Angle - FMath::Sign(Angle) * 180.f : Angle;
	return FMath::Clamp(FromFacing, -FMath::Abs(MaxYaw), FMath::Abs(MaxYaw));
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

bool UCSAnimInstance::IsGuardUp() const
{
	return GuardMontage.IsValid() && Montage_IsPlaying(GuardMontage.Get());
}

void UCSAnimInstance::UpdateGuard(bool bWant, bool bBlock)
{
	const bool bPlaying = GuardMontage.IsValid() && Montage_IsPlaying(GuardMontage.Get());
	UAnimSequenceBase* Block = bBlock ? BlockClip.LoadSynchronous() : nullptr;
	if (bWant && (!bPlaying || bGuardIsBlock != (Block != nullptr)))
	{
		// Looped for as long as the stance lasts; stopped when it ends. The block and the guard
		// replace each other in the slot (the new one blends over the old).
		UAnimSequenceBase* Clip = Block ? Block : GuardClip.LoadSynchronous();
		if (Clip)
		{
			const float Blend = bPlaying ? BlockBlendSeconds : GuardBlendSeconds;
			GuardMontage = PlaySlotAnimationAsDynamicMontage(Clip, GuardSlot, Blend, Blend, 1.f, 1000000);
			bGuardIsBlock = Block != nullptr;
		}
	}
	else if (!bWant && bPlaying)
	{
		Montage_Stop(GuardBlendSeconds, GuardMontage.Get());
		GuardMontage.Reset();
	}
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
		LegYaw = 0.f;
		bIsCrouching = bIsInAir = false;
		SetState(ECSLocomotionState::Idle);
		return;
	}

	if (const ACSCharacter* Fighter = Cast<ACSCharacter>(Character))
	{
		const UCSPhysicsBodyComponent* Body = Fighter->GetPhysicsBody();
		const bool bStanding = !Body || Body->GetState() == ECSBodyState::Normal || Body->GetState() == ECSBodyState::Hit;
		// Not over a strike playing in the same slot (thrown on the move): it would stop the strike.
		const UCSStrikeComponent* Strike = Fighter->GetStrike();
		const bool bStriking = Strike && Strike->IsPlayingClip();
		const UCSGuardComponent* Guard = Fighter->GetGuard();
		const bool bBlock = Guard && Guard->IsGuardUp();
		if (!bStriking)
		{
			UpdateGuard((Fighter->IsInCombatStance() || bBlock) && bStanding && !Fighter->IsRagdolled() && !Character->bIsCrouched, bBlock);
		}
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

	// In the fighting stance the legs shuffle (stance idle, shuffle at walk speed, jog at jog speed),
	// when the Blueprint has the Stance pose.
	const ACSCharacter* Stancer = Cast<ACSCharacter>(Character);
	bStanceLegs = StanceStride.Num() >= 2 && Stancer && Stancer->IsInCombatStance() && !bIsCrouching;
	// Sideways in the stance: the side shuffle, its steps played backward for a step to the left.
	float SideMoveAngle = 0.f;
	if (GroundSpeed > 30.f)
	{
		const FVector Facing = Character->GetActorForwardVector().GetSafeNormal2D();
		const FVector Moving = Velocity.GetSafeNormal2D();
		SideMoveAngle = FMath::RadiansToDegrees(FMath::Atan2(float(FVector::CrossProduct(Facing, Moving).Z), float(FVector::DotProduct(Facing, Moving))));
	}
	bSideStep = bStanceLegs && StanceSideStride.Num() >= 2 && GroundSpeed > 30.f && !Move->IsFalling()
		&& IsSideStep(SideMoveAngle, SideStepAngle, SideStepHysteresis, bSideStep);

	// Blend position and play rate from the speed actually moved, so the feet match the ground.
	const float StandingGaits[] = {Walk, Jog, Sprint};
	const float StanceGaits[] = {Walk, Jog};
	const float CrouchGaits[] = {Crouch};
	// The rate the feet need, unclamped, then split between rate and the length of the steps.
	const float SideGaits[] = {Walk};
	const float NaturalRate = bIsCrouching
		? ComputeLocomotion(GroundSpeed, CrouchGaits, CrouchStride, 0.01f, 100.f, LocomotionAxis)
		: bSideStep
		? ComputeLocomotion(GroundSpeed, SideGaits, StanceSideStride, 0.01f, 100.f, LocomotionAxis)
		: bStanceLegs
		? ComputeLocomotion(GroundSpeed, StanceGaits, StanceStride, 0.01f, 100.f, LocomotionAxis)
		: ComputeLocomotion(GroundSpeed, StandingGaits, GroundStride, 0.01f, 100.f, LocomotionAxis);
	float WantedStride = 1.f;
	SplitStride(NaturalRate, Tuning->StrideRateMin, Tuning->StrideRateMax, Tuning->StrideScaleMin, Tuning->StrideScaleMax, WantedStride);
	// Only walking and running have steps to change: not the air, and not a landing (the state
	// from the last update, or one that starts in this one).
	const bool bLanding = State == ECSLocomotionState::Land || (bWasInAir && GroundSpeed < 0.5f * Walk);
	if (bIsInAir || bLanding)
	{
		WantedStride = 1.f;
	}
	// Starting and stopping: the slowest speeds need the shortest steps, which would jump the
	// stride from 1 the moment the character moves. It comes in over the first of the walk speed
	// and is eased, so the legs never snap.
	WantedStride = FMath::Lerp(1.f, WantedStride, FMath::Clamp(GroundSpeed / FMath::Max(0.5f * Walk, 1.f), 0.f, 1.f));
	StrideScale = FMath::FInterpTo(StrideScale, WantedStride, DeltaSeconds, StrideInterpSpeed);
	// The rate is whatever the stride in use leaves, so the feet match the ground while it eases.
	LocomotionPlayRate = FMath::Clamp(NaturalRate / FMath::Max(StrideScale, 0.01f), Tuning->LocomotionRateMin, Tuning->LocomotionRateMax);

	// Facing the aim and moving another way (the combat stance, first person): the legs turn toward
	// the movement and the steps play backward when backing off. Facing the way it moves this is
	// nothing: the angle is 0.
	float WantedLegYaw = 0.f;
	if (bSideStep)
	{
		// The side shuffle steps sideways itself: the legs stay with the facing, and the steps play
		// backward for a step to the left (the clip steps right).
		bStepsBackward = SideMoveAngle < 0.f;
	}
	else if (!bIsInAir && !bLanding && GroundSpeed > 30.f)
	{
		const FVector Facing = Character->GetActorForwardVector().GetSafeNormal2D();
		const FVector Moving = Velocity.GetSafeNormal2D();
		const float MoveAngle = FMath::RadiansToDegrees(FMath::Atan2(float(FVector::CrossProduct(Facing, Moving).Z), float(FVector::DotProduct(Facing, Moving))));
		bool bBackward = false;
		WantedLegYaw = ComputeLegYaw(MoveAngle, Tuning->LegYawMax, bStepsBackward, bBackward);
		bStepsBackward = bBackward;
	}
	else
	{
		bStepsBackward = false;
	}
	StepDirection = FMath::FInterpTo(StepDirection, bStepsBackward ? -1.f : 1.f, DeltaSeconds, LegYawInterpSpeed);
	LocomotionPlayRate *= StepDirection;
	LegYaw = FMath::FInterpTo(LegYaw, WantedLegYaw, DeltaSeconds, LegYawInterpSpeed);

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
