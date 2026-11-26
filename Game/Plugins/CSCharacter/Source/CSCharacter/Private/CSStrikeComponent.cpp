#include "CSStrikeComponent.h"

#include "CSCharacter.h"
#include "CSCharacterLog.h"
#include "CSMovementTuning.h"
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
	return WindupSecondsOverride >= 0.f ? WindupSecondsOverride : GetTuning().PunchWindupSeconds;
}

bool UCSStrikeComponent::StartPunch()
{
	if (Phase != ECSStrikePhase::None || !CanStrike())
	{
		return false;
	}
	bRightHand = !bRightHand;
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
	bHasLastHand = false;
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

void UCSStrikeComponent::ReleaseArm()
{
	// The fist was driven fast; let go at that speed, the arm swings on past the target and up over
	// the head. It is let go moving with the body instead, and the motors bring it back.
	const ACSCharacter* Character = GetCharacter();
	USkeletalMeshComponent* Body = GetBody();
	if (!Character || !Body)
	{
		return;
	}
	const int32 Index = HandIndex();
	for (const FName Bone : {HandBones[Index], ForearmBones[Index]})
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
	SetPhase(ECSStrikePhase::None);
}

void UCSStrikeComponent::SetPhase(ECSStrikePhase NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.f;
	SetComponentTickEnabled(NewPhase != ECSStrikePhase::None);
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
	if (To.Size() <= T.PunchReach)
	{
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
	return Body ? Body->GetSocketLocation(HandBones[StrikeCount > 0 ? HandIndex() : 0]) : FVector::ZeroVector;
}

FVector UCSStrikeComponent::GetFistLocation() const
{
	const USkeletalMeshComponent* Body = GetBody();
	if (!Body)
	{
		return FVector::ZeroVector;
	}
	const int32 Index = StrikeCount > 0 ? HandIndex() : 0;
	const FVector Hand = Body->GetSocketLocation(HandBones[Index]);
	const FVector Along = (Hand - Body->GetSocketLocation(ForearmBones[Index])).GetSafeNormal();
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
	const UCSMovementTuning& T = GetTuning();
	if (Phase == ECSStrikePhase::Strike)
	{
		// The path the fist travelled in the frame just simulated, before the phase can end: the
		// last frame of a strike is the one at full reach.
		Sweep();
	}
	PhaseSeconds += DeltaTime;
	if (Phase == ECSStrikePhase::Windup && PhaseSeconds >= GetWindupSeconds())
	{
		BeginStrike();
	}
	else if (Phase == ECSStrikePhase::Strike && PhaseSeconds >= T.PunchStrikeSeconds)
	{
		SetPhase(ECSStrikePhase::Recover);
		ReleaseArm();
	}
	else if (Phase == ECSStrikePhase::Recover && PhaseSeconds >= T.PunchRecoverSeconds)
	{
		SetPhase(ECSStrikePhase::None);
		return;
	}
	if (Phase == ECSStrikePhase::Windup || Phase == ECSStrikePhase::Strike)
	{
		// A wind-up drawn out longer than the punch's own (an opponent's tell) stands still and only
		// steps in for the last PunchWindupSeconds of it: a step held through the whole tell would
		// carry the body into the target and around its side.
		const bool bLateEnough = Phase == ECSStrikePhase::Strike || GetWindupSeconds() - PhaseSeconds <= T.PunchWindupSeconds;
		if (bSteppingIn && bLateEnough)
		{
			// Every frame until in reach: ground friction would stop a single push within the punch.
			StepIn();
		}
		Drive(DeltaTime);
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
	const int32 Index = HandIndex();
	FBodyInstance* Fist = Body->GetBodyInstance(HandBones[Index]);
	if (!Fist || !Fist->IsValidBodyInstance() || !Fist->IsInstanceSimulatingPhysics())
	{
		// A physics asset with no hand body: the forearm carries the fist.
		Fist = Body->GetBodyInstance(ForearmBones[Index]);
	}
	if (!Fist || !Fist->IsValidBodyInstance() || !Fist->IsInstanceSimulatingPhysics())
	{
		return;
	}
	const UCSMovementTuning& T = GetTuning();
	const FVector Shoulder = Body->GetSocketLocation(ShoulderBones[Index]);
	const FVector Hand = Body->GetSocketLocation(HandBones[Index]);
	const FVector HandGoal = ComputeHandTarget(Phase, Shoulder, GetChestLocation(), StrikeAim, T.PunchReach);
	// The draw back is unhurried; only the strike is at full speed.
	const float Speed = Phase == ECSStrikePhase::Strike ? T.PunchSpeed : T.PunchSpeed * 0.5f;
	Fist->SetLinearVelocity(ComputeDriveVelocity(Hand, HandGoal, DeltaTime, Speed, Character->GetVelocity()), false);

	// The lean: during the strike the chest moves along the aim at PunchLean or faster. Only what is
	// missing is added, so a chest already going that way (a hit from behind, a run) is left alone.
	FBodyInstance* Chest = Phase == ECSStrikePhase::Strike && T.PunchLean > 0.f ? Body->GetBodyInstance(ChestBone) : nullptr;
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
	const float Plausible = (T.PunchSpeed * 2.f + float(Character->GetVelocity().Size())) * DeltaTime + FistRadius + 30.f;
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
		PeakSpeed = FMath::Max(PeakSpeed, float(Relative.Size()));
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
	World->SweepMultiByObjectType(Hits, From, To, FQuat::Identity, Objects, FCollisionShape::MakeSphere(FistRadius), Params);

	// What arrives: the weight behind the fist at the fastest the fist has been, along the aim. Held
	// between half the driven speed and the most the drive can give, so one odd frame (a hitch, a
	// joint snapping) cannot make the hit.
	const FVector Velocity = StrikeAim * FMath::Clamp(PeakSpeed, T.PunchSpeed * 0.5f, T.PunchSpeed + T.PunchLean);
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
			UCSPhysicsBodyComponent* Physics = Victim->GetPhysicsBody();
			const FName Bone = Component == Victim->GetMesh() ? Hit.BoneName : NAME_None;
			const ECSContactResult Result = Physics
				? Physics->ReceiveContact(Character, Bone, Velocity, T.PunchMass, Point - StrikeAim * 30.f, Point, 1.f, true)
				: ECSContactResult::None;
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
				const FVector Change = (Velocity * (T.PunchMass / Mass)).GetClampedToMaxSize(T.ContactImpulseMax);
				Component->AddImpulseAtLocation(Change * Mass, Point, Hit.BoneName);
			}
			OnStrikeLanded.Broadcast(Character, Other, ECSContactResult::None);
		}
	}
}
