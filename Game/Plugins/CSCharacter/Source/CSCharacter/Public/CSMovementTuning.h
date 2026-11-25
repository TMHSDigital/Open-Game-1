#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "CSMovementTuning.generated.h"

/**
 * Every movement feel value for ACSCharacter, in one data asset.
 *
 * The defaults are the starting values. Editing the asset and calling ACSCharacter::ApplyTuning
 * (or reloading it) changes feel without a rebuild. Ranges are shown as UI hints only: nothing is
 * clamped on edit, so a bad value is caught by IsValid, which callers check before applying a
 * reloaded asset.
 */
UCLASS(BlueprintType)
class CSCHARACTER_API UCSMovementTuning : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UCSMovementTuning();

	// ----- Speeds -----

	/** Walk speed (held walk key), cm/s. Must be positive and not above JogSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speeds", meta = (UIMin = "50", UIMax = "400", Units = "cm/s"))
	float WalkSpeed = 180.f;

	/** Jog speed (default gait), cm/s. Must be between WalkSpeed and SprintSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speeds", meta = (UIMin = "100", UIMax = "700", Units = "cm/s"))
	float JogSpeed = 380.f;

	/** Sprint top speed (held sprint key), cm/s. Reached over SprintRampSeconds from a jog. Must not be below JogSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speeds", meta = (UIMin = "200", UIMax = "1200", Units = "cm/s"))
	float SprintSpeed = 620.f;

	/** Crouched speed, cm/s. Must be positive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speeds", meta = (UIMin = "50", UIMax = "400", Units = "cm/s"))
	float CrouchSpeed = 160.f;

	/** Seconds from a steady jog to 95 percent of SprintSpeed. The same at any frame rate. Must be positive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speeds", meta = (UIMin = "0.1", UIMax = "3", Units = "s"))
	float SprintRampSeconds = 0.6f;

	/**
	 * Ground acceleration from input, cm/s^2 (addition to the spec list: every feel value lives here).
	 * 1200 reaches jog speed from rest in about 0.3 s. Must be positive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speeds", meta = (UIMin = "200", UIMax = "6000", Units = "cm/s^2"))
	float Acceleration = 1200.f;

	// ----- Turning -----

	/**
	 * Ground grip when changing direction (addition to the spec list; the movement component's
	 * GroundFriction). The velocity turns toward the input at about this rate per second, so the
	 * turn radius is roughly speed / grip: about 22 cm at a walk with the default 8. Must be positive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turning", meta = (UIMin = "1", UIMax = "20"))
	float GroundFriction = 8.f;

	/**
	 * Grip multiplier at full sprint (addition to the spec list). Grip blends from GroundFriction at
	 * jog speed down to GroundFriction * this at sprint speed, so a sprint turns wider than speed
	 * alone would make it: about 1.3 m at sprint with the defaults. Inside (0, 1].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turning", meta = (UIMin = "0.1", UIMax = "1"))
	float SprintGripScale = 0.6f;

	/** How fast the body turns to face the move direction, degrees per second (addition to the spec list). Must be positive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turning", meta = (UIMin = "90", UIMax = "1440", Units = "deg"))
	float RotationRateYaw = 540.f;

	// ----- Braking and slopes -----

	/**
	 * Braking friction when there is no input (no spec value; default 2). Speed falls by this
	 * fraction of itself per second on top of BrakingDeceleration. With 1000 deceleration a
	 * sprint stops in about 0.4 s over about 1.1 m and a jog in about 0.5 m. Must not be negative.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Braking and slopes", meta = (UIMin = "0", UIMax = "10"))
	float BrakingFriction = 2.f;

	/** Constant braking deceleration with no input, cm/s^2 (addition to the spec list). Must not be negative. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Braking and slopes", meta = (UIMin = "0", UIMax = "4000", Units = "cm/s^2"))
	float BrakingDeceleration = 1000.f;

	/** Steepest walkable slope, degrees. Must be inside (0, 90). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Braking and slopes", meta = (UIMin = "5", UIMax = "85", Units = "deg"))
	float MaxWalkableSlopeDeg = 44.f;

	/**
	 * Uphill speed loss (addition to the spec list): speed is scaled by 1 - UphillSpeedLoss * sin(slope
	 * along the move direction). 0.3 gives about 10 percent slower on a 20 degree slope. Inside [0, 1).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Braking and slopes", meta = (UIMin = "0", UIMax = "0.99"))
	float UphillSpeedLoss = 0.3f;

	/**
	 * Downhill speed gain (addition to the spec list): speed is scaled by 1 + DownhillSpeedGain * sin(slope).
	 * 0.1 gives about 3 percent faster on a 20 degree slope. Inside [0, 1].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Braking and slopes", meta = (UIMin = "0", UIMax = "1"))
	float DownhillSpeedGain = 0.1f;

	// ----- Air -----

	/** Height of a standing jump, cm. Jump speed is derived from it and the world gravity. Must be positive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (UIMin = "10", UIMax = "150", Units = "cm"))
	float JumpHeightCm = 45.f;

	/**
	 * Share of ground acceleration available in the air (no spec value; default 0.2: limited steering,
	 * a jump keeps most of its take-off direction). Inside [0, 1].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (UIMin = "0", UIMax = "1"))
	float AirControl = 0.2f;

	/**
	 * Horizontal speed lost on landing, as a fraction per m/s of fall speed above the landing speed of
	 * a normal jump (no spec value; default 0.1). A jump or a short drop costs nothing noticeable;
	 * a 3 m drop (7.7 m/s) loses about 47 percent; the loss is capped at all of it. Must not be negative.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (UIMin = "0", UIMax = "0.5"))
	float LandingSpeedLossPerMps = 0.1f;

	// ----- Camera -----

	/** Third-person field of view, degrees. Inside [60, 120]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (UIMin = "60", UIMax = "120", Units = "deg"))
	float ThirdPersonFOV = 75.f;

	/** First-person field of view, degrees. Inside [60, 120]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (UIMin = "60", UIMax = "120", Units = "deg"))
	float FirstPersonFOV = 85.f;

	/** Seconds to blend between the first- and third-person cameras. Must be positive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (UIMin = "0.05", UIMax = "1", Units = "s"))
	float CameraBlendSeconds = 0.2f;

	/** Third-person spring arm length behind the eyes, cm (no spec value; default 260). Inside (0, 1000]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (UIMin = "50", UIMax = "1000", Units = "cm"))
	float ThirdPersonArmLength = 260.f;

	/**
	 * Third-person camera offset at the end of the arm, in the view's frame (X forward, Y right,
	 * Z up), cm. Default (0, 45, 10): over the right shoulder. Each part within [-200, 200].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (UIMin = "-200", UIMax = "200"))
	FVector ThirdPersonShoulderOffset = FVector(0.f, 45.f, 10.f);

	/**
	 * Third-person positional lag: how fast the camera catches up with the character, 1/s (higher is
	 * tighter; default 12, a slight lag for weight). There is no rotational lag. Must be positive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (UIMin = "2", UIMax = "30"))
	float ThirdPersonLagSpeed = 12.f;

	/** Most the third-person camera may trail the character, cm (default 60; 0 means no limit). Not negative. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (UIMin = "0", UIMax = "200", Units = "cm"))
	float ThirdPersonLagMaxDistance = 60.f;

	/**
	 * First person: time constant of the eye's horizontal smoothing, seconds (default 0.1). Sway of
	 * the head reaches the camera this slowly; moving and turning are followed at once. Within [0, 1].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (UIMin = "0", UIMax = "1", Units = "s"))
	float FirstPersonSmoothSeconds = 0.1f;

	/**
	 * First person, standing: camera height above the feet, cm (default 165; the base character's
	 * eyes are at 168 at rest and about 162 in the idle pose). The camera follows the head only
	 * sideways and forward; its height is this fixed value, because the run cycle bounces the head
	 * by up to 24 cm (no head bob, by design). Inside (0, 300].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (UIMin = "100", UIMax = "300", Units = "cm"))
	float FirstPersonEyeHeightCm = 165.f;

	/** First person, crouched: camera height above the feet, cm (default 90; the crouch pose puts the eyes at about 86). Positive, not above the standing height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (UIMin = "50", UIMax = "160", Units = "cm"))
	float FirstPersonCrouchedEyeHeightCm = 90.f;

	/** First person: time constant of the move between the standing and crouched heights, seconds (default 0.15). Within [0, 2]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (UIMin = "0", UIMax = "2", Units = "s"))
	float FirstPersonHeightSmoothSeconds = 0.15f;

	// ----- Ragdoll -----

	/**
	 * Seconds of the last part of getting up: the blend from where the body has got to onto the
	 * movement pose. With GetUpStrength at 0 it is the whole recovery, straight from the pose the
	 * ragdoll lay in. Must be positive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "0.1", UIMax = "3", Units = "s"))
	float RagdollRecoverSeconds = 0.3f;

	/**
	 * How hard the body pulls itself up after a ragdoll: every body is driven toward the animated
	 * pose by the Normal motors times this, with gravity and the floor still acting, first toward the
	 * crouch pose (GetUpCrouchSeconds) and then toward standing (GetUpStandSeconds). 0: no pulling
	 * up, only the blend (RagdollRecoverSeconds).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "0", UIMax = "20"))
	float GetUpStrength = 4.f;

	/** Seconds the body is pulled toward the crouch pose when getting up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "0", UIMax = "3", Units = "s"))
	float GetUpCrouchSeconds = 0.55f;

	/** Seconds the body is then pulled toward the standing pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "0", UIMax = "3", Units = "s"))
	float GetUpStandSeconds = 0.5f;

	/**
	 * An impulse goes full ragdoll above this size (no spec value; default 500). Measured as the
	 * velocity change it would give the whole character, cm/s (impulse divided by mass), so it does
	 * not depend on the masses in the physics asset. 500 is a hard shove. Must be positive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "50", UIMax = "5000", Units = "cm/s"))
	float RagdollImpulseThreshold = 500.f;

	/**
	 * Landing faster than this vertical speed goes full ragdoll, cm/s (no spec value; default 1000,
	 * a drop of about 5 m). Must be positive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "300", UIMax = "5000", Units = "cm/s"))
	float RagdollLandingSpeedThreshold = 1000.f;

	/**
	 * The ragdoll counts as settled once the pelvis moves slower than this, cm/s, for
	 * RagdollSettleSeconds (no spec value; default 25). Must be positive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "5", UIMax = "200", Units = "cm/s"))
	float RagdollSettleSpeed = 25.f;

	/** Seconds the pelvis must stay below RagdollSettleSpeed before recovery starts (default 0.5). Must be positive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "0.1", UIMax = "10", Units = "s"))
	float RagdollSettleSeconds = 0.5f;

	/**
	 * Recovery starts after this long in ragdoll even if the body never settles (sliding down a
	 * slope, rocking on an edge), seconds (default 8). Must be above RagdollSettleSeconds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "1", UIMax = "30", Units = "s"))
	float RagdollMaxSeconds = 8.f;

	/**
	 * Linear damping of the ragdoll bodies while the ragdoll is on the ground (pelvis within 35 cm of
	 * the floor), 1/s (no spec value; default 0.5). In the air the bodies keep their own near-zero
	 * damping, so a fall is a real fall. With the default friction, a 750 cm/s shove flies about
	 * 2.9 m and then travels about 2 m on the ground (1.9 m at 1, 1.3 m at 2, 0.7 m at 5).
	 * Within [0, 10].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "0", UIMax = "10"))
	float RagdollLinearDamping = 0.5f;

	/** Angular damping of every ragdoll body, 1/s (default 1): less spinning and rolling. Within [0, 20]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "0", UIMax = "20"))
	float RagdollAngularDamping = 1.f;

	/**
	 * Friction of the ragdoll bodies (a physical material override; the larger of this and the
	 * ground's friction is used; default 1). Higher stops a slide on the ground sooner. Within [0, 4].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "0", UIMax = "4"))
	float RagdollFriction = 1.f;

	// ----- Body (physical animation) -----

	/**
	 * Normal: how strongly the simulated upper body (spine, arms, head) follows the animated pose.
	 * Physical animation motor strengths (UPhysicalAnimationComponent), world space by default so
	 * the body keeps up with the capsule. Every strength and force must be finite and not negative;
	 * a max force of 0 means no limit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	FPhysicalAnimationData NormalProfile;

	/**
	 * Arms and head follow the pose at this share of NormalProfile, so they sway with starts,
	 * stops and turns (default 0.35). Within [0, 1].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "0", UIMax = "1"))
	float NormalLimbStrengthScale = 0.35f;

	/**
	 * Hit: the strengths a hit limb chain drops to (an arm, the head, or the whole upper body for a
	 * hit on the torso or legs), held for HitHoldSeconds and then ramped back to Normal over
	 * HitRecoverSeconds. Same rules as NormalProfile.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	FPhysicalAnimationData HitProfile;

	/** Seconds a hit chain stays at HitProfile before ramping back (default 0.15). Not negative. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "0", UIMax = "1", Units = "s"))
	float HitHoldSeconds = 0.15f;

	/** Seconds to ramp a hit chain from HitProfile back to Normal (default 0.6). Must be positive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "0.05", UIMax = "3", Units = "s"))
	float HitRecoverSeconds = 0.6f;

	/**
	 * Share of a stagger's velocity change given to the whole character along the ground, so a
	 * shove moves the feet a little as well as the upper body (default 0.3; the vertical part is
	 * dropped, so a stagger never lifts the character). Within [0, 1].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "0", UIMax = "1"))
	float HitCapsuleVelocityShare = 0.3f;

	/**
	 * Dev tool push (P): the velocity change it gives the character, cm/s (default 300: a stagger;
	 * set above RagdollImpulseThreshold to test a ragdoll by impulse). Must be positive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "50", UIMax = "3000", Units = "cm/s"))
	float DevPushSpeed = 300.f;

	/**
	 * Slowest the walk, jog, sprint and crouch clips may play. The play rate follows the ground speed
	 * (UCSAnimInstance::ComputeLocomotion) so the feet match the ground at any gait speed; this and
	 * LocomotionRateMax only stop a clip from playing absurdly slow or fast. Inside the limits the
	 * feet do not slide; at a limit they do.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "0.25", UIMax = "1"))
	float LocomotionRateMin = 0.5f;

	/** Fastest the walk, jog, sprint and crouch clips may play (see LocomotionRateMin). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "1", UIMax = "4"))
	float LocomotionRateMax = 2.f;

	/**
	 * Stride: a clip that would have to play slower than StrideRateMin or faster than StrideRateMax
	 * to match the ground takes shorter or longer steps instead (FCSAnimNode_StrideScale), as far as
	 * StrideScaleMin and StrideScaleMax allow; what is left over is play rate again, so the feet
	 * always match. Min and max both at 1 turns the stride off and leaves play rate alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "0.5", UIMax = "1"))
	float StrideRateMin = 0.9f;

	/** See StrideRateMin. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "1", UIMax = "2"))
	float StrideRateMax = 1.15f;

	/** The shortest the steps are made, as a share of the clip's own (see StrideRateMin). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "0.5", UIMax = "1"))
	float StrideScaleMin = 0.75f;

	/** The longest the steps are made, as a multiple of the clip's own (see StrideRateMin). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "1", UIMax = "2"))
	float StrideScaleMax = 1.35f;

	// ----- Contact hits (things that run into the character) -----

	/**
	 * A body that reaches the character slower than this, cm/s, does nothing: resting on it, leaning,
	 * being pushed along. Only the other body's own speed toward the character counts, so walking
	 * into something that stands still is never a hit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "0", UIMax = "2000", Units = "cm/s"))
	float MinContactSpeed = 150.f;

	/**
	 * Scales a contact hit. The hit is the velocity change the other body's momentum would give the
	 * character (its mass times its speed toward the character, over the character's mass) times this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "0", UIMax = "5"))
	float ContactImpulseScale = 1.f;

	/**
	 * Extra scale when what hits the character is another standing character (running into it). Two
	 * characters weigh the same, so at 1 a sprint into a dummy would knock it down; at the default a
	 * walk does nothing, a jog and a sprint stagger. A ragdolled body counts as an object, not as this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "0", UIMax = "2"))
	float CharacterContactScale = 0.5f;

	/** The largest velocity change one contact hit may give, cm/s (compare RagdollImpulseThreshold). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "100", UIMax = "10000", Units = "cm/s"))
	float ContactImpulseMax = 1500.f;

	/** After a hit, the same object cannot hit the character again for this long, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "0.05", UIMax = "3", Units = "s"))
	float ContactRepeatSeconds = 0.3f;

	/** Mass of the dev ball (the throw key and the launcher), kg. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "0.5", UIMax = "100", Units = "kg"))
	float DevBallMass = 8.f;

	/** Speed the dev ball is thrown at, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (UIMin = "100", UIMax = "5000", Units = "cm/s"))
	float DevBallSpeed = 2000.f;

	// ----- Punch (UCSStrikeComponent) -----

	/** How long the fist is drawn back to the shoulder before it goes out, seconds. 0: no draw back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Punch", meta = (UIMin = "0", UIMax = "0.5", Units = "s"))
	float PunchWindupSeconds = 0.1f;

	/** How long the fist is driven out along the aim, seconds: the time in which it can hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Punch", meta = (UIMin = "0.05", UIMax = "0.6", Units = "s"))
	float PunchStrikeSeconds = 0.16f;

	/** After the strike, how long before the next punch can start, seconds. The arm swings back by itself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Punch", meta = (UIMin = "0.05", UIMax = "1", Units = "s"))
	float PunchRecoverSeconds = 0.25f;

	/** How far from the shoulder the fist is driven, cm. Past the arm's length it pulls the upper body after it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Punch", meta = (UIMin = "30", UIMax = "120", Units = "cm"))
	float PunchReach = 68.f;

	/** The fastest the fist is driven, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Punch", meta = (UIMin = "200", UIMax = "4000", Units = "cm/s"))
	float PunchSpeed = 1100.f;

	/** How fast the chest is pushed along the aim during the strike, cm/s: the upper body going into the punch. 0: the arm alone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Punch", meta = (UIMin = "0", UIMax = "600", Units = "cm/s"))
	float PunchLean = 160.f;

	/**
	 * The weight behind the fist, kg: what a punch hits like. The hit is this mass arriving at the
	 * fist's speed (see MinContactSpeed and ContactImpulseScale), so it is far more than the hand.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Punch", meta = (UIMin = "1", UIMax = "100", Units = "kg"))
	float PunchMass = 14.f;

	// ----- Dev tools -----

	/** Ragdoll and push keys, debug overlay and the tuning reload command are active. On until launch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dev tools")
	bool bDevTools = true;

	/**
	 * True if every value is usable. Otherwise false, with OutReason naming the first bad value.
	 * Rejects non-finite values, non-positive speeds, walk above jog or jog above sprint, ramp and
	 * blend times of 0 or less, a slope outside (0, 90), a field of view outside [60, 120] and camera
	 * arm, lag and smoothing values outside their documented ranges, negative or non-finite body
	 * strengths and ragdoll and hit times outside their ranges.
	 */
	bool IsValid(FString& OutReason) const;
};
