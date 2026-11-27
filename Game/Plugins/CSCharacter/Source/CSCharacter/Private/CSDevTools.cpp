#include "CSDevTools.h"

#include "CSHitBall.h"
#include "CSCharacterLog.h"
#include "CSMovementTuning.h"
#include "CSPhysicsBodyComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* GaitName(ECSGait Gait)
	{
		switch (Gait)
		{
		case ECSGait::Walk: return TEXT("Walk");
		case ECSGait::Sprint: return TEXT("Sprint");
		case ECSGait::Crouch: return TEXT("Crouch");
		case ECSGait::Jog:
		default: return TEXT("Jog");
		}
	}

	void RunReloadCommand(const TArray<FString>& Args, UWorld* World)
	{
		TArray<FString> Messages;
		UCSDevTools::ReloadTuningInWorld(World, Messages);
		if (Messages.Num() == 0)
		{
			Messages.Add(TEXT("cs.Tuning.Reload: no CS character in this world"));
			UE_LOG(LogCSCharacter, Warning, TEXT("%s"), *Messages[0]);
		}
		if (GEngine)
		{
			for (const FString& Message : Messages)
			{
				GEngine->AddOnScreenDebugMessage(INDEX_NONE, 5.f, FColor::Yellow, Message);
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ReloadCommand(
		TEXT("cs.Tuning.Reload"),
		TEXT("Validates and reapplies the movement tuning asset of every CS character (dev tools only). A bad asset is rejected and the last good values are kept."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunReloadCommand));
}

UCSDevTools::UCSDevTools()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// After the movement component (TG_PrePhysics) has moved the character this frame.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

ACSCharacter* UCSDevTools::GetCharacter() const
{
	return Cast<ACSCharacter>(GetOwner());
}

bool UCSDevTools::ReloadTuning(ACSCharacter& Character, FString& OutMessage)
{
	const UCSMovementTuning* Source = Character.Tuning ? Character.Tuning.Get() : GetDefault<UCSMovementTuning>();
	const FString AssetName = Character.Tuning ? Character.Tuning->GetPathName() : FString(TEXT("class defaults"));
	FString Reason;
	if (!Source->IsValid(Reason))
	{
		OutMessage = FString::Printf(TEXT("%s: tuning %s rejected, %s. Keeping the last good values."), *Character.GetName(), *AssetName, *Reason);
		UE_LOG(LogCSCharacter, Warning, TEXT("%s"), *OutMessage);
		if (UCSDevTools* DevTools = Character.GetDevTools())
		{
			DevTools->SetReloadResult(false, OutMessage);
		}
		return false;
	}
	// Changes on top of the asset: the local player's character re-reads the store; any other keeps its own.
	UCSDevTools* Tools = Character.GetDevTools();
	FCSTuningOverrides Overrides = Character.GetTuningOverrides();
	FString Fitted;
	if (!Source->bDevTools)
	{
		Overrides = FCSTuningOverrides();
	}
	else if (Tools && Character.IsLocallyControlled() && Character.IsPlayerControlled())
	{
		bool bFound = false;
		if (!Tools->ReadStore(Overrides, bFound, Reason, Fitted))
		{
			OutMessage = FString::Printf(TEXT("%s: tuning changes in %s rejected, %s. Keeping the last good values."),
				*Character.GetName(), *GetOverrideStore().Describe(), *Reason);
			UE_LOG(LogCSCharacter, Warning, TEXT("%s"), *OutMessage);
			Tools->SetReloadResult(false, OutMessage);
			return false;
		}
	}
	if (!Character.TryApplyTuning(Overrides, Reason))
	{
		OutMessage = FString::Printf(TEXT("%s: tuning %s with the saved changes rejected, %s. Keeping the last good values."),
			*Character.GetName(), *AssetName, *Reason);
		UE_LOG(LogCSCharacter, Warning, TEXT("%s"), *OutMessage);
		if (Tools)
		{
			Tools->SetReloadResult(false, OutMessage);
		}
		return false;
	}
	OutMessage = FString::Printf(TEXT("%s: tuning %s reloaded."), *Character.GetName(), *AssetName);
	if (Tools)
	{
		OutMessage += TEXT(" ") + Tools->GetTuningSourceText();
	}
	OutMessage += DescribeFitted(Character, Fitted);
	UE_LOG(LogCSCharacter, Log, TEXT("%s"), *OutMessage);
	if (Tools)
	{
		Tools->SetReloadResult(true, OutMessage);
	}
	return true;
}

namespace
{
	ICSTuningOverrideStore* TestOverrideStore = nullptr;

	// Changes that are in play but could not be saved. They are kept here so a new character (a
	// respawn, a map change) or an F5 does not fall back to an older file: "in use until the game closes".
	TOptional<FCSTuningOverrides> UnsavedOverrides;

	/** What automation runs get when a test set no store: nothing stored, writes dropped. */
	struct FNullOverrideStore : ICSTuningOverrideStore
	{
		virtual bool Load(FString& OutJson) override { return false; }
		virtual bool Exists() const override { return false; }
		virtual bool Save(const FString& Json) override { return true; }
		virtual bool Clear() override { return true; }
		virtual FString Describe() const override { return TEXT("no store (automation run)"); }
	};

	const FString& ProjectVersionText();
}

ICSTuningOverrideStore& UCSDevTools::GetOverrideStore()
{
	static FCSTuningOverrideFileStore FileStore;
	static FNullOverrideStore NullStore;
	if (TestOverrideStore)
	{
		return *TestOverrideStore;
	}
	// A test run must not pick up, or write, the changes saved on the machine it runs on.
	return GIsAutomationTesting ? static_cast<ICSTuningOverrideStore&>(NullStore) : FileStore;
}

void UCSDevTools::SetOverrideStoreForTests(ICSTuningOverrideStore* Store)
{
	TestOverrideStore = Store;
	UnsavedOverrides.Reset();
}

FString UCSDevTools::DescribeFitted(const ACSCharacter& Character, const FString& Fitted)
{
	if (Fitted.IsEmpty())
	{
		return FString();
	}
	UE_LOG(LogCSCharacter, Warning, TEXT("%s: saved tuning changes made to fit this build: %s"), *Character.GetName(), *Fitted);
	return FString::Printf(TEXT(" Saved values changed to fit this build: %s."), *Fitted);
}

bool UCSDevTools::ReadStore(FCSTuningOverrides& OutOverrides, bool& bOutFound, FString& OutReason, FString& OutFitted) const
{
	OutOverrides = FCSTuningOverrides();
	OutFitted.Reset();
	if (UnsavedOverrides.IsSet())
	{
		// Newer than anything in the store, which refused them.
		OutOverrides = UnsavedOverrides.GetValue();
		bOutFound = true;
		return true;
	}
	FString Json;
	bOutFound = GetOverrideStore().Load(Json);
	if (!bOutFound)
	{
		if (GetOverrideStore().Exists())
		{
			// Say so: treating a locked file as "no changes" would silently drop them.
			bOutFound = true;
			OutReason = TEXT("the saved changes are there but could not be read");
			return false;
		}
		return true;
	}
	if (!OutOverrides.Parse(Json, OutReason))
	{
		return false;
	}
	// A row renamed or a range narrowed since the file was saved costs that one value, not the file.
	OutFitted = FString::Join(OutOverrides.FitToRanges(), TEXT("; "));
	if (const ACSCharacter* Character = GetCharacter())
	{
		OutOverrides.PruneEqualTo(Character->GetPackagedTuning());
	}
	return true;
}

bool UCSDevTools::LoadOverrides()
{
	ACSCharacter* Character = GetCharacter();
	if (!Character || !IsReloadAllowed())
	{
		return false;
	}
	FCSTuningOverrides Overrides;
	bool bFound = false;
	FString Reason;
	FString Fitted;
	if (!ReadStore(Overrides, bFound, Reason, Fitted) || !Character->TryApplyTuning(Overrides, Reason))
	{
		const FString Message = FString::Printf(TEXT("%s: tuning changes in %s rejected, %s. Keeping the last good values."),
			*Character->GetName(), *GetOverrideStore().Describe(), *Reason);
		UE_LOG(LogCSCharacter, Warning, TEXT("%s"), *Message);
		SetReloadResult(false, Message);
		return false;
	}
	if (Overrides.Values.Num() > 0 || !Fitted.IsEmpty())
	{
		// Say so on screen: the build is not running on the values it was packaged with, and any
		// saved value this build could not take as it was is named.
		const FString Message = FString::Printf(TEXT("%s: %s"), *Character->GetName(), *GetTuningSourceText())
			+ DescribeFitted(*Character, Fitted);
		UE_LOG(LogCSCharacter, Log, TEXT("%s"), *Message);
		SetReloadResult(true, Message);
	}
	return true;
}

bool UCSDevTools::SetOverrides(const FCSTuningOverrides& Overrides, bool bSave, FString& OutReason)
{
	OutReason.Reset();
	ACSCharacter* Character = GetCharacter();
	if (!Character || !IsReloadAllowed())
	{
		OutReason = TEXT("dev tools are off (bDevTools), tuning cannot be changed");
		return false;
	}
	FCSTuningOverrides Wanted = Overrides;
	Wanted.Version = FCSTuningOverrides::CurrentVersion;
	Wanted.Build = ProjectVersionText();
	Wanted.PruneEqualTo(Character->GetPackagedTuning());
	if (!Character->TryApplyTuning(Wanted, OutReason))
	{
		return false;
	}
	if (bSave)
	{
		ICSTuningOverrideStore& Store = GetOverrideStore();
		const bool bStored = Wanted.Values.Num() > 0 ? Store.Save(Wanted.ToJson()) : Store.Clear();
		if (bStored)
		{
			UnsavedOverrides.Reset();
		}
		else
		{
			UnsavedOverrides = Wanted;
			OutReason = FString::Printf(TEXT("the tuning changes could not be saved to %s; they stay in use until the game closes"), *Store.Describe());
			const FString Message = FString::Printf(TEXT("%s: %s."), *Character->GetName(), *OutReason);
			UE_LOG(LogCSCharacter, Warning, TEXT("%s"), *Message);
			SetReloadResult(false, Message);
		}
	}
	return true;
}

const FCSTuningOverrides& UCSDevTools::GetOverrides() const
{
	static const FCSTuningOverrides None;
	const ACSCharacter* Character = GetCharacter();
	return Character ? Character->GetTuningOverrides() : None;
}

FString UCSDevTools::GetTuningSourceText() const
{
	const FCSTuningOverrides& Overrides = GetOverrides();
	const int32 Count = Overrides.Values.Num();
	if (Count == 0)
	{
		return TEXT("tuning: packaged");
	}
	FString Text = FString::Printf(TEXT("tuning: packaged + %d %s"), Count, Count == 1 ? TEXT("change") : TEXT("changes"));
	if (!Overrides.Build.IsEmpty() && Overrides.Build != ProjectVersionText())
	{
		Text += FString::Printf(TEXT(" (file from %s)"), *Overrides.Build);
	}
	return Text;
}

bool UCSDevTools::RequestReload()
{
	ACSCharacter* Character = GetCharacter();
	if (!Character || !IsReloadAllowed())
	{
		return false;
	}
	FString Message;
	return ReloadTuning(*Character, Message);
}

void UCSDevTools::SetReloadResult(bool bAccepted, const FString& Message)
{
	bLastReloadAccepted = bAccepted;
	LastReloadMessage = Message;
	const UWorld* World = GetWorld();
	LastReloadTime = World ? World->GetTimeSeconds() : 0.0;
	UpdateHUDHook();
}

bool UCSDevTools::IsReloadMessageVisible() const
{
	const UWorld* World = GetWorld();
	return !LastReloadMessage.IsEmpty() && LastReloadTime >= 0.0 && World
		&& World->GetTimeSeconds() - LastReloadTime <= ReloadMessageSeconds;
}

void UCSDevTools::UpdateHUDHook()
{
	// One hook draws both the overlay and the reload result; it is removed when neither is shown.
	const bool bWanted = IsOverlayVisible() || IsReloadMessageVisible();
	if (bWanted && !HUDHandle.IsValid())
	{
		HUDHandle = AHUD::OnHUDPostRender.AddUObject(this, &UCSDevTools::DrawOverlay);
	}
	else if (!bWanted && HUDHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HUDHandle);
		HUDHandle.Reset();
	}
}

int32 UCSDevTools::ReloadTuningInWorld(UWorld* World, TArray<FString>& OutMessages)
{
	int32 Reloaded = 0;
	if (!World)
	{
		return 0;
	}
	for (TActorIterator<ACSCharacter> It(World); It; ++It)
	{
		ACSCharacter* Character = *It;
		const UCSDevTools* DevTools = Character->GetDevTools();
		if (!DevTools || !DevTools->IsReloadAllowed())
		{
			FString Message = FString::Printf(TEXT("%s: dev tools are off (bDevTools), tuning not reloaded."), *Character->GetName());
			UE_LOG(LogCSCharacter, Log, TEXT("%s"), *Message);
			OutMessages.Add(MoveTemp(Message));
			continue;
		}
		FString Message;
		if (ReloadTuning(*Character, Message))
		{
			++Reloaded;
		}
		OutMessages.Add(MoveTemp(Message));
	}
	return Reloaded;
}

bool UCSDevTools::AreEnabled() const
{
	const ACSCharacter* Character = GetCharacter();
	return Character && Character->GetActiveTuning().bDevTools;
}

bool UCSDevTools::IsReloadAllowed() const
{
	const ACSCharacter* Character = GetCharacter();
	if (!Character)
	{
		return false;
	}
	const UCSMovementTuning* Source = Character->Tuning ? Character->Tuning.Get() : GetDefault<UCSMovementTuning>();
	return Source->bDevTools;
}

bool UCSDevTools::IsOverlayVisible() const
{
	return bOverlayVisible && AreEnabled();
}

void UCSDevTools::ToggleOverlay()
{
	if (!AreEnabled())
	{
		SetOverlayVisible(false);
		return;
	}
	SetOverlayVisible(!bOverlayVisible);
}

void UCSDevTools::SetOverlayVisible(bool bVisible)
{
	bOverlayVisible = bVisible;
	bHasPreviousVelocity = false;
	SetComponentTickEnabled(bVisible);
	UpdateHUDHook();
}

bool UCSDevTools::RequestRagdoll()
{
	if (!AreEnabled())
	{
		return false;
	}
	OnRagdollRequested.Broadcast();
	return true;
}

bool UCSDevTools::RequestPush()
{
	const ACSCharacter* Character = GetCharacter();
	if (!Character || !AreEnabled())
	{
		return false;
	}
	// The view direction: the controller's aim, or the actor's facing with no controller.
	const FVector Direction = Character->GetBaseAimRotation().Vector().GetSafeNormal();
	OnPushRequested.Broadcast(Direction.IsNearlyZero() ? Character->GetActorForwardVector() : Direction);
	return true;
}

ACSHitBall* UCSDevTools::RequestThrow()
{
	ACSCharacter* Character = GetCharacter();
	if (!Character || !AreEnabled())
	{
		return nullptr;
	}
	const UCSMovementTuning& T = Character->GetActiveTuning();
	FVector Direction = Character->GetBaseAimRotation().Vector().GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = Character->GetActorForwardVector();
	}
	// From the chest, far enough out to start clear of the character's own body.
	const FVector From = Character->GetActorLocation() + FVector(0.f, 0.f, 40.f) + Direction * 80.f;
	return ACSHitBall::Throw(GetWorld(), Character, From, Direction, T.DevBallMass, T.DevBallSpeed);
}

