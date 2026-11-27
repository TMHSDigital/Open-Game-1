#include "CSCharacter.h"

#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CSCameraRig.h"
#include "CSCharacterLog.h"
#include "CSDevTools.h"
#include "CSGuardComponent.h"
#include "CSStaminaComponent.h"
#include "CSMovementTuning.h"
#include "CSPhysicsBodyComponent.h"
#include "CSStrikeComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "UObject/UnrealType.h"

namespace
{
	// Capsule for a 180 cm figure: half height 90 puts the mesh origin (the feet) at -90.
	constexpr float CapsuleRadius = 35.f;
	constexpr float CapsuleHalfHeight = 90.f;
	constexpr float CrouchedHalfHeight = 60.f;
	// Pawn eye heights above the capsule centre (the camera rig uses the mesh's eye socket; these are
	// its fallback and the engine's eye viewpoint): eyes at 168 cm standing, about 110 cm crouched.
	constexpr float StandingEyeHeightAboveCentre = 78.f;
	constexpr float CrouchedEyeHeightAboveCentre = 50.f;
	// Used only before the character is in a world (no gravity to read yet).
	constexpr float FallbackGravity = 980.f;
	// The sprint ramp is defined as reaching this share of SprintSpeed in SprintRampSeconds.
	constexpr float SprintRampShare = 0.95f;
}

ACSCharacter::ACSCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);

	// UE5 humanoid meshes face +Y with the origin at the feet; the actor faces +X.
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -CapsuleHalfHeight), FRotator(0.f, -90.f, 0.f));
	// In first person the owner does not see this mesh (it sees FirstPersonBody), so it is not
	// rendered locally; it must still pose every frame (eye socket, pose copy). Its hidden shadow is the
	// owner's shadow in third person; the camera rig swaps it to FirstPersonBody in first person.
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->bCastHiddenShadow = true;
	BaseEyeHeight = StandingEyeHeightAboveCentre;
	CrouchedEyeHeight = CrouchedEyeHeightAboveCentre;

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->GetNavAgentPropertiesRef().bCanCrouch = true;
	Move->SetCrouchedHalfHeight(CrouchedHalfHeight);
	// Braking friction comes from the tuning asset as a literal value.
	Move->bUseSeparateBrakingFriction = true;
	Move->BrakingFrictionFactor = 1.f;
	// The engine ticks a movement component before its owner by default; this class sets the
	// speed caps in its own Tick, so it must run first (see BeginPlay).
	Move->bTickBeforeOwner = false;

	DevTools = CreateDefaultSubobject<UCSDevTools>(TEXT("DevTools"));

	CameraArm = CreateDefaultSubobject<UCSSpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(GetCapsuleComponent());
	// Over the right shoulder, positional lag only. Length, offset and lag come from the
	// applied tuning; the camera rig sets them every frame.
	CameraArm->bUsePawnControlRotation = true;
	CameraArm->bDoCollisionTest = true;
	CameraArm->bEnableCameraLag = true;
	CameraArm->bEnableCameraRotationLag = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->bUsePawnControlRotation = false;

	FirstPersonBody = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("FirstPersonBody"));
	FirstPersonBody->SetupAttachment(Body);
	FirstPersonBody->SetOnlyOwnerSee(true);
	FirstPersonBody->SetCastShadow(false); // on only in first person (UCSCameraRig::SetHeadHiddenForOwner)
	FirstPersonBody->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	FirstPersonBody->SetGenerateOverlapEvents(false);
	FirstPersonBody->SetVisibility(false);
	// Posed by the camera rig after the body; it needs no tick of its own.
	FirstPersonBody->PrimaryComponentTick.bCanEverTick = false;

	CameraRig = CreateDefaultSubobject<UCSCameraRig>(TEXT("CameraRig"));

	PhysicalAnimation = CreateDefaultSubobject<UPhysicalAnimationComponent>(TEXT("PhysicalAnimation"));
	PhysicsBody = CreateDefaultSubobject<UCSPhysicsBodyComponent>(TEXT("PhysicsBody"));
	Strike = CreateDefaultSubobject<UCSStrikeComponent>(TEXT("Strike"));
	Guard = CreateDefaultSubobject<UCSGuardComponent>(TEXT("Guard"));
	Stamina = CreateDefaultSubobject<UCSStaminaComponent>(TEXT("Stamina"));
}

