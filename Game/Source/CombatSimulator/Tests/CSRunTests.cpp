// Simulation runs: the setup as data, the statistics, and a run played on the real maps
// (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Run).
//
// The play tests turn UCSRunStore::bUseFile off before the game starts and back on at the end, so the
// player's saved run setup is never read or written by an automation run.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "../CSGameMode.h"
#include "../CSPlayerController.h"
#include "../CSTargetDummy.h"
#include "../Run/CSRunDirector.h"
#include "../Run/CSRunSetup.h"
#include "../Run/CSRunStats.h"
#include "../Run/CSRunStore.h"
#include "../UI/SCSChoiceRow.h"
#include "CSBallLauncher.h"
#include "CSDevTools.h"
#include "CSHitBall.h"
#include "CSPhysicsBodyComponent.h"
#include "CSPlayTestRig.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace CSRunTests
{
	using namespace CSPlayRig;

	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const FName Dummies(TEXT("Dummies"));
	const FName Formation(TEXT("Formation"));
	const FName Distance(TEXT("Distance"));
	const FName Spacing(TEXT("Spacing"));
	const FName Launchers(TEXT("Launchers"));
	const FName TimeLimit(TEXT("TimeLimit"));
	const FName KnockdownGoal(TEXT("KnockdownGoal"));

	FCSContactRecord MakeHit(FName Bone, float Size, ECSContactResult Result)
	{
		FCSContactRecord Hit;
		Hit.Time = 1.0;
		Hit.Bone = Bone;
		Hit.VelocityChange = Size;
		Hit.Result = Result;
		return Hit;
	}

	TArray<ACSTargetDummy*> LiveDummies(UWorld* World)
	{
		TArray<ACSTargetDummy*> Out;
		for (TActorIterator<ACSTargetDummy> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed())
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	int32 CountTagged(UWorld* World, FName Tag)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Count += !It->IsActorBeingDestroyed() && It->ActorHasTag(Tag) ? 1 : 0;
		}
		return Count;
	}

	/**
	 * A hit on Victim that does not depend on a ball finding its way there: a ball is made far above
	 * the map (owned by Thrower, so the director counts the throw and whose hit it is), the hit is
	 * handed to the victim's body directly, and the ball is removed before it can fall on anyone.
	 */
	ECSContactResult HitWithBall(UWorld* World, AActor* Thrower, ACSCharacter* Victim, float Mass, float Speed)
	{
		static int32 Serial = 0;
		ACSHitBall* Ball = ACSHitBall::Throw(World, Thrower, FVector(3000.f + 150.f * float(Serial++ % 20), 3000.f, 6000.f), FVector::UpVector, Mass, 10.f);
		if (!Ball || !Victim || !Victim->GetPhysicsBody())
		{
			return ECSContactResult::None;
		}
		const FVector At = Victim->GetActorLocation() + FVector(0.f, 0.f, 30.f);
		const FVector Along = Victim->GetActorForwardVector() * -1.f;
		const ECSContactResult Result = Victim->GetPhysicsBody()->ReceiveContact(Ball, NAME_None, Along * Speed, Mass, At - Along * 100.f, At);
		Ball->Destroy();
		return Result;
	}

	/** State of a play test, kept across frames. */
	struct FPlayRun
	{
		int32 Stage = 0;
		double StageStart = 0.0;
		FCSRunSetup Setup;
		FVector MapDummyHome = FVector::ZeroVector;
		int32 MapDummies = 0;
		TWeakObjectPtr<AActor> OldPawn;
		int32 PlayerBalls = 0;

		void Next()
		{
			++Stage;
			StageStart = FPlatformTime::Seconds();
		}
		double Waited() const { return FPlatformTime::Seconds() - StageStart; }
	};

	/** The reset a run start or leave asked for has finished, and the new characters have had time to start their bodies. */
	bool IsSettled(const ACSGameMode* Mode, const ACSCharacter* Player, const FPlayRun& Run, double Seconds = 1.0)
	{
		return Mode && Player && Player != Run.OldPawn.Get() && !Mode->IsResetPending() && Run.Waited() > Seconds;
	}

	void RestoreStoreFile()
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
		{
			UCSRunStore::bUseFile = true;
			return true;
		}));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSRunSetupRowsTest, "CombatSimulator.CS.Run.SetupRowsAndClamping", CSRunTests::TestFlags)
