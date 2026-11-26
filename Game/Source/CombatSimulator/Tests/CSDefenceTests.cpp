// An opponent that fights back, the block and the dodge (decision 0019)
// (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Defence).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "../CSGameMode.h"
#include "../CSTargetDummy.h"
#include "../Opponent/CSOpponentController.h"
#include "../Playtest/CSPlaytestRunner.h"
#include "../Run/CSRunDirector.h"
#include "../Run/CSRunSetup.h"
#include "../Run/CSRunStats.h"
#include "../Run/CSRunStore.h"
#include "CSGuardComponent.h"
#include "CSMovementTuning.h"
#include "CSTuningRanges.h"
#include "CSPhysicsBodyComponent.h"
#include "CSPlayTestRig.h"
#include "CSStrikeComponent.h"
#include "Dom/JsonObject.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CSDefenceTests
{
	using namespace CSPlayRig;

	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	struct FStage
	{
		int32 Stage = 0;
		double StageStart = 0.0;
		FVector Start = FVector::ZeroVector;

		void Next()
		{
			++Stage;
			StageStart = FPlatformTime::Seconds();
		}
		double Waited() const { return FPlatformTime::Seconds() - StageStart; }
	};

	void RestoreStoreFile()
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
		{
			UCSRunStore::bUseFile = true;
			return true;
		}));
	}

	/** The number at the start of Text after Key ("3 thrown, 1 landed": Key "landed" gives 1). */
	int32 CountBefore(const FString& Text, const TCHAR* Key)
	{
		const int32 At = Text.Find(Key);
		if (At == INDEX_NONE)
		{
			return -1;
		}
		FString Left = Text.Left(At).TrimEnd();
		int32 Space = INDEX_NONE;
		Left.FindLastChar(TEXT(' '), Space);
		return FCString::Atoi(*Left.RightChop(Space + 1));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSDefenceRulesTest, "CombatSimulator.CS.Defence.GuardRunSetupAndStats", CSDefenceTests::TestFlags)
