#include "CSRunSetup.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const FName DummiesKey(TEXT("Dummies"));
	const FName FormationKey(TEXT("Formation"));
	const FName DistanceKey(TEXT("Distance"));
	const FName SpacingKey(TEXT("Spacing"));
	const FName OpponentsKey(TEXT("Opponents"));
	const FName OpponentWindupKey(TEXT("OpponentWindup"));
	const FName StaminaKey(TEXT("Stamina"));
	const FName StaminaRecoveryKey(TEXT("StaminaRecovery"));
	const FName LaunchersKey(TEXT("Launchers"));
	const FName TimeLimitKey(TEXT("TimeLimit"));
	const FName KnockdownGoalKey(TEXT("KnockdownGoal"));

	FCSRunRow Row(FName Key, const TCHAR* Label, const TCHAR* Unit, float Min, float Max, float Step, float Default, const TCHAR* Help)
	{
		FCSRunRow Out;
		Out.Key = Key;
		Out.Label = Label;
		Out.Unit = Unit;
		Out.Min = Min;
		Out.Max = Max;
		Out.Step = Step;
		Out.Default = Default;
		Out.Help = Help;
		return Out;
	}

	FCSRunRow ChoiceRow(FName Key, const TCHAR* Label, TArray<FString> Choices, int32 Default, const TCHAR* Help)
	{
		FCSRunRow Out = Row(Key, Label, TEXT(""), 0.f, float(Choices.Num() - 1), 1.f, float(Default), Help);
		Out.Choices = MoveTemp(Choices);
		return Out;
	}

	float Clean(const FCSRunRow& Row, float Value)
	{
		const float Stepped = Row.Min + FMath::RoundToFloat((Value - Row.Min) / Row.Step) * Row.Step;
		return FMath::Clamp(Stepped, Row.Min, Row.Max);
	}

	/** The yaw that faces From toward To on the ground; Fallback when they are at the same spot. */
	FRotator FaceToward(const FVector& From, const FVector& To, const FRotator& Fallback)
	{
		const FVector Flat = (To - From).GetSafeNormal2D();
		return Flat.IsNearlyZero() ? Fallback : FRotator(0.f, Flat.Rotation().Yaw, 0.f);
	}
}

const TArray<FCSRunRow>& FCSRunSetup::Rows()
{
	static const TArray<FCSRunRow> Table = {
		Row(DummiesKey, TEXT("Dummies"), TEXT(""), 0.f, float(MaxDummies), 1.f, 3.f, TEXT("How many target dummies the run starts with.")),
		ChoiceRow(FormationKey, TEXT("Formation"), {TEXT("Line"), TEXT("Ring")}, 0, TEXT("Line: a row ahead of you. Ring: around you.")),
		Row(DistanceKey, TEXT("Distance"), TEXT("cm"), 200.f, 2000.f, 50.f, 500.f, TEXT("From where you start to the dummies.")),
		Row(SpacingKey, TEXT("Spacing"), TEXT("cm"), 100.f, 600.f, 25.f, 200.f, TEXT("Between neighbours in a line.")),
		ChoiceRow(OpponentsKey, TEXT("Dummies do"), {TEXT("Stand still"), TEXT("Fight back")}, 0,
			TEXT("Stand still: they take hits. Fight back: they close in and punch on a pattern; block (right mouse) or dodge (Q).")),
		Row(OpponentWindupKey, TEXT("Their wind-up"), TEXT("s"), 0.2f, 1.5f, 0.05f, 0.7f,
			TEXT("How long a fighting dummy draws its fist back before it punches: its tell. Shorter is harder.")),
		ChoiceRow(StaminaKey, TEXT("Stamina"), {TEXT("Off"), TEXT("On")}, 1,
			TEXT("On: strikes, dodges and blocked hits cost stamina for everyone; out of it, strikes are weak and the guard drops.")),
		Row(StaminaRecoveryKey, TEXT("Stamina recovery"), TEXT("x"), 0.25f, 3.f, 0.25f, 1.f,
			TEXT("How fast stamina comes back, times the usual rate.")),
		Row(LaunchersKey, TEXT("Ball launchers"), TEXT(""), 0.f, float(MaxLaunchers), 1.f, 0.f, TEXT("Launchers around your start that throw balls at whoever stands on it.")),
		Row(TimeLimitKey, TEXT("Time limit"), TEXT("s"), 0.f, 600.f, 5.f, 60.f, TEXT("The run ends after this long. 0: no limit.")),
		Row(KnockdownGoalKey, TEXT("Knockdown goal"), TEXT(""), 0.f, 20.f, 1.f, 0.f, TEXT("The run ends when you have knocked down this many. 0: no goal.")),
	};
	return Table;
}

