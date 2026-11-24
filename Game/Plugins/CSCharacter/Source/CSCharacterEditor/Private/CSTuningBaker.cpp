#include "CSTuningBaker.h"

#include "CSMovementTuning.h"
#include "CSTuningOverrides.h"
#include "CSTuningRanges.h"

FCSTuningBakeResult UCSTuningBaker::ApplyValues(UCSMovementTuning* Tuning, const FString& Json)
{
	FCSTuningBakeResult Result;
	if (!Tuning)
	{
		Result.Message = TEXT("There is no tuning asset.");
		return Result;
	}
	FCSTuningOverrides Values;
	if (!Values.Parse(Json, Result.Message))
	{
		return Result;
	}
	// Try the values on a copy first, so a rejected set leaves the asset as it was.
	UCSMovementTuning* Trial = DuplicateObject<UCSMovementTuning>(Tuning, GetTransientPackage());
	if (!Values.ApplyTo(*Trial, Result.Message) || !Trial->IsValid(Result.Message))
	{
		return Result;
	}

	TArray<FString> Paths;
	Values.Values.GetKeys(Paths);
	Paths.Sort();
	FString Lines;
	for (const FString& Path : Paths)
	{
		float Old = 0.f;
		CSTuning::GetValue(*Tuning, Path, Old);
		Lines += FString::Printf(TEXT("%s%s: %g -> %g"), Lines.IsEmpty() ? TEXT("") : TEXT("\n"), *Path, Old, Values.Values[Path]);
	}
	Tuning->Modify();
	FString Unused;
	Values.ApplyTo(*Tuning, Unused);
	Tuning->MarkPackageDirty();
	Result.bSuccess = true;
	Result.Message = Lines.IsEmpty() ? FString(TEXT("No values to write.")) : Lines;
	return Result;
}
