// The play-test overhaul (decision 0018): punches that find their target, the combat stance, the
// camera following a knockdown, hit feedback, and the scripted play-test itself
// (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Overhaul).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "../CSGameMode.h"
#include "../CSPlayerController.h"
#include "../CSTargetDummy.h"
#include "../Feedback/CSHitFeedback.h"
#include "../Playtest/CSPlaytestRunner.h"
#include "../Run/CSRunDirector.h"
#include "../Run/CSRunStore.h"
#include "Camera/CameraComponent.h"
#include "CSAnimInstance.h"
#include "CSAnimNode_LegYaw.h"
#include "CSCameraRig.h"
#include "CSPhysicsBodyComponent.h"
#include "CSPlayTestRig.h"
#include "CSStrikeComponent.h"
#include "Dom/JsonObject.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CSOverhaulTests
{
	using namespace CSPlayRig;

	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	struct FStage
	{
		int32 Stage = 0;
		int32 Case = 0;
		double StageStart = 0.0;
		int32 LandedBefore = 0;
		int32 ThrownBefore = 0;
		float Value = 0.f;
		TArray<float> Samples;

		void Next()
		{
			++Stage;
			StageStart = FPlatformTime::Seconds();
		}
		double Waited() const { return FPlatformTime::Seconds() - StageStart; }
	};

	/** Starts a run with one dummy Distance ahead and no time limit. */
	bool StartOneDummyRun(FAutomationTestBase& Test, UCSRunDirector* Director, float Distance)
	{
		FCSRunSetup Setup;
		Setup.SetValue(TEXT("Dummies"), 1.f);
		Setup.SetValue(TEXT("Distance"), Distance);
		Setup.SetValue(TEXT("TimeLimit"), 0.f);
		FString Reason;
		return Test.TestTrue(TEXT("The run starts"), Director && Director->StartRun(Setup, Reason));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOverhaulLegYawTest, "CombatSimulator.CS.Overhaul.LegYawAndBackward", CSOverhaulTests::TestFlags)
bool FCSOverhaulLegYawTest::RunTest(const FString& Parameters)
{
	bool bBackward = true;
	TestEqual(TEXT("Straight ahead: no turn"), UCSAnimInstance::ComputeLegYaw(0.f, 60.f, bBackward), 0.f);
	TestFalse(TEXT("Straight ahead is forward"), bBackward);
	TestEqual(TEXT("Moving to the right: the legs turn right"), UCSAnimInstance::ComputeLegYaw(45.f, 60.f, bBackward), 45.f);
	TestEqual(TEXT("Straight sideways: as far as they go"), UCSAnimInstance::ComputeLegYaw(90.f, 60.f, bBackward), 60.f);
	TestFalse(TEXT("Sideways is not backward"), bBackward);
	TestEqual(TEXT("Straight back: no turn"), UCSAnimInstance::ComputeLegYaw(180.f, 60.f, bBackward), 0.f);
	TestTrue(TEXT("Straight back plays backward"), bBackward);
	TestEqual(TEXT("Back and to the right: the legs turn left, the way a backward step goes"), UCSAnimInstance::ComputeLegYaw(135.f, 60.f, bBackward), -45.f);
	TestEqual(TEXT("Back and to the left: they turn right"), UCSAnimInstance::ComputeLegYaw(-135.f, 60.f, bBackward), 45.f);
	TestTrue(TEXT("And that is backward"), bBackward);
	TestEqual(TEXT("Angles wrap"), UCSAnimInstance::ComputeLegYaw(405.f, 60.f, bBackward), 45.f);
	// The margin: near the side the answer depends on what it was.
	UCSAnimInstance::ComputeLegYaw(100.f, 60.f, false, bBackward);
	TestFalse(TEXT("At 100 degrees, forward stays forward"), bBackward);
	UCSAnimInstance::ComputeLegYaw(100.f, 60.f, true, bBackward);
	TestTrue(TEXT("At 100 degrees, backward stays backward"), bBackward);
	UCSAnimInstance::ComputeLegYaw(115.f, 60.f, false, bBackward);
	TestTrue(TEXT("Past 110 degrees it turns backward"), bBackward);
	UCSAnimInstance::ComputeLegYaw(85.f, 60.f, true, bBackward);
	TestFalse(TEXT("Below 90 degrees it turns forward"), bBackward);

	// The pelvis turns about the vertical in place; a quarter turn sends its forward (+Y in the mesh) to -X.
	const FTransform Pelvis(FQuat::Identity, FVector(1.f, 2.f, 95.f));
	const FTransform Turned = FCSAnimNode_LegYaw::TurnPelvis(Pelvis, 90.f);
	TestTrue(TEXT("The pelvis stays where it is"), Turned.GetLocation().Equals(Pelvis.GetLocation()));
	TestTrue(TEXT("And turns about the vertical"), Turned.GetRotation().RotateVector(FVector(0.f, 1.f, 0.f)).Equals(FVector(-1.f, 0.f, 0.f), 1.e-4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOverhaulPlaytestPartsTest, "CombatSimulator.CS.Overhaul.PlaytestScenariosAndLog", CSOverhaulTests::TestFlags)
bool FCSOverhaulPlaytestPartsTest::RunTest(const FString& Parameters)
{
	TArray<FString> Unknown;
	const TArray<FString> All = UCSPlaytestRunner::ParseScenarios(TEXT("all"), Unknown);
	TestEqual(TEXT("all is every scenario"), All, UCSPlaytestRunner::ScenarioNames());
	const TArray<FString> Some = UCSPlaytestRunner::ParseScenarios(TEXT(" Punch, nope ,locomotion,punch"), Unknown);
	TestEqual(TEXT("Names are read in order, once, in any case"), Some, TArray<FString>({TEXT("punch"), TEXT("locomotion")}));
	TestEqual(TEXT("An unknown name is reported"), Unknown, TArray<FString>({TEXT("nope")}));

	TestFalse(TEXT("all leaves out the scenarios on their own map"), All.Contains(TEXT("gym")));
	TestEqual(TEXT("gym is played when named"), UCSPlaytestRunner::ParseScenarios(TEXT("gym"), Unknown), TArray<FString>({TEXT("gym")}));

	TArray<FString> Every = UCSPlaytestRunner::ScenarioNames();
	Every.Append(UCSPlaytestRunner::MapScenarioNames());
	for (const FString& Name : Every)
	{
		const TArray<FCSPlaytestStep> Steps = UCSPlaytestRunner::BuildScenario(Name);
		int32 Labelled = 0;
		TSet<FString> Labels;
		for (const FCSPlaytestStep& Step : Steps)
		{
			TestTrue(Name + TEXT(": every step takes time"), Step.Seconds > 0.f);
			if (!Step.Label.IsEmpty())
			{
				++Labelled;
				TestFalse(Name + TEXT(": labels are unique: ") + Step.Label, Labels.Contains(Step.Label));
				Labels.Add(Step.Label);
			}
		}
		TestTrue(Name + TEXT(" has labelled steps"), Labelled >= 3);
	}
	TestTrue(TEXT("An unknown scenario has no steps"), UCSPlaytestRunner::BuildScenario(TEXT("nope")).IsEmpty());

	// One event line is one JSON object, quotes in labels included.
	const FString Line = UCSPlaytestRunner::MakeEventLine(TEXT("result"), 3, TEXT("the player's punch \"hard\""), {{TEXT("punches"), TEXT("1 of 1 landed")}}, 12.5);
	TSharedPtr<FJsonObject> Object;
	TestTrue(TEXT("An event line is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Object) && Object.IsValid());
	if (Object.IsValid())
	{
		TestEqual(TEXT("event"), Object->GetStringField(TEXT("event")), FString(TEXT("result")));
		TestEqual(TEXT("index"), int32(Object->GetNumberField(TEXT("index"))), 3);
		TestEqual(TEXT("label"), Object->GetStringField(TEXT("label")), FString(TEXT("the player's punch \"hard\"")));
		TestTrue(TEXT("utc_ms is a recent time"), Object->GetNumberField(TEXT("utc_ms")) > 1.7e12);
		TestEqual(TEXT("data"), Object->GetObjectField(TEXT("data"))->GetStringField(TEXT("punches")), FString(TEXT("1 of 1 landed")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOverhaulPunchesLandTest, "CombatSimulator.CS.Overhaul.PunchesLandFromWhereThePlayerStands", CSOverhaulTests::TestFlags)
bool FCSOverhaulPunchesLandTest::RunTest(const FString& Parameters)
{
	using namespace CSOverhaulTests;
	UCSRunStore::bUseFile = false;
	StartPlay(this, PlaneMap, 1.5f);
	TSharedRef<FStage> Run = MakeShared<FStage>();
	Run->StageStart = FPlatformTime::Seconds();

	// Distance from the dummy, how far to its side, how far the body is turned from the aim.
	struct FCase { const TCHAR* Name; float Distance; float Side; float BodyYaw; };
	static const FCase Cases[] = {
		{TEXT("70 cm"), 70.f, 0.f, 0.f},
		{TEXT("100 cm"), 100.f, 0.f, 0.f},
		{TEXT("150 cm (steps in)"), 150.f, 0.f, 0.f},
		{TEXT("35 cm to the side"), 80.f, 35.f, 0.f},
		{TEXT("the body turned 90 degrees"), 80.f, 0.f, 90.f},
		{TEXT("pressed against it"), 45.f, 10.f, 0.f},
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
		if (Run->Stage == 2)
		{
			// Stand where the case says, give the dummy time to settle from the last punch.
			C.PlaceAtDummy(Case.Distance, Case.Side, Case.BodyYaw);
			Run->ThrownBefore = Director->GetStats().PunchesThrown;
			Run->LandedBefore = Director->GetStats().PunchesLanded;
			Run->Next();
			return false;
		}
		if (Run->Stage == 3)
		{
			if (Run->Waited() < 0.25)
			{
				return false;
			}
			UCSStrikeComponent* Strike = Player ? Player->GetStrike() : nullptr;
			if (!TestNotNull(TEXT("Player and strike"), Strike))
			{
				return true;
			}
			TestTrue(FString::Printf(TEXT("%s: the dummy is the target"), Case.Name), Strike->FindTarget() == C.Dummy());
			// The straight: the punch as tuned (the attack key opens with a jab, too short to step in from far).
			TestTrue(FString::Printf(TEXT("%s: the punch starts"), Case.Name), Player->StrikeWith(ECSStrikeMove::Straight));
			Run->Next();
			return false;
		}
		// Stage 4: wait for the punch to finish.
		const UCSStrikeComponent* Strike = Player ? Player->GetStrike() : nullptr;
		if (Strike && Strike->GetPhase() != ECSStrikePhase::None)
		{
			return false;
		}
		const int32 Landed = Director->GetStats().PunchesLanded - Run->LandedBefore;
		const FCSContactRecord& Hit = C.Dummy()->GetPhysicsBody()->GetLastContact();
		AddInfo(FString::Printf(TEXT("Punch from %s: %s, fist reached %.0f cm at %.0f cm/s; last hit %.0f cm/s on %s"), Case.Name,
			Landed > 0 ? TEXT("landed") : TEXT("MISSED"), Strike ? Strike->GetPeakReach() : 0.f, Strike ? Strike->GetPeakSpeed() : 0.f,
			Hit.VelocityChange, *Hit.Bone.ToString()));
		TestEqual(FString::Printf(TEXT("%s: one punch thrown"), Case.Name), Director->GetStats().PunchesThrown - Run->ThrownBefore, 1);
		TestEqual(FString::Printf(TEXT("%s: and it landed"), Case.Name), Landed, 1);
		if (Run->Case == 0)
		{
			// The first punch also shows on screen: a mark and a flash.
			const UCSHitFeedback* Feedback = Mode->GetHitFeedback();
			TestTrue(TEXT("A landed punch is marked on screen"), Feedback && Feedback->GetMarks().Num() > 0);
		}
		if (++Run->Case >= UE_ARRAY_COUNT(Cases))
		{
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOverhaulStanceTest, "CombatSimulator.CS.Overhaul.StanceFacesTheAimAndTheCameraFollowsAFall", CSOverhaulTests::TestFlags)
bool FCSOverhaulStanceTest::RunTest(const FString& Parameters)
{
	using namespace CSOverhaulTests;
	UCSRunStore::bUseFile = false;
	StartPlay(this, PlaneMap, 1.5f);
	TSharedRef<FStage> Run = MakeShared<FStage>();
	Run->StageStart = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UWorld* World = PlayWorld();
		ACSGameMode* Mode = World ? Cast<ACSGameMode>(World->GetAuthGameMode()) : nullptr;
		UCSRunDirector* Director = Mode ? Mode->GetRunDirector() : nullptr;
		if (!Director || Run->Waited() > 30.0)
		{
			AddError(FString::Printf(TEXT("Stage %d did not finish"), Run->Stage));
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
			// No dummies: nothing near, no attack yet. The character faces the way it moves.
			FCSRunSetup Setup;
			Setup.SetValue(TEXT("Dummies"), 0.f);
			Setup.SetValue(TEXT("TimeLimit"), 0.f);
			FString Reason;
			Director->StartRun(Setup, Reason);
			Run->Next();
			return false;
		}
		if (Run->Stage == 1)
		{
			if (Mode->IsResetPending() || !Player || Run->Waited() < 1.0)
			{
				return false;
			}
			TestFalse(TEXT("Alone, no combat stance"), Player->IsInCombatStance());
			TestFalse(TEXT("So the body turns toward its movement"), Player->GetCameraRig()->IsFacingAim());
			StartOneDummyRun(*this, Director, 300.f);
			Run->Next();
			return false;
		}
		if (Run->Stage == 2)
		{
			if (Mode->IsResetPending() || !C.Dummy() || !Player || Run->Waited() < 1.2)
			{
				return false;
			}
			TestTrue(TEXT("With a dummy 3 m away, the combat stance"), Player->IsInCombatStance());
			TestTrue(TEXT("And the body faces the aim"), Player->GetCameraRig()->IsFacingAim());
			C.PlaceAtDummy(250.f, 0.f);
			Run->Samples.Reset();
			Run->Next();
			return false;
		}
		if (Run->Stage == 3)
		{
			// Back off for 1.5 s, aiming at the dummy.
			C.AimAtDummy();
			C.Move(-1.f, 0.f);
			const ACSTargetDummy* Target = C.Dummy();
			if (Run->Waited() > 0.5 && Target)
			{
				const float Toward = float((Target->GetActorLocation() - Player->GetActorLocation()).Rotation().Yaw);
				Run->Samples.Add(FMath::Abs(FRotator::NormalizeAxis(float(Player->GetActorRotation().Yaw) - Toward)));
				if (const UCSAnimInstance* Anim = Cast<UCSAnimInstance>(Player->GetMesh()->GetAnimInstance()))
				{
					Run->Value = FMath::Min(Run->Value, Anim->GetLocomotionPlayRate());
				}
			}
			if (Run->Waited() < 1.5)
			{
				return false;
			}
			float Worst = 0.f;
			for (const float Sample : Run->Samples)
			{
				Worst = FMath::Max(Worst, Sample);
			}
			AddInfo(FString::Printf(TEXT("Backing off: body at most %.1f deg off the dummy, speed %.0f cm/s, lowest play rate %.2f"),
				Worst, Player->GetVelocity().Size2D(), Run->Value));
			TestTrue(TEXT("Backing off, the body keeps facing the dummy"), Worst < 15.f);
			TestTrue(TEXT("The steps play backward"), Run->Value < 0.f);
			TestTrue(TEXT("Backing off is slower than jogging forward"), Player->GetVelocity().Size2D() < Player->GetActiveTuning().JogSpeed * 0.85f);

			// Now a knockdown: the camera goes down with the body.
			Run->Value = float(Player->GetCameraRig()->GetCamera()->GetComponentLocation().Z);
			UCSPhysicsBodyComponent* Physics = Player->GetPhysicsBody();
			Physics->ApplyImpulseAtBone(TEXT("spine_03"), Player->GetActorForwardVector() * Player->GetActiveTuning().RagdollImpulseThreshold * 1.6f * Physics->GetBodyMass());
			Run->Next();
			return false;
		}
		if (Run->Stage == 4)
		{
			if (Run->Waited() < 1.6)
			{
				return false;
			}
			TestTrue(TEXT("The shove knocked the player down"), Player->IsRagdolled());
			const float Down = float(Player->GetCameraRig()->GetCamera()->GetComponentLocation().Z);
			AddInfo(FString::Printf(TEXT("Camera height standing %.0f cm, with the body down %.0f cm"), Run->Value, Down));
			TestTrue(TEXT("The camera follows the body down"), Down < Run->Value - 45.f);
			return true;
		}
		return true;
	}));
	EndPlay();
	RestoreStoreFile();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOverhaulPlaytestPunchScenarioTest, "CombatSimulator.CS.Overhaul.PlaytestPunchScenarioRuns", CSOverhaulTests::TestFlags)
bool FCSOverhaulPlaytestPunchScenarioTest::RunTest(const FString& Parameters)
{
	using namespace CSOverhaulTests;
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
			TestFalse(TEXT("Without -CSPlaytest the runner is idle"), Runner->IsRunning());
			Runner->Begin({TEXT("punch")}, FString(), false);
			TestTrue(TEXT("It plays when asked"), Runner->IsRunning());
			Run->Next();
			return false;
		}
		if (Runner->IsRunning())
		{
			if (Run->Waited() > 90.0)
			{
				AddError(TEXT("The punch scenario did not finish in 90 s"));
				return true;
			}
			return false;
		}
		// Read the results back as the review script will.
		int32 Segments = 0, Results = 0, PunchSegments = 0, Missed = 0;
		bool bEnded = false;
		for (const FString& Line : Runner->GetLogLines())
		{
			TSharedPtr<FJsonObject> Object;
			if (!TestTrue(TEXT("Every log line is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Object) && Object.IsValid()))
			{
				continue;
			}
			const FString Event = Object->GetStringField(TEXT("event"));
			Segments += Event == TEXT("segment") ? 1 : 0;
			bEnded |= Event == TEXT("end");
			if (Event != TEXT("result"))
			{
				continue;
			}
			++Results;
			const TSharedPtr<FJsonObject>* Data = nullptr;
			FString Punches;
			if (Object->TryGetObjectField(TEXT("data"), Data) && (*Data)->TryGetStringField(TEXT("punches"), Punches))
			{
				++PunchSegments;
				// "N of M landed"
				const int32 Landed = FCString::Atoi(*Punches);
				const int32 Thrown = FCString::Atoi(*Punches.RightChop(Punches.Find(TEXT("of ")) + 3));
				AddInfo(Object->GetStringField(TEXT("label")) + TEXT(": ") + Punches);
				// A single punch must land. In the fast combo every hit staggers the dummy somewhere
				// new and the outcome varies from run to run (two to four of four seen); at least half
				// must land.
				const bool bCombo = Object->GetStringField(TEXT("label")).Contains(TEXT("in a row"));
				Missed += bCombo ? FMath::Max(0, (Thrown + 1) / 2 - Landed) : Thrown - Landed;
				if (Landed < Thrown)
				{
					// Everything the segment noted, so a miss in a full run can be explained afterwards.
					FString Facts;
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Data)->Values)
					{
						Facts += FString::Printf(TEXT("%s=%s; "), *Pair.Key, *Pair.Value->AsString());
					}
					AddInfo(Object->GetStringField(TEXT("label")) + TEXT(" missed: ") + Facts);
				}
				TestTrue(Object->GetStringField(TEXT("label")) + TEXT(": a punch was thrown"), Thrown > 0);
			}
		}
		TestTrue(TEXT("The scenario ended"), bEnded);
		TestTrue(TEXT("Every segment has a result"), Segments > 0 && Segments == Results);
		TestTrue(TEXT("The punch cases were played"), PunchSegments >= 8);
		TestEqual(TEXT("Every single punch in the scenario landed, and at least half of the four in a row"), Missed, 0);
		return true;
	}));
	EndPlay();
	RestoreStoreFile();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
