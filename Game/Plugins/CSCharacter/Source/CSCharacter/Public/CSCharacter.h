#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CSCharacter.generated.h"

class UCameraComponent;
class UCSCameraRig;
class UCSDevTools;
class UCSPhysicsBodyComponent;
class UPhysicalAnimationComponent;
class UCSSpringArmComponent;
class UCSMovementTuning;
class UInputAction;
class UInputMappingContext;
class UPoseableMeshComponent;
class UAnimInstance;
class USkeletalMesh;
struct FInputActionValue;

DECLARE_MULTICAST_DELEGATE(FCSToggleCameraRequested);
DECLARE_MULTICAST_DELEGATE_OneParam(FCSRagdollStateChanged, bool /* bRagdolled */);

/** Movement gait. Jog is the default; walk and sprint are held, crouch is toggled (see ResolveGait). */
UENUM(BlueprintType)
enum class ECSGait : uint8
{
	Walk,
	Jog,
	Sprint,
	Crouch,
};

/**
 * A humanoid character with weighty movement, built on ACharacter and its movement component.
 *
 * Feel comes from UCSMovementTuning (Tuning; the class defaults when unset). On top of the
 * movement component's own acceleration this class adds:
 * - a sprint ramp: the speed cap rises from jog to sprint at a constant rate, so the time to sprint
 *   speed is the same at any frame rate;
 * - a landing cost: horizontal speed is reduced in proportion to the fall speed above a jump's;
 * - slope speed: slower uphill, slightly faster downhill.
 * - turn grip that loosens toward sprint speed, so a sprint turns wider than a walk.
 *
 * The mesh component faces the actor's +X when the skeletal mesh faces +Y (the UE5 humanoid
 * convention), with its origin at the feet and the capsule fitted to a 180 cm figure.
 *
 * The skeletal mesh and its animation class are soft references read from the same config section
 * (CharacterMesh, CharacterAnimClass); they fill the mesh component only when it has none, so a
 * subclass or a placed character can set its own.
 *
 * Input is Enhanced Input. The mapping context and actions are soft references read from the game's
 * config (section [/Script/CSCharacter.CSCharacter] in DefaultGame.ini) or set on a subclass, so the
 * plugin assumes no content paths. Look values are degrees with +X right and +Y up (keep
 * bEnableLegacyInputScales off). Dev keys go to UCSDevTools (DevTools); the camera key (V) switches
 * the camera rig (CameraRig: first and third person on one camera) and broadcasts
 * OnToggleCameraRequested.
 *
 * Ragdoll contract: whatever drives the ragdoll (the physics body, UCSPhysicsBodyComponent) calls
 * SetRagdolled(true) when the body goes fully simulated and SetRagdolled(false) when control returns
 * after recovery. The camera rig reads IsRagdolled every frame and uses third person in between.
 * While ragdolled, move, jump and crouch input is ignored; held sprint and walk keys are tracked and
 * applied when control returns. Jump while crouched stands the character up and jumps.
 */
