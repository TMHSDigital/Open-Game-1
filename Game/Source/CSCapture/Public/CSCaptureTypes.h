// Data types for a fixed-camera capture job. tools/ue/capture.ps1 turns docs/captures/cameras.json
// into a job file and passes its path on the command line (-CSCapture=<job.json>);
// UCSCaptureSubsystem reads it with FJsonObjectConverter, which matches JSON keys to UPROPERTY
// names (case-insensitive).

#pragma once

#include "CoreMinimal.h"
#include "CSCaptureTypes.generated.h"

/** One still image from a fixed camera. */
USTRUCT()
struct CSCAPTURE_API FCSCaptureShot
{
	GENERATED_BODY()

	/** Output file stem: the camera name, for example "stage-overview". */
	UPROPERTY()
	FString Name;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Pitch, yaw, roll in degrees. */
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	/** Horizontal field of view in degrees. */
	UPROPERTY()
	float Fov = 70.f;

	/** Image size in pixels (rendered offscreen, independent of the window size). */
	UPROPERTY()
	int32 Width = 1920;

	UPROPERTY()
	int32 Height = 1080;

	/** Hide the player's pawn so it does not block the view. */
	UPROPERTY()
	bool bHidePlayer = true;
};

/** Everything one UE process captures. One map per process (the map is on the command line). */
USTRUCT()
struct CSCAPTURE_API FCSCaptureJob
{
	GENERATED_BODY()

	/** Absolute folder for the images and result.json. */
	UPROPERTY()
	FString OutputDir;

	/** Game seconds to run before the first image, so lighting and exposure settle. */
	UPROPERTY()
	float SettleSeconds = 3.f;

	/** Console commands applied at startup, e.g. "r.MotionBlurQuality 0". */
	UPROPERTY()
	TArray<FString> Commands;

	UPROPERTY()
	TArray<FCSCaptureShot> Shots;
};

namespace CSCapture
{
	/** Parses a job from JSON text. Returns false and fills OutError on bad JSON or missing fields. */
	CSCAPTURE_API bool ParseJob(const FString& JsonText, FCSCaptureJob& OutJob, FString& OutError);
}
