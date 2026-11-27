// More attacks and stamina (decision 0020)
// (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Attacks).

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
#include "CSPhysicsBodyComponent.h"
#include "CSPlayTestRig.h"
#include "CSStaminaComponent.h"
#include "CSStrikeComponent.h"
#include "CSStrikeMove.h"
#include "CSTuningRanges.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CSAttacksTests
{
	using namespace CSPlayRig;

	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	struct FStage
	{
		int32 Stage = 0;
		int32 Case = 0;
		double StageStart = 0.0;
		int32 LandedBefore = 0;
		TArray<float> Sizes;
		float Value = 0.f;

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

	bool StartOneDummyRun(FAutomationTestBase& Test, UCSRunDirector* Director, float Distance)
	{
		FCSRunSetup Setup;
		Setup.SetValue(TEXT("Dummies"), 1.f);
		Setup.SetValue(TEXT("Distance"), Distance);
		Setup.SetValue(TEXT("TimeLimit"), 0.f);
		FString Reason;
		return Test.TestTrue(TEXT("The run starts"), Director && Director->StartRun(Setup, Reason));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAttacksRulesTest, "CombatSimulator.CS.Attacks.MovesStaminaRunSetupAndStats", CSAttacksTests::TestFlags)
bool FCSAttacksRulesTest::RunTest(const FString& Parameters)
{
	// The moves, read against the straight.
	const TArray<FCSStrikeMoveData> Moves = FCSStrikeMoveData::Defaults();
	TestEqual(TEXT("One row per move"), Moves.Num(), 5);
	for (int32 Index = 0; Index < Moves.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("Row %d is its move"), Index), int32(Moves[Index].Move), Index);
	}
	const FCSStrikeMoveData& Jab = Moves[int32(ECSStrikeMove::Jab)];
	const FCSStrikeMoveData& Straight = Moves[int32(ECSStrikeMove::Straight)];
	const FCSStrikeMoveData& Hook = Moves[int32(ECSStrikeMove::HookLeft)];
	const FCSStrikeMoveData& Kick = Moves[int32(ECSStrikeMove::Kick)];
	TestTrue(TEXT("The jab is quicker, lighter and cheaper than the straight"),
		Jab.WindupScale < Straight.WindupScale && Jab.MassScale < Straight.MassScale && Jab.StaminaCost < Straight.StaminaCost);
	TestFalse(TEXT("The jab is the lead (left) hand"), Jab.bRightSide);
	TestTrue(TEXT("A hook trades reach for weight"), Hook.ReachScale < Straight.ReachScale && Hook.MassScale > Straight.MassScale && Hook.bHook);
	TestTrue(TEXT("The hooks come from both sides"), !Moves[int32(ECSStrikeMove::HookLeft)].bRightSide && Moves[int32(ECSStrikeMove::HookRight)].bRightSide);
	TestTrue(TEXT("The kick is the slow, far, heavy, costly one"), Kick.bLeg && Kick.WindupScale > Hook.WindupScale && Kick.ReachScale > Straight.ReachScale
		&& Kick.MassScale > Hook.MassScale && Kick.StaminaCost > Hook.StaminaCost);
	TestEqual(TEXT("Moves have names"), FCSStrikeMoveData::Describe(ECSStrikeMove::HookRight), FString(TEXT("right hook")));

	// The attack key's hook: moving sideways in the stance.
	ECSStrikeMove Picked = ECSStrikeMove::Jab;
	TestTrue(TEXT("Moving right in the stance: a hook"), ACSCharacter::PickHook(true, 0.9f, Picked) && Picked == ECSStrikeMove::HookRight);
	TestTrue(TEXT("Moving left: from the left"), ACSCharacter::PickHook(true, -0.8f, Picked) && Picked == ECSStrikeMove::HookLeft);
	TestFalse(TEXT("Moving forward: no hook"), ACSCharacter::PickHook(true, 0.3f, Picked));
	TestFalse(TEXT("Out of the stance: no hook"), ACSCharacter::PickHook(false, 1.f, Picked));

	// The opponent's pattern uses the moves.
	TArray<ECSStrikeMove> Used;
	for (const FCSOpponentAttack& Attack : ACSOpponentController::DefaultPattern())
	{
		TestFalse(TEXT("Every attack throws something"), Attack.Moves.IsEmpty());
		Used.Append(Attack.Moves);
	}
	for (const ECSStrikeMove Move : {ECSStrikeMove::Jab, ECSStrikeMove::Straight, ECSStrikeMove::HookLeft, ECSStrikeMove::HookRight, ECSStrikeMove::Kick})
	{
		TestTrue(FString::Printf(TEXT("The opponent throws a %s"), *FCSStrikeMoveData::Describe(Move)), Used.Contains(Move));
	}

	// The run setup: stamina on or off, and its recovery.
	FCSRunSetup Setup;
	TestTrue(TEXT("Stamina is on by default"), Setup.bStamina);
	TestEqual(TEXT("Read as a word"), Setup.DescribeValue(TEXT("Stamina")), FString(TEXT("On")));
	TestTrue(TEXT("It can be switched off"), Setup.SetValue(TEXT("Stamina"), 0.f) && !Setup.bStamina);
	TestTrue(TEXT("The description says so"), Setup.Describe().Contains(TEXT("stamina off")));
	Setup.SetValue(TEXT("Stamina"), 1.f);
	Setup.SetValue(TEXT("StaminaRecovery"), 1.6f);
	TestEqual(TEXT("Recovery is rounded to its step"), Setup.StaminaRecovery, 1.5f, 1.e-4f);
	TestTrue(TEXT("And described"), Setup.Describe().Contains(TEXT("stamina recovery 1.5 x")));
	FCSRunSetup Loaded;
	TArray<FString> Notes;
	TestTrue(TEXT("It saves and loads"), FCSRunSetup::FromJson(Setup.ToJson(), Loaded, Notes) && Loaded == Setup);

	// The stats: strikes by move, tired, out of stamina.
	FCSRunStats Stats;
	Stats.RecordMove(TEXT("jab"), false);
	Stats.RecordMove(TEXT("jab"), false);
	Stats.RecordMoveLanded(TEXT("jab"));
	Stats.RecordMove(TEXT("kick"), true);
	Stats.RecordPunch();
	Stats.RecordPunch();
	Stats.RecordPunch();
	++Stats.Exhausted;
	TestEqual(TEXT("Moves are listed landed of thrown"), Stats.DescribeMoves(), FString(TEXT("jab 1/2, kick 0/1")));
	const FString Summary = FString::Join(Stats.SummaryLines(), TEXT("\n"));
	TestTrue(TEXT("The results list them"), Summary.Contains(TEXT("by move, landed of thrown: jab 1/2, kick 0/1")));
	TestTrue(TEXT("And the tired strikes"), Summary.Contains(TEXT("1 thrown tired")));
	TestTrue(TEXT("And running out"), Summary.Contains(TEXT("Out of stamina: 1 time")));
	// Two jabs and a kick are three strikes, not three punches (#69).
	TestTrue(TEXT("The total counts strikes, kicks included"), Summary.Contains(TEXT("Strikes: 3 thrown, 0 landed")));
	TestFalse(TEXT("Nothing is called a punch"), Summary.Contains(TEXT("Punches:")));

	for (const TCHAR* Field : {TEXT("StaminaMax"), TEXT("StaminaRegen"), TEXT("StaminaRegenDelay"), TEXT("TiredStrikeScale"),
		TEXT("BlockStaminaCost"), TEXT("DodgeStaminaCost"), TEXT("GuardReturnShare")})
	{
		TestNotNull(FString::Printf(TEXT("%s has a tuning range"), Field), CSTuning::FindRow(Field));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAttacksEveryMoveLandsTest, "CombatSimulator.CS.Attacks.EveryMoveLands", CSAttacksTests::TestFlags)
bool FCSAttacksEveryMoveLandsTest::RunTest(const FString& Parameters)
{
	using namespace CSAttacksTests;
	UCSRunStore::bUseFile = false;
	StartPlay(this, PlaneMap, 1.5f);
	TSharedRef<FStage> Run = MakeShared<FStage>();
	Run->StageStart = FPlatformTime::Seconds();
	struct FCase { ECSStrikeMove Move; float Distance; };
	static const FCase Cases[] = {
		{ECSStrikeMove::Jab, 80.f}, {ECSStrikeMove::Straight, 80.f}, {ECSStrikeMove::HookLeft, 75.f},
		{ECSStrikeMove::HookRight, 75.f}, {ECSStrikeMove::Kick, 100.f},
	};

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UWorld* World = PlayWorld();
		ACSGameMode* Mode = World ? Cast<ACSGameMode>(World->GetAuthGameMode()) : nullptr;
		UCSRunDirector* Director = Mode ? Mode->GetRunDirector() : nullptr;
		if (!Director || Run->Waited() > 30.0)
		{
			AddError(FString::Printf(TEXT("Stage %d case %d did not finish"), Run->Stage, Run->Case));
			return true;
		}
		FCSPlaytestContext C;
		C.World = World;
		C.Mode = Mode;
		C.Controller = World->GetFirstPlayerController();
		ACSCharacter* Player = C.Player();
		if (Run->Stage == 0)
		{
			if (!Player)
			{
				return false;
			}
			StartOneDummyRun(*this, Director, 250.f);
			Run->Next();
			return false;
		}
		if (Run->Stage == 1)
		{
			if (Mode->IsResetPending() || !C.Dummy() || !Player || Run->Waited() < 1.2)
			{
				return false;
			}
			Run->Next();
			return false;
		}
		const FCase& Case = Cases[Run->Case];
		const FString Name = FCSStrikeMoveData::Describe(Case.Move);
		UCSStrikeComponent* Strike = Player ? Player->GetStrike() : nullptr;
		if (Run->Stage == 2)
		{
			C.PlaceAtDummy(Case.Distance, 0.f);
			Player->GetStamina()->Refill();
			Run->LandedBefore = Director->GetStats().PunchesLanded;
			Run->Next();
			return false;
		}
		if (Run->Stage == 3)
		{
			if (Run->Waited() < 0.6)
			{
				return false;
			}
			const float Before = Player->GetStamina()->GetCurrent();
			TestTrue(FString::Printf(TEXT("The %s starts"), *Name), Player->StrikeWith(Case.Move));
			TestTrue(FString::Printf(TEXT("The %s is that move"), *Name), Strike->GetMove() == Case.Move);
			TestEqual(FString::Printf(TEXT("The %s cost its stamina"), *Name), Before - Player->GetStamina()->GetCurrent(),
				Strike->GetMoveData(Case.Move).StaminaCost, 0.5f);
			if (Case.Move == ECSStrikeMove::Kick)
			{
				// Its authored clip moves the leg (decision 0025); without one the leg is simulated and steered.
				TestTrue(TEXT("The kick plays its clip, or the kicking leg is simulated"),
					Strike->IsPlayingClip() || Player->GetPhysicsBody()->IsLimbSimulated(TEXT("thigh_r")));
			}
			Run->Next();
			return false;
		}
		if (Strike && Strike->GetPhase() != ECSStrikePhase::None)
		{
			return false;
		}
		const int32 Landed = Director->GetStats().PunchesLanded - Run->LandedBefore;
		const FCSContactRecord& Hit = C.Dummy()->GetPhysicsBody()->GetLastContact();
		AddInfo(FString::Printf(TEXT("%s from %.0f cm: %s, reached %.0f cm at %.0f cm/s; hit %.0f cm/s on %s"), *Name, Case.Distance,
			Landed > 0 ? TEXT("landed") : TEXT("MISSED"), Strike->GetPeakReach(), Strike->GetPeakSpeed(), Hit.VelocityChange, *Hit.Bone.ToString()));
		TestEqual(FString::Printf(TEXT("The %s landed"), *Name), Landed, 1);
		Run->Sizes.Add(Landed > 0 ? Hit.VelocityChange : 0.f);
		if (Case.Move == ECSStrikeMove::Kick)
		{
			TestFalse(TEXT("The leg is back on the animation after the kick"), Player->GetPhysicsBody()->IsLimbSimulated(TEXT("thigh_r")));
		}
		if (++Run->Case >= UE_ARRAY_COUNT(Cases))
		{
			// The jab is the lightest hit and the kick the heaviest.
			TestTrue(TEXT("The jab hits lighter than the straight"), Run->Sizes[0] < Run->Sizes[1]);
			TestTrue(TEXT("The kick hits hardest"), Run->Sizes[4] > Run->Sizes[1]);
			return true;
		}
		Run->Stage = 2;
		Run->StageStart = FPlatformTime::Seconds();
		return false;
	}));
	EndPlay();
	RestoreStoreFile();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAttacksStaminaTest, "CombatSimulator.CS.Attacks.StaminaRunsOutAndComesBack", CSAttacksTests::TestFlags)
