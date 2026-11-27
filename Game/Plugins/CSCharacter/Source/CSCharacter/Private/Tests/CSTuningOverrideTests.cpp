// Tuning overrides: the file that carries changed movement values in a packaged build
// (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Tuning).
//
// Every test uses a store in memory (FMemoryStore), set with UCSDevTools::SetOverrideStoreForTests,
// so the real file under Saved/Tuning is never read or written.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CSCharacter.h"
#include "CSDevTools.h"
#include "CSMoveSim.h"
#include "CSMovementTuning.h"
#include "CSTuningOverrides.h"
#include "UObject/StrongObjectPtr.h"

namespace CSTuningOverrideTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	using CSTestSupport::FMoveSim;
	using CSTestSupport::NewTuning;

	struct FMemoryStore : ICSTuningOverrideStore
	{
		FString Text;
		bool bHasText = false;
		bool bFailSave = false;
		/** Something is stored but reading it fails (a locked file). */
		bool bUnreadable = false;
		int32 Saves = 0;

		virtual bool Load(FString& OutJson) override
		{
			if (bUnreadable)
			{
				return false;
			}
			OutJson = Text;
			return bHasText;
		}
		virtual bool Exists() const override { return bHasText; }
		virtual bool Save(const FString& Json) override
		{
			if (bFailSave)
			{
				return false;
			}
			Text = Json;
			bHasText = true;
			++Saves;
			return true;
		}
		virtual bool Clear() override
		{
			Text.Reset();
			bHasText = false;
			return true;
		}
		virtual FString Describe() const override { return TEXT("the test store"); }

		void Put(const TCHAR* Json)
		{
			Text = Json;
			bHasText = true;
		}
	};

	/** Installs a memory store for the life of a test. */
	struct FScopedStore
	{
		FMemoryStore Store;
		FScopedStore() { UCSDevTools::SetOverrideStoreForTests(&Store); }
		~FScopedStore() { UCSDevTools::SetOverrideStoreForTests(nullptr); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningOverridesRoundTripTest, "CombatSimulator.CS.Tuning.OverridesRoundTrip", CSTuningOverrideTests::TestFlags)
bool FCSTuningOverridesRoundTripTest::RunTest(const FString& Parameters)
{
	FCSTuningOverrides Written;
	Written.Build = TEXT("0.1.4");
	Written.Values.Add(TEXT("WalkSpeed"), 200.f);
	Written.Values.Add(TEXT("HitProfile.PositionStrength"), 80.f);
	Written.Values.Add(TEXT("ThirdPersonShoulderOffset.Y"), -35.5f);

	FCSTuningOverrides Read;
	FString Error;
	TestTrue(TEXT("Its own text parses"), Read.Parse(Written.ToJson(), Error));
	TestEqual(TEXT("No error"), Error, FString());
	TestEqual(TEXT("Build"), Read.Build, FString(TEXT("0.1.4")));
	TestEqual(TEXT("Three values"), Read.Values.Num(), 3);
	TestEqual(TEXT("WalkSpeed"), Read.Values.FindRef(TEXT("WalkSpeed")), 200.f);
	TestEqual(TEXT("Profile member"), Read.Values.FindRef(TEXT("HitProfile.PositionStrength")), 80.f);
	TestEqual(TEXT("Vector member"), Read.Values.FindRef(TEXT("ThirdPersonShoulderOffset.Y")), -35.5f);

	// Diff gives the same list from two tuning objects.
	TStrongObjectPtr<UCSMovementTuning> Packaged(CSTuningOverrideTests::NewTuning());
	TStrongObjectPtr<UCSMovementTuning> Active(CSTuningOverrideTests::NewTuning());
	TestTrue(TEXT("Applies to a tuning object"), Read.ApplyTo(*Active, Error));
	const FCSTuningOverrides Difference = FCSTuningOverrides::Diff(*Packaged, *Active);
	TestEqual(TEXT("Diff finds the three changes"), Difference.Values.Num(), 3);
	TestEqual(TEXT("Diff has the value"), Difference.Values.FindRef(TEXT("WalkSpeed")), 200.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningOverridesApplyTest, "CombatSimulator.CS.Tuning.OverridesApply", CSTuningOverrideTests::TestFlags)
bool FCSTuningOverridesApplyTest::RunTest(const FString& Parameters)
{
	CSTuningOverrideTests::FScopedStore Scoped;
	TStrongObjectPtr<UCSMovementTuning> Tuning(CSTuningOverrideTests::NewTuning());
	CSTuningOverrideTests::FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, Tuning.Get()))
	{
		return false;
	}
	UCSDevTools* DevTools = Sim.Character->GetDevTools();
	TestEqual(TEXT("Starts on the packaged values"), DevTools->GetTuningSourceText(), FString(TEXT("tuning: packaged")));

	FCSTuningOverrides Overrides;
	Overrides.Values.Add(TEXT("WalkSpeed"), 200.f);
	FString Reason;
	TestTrue(TEXT("A good change is accepted"), DevTools->SetOverrides(Overrides, true, Reason));
	TestEqual(TEXT("The active value changed"), Sim.Character->GetActiveTuning().WalkSpeed, 200.f);
	TestEqual(TEXT("The asset did not"), Tuning->WalkSpeed, 180.f);
	TestEqual(TEXT("The packaged tuning is the asset"), Sim.Character->GetPackagedTuning().WalkSpeed, 180.f);
	Sim.Character->SetGait(ECSGait::Walk);
	TestEqual(TEXT("The movement component uses it"), Sim.Character->GetCharacterMovement()->MaxWalkSpeed, 200.f);
	TestEqual(TEXT("One change"), DevTools->GetTuningSourceText(), FString(TEXT("tuning: packaged + 1 change")));
	TestEqual(TEXT("Saved once"), Scoped.Store.Saves, 1);

	FCSTuningOverrides Saved;
	FString Error;
	TestTrue(TEXT("The saved text parses"), Saved.Parse(Scoped.Store.Text, Error));
	TestEqual(TEXT("It holds the change"), Saved.Values.FindRef(TEXT("WalkSpeed")), 200.f);

	Overrides.Values.Add(TEXT("JogSpeed"), 450.f);
	TestTrue(TEXT("A second change"), DevTools->SetOverrides(Overrides, false, Reason));
	TestEqual(TEXT("Two changes"), DevTools->GetTuningSourceText(), FString(TEXT("tuning: packaged + 2 changes")));
	TestEqual(TEXT("Not saved when not asked"), Scoped.Store.Saves, 1);
	TestTrue(TEXT("The overlay shows the source"), DevTools->GetOverlayText().Contains(TEXT("packaged + 2 changes")));

	TestTrue(TEXT("Clearing is accepted"), DevTools->SetOverrides(FCSTuningOverrides(), true, Reason));
	TestEqual(TEXT("Back on the packaged value"), Sim.Character->GetActiveTuning().WalkSpeed, 180.f);
	TestFalse(TEXT("The store is cleared"), Scoped.Store.bHasText);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningOverridesRejectBadFileTest, "CombatSimulator.CS.Tuning.OverridesRejectBadFile", CSTuningOverrideTests::TestFlags)
