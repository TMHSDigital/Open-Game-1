#include "CSCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CSMovementTuning.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	// Capsule for a 180 cm figure: half height 90 puts the mesh origin (the feet) at -90.
	constexpr float CapsuleRadius = 35.f;
	constexpr float CapsuleHalfHeight = 90.f;
	constexpr float CrouchedHalfHeight = 60.f;
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
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -CapsuleHalfHeight), FRotator(0.f, -90.f, 0.f));

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
}

const UCSMovementTuning& ACSCharacter::GetActiveTuning() const
{
	return Tuning ? *Tuning.Get() : *GetDefault<UCSMovementTuning>();
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

void ACSCharacter::ApplyTuning()
{
	const UCSMovementTuning& T = GetActiveTuning();
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

void ACSCharacter::BeginPlay()
{
	Super::BeginPlay();
	// Speed caps are set in Tick and must be in place before the movement component uses them in
	// the same frame; otherwise the ramp lags a frame (33 ms at 30 fps), which makes the time to
	// sprint speed depend on the frame rate.
	GetCharacterMovement()->AddTickPrerequisiteActor(this);
	ApplyTuning();
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