bool FCSAttacksStaminaTest::RunTest(const FString& Parameters)
{
	using namespace CSAttacksTests;
	StartPlay(this, PlaneMap, 1.5f);
	TSharedRef<FStage> Run = MakeShared<FStage>();
	Run->StageStart = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UWorld* World = PlayWorld();
		ACSCharacter* Player = PlayerCharacter(World);
		UCSStaminaComponent* Stamina = Player ? Player->GetStamina() : nullptr;
		UCSStrikeComponent* Strike = Player ? Player->GetStrike() : nullptr;
		UCSGuardComponent* Guard = Player ? Player->GetGuard() : nullptr;
		if (!Stamina || !Strike || !Guard || Run->Waited() > 20.0)
		{
			AddError(FString::Printf(TEXT("Stage %d did not finish"), Run->Stage));
			return true;
		}
		const UCSMovementTuning& T = Player->GetActiveTuning();
		switch (Run->Stage)
		{
		case 0:
		{
			TestEqual(TEXT("Stamina starts full"), Stamina->GetShare(), 1.f, 1.e-3f);
			// A full straight at the air, to compare with a tired one.
			TestTrue(TEXT("A straight starts"), Player->StrikeWith(ECSStrikeMove::Straight));
			TestFalse(TEXT("With enough stamina it is not tired"), Strike->WasTired());
			Run->Next();
			return false;
		}
		case 1:
		{
			if (Strike->GetPhase() != ECSStrikePhase::None)
			{
				return false;
			}
			Run->Value = Strike->GetWindupSeconds();
			Stamina->SetCurrent(1.f);
			TestFalse(TEXT("With 1 left it is not exhausted yet"), Stamina->IsExhausted());
			TestTrue(TEXT("A straight with 1 left starts"), Player->StrikeWith(ECSStrikeMove::Straight));
			TestTrue(TEXT("And is thrown tired"), Strike->WasTired());
			TestTrue(TEXT("It took stamina to zero"), Stamina->GetCurrent() <= 0.f && Stamina->IsExhausted());
			Run->Next();
			return false;
		}
		case 2:
		{
			if (Strike->GetPhase() != ECSStrikePhase::None)
			{
				return false;
			}
			AddInfo(FString::Printf(TEXT("A full straight winds up in %.2f s, a tired one in %.2f s"), Run->Value, Strike->GetWindupSeconds()));
			TestTrue(TEXT("The tired straight winds up slower"), Strike->GetWindupSeconds() > Run->Value * 1.3f);
			Guard->SetBlockHeld(true);
			TestFalse(TEXT("Exhausted, the guard does not come up"), Guard->IsGuardUp());
			TestFalse(TEXT("Nor can it dodge"), Player->DodgeStep());
			Run->Next();
			return false;
		}
		case 3:
		{
			// Recovery: nothing for StaminaRegenDelay, then StaminaRegen a second.
			if (Run->Waited() < T.StaminaRegenDelay + (T.GuardReturnShare * T.StaminaMax) / FMath::Max(T.StaminaRegen, 1.f) + 0.5)
			{
				return false;
			}
			AddInfo(FString::Printf(TEXT("Stamina after the rest: %.0f of %.0f"), Stamina->GetCurrent(), Stamina->GetMax()));
			TestTrue(TEXT("It came back"), Stamina->GetCurrent() >= T.GuardReturnShare * T.StaminaMax);
			TestFalse(TEXT("No longer exhausted"), Stamina->IsExhausted());
			TestTrue(TEXT("The guard is up again"), Guard->IsGuardUp());
			Guard->SetBlockHeld(false);
			// Off (a run without stamina): nothing is spent.
			Stamina->SetEnabled(false);
			Player->StrikeWith(ECSStrikeMove::Kick);
			TestEqual(TEXT("With stamina off a kick costs nothing"), Stamina->GetShare(), 1.f, 1.e-3f);
			TestFalse(TEXT("And is not tired"), Strike->WasTired());
			Stamina->SetEnabled(true);
			return true;
		}
		default:
			return true;
		}
	}));
	EndPlay();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAttacksPlaytestTest, "CombatSimulator.CS.Attacks.PlaytestMovesAndStaminaRun", CSAttacksTests::TestFlags)
