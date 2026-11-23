#include "CSCameraRig.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CSCharacter.h"
#include "CSCharacterLog.h"
#include "CSMovementTuning.h"
#include "GameFramework/CharacterMovementComponent.h"

UCSSpringArmComponent::UCSSpringArmComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// UCSCameraRig runs the arm after movement and the pose; its own tick would run before them.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UCSSpringArmComponent::UpdateArm(float DeltaSeconds, bool bAllowLag)
{
	UpdateDesiredArmLocation(bDoCollisionTest, bAllowLag && bEnableCameraLag, bAllowLag && bEnableCameraRotationLag, DeltaSeconds);
}

UCSCameraRig::UCSCameraRig()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// After the movement component has moved the capsule and the body's pose (and, later, its
	// physics) is final for the frame; the player camera manager reads the camera after all tick groups.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UCSCameraRig::Setup(USkeletalMeshComponent* InBody, UCSSpringArmComponent* InArm, UCameraComponent* InCamera, UPoseableMeshComponent* InFirstPersonBody)
{
	Body = InBody;
	Arm = InArm;
	Camera = InCamera;
	FirstPersonBody = InFirstPersonBody;
}

void UCSCameraRig::BeginPlay()
{
	Super::BeginPlay();

	// The owner's actor tick runs before its movement component (ACSCharacter sets the speed caps
	// there), so the rig depends on the movement component and the body mesh directly. Neither
	// depends back on the rig, so there is no cycle.
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			PrimaryComponentTick.AddPrerequisite(Move, Move->PrimaryComponentTick);
			bThirdPersonOrientToMovement = Move->bOrientRotationToMovement;
			bThirdPersonControllerDesiredRotation = Move->bUseControllerDesiredRotation;
		}
	}
	if (Body)
	{
		PrimaryComponentTick.AddPrerequisite(Body, Body->PrimaryComponentTick);
	}
	SyncFirstPersonBody();

	// Start in the default view with no blend.
	View = DefaultView;
	BlendTarget = GetEffectiveView();
	BlendProgress = BlendTarget == ECSView::FirstPerson ? 1.f : 0.f;
	FirstPersonWeight = BlendProgress;
	ApplyRotationMode(BlendTarget);
	bHasEye = false;
	UpdateRig(0.f);
}

void UCSCameraRig::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateRig(DeltaTime);
}

void UCSCameraRig::SyncFirstPersonBody()
{
	if (!FirstPersonBody || !Body)
	{
		return;
	}
	USkinnedAsset* Asset = Body->GetSkinnedAsset();
	if (FirstPersonBody->GetSkinnedAsset() != Asset)
	{
		FirstPersonBody->SetSkinnedAssetAndUpdate(Asset);
	}
	for (int32 i = 0; i < Body->GetNumMaterials(); ++i)
	{
		FirstPersonBody->SetMaterial(i, Body->GetMaterial(i));
	}
	for (const FName& Bone : OwnerHiddenBones)
	{
		if (FirstPersonBody->GetBoneIndex(Bone) != INDEX_NONE)
		{
			FirstPersonBody->HideBoneByName(Bone, PBO_None);
		}
	}
	FirstPersonBody->SetVisibility(false);
	bHeadHiddenForOwner = false;
	Body->SetOwnerNoSee(false);
}

void UCSCameraRig::ToggleView()
{
	SetView(View == ECSView::FirstPerson ? ECSView::ThirdPerson : ECSView::FirstPerson);
}

void UCSCameraRig::SetView(ECSView NewView)
{
	View = NewView;
}

ECSView UCSCameraRig::GetEffectiveView() const
{
	const ACSCharacter* Character = Cast<ACSCharacter>(GetOwner());
	const bool bRagdolled = Character && Character->IsRagdolled();
	return View == ECSView::FirstPerson && !bRagdolled ? ECSView::FirstPerson : ECSView::ThirdPerson;
}

FName UCSCameraRig::GetActiveCameraName() const
{
	static const FName FirstPersonName(TEXT("FirstPerson"));
	static const FName ThirdPersonName(TEXT("ThirdPerson"));
	return GetEffectiveView() == ECSView::FirstPerson ? FirstPersonName : ThirdPersonName;
}

float UCSCameraRig::GetNearClipPlane() const
{
	return FirstPersonWeight > 0.f ? FirstPersonNearClip : -1.f;
}

FVector UCSCameraRig::GetEyeLocation() const
{
	const FTransform Frame = Body ? Body->GetComponentTransform() : GetOwner()->GetActorTransform();
	return Frame.TransformPosition(SmoothedEyeInBody);
}

