// The tuning range table (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Tuning).
//
// The table is what the tuning tab, the overrides file and UCSMovementTuning::IsValid all read, so
// these tests keep it complete: a field added to the asset without a row fails here.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CSMovementTuning.h"
#include "CSTuningRanges.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace CSTuningRangeTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	bool IsNumber(const FProperty* Property)
	{
		return Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>();
	}

	/** Every numeric leaf of UCSMovementTuning's own properties, as row paths (one struct level deep). */
	TArray<FString> NumericPaths()
	{
		TArray<FString> Paths;
		for (TFieldIterator<FProperty> It(UCSMovementTuning::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (IsNumber(*It))
			{
				Paths.Add(It->GetName());
			}
			else if (const FStructProperty* Struct = CastField<FStructProperty>(*It))
			{
				for (TFieldIterator<FProperty> Inner(Struct->Struct); Inner; ++Inner)
				{
					if (IsNumber(*Inner))
					{
						Paths.Add(It->GetName() + TEXT(".") + Inner->GetName());
					}
				}
			}
		}
		return Paths;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningRangeTableCoversEveryFieldTest, "CombatSimulator.CS.Tuning.RangeTableCoversEveryField", CSTuningRangeTests::TestFlags)
bool FCSTuningRangeTableCoversEveryFieldTest::RunTest(const FString& Parameters)
{
	const UCSMovementTuning* Defaults = GetDefault<UCSMovementTuning>();
	const TArray<FString> Paths = CSTuningRangeTests::NumericPaths();
	TestTrue(TEXT("The asset has numeric fields"), Paths.Num() > 40);
	for (const FString& Path : Paths)
	{
		int32 Count = 0;
		for (const FCSTuningRow& Row : CSTuning::Rows())
		{
			Count += Row.Path == Path ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("%s has exactly one row"), *Path), Count, 1);
	}
	for (const FCSTuningRow& Row : CSTuning::Rows())
	{
		float Value = 0.f;
		TestTrue(FString::Printf(TEXT("Row %s names a numeric field"), *Row.Path), CSTuning::GetValue(*Defaults, Row.Path, Value));
		TestTrue(FString::Printf(TEXT("Row %s is a field of the asset"), *Row.Path), Paths.Contains(Row.Path));
		TestFalse(FString::Printf(TEXT("Row %s has a label"), *Row.Path), Row.Label.IsEmpty());
		TestFalse(FString::Printf(TEXT("Row %s has a group"), *Row.Path), Row.Group.IsNone());
	}
	TestNull(TEXT("bDevTools has no row"), CSTuning::FindRow(TEXT("bDevTools")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningDefaultsInsideRangesTest, "CombatSimulator.CS.Tuning.DefaultsInsideRanges", CSTuningRangeTests::TestFlags)
bool FCSTuningDefaultsInsideRangesTest::RunTest(const FString& Parameters)
{
	const UCSMovementTuning* Defaults = GetDefault<UCSMovementTuning>();
	for (const FCSTuningRow& Row : CSTuning::Rows())
	{
		float Value = 0.f;
		if (!CSTuning::GetValue(*Defaults, Row.Path, Value))
		{
			AddError(FString::Printf(TEXT("Row %s does not resolve"), *Row.Path));
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: Min is below Max"), *Row.Path), Row.Min < Row.Max);
		TestTrue(FString::Printf(TEXT("%s: Step is positive"), *Row.Path), Row.Step > 0.f);
		TestTrue(FString::Printf(TEXT("%s: default %g lies in [%g, %g]"), *Row.Path, Value, Row.Min, Row.Max), Value >= Row.Min && Value <= Row.Max);
	}
	FString Reason;
	TestTrue(TEXT("The defaults are valid"), Defaults->IsValid(Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningSetGetRoundTripTest, "CombatSimulator.CS.Tuning.SetGetRoundTrip", CSTuningRangeTests::TestFlags)
bool FCSTuningSetGetRoundTripTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UCSMovementTuning> T(NewObject<UCSMovementTuning>(GetTransientPackage()));
	float Value = 0.f;

	TestTrue(TEXT("Sets a plain float"), CSTuning::SetValue(*T, TEXT("WalkSpeed"), 210.f));
	TestEqual(TEXT("The field changed"), T->WalkSpeed, 210.f);
	TestTrue(TEXT("Reads it back"), CSTuning::GetValue(*T, TEXT("WalkSpeed"), Value));
	TestEqual(TEXT("Same value"), Value, 210.f);

	TestTrue(TEXT("Sets a vector member"), CSTuning::SetValue(*T, TEXT("ThirdPersonShoulderOffset.Y"), -30.f));
	TestEqual(TEXT("The member changed"), float(T->ThirdPersonShoulderOffset.Y), -30.f);
	TestTrue(TEXT("Reads it back"), CSTuning::GetValue(*T, TEXT("ThirdPersonShoulderOffset.Y"), Value));
	TestEqual(TEXT("Same value"), Value, -30.f);

	TestTrue(TEXT("Sets a profile member"), CSTuning::SetValue(*T, TEXT("HitProfile.PositionStrength"), 75.f));
	TestEqual(TEXT("The member changed"), T->HitProfile.PositionStrength, 75.f);

	TestFalse(TEXT("An unknown field is refused"), CSTuning::SetValue(*T, TEXT("NoSuchField"), 1.f));
	TestFalse(TEXT("An unknown member is refused"), CSTuning::SetValue(*T, TEXT("HitProfile.NoSuchField"), 1.f));
	TestFalse(TEXT("A bool is not a numeric field"), CSTuning::GetValue(*T, TEXT("bDevTools"), Value));

	// The range table is what IsValid enforces.
	FString Reason;
	T->WalkSpeed = 401.f;
	T->JogSpeed = 500.f;
	TestFalse(TEXT("A value above its row's Max is invalid"), T->IsValid(Reason));
	TestTrue(TEXT("The reason names the field"), Reason.Contains(TEXT("WalkSpeed")));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningEditorRangesAgreeTest, "CombatSimulator.CS.Tuning.EditorRangesAgree", CSTuningRangeTests::TestFlags)
bool FCSTuningEditorRangesAgreeTest::RunTest(const FString& Parameters)
{
	for (const FCSTuningRow& Row : CSTuning::Rows())
	{
		// Metadata sits on the asset's own property (for a struct member, on the struct property).
		FString Head = Row.Path, Tail;
		Row.Path.Split(TEXT("."), &Head, &Tail);
		const FProperty* Property = FindFProperty<FProperty>(UCSMovementTuning::StaticClass(), *Head);
		if (!Property || !Property->HasMetaData(TEXT("UIMin")) || !Property->HasMetaData(TEXT("UIMax")))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: UIMin equals the row's Min"), *Row.Path), Property->GetFloatMetaData(TEXT("UIMin")), Row.Min);
		TestEqual(FString::Printf(TEXT("%s: UIMax equals the row's Max"), *Row.Path), Property->GetFloatMetaData(TEXT("UIMax")), Row.Max);
	}
	return true;
}
#endif // WITH_EDITOR

#endif // WITH_DEV_AUTOMATION_TESTS
