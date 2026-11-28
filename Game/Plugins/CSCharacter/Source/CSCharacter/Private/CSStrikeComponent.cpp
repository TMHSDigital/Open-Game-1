#include "CSStrikeComponent.h"

#include "CSCharacter.h"
#include "CSCharacterLog.h"
#include "CSMovementTuning.h"
#include "CSPhysicsBodyComponent.h"
#include "CSStaminaComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/BodyInstance.h"

FCSStrikeStarted UCSStrikeComponent::OnStrikeStarted;
FCSStrikeLanded UCSStrikeComponent::OnStrikeLanded;

UCSStrikeComponent::UCSStrikeComponent()
{
	// Before physics: the velocity set here is what this frame's simulation runs with.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	// Only while a strike is under way (SetPhase): every character has this component, dummies too.
	PrimaryComponentTick.bStartWithTickEnabled = false;
	Moves = FCSStrikeMoveData::Defaults();
	MoveRow = Moves[int32(ECSStrikeMove::Straight)];
}

const FCSStrikeMoveData& UCSStrikeComponent::GetMoveData(ECSStrikeMove Move) const
{
	const int32 Index = int32(Move);
	if (Moves.IsValidIndex(Index) && Moves[Index].Move == Move)
	{
		return Moves[Index];
	}
	// Rows edited out of order: find it; none at all: the defaults.
	if (const FCSStrikeMoveData* Found = Moves.FindByPredicate([Move](const FCSStrikeMoveData& Row) { return Row.Move == Move; }))
	{
		return *Found;
	}
	static const TArray<FCSStrikeMoveData> Fallback = FCSStrikeMoveData::Defaults();
	return Fallback[Index];
}

float UCSStrikeComponent::MoveStrikeSeconds() const
{
	if (Clip)
	{
		// The clip's own timing: from the moment the fist can land to its peak.
		return FMath::Max(MoveRow.ActiveEnd - MoveRow.ActiveStart, 0.01f) / ClipRate;
	}
	return GetTuning().PunchStrikeSeconds * MoveRow.StrikeScale;
}

float UCSStrikeComponent::MoveRecoverSeconds() const
{
	if (Clip)
	{
		return FMath::Max(Clip->GetPlayLength() - MoveRow.ActiveEnd, 0.01f) / ClipRate;
	}
	return GetTuning().PunchRecoverSeconds * MoveRow.RecoverScale;
}

float UCSStrikeComponent::MoveReach() const
{
	return GetTuning().PunchReach * MoveRow.ReachScale;
}

float UCSStrikeComponent::MoveSpeed() const
{
	const UCSMovementTuning& T = GetTuning();
	return T.PunchSpeed * MoveRow.SpeedScale * (bTired ? FMath::Clamp(T.TiredStrikeScale, 0.1f, 1.f) : 1.f);
}

float UCSStrikeComponent::MoveMass() const
{
	const UCSMovementTuning& T = GetTuning();
	return T.PunchMass * MoveRow.MassScale * (bTired ? FMath::Clamp(T.TiredStrikeScale, 0.1f, 1.f) : 1.f);
}

ACSCharacter* UCSStrikeComponent::GetCharacter() const
{
	return Cast<ACSCharacter>(GetOwner());
}

USkeletalMeshComponent* UCSStrikeComponent::GetBody() const
{
	const ACSCharacter* Character = GetCharacter();
	return Character ? Character->GetMesh() : nullptr;
}

const UCSMovementTuning& UCSStrikeComponent::GetTuning() const
{
	const ACSCharacter* Character = GetCharacter();
	return Character ? Character->GetActiveTuning() : *GetDefault<UCSMovementTuning>();
}

bool UCSStrikeComponent::CanStrike() const
{
	const ACSCharacter* Character = GetCharacter();
	const UCSPhysicsBodyComponent* Physics = Character ? Character->GetPhysicsBody() : nullptr;
	if (!Physics || !Physics->HasPhysicsBody() || Character->IsRagdolled())
	{
		return false;
	}
	const ECSBodyState State = Physics->GetState();
	return State == ECSBodyState::Normal || State == ECSBodyState::Hit;
}

bool UCSStrikeComponent::IsFoe(const ACSCharacter& Other) const
{
	// The player's punches go for anyone. A character driven by the game (an opponent) fights the
	// player: another dummy in the way is not a target and does not soak the punch.
	const ACSCharacter* Character = GetCharacter();
	return !Character || Character->IsPlayerControlled() || Other.IsPlayerControlled();
}

