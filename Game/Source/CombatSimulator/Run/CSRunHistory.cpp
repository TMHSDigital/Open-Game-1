#include "CSRunHistory.h"

#include "CSRunSetup.h"
#include "CSRunStats.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FCSRunRecord FCSRunRecord::Make(const FCSRunSetup& Setup, const FCSRunStats& Stats, ECSRunEnd End, const FDateTime& When)
{
	FCSRunRecord Record;
	Record.When = When.ToString(TEXT("%Y-%m-%d %H:%M"));
	if (GConfig)
	{
		GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Record.Build, GGameIni);
	}
	Record.Setup = Setup.Describe();
	Record.End = FCSRunStats::DescribeEnd(End);
	Record.Seconds = Stats.Seconds;
	Record.Dealt = Stats.Dealt.Hits;
	Record.Knockdowns = Stats.Dealt.Knockdowns;
	Record.Taken = Stats.Taken.Hits;
	Record.StrikesThrown = Stats.PunchesThrown;
	Record.StrikesLanded = Stats.PunchesLanded;
	return Record;
}

FString FCSRunRecord::ToJson() const
{
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("when"), When);
	Writer->WriteValue(TEXT("build"), Build);
	Writer->WriteValue(TEXT("setup"), Setup);
	Writer->WriteValue(TEXT("end"), End);
	Writer->WriteValue(TEXT("seconds"), Seconds);
	Writer->WriteValue(TEXT("dealt"), Dealt);
	Writer->WriteValue(TEXT("knockdowns"), Knockdowns);
	Writer->WriteValue(TEXT("taken"), Taken);
	Writer->WriteValue(TEXT("strikesThrown"), StrikesThrown);
	Writer->WriteValue(TEXT("strikesLanded"), StrikesLanded);
	Writer->WriteObjectEnd();
	Writer->Close();
	return Out;
}

bool FCSRunRecord::FromJson(const FString& Line, FCSRunRecord& Out)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Root) || !Root.IsValid())
	{
		return false;
	}
	FCSRunRecord Record;
	double Seconds = 0.0;
	const bool bOk = Root->TryGetStringField(TEXT("when"), Record.When)
		&& Root->TryGetStringField(TEXT("setup"), Record.Setup)
		&& Root->TryGetStringField(TEXT("end"), Record.End)
		&& Root->TryGetNumberField(TEXT("seconds"), Seconds)
		&& Root->TryGetNumberField(TEXT("dealt"), Record.Dealt)
		&& Root->TryGetNumberField(TEXT("knockdowns"), Record.Knockdowns)
		&& Root->TryGetNumberField(TEXT("taken"), Record.Taken)
		&& Root->TryGetNumberField(TEXT("strikesThrown"), Record.StrikesThrown)
		&& Root->TryGetNumberField(TEXT("strikesLanded"), Record.StrikesLanded);
	if (!bOk || !FMath::IsFinite(Seconds) || Seconds < 0.0)
	{
		return false;
	}
	Root->TryGetStringField(TEXT("build"), Record.Build);  // optional
	Record.Seconds = float(Seconds);
	Out = MoveTemp(Record);
	return true;
}

FString FCSRunRecord::Describe() const
{
	return FString::Printf(TEXT("%s  %s  %d hits, %d knockdowns, %d taken, strikes %d/%d (%s)"), *When, *FCSRunStats::FormatTime(Seconds),
		Dealt, Knockdowns, Taken, StrikesLanded, StrikesThrown, *End);
}

bool FCSRunRecord::IsBetterThan(const FCSRunRecord& Other) const
{
	if (Knockdowns != Other.Knockdowns)
	{
		return Knockdowns > Other.Knockdowns;
	}
	if (Dealt != Other.Dealt)
	{
		return Dealt > Other.Dealt;
	}
	if (Taken != Other.Taken)
	{
		return Taken < Other.Taken;
	}
	return Seconds < Other.Seconds;
}

FString FCSRunHistory::GetFilePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Runs"), TEXT("History.jsonl"));
}

int32 FCSRunHistory::Parse(const FString& Text)
{
	Runs.Reset();
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	int32 Skipped = 0;
	for (const FString& Line : Lines)
	{
		if (Line.TrimStartAndEnd().IsEmpty())
		{
			continue;
		}
		FCSRunRecord Record;
		if (FCSRunRecord::FromJson(Line, Record))
		{
			Runs.Add(MoveTemp(Record));
		}
		else
		{
			++Skipped;
		}
	}
	if (Runs.Num() > MaxRuns)
	{
		Runs.RemoveAt(0, Runs.Num() - MaxRuns);
	}
	return Skipped;
}

FString FCSRunHistory::Load(const FString& Path)
{
	Runs.Reset();
	if (!IFileManager::Get().FileExists(*Path))
	{
		return FString();
	}
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		return TEXT("the run history could not be read; starting a new one");
	}
	const int32 Skipped = Parse(Text);
	return Skipped > 0 ? FString::Printf(TEXT("%d line(s) of the run history could not be read and were skipped"), Skipped) : FString();
}

bool FCSRunHistory::Add(const FCSRunRecord& Record, const FString& Path)
{
	Runs.Add(Record);
	if (Runs.Num() > MaxRuns)
	{
		Runs.RemoveAt(0, Runs.Num() - MaxRuns);
	}
	if (Path.IsEmpty())
	{
		return true;
	}
	FString Text;
	for (const FCSRunRecord& Run : Runs)
	{
		Text += Run.ToJson();
		Text += TEXT("\n");
	}
	return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

TArray<FCSRunRecord> FCSRunHistory::Recent(int32 Count) const
{
	TArray<FCSRunRecord> Out;
	for (int32 i = Runs.Num() - 1; i >= 0 && Out.Num() < Count; --i)
	{
		Out.Add(Runs[i]);
	}
	return Out;
}

const FCSRunRecord* FCSRunHistory::Best(const FString& Setup) const
{
	const FCSRunRecord* Best = nullptr;
	for (const FCSRunRecord& Run : Runs)
	{
		if (Run.Setup == Setup && (!Best || Run.IsBetterThan(*Best)))
		{
			Best = &Run;
		}
	}
	return Best;
}
