#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSHitBall.generated.h"

class ACSHitBall;
class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE_TwoParams(FCSBallThrown, ACSHitBall* /* Ball */, AActor* /* Thrower, may be null */);

/**
 * A ball that is thrown at characters to try out hit reactions: the dev throw key
 * (UCSDevTools::RequestThrow) and ACSBallLauncher make them.
 *
 * A simulated sphere that passes through a character's movement capsule (it ignores pawns) and
 * strikes the bodies of the physics body inside it, so the hit lands on the body part it touches.
 * The character works out the hit from the ball's mass and speed
 * (UCSPhysicsBodyComponent::ReceiveContact). A ball never hits the actor that threw it: its Owner.
 *
 * At most MaxBalls exist in a world; throwing another removes the oldest. A ball also removes itself
 * after LifeSeconds.
 */
UCLASS()
class CSCHARACTER_API ACSHitBall : public AActor
{
	GENERATED_BODY()

public:
	ACSHitBall();

	static constexpr int32 MaxBalls = 20;
	static constexpr float LifeSeconds = 30.f;
	static constexpr float RadiusCm = 11.f;

	/**
	 * Throws a ball of Mass kg from From along Direction at Speed cm/s. Thrower becomes its Owner and
	 * is never hit by it. Null when the world or the direction is missing.
	 */
	static ACSHitBall* Throw(UWorld* World, AActor* Thrower, const FVector& From, const FVector& Direction, float Mass, float Speed);

	/** Every ball thrown, in any world; listeners check the ball's world. */
	static FCSBallThrown OnThrown;

	/** Removes every ball in the world. Returns how many. */
	static int32 RemoveAll(UWorld* World);

	UStaticMeshComponent* GetBall() const { return Ball; }

private:
	UPROPERTY(VisibleAnywhere, Category = "CS")
	TObjectPtr<UStaticMeshComponent> Ball;
};
