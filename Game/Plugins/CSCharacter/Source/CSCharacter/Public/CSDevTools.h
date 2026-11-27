#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CSCharacter.h"
#include "CSDevTools.generated.h"

class AHUD;
class UCanvas;

/** One reading of the feel values the overlay shows, taken after the movement component has moved. */
USTRUCT(BlueprintType)
struct FCSFeelSample
{
	GENERATED_BODY()

	/** Horizontal speed, cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Dev tools")
	float Speed = 0.f;

	/** Horizontal acceleration actually achieved over the last frame, cm/s^2 (the change in horizontal velocity per second). */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Dev tools")
	float Acceleration = 0.f;

	/** Vertical speed, cm/s (positive up). */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Dev tools")
	float VerticalSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Dev tools")
	bool bGrounded = false;

	/** Angle of the floor under the character, degrees (0 when flat or in the air). */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Dev tools")
	float SlopeAngleDeg = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Dev tools")
	ECSGait Gait = ECSGait::Jog;

	/** ACSCharacter::GetActiveCameraName; None until a camera rig reports one. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Dev tools")
	FName Camera;
};

DECLARE_MULTICAST_DELEGATE(FCSRagdollRequested);
DECLARE_MULTICAST_DELEGATE_OneParam(FCSPushRequested, const FVector& /* Direction, unit length */);
DECLARE_MULTICAST_DELEGATE(FCSMapSwitchRequested);
DECLARE_MULTICAST_DELEGATE(FCSResetRequested);

class ACSHitBall;

/**
 * Feel-testing tools for ACSCharacter: the ragdoll and push keys, the feel overlay, tuning reload
 * (the F5 key and the cs.Tuning.Reload command) and the map switch key.
 *
 * Everything here is compiled into every build and gated at run time by bDevTools in the character's
 * applied tuning: with it off, the ragdoll, push, overlay, reload and map switch keys and the
 * cs.Tuning.Reload command do nothing. Ragdoll, push and map switch are requests: this component only
 * broadcasts OnRagdollRequested, OnPushRequested and OnMapSwitchRequested. The physics body binds the
 * first two; the game binds the map switch (the plugin knows no maps).
 *
 * The overlay reading is taken in TickComponent, which runs in TG_PostPhysics after the movement
 * component (the character itself ticks before its movement component), so it shows what the
 * movement did this frame. It is drawn through AHUD::OnHUDPostRender, which also runs in shipping
 * builds, for the HUD of the player controlling this character.
 */
