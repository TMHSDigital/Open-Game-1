#include "CSGuardComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "CSCharacter.h"
#include "CSMovementTuning.h"
#include "CSPhysicsBodyComponent.h"
#include "CSStrikeComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/BodyInstance.h"

FCSDodged UCSGuardComponent::OnDodged;

UCSGuardComponent::UCSGuardComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the animation pose is set: the fists are steered against it, as the punch's are.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

ACSCharacter* UCSGuardComponent::GetCharacter() const
{
	return Cast<ACSCharacter>(GetOwner());
}

const UCSMovementTuning& UCSGuardComponent::GetTuning() const
{
	const ACSCharacter* Character = GetCharacter();
	return Character ? Character->GetActiveTuning() : *GetDefault<UCSMovementTuning>();
}

bool UCSGuardComponent::IsStanding() const
{
	const ACSCharacter* Character = GetCharacter();
	const UCSPhysicsBodyComponent* Physics = Character ? Character->GetPhysicsBody() : nullptr;
	if (!Character || Character->IsRagdolled())
	{
		return false;
	}
	if (!Physics)
	{
		return true;
	}
	const ECSBodyState State = Physics->GetState();
	return State == ECSBodyState::Normal || State == ECSBodyState::Hit;
}

void UCSGuardComponent::SetBlockHeld(bool bHeld)
{
	bBlockHeld = bHeld;
}

bool UCSGuardComponent::IsGuardUp() const
{
	if (!bBlockHeld || IsDodging() || !IsStanding())
	{
		return false;
	}
	const ACSCharacter* Character = GetCharacter();
	const UCSStrikeComponent* Strike = Character ? Character->GetStrike() : nullptr;
	// A punch out opens the guard until the arm is on its way back.
	return !Strike || Strike->GetPhase() == ECSStrikePhase::None || Strike->GetPhase() == ECSStrikePhase::Recover;
}

bool UCSGuardComponent::IsInFront(const FVector& Facing, const FVector& ToFrom, float AngleDegrees)
{
	const FVector Flat = ToFrom.GetSafeNormal2D();
	const FVector Ahead = Facing.GetSafeNormal2D();
	if (Flat.IsNearlyZero() || Ahead.IsNearlyZero())
	{
		// Right on top of the character: nothing to tell; the guard takes it.
		return true;
	}
	return FVector::DotProduct(Flat, Ahead) >= FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(AngleDegrees, 0.f, 180.f)));
}

bool UCSGuardComponent::Covers(const FVector& From) const
{
	const ACSCharacter* Character = GetCharacter();
	return Character && IsGuardUp() && IsInFront(Character->GetActorForwardVector(), From - Character->GetActorLocation(), GetTuning().BlockAngle);
}

bool UCSGuardComponent::IsDodging() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() < DodgeEnd;
}

bool UCSGuardComponent::Dodge(const FVector& Direction)
{
	ACSCharacter* Character = GetCharacter();
	const UWorld* World = GetWorld();
	const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Character || !World || !Move || !Move->IsMovingOnGround() || !IsStanding())
	{
		return false;
	}
	const UCSMovementTuning& T = GetTuning();
	const double Now = World->GetTimeSeconds();
	if (T.DodgeSpeed <= 0.f || T.DodgeSeconds <= 0.f || Now < DodgeEnd + T.DodgeCooldownSeconds)
	{
		return false;
	}
	const UCSStrikeComponent* Strike = Character->GetStrike();
	if (Strike && (Strike->GetPhase() == ECSStrikePhase::Windup || Strike->GetPhase() == ECSStrikePhase::Strike))
	{
		return false;
	}
	const FVector Flat = Direction.GetSafeNormal2D();
	DodgeDirection = Flat.IsNearlyZero() ? -Character->GetActorForwardVector().GetSafeNormal2D() : Flat;
	DodgeStart = Now;
	DodgeEnd = Now + T.DodgeSeconds;
	++DodgeCount;
	OnDodged.Broadcast(Character);
	return true;
}

float UCSGuardComponent::ApplyToMovement(UCharacterMovementComponent& Move, float Cap, float JogSpeed) const
{
	const UCSMovementTuning& T = GetTuning();
	if (IsDodging())
	{
		if (!Move.IsMovingOnGround() || !IsStanding())
		{
			return Cap;
		}
		// Set every frame: a single push would be braked away by ground friction within a step.
		Move.Velocity = DodgeDirection * T.DodgeSpeed + FVector(0.f, 0.f, Move.Velocity.Z);
		return FMath::Max(Cap, T.DodgeSpeed);
	}
	if (IsGuardUp())
	{
		return FMath::Min(Cap, JogSpeed) * T.BlockSpeedScale;
	}
	return Cap;
}

void UCSGuardComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (IsGuardUp())
	{
		DriveFists(DeltaTime);
	}
}

void UCSGuardComponent::DriveFists(float DeltaTime)
{
	const ACSCharacter* Character = GetCharacter();
	USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	const UCSStrikeComponent* Strike = Character ? Character->GetStrike() : nullptr;
	if (!Body || !Strike || Body->GetBoneIndex(HeadBone) == INDEX_NONE)
	{
		return;
	}
	// The facing (the aim in a combat stance): the guard is in front of the face, whichever way the legs go.
	const FVector Ahead = Character->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Ahead);
	const FVector Head = Body->GetSocketLocation(HeadBone);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		// Index 0 is the right hand (the strike component's order).
		FBodyInstance* Fist = Body->GetBodyInstance(Strike->HandBones[Index]);
		if (!Fist || !Fist->IsValidBodyInstance() || !Fist->IsInstanceSimulatingPhysics())
		{
			Fist = Body->GetBodyInstance(Strike->ForearmBones[Index]);
		}
		if (!Fist || !Fist->IsValidBodyInstance() || !Fist->IsInstanceSimulatingPhysics())
		{
			continue;
		}
		const float Side = Index == 0 ? GuardSide : -GuardSide;
		const FVector Goal = Head + Ahead * GuardForward + Right * Side - FVector(0.f, 0.f, GuardDrop);
		const FVector Hand = Body->GetSocketLocation(Strike->HandBones[Index]);
		Fist->SetLinearVelocity(UCSStrikeComponent::ComputeDriveVelocity(Hand, Goal, DeltaTime, GuardSpeed, Character->GetVelocity()), false);
	}
}
