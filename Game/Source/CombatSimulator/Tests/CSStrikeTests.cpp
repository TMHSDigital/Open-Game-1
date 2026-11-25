// The punch: where the fist is steered, and punches thrown on the real map at a dummy and at nothing
// (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Strike).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "../CSGameMode.h"
#include "../CSTargetDummy.h"
#include "../Run/CSRunDirector.h"
#include "../Run/CSRunStore.h"
#include "CSMovementTuning.h"
#include "CSPhysicsBodyComponent.h"
#include "CSPlayTestRig.h"
#include "CSStrikeComponent.h"
#include "CSTuningRanges.h"
#include "EngineUtils.h"

namespace CSStrikeTests
{
	using namespace CSPlayRig;

	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	struct FStrikeRun
	{
		int32 Stage = 0;
		double StageStart = 0.0;
		TWeakObjectPtr<AActor> OldPawn;

		void Next()
		{
			++Stage;
			StageStart = FPlatformTime::Seconds();
		}
		double Waited() const { return FPlatformTime::Seconds() - StageStart; }
	};

	ACSTargetDummy* OnlyDummy(UWorld* World)
	{
		for (TActorIterator<ACSTargetDummy> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed())
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Stands the player at Location facing Yaw, view and body both. */
	void Place(ACSCharacter& Player, const FVector& Location, float Yaw)
	{
		const FRotator Facing(0.f, Yaw, 0.f);
		Player.SetActorLocationAndRotation(Location, Facing, false, nullptr, ETeleportType::TeleportPhysics);
		if (AController* Controller = Player.GetController())
		{
			Controller->SetControlRotation(Facing);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSStrikeSteeringTest, "CombatSimulator.CS.Strike.WhereTheFistIsSteered", CSStrikeTests::TestFlags)
bool FCSStrikeSteeringTest::RunTest(const FString& Parameters)
{
	const FVector Shoulder(100.f, 20.f, 140.f);
	const FVector Aim = FVector(1.f, 1.f, 0.f).GetSafeNormal();
	const FVector Chest(100.f, 0.f, 140.f);
	const FVector Cocked = UCSStrikeComponent::ComputeHandTarget(ECSStrikePhase::Windup, Shoulder, Chest, Aim, 70.f);
	const FVector Out = UCSStrikeComponent::ComputeHandTarget(ECSStrikePhase::Strike, Shoulder, Chest, Aim, 70.f);
	TestTrue(TEXT("The draw back is close to the shoulder"), FVector::Dist(Cocked, Shoulder) < 20.f);
	TestTrue(TEXT("And on the aim side of it"), FVector::DotProduct(Cocked - Shoulder, Aim) > 0.f);
	TestTrue(TEXT("The strike goes the reach along the aim from the middle of the chest, not from the shoulder"), Out.Equals(Chest + Aim * 70.f, 0.01f));
	TestTrue(TEXT("Out of a strike the fist is not steered away from the shoulder"),
		UCSStrikeComponent::ComputeHandTarget(ECSStrikePhase::Recover, Shoulder, Chest, Aim, 70.f).Equals(Shoulder));

	const FVector Hand(0.f, 0.f, 100.f);
	const FVector Base(50.f, 0.f, 0.f);
	// 10 cm in a 60th of a second is 600 cm/s: under the limit, the fist gets there in the frame.
	const FVector Near = UCSStrikeComponent::ComputeDriveVelocity(Hand, Hand + FVector(10.f, 0.f, 0.f), 1.f / 60.f, 1000.f, FVector::ZeroVector);
	TestTrue(TEXT("A near target is reached in one frame"), Near.Equals(FVector(600.f, 0.f, 0.f), 0.5f));
	const FVector Far = UCSStrikeComponent::ComputeDriveVelocity(Hand, Hand + FVector(0.f, 300.f, 0.f), 1.f / 60.f, 1000.f, Base);
	TestTrue(TEXT("A far target is approached at the top speed, on top of the character's own motion"), Far.Equals(FVector(50.f, 1000.f, 0.f), 0.5f));
	TestTrue(TEXT("A frame of no length adds nothing"), UCSStrikeComponent::ComputeDriveVelocity(Hand, Hand + FVector(100.f, 0.f, 0.f), 0.f, 1000.f, Base).Equals(Base));

	// Every punch value is a slider on the Tuning tab.
	for (const TCHAR* Path : {TEXT("PunchWindupSeconds"), TEXT("PunchStrikeSeconds"), TEXT("PunchRecoverSeconds"), TEXT("PunchReach"), TEXT("PunchSpeed"), TEXT("PunchLean"), TEXT("PunchMass")})
	{
		const FCSTuningRow* Row = CSTuning::Rows().FindByPredicate([Path](const FCSTuningRow& R) { return R.Path == Path; });
		float Value = 0.f;
		if (TestNotNull(FString::Printf(TEXT("%s has a row in the range table"), Path), Row))
		{
			TestTrue(FString::Printf(TEXT("%s: the default is inside its range"), Path),
				CSTuning::GetValue(*GetDefault<UCSMovementTuning>(), Path, Value) && Value >= Row->Min && Value <= Row->Max);
		}
	}
	const ACSCharacter* Defaults = GetDefault<ACSCharacter>();
	TestNotNull(TEXT("Every character has a strike component"), Defaults->GetStrike());
	TestFalse(TEXT("The attack key is configured"), Defaults->AttackAction.IsNull());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSStrikePunchTest, "CombatSimulator.CS.Strike.PunchADummyAndTheAir", CSStrikeTests::TestFlags)
bool FCSStrikePunchTest::RunTest(const FString& Parameters)
{
	using namespace CSStrikeTests;
	UCSRunStore::bUseFile = false;
	StartPlay(this, PlaneMap, 1.5f);
	TSharedRef<FStrikeRun> Run = MakeShared<FStrikeRun>();
	Run->StageStart = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UWorld* World = PlayWorld();
		ACSGameMode* Mode = World ? Cast<ACSGameMode>(World->GetAuthGameMode()) : nullptr;
		UCSRunDirector* Director = Mode ? Mode->GetRunDirector() : nullptr;
		ACSCharacter* Player = PlayerCharacter(World);
		if (!World || !Director)
		{
			AddError(TEXT("No play world or run director"));
			return true;
		}
		if (Run->Waited() > 30.0)
		{
			AddError(FString::Printf(TEXT("Stage %d did not finish in 30 s"), Run->Stage));
			return true;
		}
		const FCSRunStats& Stats = Director->GetStats();
		UCSStrikeComponent* Strike = Player ? Player->GetStrike() : nullptr;
		ACSTargetDummy* Dummy = OnlyDummy(World);

		if (Run->Stage == 0)
		{
			// A run with one dummy, so the punches are counted.
			if (!Player)
			{
				return false;
			}
			FCSRunSetup Setup;
			Setup.SetValue(TEXT("Dummies"), 1.f);
			Setup.SetValue(TEXT("Distance"), 200.f);
			Setup.SetValue(TEXT("TimeLimit"), 0.f);
			FString Reason;
			Run->OldPawn = Player;
			TestTrue(TEXT("The run starts"), Director->StartRun(Setup, Reason));
			Run->Next();
			return false;
		}
		if (Run->Stage == 1)
		{
			if (!Player || Player == Run->OldPawn.Get() || Mode->IsResetPending() || !Dummy || Run->Waited() < 1.0)
			{
				return false;
			}
			// Within arm's reach of the dummy, face to face.
			const FVector Toward = Dummy->GetActorForwardVector();
			Place(*Player, Dummy->GetActorLocation() + Toward * 75.f, (-Toward).Rotation().Yaw);
			Run->Next();
			return false;
		}
		if (Run->Stage == 2)
		{
			if (Run->Waited() < 0.6 || !Strike)
			{
				return false;
			}
			TestTrue(TEXT("No strike before the key"), Strike->GetPhase() == ECSStrikePhase::None);
			TestTrue(TEXT("The aim is level and toward the dummy"),
				FVector::DotProduct(Strike->GetAimDirection(), (Dummy->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D()) > 0.99f);
			TestTrue(TEXT("The attack starts a punch"), Player->Attack());
			TestTrue(TEXT("It begins by drawing back"), Strike->GetPhase() == ECSStrikePhase::Windup);
			TestTrue(TEXT("The first punch is with the right hand"), Strike->IsRightHand());
			TestFalse(TEXT("A second punch cannot start during the first"), Player->Attack());
			TestEqual(TEXT("One strike so far"), Strike->GetStrikeCount(), 1);
			TestEqual(TEXT("The run counted the punch"), Stats.PunchesThrown, 1);
			Run->Next();
			return false;
		}
		if (Run->Stage == 3)
		{
			if (!Strike || Strike->GetPhase() != ECSStrikePhase::None)
			{
				return false;
			}
			const FCSContactRecord& Hit = Dummy->GetPhysicsBody()->GetLastContact();
			AddInfo(FString::Printf(TEXT("Punch at the dummy: fist reached %.0f cm at up to %.0f cm/s; the dummy took %.0f cm/s on %s"),
				Strike->GetPeakReach(), Strike->GetPeakSpeed(), Hit.VelocityChange, *Hit.Bone.ToString()));
			TestTrue(TEXT("The punch landed"), Strike->HasLanded());
			TestTrue(TEXT("The dummy was hit by it"), Hit.Result == ECSContactResult::Stagger || Hit.Result == ECSContactResult::Ragdoll);
			TestEqual(TEXT("By the player"), Hit.Source, Player->GetName());
			TestFalse(TEXT("On a body part"), Hit.Bone.IsNone());
			TestTrue(TEXT("A jab staggers; it does not knock down"), Hit.VelocityChange > 50.f && Hit.VelocityChange < Dummy->GetActiveTuning().RagdollImpulseThreshold);
			TestEqual(TEXT("The run counted one hit dealt"), Stats.Dealt.Hits, 1);
			TestEqual(TEXT("And one punch landed"), Stats.PunchesLanded, 1);
			TestEqual(TEXT("It was not a ball hit"), Stats.BallHits, 0);
			TestEqual(TEXT("The player took nothing from its own punch"), Stats.Taken.Hits, 0);

			// Away from everything, facing the other way.
			const FVector Toward = Dummy->GetActorForwardVector();
			Place(*Player, Dummy->GetActorLocation() + Toward * 900.f, Toward.Rotation().Yaw);
			Run->Next();
			return false;
		}
		if (Run->Stage == 4)
		{
			if (Run->Waited() < 0.6 || !Strike)
			{
				return false;
			}
			TestTrue(TEXT("A second punch starts once the first is over"), Player->Attack());
			TestFalse(TEXT("With the other hand"), Strike->IsRightHand());
			Run->Next();
			return false;
		}
		if (!Strike || Strike->GetPhase() != ECSStrikePhase::None)
		{
			return false;
		}
		AddInfo(FString::Printf(TEXT("Punch at the air: fist reached %.0f cm at up to %.0f cm/s"), Strike->GetPeakReach(), Strike->GetPeakSpeed()));
		TestFalse(TEXT("A punch at the air lands on nobody"), Strike->HasLanded());
		TestTrue(TEXT("The arm really went out"), Strike->GetPeakReach() > 40.f);
		TestTrue(TEXT("And fast"), Strike->GetPeakSpeed() > 300.f);
		TestEqual(TEXT("Two punches thrown"), Stats.PunchesThrown, 2);
		TestEqual(TEXT("Still one landed"), Stats.PunchesLanded, 1);
		TestEqual(TEXT("Still one hit dealt"), Stats.Dealt.Hits, 1);
		const FString Summary = FString::Join(Stats.SummaryLines(), TEXT("\n"));
		TestTrue(TEXT("The results give the punches"), Summary.Contains(TEXT("Punches: 2 thrown, 1 landed (50%)")));

		Player->GetPhysicsBody()->EnterRagdoll();
		TestFalse(TEXT("A character that is down cannot punch"), Player->Attack());
		return true;
	}));
	EndPlay();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		UCSRunStore::bUseFile = true;
		return true;
	}));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
