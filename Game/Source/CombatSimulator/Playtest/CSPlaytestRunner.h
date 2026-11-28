#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CSPlaytestRunner.generated.h"

class ACameraActor;
class ACSCharacter;
class ACSGameMode;
class ACSTargetDummy;
class AHUD;
class APlayerController;
class UCanvas;

/** Where a play-test looks from. */
enum class ECSReviewCamera : uint8
{
	/** The player's own camera. */
	Player,
	/** From the side of the action, the whole body in view. */
	Side,
	/** From in front of the player, looking back at it. */
	Front,
	/** From the side of the dummy, following it (a knockdown can throw it far from the player). */
	SideOfDummy,
	/** Close, from in front and to the player's lead (left) side: the strike and the face it lands on. */
	Quarter,
};

class UCSPlaytestRunner;

/** What a step can reach: the world, the player and the run's dummy. */
struct COMBATSIMULATOR_API FCSPlaytestContext
{
	UCSPlaytestRunner* Runner = nullptr;
	UWorld* World = nullptr;
	ACSGameMode* Mode = nullptr;
	APlayerController* Controller = nullptr;

	/** The player's character now (a run or a reset replaces it). */
	ACSCharacter* Player() const;
	/** The dummy nearest the player; null when there is none. */
	ACSTargetDummy* Dummy() const;

	/** Stands the player at Location facing Yaw (body and view). */
	void Place(const FVector& Location, float Yaw) const;
	/** Stands the player Distance from the dummy's chest, Side cm to its right of the line between them, facing the dummy. */
	void PlaceAtDummy(float Distance, float Side, float BodyYawOffset = 0.f) const;
	/** Turns the view toward the dummy (yaw only), so the player aims at it. */
	void AimAtDummy() const;
	/** Moves the player this tick: Forward and Right relative to the view, -1 to 1. */
	void Move(float Forward, float Right) const;

	/** Adds a fact to the step's result line (the log and the review summary). */
	void Note(const FString& Key, const FString& Value) const;
	void Note(const FString& Key, float Value) const;
	/**
	 * An expectation of the step: noted as "check: <Name>" = "ok" or "FAILED". playtest.ps1 lists
	 * every failed check in the summary and exits 2, so a run that played but went wrong is not a pass.
	 */
	void Check(const FString& Name, bool bOk) const;
};

/** One step of a scenario: a labelled segment of the review, or a silent setup step. */
struct COMBATSIMULATOR_API FCSPlaytestStep
{
	/** Shown on screen and in the log; a step with no label does not start a segment. */
	FString Label;
	float Seconds = 1.f;
	ECSReviewCamera Camera = ECSReviewCamera::Side;
	/** The world's time dilation during the step (below 1: slow motion, so a recording catches every pose). Seconds are game time. */
	float TimeDilation = 1.f;
	TFunction<void(const FCSPlaytestContext&)> OnStart;
	TFunction<void(const FCSPlaytestContext&, float /* Seconds in */, float /* DeltaTime */)> OnTick;
	TFunction<void(const FCSPlaytestContext&)> OnEnd;
};

/**
 * Plays scripted scenarios through the game for review: a component of ACSGameMode that does
 * nothing unless the game was started with -CSPlaytest=<scenario,...> (or "all").
 *
 * Each scenario is a list of steps that drive the player's character the way the keys would
 * (movement, gait, attacks, the dev throw and ragdoll) and set up runs with dummies to fight. A
 * review camera frames the whole body from the side or the front, which the player's camera does
 * not. The step's label is drawn on screen, so every frame of a recording says what it shows.
 *
 * Events go to -CSPlaytestLog=<path> (default <Saved>/Playtest/events.jsonl), one JSON object a
 * line, each with the real time ("utc_ms") so a recording can be cut to match: "start",
 * "segment" (index, label, camera), "result" (index, label, the step's notes) and "end". The game
 * quits after the last step. tools/ue/playtest.ps1 runs the game, records it and cuts the clips.
 */
UCLASS()
class COMBATSIMULATOR_API UCSPlaytestRunner : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSPlaytestRunner();

	/** Every scenario name, in the order "all" plays them. */
	static TArray<FString> ScenarioNames();

	/** Scenarios that need their own map, so "all" leaves them out: gym (L_MovementGym). */
	static TArray<FString> MapScenarioNames() { return {TEXT("gym")}; }

	/** The steps of a scenario; empty for an unknown name. */
	static TArray<FCSPlaytestStep> BuildScenario(const FString& Name);

	/** Scenario names from a -CSPlaytest value: "all", or names separated by commas. Unknown names go to OutUnknown. */
	static TArray<FString> ParseScenarios(const FString& Value, TArray<FString>& OutUnknown);

	/** One line of the event log (no newline). */
	static FString MakeEventLine(const FString& Event, int32 Index, const FString& Label, const TMap<FString, FString>& Data, double RealSeconds);

	/** Starts playing these scenarios now (the command line does this at BeginPlay). */
	void Begin(const TArray<FString>& Scenarios, const FString& LogPath, bool bQuitWhenDone);

	bool IsRunning() const { return bRunning; }
	int32 GetStepIndex() const { return StepIndex; }
	int32 GetStepCount() const { return Steps.Num(); }
	const TArray<FString>& GetLogLines() const { return LogLines; }

	/** Facts noted by the step under way. */
	TMap<FString, FString>& GetNotes() { return Notes; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FCSPlaytestContext MakeContext() const;
	void StartStep(int32 Index);
	void EndStep();
	void Finish();
	void Log(const FString& Event, const FString& Label, const TMap<FString, FString>& Data);
	void UpdateCamera(float DeltaTime);
	void DrawLabel(AHUD* HUD, UCanvas* Canvas);

	TArray<FCSPlaytestStep> Steps;
	int32 StepIndex = INDEX_NONE;
	int32 SegmentIndex = 0;
	float StepSeconds = 0.f;
	/** Seconds to wait after BeginPlay before the first step: the character and the view settle. */
	float WarmupSeconds = 2.f;
	bool bRunning = false;
	bool bQuitWhenDone = false;
	FString LogPath;
	TArray<FString> LogLines;
	TMap<FString, FString> Notes;
	FString CurrentLabel;
	/** The last result, shown under the label for a moment. */
	FString LastResult;
	double StartRealSeconds = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> ReviewCamera;
	ECSReviewCamera CameraMode = ECSReviewCamera::Player;
	/** The side axis of the review camera, fixed when a step starts so the view does not swing. */
	FVector CameraSide = FVector::RightVector;
	bool bCameraPlaced = false;
	FDelegateHandle HudHandle;
};
