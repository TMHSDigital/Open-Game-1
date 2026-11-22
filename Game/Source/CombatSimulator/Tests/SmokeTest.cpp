// Automation tests for CombatSimulator. Run with: pwsh tools/ue/test.ps1 (filter "CombatSimulator." by default).
//
// IMPLEMENT_SIMPLE_AUTOMATION_TEST declares a test class; RunTest is its body. The second argument
// is the name shown in the Session Frontend and used by the filter. The flags say where it runs
// (EditorContext: in the editor, including headless -nullrhi runs) and which group it is in
// (ProductFilter: fast tests for this project). Returning false, or any failed TestX call, fails it.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Modules/ModuleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatSimulatorSmokeProjectLoadsTest, "CombatSimulator.Smoke.ProjectLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCombatSimulatorSmokeProjectLoadsTest::RunTest(const FString& Parameters)
{
	// Proves the editor loaded the game module and the test runner finds this module's tests.
	return TestTrue(TEXT("CombatSimulator module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("CombatSimulator")));
}

#endif // WITH_DEV_AUTOMATION_TESTS
