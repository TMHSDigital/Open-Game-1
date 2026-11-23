#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CSGameMode.generated.h"

class UCSMovementTuning;

/**
 * The game's default game mode (GlobalDefaultGameMode in DefaultEngine.ini), used by every map that
 * does not override it: L_Plane, L_MovementGym and L_CaptureStage.
 *
 * Spawns ACSCharacter for the player and gives it the game's tuning asset (CharacterTuning, set in
 * DefaultGame.ini), so the character in play runs on DA_MovementTuning and F5 or cs.Tuning.Reload
 * re-read that asset. Mesh, animation and input come from ACSCharacter's own config section.
 */
UCLASS(Config = Game)
class COMBATSIMULATOR_API ACSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACSGameMode();

	/** Tuning given to a spawned ACSCharacter that has none of its own. Empty: the class defaults. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS")
	TSoftObjectPtr<UCSMovementTuning> CharacterTuning;

	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
};
