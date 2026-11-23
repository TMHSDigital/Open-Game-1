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

	ECSLocomotionState GetLocomotionState() const { return State; }
	float GetGroundSpeed() const { return GroundSpeed; }

	/** The AnimGraph pose a state plays. */
	static ECSLocomotionPose PoseForState(ECSLocomotionState InState);

	/** Ground state for a horizontal speed (cm/s), from the gait speeds. */
	static ECSLocomotionState SelectGroundState(float Speed, bool bCrouching, float WalkSpeed, float JogSpeed, float SprintSpeed, float CrouchSpeed);

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	/** Horizontal speed, cm/s. Drives the Ground and Crouch blend spaces. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CS|Animation")
	float GroundSpeed = 0.f;

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