float UCSStrikeComponent::GetWindupSeconds() const
{
	const UCSMovementTuning& T = GetTuning();
	const float Tired = bTired ? FMath::Clamp(T.TiredStrikeScale, 0.1f, 1.f) : 1.f;
	if (Clip)
	{
		// The clip's draw back, or a longer one when asked (an opponent's tell): the clip then plays
		// its draw back slower, so the tell is the strike being loaded.
		const float Authored = MoveRow.ActiveStart;
		const float Asked = WindupSecondsOverride >= 0.f ? WindupSecondsOverride * MoveRow.WindupScale : 0.f;
		return FMath::Max(Authored + StepWindupSeconds, Asked) / Tired;
	}
	const float Base = WindupSecondsOverride >= 0.f ? WindupSecondsOverride : T.PunchWindupSeconds;
	return Base * MoveRow.WindupScale / Tired;
}

bool UCSStrikeComponent::StartPunch()
{
	// A one-two: a straight right after a jab, else a jab.
	const UWorld* World = GetWorld();
	const bool bAfterJab = StrikeCount > 0 && MoveRow.Move == ECSStrikeMove::Jab && World
		&& World->GetTimeSeconds() - LastEndTime <= ComboSeconds;
	return StartMove(bAfterJab ? ECSStrikeMove::Straight : ECSStrikeMove::Jab);
}

bool UCSStrikeComponent::StartMove(ECSStrikeMove Move)
{
	if (Phase != ECSStrikePhase::None || !CanStrike())
	{
		return false;
	}
	ACSCharacter* Character = GetCharacter();
	MoveRow = GetMoveData(Move);
	bRightHand = MoveRow.bRightSide;
	// Thrown either way; without enough stamina it is thrown tired.
	UCSStaminaComponent* Stamina = Character ? Character->GetStamina() : nullptr;
	bTired = Stamina && !Stamina->Spend(MoveRow.StaminaCost);
	Clip = bUseClips && MoveRow.Clip.IsValid() ? Cast<UAnimSequenceBase>(MoveRow.Clip.TryLoad()) : nullptr;
	ClipRate = bTired ? FMath::Clamp(GetTuning().TiredStrikeScale, 0.1f, 1.f) : 1.f;
	if (MoveRow.bLeg && !Clip)
	{
		if (UCSPhysicsBodyComponent* Physics = Character ? Character->GetPhysicsBody() : nullptr)
		{
			Physics->SetLimbSimulated(KickBones[2], true);
		}
	}
	++StrikeCount;
	bLanded = false;
	bBlocked = false;
	PeakReach = 0.f;
	PeakSpeed = 0.f;
	HitActors.Reset();
	StrikeAim = GetAimDirection();
	Target = FindTarget();
	AimAtTarget();
	bSteppingIn = Target.IsValid();
	if (const ACSCharacter* Victim = Target.Get())
	{
		const USkeletalMeshComponent* VictimBody = Victim->GetMesh();
		LungeGoal = VictimBody ? VictimBody->GetSocketLocation(ChestBone) : Victim->GetActorLocation();
	}
	// Out of reach: a clip strike waits in its draw back for the step in to cover the gap.
	StepWindupSeconds = 0.f;
	if (Clip && Target.IsValid() && GetTuning().PunchLungeSpeed > 0.f)
	{
		const float Gap = float(FVector::Dist2D(LungeGoal, GetChestLocation())) - MoveReach();
		// A quarter more than the gap needs at the lunge speed: the step takes a moment to get going.
		StepWindupSeconds = FMath::Clamp(1.25f * Gap / GetTuning().PunchLungeSpeed, 0.f, MaxStepWindupSeconds);
	}
	bHasLastHand = false;
	if (Clip)
	{
		const UCSPhysicsBodyComponent* Physics = Character ? Character->GetPhysicsBody() : nullptr;
		bStartedInHit = Physics && Physics->GetState() == ECSBodyState::Hit;
		// The draw back plays at the rate that fills the wind-up (slower for a tell).
		const float Windup = GetWindupSeconds();
		PlayClip(Windup > UE_SMALL_NUMBER ? MoveRow.ActiveStart / Windup : ClipRate);
		SetAnimationOverride(1.f);
	}
	if (GetWindupSeconds() > 0.f)
	{
		SetPhase(ECSStrikePhase::Windup);
	}
	else
	{
		BeginStrike();
	}
	OnStrikeStarted.Broadcast(GetCharacter());
	return true;
}

void UCSStrikeComponent::PlayClip(float Rate)
{
	USkeletalMeshComponent* Body = GetBody();
	UAnimInstance* Anim = Body ? Body->GetAnimInstance() : nullptr;
	if (!Anim || !Clip)
	{
		Clip = nullptr;
		return;
	}
	// A punch thrown walking: the upper body punches while the legs walk. Not a kick (the leg is the
	// strike) and not a step in (the clip's hips carry the punch the last of the way).
	const ACSCharacter* Character = GetCharacter();
	const bool bMoving = !MoveRow.bLeg && StepWindupSeconds <= 0.f && Character && Character->GetVelocity().Size2D() > MovingStrikeSpeed;
	const FName Slot = bMoving && !MovingClipSlot.IsNone() ? MovingClipSlot : ClipSlot;
	ClipMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, Slot, ClipBlendInSeconds, ClipBlendOutSeconds, Rate);
	if (!ClipMontage.IsValid())
	{
		Clip = nullptr;
	}
}