bool FCSAttacksPlaytestTest::RunTest(const FString& Parameters)
{
	using namespace CSAttacksTests;
	UCSRunStore::bUseFile = false;
	StartPlay(this, PlaneMap, 1.f);
	TSharedRef<FStage> Run = MakeShared<FStage>();
	Run->StageStart = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UWorld* World = PlayWorld();
		ACSGameMode* Mode = World ? Cast<ACSGameMode>(World->GetAuthGameMode()) : nullptr;
		UCSPlaytestRunner* Runner = Mode ? Mode->GetPlaytestRunner() : nullptr;
		if (!TestNotNull(TEXT("The play-test runner"), Runner))
		{
			return true;
		}
		if (Run->Stage == 0)
		{
			Runner->Begin({TEXT("moves"), TEXT("stamina")}, FString(), false);
			Run->Next();
			return false;
		}
		if (Runner->IsRunning())
		{
			if (Run->Waited() > 90.0)
			{
				AddError(TEXT("The scenarios did not finish in 90 s"));
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
		for (const TCHAR* Label : {TEXT("jab from 80 cm"), TEXT("straight from 80 cm"), TEXT("left hook from 75 cm"),
			TEXT("right hook from 75 cm"), TEXT("kick from 100 cm"), TEXT("kick from 150 cm")})
		{
			TestEqual(FString(Label) + TEXT(": landed"), Field(Label, TEXT("punches")), FString(TEXT("1 of 1 landed")));
		}
		TestEqual(TEXT("The attack key throws a one-two, then a hook moving right"),
			Field(TEXT("the attack key: a one-two, then a hook moving right"), TEXT("thrown")), FString(TEXT("jab, straight, right hook")));
		const TCHAR* Drain = TEXT("straights as fast as they go: stamina runs out");
		TestTrue(TEXT("Straights as fast as they go: some were thrown tired"), FCString::Atof(*Field(Drain, TEXT("thrown tired"))) >= 1.f);
		TestTrue(TEXT("And the tired ones were slower"),
			FCString::Atof(*Field(Drain, TEXT("last two fist speed"))) < FCString::Atof(*Field(Drain, TEXT("first two fist speed"))));
		TestEqual(TEXT("Out of stamina the guard does not come up"), Field(TEXT("out of stamina: the guard will not come up"), TEXT("guard up")), FString(TEXT("no")));
		const TCHAR* Rest = TEXT("rest: stamina comes back");
		TestTrue(TEXT("Resting brings stamina back"),
			FCString::Atof(*Field(Rest, TEXT("stamina share after"))) > FCString::Atof(*Field(Rest, TEXT("stamina share before"))) + 0.3f);
		return true;
	}));
	EndPlay();
	RestoreStoreFile();
	return true;
}

#endif