FVector UCSCameraRig::ReadEyeInBodySpace() const
{
	if (Body && Body->DoesSocketExist(EyeSocketName))
	{
		return Body->GetSocketTransform(EyeSocketName, RTS_Component).GetLocation();
	}
	if (Body && Body->GetSkinnedAsset() && !bWarnedNoSocket)
	{
		UE_LOG(LogCSCharacter, Warning, TEXT("%s: mesh %s has no socket %s; the first-person camera uses the pawn's eye height."),
			*GetOwner()->GetName(), *Body->GetSkinnedAsset()->GetName(), *EyeSocketName.ToString());
		bWarnedNoSocket = true;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const FVector World = Pawn ? Pawn->GetPawnViewLocation() : GetOwner()->GetActorLocation();
	const FTransform Frame = Body ? Body->GetComponentTransform() : GetOwner()->GetActorTransform();
	return Frame.InverseTransformPosition(World);
}

void UCSCameraRig::ApplyRotationMode(ECSView Effective)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	if (Effective == ECSView::FirstPerson)
	{
		// The body turns toward the aim, so the eyes look where the camera looks.
		Move->bOrientRotationToMovement = false;
		Move->bUseControllerDesiredRotation = true;
	}
	else
	{
		Move->bOrientRotationToMovement = bThirdPersonOrientToMovement;
		Move->bUseControllerDesiredRotation = bThirdPersonControllerDesiredRotation;
	}
}

void UCSCameraRig::SetHeadHiddenForOwner(bool bHidden)
{
	if (bHidden == bHeadHiddenForOwner)
	{
		return;
	}
	bHeadHiddenForOwner = bHidden;
	// The owner sees the copy without head and neck; everyone else keeps seeing the full body.
	if (Body)
	{
		Body->SetOwnerNoSee(bHidden);
	}
	if (FirstPersonBody)
	{
		FirstPersonBody->SetVisibility(bHidden);
	}
}

bool UCSCameraRig::IsInsideCapsule(const FVector& Point) const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		return false;
	}
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const FVector Axis = Capsule->GetUpVector() * (Capsule->GetScaledCapsuleHalfHeight() - Radius);
	const FVector Centre = Capsule->GetComponentLocation();
	return FMath::PointDistToSegment(Point, Centre - Axis, Centre + Axis) < Radius;
}

FVector UCSCameraRig::BlendLocation(const FVector& ThirdPerson, const FVector& Eye, float Weight) const
{
	const FVector Location = FMath::Lerp(ThirdPerson, Eye, Weight);
	if (Weight <= 0.f || Weight >= 1.f)
	{
		return Location;
	}
	// The straight path from the third-person camera to the eyes can cross the capsule outside the
	// hidden head (a crouch leans the head forward, so the path passes over the back). The camera
	// skips that stretch: inside it, it jumps to whichever end is nearer, the capsule's surface or
	// the edge of the hidden-head zone. Along the line the capsule (convex) is one interval.
	const float Length = FVector::Dist(ThirdPerson, Eye);
	const float HiddenFrom = Length > HeadHideRadius ? 1.f - (HeadHideRadius - HeadZoneMargin) / Length : 0.f;
	if (Weight >= HiddenFrom || !IsInsideCapsule(Location))
	{
		return Location;
	}
	auto At = [&](float W) { return FMath::Lerp(ThirdPerson, Eye, W); };
	// Entry: last point outside the capsule before Weight. Exit: first point outside it after
	// Weight, or the hidden-head zone, whichever comes first.
	float Out = 0.f, In = Weight;
	for (int32 i = 0; i < 24; ++i)
	{
		const float Mid = 0.5f * (Out + In);
		(IsInsideCapsule(At(Mid)) ? In : Out) = Mid;
	}
	const float Entry = Out;
	float Exit = HiddenFrom;
	if (!IsInsideCapsule(At(HiddenFrom)))
	{
		float Inside = Weight, Outside = HiddenFrom;
		for (int32 i = 0; i < 24; ++i)
		{
			const float Mid = 0.5f * (Inside + Outside);
			(IsInsideCapsule(At(Mid)) ? Inside : Outside) = Mid;
		}
		Exit = Outside;
	}
	return At(Weight - Entry <= Exit - Weight ? Entry : Exit);
}

