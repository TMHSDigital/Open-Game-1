#pragma once

#include "CoreMinimal.h"
#include "CSCharacter.h"
#include "CSTargetDummy.generated.h"

/**
 * The target dummy: a second copy of the character with no controller, placed in a map on a marked
 * spot. Everything that hits a character hits it the same way (stagger, ragdoll, getting up), which
 * is what makes it the test bed for combat moves.
 *
 * It differs from a player's character only in this: nothing possesses it, it uses the game's tuning
 * asset and follows the tuning changes of the player (ACSGameMode keeps them in step), it is tinted
 * so the two are told apart, and it remembers where it was placed so a reset can put it back.
 */
UCLASS()
class COMBATSIMULATOR_API ACSTargetDummy : public ACSCharacter
{
	GENERATED_BODY()

public:
	ACSTargetDummy(const FObjectInitializer& ObjectInitializer);

	/** Where the dummy stood when play began (or where it was respawned). */
	const FTransform& GetHomeTransform() const { return HomeTransform; }

	/** Colour multiplied into the body's material, when the material has a colour parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS")
	FLinearColor Tint = FLinearColor(0.85f, 0.45f, 0.2f);

	virtual void BeginPlay() override;

private:
	FTransform HomeTransform = FTransform::Identity;
};