bool UCSDevTools::RequestReset()
{
	if (!AreEnabled())
	{
		return false;
	}
	if (!OnResetRequested.IsBound())
	{
		UE_LOG(LogCSCharacter, Log, TEXT("%s: reset requested, but nothing handles it (the game binds OnResetRequested)."), *GetNameSafe(GetOwner()));
	}
	OnResetRequested.Broadcast();
	return true;
}

FString UCSDevTools::DescribeLastContact(const ACSCharacter* Character, const TCHAR* Who)
{
	const UCSPhysicsBodyComponent* Body = Character ? Character->GetPhysicsBody() : nullptr;
	const UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!Body || !World || Body->GetLastContact().Time < 0.0)
	{
		return FString::Printf(TEXT("Hit %-6s none yet"), Who);
	}
	const FCSContactRecord& Contact = Body->GetLastContact();
	const TCHAR* Result = TEXT("ignored");
	switch (Contact.Result)
	{
	case ECSContactResult::Stagger: Result = TEXT("stagger"); break;
	case ECSContactResult::Ragdoll: Result = TEXT("ragdoll"); break;
	case ECSContactResult::Blocked: Result = TEXT("blocked"); break;
	default: break;
	}
	return FString::Printf(TEXT("Hit %-6s %s  %.0f cm/s  %s  %.1f s ago"), Who, *Contact.Bone.ToString(), Contact.VelocityChange, Result,
		World->GetTimeSeconds() - Contact.Time);
}