void UCSStrikeComponent::SetClipRate(float Rate)
{
	USkeletalMeshComponent* Body = GetBody();
	UAnimInstance* Anim = Body ? Body->GetAnimInstance() : nullptr;
	if (Anim && ClipMontage.IsValid())
	{
		Anim->Montage_SetPlayRate(ClipMontage.Get(), Rate);
	}
}

void UCSStrikeComponent::StopClip(float BlendOut)
{
	USkeletalMeshComponent* Body = GetBody();
	UAnimInstance* Anim = Body ? Body->GetAnimInstance() : nullptr;
	if (Anim && ClipMontage.IsValid() && Anim->Montage_IsPlaying(ClipMontage.Get()))
	{
		Anim->Montage_Stop(BlendOut, ClipMontage.Get());
	}
	ClipMontage.Reset();
}

void UCSStrikeComponent::SetAnimationOverride(float Weight)
{
	const ACSCharacter* Character = GetCharacter();
	if (UCSPhysicsBodyComponent* Physics = Character ? Character->GetPhysicsBody() : nullptr)
	{
		Physics->SetAnimationOverride(Weight);
	}
}

void UCSStrikeComponent::ReleaseArm()
{
	if (Clip)
	{
		return; // the clip brings the arm back
	}
	// The fist was driven fast; let go at that speed, the arm swings on past the target and up over
	// the head. It is let go moving with the body instead, and the motors bring it back.
	const ACSCharacter* Character = GetCharacter();
	USkeletalMeshComponent* Body = GetBody();
	if (!Character || !Body)
	{
		return;
	}
	for (const FName Bone : {LimbEnd(), LimbMid()})
	{
		FBodyInstance* Part = Body->GetBodyInstance(Bone);
		if (Part && Part->IsValidBodyInstance() && Part->IsInstanceSimulatingPhysics())
		{
			Part->SetLinearVelocity(Character->GetVelocity(), false);
			Part->SetAngularVelocityInRadians(FVector::ZeroVector, false);
		}
	}
}

void UCSStrikeComponent::Cancel()
{
	StopClip(ClipBlendOutSeconds);
	SetPhase(ECSStrikePhase::None);
}

void UCSStrikeComponent::EndLeg()
{
	const ACSCharacter* Character = GetCharacter();
	if (UCSPhysicsBodyComponent* Physics = Character ? Character->GetPhysicsBody() : nullptr)
	{
		Physics->SetLimbSimulated(KickBones[2], false);
	}
}

void UCSStrikeComponent::SetPhase(ECSStrikePhase NewPhase)
{
	const bool bEnding = Phase != ECSStrikePhase::None && NewPhase == ECSStrikePhase::None;
	Phase = NewPhase;
	PhaseSeconds = 0.f;
	SetComponentTickEnabled(NewPhase != ECSStrikePhase::None);
	if (bEnding)
	{
		if (const UWorld* World = GetWorld())
		{
			LastEndTime = World->GetTimeSeconds();
		}
		if (MoveRow.bLeg && !Clip)
		{
			EndLeg();
		}
		if (Clip)
		{
			SetAnimationOverride(0.f);
			ClipMontage.Reset();
			Clip = nullptr;
		}
	}
}

ACSCharacter* UCSStrikeComponent::FindTarget() const
{
	const ACSCharacter* Self = GetCharacter();
	const UWorld* World = GetWorld();
	const UCSMovementTuning& T = GetTuning();
	if (!Self || !World || T.PunchAssistRange <= 0.f)
	{
		return nullptr;
	}
	const FVector From = GetChestLocation();
	const FVector Aim = GetAimDirection().GetSafeNormal2D();
	ACSCharacter* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
	{
		ACSCharacter* Other = *It;
		if (Other == Self || Other->IsActorBeingDestroyed() || Other->IsRagdolled() || !IsFoe(*Other))
		{
			continue;
		}
		const USkeletalMeshComponent* OtherBody = Other->GetMesh();
		const FVector Chest = OtherBody ? OtherBody->GetSocketLocation(ChestBone) : Other->GetActorLocation();
		const FVector To = (Chest - From) * FVector(1.f, 1.f, 0.f);
		const float Distance = float(To.Size());
		if (Distance > T.PunchAssistRange || Distance < 1.f)
		{
			continue;
		}
		// On another floor (a ledge above or below): not a target, or the step in would take the
		// attacker off an edge or into a wall.
		if (FMath::Abs(Chest.Z - From.Z) > MaxTargetHeightDifference)
		{
			continue;
		}
		const float Cos = float(FVector::DotProduct(To / Distance, Aim));
		// At arm's length the cone opens to the side: an opponent at the shoulder (a stagger moves it
		// there) is still the one in the fight, though no longer in front of the aim.
		const float Close = FMath::Clamp((Distance - T.PunchReach) / FMath::Max(T.PunchReach, 1.f), 0.f, 1.f);
		const float Angle = FMath::Lerp(FMath::Max(T.PunchAssistAngle, 90.f), T.PunchAssistAngle, Close);
		if (Cos < FMath::Cos(FMath::DegreesToRadians(Angle)))
		{
			continue;
		}
		// Nearer and straighter ahead wins: a target right in front beats one at the edge of the cone.
		const float Score = Distance / T.PunchAssistRange + (1.f - Cos);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Other;
		}
	}
	return Best;
}

