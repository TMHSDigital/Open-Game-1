// Baking tuning values into an asset (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Tuning).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CSMovementTuning.h"
#include "CSTuningBaker.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningBakeAppliesValuesTest, "CombatSimulator.CS.Tuning.BakeAppliesValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCSTuningBakeAppliesValuesTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UCSMovementTuning> Tuning(NewObject<UCSMovementTuning>(GetTransientPackage()));

	FCSTuningBakeResult Result = UCSTuningBaker::ApplyValues(Tuning.Get(),
		TEXT("{\"version\":1,\"build\":\"0.1.4\",\"values\":{\"JogSpeed\":450,\"HitProfile.PositionStrength\":80}}"));
	TestTrue(FString::Printf(TEXT("Good values are written (%s)"), *Result.Message), Result.bSuccess);
	TestEqual(TEXT("A plain value"), Tuning->JogSpeed, 450.f);
	TestEqual(TEXT("A profile member"), Tuning->HitProfile.PositionStrength, 80.f);
	TestTrue(TEXT("The message lists old and new"), Result.Message.Contains(TEXT("JogSpeed: 380 -> 450")));

	// A set with one bad value writes nothing at all.
	Result = UCSTuningBaker::ApplyValues(Tuning.Get(), TEXT("{\"version\":1,\"values\":{\"SprintSpeed\":800,\"WalkSpeed\":500}}"));
	TestFalse(TEXT("A value out of range is refused"), Result.bSuccess);
	TestTrue(TEXT("The message names the field"), Result.Message.Contains(TEXT("WalkSpeed")));
	TestEqual(TEXT("The good value of the same set is not written"), Tuning->SprintSpeed, 620.f);

	Result = UCSTuningBaker::ApplyValues(Tuning.Get(), TEXT("{\"version\":1,\"values\":{\"SprintSpeed\":400}}"));
	TestFalse(TEXT("Sprint below the baked jog speed is refused"), Result.bSuccess);
	TestEqual(TEXT("Sprint is unchanged"), Tuning->SprintSpeed, 620.f);

	Result = UCSTuningBaker::ApplyValues(Tuning.Get(), TEXT("not json"));
	TestFalse(TEXT("Text that is not JSON is refused"), Result.bSuccess);
	Result = UCSTuningBaker::ApplyValues(nullptr, TEXT("{\"version\":1,\"values\":{}}"));
	TestFalse(TEXT("No asset is refused"), Result.bSuccess);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
