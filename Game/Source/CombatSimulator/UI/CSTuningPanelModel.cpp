#include "CSTuningPanelModel.h"

#include "CSCharacter.h"
#include "CSDevTools.h"
#include "CSMovementTuning.h"
#include "CSTuningOverrides.h"
#include "CSTuningRanges.h"

FCSTuningPanelModel::FCSTuningPanelModel(ACSCharacter& InCharacter)
	: Character(&InCharacter)
{
}

float FCSTuningPanelModel::GetValue(const FString& Path) const
{
	float Value = 0.f;
	if (const ACSCharacter* C = Character.Get())
	{
		CSTuning::GetValue(C->GetActiveTuning(), Path, Value);
	}
	return Value;
}

bool FCSTuningPanelModel::IsChanged(const FString& Path) const
{
	const ACSCharacter* C = Character.Get();
	return C && C->GetTuningOverrides().Values.Contains(Path);
}

bool FCSTuningPanelModel::Apply(const FCSTuningOverrides& Overrides, bool bSave, FString& OutReason)
{
	OutReason.Reset();
	ACSCharacter* C = Character.Get();
	UCSDevTools* DevTools = C ? C->GetDevTools() : nullptr;
	if (!DevTools)
	{
		OutReason = TEXT("there is no character to tune");
		LastError = OutReason;
		return false;
	}
	const bool bApplied = DevTools->SetOverrides(Overrides, bSave, OutReason);
	// Applied with a reason: the values are in play but could not be saved.
	LastError = OutReason;
	return bApplied && OutReason.IsEmpty();
}

bool FCSTuningPanelModel::SetValue(const FString& Path, float Value, FString& OutReason)
{
	OutReason.Reset();
	const ACSCharacter* C = Character.Get();
	const FCSTuningRow* Row = CSTuning::FindRow(Path);
	if (!C || !Row)
	{
		OutReason = FString::Printf(TEXT("%s is not a tuning value"), *Path);
		LastError = OutReason;
		return false;
	}
	if (!FMath::IsFinite(Value))
	{
		OutReason = FString::Printf(TEXT("%s: that is not a number"), *Path);
		LastError = OutReason;
		return false;
	}
	const float Stepped = Row->Min + FMath::RoundToFloat((Value - Row->Min) / Row->Step) * Row->Step;
	FCSTuningOverrides Overrides = C->GetTuningOverrides();
	Overrides.Values.Add(Path, FMath::Clamp(Stepped, Row->Min, Row->Max));
	return Apply(Overrides, false, OutReason);
}

bool FCSTuningPanelModel::Commit(FString& OutReason)
{
	const ACSCharacter* C = Character.Get();
	return Apply(C ? C->GetTuningOverrides() : FCSTuningOverrides(), true, OutReason);
}

bool FCSTuningPanelModel::ResetRow(const FString& Path, FString& OutReason)
{
	const ACSCharacter* C = Character.Get();
	FCSTuningOverrides Overrides = C ? C->GetTuningOverrides() : FCSTuningOverrides();
	Overrides.Values.Remove(Path);
	return Apply(Overrides, true, OutReason);
}

bool FCSTuningPanelModel::ResetAll(FString& OutReason)
{
	return Apply(FCSTuningOverrides(), true, OutReason);
}

FString FCSTuningPanelModel::CopyText() const
{
	const ACSCharacter* C = Character.Get();
	return (C ? C->GetTuningOverrides() : FCSTuningOverrides()).ToJson();
}

FString FCSTuningPanelModel::StatusText() const
{
	if (!LastError.IsEmpty())
	{
		return LastError;
	}
	const ACSCharacter* C = Character.Get();
	const UCSDevTools* DevTools = C ? C->GetDevTools() : nullptr;
	return DevTools ? DevTools->GetTuningSourceText() : FString();
}
