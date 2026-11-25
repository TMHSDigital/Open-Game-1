#include "CSRunStore.h"

#include "../CombatSimulator.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

bool UCSRunStore::bUseFile = true;

UCSRunStore* UCSRunStore::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine && WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UCSRunStore>() : nullptr;
}

FString UCSRunStore::GetFilePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Runs"), TEXT("RunSetup.json"));
}

void UCSRunStore::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const FString Path = GetFilePath();
	if (!bUseFile || !IFileManager::Get().FileExists(*Path))
	{
		return;
	}
	FString Json;
	TArray<FString> Notes;
	FCSRunSetup Loaded;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		LastLoadNote = TEXT("the saved run setup could not be read; using the defaults");
	}
	else if (!FCSRunSetup::FromJson(Json, Loaded, Notes))
	{
		LastLoadNote = FString::Printf(TEXT("the saved run setup was not used (%s); using the defaults"), *FString::Join(Notes, TEXT("; ")));
	}
	else
	{
		Draft = Loaded;
		LastLoadNote = FString::Join(Notes, TEXT("; "));
	}
	if (!LastLoadNote.IsEmpty())
	{
		UE_LOG(LogCombatSimulator, Warning, TEXT("UCSRunStore: %s: %s"), *Path, *LastLoadNote);
	}
}

void UCSRunStore::SetDraft(const FCSRunSetup& Setup)
{
	Draft = Setup;
	SaveDraft();
}

bool UCSRunStore::SetDraftValue(FName Key, float Value, bool bSave)
{
	if (!Draft.SetValue(Key, Value))
	{
		return false;
	}
	if (bSave)
	{
		SaveDraft();
	}
	return true;
}

void UCSRunStore::SetDraftMap(const FString& Map, bool bSave)
{
	Draft.Map = Map;
	if (bSave)
	{
		SaveDraft();
	}
}

void UCSRunStore::SaveDraft() const
{
	if (!bUseFile)
	{
		return;
	}
	const FString Path = GetFilePath();
	if (!FFileHelper::SaveStringToFile(Draft.ToJson(), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogCombatSimulator, Warning, TEXT("UCSRunStore: could not write %s; the setup stays in use for this session"), *Path);
	}
}

bool UCSRunStore::TakePending(FCSRunSetup& OutSetup)
{
	if (!Pending.IsSet())
	{
		return false;
	}
	OutSetup = Pending.GetValue();
	Pending.Reset();
	return true;
}
