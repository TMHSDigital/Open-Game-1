// The release's start check (UCSStartupCheck; tools/steam/release.ps1 gate 11 runs it on the packaged
// build). Run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.StartupCheck

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "../CSGameMode.h"
#include "../CSStartupCheck.h"
#include "CSPlayTestRig.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSStartupCheckPassesInPlayTest, "CombatSimulator.CS.StartupCheck.PassesInPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCSStartupCheckPassesInPlayTest::RunTest(const FString& Parameters)
{
	using namespace CSPlayRig;
	const TArray<FString> Nobody = UCSStartupCheck::Problems(nullptr);
	TestEqual(TEXT("Without a player there is one problem"), Nobody.Num(), 1);
	TestTrue(TEXT("It says there is no character"), Nobody.Num() == 1 && Nobody[0].Contains(TEXT("no ACSCharacter")));

	StartPlay(this, PlaneMap, UCSStartupCheck::SettleSeconds);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = PlayWorld();
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const ACSGameMode* Mode = World ? Cast<ACSGameMode>(World->GetAuthGameMode()) : nullptr;
		if (!TestNotNull(TEXT("The game mode is an ACSGameMode"), Mode) || !TestNotNull(TEXT("The game mode has a start check"), Mode->GetStartupCheck()))
		{
			return true;
		}
		TestFalse(TEXT("Without -CSSmokeTest the check is idle"), Mode->GetStartupCheck()->IsComponentTickEnabled());
		const TArray<FString> Found = UCSStartupCheck::Problems(PC);
		TestEqual(FString::Printf(TEXT("The played game passes the check (%s)"), *FString::Join(Found, TEXT("; "))), Found.Num(), 0);
		return true;
	}));
	EndPlay();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