UCLASS(Config = Game, Blueprintable)
class CSCHARACTER_API ACSCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ACSCharacter(const FObjectInitializer& ObjectInitializer);

	/**
	 * Feel values. When empty, the UCSMovementTuning class defaults are used. The character runs on a
	 * copy taken by ApplyTuning, so edits to the asset take effect only when applied or reloaded
	 * (UCSDevTools::ReloadTuning, cs.Tuning.Reload).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Movement")
	TObjectPtr<UCSMovementTuning> Tuning;

	/**
	 * Copies Tuning (or the class defaults) and pushes the values into the movement component.
	 * Called on BeginPlay (where an invalid asset is replaced by the class defaults, with an error
	 * logged); call again after changing Tuning. Does not validate: use UCSDevTools::ReloadTuning for that.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Movement")
	void ApplyTuning();

	/** Changes gait. Crouch crouches the character; any other gait stands it up. */
	UFUNCTION(BlueprintCallable, Category = "CS|Movement")
	void SetGait(ECSGait NewGait);

	UFUNCTION(BlueprintPure, Category = "CS|Movement")
	ECSGait GetGait() const { return Gait; }

	/** The tuning in use: the copy last applied; before that, Tuning or the class defaults. */
	const UCSMovementTuning& GetActiveTuning() const;

	/**
	 * Gait from the held and toggled gait keys: crouch (toggled) wins, then sprint (held), then walk
	 * (held), otherwise jog. Pressing sprint clears the crouch toggle (see OnSprintStarted).
	 */
	static ECSGait ResolveGait(bool bCrouchToggled, bool bSprintHeld, bool bWalkHeld);

	/** The physics-driven body: sway, stagger, ragdoll and recovery (spec 5.4). */
	UCSPhysicsBodyComponent* GetPhysicsBody() const { return PhysicsBody; }

	/** Dev tools (ragdoll and push keys, feel overlay, tuning reload). */
	UCSDevTools* GetDevTools() const { return DevTools; }

	/** Name of the view in use (FirstPerson or ThirdPerson), shown by the feel overlay. */
	virtual FName GetActiveCameraName() const;

	/** The camera key (V): switches the camera rig's view, then broadcasts OnToggleCameraRequested. */
	UFUNCTION(BlueprintCallable, Category = "CS|Camera")
	virtual void ToggleCamera();

	/** V was pressed (after the camera rig switched). */
	FCSToggleCameraRequested OnToggleCameraRequested;

	/** First- and third-person views on one camera. */
	UCSCameraRig* GetCameraRig() const { return CameraRig; }
	UCameraComponent* GetCamera() const { return Camera; }

	/** The engine's camera query; adds the camera rig's first-person near plane. */
	virtual void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;

	// ----- Ragdoll state (driven by the physics body; read by the camera rig) -----

	/** True from the moment the body goes fully simulated until control returns after recovery. */
	UFUNCTION(BlueprintPure, Category = "CS|Body")
	virtual bool IsRagdolled() const { return bRagdolled; }

	/**
	 * Called by the ragdoll owner on entering ragdoll (true) and when control returns (false).
	 * Broadcasts OnRagdollStateChanged on a change; on return, the held gait keys apply.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Body")
	void SetRagdolled(bool bInRagdolled);

	/** The ragdoll state changed (parameter: ragdolled). */
	FCSRagdollStateChanged OnRagdollStateChanged;

	// ----- Visuals (set in config or on a subclass) -----

	/** Given to the mesh component when it has no skeletal mesh (UE5 humanoid: faces +Y, origin at the feet). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Visuals")
	TSoftObjectPtr<USkeletalMesh> CharacterMesh;

	/** Given to the mesh component when it has no animation class, for example an animation Blueprint derived from UCSAnimInstance. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Visuals")
	TSoftClassPtr<UAnimInstance> CharacterAnimClass;

	// ----- Input (Enhanced Input; set in config or on a subclass) -----

	/** Added to the local player's Enhanced Input subsystem when a player controller possesses the character. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputMappingContext> InputMappingContext;

	/** Axis2D: X right, Y forward. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> MoveAction;

	/** Axis2D, degrees this frame: X yaw right, Y pitch up. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> LookAction;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> JumpAction;

	/** Held. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> SprintAction;

	/** Toggles on press. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> CrouchAction;

	/** Held. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> WalkAction;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> ToggleCameraAction;

	/** Dev tool: feel overlay. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> DebugOverlayAction;

	/** Dev tool: ragdoll. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> DebugRagdollAction;

	/** Dev tool: push. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> DebugPushAction;

	/** Dev tool: reload tuning (the console is compiled out of Shipping builds). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> DebugReloadAction;

	/** Dev tool: switch map (UCSDevTools::RequestMapSwitch; the game picks the map). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> DebugMapSwitchAction;

	/** Binds every configured action (an unset one is skipped with a warning). Public so tests can bind a component directly. */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

	/** Top speed of a gait on flat ground, cm/s. */
	float GetGaitSpeed(ECSGait InGait) const;

	/** Current slope speed scale (1 on flat ground). */
	float GetSlopeSpeedFactor() const { return SlopeSpeedFactor; }

	/** Vertical speed of the last landing, cm/s (positive down). */
	float GetLastLandingSpeed() const { return LastLandingSpeed; }

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Landed(const FHitResult& Hit) override;

private:
	/** Fills an empty mesh component from CharacterMesh and CharacterAnimClass. */
	void ApplyCharacterVisuals();
	float ComputeSlopeSpeedFactor(const UCSMovementTuning& Values) const;
	float GetGravityMagnitude() const;
	void UpdateGaitFromInput();

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJumpStarted();
	void OnJumpCompleted();
	void OnSprintStarted();
	void OnSprintCompleted();
	void OnWalkStarted();
	void OnWalkCompleted();
	void OnCrouchStarted();
	void OnToggleCameraStarted();
	void OnDebugOverlayStarted();
	void OnDebugRagdollStarted();
	void OnDebugPushStarted();
	void OnDebugReloadStarted();
	void OnDebugMapSwitchStarted();

	UPROPERTY(VisibleAnywhere, Category = "CS|Dev tools")
	TObjectPtr<UCSDevTools> DevTools;

	UPROPERTY(VisibleAnywhere, Category = "CS|Camera")
	TObjectPtr<UCSCameraRig> CameraRig;

	UPROPERTY(VisibleAnywhere, Category = "CS|Body")
	TObjectPtr<UCSPhysicsBodyComponent> PhysicsBody;

	/** Drives the simulated bodies toward the animated pose; set up and run by PhysicsBody. */
	UPROPERTY(VisibleAnywhere, Category = "CS|Body")
	TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;

	/** Third-person spring arm (right shoulder), run by CameraRig. */
	UPROPERTY(VisibleAnywhere, Category = "CS|Camera")
	TObjectPtr<UCSSpringArmComponent> CameraArm;

	/** The one camera; CameraRig places it each frame. */
	UPROPERTY(VisibleAnywhere, Category = "CS|Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** Owner-only copy of the body without head and neck, shown in first person (see UCSCameraRig). */
	UPROPERTY(VisibleAnywhere, Category = "CS|Camera")
	TObjectPtr<UPoseableMeshComponent> FirstPersonBody;

	/** The values in use, copied from Tuning by ApplyTuning. */
	UPROPERTY(Transient)
	TObjectPtr<UCSMovementTuning> AppliedTuning;

	bool bSprintHeld = false;
	bool bWalkHeld = false;
	bool bCrouchToggled = false;
	bool bRagdolled = false;

	ECSGait Gait = ECSGait::Jog;
	float SlopeSpeedFactor = 1.f;
	float LastLandingSpeed = 0.f;
};