void UCSCameraRig::UpdateRig(float DeltaSeconds)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !Camera)
	{
		return;
	}
	const ACSCharacter* CSCharacter = Cast<ACSCharacter>(OwnerActor);
	const UCSMovementTuning& T = CSCharacter ? CSCharacter->GetActiveTuning() : *GetDefault<UCSMovementTuning>();

	// Blend toward the effective view at a constant rate, eased, so a switch takes CameraBlendSeconds
	// at any frame rate and a switch back mid-blend reverses from where it is.
	const ECSView Effective = GetEffectiveView();
	if (Effective != BlendTarget)
	{
		BlendTarget = Effective;
		ApplyRotationMode(Effective);
	}
	const float Goal = Effective == ECSView::FirstPerson ? 1.f : 0.f;
	const float Step = T.CameraBlendSeconds > KINDA_SMALL_NUMBER ? DeltaSeconds / T.CameraBlendSeconds : 1.f;
	BlendProgress = Goal > BlendProgress ? FMath::Min(Goal, BlendProgress + Step) : FMath::Max(Goal, BlendProgress - Step);
	FirstPersonWeight = FMath::SmoothStep(0.f, 1.f, BlendProgress);

	// Eye: smoothed in the body mesh's own space, so the capsule's movement and turning are followed
	// at once (the mesh moves with them) while sway of the head is filtered. Exponential smoothing
	// with a time constant is frame-rate independent.
	const FTransform Frame = Body ? Body->GetComponentTransform() : OwnerActor->GetActorTransform();
	const FVector RawEye = ReadEyeInBodySpace();
	const bool bFirstUpdate = !bHasEye;
	const ACharacter* OwnerCharacter = Cast<ACharacter>(OwnerActor);
	const float TargetHeight = OwnerCharacter && OwnerCharacter->bIsCrouched ? T.FirstPersonCrouchedEyeHeightCm : T.FirstPersonEyeHeightCm;
	// Sideways and forward: the eye socket, smoothed. Height: the stance's fixed eye height above
	// the feet (the mesh origin), eased between standing and crouched; the head's run-cycle bounce
	// never reaches the camera (spec 5.3: no head bob).
	auto Ease = [DeltaSeconds](float Seconds) { return Seconds > 0.f ? 1.f - FMath::Exp(-DeltaSeconds / Seconds) : 1.f; };
	if (bFirstUpdate)
	{
		SmoothedEyeInBody = FVector(RawEye.X, RawEye.Y, TargetHeight);
		bHasEye = true;
	}
	else
	{
		const float Alpha = Ease(T.FirstPersonSmoothSeconds);
		SmoothedEyeInBody.X = FMath::Lerp(SmoothedEyeInBody.X, RawEye.X, Alpha);
		SmoothedEyeInBody.Y = FMath::Lerp(SmoothedEyeInBody.Y, RawEye.Y, Alpha);
		SmoothedEyeInBody.Z = FMath::Lerp(SmoothedEyeInBody.Z, TargetHeight, Ease(T.FirstPersonHeightSmoothSeconds));
	}
	const FVector Eye = Frame.TransformPosition(SmoothedEyeInBody);

	// Aim: the control rotation, never rolled, the same in both views, so a switch keeps the aim.
	const APawn* Pawn = Cast<APawn>(OwnerActor);
	FRotator Aim = Pawn ? Pawn->GetViewRotation() : OwnerActor->GetActorRotation();
	Aim.Roll = 0.f;

	// Third person: the arm pivots on the (smoothed) eyes, so an arm pushed in by a wall ends at the
	// eyes, where the head is hidden, and a switch runs along a line from the eyes.
	FVector ThirdPerson = Eye;
	if (Arm)
	{
		// From the applied tuning every frame, so a reload takes effect at once.
		Arm->TargetArmLength = T.ThirdPersonArmLength;
		Arm->SocketOffset = T.ThirdPersonShoulderOffset;
		Arm->CameraLagSpeed = T.ThirdPersonLagSpeed;
		Arm->CameraLagMaxDistance = T.ThirdPersonLagMaxDistance;
		Arm->SetWorldLocation(Eye);
		Arm->UpdateArm(DeltaSeconds, !bFirstUpdate);
		ThirdPerson = Arm->GetSocketLocation(USpringArmComponent::SocketName);
	}

	const FVector Location = BlendLocation(ThirdPerson, Eye, FirstPersonWeight);
	Camera->SetWorldLocationAndRotation(Location, Aim);
	Camera->SetFieldOfView(FMath::Lerp(T.ThirdPersonFOV, T.FirstPersonFOV, FirstPersonWeight));

	// Head and neck: hidden from the owner while the camera is close to the eyes.
	const bool bHideHead = FVector::Dist(Location, Eye) <= HeadHideRadius;
	if (bHideHead && FirstPersonBody && Body && FirstPersonBody->GetSkinnedAsset())
	{
		FirstPersonBody->CopyPoseFromSkeletalComponent(Body);
		FirstPersonBody->RefreshBoneTransforms();
	}
	SetHeadHiddenForOwner(bHideHead);
}