UCLASS(ClassGroup = (CS), meta = (BlueprintSpawnableComponent))
class CSCHARACTER_API UCSDevTools : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSDevTools();

	/**
	 * Validates the character's Tuning asset (UCSMovementTuning::IsValid) and, if it is good, applies it.
	 * A bad asset is rejected: the character keeps its last good values and OutMessage names the
	 * first bad field. Logged either way. Not gated by bDevTools (cs.Tuning.Reload is).
	 * Edits to the asset only take effect through this or ACSCharacter::ApplyTuning.
	 *
	 * Tuning changes (the overrides file) go on top of the asset: the local player's character
	 * re-reads the file here when its dev tools allow it; any other character keeps the changes it
	 * has. A file, or a combination of asset and changes, that is not valid is rejected the same way.
	 */
	static bool ReloadTuning(ACSCharacter& Character, FString& OutMessage);

	// ----- Tuning changes kept on this machine (the overrides file) -----

	/** Where the changes are kept: <ProjectSavedDir>/Tuning/MovementTuning.json, or the store a test set. */
	static ICSTuningOverrideStore& GetOverrideStore();

	/** Tests: use Store instead of the file (null: back to the default). Automation runs never touch the real file. */
	static void SetOverrideStoreForTests(ICSTuningOverrideStore* Store);

	/**
	 * Reads the store and applies what it holds on top of the packaged tuning; with nothing stored,
	 * the packaged values. Entries equal to the packaged value are dropped. Does nothing and returns
	 * false with dev tools off in the source tuning. A bad file (not JSON, an unknown field, a value
	 * out of range, a rule between fields broken) is rejected whole: nothing changes, the on-screen
	 * reload line turns red and names the cause, and this returns false.
	 */
	bool LoadOverrides();

	/**
	 * Makes Overrides the changes in use, if the result is valid (ACSCharacter::TryApplyTuning) and
	 * dev tools are on in the source tuning; otherwise returns false with the cause in OutReason and
	 * changes nothing. With bSave the changes are also written to the store (cleared when empty); a
	 * failed write keeps the values in use, returns true, and says so in OutReason and on screen.
	 */
	bool SetOverrides(const FCSTuningOverrides& Overrides, bool bSave, FString& OutReason);

	/** The changes in use on the owner. */
	const FCSTuningOverrides& GetOverrides() const;

	/**
	 * Which values are in play, for the overlay, the reload line and screenshots: "tuning: packaged",
	 * "tuning: packaged + 3 changes", with " (file from 0.1.3)" added when the changes were written
	 * by another build.
	 */
	FString GetTuningSourceText() const;

	/**
	 * What cs.Tuning.Reload runs: ReloadTuning on every ACSCharacter in the world whose dev tools are
	 * on. Adds one message per character to OutMessages. Returns how many reloaded.
	 */
	static int32 ReloadTuningInWorld(UWorld* World, TArray<FString>& OutMessages);

	/** True when the owner's applied tuning has bDevTools on. */
	UFUNCTION(BlueprintPure, Category = "CS|Dev tools")
	bool AreEnabled() const;

	/**
	 * True when a tuning reload may run: bDevTools on the character's source tuning (the asset, or
	 * the class defaults when none is set), not on the applied copy. Reading the source is what
	 * lets a reload turn dev tools back on after a reload turned them off.
	 */
	bool IsReloadAllowed() const;

	/** F1. Shows or hides the feel overlay; does nothing with dev tools off. */
	UFUNCTION(BlueprintCallable, Category = "CS|Dev tools")
	void ToggleOverlay();

	/** True while the overlay is shown (always false with dev tools off). */
	UFUNCTION(BlueprintPure, Category = "CS|Dev tools")
	bool IsOverlayVisible() const;

	/** R. Broadcasts OnRagdollRequested; does nothing with dev tools off. Returns whether it broadcast. */
	UFUNCTION(BlueprintCallable, Category = "CS|Dev tools")
	bool RequestRagdoll();

	/** P. Broadcasts OnPushRequested along the view direction; does nothing with dev tools off. Returns whether it broadcast. */
	UFUNCTION(BlueprintCallable, Category = "CS|Dev tools")
	bool RequestPush();

	/**
	 * F2. Broadcasts OnMapSwitchRequested (the game decides which map to open); does nothing with dev
	 * tools off. Returns whether it broadcast.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Dev tools")
	bool RequestMapSwitch();

	/**
	 * T. Throws a ball (ACSHitBall, DevBallMass at DevBallSpeed) from the chest along the view; does
	 * nothing with dev tools off. The ball never hits the character that threw it. Returns the ball.
	 */
	ACSHitBall* RequestThrow();

	/**
	 * Backspace. Broadcasts OnResetRequested (the game puts the player, the dummies and the props
	 * back and removes the balls); does nothing with dev tools off. Returns whether it broadcast.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Dev tools")
	bool RequestReset();

	/**
	 * F5. ReloadTuning on the owner; does nothing with dev tools off. The console (cs.Tuning.Reload)
	 * is compiled out of Shipping builds, so this key is the reload there. Returns whether it reloaded.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Dev tools")
	bool RequestReload();

	/** The result of the last reload of the owner (accepted, or the rejection naming the field); empty before any. */
	const FString& GetLastReloadMessage() const { return LastReloadMessage; }

	/** Whether the last reload was accepted. */
	bool WasLastReloadAccepted() const { return bLastReloadAccepted; }

	/**
	 * True while the last reload result is on screen: ReloadMessageSeconds after the reload, whether
	 * or not the overlay is shown. Drawn through the same HUD hook, so it shows in Shipping builds.
	 */
	bool IsReloadMessageVisible() const;

	/** Seconds a reload result stays on screen. */
	static constexpr float ReloadMessageSeconds = 4.f;

	/** Extension point: the physics body toggles ragdoll here. */
	FCSRagdollRequested OnRagdollRequested;

	/** Extension point: the physics body applies its test impulse here, along Direction. */
	FCSPushRequested OnPushRequested;

	/** Extension point: the game opens its next map here (the plugin has no map list). */
	FCSMapSwitchRequested OnMapSwitchRequested;
	/** Extension point: the game resets the player, the dummies and the props here. */
	FCSResetRequested OnResetRequested;

	/** One overlay line for a character's last contact hit: "Hit you  spine_03  320 cm/s  stagger  1.2 s ago". */
	static FString DescribeLastContact(const ACSCharacter* Character, const TCHAR* Who);

	/** The latest reading (updated every frame while the overlay is shown). */
	const FCSFeelSample& GetFeelSample() const { return Sample; }

	/** The overlay text for the latest reading, one value per line. */
	FString GetOverlayText() const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	ACSCharacter* GetCharacter() const;
	void SetOverlayVisible(bool bVisible);
	void SetReloadResult(bool bAccepted, const FString& Message);
	/**
	 * Reads and checks the store. bOutFound: something was stored. False: it is bad, OutReason says why.
	 * Entries another build saved that this one cannot take are dropped or clamped, not a rejection:
	 * OutFitted names them (empty when all fit).
	 */
	bool ReadStore(FCSTuningOverrides& OutOverrides, bool& bOutFound, FString& OutReason, FString& OutFitted) const;
	/** Logs and returns " Saved values changed to fit this build: ..." for Fitted, or empty. */
	static FString DescribeFitted(const ACSCharacter& Character, const FString& Fitted);
	void UpdateHUDHook();
	void TakeSample(float DeltaTime);
	void DrawOverlay(AHUD* HUD, UCanvas* Canvas);

	FCSFeelSample Sample;
	FVector PreviousVelocity = FVector::ZeroVector;
	bool bHasPreviousVelocity = false;
	bool bOverlayVisible = false;
	FDelegateHandle HUDHandle;
	FString LastReloadMessage;
	bool bLastReloadAccepted = false;
	double LastReloadTime = -1.0;
};