bool FCSTuningOverridesRejectBadFileTest::RunTest(const FString& Parameters)
{
	struct FCase { const TCHAR* What; const TCHAR* Json; const TCHAR* Expect; };
	const FCase Cases[] = {
		{TEXT("an empty file"), TEXT(""), TEXT("not valid JSON")},
		{TEXT("a cut-off file"), TEXT("{"), TEXT("not valid JSON")},
		{TEXT("text for a number"), TEXT("{\"version\":1,\"values\":{\"WalkSpeed\":\"fast\"}}"), TEXT("WalkSpeed")},
		{TEXT("walk above jog"), TEXT("{\"version\":1,\"values\":{\"WalkSpeed\":390}}"), TEXT("JogSpeed")},
		{TEXT("another version"), TEXT("{\"version\":7,\"values\":{}}"), TEXT("version")},
		{TEXT("no values"), TEXT("{\"version\":1}"), TEXT("values")},
	};
	// An unknown path, the dev tools switch and a value out of range are not rejections: see
	// OverridesFitToThisBuild.
	// Each rejection is logged as a warning.
	AddExpectedMessagePlain(TEXT("rejected, "), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, UE_ARRAY_COUNT(Cases));
	for (const FCase& Case : Cases)
	{
		CSTuningOverrideTests::FScopedStore Scoped;
		TStrongObjectPtr<UCSMovementTuning> Tuning(CSTuningOverrideTests::NewTuning());
		CSTuningOverrideTests::FMoveSim Sim;
		if (!Sim.Init(*this, 1.f / 60.f, Tuning.Get()))
		{
			return false;
		}
		UCSDevTools* DevTools = Sim.Character->GetDevTools();
		Scoped.Store.Put(Case.Json);
		TestFalse(FString::Printf(TEXT("%s is rejected"), Case.What), DevTools->LoadOverrides());
		TestFalse(FString::Printf(TEXT("%s: the result is a rejection"), Case.What), DevTools->WasLastReloadAccepted());
		TestTrue(FString::Printf(TEXT("%s: the message names the cause (%s): %s"), Case.What, Case.Expect, *DevTools->GetLastReloadMessage()),
			DevTools->GetLastReloadMessage().Contains(Case.Expect));
		TestEqual(FString::Printf(TEXT("%s: the packaged value stays"), Case.What), Sim.Character->GetActiveTuning().WalkSpeed, 180.f);
		TestEqual(FString::Printf(TEXT("%s: no change is counted"), Case.What), DevTools->GetTuningSourceText(), FString(TEXT("tuning: packaged")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningOverridesFitToThisBuildTest, "CombatSimulator.CS.Tuning.OverridesFitToThisBuild", CSTuningOverrideTests::TestFlags)
bool FCSTuningOverridesFitToThisBuildTest::RunTest(const FString& Parameters)
{
	// Saved by an older build: a row since removed, the dev tools switch (never a row), a value under
	// a minimum since raised, and a change that still fits. Only the one that fits survives as it was.
	CSTuningOverrideTests::FScopedStore Scoped;
	TStrongObjectPtr<UCSMovementTuning> Tuning(CSTuningOverrideTests::NewTuning());
	CSTuningOverrideTests::FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, Tuning.Get()))
	{
		return false;
	}
	UCSDevTools* DevTools = Sim.Character->GetDevTools();
	Scoped.Store.Put(TEXT("{\"version\":1,\"values\":{\"NoSuchField\":1,\"bDevTools\":0,\"StaminaRegen\":0,\"WalkSpeed\":200}}"));
	AddExpectedMessagePlain(TEXT("made to fit this build"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestTrue(TEXT("The file is accepted"), DevTools->LoadOverrides());
	TestTrue(TEXT("The result is accepted"), DevTools->WasLastReloadAccepted());
	TestEqual(TEXT("The change that fits is in use"), Sim.Character->GetActiveTuning().WalkSpeed, 200.f);
	TestEqual(TEXT("The value under the minimum is raised to it"), Sim.Character->GetActiveTuning().StaminaRegen, 1.f);
	TestTrue(TEXT("Dev tools stay on"), Sim.Character->GetActiveTuning().bDevTools);
	const FString Message = DevTools->GetLastReloadMessage();
	for (const TCHAR* Named : {TEXT("NoSuchField"), TEXT("bDevTools"), TEXT("StaminaRegen")})
	{
		TestTrue(FString::Printf(TEXT("The message names %s: %s"), Named, *Message), Message.Contains(Named));
	}
	TestFalse(TEXT("The message does not name the change that fit"), Message.Contains(TEXT("WalkSpeed")));

	// FitToRanges on its own: what it keeps, what it changes.
	FCSTuningOverrides Overrides;
	Overrides.Values = {{TEXT("NoSuchField"), 1.f}, {TEXT("StaminaRegen"), 0.f}, {TEXT("WalkSpeed"), 200.f}};
	const TArray<FString> Notes = Overrides.FitToRanges();
	TestEqual(TEXT("Two notes"), Notes.Num(), 2);
	TestFalse(TEXT("The unknown path is gone"), Overrides.Values.Contains(TEXT("NoSuchField")));
	TestEqual(TEXT("The value is clamped"), Overrides.Values.FindRef(TEXT("StaminaRegen")), 1.f);
	TestEqual(TEXT("The fitting value is kept"), Overrides.Values.FindRef(TEXT("WalkSpeed")), 200.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningOverridesIgnoredWithDevToolsOffTest, "CombatSimulator.CS.Tuning.OverridesIgnoredWithDevToolsOff", CSTuningOverrideTests::TestFlags)
bool FCSTuningOverridesIgnoredWithDevToolsOffTest::RunTest(const FString& Parameters)
{
	CSTuningOverrideTests::FScopedStore Scoped;
	TStrongObjectPtr<UCSMovementTuning> Tuning(CSTuningOverrideTests::NewTuning());
	Tuning->bDevTools = false;
	CSTuningOverrideTests::FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, Tuning.Get()))
	{
		return false;
	}
	UCSDevTools* DevTools = Sim.Character->GetDevTools();
	Scoped.Store.Put(TEXT("{\"version\":1,\"values\":{\"WalkSpeed\":200}}"));
	TestFalse(TEXT("The file is not read"), DevTools->LoadOverrides());
	TestEqual(TEXT("The packaged value stays"), Sim.Character->GetActiveTuning().WalkSpeed, 180.f);
	TestEqual(TEXT("No change is counted"), DevTools->GetTuningSourceText(), FString(TEXT("tuning: packaged")));

	FCSTuningOverrides Overrides;
	Overrides.Values.Add(TEXT("WalkSpeed"), 200.f);
	FString Reason;
	TestFalse(TEXT("A change is refused"), DevTools->SetOverrides(Overrides, true, Reason));
	TestTrue(TEXT("The reason says dev tools are off"), Reason.Contains(TEXT("dev tools")));
	TestEqual(TEXT("The packaged value still stays"), Sim.Character->GetActiveTuning().WalkSpeed, 180.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningOverridesPruneAndCountTest, "CombatSimulator.CS.Tuning.OverridesPruneAndCount", CSTuningOverrideTests::TestFlags)
bool FCSTuningOverridesPruneAndCountTest::RunTest(const FString& Parameters)
{
	CSTuningOverrideTests::FScopedStore Scoped;
	TStrongObjectPtr<UCSMovementTuning> Tuning(CSTuningOverrideTests::NewTuning());
	CSTuningOverrideTests::FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, Tuning.Get()))
	{
		return false;
	}
	UCSDevTools* DevTools = Sim.Character->GetDevTools();
	// WalkSpeed equals the packaged 180, so only JogSpeed is a change. The build is not this one.
	Scoped.Store.Put(TEXT("{\"version\":1,\"build\":\"0.0.1-test\",\"values\":{\"WalkSpeed\":180,\"JogSpeed\":450}}"));
	TestTrue(TEXT("The file is accepted"), DevTools->LoadOverrides());
	TestEqual(TEXT("The change is active"), Sim.Character->GetActiveTuning().JogSpeed, 450.f);
	TestEqual(TEXT("One change, and the file's build is named"), DevTools->GetTuningSourceText(),
		FString(TEXT("tuning: packaged + 1 change (file from 0.0.1-test)")));
	TestEqual(TEXT("Only the real change is kept"), DevTools->GetOverrides().Values.Num(), 1);

	// With nothing stored, the packaged values come back.
	Scoped.Store.Clear();
	TestTrue(TEXT("No file is not an error"), DevTools->LoadOverrides());
	TestEqual(TEXT("Back on the packaged value"), Sim.Character->GetActiveTuning().JogSpeed, 380.f);
	TestEqual(TEXT("No change"), DevTools->GetTuningSourceText(), FString(TEXT("tuning: packaged")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningOverridesSaveFailureKeepsLiveValueTest, "CombatSimulator.CS.Tuning.OverridesSaveFailureKeepsLiveValue", CSTuningOverrideTests::TestFlags)
bool FCSTuningOverridesSaveFailureKeepsLiveValueTest::RunTest(const FString& Parameters)
{
	CSTuningOverrideTests::FScopedStore Scoped;
	Scoped.Store.bFailSave = true;
	TStrongObjectPtr<UCSMovementTuning> Tuning(CSTuningOverrideTests::NewTuning());
	CSTuningOverrideTests::FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, Tuning.Get()))
	{
		return false;
	}
	UCSDevTools* DevTools = Sim.Character->GetDevTools();
	FCSTuningOverrides Overrides;
	Overrides.Values.Add(TEXT("JogSpeed"), 450.f);
	FString Reason;
	AddExpectedMessagePlain(TEXT("could not be saved"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestTrue(TEXT("The change is still accepted"), DevTools->SetOverrides(Overrides, true, Reason));
	TestEqual(TEXT("The value is active"), Sim.Character->GetActiveTuning().JogSpeed, 450.f);
	TestTrue(TEXT("The reason says it was not saved"), Reason.Contains(TEXT("could not be saved")));
	TestTrue(TEXT("So does the on-screen message"), DevTools->GetLastReloadMessage().Contains(TEXT("could not be saved")));
	TestFalse(TEXT("And it is shown as a problem"), DevTools->WasLastReloadAccepted());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningOverridesSurviveF5Test, "CombatSimulator.CS.Tuning.OverridesSurviveF5", CSTuningOverrideTests::TestFlags)
bool FCSTuningOverridesSurviveF5Test::RunTest(const FString& Parameters)
{
	CSTuningOverrideTests::FScopedStore Scoped;
	TStrongObjectPtr<UCSMovementTuning> Tuning(CSTuningOverrideTests::NewTuning());
	CSTuningOverrideTests::FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, Tuning.Get()))
	{
		return false;
	}
	UCSDevTools* DevTools = Sim.Character->GetDevTools();
	FCSTuningOverrides Overrides;
	Overrides.Values.Add(TEXT("JogSpeed"), 450.f);
	FString Reason;
	TestTrue(TEXT("A change is accepted"), DevTools->SetOverrides(Overrides, true, Reason));
	TestTrue(TEXT("F5 reloads"), DevTools->RequestReload());
	TestEqual(TEXT("The change is still active"), Sim.Character->GetActiveTuning().JogSpeed, 450.f);
	TestTrue(TEXT("The reload line names the source"), DevTools->GetLastReloadMessage().Contains(TEXT("packaged + 1 change")));

	// An asset edit that conflicts with a saved change: the reload is rejected, the last good values stay.
	Tuning->SprintSpeed = 440.f;
	Tuning->JogSpeed = 300.f;
	AddExpectedMessagePlain(TEXT("rejected, "), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("A reload whose result is invalid is rejected"), DevTools->RequestReload());
	TestEqual(TEXT("The last good values stay"), Sim.Character->GetActiveTuning().SprintSpeed, 620.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningOverridesUnreadableFileIsReportedTest, "CombatSimulator.CS.Tuning.OverridesUnreadableFileIsReported", CSTuningOverrideTests::TestFlags)
bool FCSTuningOverridesUnreadableFileIsReportedTest::RunTest(const FString& Parameters)
{
	// A file that is there but cannot be read (locked by another program) is not "no changes".
	CSTuningOverrideTests::FScopedStore Scoped;
	TStrongObjectPtr<UCSMovementTuning> Tuning(CSTuningOverrideTests::NewTuning());
	CSTuningOverrideTests::FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, Tuning.Get()))
	{
		return false;
	}
	UCSDevTools* DevTools = Sim.Character->GetDevTools();
	Scoped.Store.Put(TEXT("{\"version\":1,\"values\":{\"JogSpeed\":450}}"));
	TestTrue(TEXT("The readable file loads"), DevTools->LoadOverrides());
	TestEqual(TEXT("Its change is in play"), Sim.Character->GetActiveTuning().JogSpeed, 450.f);

	Scoped.Store.bUnreadable = true;
	AddExpectedMessagePlain(TEXT("could not be read"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("An unreadable file is a failure"), DevTools->LoadOverrides());
	TestTrue(TEXT("The message says it could not be read"), DevTools->GetLastReloadMessage().Contains(TEXT("could not be read")));
	TestFalse(TEXT("It is shown as a problem"), DevTools->WasLastReloadAccepted());
	TestEqual(TEXT("The last good values stay in play"), Sim.Character->GetActiveTuning().JogSpeed, 450.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningOverridesKeptAfterFailedSaveTest, "CombatSimulator.CS.Tuning.OverridesKeptAfterFailedSave", CSTuningOverrideTests::TestFlags)
bool FCSTuningOverridesKeptAfterFailedSaveTest::RunTest(const FString& Parameters)
{
	// Changes that could not be saved stay in use for the rest of the run: a reload, or a new
	// character reading the store, must not fall back to the older file.
	CSTuningOverrideTests::FScopedStore Scoped;
	TStrongObjectPtr<UCSMovementTuning> Tuning(CSTuningOverrideTests::NewTuning());
	CSTuningOverrideTests::FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, Tuning.Get()))
	{
		return false;
	}
	UCSDevTools* DevTools = Sim.Character->GetDevTools();
	Scoped.Store.Put(TEXT("{\"version\":1,\"values\":{\"JogSpeed\":400}}"));
	TestTrue(TEXT("The older file loads"), DevTools->LoadOverrides());

	Scoped.Store.bFailSave = true;
	FCSTuningOverrides Overrides;
	Overrides.Values.Add(TEXT("JogSpeed"), 450.f);
	FString Reason;
	AddExpectedMessagePlain(TEXT("could not be saved"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestTrue(TEXT("The change applies though the save fails"), DevTools->SetOverrides(Overrides, true, Reason));
	TestTrue(TEXT("The store still holds the older value"), Scoped.Store.Text.Contains(TEXT("400")));

	TestTrue(TEXT("Reading the store again (a new character, F5)"), DevTools->LoadOverrides());
	TestEqual(TEXT("Keeps the unsaved change, not the older file"), Sim.Character->GetActiveTuning().JogSpeed, 450.f);

	// Once a save works, the store is the truth again.
	Scoped.Store.bFailSave = false;
	Overrides.Values.Add(TEXT("JogSpeed"), 460.f);
	TestTrue(TEXT("A later change saves"), DevTools->SetOverrides(Overrides, true, Reason));
	TestTrue(TEXT("The store holds it"), Scoped.Store.Text.Contains(TEXT("460")));
	Scoped.Store.Put(TEXT("{\"version\":1,\"values\":{\"JogSpeed\":470}}"));
	TestTrue(TEXT("Reading the store"), DevTools->LoadOverrides());
	TestEqual(TEXT("Now follows the store"), Sim.Character->GetActiveTuning().JogSpeed, 470.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
