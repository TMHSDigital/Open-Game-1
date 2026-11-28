#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CSTuningOverrides.h"
#include "CSStrikeMove.h"
#include "CSCharacter.generated.h"

class UCameraComponent;
class UCSCameraRig;
class UCSDevTools;
class UCSGuardComponent;
class UCSStaminaComponent;
class UCSPhysicsBodyComponent;
class UCSStrikeComponent;
class UPhysicalAnimationComponent;
class UCSSpringArmComponent;
class UCSMovementTuning;
class UInputAction;
class UInputMappingContext;
class UPoseableMeshComponent;
class UAnimInstance;
class USkeletalMesh;
struct FInputActionValue;
class FSoftObjectProperty;

DECLARE_MULTICAST_DELEGATE(FCSToggleCameraRequested);
DECLARE_MULTICAST_DELEGATE(FCSTuningApplied);
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
 * Spawning: a character may be spawned at any point in a frame. When it is spawned inside a world
 * tick (from a timer, a spawner or a respawn) its physics body starts one frame later, so for that
 * frame it is plain animation and ignores hits (UCSPhysicsBodyComponent::bStartPending).
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
	 * Copies Tuning (or the class defaults), writes the current overrides (GetTuningOverrides) on top
	 * when the copy has bDevTools on, and pushes the values into the movement component.
	 * Called on BeginPlay (where an invalid asset is replaced by the class defaults, with an error
	 * logged); call again after changing Tuning. Does not validate: use UCSDevTools::ReloadTuning for that.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Movement")
	void ApplyTuning();

	/** ApplyTuning with Source in place of Tuning (null: the class defaults). Tuning is not changed. */
	void ApplyTuningFrom(const UCSMovementTuning* Source);

	/**
	 * Applies Tuning with Overrides on top, if the result is valid: the overrides must name rows of
	 * the range table with values in range, and the result must pass UCSMovementTuning::IsValid.
	 * On success the result becomes the active tuning and Overrides the character's overrides. On
	 * failure nothing changes and OutReason names the field. Not gated by bDevTools: the callers
	 * (UCSDevTools) are.
	 */
	bool TryApplyTuning(const FCSTuningOverrides& Overrides, FString& OutReason);

	/** The values the build was packaged with: Tuning, or the class defaults when it is unset. */
	const UCSMovementTuning& GetPackagedTuning() const;

	/**
	 * The active tuning was replaced (ApplyTuning, TryApplyTuning, a reload). Anything that keeps
	 * state worked out from the tuning (the physics body's motors and ragdoll physics) rewrites it
	 * here, in the same frame.
	 */
	FCSTuningApplied OnTuningApplied;

	/** The changes on top of the packaged tuning that are in use (empty: none). */
	const FCSTuningOverrides& GetTuningOverrides() const { return TuningOverrides; }

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

	/** The physics-driven body: sway, stagger, ragdoll and recovery. */
	UCSPhysicsBodyComponent* GetPhysicsBody() const { return PhysicsBody; }

	/** The melee attack: a punch steered through the physics body. */
	UCSStrikeComponent* GetStrike() const { return Strike; }

	/** The defence: block and dodge. */
	UCSGuardComponent* GetGuard() const { return Guard; }

	/** Stamina: what strikes, dodges and blocked hits cost (decision 0020). */
	UCSStaminaComponent* GetStamina() const { return Stamina; }

	/** Throws Move (UCSStrikeComponent::StartMove), as the keys do. Returns whether it started. */
	UFUNCTION(BlueprintCallable, Category = "CS|Strike")
	bool StrikeWith(ECSStrikeMove Move);

	/** The kick key: a front kick with the rear leg. */
	UFUNCTION(BlueprintCallable, Category = "CS|Strike")
	bool Kick() { return StrikeWith(ECSStrikeMove::Kick); }

	/**
	 * What the attack key throws now: a hook from the side the movement keys point to when moving
	 * sideways in the combat stance (Sideways: the movement's share across the facing, -1 left to 1
	 * right; a hook when its size is at least 0.7), else the punch (a jab, or the straight of a one-two).
	 */
	static bool PickHook(bool bInStance, float Sideways, ECSStrikeMove& OutHook);

	/**
	 * The dodge key: a quick step the way the character is moving, or straight back
	 * (UCSGuardComponent::Dodge). Returns whether one started.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Guard")
	bool DodgeStep();

	/**
	 * Combat stance: a controlled character (a player's, or an opponent's AI) faces its aim instead of
	 * the way it moves, while another standing character is within CombatFacingRange, and for
	 * CombatFacingSeconds after that or after an attack. A character with no controller (a dummy
	 * standing still) never does.
	 * Backing off is slower (CombatBackSpeedScale) and never a sprint. Worked out every tick.
	 */
	UFUNCTION(BlueprintPure, Category = "CS|Strike")
	bool IsInCombatStance() const { return bCombatStance; }

	/** The attack key: a hook while moving sideways in the stance (PickHook), else a punch (UCSStrikeComponent::StartPunch). Returns whether one started. */
	UFUNCTION(BlueprintCallable, Category = "CS|Strike")
	bool Attack();

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

	/**
	 * Every input action slot of the class (its TSoftObjectPtr<UInputAction> properties), in
	 * declaration order. SetupPlayerInputComponent binds each from one table; tests use this list so
	 * a new action needs no test rig change.
	 */
	static TArray<FSoftObjectProperty*> InputActionProperties();

	/** Added to the local player's Enhanced Input subsystem when a player controller possesses the character. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputMappingContext> InputMappingContext;

	/** Axis2D: X right, Y forward. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> MoveAction;

	/** Mouse look. Axis2D, degrees this frame: X yaw right, Y pitch up. Scaled by the mouse look scale. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> LookAction;
	/**
	 * Gamepad look, the same values as LookAction. A separate action so the stick has its own
	 * sensitivity (the stick look scale).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> LookStickAction;

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

	/** Throws a punch on press. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> AttackAction;

	/** A front kick on press. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> KickAction;

	/** Held: the guard is up (UCSGuardComponent). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> BlockAction;

	/** A dodge step on press. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> DodgeAction;

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
	/** Dev tool: throw a ball where the view points (UCSDevTools::RequestThrow). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> DebugThrowAction;
	/** Dev tool: put the player, the dummies and the props back (UCSDevTools::RequestReset; the game does it). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "CS|Input")
	TSoftObjectPtr<UInputAction> DebugResetAction;

	/**
	 * The player's look settings: mouse and stick look are multiplied by these (X yaw, Y pitch; a
	 * negative Y inverts the pitch). (1, 1) leaves the mapping context's own scale, which is the
	 * tested base speed. Set by the game from its settings; the plugin stores nothing.
	 */
	void SetLookScale(FVector2D MouseScale, FVector2D StickScale);
	FVector2D GetMouseLookScale() const { return MouseLookScale; }
	FVector2D GetStickLookScale() const { return StickLookScale; }

	/** A look value from the mouse or the stick with its look scale applied: what turns the view. */
	FVector2D ScaleLook(const FVector2D& Degrees, bool bFromStick) const;

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
	void OnLookStick(const FInputActionValue& Value);
	void OnJumpStarted();
	void OnJumpCompleted();
	void OnSprintStarted();
	void OnSprintCompleted();
	void OnWalkStarted();
	void OnWalkCompleted();
	void OnCrouchStarted();
	void OnToggleCameraStarted();
	void OnAttackStarted();
	void OnBlockStarted();
	void OnBlockCompleted();
	void OnDodgeStarted();
	void OnKickStarted();
	void OnDebugOverlayStarted();
	void OnDebugRagdollStarted();
	void OnDebugPushStarted();
	void OnDebugReloadStarted();
	void OnDebugMapSwitchStarted();
	void OnDebugThrowStarted();
	void OnDebugResetStarted();

	UPROPERTY(VisibleAnywhere, Category = "CS|Dev tools")
	TObjectPtr<UCSDevTools> DevTools;

	UPROPERTY(VisibleAnywhere, Category = "CS|Camera")
	TObjectPtr<UCSCameraRig> CameraRig;

	UPROPERTY(VisibleAnywhere, Category = "CS|Body")
	TObjectPtr<UCSPhysicsBodyComponent> PhysicsBody;

	UPROPERTY(VisibleAnywhere, Category = "CS|Strike")
	TObjectPtr<UCSStrikeComponent> Strike;

	UPROPERTY(VisibleAnywhere, Category = "CS|Guard")
	TObjectPtr<UCSGuardComponent> Guard;

	UPROPERTY(VisibleAnywhere, Category = "CS|Stamina")
	TObjectPtr<UCSStaminaComponent> Stamina;

	bool bCombatStance = false;
	/** Distance to the nearest other standing character, cm, from the last UpdateCombatStance (huge: none). */
	float NearestOtherDistance = TNumericLimits<float>::Max();
	/** World time of the last attack started; very negative before any. */
	double LastAttackTime = -1.0e9;
	/** World time another standing character was last within CombatFacingRange. */
	double LastNearTime = -1.0e9;
	void UpdateCombatStance();

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

	/** Changes written on top of the copy (see TryApplyTuning). */
	FCSTuningOverrides TuningOverrides;

	/** A fresh copy of Source (null: the class defaults), without overrides. */
	UCSMovementTuning* MakeTuningCopy(const UCSMovementTuning* Source);
	/** Makes Values the active tuning and pushes it into the movement component. */
	void CommitTuning(UCSMovementTuning* Values);

	FVector2D MouseLookScale = FVector2D(1.0, 1.0);
	FVector2D StickLookScale = FVector2D(1.0, 1.0);

	bool bSprintHeld = false;
	bool bWalkHeld = false;
	bool bCrouchToggled = false;
	bool bRagdolled = false;

	ECSGait Gait = ECSGait::Jog;
	float SlopeSpeedFactor = 1.f;
	float LastLandingSpeed = 0.f;
};
