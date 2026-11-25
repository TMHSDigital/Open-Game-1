#include "CSPhysicsBodyComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CSCharacter.h"
#include "CSCharacterLog.h"
#include "CSDevTools.h"
#include "CSHitBall.h"
#include "CSMovementTuning.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace
{
	// Height above the floor the recovered capsule is placed at, cm; the movement component's floor
	// check then settles it (it keeps the capsule 1.9 to 2.4 cm above the floor while walking).
	constexpr float StandFloorGap = 2.f;
	// The recovery search looks this far above the pelvis for the start of its downward sweep, and
	// this far below it for ground, cm (on top of the capsule's half height).
	constexpr float StandSweepAbove = 60.f;
	constexpr float StandSweepBelow = 250.f;
	// The capsule follows the pelvis onto ground at most this far below it, cm (on top of the half height).
	constexpr float FollowFloorReach = 100.f;
	// The ragdoll counts as on the ground (ground damping) with the pelvis this close above the floor, cm.
	constexpr float GroundContactDistance = 35.f;

	FPhysicalAnimationData Scaled(const FPhysicalAnimationData& D, float Scale)
	{
		FPhysicalAnimationData Out = D;
		Out.OrientationStrength *= Scale;
		Out.AngularVelocityStrength *= Scale;
		Out.PositionStrength *= Scale;
		Out.VelocityStrength *= Scale;
		// A force limit of 0 means no limit; scaling keeps it at 0.
		Out.MaxLinearForce *= Scale;
		Out.MaxAngularForce *= Scale;
		return Out;
	}

	FPhysicalAnimationData Blend(const FPhysicalAnimationData& A, const FPhysicalAnimationData& B, float Weight)
	{
		FPhysicalAnimationData Out = Weight < 0.5f ? A : B;
		Out.OrientationStrength = FMath::Lerp(A.OrientationStrength, B.OrientationStrength, Weight);
		Out.AngularVelocityStrength = FMath::Lerp(A.AngularVelocityStrength, B.AngularVelocityStrength, Weight);
		Out.PositionStrength = FMath::Lerp(A.PositionStrength, B.PositionStrength, Weight);
		Out.VelocityStrength = FMath::Lerp(A.VelocityStrength, B.VelocityStrength, Weight);
		Out.MaxLinearForce = FMath::Lerp(A.MaxLinearForce, B.MaxLinearForce, Weight);
		Out.MaxAngularForce = FMath::Lerp(A.MaxAngularForce, B.MaxAngularForce, Weight);
		return Out;
	}

	// 1 while the hit holds, then a linear ramp to 0.
	float HitWeight(float Age, const UCSMovementTuning& T)
	{
		if (Age <= T.HitHoldSeconds)
		{
			return 1.f;
		}
		return FMath::Clamp(1.f - (Age - T.HitHoldSeconds) / T.HitRecoverSeconds, 0.f, 1.f);
	}

	FName BodyName(const FBodyInstance* B)
	{
		return B && B->BodySetup.IsValid() ? B->BodySetup->BoneName : NAME_None;
	}
}

UCSPhysicsBodyComponent::UCSPhysicsBodyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Before the physics step: the capsule follows where the pelvis ended the last step, and a held
	// recovery pose is put back before the step runs. The physical animation component runs after
	// this (see BeginPlay), so new motor strengths apply in the same frame.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UCSPhysicsBodyComponent::Setup(USkeletalMeshComponent* InBody, UPhysicalAnimationComponent* InPhysicalAnimation)
{
	Body = InBody;
	PhysicalAnimation = InPhysicalAnimation;
}

ACSCharacter* UCSPhysicsBodyComponent::GetCharacter() const
{
	return Cast<ACSCharacter>(GetOwner());
}

const UCSMovementTuning& UCSPhysicsBodyComponent::GetTuning() const
{
	const ACSCharacter* Character = GetCharacter();
	return Character ? Character->GetActiveTuning() : *GetDefault<UCSMovementTuning>();
}

bool UCSPhysicsBodyComponent::HasPhysicsBody() const
{
	return !bStartPending && Body && Body->GetPhysicsAsset() && Body->Bodies.Num() > 0 && Body->GetBodyInstance(PelvisBone) != nullptr;
}

float UCSPhysicsBodyComponent::GetBodyMass() const
{
	float Mass = 0.f;
	if (Body)
	{
		for (const FBodyInstance* B : Body->Bodies)
		{
			if (B && B->IsValidBodyInstance())
			{
				Mass += B->GetBodyMass();
			}
		}
	}
	return Mass;
}

void UCSPhysicsBodyComponent::BeginPlay()
{
	Super::BeginPlay();
	ACSCharacter* Character = GetCharacter();
	if (Character)
	{
		Character->LandedDelegate.AddUniqueDynamic(this, &UCSPhysicsBodyComponent::HandleLanded);
		if (UCSDevTools* DevTools = Character->GetDevTools())
		{
			DevTools->OnRagdollRequested.AddUObject(this, &UCSPhysicsBodyComponent::HandleRagdollRequested);
			DevTools->OnPushRequested.AddUObject(this, &UCSPhysicsBodyComponent::HandlePushRequested);
		}
	}
	if (!HasPhysicsBody())
	{
		UE_LOG(LogCSCharacter, Warning, TEXT("%s: the mesh has no physics asset with a %s body; the physics body (sway, stagger, ragdoll) is off."),
			*GetNameSafe(GetOwner()), *PelvisBone.ToString());
		bWarnedNoBody = true;
		SetComponentTickEnabled(false);
		return;
	}
	const UWorld* World = GetWorld();
	if (World && World->bInTick)
	{
		// Spawned in the middle of a frame: wait for the next one (see bStartPending).
		bStartPending = true;
		StartPendingSince = World->GetTimeSeconds();
		return;
	}
	StartBody();
}

