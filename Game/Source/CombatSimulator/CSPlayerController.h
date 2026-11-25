#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UI/CSPauseMenuModel.h"
#include "CSPlayerController.generated.h"

class SCSPauseScreen;
class UInputAction;

/**
 * The game's player controller (ACSGameMode::PlayerControllerClass).
 *
 * Settings: pushes the player's look settings (UCSGameSettings) onto the character it possesses, when
 * it possesses one and whenever a setting changes, so a respawned character or one in a newly opened
 * map looks the same as the last.
 *
 * Pause: the pause key (PauseAction: Escape, gamepad Start, and Tab because Escape ends Play in the
 * editor) opens and closes the pause screen. While it is open the game is paused with the engine's
 * pause (never time scale), the cursor is shown and input goes to the screen. The action is in the
 * character's mapping context (IMC_Character) and triggers while paused.
 */
UCLASS(Config = Game)
class COMBATSIMULATOR_API ACSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Opens and closes the pause screen. Set in DefaultGame.ini ([/Script/CombatSimulator.CSPlayerController]). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> PauseAction;

	/** Pushes the look settings onto the possessed ACSCharacter. Called on possession and when a setting changes. */
	UFUNCTION(BlueprintCallable, Category = "CS|Settings")
	void ApplySettings();

	/** Pauses or resumes the game and shows or hides the pause screen, the cursor and UI input. */
	UFUNCTION(BlueprintCallable, Category = "CS|Pause")
	void SetPauseScreenOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "CS|Pause")
	bool IsPauseScreenOpen() const { return bPauseScreenOpen; }

	/**
	 * Opens the pause screen on Tab (pausing the game), or switches an open screen to it. A tab the
	 * screen does not have (Tuning with dev tools off) leaves it on its first tab.
	 */
	void OpenPauseScreenOnTab(ECSPauseTab Tab);

	/** The tab the screen was last asked to open on, if any; the screen's first tab otherwise. */
	TOptional<ECSPauseTab> GetRequestedTab() const { return RequestedTab; }

	/** What the pause key does: opens the screen, or closes it. */
	UFUNCTION(BlueprintCallable, Category = "CS|Pause")
	void TogglePauseScreen();

	/**
	 * Closes the pause screen because Key was pressed on it. The key is then ignored by the game
	 * until it is released (FCSPauseKeyGuard), so holding it does not open the screen again.
	 */
	void ClosePauseScreenFromKey(const FKey& Key);

	/** Leaves the game. */
	UFUNCTION(BlueprintCallable, Category = "CS|Pause")
	void QuitToDesktop();

	/** True when the possessed character's dev tools are on: the pause screen then has its Tuning tab. */
	bool AreDevToolsOn() const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void SetupInputComponent() override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

protected:
	/** Adds the pause screen to the game viewport or removes it. Does nothing without a viewport (headless runs). */
	virtual void ShowPauseScreen(bool bShow);

private:
	bool bPauseScreenOpen = false;
	TOptional<ECSPauseTab> RequestedTab;
	FCSPauseKeyGuard PauseKeyGuard;
	FDelegateHandle SettingsChangedHandle;
	TSharedPtr<SCSPauseScreen> PauseScreen;
	/** The viewport content that holds PauseScreen, kept so it can be removed. */
	TSharedPtr<SWidget> PauseScreenHost;
};
