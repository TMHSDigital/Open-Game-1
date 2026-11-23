#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CSLandingProbe.generated.h"

class ACSCharacter;

/**
 * Test helper for CSMovementTests: listens to a character's LandedDelegate (a dynamic delegate,
 * so the listener must be a UObject with a UFUNCTION) and records the landing speed it reads.
 */
UCLASS(Transient, NotBlueprintable)
class UCSLandingProbe : public UObject
{
	GENERATED_BODY()

public:
	void Listen(ACSCharacter* InCharacter);

	UFUNCTION()
	void HandleLanded(const FHitResult& Hit);

	UPROPERTY()
	TObjectPtr<ACSCharacter> Character;

	TArray<float> SeenLandingSpeeds;
};