void UCSPhysicsBodyComponent::StartBody()
{
	if (PhysicalAnimation)
	{
		PhysicalAnimation->SetSkeletalMeshComponent(Body);
		PhysicalAnimation->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
	}
	// Physics collision for the simulated bodies, ignoring pawns, so the bodies never push the capsule.
	Body->SetCollisionProfileName(RagdollCollisionProfile);
	// The simulation must not move the mesh component (the engine default moves it with a simulated
	// root body): the mesh stays on the capsule, the capsule follows the pelvis, and the bodies stay
	// in world space, so the pose is always read in the capsule's frame and the recovery blend ends
	// in the capsule's upright frame.
	Body->PhysicsTransformUpdateMode = EPhysicsTransformUpdateMode::ComponentTransformIsKinematic;
	MeshRelativeTransform = Body->GetRelativeTransform();
	// Contact hits: the bodies and the capsule report what touches them. A skeletal mesh's bodies
	// only report when the component's own flag is on as well as theirs.
	Body->BodyInstance.bNotifyRigidBodyCollision = true;
	Body->SetNotifyRigidBodyCollision(true);
	Body->OnComponentHit.AddUniqueDynamic(this, &UCSPhysicsBodyComponent::HandleBodyHit);
	if (const ACSCharacter* Owner = GetCharacter())
	{
		if (UCapsuleComponent* Capsule = Owner->GetCapsuleComponent())
		{
			Capsule->SetNotifyRigidBodyCollision(true);
			Capsule->OnComponentHit.AddUniqueDynamic(this, &UCSPhysicsBodyComponent::HandleCapsuleHit);
		}
	}
	BaseDamping.SetNum(Body->Bodies.Num());
	for (int32 i = 0; i < Body->Bodies.Num(); ++i)
	{
		const FBodyInstance* B = Body->Bodies[i];
		BaseDamping[i] = B ? FVector2f(B->LinearDamping, B->AngularDamping) : FVector2f::ZeroVector;
	}
	EnterNormal();
}

void UCSPhysicsBodyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACSCharacter* Character = GetCharacter())
	{
		Character->LandedDelegate.RemoveDynamic(this, &UCSPhysicsBodyComponent::HandleLanded);
		if (UCSDevTools* DevTools = Character->GetDevTools())
		{
			DevTools->OnRagdollRequested.RemoveAll(this);
			DevTools->OnPushRequested.RemoveAll(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UCSPhysicsBodyComponent::SetState(ECSBodyState NewState)
{
	if (NewState == State)
	{
		return;
	}
	const ECSBodyState Old = State;
	State = NewState;
	StateSeconds = 0.f;
	OnStateChanged.Broadcast(Old, NewState);
}

bool UCSPhysicsBodyComponent::IsInChain(FName Bone, FName Root) const
{
	return Bone == Root || (Body && Body->BoneIsChildOf(Bone, Root));
}

FName UCSPhysicsBodyComponent::FindBodyBone(FName Bone) const
{
	if (!Body)
	{
		return NAME_None;
	}
	for (FName Name = Bone; Name != NAME_None; Name = Body->GetParentBone(Name))
	{
		if (Body->GetBodyInstance(Name))
		{
			return Name;
		}
	}
	return NAME_None;
}

FName UCSPhysicsBodyComponent::GetChainRoot(FName Bone) const
{
	const FName BodyBone = FindBodyBone(Bone);
	for (const FName Root : LimbChainRoots)
	{
		if (BodyBone != NAME_None && IsInChain(BodyBone, Root))
		{
			return Root;
		}
	}
	return SimulatedRootBone;
}

float UCSPhysicsBodyComponent::GetHitWeight(FName Bone) const
{
	const float* Age = HitAges.Find(FindBodyBone(Bone));
	return Age ? HitWeight(*Age, GetTuning()) : 0.f;
}

FPhysicalAnimationData UCSPhysicsBodyComponent::NormalDataFor(FName Bone) const
{
	const UCSMovementTuning& T = GetTuning();
	for (const FName Root : LimbChainRoots)
	{
		if (IsInChain(Bone, Root))
		{
			return Scaled(T.NormalProfile, T.NormalLimbStrengthScale);
		}
	}
	return T.NormalProfile;
}

void UCSPhysicsBodyComponent::ApplyMotors()
{
	const UCSMovementTuning& T = GetTuning();
	MotorTuning = &T;
	if (!PhysicalAnimation || !Body)
	{
		return;
	}
	for (const FBodyInstance* B : Body->Bodies)
	{
		const FName Name = BodyName(B);
		if (Name == NAME_None || !IsInChain(Name, SimulatedRootBone))
		{
			continue;
		}
		FPhysicalAnimationData Data = NormalDataFor(Name);
		if (const float* Age = HitAges.Find(Name))
		{
			Data = Blend(Data, T.HitProfile, HitWeight(*Age, T));
		}
		PhysicalAnimation->ApplyPhysicalAnimationSettings(Name, Data);
	}
}

void UCSPhysicsBodyComponent::EnterNormal()
{
	// Pelvis and legs animated (kinematic); the upper body simulated and driven to the pose.
	Body->SetAllBodiesSimulatePhysics(false);
	Body->SetEnableGravity(true);
	Body->SetAllBodiesBelowSimulatePhysics(SimulatedRootBone, true, true);
	Body->SetAllBodiesPhysicsBlendWeight(0.f);
	Body->SetAllBodiesBelowPhysicsBlendWeight(SimulatedRootBone, 1.f, false, true);
	if (PhysicalAnimation)
	{
		PhysicalAnimation->SetStrengthMultiplyer(1.f);
	}
	HitAges.Reset();
	ApplyMotors();
}

void UCSPhysicsBodyComponent::ApplyImpulseAtBone(FName Bone, FVector Impulse)
{
	if (!HasPhysicsBody())
	{
		if (!bWarnedNoBody)
		{
			UE_LOG(LogCSCharacter, Warning, TEXT("%s: ApplyImpulseAtBone ignored: no physics body."), *GetNameSafe(GetOwner()));
			bWarnedNoBody = true;
		}
		return;
	}
	const float Mass = GetBodyMass();
	if (Mass <= KINDA_SMALL_NUMBER || Impulse.IsNearlyZero())
	{
		return;
	}
	const UCSMovementTuning& T = GetTuning();
	// The size of a hit is the velocity change it would give the whole character (the same definition the movement tuning uses).
	const FVector VelocityChange = Impulse / Mass;
	const bool bRagdolls = VelocityChange.Size() >= T.RagdollImpulseThreshold;
	FName BodyBone = FindBodyBone(Bone);
	if (BodyBone == NAME_None)
	{
		BodyBone = PelvisBone;
	}

	if (State == ECSBodyState::Recovering && !bRagdolls)
	{
		return;
	}
	if (State == ECSBodyState::Ragdoll || bRagdolls)
	{
		EnterRagdoll();
		// The whole body takes the velocity change and the hit body takes it again, so the body
		// turns about the point that was hit.
		Body->SetAllPhysicsLinearVelocity(VelocityChange, true);
		Body->AddImpulse(VelocityChange, BodyBone, true);
		return;
	}
	Stagger(BodyBone, VelocityChange);
}

void UCSPhysicsBodyComponent::Stagger(FName BodyBone, const FVector& VelocityChange)
{
	const UCSMovementTuning& T = GetTuning();
	// Legs and pelvis are animated; a hit there staggers the upper body.
	const FName Root = IsInChain(BodyBone, SimulatedRootBone) ? GetChainRoot(BodyBone) : SimulatedRootBone;
	for (FBodyInstance* B : Body->Bodies)
	{
		const FName Name = BodyName(B);
		if (Name != NAME_None && IsInChain(Name, Root))
		{
			HitAges.Add(Name, 0.f);
			B->AddImpulse(VelocityChange, true);
		}
	}
	SetState(ECSBodyState::Hit);
	ApplyMotors();

	// The feet take a share of the shove along the ground; nothing vertical, so the character stays grounded.
	const ACSCharacter* Character = GetCharacter();
	if (UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr)
	{
		const FVector Along = FVector(VelocityChange.X, VelocityChange.Y, 0.f) * T.HitCapsuleVelocityShare;
		if (!Along.IsNearlyZero() && Move->IsMovingOnGround())
		{
			Move->Velocity += Along;
		}
	}
}

void UCSPhysicsBodyComponent::EnterRagdoll()
{
	if (!HasPhysicsBody())
	{
		if (!bWarnedNoBody)
		{
			UE_LOG(LogCSCharacter, Warning, TEXT("%s: EnterRagdoll ignored: no physics body."), *GetNameSafe(GetOwner()));
			bWarnedNoBody = true;
		}
		return;
	}
	if (State == ECSBodyState::Ragdoll)
	{
		return;
	}
	ACSCharacter* Character = GetCharacter();
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	const bool bFromRecovery = State == ECSBodyState::Recovering;
	const FVector Velocity = Move && !bFromRecovery ? Move->Velocity : FVector::ZeroVector;
	if (!bFromRecovery)
	{
		// From Recovering these were saved by the ragdoll before and the mesh is back in place.
		MeshRelativeTransform = Body->GetRelativeTransform();
		CapsuleCollision = Capsule ? Capsule->GetCollisionEnabled() : ECollisionEnabled::QueryAndPhysics;
		// Where the character stood is the first fallback spot for the recovery.
		bHasClearStand = Move && Move->IsMovingOnGround() && Move->CurrentFloor.IsWalkableFloor();
		LastClearStand = Character ? Character->GetActorLocation() : FVector::ZeroVector;
	}

	HitAges.Reset();
	Snapshot.Reset();
	Body->SetAllBodiesSimulatePhysics(true);
	Body->SetEnableGravity(true);
	Body->SetAllBodiesPhysicsBlendWeight(1.f);
	if (PhysicalAnimation)
	{
		PhysicalAnimation->SetStrengthMultiplyer(0.f);
	}
	bRagdollOnGround = false;
	ApplyRagdollPhysics();
	Body->WakeAllRigidBodies();
	// The body carries on with the character's motion (a run, a fall).
	Body->SetAllPhysicsLinearVelocity(Velocity, false);
	if (bFromRecovery)
	{
		Body->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
	}

	if (Move)
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
	if (Capsule)
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	SettledSeconds = 0.f;
	SetState(ECSBodyState::Ragdoll);
	if (Character)
	{
		Character->SetRagdolled(true);
	}
}

void UCSPhysicsBodyComponent::ApplyRagdollPhysics()
{
	const UCSMovementTuning& T = GetTuning();
	RagdollTuning = &T;
	// A physics material's values are copied into the physics engine when it is first used, so a new
	// friction gets a new material.
	if (!RagdollMaterial || RagdollMaterial->Friction != T.RagdollFriction)
	{
		RagdollMaterial = NewObject<UPhysicalMaterial>(this, NAME_None, RF_Transient);
		RagdollMaterial->Friction = T.RagdollFriction;
		RagdollMaterial->StaticFriction = T.RagdollFriction;
		// The larger of the body's and the ground's friction, not their average, so the value set
		// here is the friction the body slides with.
		RagdollMaterial->bOverrideFrictionCombineMode = true;
		RagdollMaterial->FrictionCombineMode = EFrictionCombineMode::Max;
	}
	ApplyRagdollDamping();
	Body->SetPhysMaterialOverride(RagdollMaterial);
}

void UCSPhysicsBodyComponent::ApplyRagdollDamping()
{
	// Linear damping only on the ground: in the air the body keeps the bodies' own (near zero)
	// damping, so a fall is a real fall; on the ground RagdollLinearDamping slows the slide.
	const UCSMovementTuning& T = GetTuning();
	for (int32 i = 0; i < Body->Bodies.Num(); ++i)
	{
		if (FBodyInstance* B = Body->Bodies[i]; B && B->IsValidBodyInstance())
		{
			B->LinearDamping = bRagdollOnGround ? T.RagdollLinearDamping : (BaseDamping.IsValidIndex(i) ? BaseDamping[i].X : 0.f);
			B->AngularDamping = T.RagdollAngularDamping;
			B->UpdateDampingProperties();
		}
	}
}

void UCSPhysicsBodyComponent::RestoreBodyPhysics()
{
	RagdollTuning = nullptr;
	for (int32 i = 0; i < Body->Bodies.Num() && i < BaseDamping.Num(); ++i)
	{
		if (FBodyInstance* B = Body->Bodies[i]; B && B->IsValidBodyInstance())
		{
			B->LinearDamping = BaseDamping[i].X;
			B->AngularDamping = BaseDamping[i].Y;
			B->UpdateDampingProperties();
		}
	}
	Body->SetPhysMaterialOverride(nullptr);
}

void UCSPhysicsBodyComponent::Recover()
{
	if (State == ECSBodyState::Ragdoll)
	{
		BeginRecovery();
	}
}

void UCSPhysicsBodyComponent::ToggleRagdoll()
{
	switch (State)
	{
	case ECSBodyState::Normal:
	case ECSBodyState::Hit:
		EnterRagdoll();
		break;
	case ECSBodyState::Ragdoll:
		Recover();
		break;
	case ECSBodyState::Recovering:
	default:
		break;
	}
}

void UCSPhysicsBodyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bStartPending)
	{
		const UWorld* World = GetWorld();
		if (!World || World->GetTimeSeconds() <= StartPendingSince)
		{
			return; // still the frame the character was spawned in
		}
		bStartPending = false;
		if (!HasPhysicsBody())
		{
			return;
		}
		StartBody();
	}
	if (!HasPhysicsBody())
	{
		return;
	}
	TrackNearbyBodies();
	StateSeconds += DeltaTime;
	switch (State)
	{
	case ECSBodyState::Normal:
		// A tuning reload makes a new tuning object: rewrite the motors from it.
		if (MotorTuning.Get() != &GetTuning())
		{
			ApplyMotors();
		}
		break;
	case ECSBodyState::Hit:
		TickHit(DeltaTime);
		break;
	case ECSBodyState::Ragdoll:
		TickRagdoll(DeltaTime);
		break;
	case ECSBodyState::Recovering:
		TickRecovering(DeltaTime);
		break;
	}
}

void UCSPhysicsBodyComponent::TickHit(float DeltaTime)
{
	const UCSMovementTuning& T = GetTuning();
	for (TPair<FName, float>& Pair : HitAges)
	{
		Pair.Value += DeltaTime;
	}
	// Written with this frame's weights; a finished chain gets its Normal strengths here.
	ApplyMotors();
	const float Done = T.HitHoldSeconds + T.HitRecoverSeconds;
	for (auto It = HitAges.CreateIterator(); It; ++It)
	{
		if (It.Value() >= Done - KINDA_SMALL_NUMBER)
		{
			It.RemoveCurrent();
		}
	}
	if (HitAges.IsEmpty())
	{
		SetState(ECSBodyState::Normal);
	}
}

void UCSPhysicsBodyComponent::FollowPelvis()
{
	ACSCharacter* Character = GetCharacter();
	const FBodyInstance* Pelvis = Body->GetBodyInstance(PelvisBone);
	if (!Character || !Pelvis)
	{
		return;
	}
	const FVector At = Pelvis->GetUnrealWorldTransform().GetLocation();
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Target = At;
	// On the ground below the pelvis when there is ground close below it, so the capsule (and the
	// camera, which pivots on it) stays at standing height over a body lying on the floor.
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CSRagdollFollow), false, Character);
	bool bOnGround = false;
	if (GetWorld()->LineTraceSingleByChannel(Hit, At, At - FVector(0.f, 0.f, HalfHeight + FollowFloorReach), ECC_Pawn, Params) && !Hit.bStartPenetrating)
	{
		Target.Z = FMath::Max(At.Z, Hit.ImpactPoint.Z + HalfHeight);
		bOnGround = At.Z - Hit.ImpactPoint.Z <= GroundContactDistance;
	}
	if (bOnGround != bRagdollOnGround)
	{
		// Damping changes only when ground contact changes.
		bRagdollOnGround = bOnGround;
		ApplyRagdollDamping();
	}
	// No teleport: the simulated bodies are in world space and stay where physics put them.
	Character->SetActorLocation(Target, false, nullptr, ETeleportType::None);

	// Remember the last spot passed where the capsule could stand (the recovery's fallback).
	FVector Clear;
	if (TryStandAt(At, At.Z, Clear))
	{
		LastClearStand = Clear;
		bHasClearStand = true;
	}
}