FVector UCSStrikeComponent::GetAimPointOn(const ACSCharacter& Victim) const
{
	const USkeletalMeshComponent* VictimBody = Victim.GetMesh();
	const FVector Chest = VictimBody ? VictimBody->GetSocketLocation(ChestBone) : Victim.GetActorLocation();
	const ACSCharacter* Self = GetCharacter();
	if (!Self || !VictimBody)
	{
		return Chest;
	}
	// The line of sight from the eyes along the control rotation, at the target's distance.
	const FVector Eye = Self->GetPawnViewLocation();
	const FRotator Look = Self->GetController() ? Self->GetControlRotation() : Self->GetActorRotation();
	const double Across = FVector::Dist2D(Eye, Chest);
	const double Height = Eye.Z + FMath::Tan(FMath::DegreesToRadians(FRotator::NormalizeAxis(Look.Pitch))) * Across;
	// Held to the body: no lower than a little above the pelvis, no higher than the upper chest.
	const double Low = VictimBody->GetSocketLocation(TEXT("pelvis")).Z + 20.0;
	const double High = VictimBody->GetSocketLocation(AimTopBone).Z;
	return FVector(Chest.X, Chest.Y, FMath::Clamp(Height, FMath::Min(Low, High), FMath::Max(Low, High)));
}

void UCSStrikeComponent::AimAtTarget()
{
	const ACSCharacter* Victim = Target.Get();
	if (!Victim)
	{
		return;
	}
	FRotator Toward = (GetAimPointOn(*Victim) - GetChestLocation()).Rotation();
	Toward.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Toward.Pitch), -MaxAimPitch, MaxAimPitch);
	Toward.Roll = 0.f;
	StrikeAim = Toward.Vector();
}

void UCSStrikeComponent::BeginStrike()
{
	SetPhase(ECSStrikePhase::Strike);
	AimAtTarget();
	if (Clip)
	{
		SetClipRate(ClipRate);
	}
}

void UCSStrikeComponent::StepIn()
{
	ACSCharacter* Character = GetCharacter();
	const ACSCharacter* Victim = Target.Get();
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	const UCSMovementTuning& T = GetTuning();
	if (!Victim || !Move || !Move->IsMovingOnGround() || T.PunchLungeSpeed <= 0.f)
	{
		return;
	}
	// Out of reach: step in until the target's chest is within the arm's reach of this one. That is
	// closer than the two bodies can stand (their capsules stop them first), so the step goes on
	// until they meet or the strike ends, and the arm arrives with the body behind it. The step goes
	// to where the target's chest was when the punch started, give or take LungeLeash: a punch is
	// committed, so a target that steps away (a dodge) is not chased.
	// Followed while it sways (a stagger rocks the chest back and forth), but no farther than
	// LungeLeash from where it stood: one that steps away for real is let go.
	const USkeletalMeshComponent* VictimBody = Victim->GetMesh();
	const FVector Live = VictimBody ? VictimBody->GetSocketLocation(ChestBone) : Victim->GetActorLocation();
	const FVector Goal = LungeGoal + ((Live - LungeGoal) * FVector(1.f, 1.f, 0.f)).GetClampedToMaxSize(LungeLeash);
	const FVector To = (Goal - GetChestLocation()) * FVector(1.f, 1.f, 0.f);
	// A kick goes out from the hip, which is behind the chest when the body leans back over the
	// standing leg: it steps in closer than its full reach, or the foot arrives short.
	if (To.Size() <= MoveReach() * (MoveRow.bLeg ? 0.85f : 1.f))
	{
		bSteppingIn = false;
		return;
	}
	// The bodies have met: a short strike (a hook) can reach less far than two bodies stand apart,
	// and stepping on would slide this one around the other and carry it past. Stop, and drop the
	// speed toward it, so the strike is thrown from here.
	const float Apart = float(FVector::Dist2D(Character->GetActorLocation(), Victim->GetActorLocation()));
	if (Apart <= Character->GetSimpleCollisionRadius() + Victim->GetSimpleCollisionRadius() + StepInContactMargin)
	{
		const FVector Toward = (Victim->GetActorLocation() - Character->GetActorLocation()).GetSafeNormal2D();
		const float Closing = float(FVector::DotProduct(Move->Velocity, Toward));
		if (Closing > 0.f)
		{
			Move->Velocity -= Toward * Closing;
		}
		bSteppingIn = false;
		return;
	}
	const FVector Along = To.GetSafeNormal();
	// No walkable floor a step ahead: stop rather than run off it.
	FHitResult Floor;
	const FVector Ahead = Character->GetActorLocation() + Along * (Character->GetSimpleCollisionRadius() + 30.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CSStrikeStepIn), false, Character);
	const float Below = Character->GetSimpleCollisionHalfHeight() + Move->MaxStepHeight + 10.f;
	if (!GetWorld()->LineTraceSingleByChannel(Floor, Ahead, Ahead - FVector(0.f, 0.f, Below), ECC_Visibility, Params)
		|| !Move->IsWalkable(Floor))
	{
		bSteppingIn = false;
		return;
	}
	const float Current = float(FVector::DotProduct(Move->Velocity, Along));
	if (Current < T.PunchLungeSpeed)
	{
		Move->Velocity += Along * (T.PunchLungeSpeed - Current);
	}
}