const FCSRunRow* FCSRunSetup::FindRow(FName Key)
{
	return Rows().FindByPredicate([Key](const FCSRunRow& Row) { return Row.Key == Key; });
}

float FCSRunSetup::GetValue(FName Key) const
{
	if (Key == DummiesKey) { return float(Dummies); }
	if (Key == FormationKey) { return float(uint8(Formation)); }
	if (Key == DistanceKey) { return Distance; }
	if (Key == SpacingKey) { return Spacing; }
	if (Key == OpponentsKey) { return float(uint8(Opponents)); }
	if (Key == OpponentWindupKey) { return OpponentWindup; }
	if (Key == StaminaKey) { return bStamina ? 1.f : 0.f; }
	if (Key == StaminaRecoveryKey) { return StaminaRecovery; }
	if (Key == LaunchersKey) { return float(Launchers); }
	if (Key == TimeLimitKey) { return TimeLimit; }
	if (Key == KnockdownGoalKey) { return float(KnockdownGoal); }
	return 0.f;
}

bool FCSRunSetup::SetValue(FName Key, float Value)
{
	const FCSRunRow* Found = FindRow(Key);
	if (!Found || !FMath::IsFinite(Value))
	{
		return false;
	}
	const float V = Clean(*Found, Value);
	if (Key == DummiesKey) { Dummies = FMath::RoundToInt(V); }
	else if (Key == FormationKey) { Formation = ECSRunFormation(uint8(FMath::RoundToInt(V))); }
	else if (Key == DistanceKey) { Distance = V; }
	else if (Key == SpacingKey) { Spacing = V; }
	else if (Key == OpponentsKey) { Opponents = ECSRunOpponents(uint8(FMath::RoundToInt(V))); }
	else if (Key == OpponentWindupKey) { OpponentWindup = V; }
	else if (Key == StaminaKey) { bStamina = V > 0.5f; }
	else if (Key == StaminaRecoveryKey) { StaminaRecovery = V; }
	else if (Key == LaunchersKey) { Launchers = FMath::RoundToInt(V); }
	else if (Key == TimeLimitKey) { TimeLimit = V; }
	else if (Key == KnockdownGoalKey) { KnockdownGoal = FMath::RoundToInt(V); }
	return true;
}

bool FCSRunSetup::IsChanged(FName Key) const
{
	const FCSRunRow* Found = FindRow(Key);
	return Found && !FMath::IsNearlyEqual(GetValue(Key), Found->Default, 1.e-3f);
}

FString FCSRunSetup::DescribeValue(FName Key) const
{
	const FCSRunRow* Found = FindRow(Key);
	if (!Found)
	{
		return FString();
	}
	const float Value = GetValue(Key);
	if (!Found->Choices.IsEmpty())
	{
		const int32 Index = FMath::RoundToInt(Value);
		return Found->Choices.IsValidIndex(Index) ? Found->Choices[Index] : FString();
	}
	if ((Key == TimeLimitKey || Key == KnockdownGoalKey) && Value <= 0.f)
	{
		return TEXT("none");
	}
	// A row stepped in fractions (the wind-up) shows them: "0.7 s", not "1 s".
	const FString Number = Found->Step < 1.f ? FString::SanitizeFloat(FMath::RoundToFloat(Value * 100.f) / 100.f) : FString::FromInt(FMath::RoundToInt(Value));
	return Found->Unit.IsEmpty() ? Number : Number + TEXT(" ") + Found->Unit;
}

FString FCSRunSetup::Describe() const
{
	TArray<FString> Parts;
	if (!Map.IsEmpty())
	{
		Parts.Add(Map);
	}
	if (Dummies <= 0)
	{
		Parts.Add(TEXT("no dummies"));
	}
	else
	{
		Parts.Add(FString::Printf(TEXT("%d %s %s at %d cm"), Dummies, Dummies == 1 ? TEXT("dummy") : TEXT("dummies"),
			Formation == ECSRunFormation::Ring ? TEXT("in a ring") : TEXT("in a line"), FMath::RoundToInt(Distance)));
		if (Opponents == ECSRunOpponents::FightBack)
		{
			Parts.Add(TEXT("fighting back (wind-up ") + DescribeValue(OpponentWindupKey) + TEXT(")"));
		}
	}
	if (Launchers > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d %s"), Launchers, Launchers == 1 ? TEXT("launcher") : TEXT("launchers")));
	}
	if (!bStamina)
	{
		Parts.Add(TEXT("stamina off"));
	}
	else if (IsChanged(StaminaRecoveryKey))
	{
		Parts.Add(TEXT("stamina recovery ") + DescribeValue(StaminaRecoveryKey));
	}
	Parts.Add(TimeLimit > 0.f ? FString::Printf(TEXT("%d s"), FMath::RoundToInt(TimeLimit)) : FString(TEXT("no time limit")));
	if (KnockdownGoal > 0)
	{
		Parts.Add(FString::Printf(TEXT("ends at %d %s"), KnockdownGoal, KnockdownGoal == 1 ? TEXT("knockdown") : TEXT("knockdowns")));
	}
	return FString::Join(Parts, TEXT(", "));
}

