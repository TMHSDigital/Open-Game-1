#pragma once

#include "CoreMinimal.h"

class ACSCharacter;

/**
 * What the tuning tab does to a character's movement values, without any widget (tested headless).
 *
 * Every change goes through UCSDevTools::SetOverrides, so it is validated as a whole tuning (range of
 * the row, rules between fields) before it becomes active, and a rejected change leaves everything
 * as it was. SetValue applies without saving, for a slider being dragged; Commit saves.
 */
class COMBATSIMULATOR_API FCSTuningPanelModel
{
public:
	explicit FCSTuningPanelModel(ACSCharacter& Character);

	/** False once the character is gone (respawn, map change): the tab must be rebuilt. */
	bool IsValid() const { return Character.IsValid(); }

	/** The value in use for a row path; 0 for an unknown path. */
	float GetValue(const FString& Path) const;

	/** The value differs from the packaged one. */
	bool IsChanged(const FString& Path) const;

	/**
	 * Changes one value in play: clamped to its row's range and rounded to its step, then applied if
	 * the whole tuning is still valid. Not saved. False, with the cause in OutReason, when the path
	 * is unknown, the value is not a number, or a rule between fields is broken; nothing changes then.
	 */
	bool SetValue(const FString& Path, float Value, FString& OutReason);

	/** Saves the changes in play. False, with the cause in OutReason, when they could not be saved (they stay in play). */
	bool Commit(FString& OutReason);

	/** One value back to the packaged one, saved. */
	bool ResetRow(const FString& Path, FString& OutReason);

	/** Every value back to the packaged one; the saved file is removed. */
	bool ResetAll(FString& OutReason);

	/** The changes in play as text, in the shape of the saved file (for the clipboard). */
	FString CopyText() const;

	/** The last rejection or save failure; otherwise which values are in play ("tuning: packaged + 2 changes"). */
	FString StatusText() const;

	/** StatusText is a rejection or a save failure. */
	bool IsStatusError() const { return !LastError.IsEmpty(); }

private:
	bool Apply(const struct FCSTuningOverrides& Overrides, bool bSave, FString& OutReason);

	TWeakObjectPtr<ACSCharacter> Character;
	FString LastError;
};
