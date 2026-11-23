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

/**
 * Feel-testing tools for ACSCharacter: the ragdoll and push keys, the feel overlay and tuning reload.
 *
 * Everything here is compiled into every build and gated at run time by bDevTools in the character's
 * applied tuning: with it off, the ragdoll, push and overlay keys and the cs.Tuning.Reload command do
 * nothing. Ragdoll and push are requests: this component only broadcasts OnRagdollRequested and
 * OnPushRequested, and the physics body binds to them.
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
	 */
	static bool ReloadTuning(ACSCharacter& Character, FString& OutMessage);

	/**
	 * What cs.Tuning.Reload runs: ReloadTuning on every ACSCharacter in the world whose dev tools are
	 * on. Adds one message per character to OutMessages. Returns how many reloaded.
	 */
	static int32 ReloadTuningInWorld(UWorld* World, TArray<FString>& OutMessages);

	/** True when the owner's applied tuning has bDevTools on. */
	UFUNCTION(BlueprintPure, Category = "CS|Dev tools")
	bool AreEnabled() const;

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

	/** Extension point: the physics body toggles ragdoll here. */
	FCSRagdollRequested OnRagdollRequested;

	/** Extension point: the physics body applies its test impulse here, along Direction. */
	FCSPushRequested OnPushRequested;

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
	void TakeSample(float DeltaTime);
	void DrawOverlay(AHUD* HUD, UCanvas* Canvas);

	FCSFeelSample Sample;
	FVector PreviousVelocity = FVector::ZeroVector;
	bool bHasPreviousVelocity = false;
	bool bOverlayVisible = false;
	FDelegateHandle HUDHandle;
};
