#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Braking and slopes", meta = (UIMin = "0", UIMax = "0.9"))
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

	// ----- Ragdoll -----

	/** Seconds to blend from the settled ragdoll pose back to the movement pose. Must be positive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ragdoll", meta = (UIMin = "0.1", UIMax = "3", Units = "s"))
	float RagdollRecoverSeconds = 0.5f;

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

	// ----- Dev tools -----

	/** Ragdoll and push keys, debug overlay and the tuning reload command are active. On until launch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dev tools")
	bool bDevTools = true;

	/**
	 * True if every value is usable. Otherwise false, with OutReason naming the first bad value.
	 * Rejects non-finite values, non-positive speeds, walk above jog or jog above sprint, ramp and
	 * blend times of 0 or less, a slope outside (0, 90) and a field of view outside [60, 120].
	 */
	bool IsValid(FString& OutReason) const;
};
