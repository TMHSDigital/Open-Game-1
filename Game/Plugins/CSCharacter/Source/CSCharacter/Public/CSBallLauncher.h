#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSBallLauncher.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * A fixed launcher that throws balls (ACSHitBall) at whoever stands in front of it, so a player can
 * be hit without anyone else in the world.
 *
 * It fires along its forward (+X) axis, from MuzzleHeight above its origin, every IntervalSeconds
 * while an ACSCharacter stands in the stand box: a box StandHalfSize wide centred StandDistance in
 * front of it. Otherwise it is idle. The ball's mass and speed are the standing character's tuning
 * (DevBallMass, DevBallSpeed), the same values the dev throw key uses.
 */
UCLASS()
class CSCHARACTER_API ACSBallLauncher : public AActor
{
	GENERATED_BODY()

public:
	ACSBallLauncher();

	/** Seconds between balls while someone stands in the stand box. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS", meta = (ClampMin = "0.2", Units = "s"))
	float IntervalSeconds = 2.5f;

	/** How far in front of the launcher the stand box is centred, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS", meta = (ClampMin = "100", Units = "cm"))
	float StandDistance = 800.f;

	/** Half the width and depth of the stand box, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS", meta = (ClampMin = "20", Units = "cm"))
	float StandHalfSize = 120.f;

	/** Height of the muzzle above the launcher's origin, cm (chest height of a standing character). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS", meta = (Units = "cm"))
	float MuzzleHeight = 125.f;

	/** The character the next ball is for: one standing in the stand box, or null. */
	class ACSCharacter* FindTarget() const;

	/** Throws one ball now if someone is in the stand box. Returns whether it did. */
	bool FireOnce();

	/** Balls thrown since play began. */
	int32 GetFiredCount() const { return FiredCount; }

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "CS")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "CS")
	TObjectPtr<UBoxComponent> StandBox;

	float SecondsSinceFire = 0.f;
	int32 FiredCount = 0;
};
