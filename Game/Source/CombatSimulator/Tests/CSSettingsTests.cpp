// The player's settings and the look scale they drive
// (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Settings).
//
// Every test works on its own UCSGameSettings object with bPersist off, or restores the live one, so
// the user's GameUserSettings.ini is never written.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "../CSGameSettings.h"
#include "../CSPlayerController.h"
#include "CSDevTools.h"
#include "CSPlayTestRig.h"
#include "CSTuningOverrides.h"
#include "UObject/StrongObjectPtr.h"

namespace CSSettingsTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const FName Mouse(TEXT("MouseSensitivity"));
	const FName Stick(TEXT("StickSensitivity"));
	const FName Invert(TEXT("InvertY"));

	/** A settings object of its own, at the defaults, that never writes the ini. */
	TStrongObjectPtr<UCSGameSettings> Fresh()
	{
		TStrongObjectPtr<UCSGameSettings> Settings(NewObject<UCSGameSettings>(GetTransientPackage()));
		Settings->bPersist = false;
		Settings->ResetAll();
		return Settings;
	}

	/** Tuning changes kept in memory for the play test. */
	struct FMemoryStore : ICSTuningOverrideStore
	{
		FString Text;
		virtual bool Load(FString& OutJson) override
		{
			OutJson = Text;
			return !Text.IsEmpty();
		}
		virtual bool Exists() const override { return !Text.IsEmpty(); }
		virtual bool Save(const FString& Json) override
		{
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSSettingsDefaultsTest, "CombatSimulator.CS.Settings.Defaults", CSSettingsTests::TestFlags)
bool FCSSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace CSSettingsTests;
	const TStrongObjectPtr<UCSGameSettings> Settings = Fresh();
	TestEqual(TEXT("Mouse sensitivity"), Settings->MouseSensitivity, 1.f);
	TestEqual(TEXT("Stick sensitivity"), Settings->StickSensitivity, 1.f);
	TestFalse(TEXT("Invert Y"), Settings->bInvertY);
	TestEqual(TEXT("Three rows"), UCSGameSettings::Rows().Num(), 3);
	for (const FCSSettingRow& Row : UCSGameSettings::Rows())
	{
		TestEqual(FString::Printf(TEXT("%s reads its default"), *Row.Key.ToString()), Settings->GetValue(Row.Key), Row.Default);
		TestFalse(FString::Printf(TEXT("%s has a label"), *Row.Key.ToString()), Row.Label.IsEmpty());
		TestTrue(FString::Printf(TEXT("%s: default inside its range"), *Row.Key.ToString()), Row.Default >= Row.Min && Row.Default <= Row.Max);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSSettingsUpdateClampsTest, "CombatSimulator.CS.Settings.UpdateClamps", CSSettingsTests::TestFlags)
bool FCSSettingsUpdateClampsTest::RunTest(const FString& Parameters)
{
	using namespace CSSettingsTests;
	const TStrongObjectPtr<UCSGameSettings> Settings = Fresh();
	TestTrue(TEXT("A known key is accepted"), Settings->Update(Mouse, 9.f));
	TestEqual(TEXT("Above the range stops at 5"), Settings->MouseSensitivity, 5.f);
	TestTrue(TEXT("Below the range is accepted"), Settings->Update(Mouse, -3.f));
	TestEqual(TEXT("Below the range stops at 0.1"), Settings->MouseSensitivity, 0.1f);
	TestTrue(TEXT("A value between steps"), Settings->Update(Stick, 1.234f));
	TestNearlyEqual(TEXT("Is rounded to the 0.05 step"), Settings->StickSensitivity, 1.25f, 0.0001f);
	TestTrue(TEXT("The switch turns on"), Settings->Update(Invert, 1.f));
	TestTrue(TEXT("Invert Y is on"), Settings->bInvertY);
	TestEqual(TEXT("And reads as 1"), Settings->GetValue(Invert), 1.f);
	TestFalse(TEXT("An unknown key is refused"), Settings->Update(TEXT("NoSuchKey"), 1.f));
	TestEqual(TEXT("An unknown key reads as 0"), Settings->GetValue(TEXT("NoSuchKey")), 0.f);
	TestTrue(TEXT("Not a number is accepted as the default"), Settings->Update(Mouse, std::numeric_limits<float>::quiet_NaN()));
	TestEqual(TEXT("The default"), Settings->MouseSensitivity, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSSettingsSanitizeBadValuesTest, "CombatSimulator.CS.Settings.SanitizeBadValues", CSSettingsTests::TestFlags)
bool FCSSettingsSanitizeBadValuesTest::RunTest(const FString& Parameters)
{
	using namespace CSSettingsTests;
	// What a hand-edited ini can hold.
	const TStrongObjectPtr<UCSGameSettings> Settings = Fresh();
	Settings->MouseSensitivity = std::numeric_limits<float>::quiet_NaN();
	Settings->StickSensitivity = 1000.f;
	// Four values are put right below, each with one warning.
	AddExpectedMessagePlain(TEXT("outside its range"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 4);
	Settings->Sanitize();
	TestEqual(TEXT("Not a number becomes the default"), Settings->MouseSensitivity, 1.f);
	TestEqual(TEXT("Too large becomes the default"), Settings->StickSensitivity, 1.f);

	// Text in the ini ("MouseSensitivity=abc") reads as 0: that must not become the slowest look speed.
	Settings->MouseSensitivity = 0.f;
	Settings->StickSensitivity = 1.03f;
	Settings->Sanitize();
	TestEqual(TEXT("Zero (text in the ini) becomes the default, not the minimum"), Settings->MouseSensitivity, 1.f);
	TestEqual(TEXT("A value in range is left as typed"), Settings->StickSensitivity, 1.03f);

	Settings->MouseSensitivity = -1.f;
	Settings->StickSensitivity = 0.1f;
	Settings->Sanitize();
	TestEqual(TEXT("Negative becomes the default"), Settings->MouseSensitivity, 1.f);
	TestEqual(TEXT("The lowest allowed value is kept"), Settings->StickSensitivity, 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSSettingsChangeNotifiesOnceTest, "CombatSimulator.CS.Settings.ChangeNotifiesOnce", CSSettingsTests::TestFlags)
bool FCSSettingsChangeNotifiesOnceTest::RunTest(const FString& Parameters)
{
	using namespace CSSettingsTests;
	const TStrongObjectPtr<UCSGameSettings> Settings = Fresh();
	int32 Calls = 0;
	Settings->OnChanged.AddLambda([&Calls] { ++Calls; });
	Settings->Update(Mouse, 2.f);
	TestEqual(TEXT("One change, one call"), Calls, 1);
	Settings->Update(Mouse, 2.f);
	TestEqual(TEXT("The same value again: no call"), Calls, 1);
	Settings->Update(Invert, 1.f);
	TestEqual(TEXT("A second setting: a second call"), Calls, 2);
	Settings->ResetAll();
	TestEqual(TEXT("Reset all: one call for everything"), Calls, 3);
	Settings->ResetAll();
	TestEqual(TEXT("Nothing left to reset: no call"), Calls, 3);
	TestEqual(TEXT("Back at the default"), Settings->MouseSensitivity, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSSettingsLookScaleAppliedTest, "CombatSimulator.CS.Settings.LookScaleApplied", CSSettingsTests::TestFlags)
bool FCSSettingsLookScaleAppliedTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UCSMovementTuning> Tuning(NewObject<UCSMovementTuning>(GetTransientPackage()));
	FCSBareCharacterWorld World;
	if (!World.Init(*this, Tuning.Get()))
	{
		return false;
	}
	ACSCharacter* C = World.Character;
	TestEqual(TEXT("Mouse look starts unscaled"), C->ScaleLook(FVector2D(1.0, 1.0), false), FVector2D(1.0, 1.0));
	TestEqual(TEXT("Stick look starts unscaled"), C->ScaleLook(FVector2D(1.0, 1.0), true), FVector2D(1.0, 1.0));

	C->SetLookScale(FVector2D(2.0, 2.0), FVector2D(0.5, -0.5));
	TestEqual(TEXT("Mouse look is doubled"), C->ScaleLook(FVector2D(1.0, 3.0), false), FVector2D(2.0, 6.0));
	TestEqual(TEXT("Stick look is halved, pitch inverted"), C->ScaleLook(FVector2D(4.0, 2.0), true), FVector2D(2.0, -1.0));
	return true;
}

// A new character (a respawn here; a map change works the same way) gets the look settings and the
// saved tuning changes without the player doing anything.
namespace CSSettingsTests
{
	struct FNewCharacterRun
	{
		int32 Stage = 0;
		double StageStart = 0.0;
		TWeakObjectPtr<APawn> OldPawn;
		FMemoryStore Store;
		float OldMouse = 1.f;
		bool bOldPersist = true;
		bool bRestored = false;

		void Restore()
		{
			if (!bRestored)
			{
				bRestored = true;
				UCSGameSettings* Settings = UCSGameSettings::Get();
				Settings->Update(Mouse, OldMouse);
				Settings->bPersist = bOldPersist;
				UCSDevTools::SetOverrideStoreForTests(nullptr);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSSettingsAppliedToNewCharacterTest, "CombatSimulator.CS.Settings.AppliedToNewCharacter", CSSettingsTests::TestFlags)
bool FCSSettingsAppliedToNewCharacterTest::RunTest(const FString& Parameters)
{
	using namespace CSSettingsTests;
	using namespace CSPlayRig;
	StartPlay(this, PlaneMap, 1.f);
	TSharedRef<FNewCharacterRun> Run = MakeShared<FNewCharacterRun>();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UWorld* World = PlayWorld();
		ACSCharacter* Character = PlayerCharacter(World);
		if (!World)
		{
			AddError(TEXT("No play world"));
			Run->Restore();
			return true;
		}
		const double Now = World->GetTimeSeconds();
		if (Run->Stage == 0)
		{
			if (!TestNotNull(TEXT("Player character"), Character)
				|| !TestNotNull(TEXT("The player controller is an ACSPlayerController"), Cast<ACSPlayerController>(World->GetFirstPlayerController())))
			{
				return true;
			}
			UCSGameSettings* Settings = UCSGameSettings::Get();
			Run->OldMouse = Settings->MouseSensitivity;
			Run->bOldPersist = Settings->bPersist;
			Settings->bPersist = false;
			Settings->Update(Mouse, Run->OldMouse > 1.5f ? 1.f : 2.f);
			const float Wanted = Settings->MouseSensitivity;
			TestEqual(TEXT("A changed setting reaches the character at once"), float(Character->GetMouseLookScale().X), Wanted);

			Run->Store.Text = TEXT("{\"version\":1,\"values\":{\"JogSpeed\":450}}");
			UCSDevTools::SetOverrideStoreForTests(&Run->Store);
			TestTrue(TEXT("The saved tuning change loads"), Character->GetDevTools()->LoadOverrides());
			TestEqual(TEXT("And is in play"), Character->GetActiveTuning().JogSpeed, 450.f);

			Run->OldPawn = Character;
			Character->Destroy();
			Run->Stage = 1;
			Run->StageStart = Now;
			return false;
		}
		if (Character && Character != Run->OldPawn.Get())
		{
			const float Wanted = UCSGameSettings::Get()->MouseSensitivity;
			TestEqual(TEXT("The new character has the mouse look scale"), float(Character->GetMouseLookScale().X), Wanted);
			TestEqual(TEXT("The new character has the saved tuning change"), Character->GetActiveTuning().JogSpeed, 450.f);
			TestTrue(TEXT("And counts it"), Character->GetDevTools()->GetTuningSourceText().StartsWith(TEXT("tuning: packaged + 1 change")));
			Run->Restore();
			return true;
		}
		if (Now - Run->StageStart > 10.0)
		{
			AddError(TEXT("No new character 10 s after the old one was destroyed"));
			Run->Restore();
			return true;
		}
		return false;
	}));
	// Whatever happened above, put the settings and the store back before the next test.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Run]()
	{
		Run->Restore();
		return true;
	}));
	EndPlay();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
