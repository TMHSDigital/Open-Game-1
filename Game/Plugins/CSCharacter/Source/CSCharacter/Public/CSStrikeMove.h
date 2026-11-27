#pragma once

#include "CoreMinimal.h"
#include "CSStrikeMove.generated.h"

/** A strike a CS character can throw (decision 0020). */
UENUM(BlueprintType)
enum class ECSStrikeMove : uint8
{
	/** Lead (left) hand, straight: quick and light. */
	Jab,
	/** Rear (right) hand, straight: the full punch. */
	Straight,
	/** Left hand, swung around from the side. */
	HookLeft,
	/** Right hand, swung around from the side. */
	HookRight,
	/** Rear (right) leg, a front kick to the body: slow and heavy. */
	Kick,
};

/**
 * One strike as data: how it scales the punch tuning (UCSMovementTuning, group Punch) and what it
 * costs. The player and the opponent read the same rows (UCSStrikeComponent::Moves), so a move is
 * tuned in one place. Adapted from the data-defined move of slap-boxing-simulator (Game-Systems).
 */
USTRUCT(BlueprintType)
struct CSCHARACTER_API FCSStrikeMoveData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike")
	ECSStrikeMove Move = ECSStrikeMove::Straight;

	/** Thrown with the right arm or leg (else the left). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike")
	bool bRightSide = true;

	/** A kick: the leg is simulated and the foot steered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike")
	bool bLeg = false;

	/** A hook: the fist comes around from the side instead of straight out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike")
	bool bHook = false;

	/** Times PunchWindupSeconds, PunchStrikeSeconds, PunchRecoverSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike")
	float WindupScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike")
	float StrikeScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike")
	float RecoverScale = 1.f;

	/** Times PunchReach, PunchSpeed and PunchMass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike")
	float ReachScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike")
	float SpeedScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike")
	float MassScale = 1.f;

	/** Stamina it costs (UCSStaminaComponent). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike")
	float StaminaCost = 10.f;

	/**
	 * The authored clip the strike plays (decision 0025), built by
	 * Art/Characters/Base/Strikes/build_strikes.py. Without it (not found) the strike falls back to
	 * steering the fist through the physics body.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike", meta = (AllowedClasses = "/Script/Engine.AnimSequenceBase"))
	FSoftObjectPath Clip;

	/** Seconds into the clip the fist or foot can first land, and the last (its peak), from strikes.json. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike", meta = (ClampMin = "0", Units = "s"))
	float ActiveStart = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Strike", meta = (ClampMin = "0", Units = "s"))
	float ActiveEnd = 0.f;

	/** The rows every character starts with, one per move, in ECSStrikeMove order. */
	static TArray<FCSStrikeMoveData> Defaults();

	/** "jab", "straight", "left hook", "right hook", "kick". */
	static FString Describe(ECSStrikeMove Move);
};
