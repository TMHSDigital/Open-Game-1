#include "CSAnimInstance.h"

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

void UCSAnimInstance::SetState(ECSLocomotionState NewState)
{
	if (NewState != State)
	{
		State = NewState;
		StateTime = 0.f;
	}
	PoseIndex = int32(PoseForState(State));
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
		bIsCrouching = bIsInAir = false;
		SetState(ECSLocomotionState::Idle);
		return;
	}

	const FVector Velocity = Move->Velocity;
	GroundSpeed = Velocity.Size2D();
	VerticalSpeed = Velocity.Z;
	bIsCrouching = Character->bIsCrouched;
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