void UCSPhysicsBodyComponent::TickRagdoll(float DeltaTime)
{
	const UCSMovementTuning& T = GetTuning();
	if (RagdollTuning.Get() != &T)
	{
		// A reload while ragdolled: new damping and friction at once.
		ApplyRagdollPhysics();
	}
	FollowPelvis();
	const float Speed = Body->GetPhysicsLinearVelocity(PelvisBone).Size();
	SettledSeconds = Speed < T.RagdollSettleSpeed ? SettledSeconds + DeltaTime : 0.f;
	if ((SettledSeconds >= T.RagdollSettleSeconds || StateSeconds >= T.RagdollMaxSeconds) && !BeginRecovery())
	{
		// Nowhere to put the capsule yet: stay down and try again after another settle time.
		SettledSeconds = 0.f;
		StateSeconds = FMath::Min(StateSeconds, T.RagdollMaxSeconds - T.RagdollSettleSeconds);
	}
}

float UCSPhysicsBodyComponent::GetUpYaw() const
{
	const AActor* Owner = GetOwner();
	const float Current = Owner ? Owner->GetActorRotation().Yaw : 0.f;
	const FBodyInstance* Pelvis = Body->GetBodyInstance(PelvisBone);
	const USkeletalMesh* Mesh = Cast<USkeletalMesh>(Body->GetSkinnedAsset());
	if (!Pelvis || !Mesh)
	{
		return Current;
	}
	// The pelvis's own front and head-ward axes, from the reference pose: the mesh faces +Y with +Z up.
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	FTransform RestCS = FTransform::Identity;
	for (int32 B = Ref.FindBoneIndex(PelvisBone); B != INDEX_NONE; B = Ref.GetParentIndex(B))
	{
		RestCS = RestCS * Ref.GetRefBonePose()[B];
	}
	const FQuat Now = Pelvis->GetUnrealWorldTransform().GetRotation() * RestCS.GetRotation().Inverse();
	const FVector Front = Now.RotateVector(FVector(0.f, 1.f, 0.f));
	const FVector HeadWard = Now.RotateVector(FVector(0.f, 0.f, 1.f));
	// On the side: face the way the body faces. On the back: sit up toward the feet. On the front:
	// push up and face toward the head.
	FVector Facing = FMath::Abs(Front.Z) < 0.7f ? Front : (Front.Z > 0.f ? -HeadWard : HeadWard);
	Facing.Z = 0.f;
	return Facing.Normalize(0.01f) ? Facing.Rotation().Yaw : Current;
}