FString FCSRunSetup::ToJson() const
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), Version);
	Root->SetStringField(TEXT("map"), Map);
	const TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
	for (const FCSRunRow& Row : Rows())
	{
		Values->SetNumberField(Row.Key.ToString(), GetValue(Row.Key));
	}
	Root->SetObjectField(TEXT("values"), Values);
	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

bool FCSRunSetup::FromJson(const FString& Json, FCSRunSetup& OutSetup, TArray<FString>& OutNotes)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutNotes.Add(TEXT("not a JSON object"));
		return false;
	}
	double FileVersion = 0.0;
	if (!Root->TryGetNumberField(TEXT("version"), FileVersion) || FileVersion < 1.0)
	{
		OutNotes.Add(TEXT("no version"));
		return false;
	}
	if (FileVersion > double(Version))
	{
		OutNotes.Add(FString::Printf(TEXT("version %d is newer than this build reads (%d)"), int32(FileVersion), Version));
		return false;
	}
	FCSRunSetup Setup;
	Root->TryGetStringField(TEXT("map"), Setup.Map);
	const TSharedPtr<FJsonObject>* Values = nullptr;
	if (Root->TryGetObjectField(TEXT("values"), Values) && Values && Values->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Values)->Values)
		{
			const FName Key(*Pair.Key);
			const FCSRunRow* Found = FindRow(Key);
			double Number = 0.0;
			if (!Found)
			{
				OutNotes.Add(FString::Printf(TEXT("%s: not a field of a run setup"), *Pair.Key));
			}
			else if (!Pair.Value.IsValid() || !Pair.Value->TryGetNumber(Number) || !FMath::IsFinite(Number))
			{
				OutNotes.Add(FString::Printf(TEXT("%s: not a number; the default is used"), *Pair.Key));
			}
			else
			{
				Setup.SetValue(Key, float(Number));
				if (!FMath::IsNearlyEqual(Setup.GetValue(Key), float(Number), 1.e-3f))
				{
					OutNotes.Add(FString::Printf(TEXT("%s: %g is outside %g to %g; using %g"), *Pair.Key, Number, Found->Min, Found->Max, Setup.GetValue(Key)));
				}
			}
		}
	}
	OutSetup = Setup;
	return true;
}

bool FCSRunSetup::operator==(const FCSRunSetup& Other) const
{
	if (Map != Other.Map)
	{
		return false;
	}
	for (const FCSRunRow& Row : Rows())
	{
		if (!FMath::IsNearlyEqual(GetValue(Row.Key), Other.GetValue(Row.Key), 1.e-3f))
		{
			return false;
		}
	}
	return true;
}

TArray<FTransform> FCSRunSetup::DummySpots(const FTransform& Start) const
{
	TArray<FTransform> Spots;
	const FVector Origin = Start.GetLocation();
	const FRotator Facing(0.f, Start.Rotator().Yaw, 0.f);
	const FVector Forward = Facing.Vector();
	const FVector Right = FRotationMatrix(Facing).GetUnitAxis(EAxis::Y);
	for (int32 Index = 0; Index < Dummies; ++Index)
	{
		FVector Location;
		if (Formation == ECSRunFormation::Ring)
		{
			const float Angle = 360.f * float(Index) / float(Dummies);
			Location = Origin + FRotator(0.f, Facing.Yaw + Angle, 0.f).Vector() * Distance;
		}
		else
		{
			const float Across = (float(Index) - float(Dummies - 1) * 0.5f) * Spacing;
			Location = Origin + Forward * Distance + Right * Across;
		}
		Spots.Add(FTransform(FaceToward(Location, Origin, Facing), Location));
	}
	return Spots;
}

TArray<FTransform> FCSRunSetup::LauncherSpots(const FTransform& Start, float StandDistance) const
{
	// Behind first: a launcher ahead of the player would stand among the dummies.
	static const float Angles[MaxLaunchers] = {180.f, 90.f, 270.f, 0.f};
	TArray<FTransform> Spots;
	const FVector Origin = Start.GetLocation();
	const float Yaw = Start.Rotator().Yaw;
	for (int32 Index = 0; Index < FMath::Min(Launchers, MaxLaunchers); ++Index)
	{
		const FVector Location = Origin + FRotator(0.f, Yaw + Angles[Index], 0.f).Vector() * StandDistance;
		Spots.Add(FTransform(FaceToward(Location, Origin, FRotator::ZeroRotator), Location));
	}
	return Spots;
}
