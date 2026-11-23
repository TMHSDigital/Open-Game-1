#include "CSCaptureSubsystem.h"

#include "AssetCompilingManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HighResScreenshot.h"
#include "ImageWriteQueue.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "CSCaptureLog.h"
#include "UnrealClient.h"

namespace
{
	const TCHAR* JobSwitch = TEXT("CSCapture=");

	// Seconds each image after the first waits at its camera, for Lumen and exposure to catch up.
	constexpr float ShotSettleSeconds = 1.f;

	/**
	 * True while the editor is still compiling assets (textures, shaders, meshes) in the background.
	 * Captures run the editor binary in -game, where an uncompiled texture draws as the engine's
	 * plain white default. Shots wait for this to reach zero.
	 */
	bool IsStillCompilingAssets()
	{
#if WITH_EDITOR
		return FAssetCompilingManager::Get().GetNumRemainingAssets() > 0;
#else
		return false;
#endif
	}

	void WaitForImageWrites()
	{
		if (IImageWriteQueue* Queue = GetHighResScreenshotConfig().ImageWriteQueue)
		{
			Queue->CreateFence().Wait();
		}
	}
}

bool UCSCaptureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// GIsEditor is false in a -game run of the editor binary, true in the editor itself (and PIE).
	FString Unused;
	return !GIsEditor && FParse::Value(FCommandLine::Get(), JobSwitch, Unused) && Super::ShouldCreateSubsystem(Outer);
}

void UCSCaptureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FParse::Value(FCommandLine::Get(), JobSwitch, JobPath);
	FString JsonText;
	FString Error;
	if (!FFileHelper::LoadFileToString(JsonText, *JobPath))
	{
		Error = FString::Printf(TEXT("cannot read job file '%s'"), *JobPath);
	}
	else
	{
		CSCapture::ParseJob(JsonText, Job, Error);
	}

	// FTSTicker calls Tick once per engine frame, after the world ticked and drew. Returning true keeps it registered.
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick));

	if (!Error.IsEmpty())
	{
		Fail(Error);
		return;
	}
	IFileManager::Get().MakeDirectory(*Job.OutputDir, /*Tree=*/true);
	UE_LOG(LogCSCapture, Display, TEXT("Job '%s': %d shots, output %s"), *JobPath, Job.Shots.Num(), *Job.OutputDir);
}

void UCSCaptureSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Super::Deinitialize();
}

