#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "CSOpponentController.generated.h"

class ACSCharacter;
class ACSTargetDummy;

/** What an opponent is doing. */
UENUM(BlueprintType)
enum class ECSOpponentState : uint8
{
	/** Not fighting, or nobody to fight within FightRange: stands. */
	Idle,
	/** Walking in until AttackDistance. */
	Close,
	/** A punch (or a pair) is under way: the wind-up is the tell. */
	Attack,
	/** Stands for a moment after its punches. */
	Recover,
	/** Steps back out of reach. */
	BackOff,
	/** Moves around the target at about CircleDistance, then closes in again. */
	Circle,
	/** The target is down: keeps WaitDistance away and does not hit it. */
	Wait,
	/** Itself staggered, down or getting up: does nothing until it stands again. */
	Hurt,
};

/**
 * The brain of a dummy that fights back (decision 0019): a small state machine on a pattern the
 * player can learn, adapted from the telegraphed opponent of slap-boxing-simulator (Game-Systems).
 *
 * The cycle: close in to AttackDistance, punch (the Pattern says how many punches this time, then
 * the next entry), recover, back off, circle, and close in again. The punch is the player's own
 * (UCSStrikeComponent, with the same target assist and step-in); only its wind-up is the
 * opponent's (WindupSeconds), drawn out so it can be read: that is the tell, and the one knob that
 * makes it easier or harder. It never hits a target that is down (Wait), and stops while it is
 * itself staggered or down (Hurt).
 *
 * It faces its target through the combat stance like the player: the focal point is the target's
 * upper chest, so the control rotation, the body's facing and the punch's aim all follow it.
 *
 * It fights while its dummy wants to (ACSTargetDummy::WantsToFight): a dummy of a run set to fight
 * back while the run is on, or a dummy told to fight (bFightsBack).
 */
UCLASS()
class COMBATSIMULATOR_API ACSOpponentController : public AAIController
{
	GENERATED_BODY()

public:
	ACSOpponentController();

	ECSOpponentState GetOpponentState() const { return OpponentState; }
	float GetStateSeconds() const { return StateSeconds; }

	/** The character it fights: the nearest player-controlled character; null when there is none. */
	ACSCharacter* FindOpponentTarget() const;

	/** Punches it has started since play began. */
	int32 GetPunchesThrown() const { return PunchesThrown; }

	/** How many punches each attack throws, in turn, round and round. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Opponent")
	TArray<int32> Pattern = {1, 1, 2};

	/** Closer than this (centre to centre, seen from above) it starts an attack, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Opponent", meta = (Units = "cm"))
	float AttackDistance = 140.f;

	/** It fights a target within this far, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Opponent", meta = (Units = "cm"))
	float FightRange = 1500.f;

	/** Farther than this it jogs in; nearer it walks, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Opponent", meta = (Units = "cm"))
	float JogDistance = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Opponent", meta = (Units = "s"))
	float RecoverSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Opponent", meta = (Units = "s"))
	float BackOffSeconds = 0.6f;

	/** Circling lasts between these, seconds (picked each time). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Opponent", meta = (Units = "s"))
	float CircleSecondsMin = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Opponent", meta = (Units = "s"))
	float CircleSecondsMax = 1.6f;

	/** The distance it circles at, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Opponent", meta = (Units = "cm"))
	float CircleDistance = 220.f;

	/** While the target is down it keeps at least this far away, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Opponent", meta = (Units = "cm"))
	float WaitDistance = 250.f;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	void Enter(ECSOpponentState NewState);
	/** Moves the pawn along Direction (flattened) this tick. */
	void MoveAlong(ACSCharacter& Self, const FVector& Direction) const;
	/** Starts the next punch of the attack; false when the strike would not start. */
	bool Punch(ACSCharacter& Self);

	ECSOpponentState OpponentState = ECSOpponentState::Idle;
	float StateSeconds = 0.f;
	int32 PatternIndex = 0;
	int32 PunchesLeft = 0;
	int32 PunchesThrown = 0;
	float CircleSign = 1.f;
	float CircleFor = 1.f;
	/** Seeded from the pawn's name, so a play-test repeats. */
	FRandomStream Random;
};
