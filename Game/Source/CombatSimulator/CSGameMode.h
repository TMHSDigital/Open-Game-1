#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UObject/ObjectKey.h"
#include "CSGameMode.generated.h"

class ACSCharacter;
class ACSTargetDummy;
class UCSMovementTuning;

/**
 * The game's default game mode (GlobalDefaultGameMode in DefaultEngine.ini), used by every map that
 * does not override it: L_Plane, L_MovementGym and L_CaptureStage.
 *
 * Spawns ACSCharacter for the player and gives it the game's tuning asset (CharacterTuning, set in
 * DefaultGame.ini), so the character in play runs on DA_MovementTuning and F5 or cs.Tuning.Reload
 * re-read that asset. Mesh, animation and input come from ACSCharacter's own config section.
 *
 * Respawn: when a player's pawn is destroyed, or falls below RespawnKillZ (off the edge of the plane),
 * the player gets a new character at a PlayerStart at the start of the next frame. The new character
 * is spawned before the frame's ticks begin, as at the start of play: spawned in the middle of a frame,
 * its physical animation could tick while the engine is blending the mesh back from physics.
 *
 * Reset: binds each spawned character's dev tools reset request (Backspace) to ResetPlayground, which
 * puts every physics prop back where it was when the map loaded, replaces every target dummy with a
 * fresh one on its spot, removes the thrown balls and respawns the players.
 *
 * Dummies follow the player's tuning: whenever a player's character applies its tuning (a slider on
 * the tuning tab, F5, a respawn), every ACSTargetDummy gets the same changes, so a hit feels the same
 * on both.
 *
 * Map switch: binds each spawned character's dev tools map switch request (F2, gated by bDevTools in
 * UCSDevTools) and opens the next map in DevSwitchMaps (DefaultGame.ini), so the plugin knows no maps.
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

	/**
	 * Maps the dev map switch key (F2) goes through in order, wrapping: long package names such as
	 * /Game/Maps/L_Plane. A packaged build can only open maps that are in MapsToCook.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Dev tools")
	TArray<FString> DevSwitchMaps;

	/** A player's pawn below this height, cm, is destroyed and the player respawns at a PlayerStart. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS")
	float RespawnKillZ = -5000.f;

	/**
	 * The map after CurrentMap in DevSwitchMaps (the first after the last); the first map when
	 * CurrentMap is not listed; empty when the list is empty. CurrentMap may be a long or short package
	 * name and may carry the editor's PIE prefix.
	 */
	FString GetMapSwitchTarget(const FString& CurrentMap) const;

	/** Opens GetMapSwitchTarget for the current map; what F2 does. Returns the map opened (empty: none). */
	UFUNCTION(BlueprintCallable, Category = "CS|Dev tools")
	FString SwitchMap();

	/**
	 * Puts the play space back: physics props to where they were when the map loaded, at rest; a
	 * fresh dummy on every dummy's spot; no balls; every player at a PlayerStart. The dummies and
	 * players reappear at the start of the next frame. What Backspace does.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Dev tools")
	void ResetPlayground();

	/** Gives every target dummy the tuning changes Player runs on. */
	void SyncDummies(const ACSCharacter* Player);

	/** The map the last SwitchMap opened; empty before any. */
	const FString& GetLastMapSwitchTarget() const { return LastMapSwitchTarget; }

	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UFUNCTION()
	void HandlePawnDestroyed(AActor* DestroyedActor);
	void HandleMapSwitchRequested();
	void HandleResetRequested();
	void HandleWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	/** The player controller each spawned pawn was made for, so its destruction can respawn the player. */
	TMap<TObjectKey<AActor>, TWeakObjectPtr<AController>> PawnControllers;

	/** Players whose pawn was destroyed, respawned at the start of the next frame. */
	TArray<TWeakObjectPtr<AController>> PendingRespawns;

	FDelegateHandle WorldTickStartHandle;

	/** Where each physics prop was when the map loaded. */
	TMap<TWeakObjectPtr<AActor>, FTransform> PropHomes;

	/** A dummy removed by a reset, to be spawned again at the start of the next frame. */
	struct FDummyRespawn
	{
		TSubclassOf<ACSTargetDummy> Class;
		FTransform Home;
		TWeakObjectPtr<UCSMovementTuning> Tuning;
		TArray<FName> Tags;
	};
	TArray<FDummyRespawn> PendingDummies;

	FString LastMapSwitchTarget;
};
