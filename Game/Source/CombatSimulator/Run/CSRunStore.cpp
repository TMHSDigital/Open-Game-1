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
	if (bUseFile)
	{
		// A broken history costs only the history: the game starts either way.
		const FString HistoryNote = History.Load(FCSRunHistory::GetFilePath());
		if (!HistoryNote.IsEmpty())
		{
			UE_LOG(LogCombatSimulator, Warning, TEXT("UCSRunStore: %s: %s"), *FCSRunHistory::GetFilePath(), *HistoryNote);
		}
	}
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

void UCSRunStore::RecordRun(const FCSRunRecord& Record)
{
	if (!History.Add(Record, bUseFile ? FCSRunHistory::GetFilePath() : FString()))
	{
		UE_LOG(LogCombatSimulator, Warning, TEXT("UCSRunStore: the run history could not be written to %s"), *FCSRunHistory::GetFilePath());
	}
}

TArray<FString> UCSRunStore::DescribeHistory(const FString& Setup, int32 Recent) const
{
	TArray<FString> Lines;
	if (const FCSRunRecord* Best = History.Best(Setup))
	{
		Lines.Add(TEXT("Best with this setup: ") + Best->Describe());
	}
	const TArray<FCSRunRecord> Latest = History.Recent(Recent);
	if (!Latest.IsEmpty())
	{
		Lines.Add(TEXT("Recent runs:"));
		for (const FCSRunRecord& Run : Latest)
		{
			Lines.Add(TEXT("  ") + Run.Describe() + (Run.Setup == Setup ? FString() : TEXT("  [") + Run.Setup + TEXT("]")));
		}
	}
	return Lines;
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

bool UCSRunStore::TakePending(const FString& CurrentMap, FCSRunSetup& OutSetup)
{
	if (!Pending.IsSet())
	{
		return false;
	}
	const FCSRunSetup Waiting = Pending.GetValue();
	Pending.Reset();
	if (Waiting.Map != CurrentMap)
	{
		UE_LOG(LogCombatSimulator, Warning, TEXT("UCSRunStore: a run was waiting for %s but %s opened; it is dropped"), *Waiting.Map, *CurrentMap);
		return false;
	}
	OutSetup = Waiting;
	return true;
}