bool UCSDevTools::RequestMapSwitch()
{
	if (!AreEnabled())
	{
		return false;
	}
	if (!OnMapSwitchRequested.IsBound())
	{
		UE_LOG(LogCSCharacter, Log, TEXT("%s: map switch requested, but nothing handles it (the game binds OnMapSwitchRequested)."), *GetNameSafe(GetOwner()));
	}
	OnMapSwitchRequested.Broadcast();
	return true;
}

void UCSDevTools::BeginPlay()
{
	Super::BeginPlay();
	if (const ACSCharacter* Character = GetCharacter())
	{
		// TG_PostPhysics already orders this after the movement component; the prerequisite keeps
		// that true if either tick group is changed. The movement component does not depend on
		// this component, so there is no cycle.
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			AddTickPrerequisiteComponent(Move);
		}
	}
}

void UCSDevTools::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	LastReloadMessage.Reset();
	SetOverlayVisible(false);
	Super::EndPlay(EndPlayReason);
}

void UCSDevTools::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsOverlayVisible())
	{
		// Dev tools were turned off by a reload while the overlay was up.
		SetOverlayVisible(false);
		return;
	}
	TakeSample(DeltaTime);
}

void UCSDevTools::TakeSample(float DeltaTime)
{
	const ACSCharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	const FVector Velocity = Move->Velocity;
	Sample.Speed = Velocity.Size2D();
	Sample.VerticalSpeed = Velocity.Z;
	Sample.Acceleration = (bHasPreviousVelocity && DeltaTime > 0.f)
		? (FVector(Velocity.X, Velocity.Y, 0.f) - FVector(PreviousVelocity.X, PreviousVelocity.Y, 0.f)).Size() / DeltaTime
		: 0.f;
	PreviousVelocity = Velocity;
	bHasPreviousVelocity = true;

	Sample.bGrounded = Move->IsMovingOnGround();
	Sample.SlopeAngleDeg = 0.f;
	if (Sample.bGrounded && Move->CurrentFloor.bBlockingHit)
	{
		const float NormalZ = FMath::Clamp(float(Move->CurrentFloor.HitResult.ImpactNormal.Z), -1.f, 1.f);
		Sample.SlopeAngleDeg = FMath::RadiansToDegrees(FMath::Acos(NormalZ));
	}
	Sample.Gait = Character->GetGait();
	Sample.Camera = Character->GetActiveCameraName();
}