const UCSMovementTuning& ACSCharacter::GetActiveTuning() const
{
	if (AppliedTuning)
	{
		return *AppliedTuning.Get();
	}
	return Tuning ? *Tuning.Get() : *GetDefault<UCSMovementTuning>();
}

ECSGait ACSCharacter::ResolveGait(bool bCrouchToggled, bool bSprintHeld, bool bWalkHeld)
{
	if (bCrouchToggled)
	{
		return ECSGait::Crouch;
	}
	if (bSprintHeld)
	{
		return ECSGait::Sprint;
	}
	return bWalkHeld ? ECSGait::Walk : ECSGait::Jog;
}

float ACSCharacter::GetGaitSpeed(ECSGait InGait) const
{
	const UCSMovementTuning& T = GetActiveTuning();
	switch (InGait)
	{
	case ECSGait::Walk: return T.WalkSpeed;
	case ECSGait::Sprint: return T.SprintSpeed;
	case ECSGait::Crouch: return T.CrouchSpeed;
	case ECSGait::Jog:
	default: return T.JogSpeed;
	}
}

float ACSCharacter::GetGravityMagnitude() const
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	if (GetWorld() && Move)
	{
		const float G = FMath::Abs(Move->GetGravityZ());
		if (G > KINDA_SMALL_NUMBER)
		{
			return G;
		}
	}
	return FallbackGravity;
}

const UCSMovementTuning& ACSCharacter::GetPackagedTuning() const
{
	return Tuning ? *Tuning.Get() : *GetDefault<UCSMovementTuning>();
}

UCSMovementTuning* ACSCharacter::MakeTuningCopy(const UCSMovementTuning* Source)
{
	// Run on a copy, so later edits to the asset (a bad value typed mid-play) change nothing until
	// they are applied or reloaded. With no asset the copy holds the class defaults.
	return NewObject<UCSMovementTuning>(this, NAME_None, RF_Transient, const_cast<UCSMovementTuning*>(Source));
}

void ACSCharacter::ApplyTuning()
{
	ApplyTuningFrom(Tuning.Get());
}

void ACSCharacter::ApplyTuningFrom(const UCSMovementTuning* Source)
{
	UCSMovementTuning* Values = MakeTuningCopy(Source);
	// Overrides are a dev tool: a tuning with bDevTools off runs exactly as packaged.
	if (Values->bDevTools && TuningOverrides.Values.Num() > 0)
	{
		FString Reason;
		if (!TuningOverrides.ApplyTo(*Values, Reason))
		{
			UE_LOG(LogCSCharacter, Warning, TEXT("%s: tuning changes dropped, %s."), *GetName(), *Reason);
			TuningOverrides = FCSTuningOverrides();
		}
	}
	CommitTuning(Values);
}

bool ACSCharacter::TryApplyTuning(const FCSTuningOverrides& Overrides, FString& OutReason)
{
	UCSMovementTuning* Values = MakeTuningCopy(Tuning.Get());
	if (!Overrides.ApplyTo(*Values, OutReason) || !Values->IsValid(OutReason))
	{
		return false;
	}
	TuningOverrides = Overrides;
	CommitTuning(Values);
	return true;
}

void ACSCharacter::CommitTuning(UCSMovementTuning* Values)
{
	AppliedTuning = Values;
	const UCSMovementTuning& T = *AppliedTuning;
	UCharacterMovementComponent* Move = GetCharacterMovement();

	Move->MaxAcceleration = T.Acceleration;
	Move->GroundFriction = T.GroundFriction;
	Move->RotationRate = FRotator(0.f, T.RotationRateYaw, 0.f);
	Move->BrakingDecelerationWalking = T.BrakingDeceleration;
	Move->bUseSeparateBrakingFriction = true;
	Move->BrakingFrictionFactor = 1.f;
	Move->BrakingFriction = T.BrakingFriction;
	Move->AirControl = T.AirControl;
	Move->SetWalkableFloorAngle(T.MaxWalkableSlopeDeg);
	// Apex height h of a jump with take-off speed v under gravity g: v = sqrt(2 g h).
	Move->JumpZVelocity = FMath::Sqrt(2.f * GetGravityMagnitude() * T.JumpHeightCm);

	// A sprint starts its ramp from jog speed; Tick raises the cap from there.
	Move->MaxWalkSpeed = GetGaitSpeed(Gait == ECSGait::Sprint ? ECSGait::Jog : Gait) * SlopeSpeedFactor;
	Move->MaxWalkSpeedCrouched = T.CrouchSpeed * SlopeSpeedFactor;
	OnTuningApplied.Broadcast();
}

