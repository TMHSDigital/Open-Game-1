#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "CSCameraRig.generated.h"

class ACharacter;
class ACSCharacter;
class UCameraComponent;
class UPoseableMeshComponent;
class USkeletalMeshComponent;

/** Camera view of a character. */
UENUM(BlueprintType)
enum class ECSView : uint8
{
	FirstPerson,
	ThirdPerson,
};

/**
 * A spring arm that does not tick on its own: UCSCameraRig updates it once a frame, after the
 * movement component has moved the capsule and the pose has been evaluated, so the third-person
 * camera never lags the character by a frame.
 */
UCLASS(ClassGroup = CS)
class CSCHARACTER_API UCSSpringArmComponent : public USpringArmComponent
{
	GENERATED_BODY()

public:
	UCSSpringArmComponent(const FObjectInitializer& ObjectInitializer);

	/** Runs the arm (collision probe, positional lag) for this frame. Without lag the arm snaps. */
	void UpdateArm(float DeltaSeconds, bool bAllowLag);
};

/**
 * First- and third-person views on one camera, switched by ToggleView (V) with a blend.
 *
 * Third person: a spring arm over the right shoulder, collision-aware, with positional lag and no
 * rotational lag, pivoting on the (smoothed) eyes. A switch moves the camera in a straight line
 * between the two views and skips the stretch, if any, that is inside the capsule but outside the
 * hidden head, so the camera is never inside the visible body.
 *
 * First person: the camera follows the body's eye socket (EyeSocketName) sideways and forward,
 * smoothed in the mesh's own space, so movement and turning are followed at once but sway of the
 * head (animation, later physics) is filtered. Its height above the feet is the tuning's fixed eye
 * height for the stance (standing or crouched), eased between the two: the run cycle bounces the
 * head by up to 24 cm and none of that reaches the view (no head bob). The view rotation is always
 * the control rotation with the roll removed: the head's pitch and roll never reach the view and the
 * view is never rolled. The near plane is pulled in (FirstPersonNearClip) so hands do not clip.
 *
 * Head and neck are hidden from the owner only. Bone hiding on the body would hide them from every
 * viewer, so the rig shows an owner-only copy of the body (FirstPersonBody: a poseable mesh with
 * bOnlyOwnerSee, a per-frame copy of the body's pose, OwnerHiddenBones hidden) and makes the body
 * bOwnerNoSee while the camera is within HeadHideRadius of the eyes. Other viewers keep seeing the
 * full body. Shadows swap with the views: in third person the body casts the shadow (it has
 * bCastHiddenShadow); in first person the headless copy casts the owner's shadow and the body's
 * hidden shadow is off, so the owner never sees its own head's shadow on its chest.
 *
 * While the character is ragdolled (ACSCharacter::IsRagdolled) the effective view is third person,
 * whatever the chosen view; the chosen view returns on recovery.
 *
 * Feel values come from the owner's applied tuning every frame (UCSMovementTuning: FOVs, blend
 * time, arm length, shoulder offset, lag, first-person smoothing and eye heights), so a tuning
 * reload changes the rig at once. Ticks in TG_PostPhysics after the movement component and the body mesh.
 */
