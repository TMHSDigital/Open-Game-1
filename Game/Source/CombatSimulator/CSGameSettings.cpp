#include "CSGameSettings.h"

#include "CombatSimulator.h"

namespace
{
	const FName MouseSensitivityKey(TEXT("MouseSensitivity"));
	const FName StickSensitivityKey(TEXT("StickSensitivity"));
	const FName InvertYKey(TEXT("InvertY"));

	FCSSettingRow NumberRow(FName Key, const TCHAR* Label, float Min, float Max, float Step, float Default)
	{
		FCSSettingRow Row;
		Row.Key = Key;
		Row.Label = Label;
		Row.Group = TEXT("Look");
		Row.Min = Min;
		Row.Max = Max;
		Row.Step = Step;
		Row.Default = Default;
		return Row;
	}

	FCSSettingRow SwitchRow(FName Key, const TCHAR* Label, bool bDefault)
	{
		FCSSettingRow Row;
		Row.Key = Key;
		Row.Label = Label;
		Row.Group = TEXT("Look");
		Row.bIsBool = true;
		Row.Min = 0.f;
		Row.Max = 1.f;
		Row.Step = 1.f;
		Row.Default = bDefault ? 1.f : 0.f;
		return Row;
	}

	/** What Update stores: the value clamped to the row and rounded to its step; the default when it is not a finite number. */
	float Clean(const FCSSettingRow& Row, float Value)
	{
		if (!FMath::IsFinite(Value))
		{
			return Row.Default;
		}
		if (Row.bIsBool)
		{
			return Value >= 0.5f ? 1.f : 0.f;
		}
		const float Stepped = Row.Min + FMath::RoundToFloat((Value - Row.Min) / Row.Step) * Row.Step;
		return FMath::Clamp(Stepped, Row.Min, Row.Max);
	}

	/**
	 * What a value read from the ini becomes: itself when it is a finite number inside the row's range,
	 * otherwise the default. Not clamped: text in the ini reads as 0, and clamping that would give the
	 * slowest look speed instead of the normal one.
	 */
	float Loaded(const FCSSettingRow& Row, float Value)
	{
		return FMath::IsFinite(Value) && Value >= Row.Min && Value <= Row.Max ? Value : Row.Default;
	}
}

UCSGameSettings* UCSGameSettings::Get()
{
	UCSGameSettings* Settings = GetMutableDefault<UCSGameSettings>();
	static bool bSanitized = false;
	if (!bSanitized)
	{
		// The class default object has already read the ini; a hand-edited value may be anything.
		bSanitized = true;
		Settings->Sanitize();
	}
	return Settings;
}

const TArray<FCSSettingRow>& UCSGameSettings::Rows()
{
	static const TArray<FCSSettingRow> Table = {
		NumberRow(MouseSensitivityKey, TEXT("Mouse sensitivity"), 0.1f, 5.f, 0.05f, 1.f),
		NumberRow(StickSensitivityKey, TEXT("Stick sensitivity"), 0.1f, 5.f, 0.05f, 1.f),
		SwitchRow(InvertYKey, TEXT("Invert Y"), false),
	};
	return Table;
}

const FCSSettingRow* UCSGameSettings::FindRow(FName Key)
{
	return Rows().FindByPredicate([Key](const FCSSettingRow& Row) { return Row.Key == Key; });
}

float UCSGameSettings::GetValue(FName Key) const
{
	if (Key == MouseSensitivityKey)
	{
		return MouseSensitivity;
	}
	if (Key == StickSensitivityKey)
	{
		return StickSensitivity;
	}
	if (Key == InvertYKey)
	{
		return bInvertY ? 1.f : 0.f;
	}
	return 0.f;
}

bool UCSGameSettings::SetField(FName Key, float Value)
{
	if (Key == MouseSensitivityKey)
	{
		MouseSensitivity = Value;
	}
	else if (Key == StickSensitivityKey)
	{
		StickSensitivity = Value;
	}
	else if (Key == InvertYKey)
	{
		bInvertY = Value >= 0.5f;
	}
	else
	{
		return false;
	}
	return true;
}

bool UCSGameSettings::Update(FName Key, float Value, bool bSave)
{
	const FCSSettingRow* Row = FindRow(Key);
	if (!Row)
	{
		return false;
	}
	const float Cleaned = Clean(*Row, Value);
	if (FMath::IsNearlyEqual(Cleaned, GetValue(Key), 1.e-4f))
	{
		return true;
	}
	SetField(Key, Cleaned);
	if (bSave)
	{
		Save();
	}
	else
	{
		bUnsaved = true;
	}
	OnChanged.Broadcast();
	return true;
}

void UCSGameSettings::SaveIfChanged()
{
	if (bUnsaved)
	{
		Save();
	}
}

void UCSGameSettings::Save()
{
	bUnsaved = false;
	++SaveCount;
	if (bPersist)
	{
		SaveConfig();
	}
}

void UCSGameSettings::ResetAll()
{
	bool bChanged = false;
	for (const FCSSettingRow& Row : Rows())
	{
		if (!FMath::IsNearlyEqual(GetValue(Row.Key), Row.Default, 1.e-4f))
		{
			SetField(Row.Key, Row.Default);
			bChanged = true;
		}
	}
	if (!bChanged)
	{
		return;
	}
	Save();
	OnChanged.Broadcast();
}

void UCSGameSettings::Sanitize()
{
	for (const FCSSettingRow& Row : Rows())
	{
		const float Value = GetValue(Row.Key);
		const float Cleaned = Loaded(Row, Value);
		// NaN never compares equal, so a value that is not a number is always replaced.
		if (!(Cleaned == Value))
		{
			UE_LOG(LogCombatSimulator, Warning, TEXT("Settings: %s was %g, outside its range; using the default %g."), *Row.Key.ToString(), Value, Cleaned);
			SetField(Row.Key, Cleaned);
		}
	}
	SettingsVersion = 1;
}