bool FCSDefenceRulesTest::RunTest(const FString& Parameters)
{
	// The guard covers what is in front.
	const FVector Ahead(1.f, 0.f, 0.f);
	TestTrue(TEXT("Straight ahead is covered"), UCSGuardComponent::IsInFront(Ahead, FVector(100.f, 0.f, 50.f), 70.f));
	TestTrue(TEXT("60 degrees off is covered at 70"), UCSGuardComponent::IsInFront(Ahead, FVector(50.f, 86.6f, 0.f), 70.f));
	TestFalse(TEXT("90 degrees off is not"), UCSGuardComponent::IsInFront(Ahead, FVector(0.f, 100.f, 0.f), 70.f));
	TestFalse(TEXT("Behind is not"), UCSGuardComponent::IsInFront(Ahead, FVector(-100.f, 0.f, 0.f), 70.f));
	TestTrue(TEXT("Right on top: taken by the guard"), UCSGuardComponent::IsInFront(Ahead, FVector(0.f, 0.f, 30.f), 70.f));

	// The run setup: whether the dummies fight back, and their wind-up.
	FCSRunSetup Setup;
	TestEqual(TEXT("Dummies stand still by default"), Setup.Opponents, ECSRunOpponents::StandStill);
	TestFalse(TEXT("And the description says nothing of fighting"), Setup.Describe().Contains(TEXT("fighting")));
	TestTrue(TEXT("Fight back can be set"), Setup.SetValue(TEXT("Opponents"), 1.f));
	TestTrue(TEXT("The wind-up can be set"), Setup.SetValue(TEXT("OpponentWindup"), 0.43f));
	TestEqual(TEXT("It is rounded to its step"), Setup.OpponentWindup, 0.45f, 1.e-4f);
	TestEqual(TEXT("And read with its fraction"), Setup.DescribeValue(TEXT("OpponentWindup")), FString(TEXT("0.45 s")));
	TestEqual(TEXT("The choice reads as a word"), Setup.DescribeValue(TEXT("Opponents")), FString(TEXT("Fight back")));
	TestTrue(TEXT("The description says so"), Setup.Describe().Contains(TEXT("fighting back (wind-up 0.45 s)")));
	FCSRunSetup Loaded;
	TArray<FString> Notes;
	TestTrue(TEXT("It saves and loads"), FCSRunSetup::FromJson(Setup.ToJson(), Loaded, Notes) && Loaded == Setup);
	FCSRunSetup Old;
	TestTrue(TEXT("A setup saved before these fields loads with them at their defaults"),
		FCSRunSetup::FromJson(TEXT("{\"version\": 1, \"map\": \"L_Plane\", \"values\": {\"Dummies\": 2}}"), Old, Notes)
		&& Old.Opponents == ECSRunOpponents::StandStill && FMath::IsNearlyEqual(Old.OpponentWindup, 0.7f));

	// The stats: blocked hits, and the opponents' punches.
	FCSRunStats Stats;
	FCSContactRecord Hit;
	Hit.Bone = TEXT("hand_l");
	Hit.VelocityChange = 40.f;
	Hit.Result = ECSContactResult::Blocked;
	Stats.RecordHit(ECSRunHitSide::Taken, Hit);
	Hit.Result = ECSContactResult::Stagger;
	Stats.RecordHit(ECSRunHitSide::Taken, Hit);
	TestEqual(TEXT("A blocked hit is counted as blocked"), Stats.Taken.Blocked, 1);
	TestEqual(TEXT("Not as a stagger or no effect"), Stats.Taken.Staggers + Stats.Taken.NoEffect, 1);
	TestTrue(TEXT("The HUD counts what got through and what was blocked"), Stats.HudLine(0.f, 0).EndsWith(TEXT("taken 1   blocked 1")));
	Stats.RecordOpponentPunch();
	Stats.RecordOpponentPunch();
	Stats.RecordOpponentPunch();
	Stats.RecordOpponentPunchResult(ECSContactResult::Stagger);
	Stats.RecordOpponentPunchResult(ECSContactResult::Blocked);
	Stats.RecordDodge();
	const FString Summary = FString::Join(Stats.SummaryLines(), TEXT("\n"));
	TestTrue(TEXT("The results list their punches"), Summary.Contains(TEXT("Their punches: 3 thrown, 1 landed, 1 blocked, 1 missed")));
	TestTrue(TEXT("And the dodges"), Summary.Contains(TEXT("Dodges: 1")));
	TestTrue(TEXT("And the blocked hits taken"), Summary.Contains(TEXT("1 blocked)")));

	// The new tuning has ranges, so the tuning tab shows it.
	for (const TCHAR* Field : {TEXT("BlockAbsorb"), TEXT("BlockAngle"), TEXT("BlockSpeedScale"), TEXT("DodgeSpeed"), TEXT("DodgeSeconds"), TEXT("DodgeCooldownSeconds")})
	{
		TestNotNull(FString::Printf(TEXT("%s has a tuning range"), Field), CSTuning::FindRow(Field));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSDefenceBlockAndDodgeTest, "CombatSimulator.CS.Defence.BlockTakesTheHitAndTheDodgeSteps", CSDefenceTests::TestFlags)
bool FCSDefenceBlockAndDodgeTest::RunTest(const FString& Parameters)
{
	using namespace CSDefenceTests;
	StartPlay(this, PlaneMap, 1.5f);
	TSharedRef<FStage> Run = MakeShared<FStage>();
	Run->StageStart = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UWorld* World = PlayWorld();
		ACSCharacter* Player = PlayerCharacter(World);
		UCSGuardComponent* Guard = Player ? Player->GetGuard() : nullptr;
		UCSPhysicsBodyComponent* Body = Player ? Player->GetPhysicsBody() : nullptr;
		if (!Guard || !Body || Run->Waited() > 20.0)
		{
			AddError(FString::Printf(TEXT("Stage %d did not finish (player, guard and body: %d)"), Run->Stage, Guard && Body ? 1 : 0));
			return true;
		}
		const UCSMovementTuning& T = Player->GetActiveTuning();
		const FVector Chest = Player->GetMesh()->GetSocketLocation(TEXT("spine_03"));
		const FVector Ahead = Player->GetActorForwardVector();
		// A punch-sized hit: 14 kg at 600 cm/s from 40 cm away.
		auto Hit = [&](const FVector& From, AActor* Source)
		{
			return Body->ReceiveContact(Source, TEXT("spine_03"), (Chest - From).GetSafeNormal() * 600.f, 14.f, From, Chest, 1.f, true);
		};
		switch (Run->Stage)
		{
		case 0:
		{
			TestFalse(TEXT("The guard is down until the block is held"), Guard->IsGuardUp());
			Guard->SetBlockHeld(true);
			TestTrue(TEXT("Held, it is up"), Guard->IsGuardUp());
			const ECSContactResult Front = Hit(Chest + Ahead * 40.f, nullptr);
			TestEqual(TEXT("A hit from in front is blocked"), Front, ECSContactResult::Blocked);
			const float Blocked = Body->GetLastContact().VelocityChange;
			TestEqual(TEXT("The guard counted it"), Guard->GetBlockedCount(), 1);
			Run->Next();
			Run->Start = FVector(Blocked, 0.f, 0.f);
			return false;
		}
		case 1:
		{
			if (Run->Waited() < 1.0)
			{
				return false; // settle from the last hit
			}
			const ECSContactResult Back = Hit(Chest - Ahead * 40.f, nullptr);
			TestEqual(TEXT("A hit from behind gets through the guard"), Back, ECSContactResult::Stagger);
			const float Full = Body->GetLastContact().VelocityChange;
			AddInfo(FString::Printf(TEXT("Blocked %.0f cm/s, through the guard %.0f cm/s"), Run->Start.X, Full));
			TestTrue(TEXT("The guard took about BlockAbsorb of it"), FMath::IsNearlyEqual(Run->Start.X, Full * (1.f - T.BlockAbsorb), Full * 0.1f));
			Guard->SetBlockHeld(false);
			TestFalse(TEXT("Let go, the guard is down"), Guard->IsGuardUp());
			Run->Next();
			return false;
		}
		case 2:
		{
			if (Run->Waited() < 1.0)
			{
				return false;
			}
			TestEqual(TEXT("Without the guard a hit from in front staggers"), Hit(Chest + Ahead * 40.f, nullptr), ECSContactResult::Stagger);
			Run->Next();
			return false;
		}
		case 3:
		{
			if (Run->Waited() < 1.2)
			{
				return false;
			}
			Run->Start = Player->GetActorLocation();
			TestTrue(TEXT("A dodge starts"), Player->DodgeStep());
			TestTrue(TEXT("It is under way"), Guard->IsDodging());
			TestFalse(TEXT("A second cannot start at once"), Player->DodgeStep());
			Run->Next();
			return false;
		}
		default:
		{
			if (Run->Waited() < T.DodgeSeconds + 0.3)
			{
				return false;
			}
			const FVector Moved = Player->GetActorLocation() - Run->Start;
			const float Back = -float(FVector::DotProduct(Moved, Ahead));
			AddInfo(FString::Printf(TEXT("A dodge from standing moved %.0f cm back"), Back));
			TestTrue(TEXT("Standing still, a dodge steps straight back, most of DodgeSpeed * DodgeSeconds"), Back >= 0.7f * T.DodgeSpeed * T.DodgeSeconds);
			TestFalse(TEXT("It is over"), Guard->IsDodging());
			TestEqual(TEXT("And counted once"), Guard->GetDodgeCount(), 1);
			return true;
		}
		}
	}));
	EndPlay();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSDefenceFightScenarioTest, "CombatSimulator.CS.Defence.PlaytestFightScenarioRuns", CSDefenceTests::TestFlags)