namespace
{
	// The project version the build was packaged as (ProjectVersion in DefaultGame.ini), so a
	// screenshot or clip of the overlay can be tied to a Steam build.
	const FString& ProjectVersionText()
	{
		static FString Version;
		static bool bRead = false;
		if (!bRead)
		{
			bRead = true;
			if (!GConfig || !GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni) || Version.IsEmpty())
			{
				Version = TEXT("unknown");
			}
		}
		return Version;
	}

	/** The character closest to Character, other than itself: the dummy the overlay reports on. */
	const ACSCharacter* FindNearestOtherCharacter(const ACSCharacter* Character)
	{
		const ACSCharacter* Best = nullptr;
		double BestDistance = TNumericLimits<double>::Max();
		if (const UWorld* World = Character ? Character->GetWorld() : nullptr)
		{
			for (TActorIterator<ACSCharacter> It(World); It; ++It)
			{
				const double Distance = FVector::DistSquared(It->GetActorLocation(), Character->GetActorLocation());
				if (*It != Character && Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = *It;
				}
			}
		}
		return Best;
	}

	const TCHAR* BodyStateName(const ACSCharacter* Character)
	{
		const UCSPhysicsBodyComponent* Body = Character ? Character->GetPhysicsBody() : nullptr;
		if (!Body || !Body->HasPhysicsBody())
		{
			return TEXT("none");
		}
		switch (Body->GetState())
		{
		case ECSBodyState::Normal: return TEXT("Normal");
		case ECSBodyState::Hit: return TEXT("Hit");
		case ECSBodyState::Ragdoll: return TEXT("Ragdoll");
		case ECSBodyState::Recovering: return TEXT("Recovering");
		}
		return TEXT("?");
	}
}