FVector UCSStrikeComponent::GetAimDirection() const
{
	const ACSCharacter* Character = GetCharacter();
	if (!Character)
	{
		return FVector::ForwardVector;
	}
	// The control rotation is where the player aims. The view point is not: another camera (a cut,
	// a review camera) may be the view target, and a punch must not follow it.
	FRotator Aim = Character->GetController() ? Character->GetControlRotation() : Character->GetActorRotation();
	Aim.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Aim.Pitch), -MaxAimPitch, MaxAimPitch);
	Aim.Roll = 0.f;
	const FVector Direction = Aim.Vector();
	return Direction.IsNearlyZero() ? Character->GetActorForwardVector() : Direction;
}

FVector UCSStrikeComponent::GetHandLocation() const
{
	const USkeletalMeshComponent* Body = GetBody();
	return Body ? Body->GetSocketLocation(StrikeCount > 0 ? LimbEnd() : HandBones[0]) : FVector::ZeroVector;
}

FVector UCSStrikeComponent::GetFistLocation() const
{
	const USkeletalMeshComponent* Body = GetBody();
	if (!Body)
	{
		return FVector::ZeroVector;
	}
	const FName End = StrikeCount > 0 ? LimbEnd() : HandBones[0];
	const FName Mid = StrikeCount > 0 ? LimbMid() : ForearmBones[0];
	const FVector Hand = Body->GetSocketLocation(End);
	// The middle of the fist is past the wrist along the forearm; for a kick the ankle itself, which
	// leads a front kick, with the foot sweep's larger radius around it.
	if (StrikeCount > 0 && MoveRow.bLeg)
	{
		return Hand;
	}
	const FVector Along = (Hand - Body->GetSocketLocation(Mid)).GetSafeNormal();
	return Hand + Along * FistOffset;
}

FVector UCSStrikeComponent::GetChestLocation() const
{
	const USkeletalMeshComponent* Body = GetBody();
	return Body ? (Body->GetSocketLocation(ShoulderBones[0]) + Body->GetSocketLocation(ShoulderBones[1])) * 0.5 : FVector::ZeroVector;
}

FVector UCSStrikeComponent::ComputeHandTarget(ECSStrikePhase InPhase, const FVector& Shoulder, const FVector& Chest, const FVector& Aim, float Reach)
{
	switch (InPhase)
	{
	case ECSStrikePhase::Windup:
		// Cocked: at the shoulder, a hand's width forward so the elbow folds back and not through the chest.
		return Shoulder + Aim * 12.f;
	case ECSStrikePhase::Strike:
		return Chest + Aim * Reach;
	default:
		return Shoulder;
	}
}

FVector UCSStrikeComponent::ComputeHookTarget(ECSStrikePhase InPhase, const FVector& Shoulder, const FVector& Chest, const FVector& Aim,
	const FVector& Outward, float Reach, float Progress)
{
	switch (InPhase)
	{
	case ECSStrikePhase::Windup:
		// Cocked out to the side, elbow up: the hook's tell.
		return Shoulder + Outward * 22.f + Aim * 6.f;
	case ECSStrikePhase::Strike:
		return Chest + Aim * Reach + Outward * FMath::Lerp(40.f, -10.f, FMath::Clamp(Progress, 0.f, 1.f));
	default:
		return Shoulder;
	}
}

FVector UCSStrikeComponent::ComputeKickTarget(ECSStrikePhase InPhase, const FVector& Hip, const FVector& Aim, float Reach)
{
	const FVector Level = Aim.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Aim.GetSafeNormal2D();
	switch (InPhase)
	{
	case ECSStrikePhase::Windup:
		// Chambered: the knee comes up and forward, the foot under it.
		return Hip + Level * 30.f - FVector(0.f, 0.f, 40.f);
	case ECSStrikePhase::Strike:
		// Straight out at about hip height: into the body of someone standing.
		return Hip + Level * Reach - FVector(0.f, 0.f, 5.f);
	default:
		return Hip - FVector(0.f, 0.f, 90.f);
	}
}

