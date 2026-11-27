#include "CSPauseMenuModel.h"

#include "InputAction.h"
#include "InputMappingContext.h"

bool FCSPauseKeyGuard::IsCloseKey(const FKey& Key)
{
	return Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Right;
}

void FCSPauseKeyGuard::Arm(const FKey& Key, double Now)
{
	Armed = Key;
	ArmedAt = Now;
}

bool FCSPauseKeyGuard::ShouldSwallow(const FKey& Key, bool bIsRelease, double Now)
{
	if (!Armed.IsValid() || Key != Armed)
	{
		return false;
	}
	if (bIsRelease || Now - ArmedAt > MaxSeconds)
	{
		Armed = FKey();
		return false;
	}
	return true;
}

TArray<ECSPauseTab> FCSPauseMenuModel::Tabs(bool bDevTools)
{
	TArray<ECSPauseTab> Out;
	Out.Add(ECSPauseTab::Run);
	Out.Add(ECSPauseTab::Settings);
	if (bDevTools)
	{
		Out.Add(ECSPauseTab::Tuning);
	}
	Out.Add(ECSPauseTab::Keys);
	return Out;
}

FString FCSPauseMenuModel::TabLabel(ECSPauseTab Tab)
{
	switch (Tab)
	{
	case ECSPauseTab::Run: return TEXT("Run");
	case ECSPauseTab::Settings: return TEXT("Settings");
	case ECSPauseTab::Tuning: return TEXT("Tuning");
	case ECSPauseTab::Keys: return TEXT("Keys");
	}
	return FString();
}

TArray<FCSKeyRow> FCSPauseMenuModel::KeyRows(const UInputMappingContext& Context, bool bDevTools, bool bFixedOnly)
{
	TArray<FCSKeyRow> Rows;
	TArray<const UInputAction*> Actions;
	for (const FEnhancedActionKeyMapping& Mapping : Context.GetMappings())
	{
		const UInputAction* Action = Mapping.Action;
		if (!Action || (bFixedOnly && Mapping.IsPlayerMappable()))
		{
			continue;
		}
		FString Name = Action->GetName();
		const bool bDev = Name.StartsWith(TEXT("IA_Debug"));
		if (bDev && !bDevTools)
		{
			continue;
		}
		int32 Index = Actions.IndexOfByKey(Action);
		if (Index == INDEX_NONE)
		{
			Index = Actions.Add(Action);
			Name.RemoveFromStart(TEXT("IA_"));
			FCSKeyRow Row;
			Row.Action = FName::NameToDisplayString(Name, false);
			Row.bDev = bDev;
			Rows.Add(MoveTemp(Row));
		}
		FString& Keys = Rows[Index].Keys;
		if (!Keys.IsEmpty())
		{
			Keys += TEXT(" / ");
		}
		Keys += Mapping.Key.GetDisplayName().ToString();
	}
	return Rows;
}