FString UCSDevTools::GetOverlayText() const
{
	return FString::Printf(
		TEXT("Speed     %6.0f cm/s\n")
		TEXT("Accel     %6.0f cm/s2\n")
		TEXT("Vertical  %6.0f cm/s\n")
		TEXT("Grounded  %s\n")
		TEXT("Slope     %6.1f deg\n")
		TEXT("Gait      %s\n")
		TEXT("Camera    %s\n")
		TEXT("Body      %s\n")
		TEXT("Build     %s\n")
		TEXT("Values    %s\n")
		TEXT("%s\n")
		TEXT("%s\n")
		TEXT("Keys      F1 overlay  F2 map  F5 reload tuning  R ragdoll  P push  T throw ball  Backspace reset  V camera"),
		Sample.Speed,
		Sample.Acceleration,
		Sample.VerticalSpeed,
		Sample.bGrounded ? TEXT("yes") : TEXT("no"),
		Sample.SlopeAngleDeg,
		GaitName(Sample.Gait),
		Sample.Camera.IsNone() ? TEXT("n/a") : *Sample.Camera.ToString(),
		BodyStateName(GetCharacter()),
		*ProjectVersionText(),
		*GetTuningSourceText(),
		*DescribeLastContact(GetCharacter(), TEXT("you")),
		*DescribeLastContact(FindNearestOtherCharacter(GetCharacter()), TEXT("dummy")));
}

