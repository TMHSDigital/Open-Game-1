// The history of finished runs (Issue #70): one line per run, capped, a broken file never fatal, and
// the best run per setup (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.RunHistory).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../Run/CSRunHistory.h"
#include "../Run/CSRunSetup.h"
#include "../Run/CSRunStats.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace CSRunHistoryTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	FCSRunRecord Record(const TCHAR* Setup, int32 Knockdowns, int32 Dealt, int32 Taken, float Seconds)
	{
		FCSRunRecord R;
		R.When = TEXT("2026-11-27 12:00");
		R.Build = TEXT("0.1.11");
		R.Setup = Setup;
		R.End = TEXT("time up");
		R.Knockdowns = Knockdowns;
		R.Dealt = Dealt;
		R.Taken = Taken;
		R.Seconds = Seconds;
		R.StrikesThrown = Dealt + 2;
		R.StrikesLanded = Dealt;
		return R;
	}

	FString TempPath()
	{
		return FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("RunHistory"), FGuid::NewGuid().ToString() + TEXT(".jsonl"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSRunHistoryRoundTripTest, "CombatSimulator.CS.RunHistory.RoundTripAndCap", CSRunHistoryTests::TestFlags)
bool FCSRunHistoryRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace CSRunHistoryTests;
	const FString Path = TempPath();
	FCSRunHistory History;
	for (int32 i = 0; i < FCSRunHistory::MaxRuns + 5; ++i)
	{
		TestTrue(TEXT("The file is written"), History.Add(Record(TEXT("L_Plane, 1 dummy"), 0, i, 0, 30.f), i + 1 == FCSRunHistory::MaxRuns + 5 ? Path : FString()));
	}
	TestEqual(TEXT("Capped in memory"), History.Runs.Num(), FCSRunHistory::MaxRuns);
	TestEqual(TEXT("The oldest went first"), History.Runs[0].Dealt, 5);

	FCSRunHistory Back;
	TestEqual(TEXT("A clean file: no note"), Back.Load(Path), FString());
	TestEqual(TEXT("Every run comes back"), Back.Runs.Num(), FCSRunHistory::MaxRuns);
	TestEqual(TEXT("With its fields"), Back.Runs.Last().ToJson(), History.Runs.Last().ToJson());
	TestEqual(TEXT("Newest first in Recent"), Back.Recent(2)[0].Dealt, FCSRunHistory::MaxRuns + 4);

	FCSRunHistory None;
	TestEqual(TEXT("No file: no note, no runs"), None.Load(Path + TEXT(".missing")), FString());
	TestEqual(TEXT("No runs"), None.Runs.Num(), 0);
	IFileManager::Get().Delete(*Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSRunHistoryBrokenLinesTest, "CombatSimulator.CS.RunHistory.BrokenLinesAreSkipped", CSRunHistoryTests::TestFlags)
bool FCSRunHistoryBrokenLinesTest::RunTest(const FString& Parameters)
{
	using namespace CSRunHistoryTests;
	const FString Good = Record(TEXT("L_Plane, 1 dummy"), 1, 4, 2, 45.f).ToJson();
	const FString Text = Good + TEXT("\n{\"when\":\"x\"\nnot json at all\n") + TEXT("{\"when\":\"a\",\"setup\":\"s\",\"end\":\"e\",\"seconds\":-3,\"dealt\":1,\"knockdowns\":0,\"taken\":0,\"strikesThrown\":1,\"strikesLanded\":1}\n\n") + Good + TEXT("\n");
	const FString Path = TempPath();
	TestTrue(TEXT("The fixture is written"), FFileHelper::SaveStringToFile(Text, *Path));
	FCSRunHistory History;
	const FString Note = History.Load(Path);
	TestTrue(FString::Printf(TEXT("The note counts the skipped lines: %s"), *Note), Note.StartsWith(TEXT("3 line(s)")));
	TestEqual(TEXT("The good lines are kept"), History.Runs.Num(), 2);
	IFileManager::Get().Delete(*Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSRunHistoryBestTest, "CombatSimulator.CS.RunHistory.BestPerSetup", CSRunHistoryTests::TestFlags)
bool FCSRunHistoryBestTest::RunTest(const FString& Parameters)
{
	using namespace CSRunHistoryTests;
	FCSRunHistory History;
	History.Add(Record(TEXT("A"), 1, 5, 3, 60.f));
	History.Add(Record(TEXT("A"), 2, 3, 3, 60.f));  // more knockdowns wins
	History.Add(Record(TEXT("A"), 2, 3, 1, 60.f));  // then fewer taken
	History.Add(Record(TEXT("A"), 2, 3, 1, 50.f));  // then a shorter time
	History.Add(Record(TEXT("B"), 9, 9, 0, 10.f));  // another setup does not count
	const FCSRunRecord* Best = History.Best(TEXT("A"));
	if (!TestNotNull(TEXT("A has a best run"), Best))
	{
		return false;
	}
	TestEqual(TEXT("Knockdowns, then hits, then taken, then time"), Best->Seconds, 50.f);
	TestEqual(TEXT("The best of A is from A"), Best->Setup, FString(TEXT("A")));
	TestNull(TEXT("No run with an unknown setup"), History.Best(TEXT("C")));

	// Made from a real run's statistics.
	FCSRunStats Stats;
	Stats.Seconds = 65.f;
	Stats.Dealt.Hits = 6;
	Stats.Dealt.Knockdowns = 2;
	Stats.Taken.Hits = 1;
	Stats.PunchesThrown = 9;
	Stats.PunchesLanded = 6;
	FCSRunSetup Setup;
	Setup.Map = TEXT("L_Plane");
	const FCSRunRecord Made = FCSRunRecord::Make(Setup, Stats, ECSRunEnd::TimeLimit, FDateTime(2026, 11, 27, 14, 5));
	TestEqual(TEXT("When"), Made.When, FString(TEXT("2026-11-27 14:05")));
	TestEqual(TEXT("The setup text"), Made.Setup, Setup.Describe());
	TestTrue(FString::Printf(TEXT("A readable line: %s"), *Made.Describe()), Made.Describe().Contains(TEXT("1:05  6 hits, 2 knockdowns, 1 taken, strikes 6/9")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
