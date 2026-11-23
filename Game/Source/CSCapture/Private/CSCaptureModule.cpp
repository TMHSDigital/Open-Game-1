#include "CSCaptureLog.h"
#include "CSCaptureTypes.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogCSCapture);

// A module with no startup logic only needs this registration line.
IMPLEMENT_MODULE(FDefaultModuleImpl, CSCapture);

namespace
{
	constexpr int32 MinSize = 64;
	constexpr int32 MaxSize = 8192;
}

bool CSCapture::ParseJob(const FString& JsonText, FCSCaptureJob& OutJob, FString& OutError)
{
	OutJob = FCSCaptureJob();
	FText FailReason;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonText, &OutJob, /*CheckFlags=*/0, /*SkipFlags=*/0, /*bStrictMode=*/false, &FailReason))
	{
		OutError = FailReason.IsEmpty() ? TEXT("invalid JSON") : FailReason.ToString();
		return false;
	}
	if (OutJob.OutputDir.IsEmpty())
	{
		OutError = TEXT("job has no OutputDir");
		return false;
	}
	if (OutJob.Shots.Num() == 0)
	{
		OutError = TEXT("job has no shots");
		return false;
	}
	for (const FCSCaptureShot& Shot : OutJob.Shots)
	{
		if (Shot.Name.IsEmpty())
		{
			OutError = TEXT("a shot has no name");
			return false;
		}
		if (Shot.Width < MinSize || Shot.Height < MinSize || Shot.Width > MaxSize || Shot.Height > MaxSize)
		{
			OutError = FString::Printf(TEXT("shot '%s': resolution %dx%d is outside %d..%d"), *Shot.Name, Shot.Width, Shot.Height, MinSize, MaxSize);
			return false;
		}
		if (!Shot.View.IsEmpty() && !Shot.IsPlayerView() && !Shot.View.Equals(TEXT("camera"), ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(TEXT("shot '%s': view '%s' is not \"camera\" or \"player\""), *Shot.Name, *Shot.View);
			return false;
		}
		if (Shot.SettleSeconds < 0.f || Shot.SettleSeconds > 60.f)
		{
			OutError = FString::Printf(TEXT("shot '%s': settleSeconds %.2f is outside 0..60"), *Shot.Name, Shot.SettleSeconds);
			return false;
		}
		if (Shot.Fov <= 0.f || Shot.Fov >= 180.f)
		{
			OutError = FString::Printf(TEXT("shot '%s': fov %.1f is outside 0..180"), *Shot.Name, Shot.Fov);
			return false;
		}
	}
	return true;
}

namespace
{
	/**
	 * Console command for authoring cameras: in PIE (or -game), fly the camera to a good spot and
	 * type "cs.Capture.DumpView my-camera". It logs an entry to paste into docs/captures/cameras.json.
	 */
	FAutoConsoleCommandWithWorldAndArgs DumpViewCommand(
		TEXT("cs.Capture.DumpView"),
		TEXT("Logs the current camera as a docs/captures/cameras.json entry. Usage: cs.Capture.DumpView <name>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			if (!PC || !PC->PlayerCameraManager)
			{
				UE_LOG(LogCSCapture, Warning, TEXT("DumpView: no player camera in this world."));
				return;
			}
			const FVector L = PC->PlayerCameraManager->GetCameraLocation();
			const FRotator R = PC->PlayerCameraManager->GetCameraRotation();
			const float Fov = PC->PlayerCameraManager->GetFOVAngle();
			const FString Name = Args.Num() > 0 ? Args[0] : TEXT("unnamed");
			const FString Map = World->GetOutermost()->GetName();
			UE_LOG(LogCSCapture, Display,
				TEXT("{ \"name\": \"%s\", \"map\": \"%s\", \"location\": [%.0f, %.0f, %.0f], \"rotation\": [%.1f, %.1f, 0], \"fov\": %.0f, \"resolution\": [1920, 1080], \"since\": \"%s\" }"),
				*Name, *Map, L.X, L.Y, L.Z, R.Pitch, R.Yaw, Fov, *FDateTime::Now().ToString(TEXT("%Y-%m-%d")));
		}));
}
