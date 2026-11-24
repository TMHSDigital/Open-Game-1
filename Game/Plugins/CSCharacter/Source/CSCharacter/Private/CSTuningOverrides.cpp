#include "CSTuningOverrides.h"

#include "CSMovementTuning.h"
#include "CSTuningRanges.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool FCSTuningOverrides::Parse(const FString& Json, FString& OutError)
{
	OutError.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (Json.TrimStartAndEnd().IsEmpty() || !FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("the text is not valid JSON");
		return false;
	}
	double FileVersion = 0.0;
	if (!Root->TryGetNumberField(TEXT("version"), FileVersion))
	{
		OutError = TEXT("there is no version number");
		return false;
	}
	if (int32(FileVersion) != CurrentVersion)
	{
		OutError = FString::Printf(TEXT("version %d is not supported (this build reads version %d)"), int32(FileVersion), CurrentVersion);
		return false;
	}
	const TSharedPtr<FJsonObject>* ValuesObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("values"), ValuesObject) || !ValuesObject || !ValuesObject->IsValid())
	{
		OutError = TEXT("there is no \"values\" object");
		return false;
	}
	TMap<FString, float> Read;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*ValuesObject)->Values)
	{
		double Number = 0.0;
		if (!Entry.Value.IsValid() || Entry.Value->Type != EJson::Number || !Entry.Value->TryGetNumber(Number) || !FMath::IsFinite(Number))
		{
			OutError = FString::Printf(TEXT("%s is not a number"), *Entry.Key);
			return false;
		}
		Read.Add(Entry.Key, float(Number));
	}
	Version = CurrentVersion;
	Build.Reset();
	Root->TryGetStringField(TEXT("build"), Build);
	Values = MoveTemp(Read);
	return true;
}

FString FCSTuningOverrides::ToJson() const
{
	TArray<FString> Paths;
	Values.GetKeys(Paths);
	Paths.Sort();

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("version"), CurrentVersion);
	Writer->WriteValue(TEXT("build"), Build);
	Writer->WriteObjectStart(TEXT("values"));
	for (const FString& Path : Paths)
	{
		Writer->WriteValue(Path, Values[Path]);
	}
	Writer->WriteObjectEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Out;
}

bool FCSTuningOverrides::ApplyTo(UCSMovementTuning& Target, FString& OutError) const
{
	OutError.Reset();
	// Check everything first, so a bad entry leaves Target untouched.
	for (const TPair<FString, float>& Entry : Values)
	{
		const FCSTuningRow* Row = CSTuning::FindRow(Entry.Key);
		if (!Row)
		{
			OutError = FString::Printf(TEXT("%s is not a tuning value"), *Entry.Key);
			return false;
		}
		if (!FMath::IsFinite(Entry.Value) || Entry.Value < Row->Min || Entry.Value > Row->Max)
		{
			OutError = FString::Printf(TEXT("%s must be within [%g, %g] (is %g)"), *Entry.Key, Row->Min, Row->Max, Entry.Value);
			return false;
		}
	}
	for (const TPair<FString, float>& Entry : Values)
	{
		CSTuning::SetValue(Target, Entry.Key, Entry.Value);
	}
	return true;
}

void FCSTuningOverrides::PruneEqualTo(const UCSMovementTuning& Packaged)
{
	for (auto It = Values.CreateIterator(); It; ++It)
	{
		float PackagedValue = 0.f;
		if (CSTuning::GetValue(Packaged, It.Key(), PackagedValue) && FMath::IsNearlyEqual(PackagedValue, It.Value(), 1.e-4f))
		{
			It.RemoveCurrent();
		}
	}
}

FCSTuningOverrides FCSTuningOverrides::Diff(const UCSMovementTuning& Packaged, const UCSMovementTuning& Active)
{
	FCSTuningOverrides Out;
	for (const FCSTuningRow& Row : CSTuning::Rows())
	{
		float PackagedValue = 0.f, ActiveValue = 0.f;
		if (CSTuning::GetValue(Packaged, Row.Path, PackagedValue) && CSTuning::GetValue(Active, Row.Path, ActiveValue)
			&& !FMath::IsNearlyEqual(PackagedValue, ActiveValue, 1.e-4f))
		{
			Out.Values.Add(Row.Path, ActiveValue);
		}
	}
	return Out;
}

FString FCSTuningOverrideFileStore::GetPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Tuning") / TEXT("MovementTuning.json"));
}

bool FCSTuningOverrideFileStore::Load(FString& OutJson)
{
	const FString Path = GetPath();
	return IFileManager::Get().FileExists(*Path) && FFileHelper::LoadFileToString(OutJson, *Path);
}

bool FCSTuningOverrideFileStore::Save(const FString& Json)
{
	// SaveStringToFile creates the folder. Written without a byte order mark so any editor reads it.
	return FFileHelper::SaveStringToFile(Json, *GetPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FCSTuningOverrideFileStore::Clear()
{
	const FString Path = GetPath();
	return !IFileManager::Get().FileExists(*Path) || IFileManager::Get().Delete(*Path, false, true);
}