void ACSCharacter::SetGait(ECSGait NewGait)
{
	Gait = NewGait;
	if (Gait == ECSGait::Crouch)
	{
		Crouch();
	}
	else if (bIsCrouched || GetCharacterMovement()->bWantsToCrouch)
	{
		UnCrouch();
	}
	if (Gait != ECSGait::Sprint)
	{
		GetCharacterMovement()->MaxWalkSpeed = GetGaitSpeed(Gait) * SlopeSpeedFactor;
	}
}

void ACSCharacter::ApplyCharacterVisuals()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}
	if (!MeshComp->GetSkeletalMeshAsset() && !CharacterMesh.IsNull())
	{
		if (USkeletalMesh* Loaded = CharacterMesh.LoadSynchronous())
		{
			MeshComp->SetSkeletalMeshAsset(Loaded);
		}
		else
		{
			UE_LOG(LogCSCharacter, Warning, TEXT("%s: CharacterMesh %s could not be loaded."), *GetName(), *CharacterMesh.ToString());
		}
	}
	if (!MeshComp->GetAnimClass() && !CharacterAnimClass.IsNull())
	{
		if (UClass* Loaded = CharacterAnimClass.LoadSynchronous())
		{
			MeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			MeshComp->SetAnimInstanceClass(Loaded);
		}
		else
		{
			UE_LOG(LogCSCharacter, Warning, TEXT("%s: CharacterAnimClass %s could not be loaded."), *GetName(), *CharacterAnimClass.ToString());
		}
	}
}

void ACSCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyCharacterVisuals();
}

void ACSCharacter::PostInitializeComponents()
{
	// Before Super: ACharacter sets up the mesh's tick and pose there.
	ApplyCharacterVisuals();
	Super::PostInitializeComponents();
	CameraRig->Setup(GetMesh(), CameraArm, Camera, FirstPersonBody);
	PhysicsBody->Setup(GetMesh(), PhysicalAnimation);
}

void ACSCharacter::BeginPlay()
{
	Super::BeginPlay();
	// Speed caps are set in Tick and must be in place before the movement component uses them in
	// the same frame; otherwise the ramp lags a frame (33 ms at 30 fps), which makes the time to
	// sprint speed depend on the frame rate.
	GetCharacterMovement()->AddTickPrerequisiteActor(this);

	FString Reason;
	if (Tuning && !Tuning->IsValid(Reason))
	{
		// No last good values yet: start on the class defaults; a fixed asset can be reloaded.
		UE_LOG(LogCSCharacter, Error, TEXT("%s: tuning %s is invalid (%s); using the class defaults until it is fixed and reloaded."),
			*GetName(), *Tuning->GetPathName(), *Reason);
		ApplyTuningFrom(nullptr);
		return;
	}
	ApplyTuning();
}

void ACSCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	const APlayerController* PC = Cast<APlayerController>(Controller);
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	// The local player's character runs on the tuning changes saved on this machine (a respawned or
	// newly loaded character picks them up here).
	if (PC && PC->IsLocalController() && DevTools)
	{
		DevTools->LoadOverrides();
	}
	if (!Subsystem)
	{
		return;
	}
	UInputMappingContext* Context = InputMappingContext.LoadSynchronous();
	if (!Context)
	{
		UE_LOG(LogCSCharacter, Warning, TEXT("%s: InputMappingContext is not set (config section [/Script/CSCharacter.CSCharacter]); keys will do nothing."), *GetName());
		return;
	}
	if (!Subsystem->HasMappingContext(Context))
	{
		// Registered with the Enhanced Input user settings (when the project enables them), so the
		// player's own keys for its rebindable actions apply.
		FModifyContextOptions Options;
		Options.bNotifyUserSettings = true;
		Subsystem->AddMappingContext(Context, 0, Options);
	}
}

void ACSCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogCSCharacter, Error, TEXT("%s: the input component is not a UEnhancedInputComponent (set DefaultInputComponentClass); keys will do nothing."), *GetName());
		return;
	}
	// One row per action and trigger event: the slot, when it fires, and its handler (a press handler,
	// or a value handler for the axes). A new action is a property, a handler and a row here; the
	// test rigs find the properties by reflection (InputActionProperties), and
	// CombatSimulator.CS.DevTools.EveryInputActionIsBound fails when a property has no row.
	// Dev keys are always bound; UCSDevTools decides at press time whether they do anything, so a
	// reload can turn bDevTools on or off (the reload key itself reads the source tuning).
	struct FBinding
	{
		TSoftObjectPtr<UInputAction> ACSCharacter::* Slot;
		const TCHAR* Name;
		ETriggerEvent Event;
		void (ACSCharacter::*Press)();
		void (ACSCharacter::*Value)(const FInputActionValue&);
	};
#define CS_VALUE(Slot, Handler) {&ACSCharacter::Slot, TEXT(#Slot), ETriggerEvent::Triggered, nullptr, &ACSCharacter::Handler}
#define CS_PRESS(Slot, Event, Handler) {&ACSCharacter::Slot, TEXT(#Slot), ETriggerEvent::Event, &ACSCharacter::Handler, nullptr}
	static const FBinding Bindings[] = {
		CS_VALUE(MoveAction, OnMove),
		CS_VALUE(LookAction, OnLook),
		CS_VALUE(LookStickAction, OnLookStick),
		CS_PRESS(JumpAction, Started, OnJumpStarted),
		CS_PRESS(JumpAction, Completed, OnJumpCompleted),
		CS_PRESS(SprintAction, Started, OnSprintStarted),
		CS_PRESS(SprintAction, Completed, OnSprintCompleted),
		CS_PRESS(WalkAction, Started, OnWalkStarted),
		CS_PRESS(WalkAction, Completed, OnWalkCompleted),
		CS_PRESS(CrouchAction, Started, OnCrouchStarted),
		CS_PRESS(ToggleCameraAction, Started, OnToggleCameraStarted),
		CS_PRESS(AttackAction, Started, OnAttackStarted),
		CS_PRESS(KickAction, Started, OnKickStarted),
		CS_PRESS(BlockAction, Started, OnBlockStarted),
		CS_PRESS(BlockAction, Completed, OnBlockCompleted),
		CS_PRESS(DodgeAction, Started, OnDodgeStarted),
		CS_PRESS(DebugOverlayAction, Started, OnDebugOverlayStarted),
		CS_PRESS(DebugRagdollAction, Started, OnDebugRagdollStarted),
		CS_PRESS(DebugPushAction, Started, OnDebugPushStarted),
		CS_PRESS(DebugReloadAction, Started, OnDebugReloadStarted),
		CS_PRESS(DebugMapSwitchAction, Started, OnDebugMapSwitchStarted),
		CS_PRESS(DebugThrowAction, Started, OnDebugThrowStarted),
		CS_PRESS(DebugResetAction, Started, OnDebugResetStarted),
	};
#undef CS_VALUE
#undef CS_PRESS

	TSoftObjectPtr<UInputAction> ACSCharacter::* Warned = nullptr;
	for (const FBinding& Row : Bindings)
	{
		UInputAction* Action = (this->*Row.Slot).LoadSynchronous();
		if (!Action)
		{
			// Once per action, not per trigger event (an action's rows are next to each other).
			if (Row.Slot != Warned)
			{
				Warned = Row.Slot;
				UE_LOG(LogCSCharacter, Warning, TEXT("%s: %s is not set; that key does nothing."), *GetName(), Row.Name);
			}
			continue;
		}
		if (Row.Value)
		{
			Input->BindAction(Action, Row.Event, this, Row.Value);
		}
		else
		{
			Input->BindAction(Action, Row.Event, this, Row.Press);
		}
	}
}

TArray<FSoftObjectProperty*> ACSCharacter::InputActionProperties()
{
	TArray<FSoftObjectProperty*> Slots;
	for (TFieldIterator<FSoftObjectProperty> It(StaticClass()); It; ++It)
	{
		if (It->PropertyClass && It->PropertyClass->IsChildOf(UInputAction::StaticClass()))
		{
			Slots.Add(*It);
		}
	}
	return Slots;
}