FVector UCSStrikeComponent::ComputeDriveVelocity(const FVector& Hand, const FVector& Goal, float DeltaTime, float MaxSpeed, const FVector& BaseVelocity)
{
	if (DeltaTime <= UE_SMALL_NUMBER)
	{
		return BaseVelocity;
	}
	return BaseVelocity + ((Goal - Hand) / DeltaTime).GetClampedToMaxSize(FMath::Max(MaxSpeed, 0.f));
}

void UCSStrikeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Phase == ECSStrikePhase::None)
	{
		return;
	}
	if (!CanStrike())
	{
		// Knocked down mid-punch: the body is the ragdoll's now.
		Cancel();
		return;
	}
	if (Clip && Phase != ECSStrikePhase::Recover)
	{
		// Hit while loading or throwing: the strike is broken off and the hit shows.
		const ACSCharacter* Self = GetCharacter();
		const UCSPhysicsBodyComponent* Physics = Self ? Self->GetPhysicsBody() : nullptr;
		const bool bInHit = Physics && Physics->GetState() == ECSBodyState::Hit;
		if (bInHit && !bStartedInHit)
		{
			Cancel();
			return;
		}
		bStartedInHit &= bInHit;
	}
	const UCSMovementTuning& T = GetTuning();
	if (Phase == ECSStrikePhase::Strike)
	{
		// The path the fist travelled in the frame just simulated, before the phase can end: the
		// last frame of a strike is the one at full reach.
		Sweep();
	}
	else if (Phase == ECSStrikePhase::Windup && Clip)
	{
		// Followed through the draw back, so the first frame of the strike sweeps (and measures)
		// the way the fist came, not a standing start.
		LastHandLocation = GetFistLocation();
		bHasLastHand = GetBody() != nullptr;
	}
	PhaseSeconds += DeltaTime;
	if (Phase == ECSStrikePhase::Windup && PhaseSeconds >= GetWindupSeconds())
	{
		BeginStrike();
	}
	else if (Phase == ECSStrikePhase::Strike && PhaseSeconds >= MoveStrikeSeconds())
	{
		SetPhase(ECSStrikePhase::Recover);
		ReleaseArm();
	}
	else if (Phase == ECSStrikePhase::Recover && PhaseSeconds >= MoveRecoverSeconds())
	{
		SetPhase(ECSStrikePhase::None);
		return;
	}
	if (Clip && Phase == ECSStrikePhase::Recover)
	{
		// Back to the simulation over the recovery, so the body is its own again in the stance.
		SetAnimationOverride(1.f - FMath::Clamp(PhaseSeconds / FMath::Max(MoveRecoverSeconds(), 0.01f), 0.f, 1.f));
	}
	if (Phase == ECSStrikePhase::Windup || Phase == ECSStrikePhase::Strike)
	{
		// A wind-up drawn out longer than the punch's own (an opponent's tell) stands still and only
		// steps in for the last PunchWindupSeconds of it: a step held through the whole tell would
		// carry the body into the target and around its side.
		// The draw back stretched to step in (StepWindupSeconds) is stepping time, not a tell.
		const bool bLateEnough = Phase == ECSStrikePhase::Strike
			|| GetWindupSeconds() - PhaseSeconds <= T.PunchWindupSeconds * MoveRow.WindupScale + StepWindupSeconds;
		if (bSteppingIn && bLateEnough)
		{
			// Every frame until in reach: ground friction would stop a single push within the punch.
			StepIn();
		}
		if (!Clip)
		{
			Drive(DeltaTime);
		}
	}
}

