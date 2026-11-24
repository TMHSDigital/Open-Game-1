#include "CSPauseMenuModel.h"

#include "InputAction.h"
#include "InputMappingContext.h"

bool FCSPauseKeyLatch::IsCloseKey(const FKey& Key)
{
	return Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Right;
}

void FCSPauseKeyLatch::KeyDown(const FKey& Key, bool bIsRepeat)
{
	if (!bIsRepeat && IsCloseKey(Key))
	{
		Armed = Key;
	}
}

bool FCSPauseKeyLatch::KeyUp(const FKey& Key)
{
	if (Armed.IsValid() && Key == Armed)
	{
		Armed = FKey();
		return true;
	}
	return false;
}

TArray<ECSPauseTab> FCSPauseMenuModel::Tabs(bool bDevTools)
{
	TArray<ECSPauseTab> Out;
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
	case ECSPauseTab::Settings: return TEXT("Settings");
	case ECSPauseTab::Tuning: return TEXT("Tuning");
	case ECSPauseTab::Keys: return TEXT("Keys");
	}
	return FString();
}

TArray<FCSKeyRow> FCSPauseMenuModel::KeyRows(const UInputMappingContext& Context, bool bDevTools)
{
	TArray<FCSKeyRow> Rows;
	TArray<const UInputAction*> Actions;
	for (const FEnhancedActionKeyMapping& Mapping : Context.GetMappings())
	{
		const UInputAction* Action = Mapping.Action;
		if (!Action)
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