bool FCSRunSetupRowsTest::RunTest(const FString& Parameters)
{
	using namespace CSRunTests;
	const FCSRunSetup Fresh;
	TSet<FName> Keys;
	for (const FCSRunRow& Row : FCSRunSetup::Rows())
	{
		const FString Name = Row.Key.ToString();
		TestFalse(Name + TEXT(": the key is used once"), Keys.Contains(Row.Key));
		Keys.Add(Row.Key);
		TestFalse(Name + TEXT(" has a label"), Row.Label.IsEmpty());
		TestFalse(Name + TEXT(" has a help line"), Row.Help.IsEmpty());
		TestTrue(Name + TEXT(": the range is not empty"), Row.Max > Row.Min && Row.Step > 0.f);
		TestTrue(Name + TEXT(": the default is in range"), Row.Default >= Row.Min && Row.Default <= Row.Max);
		TestEqual(Name + TEXT(": a fresh setup holds the default"), Fresh.GetValue(Row.Key), Row.Default);
		TestFalse(Name + TEXT(": a fresh setup is unchanged"), Fresh.IsChanged(Row.Key));
		TestFalse(Name + TEXT(" reads as text"), Fresh.DescribeValue(Row.Key).IsEmpty());
		if (!Row.Choices.IsEmpty())
		{
			TestEqual(Name + TEXT(": a choice's range is its choices"), Row.Max, float(Row.Choices.Num() - 1));
		}

		FCSRunSetup Setup;
		TestTrue(Name + TEXT(": too high is taken"), Setup.SetValue(Row.Key, Row.Max + 1000.f));
		TestEqual(Name + TEXT(": and clamped to the maximum"), Setup.GetValue(Row.Key), Row.Max);
		Setup.SetValue(Row.Key, Row.Min - 1000.f);
		TestEqual(Name + TEXT(": too low is clamped to the minimum"), Setup.GetValue(Row.Key), Row.Min);
		TestFalse(Name + TEXT(": not a number is refused"), Setup.SetValue(Row.Key, NAN));
		TestEqual(Name + TEXT(": and changes nothing"), Setup.GetValue(Row.Key), Row.Min);
	}
	FCSRunSetup Setup;
	TestFalse(TEXT("An unknown key is refused"), Setup.SetValue(TEXT("Elephants"), 3.f));
	TestTrue(TEXT("An unknown key changes nothing"), Setup == Fresh);
	Setup.SetValue(Distance, 512.f);
	TestEqual(TEXT("A value is rounded to its step"), Setup.Distance, 500.f);
	Setup.SetValue(Dummies, 2.6f);
	TestEqual(TEXT("A count is a whole number"), Setup.Dummies, 3);
	TestTrue(TEXT("Dummies is a field of the struct"), FCSRunSetup::FindRow(Dummies) != nullptr && Setup.GetValue(Dummies) == float(Setup.Dummies));

	Setup = FCSRunSetup();
	Setup.Map = TEXT("L_Plane");
	Setup.SetValue(KnockdownGoal, 2.f);
	Setup.SetValue(Launchers, 1.f);
	TestEqual(TEXT("A setup reads as one line"), Setup.Describe(), FString(TEXT("L_Plane, 3 dummies in a line at 500 cm, 1 launcher, 60 s, ends at 2 knockdowns")));
	Setup.SetValue(TimeLimit, 0.f);
	Setup.SetValue(Dummies, 0.f);
	Setup.SetValue(KnockdownGoal, 0.f);
	Setup.SetValue(Launchers, 0.f);
	TestEqual(TEXT("An empty, endless setup reads as one line"), Setup.Describe(), FString(TEXT("L_Plane, no dummies, no time limit")));
	TestEqual(TEXT("No time limit reads as none"), Setup.DescribeValue(TimeLimit), FString(TEXT("none")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSRunSetupJsonTest, "CombatSimulator.CS.Run.SetupAsText", CSRunTests::TestFlags)
bool FCSRunSetupJsonTest::RunTest(const FString& Parameters)
{
	using namespace CSRunTests;
	FCSRunSetup Setup;
	Setup.Map = TEXT("L_MovementGym");
	Setup.SetValue(Dummies, 5.f);
	Setup.SetValue(Formation, 1.f);
	Setup.SetValue(Distance, 850.f);
	Setup.SetValue(Spacing, 325.f);
	Setup.SetValue(Launchers, 2.f);
	Setup.SetValue(TimeLimit, 90.f);
	Setup.SetValue(KnockdownGoal, 4.f);

	FCSRunSetup Back;
	TArray<FString> Notes;
	TestTrue(TEXT("Its own text reads back"), FCSRunSetup::FromJson(Setup.ToJson(), Back, Notes));
	TestTrue(TEXT("And is the same setup"), Back == Setup);
	TestEqual(TEXT("With nothing to remark"), Notes.Num(), 0);
	TestTrue(TEXT("The ring came through"), Back.Formation == ECSRunFormation::Ring);

	// What it can take, it takes.
	Notes.Reset();
	TestTrue(TEXT("A partial file is read"), FCSRunSetup::FromJson(TEXT("{\"version\":1,\"values\":{\"Dummies\":99,\"Elephants\":2,\"Distance\":\"far\"}}"), Back, Notes));
	TestEqual(TEXT("Out of range is clamped"), Back.Dummies, FCSRunSetup::MaxDummies);
	TestEqual(TEXT("A missing field has its default"), Back.TimeLimit, 60.f);
	TestEqual(TEXT("Text for a number leaves the default"), Back.Distance, 500.f);
	TestTrue(TEXT("No map means the map being played"), Back.Map.IsEmpty());
	TestEqual(TEXT("Each of the three problems is remarked on"), Notes.Num(), 3);

	// What it cannot read at all, it refuses and leaves the caller's setup alone.
	Back = Setup;
	Notes.Reset();
	TestFalse(TEXT("Not JSON is refused"), FCSRunSetup::FromJson(TEXT("dummies = 3"), Back, Notes));
	TestFalse(TEXT("No version is refused"), FCSRunSetup::FromJson(TEXT("{\"values\":{\"Dummies\":1}}"), Back, Notes));
	TestFalse(TEXT("A newer version is refused"), FCSRunSetup::FromJson(TEXT("{\"version\":999,\"values\":{\"Dummies\":1}}"), Back, Notes));
	TestTrue(TEXT("A refused file changes nothing"), Back == Setup);
	TestEqual(TEXT("Each refusal says why"), Notes.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSRunSetupSpotsTest, "CombatSimulator.CS.Run.DummyAndLauncherSpots", CSRunTests::TestFlags)
bool FCSRunSetupSpotsTest::RunTest(const FString& Parameters)
{
	using namespace CSRunTests;
	// A start that faces +Y, off the origin, so a mix-up of axes shows.
	const FVector Origin(1000.f, -400.f, 90.f);
	const FTransform Start(FRotator(0.f, 90.f, 0.f), Origin);
	auto FacesStart = [&Origin](const FTransform& Spot)
	{
		const FVector ToStart = (Origin - Spot.GetLocation()).GetSafeNormal2D();
		return FVector::DotProduct(Spot.GetRotation().GetForwardVector(), ToStart) > 0.999f;
	};

	FCSRunSetup Setup;
	Setup.SetValue(Dummies, 3.f);
	Setup.SetValue(Distance, 600.f);
	Setup.SetValue(Spacing, 200.f);
	TArray<FTransform> Line = Setup.DummySpots(Start);
	if (TestEqual(TEXT("A line of three"), Line.Num(), 3))
	{
		TestTrue(TEXT("The middle one is straight ahead"), Line[1].GetLocation().Equals(Origin + FVector(0.f, 600.f, 0.f), 0.5f));
		TestTrue(TEXT("The row is across the view"), FMath::IsNearlyEqual(float(Line[0].GetLocation().Y), float(Line[2].GetLocation().Y), 0.5f));
		TestTrue(TEXT("Neighbours are the spacing apart"), FMath::IsNearlyEqual(float(FVector::Dist(Line[0].GetLocation(), Line[1].GetLocation())), 200.f, 0.5f));
		TestTrue(TEXT("The row is centred on the view"), FMath::IsNearlyEqual(float(Line[0].GetLocation().X + Line[2].GetLocation().X), 2.f * float(Origin.X), 0.5f));
		for (const FTransform& Spot : Line)
		{
			TestTrue(TEXT("A dummy in the line faces the start"), FacesStart(Spot));
			TestEqual(TEXT("And is at the start's height"), float(Spot.GetLocation().Z), float(Origin.Z));
		}
	}

	Setup.SetValue(Formation, 1.f);
	Setup.SetValue(Dummies, 4.f);
	TArray<FTransform> Ring = Setup.DummySpots(Start);
	if (TestEqual(TEXT("A ring of four"), Ring.Num(), 4))
	{
		TestTrue(TEXT("The first is straight ahead"), Ring[0].GetLocation().Equals(Origin + FVector(0.f, 600.f, 0.f), 0.5f));
		for (const FTransform& Spot : Ring)
		{
			TestTrue(TEXT("Every one is the distance away"), FMath::IsNearlyEqual(float(FVector::Dist2D(Spot.GetLocation(), Origin)), 600.f, 0.5f));
			TestTrue(TEXT("And faces the start"), FacesStart(Spot));
		}
		TestTrue(TEXT("They are spread evenly"), FMath::IsNearlyEqual(float(FVector::Dist(Ring[0].GetLocation(), Ring[2].GetLocation())), 1200.f, 0.5f));
	}
	Setup.SetValue(Dummies, 0.f);
	TestEqual(TEXT("No dummies, no spots"), Setup.DummySpots(Start).Num(), 0);

	Setup.SetValue(Launchers, float(FCSRunSetup::MaxLaunchers));
	TArray<FTransform> Around = Setup.LauncherSpots(Start, 800.f);
	if (TestEqual(TEXT("Four launchers"), Around.Num(), FCSRunSetup::MaxLaunchers))
	{
		TestTrue(TEXT("The first is behind the start"), Around[0].GetLocation().Equals(Origin - FVector(0.f, 800.f, 0.f), 0.5f));
		TestTrue(TEXT("The last is ahead of it"), Around[3].GetLocation().Equals(Origin + FVector(0.f, 800.f, 0.f), 0.5f));
		for (const FTransform& Spot : Around)
		{
			// A launcher fires along its forward axis at a square StandDistance ahead: the start.
			const FVector Square = Spot.GetLocation() + Spot.GetRotation().GetForwardVector() * 800.f;
			TestTrue(TEXT("A launcher's square is the start"), FVector::Dist2D(Square, Origin) < 1.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSRunStatsTest, "CombatSimulator.CS.Run.Statistics", CSRunTests::TestFlags)
bool FCSRunStatsTest::RunTest(const FString& Parameters)
{
	using namespace CSRunTests;
	FCSRunStats Stats;
	TestEqual(TEXT("Nothing thrown: accuracy 0"), Stats.Accuracy(), 0.f);
	TestEqual(TEXT("Nothing hit: average 0"), Stats.Dealt.Average(), 0.f);
	TestTrue(TEXT("Nothing hit: no body parts"), Stats.Dealt.DescribeBones().IsEmpty());

	for (int32 Index = 0; Index < 4; ++Index)
	{
		Stats.RecordThrow();
	}
	// Two with a ball and one with the body; a contact that was no hit counts for nothing.
	Stats.RecordHit(ECSRunHitSide::Dealt, MakeHit(TEXT("spine_03"), 200.f, ECSContactResult::Stagger), true);
	Stats.RecordHit(ECSRunHitSide::Dealt, MakeHit(TEXT("head"), 900.f, ECSContactResult::Ragdoll), true);
	Stats.RecordHit(ECSRunHitSide::Dealt, MakeHit(TEXT("spine_03"), 100.f, ECSContactResult::Ignored), false);
	Stats.RecordHit(ECSRunHitSide::Dealt, MakeHit(TEXT("head"), 500.f, ECSContactResult::None), true);
	Stats.RecordHit(ECSRunHitSide::Taken, MakeHit(TEXT("upperarm_l"), 300.f, ECSContactResult::Stagger));
	Stats.RecordHit(ECSRunHitSide::Other, MakeHit(TEXT("pelvis"), 300.f, ECSContactResult::Stagger));

	TestEqual(TEXT("A contact that was not a hit is not counted"), Stats.Dealt.Hits, 3);
	TestEqual(TEXT("Staggers dealt"), Stats.Dealt.Staggers, 1);
	TestEqual(TEXT("Knockdowns dealt"), Stats.Dealt.Knockdowns, 1);
	TestEqual(TEXT("Hits with no effect"), Stats.Dealt.NoEffect, 1);
	TestEqual(TEXT("Biggest hit dealt"), Stats.Dealt.Biggest, 900.f);
	TestEqual(TEXT("Average hit dealt"), Stats.Dealt.Average(), 400.f);
	TestEqual(TEXT("Body parts, most hit first"), Stats.Dealt.DescribeBones(), FString(TEXT("spine_03 2, head 1")));
	TestEqual(TEXT("Only the first few body parts are named"), Stats.Dealt.DescribeBones(1), FString(TEXT("spine_03 2, and 1 more")));
	TestEqual(TEXT("Hits taken"), Stats.Taken.Hits, 1);
	TestEqual(TEXT("Hits between others"), Stats.OtherHits, 1);
	TestEqual(TEXT("Ball hits are counted apart from hits with the body"), Stats.BallHits, 2);
	TestEqual(TEXT("Two of four balls hit; the body hit is not accuracy"), Stats.Accuracy(), 0.5f);

	Stats.Seconds = 65.4f;
	TestEqual(TEXT("Time reads as minutes and seconds"), FCSRunStats::FormatTime(Stats.Seconds), FString(TEXT("1:05")));
	TestEqual(TEXT("The line on screen, with a limit and a goal"), Stats.HudLine(120.f, 3), FString(TEXT("Run 1:05 / 2:00   hits 3   knockdowns 1 / 3   taken 1")));
	TestEqual(TEXT("The line on screen, with neither"), Stats.HudLine(0.f, 0), FString(TEXT("Run 1:05   hits 3   knockdowns 1   taken 1")));

	const FString Summary = FString::Join(Stats.SummaryLines(), TEXT("\n"));
	TestTrue(TEXT("The results give the time"), Summary.Contains(TEXT("Time: 1:05")));
	TestTrue(TEXT("The results give the accuracy"), Summary.Contains(TEXT("Accuracy: 50%")));
	TestTrue(TEXT("The results count what was dealt"), Summary.Contains(TEXT("Hits dealt: 3 (1 staggers, 1 knockdowns, 1 with no effect)")));
	TestTrue(TEXT("The results count what was taken"), Summary.Contains(TEXT("Hits taken: 1 (1 staggers, 0 knockdowns, 0 with no effect)")));
	TestTrue(TEXT("The results name where the hits landed"), Summary.Contains(TEXT("where: spine_03 2, head 1")));
	for (const ECSRunEnd End : {ECSRunEnd::Manual, ECSRunEnd::TimeLimit, ECSRunEnd::KnockdownGoal})
	{
		TestFalse(TEXT("Every way a run ends has words"), FCSRunStats::DescribeEnd(End).IsEmpty());
	}

	TestEqual(TEXT("Next of the last choice is the first"), SCSChoiceRow::Step(2, 1, 3), 0);
	TestEqual(TEXT("Previous of the first choice is the last"), SCSChoiceRow::Step(0, -1, 3), 2);
	TestEqual(TEXT("No choices: index 0"), SCSChoiceRow::Step(5, 1, 0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSRunPlayTest, "CombatSimulator.CS.Run.StartCountEndRerunLeave", CSRunTests::TestFlags)
bool FCSRunPlayTest::RunTest(const FString& Parameters)
{
	using namespace CSRunTests;
	UCSRunStore::bUseFile = false;
	StartPlay(this, PlaneMap, 1.5f);
	TSharedRef<FPlayRun> Run = MakeShared<FPlayRun>();
	Run->StageStart = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UWorld* World = PlayWorld();
		ACSGameMode* Mode = World ? Cast<ACSGameMode>(World->GetAuthGameMode()) : nullptr;
		UCSRunDirector* Director = Mode ? Mode->GetRunDirector() : nullptr;
		ACSPlayerController* PC = World ? Cast<ACSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		ACSCharacter* Player = PlayerCharacter(World);
		if (!World || !Mode || !PC || !TestNotNull(TEXT("The game mode has a run director"), Director))
		{
			AddError(TEXT("No play world, ACSGameMode, ACSPlayerController or run director"));
			return true;
		}
		if (Run->Waited() > 30.0)
		{
			AddError(FString::Printf(TEXT("Stage %d did not finish in 30 s (reset pending %d, player %s)"), Run->Stage, int32(Mode->IsResetPending()), *GetNameSafe(Player)));
			return true;
		}
		const FCSRunStats& Stats = Director->GetStats();

		if (Run->Stage == 0)
		{
			// Free play, then a run of three dummies and two launchers that ends at the first knockdown.
			if (!TestNotNull(TEXT("Player character"), Player))
			{
				return true;
			}
			TestTrue(TEXT("Play starts with no run"), Director->GetState() == ECSRunState::Idle);
			TestTrue(TEXT("The plane is a run map"), Mode->GetRunMapNames().Contains(TEXT("L_Plane")));
			TestEqual(TEXT("A map that is not listed has no package"), Mode->FindRunMap(TEXT("L_Nowhere")), FString());
			const TArray<ACSTargetDummy*> Before = LiveDummies(World);
			Run->MapDummies = Before.Num();
			if (!TestTrue(TEXT("The map has a dummy of its own"), Before.Num() > 0))
			{
				return true;
			}
			Run->MapDummyHome = Before[0]->GetActorLocation();

			FCSRunSetup Unknown;
			Unknown.Map = TEXT("L_Nowhere");
			FString Reason;
			TestFalse(TEXT("A run on a map the game does not have is refused"), Director->StartRun(Unknown, Reason));
			TestTrue(TEXT("And the refusal names the map"), Reason.Contains(TEXT("L_Nowhere")));
			TestTrue(TEXT("And nothing started"), Director->GetState() == ECSRunState::Idle);

			Run->Setup.SetValue(Dummies, 3.f);
			Run->Setup.SetValue(Distance, 600.f);
			Run->Setup.SetValue(Spacing, 250.f);
			Run->Setup.SetValue(Launchers, 2.f);
			Run->Setup.SetValue(TimeLimit, 0.f);
			Run->Setup.SetValue(KnockdownGoal, 1.f);
			Run->OldPawn = Player;
			TestTrue(TEXT("The run starts"), Director->StartRun(Run->Setup, Reason));
			TestTrue(TEXT("It is on at once"), Director->GetState() == ECSRunState::Running);
			TestEqual(TEXT("It took the name of the map it is on"), Director->GetSetup().Map, FString(TEXT("L_Plane")));
			Run->Next();
			return false;
		}
		if (Run->Stage == 1)
		{
			if (!IsSettled(Mode, Player, *Run))
			{
				return false;
			}
			// What the setup asked for is in the world.
			const TArray<ACSTargetDummy*> Now = LiveDummies(World);
			if (!TestEqual(TEXT("The run has its three dummies, and the map's is gone"), Now.Num(), 3))
			{
				return true;
			}
			const TArray<FTransform> Spots = Run->Setup.DummySpots(Mode->GetRunStart());
			for (ACSTargetDummy* Dummy : Now)
			{
				TestTrue(TEXT("A run dummy is tagged RunDummy"), Dummy->ActorHasTag(TEXT("RunDummy")));
				TestTrue(TEXT("And Dummy, like a map's"), Dummy->ActorHasTag(TEXT("Dummy")));
				TestTrue(TEXT("It stands on the floor"), Dummy->GetCharacterMovement()->IsMovingOnGround());
				TestFalse(TEXT("It is standing"), Dummy->IsRagdolled());
				const bool bOnSpot = Spots.ContainsByPredicate([Dummy](const FTransform& Spot) { return FVector::Dist2D(Spot.GetLocation(), Dummy->GetActorLocation()) < 30.f; });
				TestTrue(TEXT("It is on one of the setup's spots"), bOnSpot);
			}
			TestEqual(TEXT("The run has its two launchers"), CountTagged(World, TEXT("RunLauncher")), 2);
			TestTrue(TEXT("The player is back at the start"), FVector::Dist2D(Player->GetActorLocation(), Mode->GetRunStart().GetLocation()) < 100.f);
			TestTrue(TEXT("Run time is passing"), Stats.Seconds > 0.f);
			TestEqual(TEXT("Nothing is counted yet"), Stats.Dealt.Hits + Stats.Taken.Hits + Stats.OtherHits + Stats.BallsThrown, 0);
			TestTrue(TEXT("The status line says a run is on"), Director->GetStatusText().StartsWith(TEXT("Run on: L_Plane, 3 dummies")));

			// Whose hit is whose.
			TestTrue(TEXT("A hit on the player is taken"), Director->Classify(Player, Now[0]) == ECSRunHitSide::Taken);
			TestTrue(TEXT("The player running into a dummy is dealt"), Director->Classify(Now[0], Player) == ECSRunHitSide::Dealt);
			TestTrue(TEXT("One dummy on another is neither"), Director->Classify(Now[0], Now[1]) == ECSRunHitSide::Other);

			// A ball stays the player's after the character that threw it is gone (its Owner is cleared then).
			ACSHitBall* Orphan = ACSHitBall::Throw(World, Player, FVector(-3000.f, 3000.f, 6000.f), FVector::UpVector, 1.f, 10.f);
			++Run->PlayerBalls;
			if (TestNotNull(TEXT("A ball thrown by the player"), Orphan))
			{
				TestTrue(TEXT("It is known as the player's"), Director->IsPlayerBall(Orphan));
				Orphan->SetOwner(nullptr);
				TestTrue(TEXT("Without an owner its hit is still dealt"), Director->Classify(Now[0], Orphan) == ECSRunHitSide::Dealt);
				Orphan->Destroy();
			}

			const ECSContactResult Light = HitWithBall(World, Player, Now[0], 8.f, 2000.f);
			++Run->PlayerBalls;
			TestTrue(TEXT("A light ball staggers a dummy"), Light == ECSContactResult::Stagger);
			TestEqual(TEXT("The player's ball on a dummy is a hit dealt"), Stats.Dealt.Hits, 1);
			TestEqual(TEXT("And a ball hit"), Stats.BallHits, 1);
			TestEqual(TEXT("And a stagger"), Stats.Dealt.Staggers, 1);
			TestTrue(TEXT("Its size and body part are kept"), Stats.Dealt.Biggest > 0.f && Stats.Dealt.ByBone.Num() == 1);

			TestTrue(TEXT("A ball nobody threw hits the player"), HitWithBall(World, nullptr, Player, 8.f, 2000.f) != ECSContactResult::None);
			TestEqual(TEXT("That is a hit taken"), Stats.Taken.Hits, 1);
			TestTrue(TEXT("A ball nobody threw hits a dummy"), HitWithBall(World, nullptr, Now[1], 8.f, 2000.f) != ECSContactResult::None);
			TestEqual(TEXT("That is a hit between others"), Stats.OtherHits, 1);
			TestEqual(TEXT("Neither is the player's"), Stats.Dealt.Hits, 1);
			TestEqual(TEXT("Only the player's throws are counted"), Stats.BallsThrown, Run->PlayerBalls);

			const ECSContactResult Heavy = HitWithBall(World, Player, Now[2], 400.f, 3000.f);
			++Run->PlayerBalls;
			TestTrue(TEXT("A heavy ball knocks a dummy down"), Heavy == ECSContactResult::Ragdoll);
			TestEqual(TEXT("A knockdown dealt"), Stats.Dealt.Knockdowns, 1);
			Run->Next();
			return false;
		}
		if (Run->Stage == 2)
		{
			// The goal was one knockdown: the director ends the run on its next tick, and the results
			// open a moment later (ResultsDelaySeconds).
			if (Director->GetState() != ECSRunState::Ended || !PC->IsPauseScreenOpen())
			{
				TestFalse(TEXT("The results do not open the moment the run ends"), Director->GetState() == ECSRunState::Ended
					&& !Director->AreResultsPending() && !PC->IsPauseScreenOpen() && Run->Waited() < Director->ResultsDelaySeconds * 0.5);
				return false;
			}
			TestTrue(TEXT("The run ended at its knockdown goal"), Director->GetEndReason() == ECSRunEnd::KnockdownGoal);
			TestTrue(TEXT("The pause screen opened"), PC->IsPauseScreenOpen());
			TestTrue(TEXT("On the Run tab"), PC->GetRequestedTab().IsSet() && PC->GetRequestedTab().GetValue() == ECSPauseTab::Run);
			TestTrue(TEXT("The game is paused"), World->IsPaused());
			TestTrue(TEXT("The status line says how it ended"), Director->GetStatusText().StartsWith(TEXT("Run over (knockdown goal reached)")));
			PC->SetPauseScreenOpen(false);

			const TArray<ACSTargetDummy*> Now = LiveDummies(World);
			if (Now.Num() > 0 && Player)
			{
				HitWithBall(World, Player, Now[0], 8.f, 2000.f);
				TestEqual(TEXT("A hit after the end is not counted"), Stats.Dealt.Hits, 2);
				TestEqual(TEXT("Nor is the throw"), Stats.BallsThrown, Run->PlayerBalls);
			}
			Director->EndRun(ECSRunEnd::Manual);
			TestTrue(TEXT("Ending a run that is over changes nothing"), Director->GetEndReason() == ECSRunEnd::KnockdownGoal);

			// Backspace, with a run over: the same run again.
			Run->OldPawn = Player;
			TestTrue(TEXT("The reset key is accepted"), Player && Player->GetDevTools()->RequestReset());
			TestTrue(TEXT("It started the run again"), Director->GetState() == ECSRunState::Running);
			TestTrue(TEXT("With the same setup"), Director->GetSetup().Dummies == 3 && Director->GetSetup().KnockdownGoal == 1);
			TestEqual(TEXT("And nothing counted"), Stats.Dealt.Hits + Stats.Taken.Hits + Stats.OtherHits + Stats.BallsThrown, 0);
			Run->Next();
			return false;
		}
		if (Run->Stage == 3)
		{
			if (!IsSettled(Mode, Player, *Run))
			{
				return false;
			}
			TestEqual(TEXT("The second run has three fresh dummies"), LiveDummies(World).Num(), 3);
			TestEqual(TEXT("And two launchers, not four"), CountTagged(World, TEXT("RunLauncher")), 2);
			for (const ACSTargetDummy* Dummy : LiveDummies(World))
			{
				TestFalse(TEXT("The one knocked down in the first run is standing again"), Dummy->IsRagdolled());
			}

			Director->EndRun(ECSRunEnd::Manual);
			TestTrue(TEXT("The player can end a run"), Director->GetState() == ECSRunState::Ended && Director->GetEndReason() == ECSRunEnd::Manual);
			TestTrue(TEXT("The results are on their way then too"), Director->AreResultsPending());
			TestFalse(TEXT("But not open yet: the player may still be clicking"), PC->IsPauseScreenOpen());

			Run->OldPawn = Player;
			Director->LeaveRun();
			TestTrue(TEXT("Leaving goes back to no run"), Director->GetState() == ECSRunState::Idle);
			TestFalse(TEXT("Leaving drops the results that were on their way"), Director->AreResultsPending());
			TestFalse(TEXT("With no run there is nothing to run again"), Director->Rerun());
			Run->Next();
			return false;
		}
		if (!IsSettled(Mode, Player, *Run))
		{
			return false;
		}
		// The map is as it was made.
		const TArray<ACSTargetDummy*> Now = LiveDummies(World);
		if (TestEqual(TEXT("The map's own dummies are back"), Now.Num(), Run->MapDummies))
		{
			TestFalse(TEXT("They are not run dummies"), Now[0]->ActorHasTag(TEXT("RunDummy")));
			TestTrue(TEXT("The first is on its own spot"), Now.ContainsByPredicate([&Run](const ACSTargetDummy* Dummy) { return FVector::Dist2D(Dummy->GetActorLocation(), Run->MapDummyHome) < 30.f; }));
		}
		TestEqual(TEXT("The run's launchers are gone"), CountTagged(World, TEXT("RunLauncher")), 0);
		TestTrue(TEXT("The status line says there is no run"), Director->GetStatusText().StartsWith(TEXT("No run")));
		return true;
	}));
	EndPlay();
	RestoreStoreFile();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSRunTimeLimitTest, "CombatSimulator.CS.Run.EndsAtItsTimeLimit", CSRunTests::TestFlags)
bool FCSRunTimeLimitTest::RunTest(const FString& Parameters)
{
	using namespace CSRunTests;
	UCSRunStore::bUseFile = false;
	StartPlay(this, PlaneMap, 1.0f);
	TSharedRef<FPlayRun> Run = MakeShared<FPlayRun>();
	Run->StageStart = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UWorld* World = PlayWorld();
		ACSGameMode* Mode = World ? Cast<ACSGameMode>(World->GetAuthGameMode()) : nullptr;
		UCSRunDirector* Director = Mode ? Mode->GetRunDirector() : nullptr;
		ACSPlayerController* PC = World ? Cast<ACSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Director || !PC)
		{
			AddError(TEXT("No run director or ACSPlayerController"));
			return true;
		}
		if (Run->Stage == 0)
		{
			// The shortest limit there is, and nobody in it.
			Run->Setup.SetValue(Dummies, 0.f);
			Run->Setup.SetValue(TimeLimit, 5.f);
			FString Reason;
			TestTrue(TEXT("The run starts"), Director->StartRun(Run->Setup, Reason));
			Run->Next();
			return false;
		}
		if (Director->GetState() == ECSRunState::Ended && PC->IsPauseScreenOpen())
		{
			TestTrue(TEXT("The run ended at its time limit"), Director->GetEndReason() == ECSRunEnd::TimeLimit);
			TestEqual(TEXT("Its time is the limit"), Director->GetStats().Seconds, 5.f);
			TestEqual(TEXT("A run with no dummies has none, the map's included"), LiveDummies(World).Num(), 0);
			TestTrue(TEXT("The results opened"), PC->IsPauseScreenOpen());
			TestTrue(TEXT("It took about as long as it said, and the results a moment more"), Run->Waited() > 4.0 + Director->ResultsDelaySeconds * 0.8);
			PC->SetPauseScreenOpen(false);
			return true;
		}
		if (Run->Waited() > 40.0)
		{
			AddError(FString::Printf(TEXT("A 5 s run was still on after 40 s (run time %.1f s)"), Director->GetStats().Seconds));
			return true;
		}
		return false;
	}));
	EndPlay();
	RestoreStoreFile();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSRunOtherMapTest, "CombatSimulator.CS.Run.OpensItsMap", CSRunTests::TestFlags)
bool FCSRunOtherMapTest::RunTest(const FString& Parameters)
{
	using namespace CSRunTests;
	UCSRunStore::bUseFile = false;
	StartPlay(this, PlaneMap, 0.5f);
	TSharedRef<FPlayRun> Run = MakeShared<FPlayRun>();
	Run->StageStart = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UWorld* World = PlayWorld();
		ACSGameMode* Mode = World ? Cast<ACSGameMode>(World->GetAuthGameMode()) : nullptr;
		UCSRunDirector* Director = Mode ? Mode->GetRunDirector() : nullptr;
		ACSCharacter* Player = PlayerCharacter(World);
		if (Run->Waited() > StartTimeoutSeconds)
		{
			AddError(FString::Printf(TEXT("Stage %d: the run did not get to the gym (playing %s)"), Run->Stage, World ? *World->GetMapName() : TEXT("nothing")));
			return true;
		}
		if (Run->Stage == 0)
		{
			if (!Director || !Player || !World->GetMapName().EndsWith(TEXT("L_Plane")))
			{
				return false;
			}
			UCSRunStore* Store = UCSRunStore::Get(World);
			if (!TestNotNull(TEXT("The game has a run store"), Store))
			{
				return true;
			}
			// The draft is what the Run tab edits; a fresh game holds the defaults (the file is off).
			TestTrue(TEXT("The draft starts at the defaults"), Store->GetDraft() == FCSRunSetup());
			TestTrue(TEXT("A field of the draft can be changed"), Store->SetDraftValue(Dummies, 2.f, true));
			Store->SetDraftValue(Formation, 1.f, true);
			Store->SetDraftValue(TimeLimit, 0.f, true);
			Store->SetDraftMap(TEXT("L_MovementGym"), true);
			TestFalse(TEXT("Nothing is waiting yet"), Store->HasPending());

			FString Reason;
			TestTrue(TEXT("A run on the other map is accepted"), Director->StartRun(Store->GetDraft(), Reason));
			TestTrue(TEXT("It waits for that map"), Store->HasPending());
			TestTrue(TEXT("Nothing runs on this one"), Director->GetState() == ECSRunState::Idle);
			Run->Next();
			return false;
		}
		// The gym, with the run on and its dummies in.
		if (!Director || !Player || !World->GetMapName().EndsWith(TEXT("L_MovementGym")) || Director->GetState() != ECSRunState::Running
			|| Mode->IsResetPending() || LiveDummies(World).Num() != 2)
		{
			return false;
		}
		UCSRunStore* Store = UCSRunStore::Get(World);
		TestTrue(TEXT("The waiting run was taken"), Store && !Store->HasPending());
		if (Store)
		{
			// A run waiting for a map that is not the one that opened is dropped, not started here.
			FCSRunSetup Elsewhere;
			Elsewhere.Map = TEXT("L_Plane");
			Store->SetPending(Elsewhere);
			FCSRunSetup Taken;
			AddExpectedError(TEXT("a run was waiting for L_Plane"), EAutomationExpectedErrorFlags::Contains, 1);
			TestFalse(TEXT("A run waiting for another map is not taken here"), Store->TakePending(TEXT("L_MovementGym"), Taken));
			TestFalse(TEXT("And it is dropped"), Store->HasPending());
		}
		TestTrue(TEXT("The draft came along"), Store && Store->GetDraft().Dummies == 2 && Store->GetDraft().Map == TEXT("L_MovementGym"));
		TestEqual(TEXT("The run knows where it is"), Director->GetSetup().Map, FString(TEXT("L_MovementGym")));
		TestTrue(TEXT("It is the ring that was asked for"), Director->GetSetup().Formation == ECSRunFormation::Ring);
		for (const ACSTargetDummy* Dummy : LiveDummies(World))
		{
			TestTrue(TEXT("The gym's dummies are the run's"), Dummy->ActorHasTag(TEXT("RunDummy")));
		}
		TestEqual(TEXT("The gym's own launcher is still there"), CountTagged(World, TEXT("Launcher")), 1);
		return true;
	}));
	EndPlay();
	RestoreStoreFile();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
