#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CSCaptureTypes.h"
#include "CSCaptureSubsystem.generated.h"

class ACameraActor;

/**
 * Runs one fixed-camera capture job, then quits the game. Exists only when the command line has
 * -CSCapture=<job.json> (see ShouldCreateSubsystem), so normal play and the editor never see it.
 *
 * A game instance subsystem is created automatically with the game instance; no Blueprint or level
 * setup is needed. It drives itself from the core ticker (FTSTicker), once per engine frame.
 *
 * Run it with tools/ue/capture.ps1, which adds -benchmark -fps=30: a fixed timestep, so game time
 * advances exactly 1/30 s per frame however slowly frames render. The settle time before each
 * image is therefore the same on every machine.
 */
UCLASS()
class CSCAPTURE_API UCSCaptureSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	enum class EPhase : uint8 { WaitForWorld, Shots, Flush, Done };

	bool Tick(float DeltaTime);
	void Fail(const FString& Reason);
	void Finish();

	void BeginShot(const FCSCaptureShot& Shot);
	void HidePlayer(bool bHide) const;
	/** Points the player's view at the capture camera, or at its own pawn for a "player" shot (and sets the aim when bSetRotation). */
	void ApplyView(const FCSCaptureShot& Shot, bool bSetRotation);
	void RequestImage(const FCSCaptureShot& Shot);
	void WriteResult(bool bSuccess, const FString& Message) const;

	FCSCaptureJob Job;
	FString JobPath;
	FTSTicker::FDelegateHandle TickHandle;

	EPhase Phase = EPhase::WaitForWorld;
	/** Game seconds spent in the current phase (fixed step, so frame count / 30). */
	float PhaseTime = 0.f;
	int32 ShotIndex = 0;
	/** Frames to wait after a shot request so the image is taken before the camera moves. */
	int32 ShotCooldownFrames = 0;
	TArray<FString> WrittenFiles;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	bool bFailed = false;
	FString FailReason;
};
