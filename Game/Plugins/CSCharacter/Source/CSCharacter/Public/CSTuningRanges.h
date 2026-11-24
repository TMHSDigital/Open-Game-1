#pragma once

#include "CoreMinimal.h"

class UCSMovementTuning;

/**
 * One numeric field of UCSMovementTuning: where it lives, how it is shown and the range it may take.
 *
 * The rows are the single source of the per-field bounds: UCSMovementTuning::IsValid checks every row,
 * the tuning tab of the pause screen builds one slider per row, and the overrides file may only name
 * row paths. Shipping builds have no property metadata, so the UIMin and UIMax shown in the editor
 * cannot be read at run time; a test keeps them equal to the rows.
 */
struct CSCHARACTER_API FCSTuningRow
{
	/** Property path on UCSMovementTuning: "WalkSpeed", "ThirdPersonShoulderOffset.X", "HitProfile.PositionStrength". */
	FString Path;
	/** Section the row is listed under, as in the asset ("Speeds", "Camera", ...). */
	FName Group;
	/** Text shown on the row. */
	FString Label;
	/** "cm/s", "s", "deg", ... or empty. */
	FString Unit;
	float Min = 0.f;
	float Max = 1.f;
	/** Slider and rounding step. */
	float Step = 0.1f;
};

namespace CSTuning
{
	/** Every numeric tuning field, in the asset's order. bDevTools has no row and cannot be changed through one. */
	CSCHARACTER_API const TArray<FCSTuningRow>& Rows();

	/** The row for a path, or null. */
	CSCHARACTER_API const FCSTuningRow* FindRow(const FString& Path);

	/** Reads the field at a row path. False when the path names no numeric field. */
	CSCHARACTER_API bool GetValue(const UCSMovementTuning& Tuning, const FString& Path, float& OutValue);

	/** Writes the field at a row path (no range check). False when the path names no numeric field. */
	CSCHARACTER_API bool SetValue(UCSMovementTuning& Tuning, const FString& Path, float Value);
}
