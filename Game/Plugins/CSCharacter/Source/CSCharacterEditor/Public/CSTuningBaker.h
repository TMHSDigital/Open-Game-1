#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CSTuningBaker.generated.h"

class UCSMovementTuning;

USTRUCT(BlueprintType)
struct FCSTuningBakeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	bool bSuccess = false;

	/** One line per value written ("JogSpeed: 380 -> 450"), or why nothing was written. */
	UPROPERTY(BlueprintReadOnly, Category = "Tuning")
	FString Message;
};

/**
 * Writes tuning values found in a running build back into a tuning asset.
 *
 * The owner changes values on the tuning tab of the pause screen and copies them out ("Copy
 * values"); Game/Content/Python/apply_tuning_values.py hands that text to ApplyValues, so the next
 * build is packaged with them. Editor only; callable from Python as unreal.CSTuningBaker.apply_values.
 */
UCLASS()
class CSCHARACTEREDITOR_API UCSTuningBaker : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Applies Json (the text of "Copy values", the same shape as the overrides file) to Tuning.
	 * The values go through the same checks as in the game: every path must be a row of the range
	 * table with a value in range, and the result must pass UCSMovementTuning::IsValid. If anything
	 * fails, Tuning is left untouched and Message says why. The caller saves the asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Tuning")
	static FCSTuningBakeResult ApplyValues(UCSMovementTuning* Tuning, const FString& Json);
};
