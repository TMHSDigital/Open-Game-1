#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CSCharacter.generated.h"

class UCSMovementTuning;

/** Movement gait. Jog is the default; walk and sprint are held, crouch is toggled by the caller. */
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
 */
UCLASS(Blueprintable)
class CSCHARACTER_API ACSCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ACSCharacter(const FObjectInitializer& ObjectInitializer);

	/** Feel values. When empty, the UCSMovementTuning class defaults are used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Movement")
	TObjectPtr<UCSMovementTuning> Tuning;

	/** Pushes the tuning values into the movement component. Called on BeginPlay; call again after changing Tuning. */
	UFUNCTION(BlueprintCallable, Category = "CS|Movement")
	void ApplyTuning();

	/** Changes gait. Crouch crouches the character; any other gait stands it up. */
	UFUNCTION(BlueprintCallable, Category = "CS|Movement")
	void SetGait(ECSGait NewGait);

	UFUNCTION(BlueprintPure, Category = "CS|Movement")
	ECSGait GetGait() const { return Gait; }

	/** The tuning in use: Tuning, or the class defaults when it is empty. */
	const UCSMovementTuning& GetActiveTuning() const;

	/** Top speed of a gait on flat ground, cm/s. */
	float GetGaitSpeed(ECSGait InGait) const;

	/** Current slope speed scale (1 on flat ground). */
	float GetSlopeSpeedFactor() const { return SlopeSpeedFactor; }

	/** Vertical speed of the last landing, cm/s (positive down). */
	float GetLastLandingSpeed() const { return LastLandingSpeed; }

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Landed(const FHitResult& Hit) override;

private:
	float ComputeSlopeSpeedFactor(const UCSMovementTuning& Values) const;
	float GetGravityMagnitude() const;

	ECSGait Gait = ECSGait::Jog;
	float SlopeSpeedFactor = 1.f;
	float LastLandingSpeed = 0.f;
};
