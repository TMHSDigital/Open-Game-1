#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CSPhysicsBodyComponent.generated.h"

class ACSCharacter;
class UPhysicalAnimationComponent;
class UPhysicalMaterial;
class USkeletalMeshComponent;
class UCSMovementTuning;
struct FBodyInstance;
struct FPhysicalAnimationData;

/** What the physics body is doing. */
UENUM(BlueprintType)
enum class ECSBodyState : uint8
{
	/** The upper body follows the animation through physical animation motors (sway, no stagger). */
	Normal,
	/** A limb chain was hit: its motors are weak and ramping back to Normal (stagger without falling). */
	Hit,
	/** Fully simulated; the capsule follows the pelvis; no control. */
	Ragdoll,
	/** Settled: the ragdoll pose is held and blended to the animated pose, then control returns. */
	Recovering,
};

DECLARE_MULTICAST_DELEGATE(FCSBodyRecovered);
DECLARE_MULTICAST_DELEGATE_TwoParams(FCSBodyStateChanged, ECSBodyState /* Old */, ECSBodyState /* New */);

/**
 * The physics-driven body of a CS character, wrapping a UPhysicalAnimationComponent.
 * The strengths and times are in the owner's applied tuning (UCSMovementTuning: NormalProfile,
 * HitProfile and the Ragdoll and Body values), read every frame, so a reload takes effect at once.
 *
 * Normal: the pelvis and legs stay animated (kinematic), so footing and the capsule are untouched.
 * Every body from SimulatedRootBone down (spine, arms, head) is simulated and pulled toward the
 * animated pose by world-space motors at NormalProfile; arms and head (LimbChainRoots) at
 * NormalLimbStrengthScale of it, so they sway with starts, stops and turns.
 *
 * Hit (ApplyImpulseAtBone below the ragdoll threshold): the hit chain (the arm or head holding the
 * bone, otherwise the whole upper body) drops to HitProfile, gets the hit's velocity change, holds
 * for HitHoldSeconds and ramps back over HitRecoverSeconds. The character also gets
 * HitCapsuleVelocityShare of the velocity change along the ground; it never leaves the ground.
 *
 * Ragdoll (EnterRagdoll, an impulse whose velocity change, impulse / mass, reaches
 * RagdollImpulseThreshold, or a landing faster than RagdollLandingSpeedThreshold): every body is
 * simulated with no motors and starts with the character's velocity; movement stops; the capsule
 * stops colliding and follows the pelvis (on the floor below it when there is one). Calls
 * ACSCharacter::SetRagdolled(true), so the camera rig uses third person and input is ignored.
 *
 * Ragdoll damping and friction (RagdollLinearDamping, RagdollAngularDamping, RagdollFriction) apply
 * while ragdolled, also after a reload, and are undone at the end of the recovery. Linear damping
 * applies only while the pelvis is on the ground, so a fall is not slowed.
 *
 * Recovering: when the pelvis has stayed slower than RagdollSettleSpeed for RagdollSettleSeconds
 * (or after RagdollMaxSeconds, or on R), the world transforms of the bodies are snapshotted and
 * held there (no gravity, no velocity); the capsule is stood upright on walkable ground near the
 * pelvis (never inside geometry or below the floor; if there is none, at the last standable spot
 * the ragdoll passed; failing that, clear of geometry above the pelvis to fall; failing that the
 * ragdoll waits and tries again), facing the way the body would get up; and the
 * physics blend weight runs from 1 (the held ragdoll pose) to 0 (the animated pose) over
 * RagdollRecoverSeconds. Then the bodies return to Normal, SetRagdolled(false) is called and
 * OnRecovered is broadcast.
 *
 * Dev tools: R (UCSDevTools::OnRagdollRequested) toggles the ragdoll (from Ragdoll it starts the
 * recovery at once); P (OnPushRequested) pushes the chest along the view at DevPushSpeed.
 *
 * Needs a skeletal mesh with a physics asset whose bodies use UE5 humanoid bone names (the names
 * below are properties); without one every call is a logged no-op and the state stays Normal.
 * The mesh's collision profile is set to RagdollCollisionProfile (QueryAndPhysics, ignores pawns,
 * so the bodies never push the capsule).
 */
