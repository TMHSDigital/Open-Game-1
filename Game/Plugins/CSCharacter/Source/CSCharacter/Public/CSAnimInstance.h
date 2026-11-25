#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "CSAnimInstance.generated.h"

/** What the character is doing, as the animation shows it. */
UENUM(BlueprintType)
enum class ECSLocomotionState : uint8
{
	Idle,
	Walk,
	Jog,
	Sprint,
	CrouchIdle,
	CrouchWalk,
	JumpStart,
	Fall,
	Land,
};

/**
 * The pose an animation graph plays for a state: the child index of its "Blend Poses by int" node,
 * in this order. Ground and Crouch are speed blend spaces (idle to sprint, crouch idle to crouch
 * walk), so Idle, Walk, Jog and Sprint share Ground and blend by GroundSpeed.
 */
UENUM(BlueprintType)
enum class ECSLocomotionPose : uint8
{
	Ground,
	Crouch,
	JumpStart,
	Fall,
	Land,
};

/** How far a locomotion clip travels: what turns a ground speed into a play rate. */
USTRUCT(BlueprintType)
struct FCSStrideSample
{
	GENERATED_BODY()

	/** Ground speed the clip covers at play rate 1 on this character, cm/s. 0 for a clip that stays in place (idle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Animation")
	float StrideSpeed = 0.f;

	/** Length of the clip, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Animation")
	float Seconds = 1.f;
};

/**
 * Animation instance for a humanoid character: reads the owner's movement once per animation update
 * and picks a locomotion state. An animation Blueprint derived from this class plays the pose for
 * PoseIndex and feeds GroundSpeed to its blend spaces.
 *
 * Reads movement in NativeUpdateAnimation, which runs with the mesh tick. ACharacter makes the mesh
 * tick after the movement component, so the values are this frame's movement, whatever order the
 * character's own Tick runs in.
 *
 * Gait speeds come from the owning ACSCharacter's tuning (UCSMovementTuning class defaults for any
 * other owner), so states follow tuning changes. State rules:
 * - on the ground, standing: Idle below half the walk speed, then the nearest gait speed (Walk, Jog,
 *   Sprint, split at the midpoints);
 * - crouched: CrouchIdle below half the crouch speed, else CrouchWalk;
 * - leaving the ground moving up (a jump): JumpStart until the top of the jump or JumpStartSeconds,
 *   then Fall; leaving it moving down (a ledge): Fall at once;
 * - touching down slower than half the walk speed: Land for LandSeconds, cut short as soon as the
 *   character moves faster than that; a faster touchdown goes straight to the ground state.
 *
 * Feet and ground: the Ground and Crouch blend spaces are laid out by gait, not by speed (X 0 idle,
 * 1 walk, 2 jog, 3 sprint; crouch 0 idle, 1 walk), with every sample at rate 1. Each update
 * LocomotionAxis is where the ground speed falls between the gait speeds in use, and
 * LocomotionPlayRate is the ground speed divided by the speed the blended clips cover at rate 1
 * (GroundStride, CrouchStride), so the feet match the ground whatever the gait speeds are tuned to.
 *
 * Stride: a clip far from the wanted speed would play visibly fast or slow, so the rate is kept
 * inside a band and the rest is made up by the length of the steps (SplitStride, StrideScale, read
 * by FCSAnimNode_StrideScale in the graph). Rate times stride is always the rate the feet need.
 */
UCLASS(Blueprintable)
class CSCHARACTER_API UCSAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Longest time JumpStart shows before Fall, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Animation")
	float JumpStartSeconds = 0.35f;

