#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CSPhysicsBodyComponent.h"
#include "CSStrikeComponent.generated.h"

class ACSCharacter;
class UCSMovementTuning;
class USkeletalMeshComponent;

/** Where a strike is. */
UENUM(BlueprintType)
enum class ECSStrikePhase : uint8
{
	/** No strike: the arms follow the animation. */
	None,
	/** The fist is drawn back to the shoulder. */
	Windup,
	/** The fist is driven out along the aim; this is when it can hit. */
	Strike,
	/** The fist is let go and the arm returns to the animation; a new strike cannot start yet. */
	Recover,
};

DECLARE_MULTICAST_DELEGATE_OneParam(FCSStrikeStarted, ACSCharacter* /* Attacker */);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FCSStrikeLanded, ACSCharacter* /* Attacker */, AActor* /* Victim */, ECSContactResult /* None for a prop */);

/**
 * The first melee attack of a CS character: a straight punch, thrown with alternating hands.
 *
 * There is no punch animation. The arms are simulated bodies that physical animation pulls toward
 * the animated pose (UCSPhysicsBodyComponent, Normal state); a punch takes one hand away from that
 * for a moment and steers it: back to the shoulder (Windup), then out to a point PunchReach along
 * the aim from the middle of the chest at up to PunchSpeed (Strike), then lets it go (Recover) and
 * the arm swings back to the animation by itself. The fist is steered by setting its body's velocity
 * toward a target every frame, so the rest of the arm follows through its joints and whatever the
 * fist runs into stops it. The target is on the centre line, not straight out from the shoulder: both
 * hands strike what the character faces. During Strike the chest is also pushed along the aim at
 * PunchLean, so the upper body goes into the punch: two characters cannot stand closer than their
 * capsules, and an arm alone barely reaches across that gap.
 *
 * The hit: during Strike the path of the fist is swept each frame. The first body of another
 * character it meets takes a contact hit through UCSPhysicsBodyComponent::ReceiveContact, the same
 * entry a thrown ball uses, as if something of PunchMass had arrived at the fist's top speed along
 * the aim. PunchMass is the part of the attacker's weight that is behind the fist, not the weight of
 * the hand. So a punch staggers or knocks down by the rules of every other hit, lands on the body
 * part it touched, and is seen by anything that listens for hits. A simulated prop in the way gets
 * the same momentum as an impulse. One strike hits one character and each prop once.
 *
 * A strike cannot start while one is under way, while the character is ragdolled or getting up,
 * or without a physics body. Going down ends a strike at once.
 *
 * Tuning (UCSMovementTuning, group Punch): PunchWindupSeconds, PunchStrikeSeconds,
 * PunchRecoverSeconds, PunchReach, PunchSpeed, PunchLean, PunchMass.
 */