bool UCSPhysicsBodyComponent::FindStandLocation(const FVector& Around, FVector& OutLocation) const
{
	const ACSCharacter* Character = GetCharacter();
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		return false;
	}
	const float Radius = Capsule->GetScaledCapsuleRadius();
	// Rings outward from the pelvis, one capsule radius apart; the first candidate where the capsule
	// stands on walkable ground and fits wins.
	const int32 Rings = FMath::Max(0, FMath::CeilToInt(RecoverySearchRadius / Radius));
	for (int32 Ring = 0; Ring <= Rings; ++Ring)
	{
		const float Distance = FMath::Min(Ring * Radius, RecoverySearchRadius);
		const int32 Spokes = Ring == 0 ? 1 : 8;
		for (int32 Spoke = 0; Spoke < Spokes; ++Spoke)
		{
			const float Angle = 2.f * PI * Spoke / Spokes;
			if (TryStandAt(Around + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Distance, Around.Z, OutLocation))
			{
				return true;
			}
		}
	}
	return false;
}

bool UCSPhysicsBodyComponent::TryStandAt(const FVector& XY, float AroundZ, FVector& OutLocation) const
{
	const ACSCharacter* Character = GetCharacter();
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		return false;
	}
	const UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	const float WalkableZ = Move ? Move->GetWalkableFloorZ() : 0.71f;
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), HalfHeight);
	const ECollisionChannel Channel = Capsule->GetCollisionObjectType();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CSRagdollRecovery), false, Character);
	const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
	UWorld* World = GetWorld();
	// A capsule swept down from above; it must not start inside anything, must land on walkable
	// ground and must fit where it lands.
	const FVector Top(XY.X, XY.Y, AroundZ + HalfHeight + StandSweepAbove);
	const FVector Bottom(XY.X, XY.Y, AroundZ - HalfHeight - StandSweepBelow);
	FHitResult Hit;
	if (!World->SweepSingleByChannel(Hit, Top, Bottom, FQuat::Identity, Channel, Shape, Params, Responses)
		|| Hit.bStartPenetrating || Hit.ImpactNormal.Z < WalkableZ)
	{
		return false;
	}
	const FVector Candidate = Hit.Location + FVector(0.f, 0.f, StandFloorGap);
	if (World->OverlapBlockingTestByChannel(Candidate, FQuat::Identity, Channel, Shape, Params, Responses))
	{
		return false;
	}
	OutLocation = Candidate;
	return true;
}