	/** How long Land shows after touching down while standing still, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Animation")
	float LandSeconds = 0.4f;

	/** How fast LegYaw follows the one wanted, 1/s (FInterpTo). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Animation")
	float LegYawInterpSpeed = 10.f;

	/** How fast the stride scale follows the one wanted, 1/s (FInterpTo): about a fifth of a second to get there. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Animation")
	float StrideInterpSpeed = 12.f;

	/**
	 * Stride of each sample of the Ground blend space, in axis order: idle, walk, jog, sprint.
	 * Filled on the animation Blueprint's class defaults by setup_animation.py. Empty: rate 1.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Animation")
	TArray<FCSStrideSample> GroundStride;

	/** Stride of each sample of the Crouch blend space: crouch idle, crouch walk. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Animation")
	TArray<FCSStrideSample> CrouchStride;

	ECSLocomotionState GetLocomotionState() const { return State; }
	float GetGroundSpeed() const { return GroundSpeed; }
	float GetLocomotionAxis() const { return LocomotionAxis; }
	float GetLocomotionPlayRate() const { return LocomotionPlayRate; }
	float GetStrideScale() const { return StrideScale; }
	float GetLegYaw() const { return LegYaw; }

	/**
	 * Facing one way and moving another (MoveAngle: the way of movement from the facing, degrees,
	 * positive to the right): how far to turn the legs, at most MaxYaw either way, and whether the
	 * steps play backward. Up to 100 degrees the legs turn toward the movement; beyond that the
	 * character is backing off, the steps play in reverse, and the legs turn by what is left of the
	 * angle from straight back.
	 */
	static float ComputeLegYaw(float MoveAngle, float MaxYaw, bool& bOutBackward);

	/**
	 * Blend position and play rate for a ground speed.
	 * GaitSpeeds are the speeds at axis 1, 2, ... (axis 0 is standing still); Stride has one entry
	 * per axis position, starting at 0. OutAxis runs linearly between neighbouring gait speeds and
	 * stops at the last. The return value is GroundSpeed divided by the speed the two neighbouring
	 * clips cover together at rate 1, clamped to [RateMin, RateMax]; 1 when that speed is zero
	 * (standing still) or Stride is too short.
	 *
	 * The blended speed: a blend space plays its samples in step by normalised time, so with weight
	 * a on the upper clip one blended cycle takes (1-a) T0 + a T1 seconds and covers
	 * (1-a) v0 T0 + a v1 T1 centimetres (v stride speed, T clip length).
	 */
	static float ComputeLocomotion(float InGroundSpeed, TConstArrayView<float> GaitSpeeds, TConstArrayView<FCSStrideSample> Stride,
		float RateMin, float RateMax, float& OutAxis);

	/**
	 * Splits the rate the feet need (NaturalRate, from ComputeLocomotion with wide limits) between
	 * play rate and stride. The rate is held inside [RateMin, RateMax] and the stride takes the
	 * rest, inside [ScaleMin, ScaleMax]; whatever the stride cannot take goes back to the rate.
	 * Returns the play rate and sets OutStrideScale; their product is NaturalRate. A rate of 0 or
	 * less (no stride table) is returned as it is, with a stride of 1.
	 */
	static float SplitStride(float NaturalRate, float RateMin, float RateMax, float ScaleMin, float ScaleMax, float& OutStrideScale);

	/** The AnimGraph pose a state plays. */
	static ECSLocomotionPose PoseForState(ECSLocomotionState InState);

	/** Ground state for a horizontal speed (cm/s), from the gait speeds. */
	static ECSLocomotionState SelectGroundState(float Speed, bool bCrouching, float WalkSpeed, float JogSpeed, float SprintSpeed, float CrouchSpeed);

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	/** Horizontal speed, cm/s. Drives the Ground and Crouch blend spaces. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CS|Animation")
	float GroundSpeed = 0.f;

	/** Position on the Ground or Crouch blend space axis: 0 idle, 1 walk, 2 jog, 3 sprint (crouch: 0 idle, 1 walk). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CS|Animation")
	float LocomotionAxis = 0.f;

	/** Play rate of the Ground and Crouch blend spaces (see ComputeLocomotion). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CS|Animation")
	float LocomotionPlayRate = 1.f;

	/** How much longer (above 1) or shorter the steps are than the clips' own (see SplitStride). 1 off the ground. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CS|Animation")
	float StrideScale = 1.f;

	/** How far the legs are turned from the facing toward the way they move, degrees, positive right (see ComputeLegYaw). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CS|Animation")
	float LegYaw = 0.f;

	/** Vertical speed, cm/s, up positive. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CS|Animation")
	float VerticalSpeed = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CS|Animation")
	bool bIsCrouching = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CS|Animation")
	bool bIsInAir = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CS|Animation")
	ECSLocomotionState State = ECSLocomotionState::Idle;

	/** int32(PoseForState(State)): the child the AnimGraph's "Blend Poses by int" node shows. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CS|Animation")
	int32 PoseIndex = 0;

	/** Seconds in the current state. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CS|Animation")
	float StateTime = 0.f;

private:
	void SetState(ECSLocomotionState NewState);
};
