#pragma once

#include "CoreMinimal.h"
#include "CSCharacter.h"
#include "CSTargetDummy.generated.h"

/**
 * The target dummy: a second copy of the character, placed in a map on a marked spot. It has no
 * controller until it fights back: then an ACSOpponentController drives it (decision 0019). Everything that hits a character hits it the same way (stagger, ragdoll, getting up), which
 * is what makes it the test bed for combat moves.
 *
 * It differs from a player's character only in this: nothing possesses it, it uses the game's tuning
 * asset and follows the tuning changes of the player (ACSGameMode keeps them in step), it is tinted
 * so the two are told apart, and it remembers where it was placed so a reset can put it back.
 */
UCLASS()
class COMBATSIMULATOR_API ACSTargetDummy : public ACSCharacter
{
	GENERATED_BODY()

public:
	ACSTargetDummy(const FObjectInitializer& ObjectInitializer);

	/** Where the dummy stood when play began (or where it was respawned). */
	const FTransform& GetHomeTransform() const { return HomeTransform; }

	/**
	 * Fights back on its own (ACSOpponentController), run or no run. A dummy of a run fights back
	 * when the run is set to (FCSRunSetup::Opponents) while the run is on, whatever this says.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Opponent")
	bool bFightsBack = false;

	/** How long it draws its fist back before a punch when it is not in a run, seconds (a run's dummy uses the run's). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Opponent", meta = (ClampMin = "0", Units = "s"))
	float OpponentWindupSeconds = 0.7f;

	/** Sets bFightsBack; fighting needs a controller, which is made the first time. */
	void SetFightsBack(bool bInFightsBack);

	/** It should fight now: bFightsBack, or a dummy of a run that is on and set to fight back. */
	bool WantsToFight() const;

	/** Its wind-up now: the run's when it is a run's dummy in a run set to fight back, else OpponentWindupSeconds. */
	float GetOpponentWindupSeconds() const;

	/** Colour multiplied into the body's material, when the material has a colour parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS")
	FLinearColor Tint = FLinearColor(0.85f, 0.45f, 0.2f);

	virtual void BeginPlay() override;

private:
	/** The run's director, when this is a dummy of a run (tag RunDummy). */
	const class UCSRunDirector* GetRunDirector() const;
	/** Makes the controller that fights for it, if it has none. */
	void EnsureController();

	FTransform HomeTransform = FTransform::Identity;
};
