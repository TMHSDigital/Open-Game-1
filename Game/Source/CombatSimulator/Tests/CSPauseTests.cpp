// Pause, the pause screen's model and the tuning tab's model
// (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Pause, or CombatSimulator.CS.TuningPanel).
//
// The Slate widgets themselves are thin and are checked by hand (docs/WORKFLOWS.md); everything they
// show or do goes through the models tested here.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "../CSPlayerController.h"
#include "../UI/CSPauseMenuModel.h"
#include "../UI/CSTuningPanelModel.h"
#include "../UI/SCSValueRow.h"
#include "CSDevTools.h"
#include "CSPlayTestRig.h"
#include "CSTuningOverrides.h"
#include "CSTuningRanges.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"

namespace CSPauseTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const TCHAR* ContextPath = TEXT("/Game/Input/IMC_Character.IMC_Character");
	const TCHAR* PausePath = TEXT("/Game/Input/IA_Pause.IA_Pause");
	const TCHAR* Regenerate = TEXT(" (missing or stale: run Game/Content/Python/create_input_assets.py, see its docstring)");

	struct FMemoryStore : ICSTuningOverrideStore
	{
		FString Text;
		bool bFailSave = false;
		virtual bool Load(FString& OutJson) override
		{
			OutJson = Text;
			return !Text.IsEmpty();
		}
		virtual bool Exists() const override { return !Text.IsEmpty(); }
		virtual bool Save(const FString& Json) override
		{
			if (bFailSave)
			{
				return false;
			}
			Text = Json;
			return true;
		}
		virtual bool Clear() override
		{
			Text.Reset();
			return true;
		}
		virtual FString Describe() const override { return TEXT("the test store"); }
	};

	/** A character with dev tools on, and a tuning panel model on it, saving to memory. */
	struct FPanelRig
	{
		FMemoryStore Store;
		TStrongObjectPtr<UCSMovementTuning> Tuning;
		FCSBareCharacterWorld World;
		TUniquePtr<FCSTuningPanelModel> Model;

		bool Init(FAutomationTestBase& Test)
		{
			UCSDevTools::SetOverrideStoreForTests(&Store);
			Tuning.Reset(NewObject<UCSMovementTuning>(GetTransientPackage()));
			if (!World.Init(Test, Tuning.Get()))
			{
				return false;
			}
			Model = MakeUnique<FCSTuningPanelModel>(*World.Character);
			return true;
		}

		~FPanelRig() { UCSDevTools::SetOverrideStoreForTests(nullptr); }

		/** What the store holds, parsed. */
		TMap<FString, float> Saved(FAutomationTestBase& Test) const
		{
			FCSTuningOverrides Read;
			FString Error;
			if (!Store.Text.IsEmpty())
			{
				Test.TestTrue(TEXT("The saved text parses"), Read.Parse(Store.Text, Error));
			}
			return Read.Values;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSPauseParseNumberTest, "CombatSimulator.CS.Pause.ParseNumber", CSPauseTests::TestFlags)
bool FCSPauseParseNumberTest::RunTest(const FString& Parameters)
{
	float Value = -1.f;
	TestTrue(TEXT("1.5"), SCSValueRow::ParseNumber(TEXT("1.5"), Value));
	TestEqual(TEXT("1.5 reads as 1.5"), Value, 1.5f);
	TestTrue(TEXT("A comma decimal"), SCSValueRow::ParseNumber(TEXT("1,5"), Value));
	TestEqual(TEXT("1,5 reads as 1.5"), Value, 1.5f);
	TestTrue(TEXT("Spaces and a sign"), SCSValueRow::ParseNumber(TEXT("  -20 "), Value));
	TestEqual(TEXT("-20"), Value, -20.f);
	for (const TCHAR* Bad : {TEXT(""), TEXT("   "), TEXT("abc"), TEXT("nan"), TEXT("inf"), TEXT("1e99"), TEXT("12px"), TEXT("1.2.3"), TEXT("99999999999999999999"),
		TEXT("-"), TEXT("."), TEXT("+"), TEXT("-."), TEXT(",")})
	{
		Value = 7.f;
		TestFalse(FString::Printf(TEXT("\"%s\" is not a number"), Bad), SCSValueRow::ParseNumber(Bad, Value));
		TestEqual(FString::Printf(TEXT("\"%s\" leaves the value alone"), Bad), Value, 7.f);
	}
	TestEqual(TEXT("Whole steps show no decimals"), SCSValueRow::FormatNumber(380.f, 5.f), FString(TEXT("380")));
	TestEqual(TEXT("Tenth steps show one"), SCSValueRow::FormatNumber(8.5f, 0.5f), FString(TEXT("8.5")));
	TestEqual(TEXT("Hundredth steps show two"), SCSValueRow::FormatNumber(0.35f, 0.01f), FString(TEXT("0.35")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSPauseHeldCloseKeyDoesNotReopenTest, "CombatSimulator.CS.Pause.HeldCloseKeyDoesNotReopen", CSPauseTests::TestFlags)
bool FCSPauseHeldCloseKeyDoesNotReopenTest::RunTest(const FString& Parameters)
{
	for (const FKey& Key : {EKeys::Escape, EKeys::Tab, EKeys::Gamepad_Special_Right, EKeys::Gamepad_FaceButton_Right})
	{
		TestTrue(FString::Printf(TEXT("%s closes the screen"), *Key.ToString()), FCSPauseKeyGuard::IsCloseKey(Key));
	}
	TestFalse(TEXT("Other keys do not"), FCSPauseKeyGuard::IsCloseKey(EKeys::W) || FCSPauseKeyGuard::IsCloseKey(EKeys::Enter));

	FCSPauseKeyGuard Guard;
	TestFalse(TEXT("Nothing is swallowed before the screen was closed by a key"), Guard.ShouldSwallow(EKeys::Escape, false, 10.0));

	// Escape closed the screen at t = 100 and is still held: its repeats must not reach the game.
	Guard.Arm(EKeys::Escape, 100.0);
	TestTrue(TEXT("The first repeat is swallowed"), Guard.ShouldSwallow(EKeys::Escape, false, 100.3));
	TestTrue(TEXT("So is every later one"), Guard.ShouldSwallow(EKeys::Escape, false, 101.5));
	TestFalse(TEXT("Other keys pass"), Guard.ShouldSwallow(EKeys::W, false, 101.6));
	TestFalse(TEXT("The release passes"), Guard.ShouldSwallow(EKeys::Escape, true, 102.0));
	TestFalse(TEXT("And the next press opens the screen again"), Guard.ShouldSwallow(EKeys::Escape, false, 102.5));

	// A release lost with focus must not swallow a press made much later.
	Guard.Arm(EKeys::Tab, 200.0);
	TestTrue(TEXT("Swallowed inside the limit"), Guard.ShouldSwallow(EKeys::Tab, false, 200.0 + FCSPauseKeyGuard::MaxSeconds - 0.1));
	Guard.Arm(EKeys::Tab, 300.0);
	TestFalse(TEXT("Not swallowed after the limit"), Guard.ShouldSwallow(EKeys::Tab, false, 300.0 + FCSPauseKeyGuard::MaxSeconds + 0.1));
	TestFalse(TEXT("And the guard is over"), Guard.ShouldSwallow(EKeys::Tab, false, 300.0 + FCSPauseKeyGuard::MaxSeconds + 0.2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSPauseTabsFollowDevToolsTest, "CombatSimulator.CS.Pause.TabsFollowDevTools", CSPauseTests::TestFlags)
bool FCSPauseTabsFollowDevToolsTest::RunTest(const FString& Parameters)
{
	const TArray<ECSPauseTab> Off = FCSPauseMenuModel::Tabs(false);
	const TArray<ECSPauseTab> On = FCSPauseMenuModel::Tabs(true);
	TestTrue(TEXT("Dev tools off: Settings and Keys"), Off == TArray<ECSPauseTab>({ECSPauseTab::Settings, ECSPauseTab::Keys}));
	TestTrue(TEXT("Dev tools on: Settings, Tuning, Keys"), On == TArray<ECSPauseTab>({ECSPauseTab::Settings, ECSPauseTab::Tuning, ECSPauseTab::Keys}));
	for (const ECSPauseTab Tab : On)
	{
		TestFalse(TEXT("Every tab has a label"), FCSPauseMenuModel::TabLabel(Tab).IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSPauseActionWorksWhilePausedTest, "CombatSimulator.CS.Pause.ActionWorksWhilePaused", CSPauseTests::TestFlags)
bool FCSPauseActionWorksWhilePausedTest::RunTest(const FString& Parameters)
{
	using namespace CSPauseTests;
	const UInputAction* Pause = LoadObject<UInputAction>(nullptr, PausePath);
	const UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, ContextPath);
	if (!TestNotNull(FString(TEXT("IA_Pause exists")) + Regenerate, Pause) || !TestNotNull(FString(TEXT("IMC_Character exists")) + Regenerate, Context))
	{
		return false;
	}
	TestTrue(TEXT("The pause action triggers while the game is paused"), Pause->bTriggerWhenPaused);
	TestEqual(TEXT("The player controller uses IA_Pause (DefaultGame.ini)"),
		GetDefault<ACSPlayerController>()->PauseAction.ToSoftObjectPath(), FSoftObjectPath(PausePath));

	TArray<FKey> Keys;
	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		if (Mapping.Action == Pause)
		{
			Keys.Add(Mapping.Key);
		}
		else if (Mapping.Action)
		{
			TestFalse(FString::Printf(TEXT("%s does not trigger while paused"), *Mapping.Action->GetName()), Mapping.Action->bTriggerWhenPaused);
		}
	}
	TestEqual(TEXT("Three pause keys"), Keys.Num(), 3);
	TestTrue(TEXT("Escape"), Keys.Contains(EKeys::Escape));
	TestTrue(TEXT("Tab (Escape ends Play in the editor)"), Keys.Contains(EKeys::Tab));
	TestTrue(TEXT("Gamepad Start"), Keys.Contains(EKeys::Gamepad_Special_Right));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSPauseKeyRowsFromContextTest, "CombatSimulator.CS.Pause.KeyRowsFromContext", CSPauseTests::TestFlags)
bool FCSPauseKeyRowsFromContextTest::RunTest(const FString& Parameters)
{
	using namespace CSPauseTests;
	const UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, ContextPath);
	if (!TestNotNull(FString(TEXT("IMC_Character exists")) + Regenerate, Context))
	{
		return false;
	}
	auto Find = [](const TArray<FCSKeyRow>& Rows, const TCHAR* Action) -> const FCSKeyRow*
	{
		return Rows.FindByPredicate([Action](const FCSKeyRow& Row) { return Row.Action == Action; });
	};

	const TArray<FCSKeyRow> Player = FCSPauseMenuModel::KeyRows(*Context, false);
	const FCSKeyRow* Jump = Find(Player, TEXT("Jump"));
	if (TestNotNull(TEXT("There is a Jump row"), Jump))
	{
		TestTrue(FString::Printf(TEXT("Jump lists the space bar (%s)"), *Jump->Keys), Jump->Keys.Contains(TEXT("Space Bar")));
		TestTrue(TEXT("Jump lists a gamepad button too"), Jump->Keys.Contains(TEXT(" / ")));
	}
	const FCSKeyRow* Move = Find(Player, TEXT("Move"));
	if (TestNotNull(TEXT("There is one Move row for all four keys and the stick"), Move))
	{
		TestTrue(FString::Printf(TEXT("Move lists W (%s)"), *Move->Keys), Move->Keys.StartsWith(TEXT("W")));
	}
	TestNotNull(TEXT("The pause key is listed"), Find(Player, TEXT("Pause")));
	for (const FCSKeyRow& Row : Player)
	{
		TestFalse(FString::Printf(TEXT("%s is not a dev key"), *Row.Action), Row.bDev || Row.Action.StartsWith(TEXT("Debug")));
		TestFalse(FString::Printf(TEXT("%s has keys"), *Row.Action), Row.Keys.IsEmpty());
	}

	const TArray<FCSKeyRow> Dev = FCSPauseMenuModel::KeyRows(*Context, true);
	TestTrue(TEXT("Dev tools on adds rows"), Dev.Num() > Player.Num());
	const FCSKeyRow* Reload = Find(Dev, TEXT("Debug Reload"));
	if (TestNotNull(TEXT("There is a reload row with dev tools on"), Reload))
	{
		TestTrue(FString::Printf(TEXT("Reload lists F5 (%s)"), *Reload->Keys), Reload->Keys.Contains(TEXT("F5")));
		TestTrue(TEXT("It is marked as a dev key"), Reload->bDev);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSPauseOpensAndClosesTest, "CombatSimulator.CS.Pause.OpensAndCloses", CSPauseTests::TestFlags)
bool FCSPauseOpensAndClosesTest::RunTest(const FString& Parameters)
{
	using namespace CSPlayRig;
	StartPlay(this, PlaneMap, 0.5f);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = PlayWorld();
		ACSPlayerController* PC = World ? Cast<ACSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!TestNotNull(TEXT("The player controller is an ACSPlayerController"), PC))
		{
			return true;
		}
		TestFalse(TEXT("Starts closed"), PC->IsPauseScreenOpen());
		TestFalse(TEXT("Starts running"), UGameplayStatics::IsGamePaused(World));

		PC->SetPauseScreenOpen(true);
		TestTrue(TEXT("Open"), PC->IsPauseScreenOpen());
		TestTrue(TEXT("The game is paused"), UGameplayStatics::IsGamePaused(World));
		TestTrue(TEXT("The cursor is shown"), PC->ShouldShowMouseCursor());
		TestEqual(TEXT("Time scale is untouched: this is the engine's pause"), World->GetWorldSettings()->GetEffectiveTimeDilation(), 1.f);
		TestTrue(TEXT("The Tuning tab is offered with dev tools on (DA_MovementTuning)"), PC->AreDevToolsOn());

		PC->TogglePauseScreen();
		TestFalse(TEXT("The pause key closes it"), PC->IsPauseScreenOpen());
		TestFalse(TEXT("The game runs again"), UGameplayStatics::IsGamePaused(World));
		TestFalse(TEXT("The cursor is hidden"), PC->ShouldShowMouseCursor());

		PC->TogglePauseScreen();
		TestTrue(TEXT("The pause key opens it"), PC->IsPauseScreenOpen());
		PC->SetPauseScreenOpen(false);
		TestFalse(TEXT("Left running for the next test"), UGameplayStatics::IsGamePaused(World));
		return true;
	}));
	EndPlay();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningPanelSetAppliesLiveTest, "CombatSimulator.CS.TuningPanel.SetAppliesLive", CSPauseTests::TestFlags)
bool FCSTuningPanelSetAppliesLiveTest::RunTest(const FString& Parameters)
{
	CSPauseTests::FPanelRig Rig;
	if (!Rig.Init(*this))
	{
		return false;
	}
	FString Reason;
	TestEqual(TEXT("Shows the packaged value"), Rig.Model->GetValue(TEXT("JogSpeed")), 380.f);
	TestFalse(TEXT("Nothing is marked changed"), Rig.Model->IsChanged(TEXT("JogSpeed")));
	TestEqual(TEXT("Status: packaged"), Rig.Model->StatusText(), FString(TEXT("tuning: packaged")));

	TestTrue(TEXT("A change is accepted"), Rig.Model->SetValue(TEXT("JogSpeed"), 450.f, Reason));
	TestEqual(TEXT("It is in play on the character"), Rig.World.Character->GetActiveTuning().JogSpeed, 450.f);
	TestEqual(TEXT("The row shows it"), Rig.Model->GetValue(TEXT("JogSpeed")), 450.f);
	TestTrue(TEXT("The row is marked changed"), Rig.Model->IsChanged(TEXT("JogSpeed")));
	TestTrue(TEXT("Not saved while dragging"), Rig.Store.Text.IsEmpty());
	TestEqual(TEXT("Status counts it"), Rig.Model->StatusText(), FString(TEXT("tuning: packaged + 1 change")));

	TestTrue(TEXT("Commit saves"), Rig.Model->Commit(Reason));
	TestEqual(TEXT("The store holds the change"), Rig.Saved(*this).FindRef(TEXT("JogSpeed")), 450.f);
	TestEqual(TEXT("And only that"), Rig.Saved(*this).Num(), 1);

	TestFalse(TEXT("An unknown row is refused"), Rig.Model->SetValue(TEXT("NoSuchField"), 1.f, Reason));
	TestTrue(TEXT("With a reason"), Reason.Contains(TEXT("NoSuchField")));
	TestFalse(TEXT("Not a number is refused"), Rig.Model->SetValue(TEXT("JogSpeed"), std::numeric_limits<float>::quiet_NaN(), Reason));
	TestEqual(TEXT("The value stays"), Rig.Model->GetValue(TEXT("JogSpeed")), 450.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningPanelClampsAndRoundsTest, "CombatSimulator.CS.TuningPanel.ClampsAndRounds", CSPauseTests::TestFlags)
bool FCSTuningPanelClampsAndRoundsTest::RunTest(const FString& Parameters)
{
	CSPauseTests::FPanelRig Rig;
	if (!Rig.Init(*this))
	{
		return false;
	}
	FString Reason;
	const FCSTuningRow* Sprint = CSTuning::FindRow(TEXT("SprintSpeed"));
	if (!TestNotNull(TEXT("SprintSpeed has a row"), Sprint))
	{
		return false;
	}
	TestTrue(TEXT("A value far above the range is accepted as the top of the range"), Rig.Model->SetValue(TEXT("SprintSpeed"), 1.e6f, Reason));
	TestEqual(TEXT("It stops at the row's Max"), Rig.Model->GetValue(TEXT("SprintSpeed")), Sprint->Max);
	TestTrue(TEXT("A value between steps"), Rig.Model->SetValue(TEXT("SprintSpeed"), 703.f, Reason));
	TestEqual(TEXT("Is rounded to the 5 cm/s step"), Rig.Model->GetValue(TEXT("SprintSpeed")), 705.f);

	// Walk cannot reach its own Max (400) while jog is 380: the rule between the two refuses it.
	Rig.Model->SetValue(TEXT("WalkSpeed"), 1.e6f, Reason);
	TestTrue(TEXT("Never above the row's Max"), Rig.Model->GetValue(TEXT("WalkSpeed")) <= 400.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningPanelCrossFieldRejectedTest, "CombatSimulator.CS.TuningPanel.CrossFieldRejected", CSPauseTests::TestFlags)
bool FCSTuningPanelCrossFieldRejectedTest::RunTest(const FString& Parameters)
{
	CSPauseTests::FPanelRig Rig;
	if (!Rig.Init(*this))
	{
		return false;
	}
	FString Reason;
	TArray<float> Before;
	for (const FCSTuningRow& Row : CSTuning::Rows())
	{
		Before.Add(Rig.Model->GetValue(Row.Path));
	}
	TestFalse(TEXT("Walk above jog is refused"), Rig.Model->SetValue(TEXT("WalkSpeed"), 400.f, Reason));
	TestTrue(FString::Printf(TEXT("The reason names both fields (%s)"), *Reason), Reason.Contains(TEXT("WalkSpeed")) && Reason.Contains(TEXT("JogSpeed")));
	TestTrue(TEXT("The status line shows the reason"), Rig.Model->IsStatusError() && Rig.Model->StatusText().Contains(TEXT("JogSpeed")));
	int32 Index = 0;
	for (const FCSTuningRow& Row : CSTuning::Rows())
	{
		TestEqual(FString::Printf(TEXT("%s is unchanged"), *Row.Path), Rig.Model->GetValue(Row.Path), Before[Index++]);
	}
	TestFalse(TEXT("Nothing is marked changed"), Rig.Model->IsChanged(TEXT("WalkSpeed")));

	// The next good change clears the message.
	TestTrue(TEXT("A good change after it"), Rig.Model->SetValue(TEXT("WalkSpeed"), 200.f, Reason));
	TestFalse(TEXT("The status is no longer an error"), Rig.Model->IsStatusError());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningPanelResetRowAndAllTest, "CombatSimulator.CS.TuningPanel.ResetRowAndAll", CSPauseTests::TestFlags)
bool FCSTuningPanelResetRowAndAllTest::RunTest(const FString& Parameters)
{
	CSPauseTests::FPanelRig Rig;
	if (!Rig.Init(*this))
	{
		return false;
	}
	FString Reason;
	Rig.Model->SetValue(TEXT("JogSpeed"), 450.f, Reason);
	Rig.Model->SetValue(TEXT("AirControl"), 0.5f, Reason);
	Rig.Model->Commit(Reason);
	TestEqual(TEXT("Two changes saved"), Rig.Saved(*this).Num(), 2);

	TestTrue(TEXT("Reset one row"), Rig.Model->ResetRow(TEXT("JogSpeed"), Reason));
	TestEqual(TEXT("It is back on the packaged value"), Rig.Model->GetValue(TEXT("JogSpeed")), 380.f);
	TestEqual(TEXT("The other change stays"), Rig.Model->GetValue(TEXT("AirControl")), 0.5f);
	TestEqual(TEXT("One change saved"), Rig.Saved(*this).Num(), 1);

	TestTrue(TEXT("Reset all"), Rig.Model->ResetAll(Reason));
	TestEqual(TEXT("Everything is packaged again"), Rig.Model->GetValue(TEXT("AirControl")), 0.2f);
	TestTrue(TEXT("The store is cleared"), Rig.Store.Text.IsEmpty());
	TestEqual(TEXT("Status: packaged"), Rig.Model->StatusText(), FString(TEXT("tuning: packaged")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningPanelCopyTextRoundTripsTest, "CombatSimulator.CS.TuningPanel.CopyTextRoundTrips", CSPauseTests::TestFlags)
bool FCSTuningPanelCopyTextRoundTripsTest::RunTest(const FString& Parameters)
{
	CSPauseTests::FPanelRig Rig;
	if (!Rig.Init(*this))
	{
		return false;
	}
	FString Reason;
	Rig.Model->SetValue(TEXT("JogSpeed"), 450.f, Reason);
	Rig.Model->SetValue(TEXT("ThirdPersonShoulderOffset.Y"), 60.f, Reason);
	FCSTuningOverrides Read;
	FString Error;
	TestTrue(TEXT("The copied text parses"), Read.Parse(Rig.Model->CopyText(), Error));
	TestEqual(TEXT("Two values"), Read.Values.Num(), 2);
	TestEqual(TEXT("JogSpeed"), Read.Values.FindRef(TEXT("JogSpeed")), 450.f);
	TestEqual(TEXT("A vector member"), Read.Values.FindRef(TEXT("ThirdPersonShoulderOffset.Y")), 60.f);
	TestFalse(TEXT("It names the build it came from"), Read.Build.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSTuningPanelSaveFailureShownTest, "CombatSimulator.CS.TuningPanel.SaveFailureShown", CSPauseTests::TestFlags)
bool FCSTuningPanelSaveFailureShownTest::RunTest(const FString& Parameters)
{
	CSPauseTests::FPanelRig Rig;
	if (!Rig.Init(*this))
	{
		return false;
	}
	Rig.Store.bFailSave = true;
	FString Reason;
	TestTrue(TEXT("The change applies"), Rig.Model->SetValue(TEXT("JogSpeed"), 450.f, Reason));
	AddExpectedMessagePlain(TEXT("could not be saved"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("Commit reports the failed save"), Rig.Model->Commit(Reason));
	TestTrue(TEXT("The reason says so"), Reason.Contains(TEXT("could not be saved")));
	TestTrue(TEXT("The status line says so"), Rig.Model->IsStatusError() && Rig.Model->StatusText().Contains(TEXT("could not be saved")));
	TestEqual(TEXT("The value is still in play"), Rig.World.Character->GetActiveTuning().JogSpeed, 450.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