void UCSStrikeComponent::Drive(float DeltaTime)
{
	const ACSCharacter* Character = GetCharacter();
	USkeletalMeshComponent* Body = GetBody();
	if (!Character || !Body)
	{
		return;
	}
	FBodyInstance* Fist = Body->GetBodyInstance(LimbEnd());
	if (!Fist || !Fist->IsValidBodyInstance() || !Fist->IsInstanceSimulatingPhysics())
	{
		// A physics asset with no hand (or foot) body: the forearm (or calf) carries it.
		Fist = Body->GetBodyInstance(LimbMid());
	}
	if (!Fist || !Fist->IsValidBodyInstance() || !Fist->IsInstanceSimulatingPhysics())
	{
		return;
	}
	const UCSMovementTuning& T = GetTuning();
	const FVector Root = Body->GetSocketLocation(LimbRoot());
	const FVector Hand = Body->GetSocketLocation(LimbEnd());
	FVector HandGoal;
	if (MoveRow.bLeg)
	{
		HandGoal = ComputeKickTarget(Phase, Root, StrikeAim, MoveReach());
	}
	else if (MoveRow.bHook)
	{
		const FVector Level = StrikeAim.GetSafeNormal2D();
		const FVector Outward = FVector::CrossProduct(FVector::UpVector, Level) * (bRightHand ? 1.f : -1.f);
		HandGoal = ComputeHookTarget(Phase, Root, GetChestLocation(), StrikeAim, Outward, MoveReach(),
			PhaseSeconds / FMath::Max(MoveStrikeSeconds(), 0.01f));
	}
	else
	{
		HandGoal = ComputeHandTarget(Phase, Root, GetChestLocation(), StrikeAim, MoveReach());
	}
	// The draw back is unhurried; only the strike is at full speed.
	const float Speed = Phase == ECSStrikePhase::Strike ? MoveSpeed() : MoveSpeed() * 0.5f;
	Fist->SetLinearVelocity(ComputeDriveVelocity(Hand, HandGoal, DeltaTime, Speed, Character->GetVelocity()), false);
	if (MoveRow.bLeg)
	{
		// The knee leads a kick: up and forward between the hip and the foot. Steering the foot alone
		// drags the leg out straight along the floor.
		FBodyInstance* Shin = Body->GetBodyInstance(LimbMid());
		if (Shin && Shin->IsValidBodyInstance() && Shin->IsInstanceSimulatingPhysics())
		{
			const FVector Level = StrikeAim.GetSafeNormal2D();
			const FVector KneeGoal = (Root + HandGoal) * 0.5 + Level * 15.f + FVector(0.f, 0.f, 15.f);
			Shin->SetLinearVelocity(ComputeDriveVelocity(Body->GetSocketLocation(LimbMid()), KneeGoal, DeltaTime, Speed, Character->GetVelocity()), false);
		}
	}

	// The lean: during the strike the chest moves along the aim at PunchLean or faster. Only what is
	// missing is added, so a chest already going that way (a hit from behind, a run) is left alone.
	// Not for a kick: leaning the chest in over a kick would tip the body onto the standing leg.
	FBodyInstance* Chest = Phase == ECSStrikePhase::Strike && T.PunchLean > 0.f && !MoveRow.bLeg ? Body->GetBodyInstance(ChestBone) : nullptr;
	if (Chest && Chest->IsValidBodyInstance() && Chest->IsInstanceSimulatingPhysics())
	{
		const FVector Current = Chest->GetUnrealWorldVelocity();
		const float Along = float(FVector::DotProduct(Current - Character->GetVelocity(), StrikeAim));
		if (Along < T.PunchLean)
		{
			Chest->SetLinearVelocity(Current + StrikeAim * (T.PunchLean - Along), false);
		}
	}
}

