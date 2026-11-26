#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CSGuardComponent.generated.h"

class ACSCharacter;
class UCharacterMovementComponent;
class UCSMovementTuning;
class USkeletalMeshComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FCSDodged, ACSCharacter* /* Who */);

/**
 * The defence of a CS character: a block that is held and a dodge that is pressed.
 *
 * Block. While the block is held the guard is up (IsGuardUp), unless the character is down or
 * getting up, is dodging, or has a punch out: a punch thrown from the guard opens it until the arm
 * is back (strike phase None or Recover). With the guard up both fists are steered every frame to
 * points in front of the head, through the physics body as the punch steers one (no clip), and
 * movement is held to BlockSpeedScale of at most a jog. A hit from in front (within BlockAngle of
 * the facing) on a character with its guard up keeps only 1 - BlockAbsorb of its force and counts
 * as Blocked, not landed (UCSPhysicsBodyComponent::ReceiveContact asks Covers). A blocked hit that
 * is still past the knockdown threshold knocks the character down anyway: a big enough hit breaks
 * a guard.
 *
 * Dodge. A press starts a quick step: for DodgeSeconds the character moves at DodgeSpeed along the
 * way it was moving, or straight back when it was not, then DodgeCooldownSeconds must pass before
 * the next. Only on the ground, standing (body Normal or Hit), and not during a punch; no punch
 * starts during it. It ends early at an edge (no walkable floor ahead), so it never launches the
 * character off a ledge. It is a real
 * step with no immunity: a punch misses because the body is no longer where it was going.
 *
 * Tuning (UCSMovementTuning, group Defence): BlockAbsorb, BlockAngle, BlockSpeedScale, DodgeSpeed,
 * DodgeSeconds, DodgeCooldownSeconds.
 */
UCLASS(ClassGroup = CS, meta = (BlueprintSpawnableComponent))
class CSCHARACTER_API UCSGuardComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSGuardComponent();

	/** The block key: held is true, let go is false. */
	UFUNCTION(BlueprintCallable, Category = "CS|Guard")
	void SetBlockHeld(bool bHeld);

	UFUNCTION(BlueprintPure, Category = "CS|Guard")
	bool IsBlockHeld() const { return bBlockHeld; }

	/** The block is held and nothing keeps the guard down (see the class comment). */
	UFUNCTION(BlueprintPure, Category = "CS|Guard")
	bool IsGuardUp() const;

	/** The guard is up and something at From is in front of it (within BlockAngle of the facing, seen from above). */
	bool Covers(const FVector& From) const;

	/** From (relative to the character, seen from above) is within AngleDegrees of Facing. */
	static bool IsInFront(const FVector& Facing, const FVector& ToFrom, float AngleDegrees);

	/** Starts a dodge along Direction (flattened; zero: straight back). False when one cannot start now. */
	UFUNCTION(BlueprintCallable, Category = "CS|Guard")
	bool Dodge(const FVector& Direction);

	UFUNCTION(BlueprintPure, Category = "CS|Guard")
	bool IsDodging() const;

	/** The way the dodge under way, or the last one, went (flat, unit). */
	const FVector& GetDodgeDirection() const { return DodgeDirection; }

	/** Dodges since play began. */
	int32 GetDodgeCount() const { return DodgeCount; }

	/** Hits taken on the guard since play began (counted by the physics body). */
	int32 GetBlockedCount() const { return BlockedCount; }
	void NoteBlocked() { ++BlockedCount; }

	/**
	 * Called by the character every tick, before its movement runs: during a dodge sets the
	 * movement velocity to the dodge, and returns the speed cap the movement should use from Cap
	 * (the dodge speed while dodging, Cap held down while the guard is up, Cap otherwise).
	 */
	float ApplyToMovement(UCharacterMovementComponent& Move, float Cap, float JogSpeed);

	/** Every dodge started, by anyone. */
	static FCSDodged OnDodged;

	/** How far in front of the head the fists are held, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Guard", meta = (Units = "cm"))
	float GuardForward = 24.f;

	/** How far each fist is from the middle, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Guard", meta = (Units = "cm"))
	float GuardSide = 11.f;

	/** How far below the head bone the fists are held, cm (about chin height). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Guard", meta = (Units = "cm"))
	float GuardDrop = 8.f;

	/** The fastest a fist is moved to its guard point, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Guard", meta = (Units = "cm/s"))
	float GuardSpeed = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Guard")
	FName HeadBone = TEXT("head");

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	ACSCharacter* GetCharacter() const;
	const UCSMovementTuning& GetTuning() const;
	/** Standing on its own feet: physics body Normal or Hit, not ragdolled. */
	bool IsStanding() const;
	void DriveFists(float DeltaTime);
	/** Walkable floor a stride ahead along the dodge. */
	bool HasFloorAhead(const UCharacterMovementComponent& Move) const;

	bool bBlockHeld = false;
	double DodgeStart = -1000.0;
	double DodgeEnd = -1000.0;
	FVector DodgeDirection = FVector::BackwardVector;
	int32 DodgeCount = 0;
	int32 BlockedCount = 0;
};
