#pragma once

#include "CoreMinimal.h"
#include "CSRunSetup.h"
#include "CSRunStats.h"
#include "Components/ActorComponent.h"
#include "CSRunDirector.generated.h"

class ACSBallLauncher;
class ACSCharacter;
class ACSGameMode;
class ACSHitBall;
class AHUD;
class UCanvas;

/** Where a map is in the life of a run. */
enum class ECSRunState : uint8
{
	/** No run: the map as it was made, free play. */
	Idle,
	/** A run is on: its time and hits are counted. */
	Running,
	/** The run is over: its results stand until it is started again or left. */
	Ended,
};

DECLARE_MULTICAST_DELEGATE_OneParam(FCSRunStateChanged, ECSRunState /* New */);

/**
 * Runs a simulation run on the map being played: a component of ACSGameMode.
 *
 * A run is an FCSRunSetup put into the world. StartRun puts the play space back (props, balls, the
 * player at the start), replaces the map's dummies with the setup's, adds its launchers and starts
 * counting. A setup for another map opens that map first; the run starts there on its own
 * (UCSRunStore carries it across). The run ends by EndRun, by its time limit or by its knockdown
 * goal; the world plays on, but nothing more is counted and the Run tab opens with the results.
 * Rerun starts the same setup again; LeaveRun goes back to the map as it was made.
 *
 * Counting: the director listens to the character plugin's hit and throw events for its own world.
 * A hit on the player's character is taken; a hit on anyone else by the player's character, or by
 * something the player threw, is dealt; anything else is counted as between others. A punch is a
 * hit by the player's character like any other; the punches thrown and landed are counted beside
 * it from the strike component's events. Characters and what hits them know nothing about runs.
 *
 * While a run is on, one line (FCSRunStats::HudLine) is drawn on the player's screen. It is not a
 * dev tool: it shows with dev tools off.
 */
UCLASS()
class COMBATSIMULATOR_API UCSRunDirector : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSRunDirector();

	/**
	 * Starts Setup: here when its map is the one being played (or it names none), otherwise by
	 * opening that map. False, with the cause in OutReason, when the map is not one of the game's
	 * run maps or there is no world.
	 */
	bool StartRun(const FCSRunSetup& Setup, FString& OutReason);

	/** Ends the run that is on. Does nothing in any other state. */
	void EndRun(ECSRunEnd Reason = ECSRunEnd::Manual);

	/** Starts the last setup again, from Running or Ended. False when there has been no run. */
	bool Rerun();

	/** Back to the map as it was made: its own dummies, no launchers of the run, nothing counted. */
	void LeaveRun();

	ECSRunState GetState() const { return State; }
	ECSRunEnd GetEndReason() const { return EndReason; }

	/** The setup of the run that is on or just ended. */
	const FCSRunSetup& GetSetup() const { return Setup; }
	const FCSRunStats& GetStats() const { return Stats; }

	/** One line for the top of the Run tab: "No run: free play.", "Run on: ...", "Run over (time limit reached): ...". */
	FString GetStatusText() const;

	/** Other is a ball the player threw in this run (also after the character that threw it is gone). */
	bool IsPlayerBall(const AActor* Other) const;

	/** Which side of the run a hit on Victim by Other belongs to. */
	ECSRunHitSide Classify(const ACSCharacter* Victim, const AActor* Other) const;

	FCSRunStateChanged OnStateChanged;

	/**
	 * Real seconds between the end of a run and its results opening: the player may still be
	 * clicking (an attack), and the clicks would land on the results.
	 */
	UPROPERTY(EditAnywhere, Category = "CS|Runs", meta = (ClampMin = "0", Units = "s"))
	float ResultsDelaySeconds = 1.f;

	/** The results are waiting to open (the run has just ended). */
	bool AreResultsPending() const { return ResultsAt > 0.0; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	ACSGameMode* GetMode() const;
	ACSCharacter* GetPlayerCharacter() const;
	void SetState(ECSRunState NewState);
	void BeginRunHere(const FCSRunSetup& InSetup);
	void RemoveLaunchers();
	void HandleContact(ACSCharacter* Victim, AActor* Other, const FCSContactRecord& Hit);
	void HandleThrow(ACSHitBall* Ball, AActor* Thrower);
	void HandleStrikeStarted(ACSCharacter* Attacker);
	void HandleStrikeLanded(ACSCharacter* Attacker, AActor* Victim, ECSContactResult Result);
	void DrawHud(AHUD* HUD, UCanvas* Canvas);
	void OpenResults();

	ECSRunState State = ECSRunState::Idle;
	ECSRunEnd EndReason = ECSRunEnd::None;
	FCSRunSetup Setup;
	FCSRunStats Stats;
	bool bHasSetup = false;
	/** Real time the results open; 0 when none are waiting. */
	double ResultsAt = 0.0;
	/** A run carried over from another map, started on the first tick (the player exists by then). */
	bool bCheckPending = true;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACSBallLauncher>> Launchers;

	/**
	 * The balls the player threw in this run. A ball's Owner is cleared when its thrower is destroyed
	 * (a fall off the map and a respawn), so the owner alone would lose whose ball it was.
	 */
	TSet<TWeakObjectPtr<const AActor>> PlayerBalls;

	FDelegateHandle ContactHandle;
	FDelegateHandle ThrowHandle;
	FDelegateHandle StrikeStartedHandle;
	FDelegateHandle StrikeLandedHandle;
	FDelegateHandle HudHandle;
};
