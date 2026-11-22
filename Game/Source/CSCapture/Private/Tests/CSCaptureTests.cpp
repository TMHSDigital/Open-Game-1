// Capture driver tests: job parsing and the editor gate. Rendering itself is checked by
// tools/ue/test-capture.ps1, since automation tests run with -nullrhi.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CSCaptureSubsystem.h"
#include "CSCaptureTypes.h"

namespace
{
	constexpr EAutomationTestFlags CaptureTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSCaptureJobParsesTest, "CombatSimulator.Capture.JobParses", CaptureTestFlags)
bool FCSCaptureJobParsesTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"json({
		"outputDir": "C:/tmp/cap",
		"commands": ["r.MotionBlurQuality 0"],
		"shots": [
			{ "name": "stage-overview", "location": { "x": 100, "y": 200, "z": 300 },
			  "rotation": { "pitch": -20, "yaw": 45, "roll": 0 }, "fov": 60, "width": 1280, "height": 720 },
			{ "name": "stage-closeup", "bHidePlayer": false }
		]
	})json");

	FCSCaptureJob Job;
	FString Error;
	if (!TestTrue(TEXT("Job parses"), CSCapture::ParseJob(Json, Job, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("Two shots"), Job.Shots.Num(), 2);
	TestEqual(TEXT("Location Y"), Job.Shots[0].Location.Y, 200.0);
	TestEqual(TEXT("Rotation yaw"), Job.Shots[0].Rotation.Yaw, 45.0);
	TestEqual(TEXT("Fov"), Job.Shots[0].Fov, 60.f);
	TestEqual(TEXT("Width"), Job.Shots[0].Width, 1280);
	TestEqual(TEXT("Default width"), Job.Shots[1].Width, 1920);
	TestTrue(TEXT("Default bHidePlayer is true"), Job.Shots[0].bHidePlayer);
	TestFalse(TEXT("bHidePlayer read from JSON"), Job.Shots[1].bHidePlayer);
	TestEqual(TEXT("Default settle"), Job.SettleSeconds, 3.f);
	TestEqual(TEXT("One command"), Job.Commands.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSCaptureJobRejectsBadTest, "CombatSimulator.Capture.JobRejectsBadInput", CaptureTestFlags)
bool FCSCaptureJobRejectsBadTest::RunTest(const FString& Parameters)
{
	FCSCaptureJob Job;
	FString Error;
	TestFalse(TEXT("Not JSON"), CSCapture::ParseJob(TEXT("not json"), Job, Error));
	TestFalse(TEXT("No output dir"), CSCapture::ParseJob(TEXT(R"({ "shots": [ { "name": "a" } ] })"), Job, Error));
	TestFalse(TEXT("No shots"), CSCapture::ParseJob(TEXT(R"({ "outputDir": "C:/x" })"), Job, Error));
	TestFalse(TEXT("Unnamed shot"), CSCapture::ParseJob(TEXT(R"({ "outputDir": "C:/x", "shots": [ {} ] })"), Job, Error));
	TestFalse(TEXT("Tiny resolution"), CSCapture::ParseJob(TEXT(R"({ "outputDir": "C:/x", "shots": [ { "name": "a", "width": 10 } ] })"), Job, Error));
	TestFalse(TEXT("Huge resolution"), CSCapture::ParseJob(TEXT(R"({ "outputDir": "C:/x", "shots": [ { "name": "a", "width": 9000 } ] })"), Job, Error));
	TestFalse(TEXT("Bad fov"), CSCapture::ParseJob(TEXT(R"({ "outputDir": "C:/x", "shots": [ { "name": "a", "fov": 0 } ] })"), Job, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSCaptureNotInEditorTest, "CombatSimulator.Capture.SubsystemOffInEditor", CaptureTestFlags)
bool FCSCaptureNotInEditorTest::RunTest(const FString& Parameters)
{
	// The gate that keeps the capture driver out of the editor and PIE: these tests run in the
	// editor (GIsEditor), so the subsystem must refuse to exist here.
	TestTrue(TEXT("Running in the editor"), GIsEditor);
	TestFalse(TEXT("No capture subsystem in the editor"), GetDefault<UCSCaptureSubsystem>()->ShouldCreateSubsystem(nullptr));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
