#include "CSStrikeComponent.h"

#include "CSCharacter.h"
#include "CSCharacterLog.h"
#include "CSMovementTuning.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
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

bool UCSStrikeComponent::StartPunch()
{
	if (Phase != ECSStrikePhase::None || !CanStrike())
	{
		return false;
	}
	bRightHand = !bRightHand;
	++StrikeCount;
	bLanded = false;
	PeakReach = 0.f;
	PeakSpeed = 0.f;
	HitActors.Reset();
	StrikeAim = GetAimDirection();
	bHasLastHand = false;
	SetPhase(GetTuning().PunchWindupSeconds > 0.f ? ECSStrikePhase::Windup : ECSStrikePhase::Strike);
	OnStrikeStarted.Broadcast(GetCharacter());
	return true;
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

FVector UCSStrikeComponent::GetAimDirection() const
{
	const ACSCharacter* Character = GetCharacter();
	if (!Character)
	{
		return FVector::ForwardVector;
	}
	FRotator Aim = Character->GetBaseAimRotation();
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

FVector UCSStrikeComponent::ComputeDriveVelocity(const FVector& Hand, const FVector& Target, float DeltaTime, float MaxSpeed, const FVector& BaseVelocity)
{
	if (DeltaTime <= UE_SMALL_NUMBER)
	{
		return BaseVelocity;
	}
	return BaseVelocity + ((Target - Hand) / DeltaTime).GetClampedToMaxSize(FMath::Max(MaxSpeed, 0.f));
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
	if (Phase == ECSStrikePhase::Windup && PhaseSeconds >= T.PunchWindupSeconds)
	{
		SetPhase(ECSStrikePhase::Strike);
	}
	else if (Phase == ECSStrikePhase::Strike && PhaseSeconds >= T.PunchStrikeSeconds)
	{
		SetPhase(ECSStrikePhase::Recover);
	}
	else if (Phase == ECSStrikePhase::Recover && PhaseSeconds >= T.PunchRecoverSeconds)
	{
		SetPhase(ECSStrikePhase::None);
		return;
	}
	if (Phase == ECSStrikePhase::Windup || Phase == ECSStrikePhase::Strike)
	{
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
	const FVector Target = ComputeHandTarget(Phase, Shoulder, GetChestLocation(), StrikeAim, T.PunchReach);
	// The draw back is unhurried; only the strike is at full speed.
	const float Speed = Phase == ECSStrikePhase::Strike ? T.PunchSpeed : T.PunchSpeed * 0.5f;
	Fist->SetLinearVelocity(ComputeDriveVelocity(Hand, Target, DeltaTime, Speed, Character->GetVelocity()), false);

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
	if (bHasLastHand && DeltaTime > UE_SMALL_NUMBER)
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
			if (bLanded)
			{
				continue; // one strike, one character
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