UCLASS(ClassGroup = CS, meta = (BlueprintSpawnableComponent))
class CSCHARACTER_API UCSCameraRig : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSCameraRig();

	/** Gives the rig the components it drives. ACSCharacter calls this from PostInitializeComponents. */
	void Setup(USkeletalMeshComponent* InBody, UCSSpringArmComponent* InArm, UCameraComponent* InCamera, UPoseableMeshComponent* InFirstPersonBody);

	/** Switches the chosen view (V). During a ragdoll only the choice changes; the view stays third person. */
	UFUNCTION(BlueprintCallable, Category = "CS|Camera")
	void ToggleView();

	UFUNCTION(BlueprintCallable, Category = "CS|Camera")
	void SetView(ECSView NewView);

	/** The view the player chose. */
	UFUNCTION(BlueprintPure, Category = "CS|Camera")
	ECSView GetView() const { return View; }

	/** The view in use: the chosen view, except third person while the character is ragdolled. */
	UFUNCTION(BlueprintPure, Category = "CS|Camera")
	ECSView GetEffectiveView() const;

	/** FirstPerson or ThirdPerson: the effective view, for the feel overlay. */
	FName GetActiveCameraName() const;

	/** 0 in third person, 1 in first person, eased in between during a blend. */
	UFUNCTION(BlueprintPure, Category = "CS|Camera")
	float GetFirstPersonWeight() const { return FirstPersonWeight; }

	/** The first-person camera point in world space: the eye socket (or the pawn eye without one) sideways and forward, smoothed, at the stance eye height. */
	UFUNCTION(BlueprintPure, Category = "CS|Camera")
	FVector GetEyeLocation() const;

	/** True while the head and neck are hidden from the owner (the camera is within HeadHideRadius of the eyes). */
	UFUNCTION(BlueprintPure, Category = "CS|Camera")
	bool IsHeadHiddenForOwner() const { return bHeadHiddenForOwner; }

	/** Near plane for the current view, cm; 0 or less means the engine default. */
	float GetNearClipPlane() const;

	UCameraComponent* GetCamera() const { return Camera; }
	UCSSpringArmComponent* GetArm() const { return Arm; }
	UPoseableMeshComponent* GetFirstPersonBody() const { return FirstPersonBody; }

	/**
	 * Gives the owner-only first-person body the body's mesh and materials and hides OwnerHiddenBones
	 * on it. Runs at BeginPlay; call again after changing the body's mesh or materials.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Camera")
	void SyncFirstPersonBody();

	/**
	 * Asks for the body to turn toward the aim (the combat stance) whatever the view; first person
	 * always does. The character sets this every tick.
	 */
	void SetFaceAimRequested(bool bRequested) { bFaceAimRequested = bRequested; }

	/** The body is turning toward the aim (first person, or requested), not toward its movement. */
	bool IsFacingAim() const { return bAppliedFaceAim; }

	/** Runs the rig for this frame (TickComponent calls it). */
	void UpdateRig(float DeltaSeconds);

	/** View at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Camera")
	ECSView DefaultView = ECSView::ThirdPerson;

	/**
	 * While the character is ragdolled or getting up, the third-person camera pivots on this bone,
	 * RagdollPivotHeight above it, instead of the eyes, so it follows the body down to the ground.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Camera")
	FName RagdollPivotBone = TEXT("pelvis");

	/** See RagdollPivotBone, cm: about the eyes' height above the pelvis when standing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Camera", meta = (Units = "cm"))
	float RagdollPivotHeight = 65.f;

	/** How quickly the pivot moves to the body and back, seconds (time constant). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Camera", meta = (ClampMin = "0", Units = "s"))
	float RagdollPivotSeconds = 0.15f;

	/** Socket on the body mesh the first-person camera sits on (on the head bone, between the eyes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Camera")
	FName EyeSocketName = TEXT("eyes");

	/** Bones hidden from the owner in first person (UE5 humanoid names). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Camera")
	TArray<FName> OwnerHiddenBones = {TEXT("neck_01"), TEXT("neck_02"), TEXT("head")};

	/** Near plane in first person, cm (the engine default is 10). Close enough that hands raised to the face do not clip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Camera", meta = (ClampMin = "1", Units = "cm"))
	float FirstPersonNearClip = 3.f;

	/**
	 * The head and neck are hidden from the owner while the camera is this close to the eyes, cm:
	 * first person, the end of a blend into it, and a third-person arm pushed into the head by a
	 * wall. 30 covers the head (its back is about 21 cm behind the eyes) and the neck down to its base.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Camera", meta = (ClampMin = "5", Units = "cm"))
	float HeadHideRadius = 30.f;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** The camera between ThirdPerson and Eye at Weight, skipping the part of the path inside the capsule but outside the hidden head. */
	FVector BlendLocation(const FVector& ThirdPerson, const FVector& Eye, float Weight) const;
	bool IsInsideCapsule(const FVector& Point) const;

	/** A blend that skips into the hidden-head zone stops this far inside it, cm. */
	static constexpr float HeadZoneMargin = 1.f;

	/** The eye point in the body mesh's space, unsmoothed. */
	FVector ReadEyeInBodySpace() const;
	void SetHeadHiddenForOwner(bool bHidden);
	void ApplyRotationMode(bool bFaceAim);

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(Transient)
	TObjectPtr<UCSSpringArmComponent> Arm;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> FirstPersonBody;

	ECSView View = ECSView::ThirdPerson;
	/** Effective view the last update blended toward. */
	ECSView BlendTarget = ECSView::ThirdPerson;
	/** Linear blend progress toward first person, 0..1; FirstPersonWeight is its eased value. */
	float BlendProgress = 0.f;
	float FirstPersonWeight = 0.f;
	FVector SmoothedEyeInBody = FVector::ZeroVector;
	bool bHasEye = false;
	bool bHeadHiddenForOwner = false;
	mutable bool bWarnedNoSocket = false;

	bool bFaceAimRequested = false;
	bool bAppliedFaceAim = false;
	/** Where the third-person pivot is moved from the eyes (toward the ragdolled body), smoothed. */
	FVector PivotOffset = FVector::ZeroVector;

	/** The movement component's rotation settings in third person, restored when leaving first person. */
	bool bThirdPersonOrientToMovement = true;
	bool bThirdPersonControllerDesiredRotation = false;
};