UCLASS(ClassGroup = CS, meta = (BlueprintSpawnableComponent))
class CSCHARACTER_API UCSStrikeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSStrikeComponent();

	/** Throws a punch with the hand that did not throw the last one. False when a strike cannot start (see the class comment). */
	UFUNCTION(BlueprintCallable, Category = "CS|Strike")
	bool StartPunch();

	/** Ends the strike under way without a recovery. */
	void Cancel();

	UFUNCTION(BlueprintPure, Category = "CS|Strike")
	ECSStrikePhase GetPhase() const { return Phase; }

	/** Seconds in the current phase. */
	float GetPhaseSeconds() const { return PhaseSeconds; }

	/** The strike under way, or the last one, is with the right hand. */
	bool IsRightHand() const { return bRightHand; }

	/** Strikes started since play began. */
	int32 GetStrikeCount() const { return StrikeCount; }

	/** The strike under way, or the last one, hit a character. */
	bool HasLanded() const { return bLanded; }

	/** The farthest the fist got from the middle of the chest along the aim in the strike under way or the last one, cm. */
	float GetPeakReach() const { return PeakReach; }

	/** The fastest the fist moved in the strike under way or the last one, cm/s. */
	float GetPeakSpeed() const { return PeakSpeed; }

	/** Where a strike goes: the view direction, its pitch held within MaxAimPitch; the actor's facing without a view. */
	FVector GetAimDirection() const;

	/** World location of the striking hand (the right one before any strike). */
	FVector GetHandLocation() const;

	/** World location of the middle of the striking fist: FistOffset past the hand bone, along the forearm. */
	FVector GetFistLocation() const;

	/**
	 * Where the fist is steered in a phase: at its own shoulder (a little along the aim) in Windup,
	 * Reach along the aim from Chest (the point between the shoulders) in Strike. The shoulder itself
	 * in any other phase.
	 */
	static FVector ComputeHandTarget(ECSStrikePhase InPhase, const FVector& Shoulder, const FVector& Chest, const FVector& Aim, float Reach);

	/**
	 * The velocity that takes the fist from Hand to Target in one frame of DeltaTime, at most
	 * MaxSpeed, on top of BaseVelocity (the character's own motion). BaseVelocity alone when the
	 * frame has no length.
	 */
	static FVector ComputeDriveVelocity(const FVector& Hand, const FVector& Target, float DeltaTime, float MaxSpeed, const FVector& BaseVelocity);

	/** Every strike started, in any world; listeners check the attacker's world. */
	static FCSStrikeStarted OnStrikeStarted;

	/** Every strike that reached a character (with what the hit did) or a prop (None), in any world. */
	static FCSStrikeLanded OnStrikeLanded;

	/** The bones of the right and the left fist. */
	UPROPERTY(EditAnywhere, Category = "CS|Strike")
	FName HandBones[2] = {TEXT("hand_r"), TEXT("hand_l")};

	/** The bones of the right and the left forearm: steered when the hand has no body of its own. */
	UPROPERTY(EditAnywhere, Category = "CS|Strike")
	FName ForearmBones[2] = {TEXT("lowerarm_r"), TEXT("lowerarm_l")};

	/** The bones of the right and the left shoulder: where a punch starts from. */
	UPROPERTY(EditAnywhere, Category = "CS|Strike")
	FName ShoulderBones[2] = {TEXT("upperarm_r"), TEXT("upperarm_l")};

	/** The chest body that leans into a punch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike")
	FName ChestBone = TEXT("spine_03");

	/** The aim's pitch is held within this, degrees, so a punch cannot go straight up or into the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike", meta = (ClampMin = "0", ClampMax = "89", Units = "deg"))
	float MaxAimPitch = 40.f;

	/**
	 * How far past the hand bone (the wrist) the middle of the fist is, cm, along the forearm. The
	 * sweep is centred there: centred on the wrist it ends short of what the knuckles touch.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike", meta = (ClampMin = "0", Units = "cm"))
	float FistOffset = 8.f;

	/** Radius of the swept fist, cm: a little more than the hand's body, which is stopped at the surface it hits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike", meta = (ClampMin = "1", Units = "cm"))
	float FistRadius = 12.f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	ACSCharacter* GetCharacter() const;
	USkeletalMeshComponent* GetBody() const;
	const UCSMovementTuning& GetTuning() const;
	bool CanStrike() const;
	void SetPhase(ECSStrikePhase NewPhase);
	/** Steers the fist toward this phase's target. */
	void Drive(float DeltaTime);
	/** Sweeps the fist's path since the last frame and delivers what it meets. */
	void Sweep();
	int32 HandIndex() const { return bRightHand ? 0 : 1; }
	/** The point between the shoulders. */
	FVector GetChestLocation() const;

	ECSStrikePhase Phase = ECSStrikePhase::None;
	float PhaseSeconds = 0.f;
	/** Starts false so the first punch is with the right hand. */
	bool bRightHand = false;
	int32 StrikeCount = 0;
	bool bLanded = false;
	float PeakReach = 0.f;
	float PeakSpeed = 0.f;
	/** The aim, fixed when the strike starts: turning the view mid-punch does not bend it. */
	FVector StrikeAim = FVector::ForwardVector;
	FVector LastHandLocation = FVector::ZeroVector;
	bool bHasLastHand = false;
	/** What this strike has already hit. */
	TArray<TWeakObjectPtr<AActor>> HitActors;
};