void UCSStrikeComponent::Sweep()
{
	ACSCharacter* Character = GetCharacter();
	const USkeletalMeshComponent* Body = GetBody();
	UWorld* World = GetWorld();
	if (!Character || !Body || !World)
	{
		return;
	}
	const FVector Hand = GetFistLocation();
	const UCSMovementTuning& T = GetTuning();
	const float DeltaTime = World->GetDeltaSeconds();
	// Farther than the fist can go in a frame: the character was moved (a reset, a teleport). The
	// gap is not a path the fist travelled, so it is neither swept nor measured.
	const float Radius = MoveRow.bLeg ? FootRadius : FistRadius;
	const float Plausible = (MoveSpeed() * 2.f + float(Character->GetVelocity().Size())) * DeltaTime + Radius + 30.f;
	if (bHasLastHand && FVector::Dist(LastHandLocation, Hand) > Plausible)
	{
		bHasLastHand = false;
	}
	const FVector From = bHasLastHand ? LastHandLocation : Hand;
	// Measured until the punch lands: the hit-stop that follows slows the world, and a frame that
	// short would turn the fist settling against the body into a huge speed.
	if (bHasLastHand && !bLanded && DeltaTime > UE_SMALL_NUMBER)
	{
		// Relative to the character, so running does not count as a fast fist.
		const FVector Relative = (Hand - From) / DeltaTime - Character->GetVelocity();
		// A clip strike hits at its tuned speed (see below), and a frame of the clip blending over the
		// last one is no measure of it: that is the speed it reports.
		PeakSpeed = Clip ? MoveSpeed() : FMath::Max(PeakSpeed, float(Relative.Size()));
	}
	PeakReach = FMath::Max(PeakReach, float(FVector::DotProduct(Hand - GetChestLocation(), StrikeAim)));
	LastHandLocation = Hand;
	bHasLastHand = true;

	// Bodies only: a character's capsule is wider than the character, and a punch that passes
	// beside the body inside the capsule has missed.
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CSStrikeSweep), false, Character);
	TArray<FHitResult> Hits;
	// A sweep of no length finds nothing in some cases; a hair's length finds what the fist is in.
	const FVector To = From.Equals(Hand, 0.01f) ? Hand + StrikeAim * 0.1f : Hand;
	World->SweepMultiByObjectType(Hits, From, To, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params);

	// What arrives: the weight behind the fist at the fastest the fist has been, along the aim. Held
	// between half the driven speed and the most the drive can give, so one odd frame (a hitch, a
	// joint snapping) cannot make the hit.
	// A clip strike moves as authored, so it hits at the move's tuned speed (slower when tired): the
	// speed is part of the move, not of how fast the authored hand happens to travel in a frame.
	const FVector Velocity = Clip ? StrikeAim * MoveSpeed()
		: StrikeAim * FMath::Clamp(PeakSpeed, MoveSpeed() * 0.5f, MoveSpeed() + T.PunchLean);
	for (const FHitResult& Hit : Hits)
	{
		AActor* Other = Hit.GetActor();
		UPrimitiveComponent* Component = Hit.GetComponent();
		if (!Other || !Component || Other->GetOwner() == Character || HitActors.Contains(Other))
		{
			continue;
		}
		const FVector Point = Hit.bStartPenetrating ? From : FVector(Hit.ImpactPoint);
		if (ACSCharacter* Victim = Cast<ACSCharacter>(Other))
		{
			if (bLanded || bBlocked || !IsFoe(*Victim))
			{
				continue; // one strike, one character; and not on the attacker's own side
			}
			// A front kick goes to the body (from the upper thigh up): brushing the other's knees, shins
			// or feet on the way up is not it.
			const USkeletalMeshComponent* VictimMesh = Victim->GetMesh();
			if (MoveRow.bLeg && VictimMesh && Point.Z < VictimMesh->GetSocketLocation(TEXT("pelvis")).Z - 35.f)
			{
				continue;
			}
			UCSPhysicsBodyComponent* Physics = Victim->GetPhysicsBody();
			const FName Bone = Component == Victim->GetMesh() ? Hit.BoneName : NAME_None;
			const ECSContactResult Result = Physics
				? Physics->ReceiveContact(Character, Bone, Velocity, MoveMass(), Point - StrikeAim * 30.f, Point, 1.f, true)
				: ECSContactResult::None;
			if (Result == ECSContactResult::Stagger && HeadSnapScale > 0.f && HeadBones.Contains(Bone))
			{
				// The head whips back on the neck: the hit loosened its motors, this gives it the motion.
				if (USkeletalMeshComponent* Struck = Victim->GetMesh())
				{
					FBodyInstance* Head = Struck->GetBodyInstance(Bone);
					if (Head && Head->IsValidBodyInstance() && Head->IsInstanceSimulatingPhysics())
					{
						Head->AddImpulse(Velocity * HeadSnapScale * FMath::Sqrt(MoveRow.MassScale), true);
					}
				}
			}
			if (Result == ECSContactResult::Stagger && !MoveRow.bLeg && BodyRockShare > 0.f && Physics)
			{
				// The upper body rocks back with the punch (no new hit: the impulse goes straight to the body).
				const float Rock = Velocity.Size() * BodyRockShare * FMath::Sqrt(FMath::Max(MoveRow.MassScale, 0.f));
				Physics->ApplyImpulseAtBone(BodyRockBone, StrikeAim.GetSafeNormal2D() * Rock * Physics->GetBodyMass());
			}
			if (Result == ECSContactResult::Stagger || Result == ECSContactResult::Ragdoll)
			{
				bLanded = true;
				HitActors.Add(Other);
				OnStrikeLanded.Broadcast(Character, Victim, Result);
			}
			else if (Result == ECSContactResult::Blocked)
			{
				// Taken on the guard: the punch has not landed, and the arm stops there instead of
				// being driven on through the guard.
				bBlocked = true;
				HitActors.Add(Other);
				OnStrikeLanded.Broadcast(Character, Victim, Result);
				SetPhase(ECSStrikePhase::Recover);
				ReleaseArm();
				if (Clip)
				{
					// Stopped on the guard: the clip goes no further and the arm comes back.
					StopClip(FMath::Max(MoveRecoverSeconds(), 0.15f));
				}
				return;
			}
			else if (Result == ECSContactResult::Ignored)
			{
				// Already down: it took nothing. Not asked again this strike, and the punch goes on.
				HitActors.Add(Other);
			}
		}
		else if (Component->IsSimulatingPhysics())
		{
			HitActors.Add(Other);
			// As for a character: the velocity change the punch's momentum gives this body, with the
			// same ceiling, so a light prop is knocked away and not shot out of the map.
			const FBodyInstance* Struck = Component->GetBodyInstance(Hit.BoneName);
			const float Mass = Struck && Struck->IsValidBodyInstance() ? Struck->GetBodyMass() : Component->GetMass();
			if (Mass > UE_SMALL_NUMBER)
			{
				const FVector Change = (Velocity * (MoveMass() / Mass)).GetClampedToMaxSize(T.ContactImpulseMax);
				Component->AddImpulseAtLocation(Change * Mass, Point, Hit.BoneName);
			}
			OnStrikeLanded.Broadcast(Character, Other, ECSContactResult::None);
		}
	}
}