void UCSPhysicsBodyComponent::HoldSnapshot()
{
	for (int32 i = 0; i < Body->Bodies.Num() && i < Snapshot.Num(); ++i)
	{
		FBodyInstance* B = Body->Bodies[i];
		if (B && B->IsValidBodyInstance() && B->IsInstanceSimulatingPhysics())
		{
			B->SetBodyTransform(Snapshot[i], ETeleportType::TeleportPhysics);
			B->SetLinearVelocity(FVector::ZeroVector, false);
			B->SetAngularVelocityInRadians(FVector::ZeroVector, false);
		}
	}
}

bool UCSPhysicsBodyComponent::BeginRecovery()
{
	ACSCharacter* Character = GetCharacter();
	if (!Character)
	{
		return false;
	}
	// Where to stand, first: near the pelvis on walkable ground; else the last standable spot the
	// ragdoll passed (a steep slope); else clear of geometry above the pelvis, to fall from there.
	const FVector PelvisAt = Body->GetBodyInstance(PelvisBone)->GetUnrealWorldTransform().GetLocation();
	const float Yaw = GetUpYaw();
	FVector Stand;
	FVector Retry;
	bStandFound = FindStandLocation(PelvisAt, Stand);
	if (!bStandFound && bHasClearStand && TryStandAt(LastClearStand, LastClearStand.Z, Retry))
	{
		Stand = Retry;
		bStandFound = true;
		UE_LOG(LogCSCharacter, Log, TEXT("%s: no walkable ground within %.0f cm of the ragdoll; standing up at the last clear spot %s."),
			*Character->GetName(), RecoverySearchRadius, *Stand.ToString());
	}
	if (!bStandFound)
	{
		UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		Stand = PelvisAt + FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight());
		FRotator Rotation(0.f, Yaw, 0.f);
		// FindTeleportSpot only sees geometry for a capsule with query collision, and the ragdoll's
		// capsule has none: switch queries on around the call. Then check the spot it gives.
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		const bool bMoved = GetWorld()->FindTeleportSpot(Character, Stand, Rotation);
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CSRagdollRecovery), false, Character);
		const bool bBlocked = GetWorld()->OverlapBlockingTestByChannel(Stand, FQuat::Identity, Capsule->GetCollisionObjectType(),
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params,
			FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
		if (!bMoved || bBlocked)
		{
			UE_LOG(LogCSCharacter, Warning, TEXT("%s: nowhere clear to stand up near %s; staying down and trying again."),
				*Character->GetName(), *PelvisAt.ToString());
			return false;
		}
		UE_LOG(LogCSCharacter, Log, TEXT("%s: no standable spot found; recovering in the air at %s."), *Character->GetName(), *Stand.ToString());
	}

	// Snapshot the ragdoll pose: the bodies stay there (no gravity, no velocity) while the blend runs.
	Snapshot.SetNum(Body->Bodies.Num());
	for (int32 i = 0; i < Body->Bodies.Num(); ++i)
	{
		const FBodyInstance* B = Body->Bodies[i];
		Snapshot[i] = B && B->IsValidBodyInstance() ? B->GetUnrealWorldTransform() : FTransform::Identity;
	}
	Body->SetEnableGravity(false);
	HoldSnapshot();

	// Stand the capsule up, facing the way the body gets up. The mesh goes back to its place on the
	// capsule; the held bodies do not move (no teleport).
	Character->SetActorLocationAndRotation(Stand, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::None);
	if (!Body->GetRelativeTransform().Equals(MeshRelativeTransform, 0.01f))
	{
		// Moving a mesh with a simulated root needs the teleport flag; the teleport moves the bodies
		// too, so they are put back on the snapshot straight after.
		Body->SetRelativeTransform(MeshRelativeTransform, false, nullptr, ETeleportType::TeleportPhysics);
		HoldSnapshot();
	}
	Body->SetAllBodiesPhysicsBlendWeight(1.f);
	SetState(ECSBodyState::Recovering);
	return true;
}