UCLASS(ClassGroup = CS, meta = (BlueprintSpawnableComponent))
class CSCHARACTER_API UCSPhysicsBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSPhysicsBodyComponent();

	/** Gives the component the mesh and the physical animation component it drives. ACSCharacter calls this from PostInitializeComponents. */
	void Setup(USkeletalMeshComponent* InBody, UPhysicalAnimationComponent* InPhysicalAnimation);

	/**
	 * A hit at Bone (or the nearest bone above it with a body). Impulse in kg cm/s; its size divided
	 * by the character's mass (a velocity change, cm/s) decides: at or above RagdollImpulseThreshold
	 * the character ragdolls, below it the bone's chain staggers. In Ragdoll the impulse just pushes
	 * the bodies; while Recovering it ragdolls again at the threshold and is ignored below it.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Body")
	void ApplyImpulseAtBone(FName Bone, FVector Impulse);

	/** Goes full ragdoll now (also from Recovering). Does nothing when already in Ragdoll. */
	UFUNCTION(BlueprintCallable, Category = "CS|Body")
	void EnterRagdoll();

	/** From Ragdoll: starts the recovery now, settled or not. Otherwise does nothing. */
	UFUNCTION(BlueprintCallable, Category = "CS|Body")
	void Recover();

	/** R: ragdoll from Normal or Hit, recover from Ragdoll. */
	UFUNCTION(BlueprintCallable, Category = "CS|Body")
	void ToggleRagdoll();

	UFUNCTION(BlueprintPure, Category = "CS|Body")
	ECSBodyState GetState() const { return State; }

	/** Seconds in the current state. */
	UFUNCTION(BlueprintPure, Category = "CS|Body")
	float GetStateSeconds() const { return StateSeconds; }

	/** True when the mesh has a physics asset with bodies, so the states can run. */
	UFUNCTION(BlueprintPure, Category = "CS|Body")
	bool HasPhysicsBody() const;

	/** Mass of all bodies, kg (0 without a physics body). */
	UFUNCTION(BlueprintPure, Category = "CS|Body")
	float GetBodyMass() const;

	/** In Ragdoll: true while the pelvis is on the ground (RagdollLinearDamping applies), false in the air. */
	bool IsRagdollOnGround() const { return State == ECSBodyState::Ragdoll && bRagdollOnGround; }

	/** Hit weight of a body: 1 at HitProfile, 0 at Normal (0 for a body never hit or unknown). */
	float GetHitWeight(FName Bone) const;

	/** The bone the chain containing Bone starts at (one of LimbChainRoots, else SimulatedRootBone). */
	FName GetChainRoot(FName Bone) const;

	/** Control returned after a ragdoll (end of Recovering). */
	FCSBodyRecovered OnRecovered;

	/** Every state change. */
	FCSBodyStateChanged OnStateChanged;

	/** The ragdoll's root body. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Body")
	FName PelvisBone = TEXT("pelvis");

	/** In Normal and Hit, bodies from this bone down are simulated; the rest stay animated. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Body")
	FName SimulatedRootBone = TEXT("spine_01");

	/** Limb chains: lighter in Normal (NormalLimbStrengthScale), and a hit on one weakens only that chain. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Body")
	TArray<FName> LimbChainRoots = {TEXT("upperarm_l"), TEXT("upperarm_r"), TEXT("head")};

	/** Where the dev push (P) lands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Body")
	FName PushBone = TEXT("spine_03");

	/** Collision profile of the mesh while it has a physics body: physics collision, ignores pawns (the capsule). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Body")
	FName RagdollCollisionProfile = TEXT("Ragdoll");

	/** Recovery looks this far from the pelvis for walkable ground the capsule fits on, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Body", meta = (ClampMin = "0", Units = "cm"))
	float RecoverySearchRadius = 300.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	ACSCharacter* GetCharacter() const;
	const UCSMovementTuning& GetTuning() const;
	void SetState(ECSBodyState NewState);

	/** Simulates the upper body with Normal motors (pelvis and legs animated). */
	void EnterNormal();
	/** Writes the motor strengths of every simulated body from the tuning and the hit weights. */
	void ApplyMotors();
	/** Normal motor data for a body (NormalProfile, scaled for limb chains). */
	FPhysicalAnimationData NormalDataFor(FName Bone) const;
	bool IsInChain(FName Bone, FName Root) const;
	/** Bone, or the nearest ancestor with a body. NAME_None if none. */
	FName FindBodyBone(FName Bone) const;
	void Stagger(FName Bone, const FVector& VelocityChange);

	void TickHit(float DeltaTime);
	void TickRagdoll(float DeltaTime);
	void TickRecovering(float DeltaTime);

	/** Moves the capsule to the pelvis (on the floor below it when there is one) without touching the bodies. */
	void FollowPelvis();
	/**
	 * Starts Recovering: picks where to stand, snapshots, stands the capsule up, starts the blend.
	 * False (and still Ragdoll) when there is nowhere clear to put the capsule at all.
	 */
	bool BeginRecovery();
	/** Ragdoll damping and friction from the tuning onto every body. */
	void ApplyRagdollPhysics();
	/** Ragdoll damping for the current ground contact (linear damping only on the ground). */
	void ApplyRagdollDamping();
	/** The bodies' own damping and material back (out of Ragdoll). */
	void RestoreBodyPhysics();
	/** Holds every body at its snapshot transform with no velocity. */
	void HoldSnapshot();
	void FinishRecovery();

	/**
	 * A capsule centre standing on walkable ground near Around, not overlapping anything, searched
	 * outward up to RecoverySearchRadius. False if there is none.
	 */
	bool FindStandLocation(const FVector& Around, FVector& OutLocation) const;
	/**
	 * One candidate of FindStandLocation: a capsule swept down at XY from above AroundZ lands on
	 * walkable ground and fits there. OutLocation is its centre.
	 */
	bool TryStandAt(const FVector& XY, float AroundZ, FVector& OutLocation) const;
	/** Yaw the character faces after getting up, from the pelvis and spine. */
	float GetUpYaw() const;

	UFUNCTION()
	void HandleLanded(const FHitResult& Hit);
	void HandleRagdollRequested();
	void HandlePushRequested(const FVector& Direction);

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;

	ECSBodyState State = ECSBodyState::Normal;
	float StateSeconds = 0.f;
	/** Seconds the pelvis has been slower than RagdollSettleSpeed. */
	float SettledSeconds = 0.f;
	/** Seconds since each body in a hit chain was hit. */
	TMap<FName, float> HitAges;
	/** The tuning object the motors were last written from (a reload makes a new one). */
	TWeakObjectPtr<const UCSMovementTuning> MotorTuning;
	/** Body world transforms at the start of Recovering, in Body->Bodies order. */
	TArray<FTransform> Snapshot;
	/** The mesh's place on the capsule before the ragdoll, restored by the recovery. */
	FTransform MeshRelativeTransform = FTransform::Identity;
	ECollisionEnabled::Type CapsuleCollision = ECollisionEnabled::QueryAndPhysics;
	/** The recovery found walkable ground to stand on (otherwise the character falls from where it stood up). */
	bool bStandFound = true;
	/**
	 * The last place the capsule could stand during this ragdoll (where it started, if on walkable
	 * ground, then wherever the pelvis passed over walkable ground): the recovery's fallback when
	 * nothing near the pelvis is standable (a steep slope).
	 */
	FVector LastClearStand = FVector::ZeroVector;
	bool bHasClearStand = false;
	/** Each body's own damping, restored after the ragdoll (Body->Bodies order). */
	TArray<FVector2f> BaseDamping;
	/** Friction override for the ragdoll bodies (RagdollFriction), made on first use. */
	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> RagdollMaterial;
	/** In Ragdoll: the pelvis is close above the ground (ground damping on). */
	bool bRagdollOnGround = false;
	/** The tuning object the ragdoll damping was last written from. */
	TWeakObjectPtr<const UCSMovementTuning> RagdollTuning;
	bool bWarnedNoBody = false;

	/**
	 * True from BeginPlay until the first tick in a later frame, when the character was spawned in
	 * the middle of a world tick (a timer, an AI spawner, a respawn). Starting the simulated body
	 * in that same frame crashes in the engine's physics blend, because the new mesh has not been
	 * through a full frame yet. While it is set the body counts as absent (HasPhysicsBody is false)
	 * and hits are ignored: the character is plain animation for that one frame.
	 */
	bool bStartPending = false;
	double StartPendingSince = 0.0;

	/** Hooks the physical animation up to the mesh and enters the Normal state. */
	void StartBody();
};