void ACSCharacter::OnMove(const FInputActionValue& Value)
{
	if (bRagdolled)
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	// Along the view's yaw only, so looking up or down does not slow the character.
	const FRotationMatrix Basis(FRotator(0.f, GetControlRotation().Yaw, 0.f));
	AddMovementInput(Basis.GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(Basis.GetUnitAxis(EAxis::Y), Axis.X);
}

bool ACSCharacter::PickHook(bool bInStance, float Sideways, ECSStrikeMove& OutHook)
{
	if (!bInStance || FMath::Abs(Sideways) < 0.7f)
	{
		return false;
	}
	OutHook = Sideways > 0.f ? ECSStrikeMove::HookRight : ECSStrikeMove::HookLeft;
	return true;
}

bool ACSCharacter::Attack()
{
	// Mid-dodge the body is carried by the dodge: a punch would aim at nothing and lose its step in.
	if (!Strike || (Guard && Guard->IsDodging()))
	{
		return false;
	}
	const FVector Wanted = GetCharacterMovement() ? GetCharacterMovement()->GetCurrentAcceleration().GetSafeNormal2D() : FVector::ZeroVector;
	const float Sideways = float(FVector::DotProduct(Wanted, GetActorRightVector().GetSafeNormal2D()));
	ECSStrikeMove Hook = ECSStrikeMove::HookRight;
	if (PickHook(bCombatStance && !Wanted.IsNearlyZero(), Sideways, Hook))
	{
		return StrikeWith(Hook);
	}
	if (!Strike->StartPunch())
	{
		return false;
	}
	LastAttackTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	return true;
}

bool ACSCharacter::StrikeWith(ECSStrikeMove Move)
{
	if (!Strike || (Guard && Guard->IsDodging()) || !Strike->StartMove(Move))
	{
		return false;
	}
	LastAttackTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	return true;
}

void ACSCharacter::OnKickStarted()
{
	Kick();
}

bool ACSCharacter::DodgeStep()
{
	// The way the movement keys point now; none held: straight back (the guard's default).
	const FVector Wanted = GetCharacterMovement() ? GetCharacterMovement()->GetCurrentAcceleration() : FVector::ZeroVector;
	return Guard && Guard->Dodge(Wanted);
}

void ACSCharacter::OnBlockStarted()
{
	if (Guard)
	{
		Guard->SetBlockHeld(true);
	}
}

void ACSCharacter::OnBlockCompleted()
{
	if (Guard)
	{
		Guard->SetBlockHeld(false);
	}
}

void ACSCharacter::OnDodgeStarted()
{
	DodgeStep();
}

void ACSCharacter::UpdateCombatStance()
{
	const UWorld* World = GetWorld();
	const UCSMovementTuning& T = GetActiveTuning();
	bool bStance = false;
	// Any controller: the player's, or an opponent's AI. A dummy with none stands as it was placed.
	if (World && GetController() && !bRagdolled)
	{
		const double Now = World->GetTimeSeconds();
		if (T.CombatFacingRange > 0.f)
		{
			const float RangeSquared = T.CombatFacingRange * T.CombatFacingRange;
			for (TActorIterator<ACSCharacter> It(World); It; ++It)
			{
				const ACSCharacter* Other = *It;
				if (Other != this && !Other->IsActorBeingDestroyed() && !Other->IsRagdolled()
					&& FVector::DistSquared2D(Other->GetActorLocation(), GetActorLocation()) <= RangeSquared)
				{
					LastNearTime = Now;
					break;
				}
			}
		}
		// Held for CombatFacingSeconds after an attack, and after the opponent was last in range:
		// backing off out of range must not turn the body around mid-retreat.
		bStance = Now - FMath::Max(LastAttackTime, LastNearTime) < T.CombatFacingSeconds;
	}
	bCombatStance = bStance;
	if (CameraRig)
	{
		CameraRig->SetFaceAimRequested(bCombatStance);
	}
}

void ACSCharacter::OnAttackStarted()
{
	Attack();
}

void ACSCharacter::OnDebugThrowStarted()
{
	DevTools->RequestThrow();
}

void ACSCharacter::OnDebugResetStarted()
{
	DevTools->RequestReset();
}

void ACSCharacter::SetLookScale(FVector2D MouseScale, FVector2D StickScale)
{
	MouseLookScale = MouseScale;
	StickLookScale = StickScale;
}

FVector2D ACSCharacter::ScaleLook(const FVector2D& Degrees, bool bFromStick) const
{
	return Degrees * (bFromStick ? StickLookScale : MouseLookScale);
}

void ACSCharacter::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = ScaleLook(Value.Get<FVector2D>(), false);
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void ACSCharacter::OnLookStick(const FInputActionValue& Value)
{
	const FVector2D Axis = ScaleLook(Value.Get<FVector2D>(), true);
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void ACSCharacter::OnJumpStarted()
{
	if (bRagdolled)
	{
		return;
	}
	// The engine never jumps a crouched character, so a jump from a crouch clears the crouch toggle
	// and stands up now (if there is room; with no room it stays crouched and does not jump).
	if (bCrouchToggled)
	{
		bCrouchToggled = false;
		UpdateGaitFromInput();
	}
	if (bIsCrouched)
	{
		GetCharacterMovement()->UnCrouch();
	}
	Jump();
}

void ACSCharacter::OnJumpCompleted()
{
	StopJumping();
}

void ACSCharacter::OnSprintStarted()
{
	bSprintHeld = true;
	// Sprinting from a crouch stands the character up; Ctrl crouches again.
	bCrouchToggled = false;
	UpdateGaitFromInput();
}

void ACSCharacter::OnSprintCompleted()
{
	bSprintHeld = false;
	UpdateGaitFromInput();
}

void ACSCharacter::OnWalkStarted()
{
	bWalkHeld = true;
	UpdateGaitFromInput();
}

void ACSCharacter::OnWalkCompleted()
{
	bWalkHeld = false;
	UpdateGaitFromInput();
}

void ACSCharacter::OnCrouchStarted()
{
	if (bRagdolled)
	{
		return;
	}
	bCrouchToggled = !bCrouchToggled;
	UpdateGaitFromInput();
}

void ACSCharacter::UpdateGaitFromInput()
{
	// Held keys are tracked while ragdolled; SetRagdolled(false) applies them.
	if (bRagdolled)
	{
		return;
	}
	const ECSGait NewGait = ResolveGait(bCrouchToggled, bSprintHeld, bWalkHeld);
	if (NewGait != Gait)
	{
		SetGait(NewGait);
	}
}

void ACSCharacter::ToggleCamera()
{
	CameraRig->ToggleView();
	OnToggleCameraRequested.Broadcast();
}

FName ACSCharacter::GetActiveCameraName() const
{
	return CameraRig ? CameraRig->GetActiveCameraName() : FName(NAME_None);
}

void ACSCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	const float NearClip = CameraRig ? CameraRig->GetNearClipPlane() : 0.f;
	if (NearClip > 0.f)
	{
		OutResult.PerspectiveNearClipPlane = NearClip;
	}
}

void ACSCharacter::SetRagdolled(bool bInRagdolled)
{
	if (bRagdolled == bInRagdolled)
	{
		return;
	}
	bRagdolled = bInRagdolled;
	if (!bRagdolled)
	{
		UpdateGaitFromInput();
	}
	OnRagdollStateChanged.Broadcast(bRagdolled);
}

void ACSCharacter::OnToggleCameraStarted()
{
	ToggleCamera();
}

void ACSCharacter::OnDebugOverlayStarted()
{
	DevTools->ToggleOverlay();
}

void ACSCharacter::OnDebugRagdollStarted()
{
	DevTools->RequestRagdoll();
}

void ACSCharacter::OnDebugPushStarted()
{
	DevTools->RequestPush();
}

void ACSCharacter::OnDebugReloadStarted()
{
	DevTools->RequestReload();
}

void ACSCharacter::OnDebugMapSwitchStarted()
{
	DevTools->RequestMapSwitch();
}

float ACSCharacter::ComputeSlopeSpeedFactor(const UCSMovementTuning& T) const
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move->IsMovingOnGround() || !Move->CurrentFloor.IsWalkableFloor())
	{
		return 1.f;
	}
	FVector Dir = Move->GetCurrentAcceleration().GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = Move->Velocity.GetSafeNormal2D();
	}
	const FVector Normal = Move->CurrentFloor.HitResult.ImpactNormal;
	if (Dir.IsNearlyZero() || Normal.Z <= KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}
	// Rise per horizontal distance along Dir is -(N . Dir) / N.z; positive is uphill.
	const float SlopeAlong = FMath::Atan(-FVector::DotProduct(Normal, Dir) / Normal.Z);
	const float Sin = FMath::Sin(SlopeAlong);
	return Sin >= 0.f ? 1.f - T.UphillSpeedLoss * Sin : 1.f - T.DownhillSpeedGain * Sin;
}

void ACSCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UCSMovementTuning& T = GetActiveTuning();
	UCharacterMovementComponent* Move = GetCharacterMovement();
	SlopeSpeedFactor = ComputeSlopeSpeedFactor(T);
	UpdateCombatStance();

	float Cap = GetGaitSpeed(Gait);
	if (Gait == ECSGait::Sprint)
	{
		// The cap climbs from the current speed (at least jog) by a fixed rate per second, so the
		// speed, which follows the cap because Acceleration is much larger than the rate, reaches
		// 95 percent of sprint in SprintRampSeconds whatever the frame rate. Losing speed (a wall,
		// a landing) lowers the cap with it, so the ramp restarts from there.
		const float Rate = (SprintRampShare * T.SprintSpeed - T.JogSpeed) / T.SprintRampSeconds;
		const float FlatSpeed = Move->Velocity.Size2D() / SlopeSpeedFactor;
		Cap = Rate > 0.f
			? FMath::Clamp(FMath::Max(T.JogSpeed, FlatSpeed) + Rate * DeltaSeconds, T.JogSpeed, T.SprintSpeed)
			: T.SprintSpeed;
	}
	if (bCombatStance)
	{
		// Facing the aim: backing off is slower than going forward, and never a sprint.
		const FVector Wanted = Move->GetCurrentAcceleration().GetSafeNormal2D();
		const float Back = FMath::Clamp(-float(FVector::DotProduct(Wanted, GetActorForwardVector().GetSafeNormal2D())), 0.f, 1.f);
		if (Back > 0.5f)
		{
			Cap = FMath::Min(Cap, T.JogSpeed);
		}
		Cap *= FMath::Lerp(1.f, T.CombatBackSpeedScale, Back);
	}
	if (Guard)
	{
		// The guard slows movement while it is up; a dodge sets the velocity and lifts the cap.
		Cap = Guard->ApplyToMovement(*Move, Cap, T.JogSpeed);
	}
	Move->MaxWalkSpeed = Cap * SlopeSpeedFactor;

	// Grip blends from GroundFriction at jog speed to GroundFriction * SprintGripScale at sprint
	// speed, so the turn radius (about speed / grip) widens faster than speed alone.
	const float SprintShare = T.SprintSpeed > T.JogSpeed
		? FMath::Clamp((Cap - T.JogSpeed) / (T.SprintSpeed - T.JogSpeed), 0.f, 1.f)
		: 0.f;
	Move->GroundFriction = T.GroundFriction * FMath::Lerp(1.f, T.SprintGripScale, SprintShare);
	Move->MaxWalkSpeedCrouched = T.CrouchSpeed * SlopeSpeedFactor;
}

void ACSCharacter::Landed(const FHitResult& Hit)
{
	// Called before the movement component switches to walking, so Velocity still holds the fall.
	// Recorded before Super::Landed, which fires OnLanded and LandedDelegate: listeners (a ragdoll
	// trigger) read this landing's speed, not the previous one.
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const UCSMovementTuning& T = GetActiveTuning();
	LastLandingSpeed = FMath::Max(0.f, -Move->Velocity.Z);

	Super::Landed(Hit);

	// A normal jump lands at its take-off speed; only the fall speed above that costs speed.
	const float FreeSpeed = FMath::Sqrt(2.f * GetGravityMagnitude() * T.JumpHeightCm);
	const float ExcessMps = FMath::Max(0.f, LastLandingSpeed - FreeSpeed) / 100.f;
	const float Keep = FMath::Clamp(1.f - T.LandingSpeedLossPerMps * ExcessMps, 0.f, 1.f);
	Move->Velocity.X *= Keep;
	Move->Velocity.Y *= Keep;
}
