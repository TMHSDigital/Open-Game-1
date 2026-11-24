#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CSGameSettings.generated.h"

/** One setting as the settings tab shows it: a number with a range, or a switch. */
struct FCSSettingRow
{
	/** Stable id: what Update and GetValue take. */
	FName Key;
	FString Label;
	FName Group;
	bool bIsBool = false;
	float Min = 0.f;
	float Max = 1.f;
	float Step = 0.1f;
	float Default = 0.f;
};

/**
 * The player's settings, kept per machine in the user's GameUserSettings.ini.
 *
 * One record with typed, clamped fields. Update is the only way a value changes: it clamps, stores,
 * saves and tells the listeners, so a value cannot be saved without being applied. The settings tab
 * is built from Rows, so a new setting is one field, one row and one consumer.
 *
 * A missing ini means the defaults; a value that is out of range or not a number (a hand-edited ini)
 * is clamped or put back to its default by Sanitize when the settings are first read.
 *
 * Applied by ACSPlayerController::ApplySettings (look scale on the character).
 */
UCLASS(Config = GameUserSettings)
class COMBATSIMULATOR_API UCSGameSettings : public UObject
{
	GENERATED_BODY()

public:
	/** The settings in use (the class default object, read from the ini and sanitized on first use). */
	static UCSGameSettings* Get();

	/** Every setting, in the order the settings tab lists them. */
	static const TArray<FCSSettingRow>& Rows();

	/** The row for a key, or null. */
	static const FCSSettingRow* FindRow(FName Key);

	/** Current value; a switch reads as 0 or 1. 0 for an unknown key. */
	float GetValue(FName Key) const;

	/**
	 * Sets a value: clamped to its row and rounded to its step, saved, and announced through
	 * OnChanged when it differs from the old value. False for an unknown key.
	 */
	bool Update(FName Key, float Value);

	/** Every setting back to its default; saved and announced once if anything changed. */
	void ResetAll();

	/** Clamps every field to its row; a value that is not a finite number becomes the default. */
	void Sanitize();

	/** A value changed. */
	FSimpleMulticastDelegate OnChanged;

	/** False: Update and ResetAll do not write the ini (tests). */
	bool bPersist = true;

	/** Version of the stored fields, for migrations when a setting is renamed or changes meaning. */
	UPROPERTY(Config)
	int32 SettingsVersion = 1;

	/** Multiplies mouse look. 1 is the tested base speed (create_input_assets.py MOUSE_DEG_PER_COUNT). */
	UPROPERTY(Config)
	float MouseSensitivity = 1.f;

	/** Multiplies right stick look. 1 is the tested base speed (STICK_DEG_PER_SECOND). */
	UPROPERTY(Config)
	float StickSensitivity = 1.f;

	/** Pushing the mouse or stick up looks down. */
	UPROPERTY(Config)
	bool bInvertY = false;

private:
	bool SetField(FName Key, float Value);
};
