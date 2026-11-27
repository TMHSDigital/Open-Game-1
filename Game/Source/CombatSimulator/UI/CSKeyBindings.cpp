#include "CSKeyBindings.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "InputAction.h"
#include "InputMappingContext.h"

bool FCSKeyBindings::bSaveToDisk = true;

namespace
{
	// The player's mapping that stands for a default mapping of the context: same mapping name, and
	// the context's key as its default.
	const FPlayerKeyMapping* FindPlayerMapping(const UEnhancedPlayerMappableKeyProfile& Profile, const FEnhancedActionKeyMapping& Mapping)
	{
		const FKeyMappingRow* Row = Profile.GetPlayerMappingRows().Find(Mapping.GetMappingName());
		if (!Row)
		{
			return nullptr;
		}
		for (const FPlayerKeyMapping& PlayerMapping : Row->Mappings)
		{
			if (PlayerMapping.GetDefaultKey() == Mapping.Key)
			{
				return &PlayerMapping;
			}
		}
		return nullptr;
	}

	// What a mapping of the context is called in a message: its display name when rebindable
	// ("Move Forward"), else its action's name ("Pause", "Debug Reload").
	FString Describe(const FEnhancedActionKeyMapping& Mapping, const FPlayerKeyMapping* PlayerMapping)
	{
		if (PlayerMapping && !PlayerMapping->GetDisplayName().IsEmpty())
		{
			return PlayerMapping->GetDisplayName().ToString();
		}
		FString Name = Mapping.Action ? Mapping.Action->GetName() : FString();
		Name.RemoveFromStart(TEXT("IA_"));
		return FName::NameToDisplayString(Name, false);
	}

	void Save(UEnhancedInputUserSettings& Settings)
	{
		if (FCSKeyBindings::bSaveToDisk)
		{
			Settings.SaveSettings();
		}
	}
}

UEnhancedInputUserSettings* FCSKeyBindings::GetSettings(const APlayerController* Controller)
{
	const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	const UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetUserSettings() : nullptr;
}

TArray<FCSKeyBinding> FCSKeyBindings::List(const UInputMappingContext& Context, const UEnhancedInputUserSettings& Settings)
{
	TArray<FCSKeyBinding> Out;
	const UEnhancedPlayerMappableKeyProfile* Profile = Settings.GetActiveKeyProfile();
	if (!Profile)
	{
		return Out;
	}
	for (const FEnhancedActionKeyMapping& Mapping : Context.GetMappings())
	{
		if (!Mapping.IsPlayerMappable())
		{
			continue;
		}
		const FPlayerKeyMapping* PlayerMapping = FindPlayerMapping(*Profile, Mapping);
		if (!PlayerMapping)
		{
			// The context is not registered with the settings yet (no character has been possessed).
			continue;
		}
		FCSKeyBinding& Binding = Out.AddDefaulted_GetRef();
		Binding.MappingName = PlayerMapping->GetMappingName();
		Binding.Slot = PlayerMapping->GetSlot();
		Binding.HardwareDeviceId = PlayerMapping->GetHardwareDeviceId().HardwareDeviceIdentifier;
		Binding.Label = Describe(Mapping, PlayerMapping);
		Binding.bGamepad = Mapping.Key.IsGamepadKey();
		Binding.DefaultKey = PlayerMapping->GetDefaultKey();
		Binding.CurrentKey = PlayerMapping->GetCurrentKey();
	}
	return Out;
}

bool FCSKeyBindings::CanUse(const UInputMappingContext& Context, const UEnhancedInputUserSettings& Settings, const FCSKeyBinding& Binding, const FKey& NewKey, FString& OutReason)
{
	const FString KeyName = NewKey.IsValid() ? NewKey.GetDisplayName().ToString() : FString(TEXT("That key"));
	if (!NewKey.IsValid() || NewKey.IsAxis1D() || NewKey.IsAxis2D() || NewKey.IsAxis3D() || NewKey.IsTouch() || NewKey.IsGesture())
	{
		OutReason = FString::Printf(TEXT("%s cannot be used: choose a key or a button."), *KeyName);
		return false;
	}
	if (NewKey.IsGamepadKey() != Binding.bGamepad)
	{
		OutReason = Binding.bGamepad
			? FString::Printf(TEXT("%s is not a gamepad button: this key is for the gamepad."), *KeyName)
			: FString::Printf(TEXT("%s is a gamepad button: this key is for the keyboard and mouse."), *KeyName);
		return false;
	}
	const UEnhancedPlayerMappableKeyProfile* Profile = Settings.GetActiveKeyProfile();
	for (const FEnhancedActionKeyMapping& Mapping : Context.GetMappings())
	{
		const FPlayerKeyMapping* PlayerMapping = (Profile && Mapping.IsPlayerMappable()) ? FindPlayerMapping(*Profile, Mapping) : nullptr;
		if (PlayerMapping && PlayerMapping->GetMappingName() == Binding.MappingName && PlayerMapping->GetSlot() == Binding.Slot)
		{
			continue;
		}
		const FKey InUse = PlayerMapping ? PlayerMapping->GetCurrentKey() : Mapping.Key;
		if (InUse == NewKey)
		{
			OutReason = FString::Printf(TEXT("%s is already on %s."), *KeyName, *Describe(Mapping, PlayerMapping));
			return false;
		}
	}
	return true;
}

bool FCSKeyBindings::Rebind(UEnhancedInputUserSettings& Settings, const UInputMappingContext& Context, const FCSKeyBinding& Binding, const FKey& NewKey, FString& OutMessage)
{
	if (!CanUse(Context, Settings, Binding, NewKey, OutMessage))
	{
		return false;
	}
	FMapPlayerKeyArgs Args;
	Args.MappingName = Binding.MappingName;
	Args.Slot = Binding.Slot;
	Args.HardwareDeviceId = Binding.HardwareDeviceId;
	Args.NewKey = NewKey;
	FGameplayTagContainer Failure;
	Settings.MapPlayerKey(Args, Failure);
	if (!Failure.IsEmpty())
	{
		OutMessage = FString::Printf(TEXT("%s could not be changed (%s)."), *Binding.Label, *Failure.ToStringSimple());
		return false;
	}
	Save(Settings);
	OutMessage = FString::Printf(TEXT("%s is now on %s."), *Binding.Label, *NewKey.GetDisplayName().ToString());
	return true;
}

void FCSKeyBindings::ResetAll(UEnhancedInputUserSettings& Settings)
{
	FGameplayTagContainer Failure;
	Settings.ResetKeyProfileIdToDefault(Settings.GetActiveKeyProfileId(), Failure);
	Save(Settings);
}