void UCSPhysicsBodyComponent::TickRecovering(float DeltaTime)
{
	const UCSMovementTuning& T = GetTuning();
	HoldSnapshot();
	const float Alpha = FMath::Clamp(StateSeconds / T.RagdollRecoverSeconds, 0.f, 1.f);
	if (Alpha >= 1.f - KINDA_SMALL_NUMBER)
	{
		FinishRecovery();
		return;
	}
	// Physics weight 1 is the held ragdoll pose, 0 the animated pose; smoothed at both ends.
	Body->SetAllBodiesPhysicsBlendWeight(1.f - FMath::SmoothStep(0.f, 1.f, Alpha));
}

void UCSPhysicsBodyComponent::FinishRecovery()
{
	ACSCharacter* Character = GetCharacter();
	Snapshot.Reset();
	// Back to the animated pose: every body kinematic and moved onto the pose at once, so the
	// upper body starts Normal from the pose and not from where the ragdoll lay.
	Body->SetAllBodiesPhysicsBlendWeight(0.f);
	Body->SetAllBodiesSimulatePhysics(false);
	Body->SetEnableGravity(true);
	RestoreBodyPhysics();
	Body->SetRelativeTransform(MeshRelativeTransform);
	Body->UpdateKinematicBonesToAnim(Body->GetComponentSpaceTransforms(), ETeleportType::TeleportPhysics, false, EAllowKinematicDeferral::DisallowDeferral);
	Body->SetAllPhysicsLinearVelocity(FVector::ZeroVector, false);
	Body->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
	EnterNormal();

	if (Character)
	{
		Character->GetCapsuleComponent()->SetCollisionEnabled(CapsuleCollision);
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			Move->Velocity = FVector::ZeroVector;
			// Walking finds and snaps to the floor below the capsule.
			Move->SetMovementMode(bStandFound ? MOVE_Walking : MOVE_Falling);
		}
	}
	SetState(ECSBodyState::Normal);
	if (Character)
	{
		Character->SetRagdolled(false);
	}
	OnRecovered.Broadcast();
}

void UCSPhysicsBodyComponent::HandleLanded(const FHitResult& Hit)
{
	// ACSCharacter records the landing speed before LandedDelegate fires, so this reads this landing.
	const ACSCharacter* Character = GetCharacter();
	if (Character && State != ECSBodyState::Ragdoll && Character->GetLastLandingSpeed() > GetTuning().RagdollLandingSpeedThreshold)
	{
		EnterRagdoll();
	}
}

// ----- Contact hits -----

FVector UCSPhysicsBodyComponent::ComputeContactVelocityChange(const FVector& OtherVelocity, float OtherMass, const FVector& TowardUs, float OurMass,
	float MinSpeed, float Scale, float MaxVelocityChange)
{
	if (OtherMass <= 0.f || OurMass <= KINDA_SMALL_NUMBER || Scale <= 0.f || OtherVelocity.ContainsNaN())
	{
		return FVector::ZeroVector;
	}
	const FVector Toward = TowardUs.GetSafeNormal();
	const float Approach = Toward.IsNearlyZero() ? OtherVelocity.Size() : float(FVector::DotProduct(OtherVelocity, Toward));
	if (Approach <= 0.f || Approach < MinSpeed)
	{
		return FVector::ZeroVector;
	}
	const float Size = FMath::Min(Approach * OtherMass / OurMass * Scale, FMath::Max(MaxVelocityChange, 0.f));
	return OtherVelocity.GetSafeNormal() * Size;
}

FName UCSPhysicsBodyComponent::FindNearestBodyBone(const FVector& WorldPoint) const
{
	FName Best = NAME_None;
	double BestDistance = TNumericLimits<double>::Max();
	if (Body)
	{
		for (const FBodyInstance* B : Body->Bodies)
		{
			const FName Name = BodyName(B);
			if (Name == NAME_None || !B->IsValidBodyInstance())
			{
				continue;
			}
			const double Distance = FVector::DistSquared(B->GetUnrealWorldTransform().GetLocation(), WorldPoint);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Name;
			}
		}
	}
	return Best;
}

