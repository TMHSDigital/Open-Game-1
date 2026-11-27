#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UserSettings/EnhancedInputUserSettings.h"

class APlayerController;
class UInputMappingContext;

/** One key the player can change on the pause screen's Keys tab. */
struct FCSKeyBinding
{
	/** The Enhanced Input mapping name ("Kick", "MoveForward"): what the saved settings store. */
	FName MappingName;
	EPlayerMappableKeySlot Slot = EPlayerMappableKeySlot::First;
	FName HardwareDeviceId;
	/** "Kick", "Move Forward". */
	FString Label;
	/** A gamepad row (takes only gamepad buttons); otherwise keyboard and mouse. */
	bool bGamepad = false;
	FKey DefaultKey;
	FKey CurrentKey;

	bool IsChanged() const { return CurrentKey != DefaultKey; }
};

/**
 * Player key rebinding, through the Enhanced Input user settings (enabled in DefaultInput.ini; the
 * rebindable mappings are marked by Game/Content/Python/create_input_assets.py). Works on the
 * settings object of a local player, without any widget, so it can be tested.
 *
 * A key is refused, with a reason, when it is not a button of the row's device, or when any other
 * mapping of the context already uses it (rebindable or not: the pause keys, look and the dev keys
 * count too), so a key never does two things and the pause screen can always be opened.
 */
struct COMBATSIMULATOR_API FCSKeyBindings
{
	/** The local player's Enhanced Input user settings; null without them (or without a local player). */
	static UEnhancedInputUserSettings* GetSettings(const APlayerController* Controller);

	/** Every rebindable key of the context, in the context's order, with the player's current key. */
	static TArray<FCSKeyBinding> List(const UInputMappingContext& Context, const UEnhancedInputUserSettings& Settings);

	/** Whether NewKey may go on Binding. False: OutReason says why ("Space Bar is already on Jump."). */
	static bool CanUse(const UInputMappingContext& Context, const UEnhancedInputUserSettings& Settings, const FCSKeyBinding& Binding, const FKey& NewKey, FString& OutReason);

	/**
	 * Puts NewKey on Binding and saves the settings. True with "Kick is now on G."; false with the
	 * reason, and nothing changed.
	 */
	static bool Rebind(UEnhancedInputUserSettings& Settings, const UInputMappingContext& Context, const FCSKeyBinding& Binding, const FKey& NewKey, FString& OutMessage);

	/** Every rebindable key back to its default, saved. */
	static void ResetAll(UEnhancedInputUserSettings& Settings);

	/** False in tests: changes are applied but not written to the player's save file. */
	static bool bSaveToDisk;
};