bool FCSDefenceFightScenarioTest::RunTest(const FString& Parameters)
{
	using namespace CSDefenceTests;
	UCSRunStore::bUseFile = false;
	StartPlay(this, PlaneMap, 1.f);
	TSharedRef<FStage> Run = MakeShared<FStage>();
	Run->StageStart = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UWorld* World = PlayWorld();
		ACSGameMode* Mode = World ? Cast<ACSGameMode>(World->GetAuthGameMode()) : nullptr;
		UCSPlaytestRunner* Runner = Mode ? Mode->GetPlaytestRunner() : nullptr;
		if (!TestNotNull(TEXT("The game mode has a play-test runner"), Runner))
		{
			return true;
		}
		if (Run->Stage == 0)
		{
			Runner->Begin({TEXT("fight")}, FString(), false);
			TestTrue(TEXT("The fight scenario plays"), Runner->IsRunning());
			Run->Next();
			return false;
		}
		if (Runner->IsRunning())
		{
			if (Run->Waited() > 120.0)
			{
				AddError(TEXT("The fight scenario did not finish in 120 s"));
				return true;
			}
			return false;
		}
		TMap<FString, TSharedPtr<FJsonObject>> Results;
		for (const FString& Line : Runner->GetLogLines())
		{
			TSharedPtr<FJsonObject> Object;
			const TSharedPtr<FJsonObject>* Data = nullptr;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Object) && Object.IsValid()
				&& Object->GetStringField(TEXT("event")) == TEXT("result") && Object->TryGetObjectField(TEXT("data"), Data))
			{
				Results.Add(Object->GetStringField(TEXT("label")), *Data);
				AddInfo(Object->GetStringField(TEXT("label")) + TEXT(": ") + Line.Mid(Line.Find(TEXT("\"data\""))));
			}
		}
		auto Field = [&Results](const TCHAR* Label, const TCHAR* Key)
		{
			const TSharedPtr<FJsonObject>* Found = Results.Find(Label);
			FString Value;
			return Found && (*Found)->TryGetStringField(Key, Value) ? Value : FString();
		};
		const FString Stand = Field(TEXT("it closes in and punches; the player stands"), TEXT("their punches"));
		TestTrue(TEXT("Standing: it threw punches"), CountBefore(Stand, TEXT("thrown")) >= 1);
		TestTrue(TEXT("Standing: at least one landed"), CountBefore(Stand, TEXT("landed")) >= 1);
		const FString Block = Field(TEXT("the player holds the block"), TEXT("their punches"));
		TestTrue(TEXT("Blocking: at least one was blocked"), CountBefore(Block, TEXT("blocked")) >= 1);
		TestEqual(TEXT("Blocking: none landed"), CountBefore(Block, TEXT("landed")), 0);
		const FString Dodge = Field(TEXT("the player dodges back on the tell"), TEXT("their punches"));
		TestTrue(TEXT("Dodging: the player dodged"), FCString::Atoi(*Field(TEXT("the player dodges back on the tell"), TEXT("dodges"))) >= 1);
		TestTrue(TEXT("Dodging: at least one of its punches missed"), CountBefore(Dodge, TEXT("missed")) >= 1);
		const FString Trade = Field(TEXT("block, and punch back while it recovers"), TEXT("punches"));
		TestTrue(TEXT("Trading: the player's punches landed"), FCString::Atoi(*Trade) >= 1);
		TestEqual(TEXT("Down: it waited"), Field(TEXT("the player is down: it waits"), TEXT("it waited")), FString(TEXT("yes")));
		TestEqual(TEXT("Down: it did not hit the player"), FCString::Atof(*Field(TEXT("the player is down: it waits"), TEXT("their punches landed while down"))), 0.f);
		TestEqual(TEXT("Down: the player got back up"), Field(TEXT("the player is down: it waits"), TEXT("back up")), FString(TEXT("yes")));
		const TCHAR* NoStamina = TEXT("blocking with no stamina: the guard gives way");
		TestEqual(TEXT("No stamina: the guard was down"), Field(NoStamina, TEXT("guard was up")), FString(TEXT("no")));
		TestTrue(TEXT("No stamina: its strikes got through"), CountBefore(Field(NoStamina, TEXT("their punches")), TEXT("landed")) >= 1);
		TestEqual(TEXT("The run ended: it stopped"), Field(TEXT("the run ends: it stops"), TEXT("it stopped")), FString(TEXT("yes")));
		return true;
	}));
	EndPlay();
	RestoreStoreFile();
	return true;
}

#endif