ECSContactResult UCSPhysicsBodyComponent::ReceiveContact(AActor* OtherActor, FName Bone, const FVector& OtherVelocity, float OtherMass,
	const FVector& OtherLocation, const FVector& ImpactPoint)
{
	const ACSCharacter* Character = GetCharacter();
	const UWorld* World = GetWorld();
	if (!Character || !World || !HasPhysicsBody())
	{
		return ECSContactResult::None;
	}
	// Never the character itself, and never something it threw.
	if (OtherActor && (OtherActor == Character || OtherActor->GetOwner() == Character || OtherActor->GetInstigator() == Character))
	{
		return ECSContactResult::None;
	}
	const UCSMovementTuning& T = GetTuning();
	const double Now = World->GetTimeSeconds();
	if (OtherActor)
	{
		if (const double* Last = ContactTimes.Find(OtherActor); Last && Now - *Last < T.ContactRepeatSeconds)
		{
			return ECSContactResult::None;
		}
	}
	const FVector Change = ComputeContactVelocityChange(OtherVelocity, OtherMass, ImpactPoint - OtherLocation, GetBodyMass(),
		T.MinContactSpeed, T.ContactImpulseScale, T.ContactImpulseMax);
	if (Change.IsNearlyZero())
	{
		return ECSContactResult::None;
	}
	FName BodyBone = FindBodyBone(Bone);
	if (BodyBone == NAME_None)
	{
		BodyBone = FindNearestBodyBone(ImpactPoint);
	}
	if (OtherActor)
	{
		// Forget actors that are gone, so the map does not grow with every ball thrown.
		for (auto It = ContactTimes.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
		ContactTimes.Add(OtherActor, Now);
	}

	ECSContactResult Result = ECSContactResult::Ignored;
	const bool bRagdolls = Change.Size() >= T.RagdollImpulseThreshold;
	if (State == ECSBodyState::Ragdoll)
	{
		// Fully simulated already: the physics engine has moved the bodies; adding the hit again would double it.
	}
	else if (State == ECSBodyState::Recovering && !bRagdolls)
	{
		// Getting up: a hit below the threshold does nothing (as in ApplyImpulseAtBone).
	}
	else
	{
		ApplyImpulseAtBone(BodyBone, Change * GetBodyMass());
		Result = bRagdolls ? ECSContactResult::Ragdoll : ECSContactResult::Stagger;
	}
	LastContact.Time = Now;
	LastContact.Bone = BodyBone;
	LastContact.VelocityChange = float(Change.Size());
	LastContact.Result = Result;
	LastContact.Source = GetNameSafe(OtherActor);
	return Result;
}

void UCSPhysicsBodyComponent::TrackNearbyBodies()
{
	const ACSCharacter* Character = GetCharacter();
	const UWorld* World = GetWorld();
	if (!Character || !World)
	{
		return;
	}
	PreviousNearbyVelocities = MoveTemp(NearbyVelocities);
	NearbyVelocities.Reset();

	// Everything that could reach the character within a few frames, however fast.
	static constexpr float Radius = 400.f;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CSContactTrack), false, Character);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Character->GetActorLocation(), FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!Component || NearbyVelocities.Contains(Component))
		{
			continue;
		}
		const AActor* Owner = Component->GetOwner();
		if (const ACSCharacter* Other = Cast<ACSCharacter>(Owner))
		{
			// A character moves by its movement component; its ragdoll by its bodies.
			const bool bRagdoll = Other->IsRagdolled() && Component != Other->GetCapsuleComponent();
			NearbyVelocities.Add(Component, bRagdoll ? Component->GetPhysicsLinearVelocity() : Other->GetVelocity());
		}
		else if (Component->IsSimulatingPhysics())
		{
			NearbyVelocities.Add(Component, Component->GetPhysicsLinearVelocity());
		}
	}
}

void UCSPhysicsBodyComponent::HandleContactEvent(AActor* OtherActor, UPrimitiveComponent* OtherComp, FName OurBone, const FVector& ImpactPoint)
{
	const ACSCharacter* Character = GetCharacter();
	if (!OtherComp || !OtherActor || !Character || OtherActor == Character)
	{
		return;
	}
	float Mass = 0.f;
	if (const ACSCharacter* Other = Cast<ACSCharacter>(OtherActor))
	{
		const UCSPhysicsBodyComponent* OtherBody = Other->GetPhysicsBody();
		Mass = OtherBody ? OtherBody->GetBodyMass() : 0.f;
	}
	else if (OtherComp->IsSimulatingPhysics())
	{
		Mass = OtherComp->GetMass();
	}
	if (Mass <= 0.f)
	{
		return; // the floor, a wall: nothing that moves
	}
	// How it moved before the contact: the contact itself has already slowed or turned it.
	FVector Velocity;
	if (const FVector* Before = PreviousNearbyVelocities.Find(OtherComp))
	{
		Velocity = *Before;
	}
	else if (const FVector* Latest = NearbyVelocities.Find(OtherComp))
	{
		Velocity = *Latest;
	}
	else
	{
		Velocity = OtherComp->GetComponentVelocity();
	}
	ReceiveContact(OtherActor, OurBone, Velocity, Mass, OtherComp->Bounds.Origin, ImpactPoint);
}

void UCSPhysicsBodyComponent::HandleBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// MyBoneName is the body of this mesh that was struck.
	HandleContactEvent(OtherActor, OtherComp, Hit.MyBoneName, Hit.ImpactPoint);
}

void UCSPhysicsBodyComponent::HandleCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// The capsule has no body parts: the hit counts for the body nearest the contact.
	HandleContactEvent(OtherActor, OtherComp, NAME_None, Hit.ImpactPoint);
}

void UCSPhysicsBodyComponent::HandleRagdollRequested()
{
	ToggleRagdoll();
}

void UCSPhysicsBodyComponent::HandlePushRequested(const FVector& Direction)
{
	ApplyImpulseAtBone(PushBone, Direction.GetSafeNormal() * GetTuning().DevPushSpeed * GetBodyMass());
}