bool UCSCaptureSubsystem::Tick(float DeltaTime)
{
	if (Phase == EPhase::Done)
	{
		return true;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;

	if (Phase == EPhase::WaitForWorld)
	{
		// Wait until the map is playing and has a local player to own the view.
		if (!World || !World->HasBegunPlay() || !PC)
		{
			return true;
		}
		for (const FString& Command : Job.Commands)
		{
			GEngine->Exec(World, *Command);
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Camera = World->SpawnActor<ACameraActor>(Params);
		Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
		PC->SetViewTarget(Camera);
		// The player's own camera manager would otherwise fade or blend; the capture camera wins outright.
		PC->bAutoManageActiveCameraTarget = false;

		Phase = EPhase::Shots;
		PhaseTime = 0.f;
		ShotIndex = 0;
		BeginShot(Job.Shots[0]);
		return true;
	}

	PhaseTime += DeltaTime;

	switch (Phase)
	{
	case EPhase::Shots:
	{
		if (ShotCooldownFrames > 0)
		{
			// The request made last frame is taken during this frame's draw; then move on.
			if (--ShotCooldownFrames == 0)
			{
				++ShotIndex;
				PhaseTime = 0.f;
				if (Job.Shots.IsValidIndex(ShotIndex))
				{
					BeginShot(Job.Shots[ShotIndex]);
				}
				else
				{
					HidePlayer(false);
					Phase = EPhase::Flush;
				}
			}
			break;
		}
		const FCSCaptureShot& Shot = Job.Shots[ShotIndex];
		float Wait = ShotIndex == 0 ? Job.SettleSeconds : ShotSettleSeconds;
		if (Shot.SettleSeconds > 0.f)
		{
			Wait = ShotIndex == 0 ? FMath::Max(Wait, Shot.SettleSeconds) : Shot.SettleSeconds;
		}
		if (PhaseTime >= Wait && !IsStillCompilingAssets())
		{
			// The pawn may have spawned after BeginShot; apply the view and hide flag again right before the image.
			ApplyView(Shot, /*bSetRotation=*/false);
			HidePlayer(Shot.bHidePlayer && !Shot.IsPlayerView());
			RequestImage(Shot);
			ShotCooldownFrames = 2;
		}
		break;
	}

	case EPhase::Flush:
		// One more frame so the last request is drawn, then wait for the PNG writes.
		if (PhaseTime > DeltaTime * 2.f)
		{
			Finish();
		}
		break;

	default:
		break;
	}
	return true;
}

void UCSCaptureSubsystem::BeginShot(const FCSCaptureShot& Shot)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	for (const FString& Command : Shot.Commands)
	{
		UE_LOG(LogCSCapture, Display, TEXT("Shot %s: command '%s'"), *Shot.Name, *Command);
		GEngine->Exec(World, *Command);
	}
	HidePlayer(Shot.bHidePlayer && !Shot.IsPlayerView());
	if (Camera)
	{
		Camera->SetActorLocationAndRotation(Shot.Location, Shot.Rotation);
		Camera->GetCameraComponent()->SetFieldOfView(Shot.Fov);
	}
	ApplyView(Shot, /*bSetRotation=*/true);
	if (Shot.IsPlayerView())
	{
		UE_LOG(LogCSCapture, Display, TEXT("Shot %s: player view, control rotation %s, %dx%d"), *Shot.Name, *Shot.Rotation.ToString(), Shot.Width, Shot.Height);
	}
	else
	{
		UE_LOG(LogCSCapture, Display, TEXT("Shot %s: %s %s fov %.0f, %dx%d"), *Shot.Name, *Shot.Location.ToString(), *Shot.Rotation.ToString(), Shot.Fov, Shot.Width, Shot.Height);
	}
}

void UCSCaptureSubsystem::ApplyView(const FCSCaptureShot& Shot, bool bSetRotation)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}
	if (!Shot.IsPlayerView())
	{
		if (Camera && PC->GetViewTarget() != Camera)
		{
			PC->SetViewTarget(Camera);
		}
		return;
	}
	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		UE_LOG(LogCSCapture, Warning, TEXT("Shot %s: player view, but the player has no pawn"), *Shot.Name);
		return;
	}
	if (PC->GetViewTarget() != Pawn)
	{
		PC->SetViewTarget(Pawn);
	}
	if (bSetRotation)
	{
		PC->SetControlRotation(Shot.Rotation);
	}
}

void UCSCaptureSubsystem::HidePlayer(bool bHide) const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (APawn* Player = PC ? PC->GetPawn() : nullptr)
	{
		Player->SetActorHiddenInGame(bHide);
	}
}

void UCSCaptureSubsystem::RequestImage(const FCSCaptureShot& Shot)
{
	// Always pass the extension: names with dots would otherwise read as having one.
	const FString FilePath = FPaths::Combine(Job.OutputDir, Shot.Name) + TEXT(".png");
	// The same path as the HighResShot console command: the next viewport draw renders an
	// offscreen Width x Height image (independent of the window size) and queues a PNG write.
	FHighResScreenshotConfig& Config = GetHighResScreenshotConfig();
	Config.SetFilename(FilePath);
	if (!Config.SetResolution(Shot.Width, Shot.Height))
	{
		Fail(FString::Printf(TEXT("%dx%d exceeds the GPU's maximum texture size"), Shot.Width, Shot.Height));
		return;
	}
	WrittenFiles.Add(FilePath);
}

void UCSCaptureSubsystem::Fail(const FString& Reason)
{
	UE_LOG(LogCSCapture, Error, TEXT("Capture failed: %s"), *Reason);
	bFailed = true;
	FailReason = Reason;
	Finish();
}

void UCSCaptureSubsystem::Finish()
{
	Phase = EPhase::Done;
	WaitForImageWrites();
	GetHighResScreenshotConfig().SetFilename(FString());
	WriteResult(!bFailed, bFailed ? FailReason : TEXT("ok"));
	UE_LOG(LogCSCapture, Display, TEXT("Capture finished (%s), %d images. Exiting."), bFailed ? TEXT("failed") : TEXT("ok"), WrittenFiles.Num());
	FPlatformMisc::RequestExitWithStatus(/*Force=*/false, bFailed ? 1 : 0);
}

void UCSCaptureSubsystem::WriteResult(bool bSuccess, const FString& Message) const
{
	if (Job.OutputDir.IsEmpty())
	{
		return;
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("message"), Message);
	Result->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	TArray<TSharedPtr<FJsonValue>> Files;
	for (const FString& File : WrittenFiles)
	{
		Files.Add(MakeShared<FJsonValueString>(File));
	}
	Result->SetArrayField(TEXT("files"), Files);

	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Result, Writer);
	FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Job.OutputDir, TEXT("result.json")));
}
