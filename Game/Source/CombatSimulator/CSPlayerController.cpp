#include "CSPlayerController.h"

#include "CSCharacter.h"
#include "CSDevTools.h"
#include "CSGameSettings.h"
#include "CombatSimulator.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EnhancedInputComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "InputAction.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/SCSPauseScreen.h"
#include "Widgets/SWeakWidget.h"

void ACSPlayerController::BeginPlay()
{
	Super::BeginPlay();
	SettingsChangedHandle = UCSGameSettings::Get()->OnChanged.AddUObject(this, &ACSPlayerController::ApplySettings);
	ApplySettings();
}

void ACSPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UCSGameSettings::Get()->OnChanged.Remove(SettingsChangedHandle);
	SettingsChangedHandle.Reset();
	// Leaving the map with the screen open: take the widget down with the world it belongs to.
	ShowPauseScreen(false);
	bPauseScreenOpen = false;
	Super::EndPlay(EndPlayReason);
}

void ACSPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ApplySettings();
}

void ACSPlayerController::ApplySettings()
{
	ACSCharacter* Possessed = Cast<ACSCharacter>(GetPawn());
	if (!Possessed)
	{
		return;
	}
	const UCSGameSettings* Settings = UCSGameSettings::Get();
	const float Pitch = Settings->bInvertY ? -1.f : 1.f;
	Possessed->SetLookScale(
		FVector2D(Settings->MouseSensitivity, Settings->MouseSensitivity * Pitch),
		FVector2D(Settings->StickSensitivity, Settings->StickSensitivity * Pitch));
}

void ACSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogCombatSimulator, Error, TEXT("%s: the input component is not a UEnhancedInputComponent (set DefaultInputComponentClass); the pause key will do nothing."), *GetName());
		return;
	}
	UInputAction* Action = PauseAction.LoadSynchronous();
	if (!Action)
	{
		UE_LOG(LogCombatSimulator, Warning, TEXT("%s: PauseAction is not set (config section [/Script/CombatSimulator.CSPlayerController]); the pause key does nothing."), *GetName());
		return;
	}
	Input->BindAction(Action, ETriggerEvent::Started, this, &ACSPlayerController::TogglePauseScreen);
}

void ACSPlayerController::TogglePauseScreen()
{
	RequestedTab.Reset();
	SetPauseScreenOpen(!bPauseScreenOpen);
}

void ACSPlayerController::OpenPauseScreenOnTab(ECSPauseTab Tab)
{
	RequestedTab = Tab;
	if (!bPauseScreenOpen)
	{
		SetPauseScreenOpen(true);
	}
	else if (PauseScreen.IsValid() && PauseScreen->GetTab() != Tab)
	{
		PauseScreen->SetTab(Tab);
	}
}

void ACSPlayerController::SetPauseScreenOpen(bool bOpen)
{
	if (bOpen == bPauseScreenOpen)
	{
		return;
	}
	bPauseScreenOpen = bOpen;
	// The engine's pause: time stops for the world, the screen and this controller's input keep running.
	SetPause(bOpen);
	SetShowMouseCursor(bOpen);
	ShowPauseScreen(bOpen);
	if (bOpen)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		if (PauseScreen.IsValid())
		{
			Mode.SetWidgetToFocus(PauseScreen);
		}
		SetInputMode(Mode);
		if (PauseScreen.IsValid())
		{
			PauseScreen->FocusFirstControl();
		}
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}
}

void ACSPlayerController::ClosePauseScreenFromKey(const FKey& Key)
{
	PauseKeyGuard.Arm(Key, FPlatformTime::Seconds());
	SetPauseScreenOpen(false);
}

bool ACSPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (PauseKeyGuard.ShouldSwallow(Params.Key, Params.Event == IE_Released, FPlatformTime::Seconds()))
	{
		return true;
	}
	return Super::InputKey(Params);
}

void ACSPlayerController::QuitToDesktop()
{
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

bool ACSPlayerController::AreDevToolsOn() const
{
	const ACSCharacter* Possessed = Cast<ACSCharacter>(GetPawn());
	const UCSDevTools* DevTools = Possessed ? Possessed->GetDevTools() : nullptr;
	return DevTools && DevTools->IsReloadAllowed();
}

void ACSPlayerController::ShowPauseScreen(bool bShow)
{
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (PauseScreenHost.IsValid())
	{
		if (Viewport)
		{
			Viewport->RemoveViewportWidgetContent(PauseScreenHost.ToSharedRef());
		}
		PauseScreenHost.Reset();
		PauseScreen.Reset();
	}
	// No viewport, or no Slate (a headless run): pausing still works, there is just nothing to draw.
	if (!bShow || !Viewport || !FSlateApplication::IsInitialized() || !IsLocalController())
	{
		return;
	}
	PauseScreen = SNew(SCSPauseScreen).Owner(this).InitialTab(RequestedTab);
	PauseScreenHost = SNew(SWeakWidget).PossiblyNullContent(PauseScreen);
	// Above the HUD and any other viewport content.
	Viewport->AddViewportWidgetContent(PauseScreenHost.ToSharedRef(), 100);
}