void UCSDevTools::DrawOverlay(AHUD* HUD, UCanvas* Canvas)
{
	const ACSCharacter* Character = GetCharacter();
	const bool bOverlay = IsOverlayVisible();
	const bool bReload = IsReloadMessageVisible();
	if (!bOverlay && !bReload)
	{
		// The reload result has timed out with the overlay off. Removing a binding during a
		// broadcast is safe for multicast delegates.
		UpdateHUDHook();
		return;
	}
	if (!HUD || !Canvas || !Character || HUD->GetOwningPawn() != Character || !GEngine)
	{
		return;
	}
	// While the game is paused the pause screen is up: its tuning tab shows the same status line, and
	// the overlay's text would lie over the screen's rows.
	if (const UWorld* World = GetWorld(); World && World->IsPaused())
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	const float LineHeight = Font ? Font->GetMaxCharHeight() + 2.f : 14.f;
	const float X = 24.f;
	float Y = 72.f;
	auto DrawLine = [&](const FString& Line, const FColor& Color)
	{
		// A one-pixel shadow keeps the text readable on any background.
		Canvas->SetDrawColor(FColor::Black);
		Canvas->DrawText(Font, Line, X + 1.f, Y + 1.f);
		Canvas->SetDrawColor(Color);
		Canvas->DrawText(Font, Line, X, Y);
		Y += LineHeight;
	};
	if (bReload)
	{
		DrawLine(LastReloadMessage, bLastReloadAccepted ? FColor::Green : FColor(255, 96, 96));
	}
	Y = 96.f;
	if (bOverlay)
	{
		TArray<FString> Lines;
		GetOverlayText().ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			DrawLine(Line, FColor::White);
		}
	}
}
