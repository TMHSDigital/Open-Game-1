#include "CSPlaytestRunner.h"

#include "../CSGameMode.h"
#include "../CSPlayerController.h"
#include "../CSTargetDummy.h"
#include "../CombatSimulator.h"
#include "../Run/CSRunDirector.h"
#include "../Run/CSRunSetup.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CSCameraRig.h"
#include "CSCharacter.h"
#include "CSHitBall.h"
#include "CSMovementTuning.h"
#include "CSPhysicsBodyComponent.h"
#include "CSStrikeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// ---------------------------------------------------------------- context

ACSCharacter* FCSPlaytestContext::Player() const
{
	return Controller ? Cast<ACSCharacter>(Controller->GetPawn()) : nullptr;
}

ACSTargetDummy* FCSPlaytestContext::Dummy() const
{
	const ACSCharacter* Self = Player();
	ACSTargetDummy* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (TActorIterator<ACSTargetDummy> It(World); It; ++It)
	{
		if (It->IsActorBeingDestroyed())
		{
			continue;
		}
		const double Distance = Self ? FVector::DistSquared(It->GetActorLocation(), Self->GetActorLocation()) : 0.0;
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = *It;
		}
	}
	return Best;
}

void FCSPlaytestContext::Place(const FVector& Location, float Yaw) const
{
	ACSCharacter* Self = Player();
	if (!Self)
	{
		return;
	}
	Self->SetActorLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	Self->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	if (Controller)
	{
		Controller->SetControlRotation(FRotator(0.f, Yaw, 0.f));
	}
}

void FCSPlaytestContext::PlaceAtDummy(float Distance, float Side, float BodyYawOffset) const
{
	const ACSTargetDummy* Target = Dummy();
	ACSCharacter* Self = Player();
	if (!Target || !Self)
	{
		return;
	}
	const FVector Home = Target->GetActorLocation();
	const FVector Forward = Target->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	FVector At = Home + Forward * Distance + Right * Side;
	At.Z = Self->GetActorLocation().Z;
	const float FaceYaw = float((Home - At).Rotation().Yaw);
	Place(At, FaceYaw);
	// The body may be turned away from the aim; the view stays on the dummy.
	Self->SetActorRotation(FRotator(0.f, FaceYaw + BodyYawOffset, 0.f));
}

void FCSPlaytestContext::AimAtDummy() const
{
	const ACSTargetDummy* Target = Dummy();
	const ACSCharacter* Self = Player();
	if (Target && Self && Controller)
	{
		Controller->SetControlRotation(FRotator(0.f, float((Target->GetActorLocation() - Self->GetActorLocation()).Rotation().Yaw), 0.f));
	}
}

void FCSPlaytestContext::Move(float Forward, float Right) const
{
	ACSCharacter* Self = Player();
	if (!Self || !Controller)
	{
		return;
	}
	const FRotationMatrix Basis(FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f));
	if (!FMath::IsNearlyZero(Forward))
	{
		Self->AddMovementInput(Basis.GetUnitAxis(EAxis::X), Forward);
	}
	if (!FMath::IsNearlyZero(Right))
	{
		Self->AddMovementInput(Basis.GetUnitAxis(EAxis::Y), Right);
	}
}

void FCSPlaytestContext::Note(const FString& Key, const FString& Value) const
{
	if (Runner)
	{
		Runner->GetNotes().Add(Key, Value);
	}
}

void FCSPlaytestContext::Note(const FString& Key, float Value) const
{
	Note(Key, FString::Printf(TEXT("%.1f"), Value));
}

// ---------------------------------------------------------------- scenarios

namespace CSPlaytestScenarios
{
	using FCtx = FCSPlaytestContext;

	/** A silent step that starts a run with Dummies dummies Distance ahead and no time limit, and waits for it. */
	FCSPlaytestStep SetUpRun(int32 Dummies, float Distance)
	{
		FCSPlaytestStep Step;
		Step.Seconds = 1.6f;
		Step.Camera = ECSReviewCamera::Player;
		Step.OnStart = [Dummies, Distance](const FCtx& C)
		{
			if (UCSRunDirector* Director = C.Mode ? C.Mode->GetRunDirector() : nullptr)
			{
				FCSRunSetup Setup;
				Setup.SetValue(TEXT("Dummies"), float(Dummies));
				Setup.SetValue(TEXT("Distance"), Distance);
				Setup.SetValue(TEXT("TimeLimit"), 0.f);
				Setup.SetValue(TEXT("KnockdownGoal"), 0.f);
				FString Reason;
				Director->StartRun(Setup, Reason);
			}
		};
		Step.OnEnd = [](const FCtx& C)
		{
			if (ACSCharacter* Self = C.Player())
			{
				Self->GetCameraRig()->SetView(ECSView::ThirdPerson);
				Self->SetGait(ECSGait::Jog);
			}
		};
		return Step;
	}

	/** Measures the top ground speed of a step into the notes. */
	void TrackSpeed(const FCtx& C, float& Top)
	{
		if (const ACSCharacter* Self = C.Player())
		{
			Top = FMath::Max(Top, float(Self->GetVelocity().Size2D()));
			C.Note(TEXT("top speed cm/s"), Top);
		}
	}

	FCSPlaytestStep Locomotion(const TCHAR* Label, float Seconds, ECSGait Gait, float Forward, ECSReviewCamera Camera = ECSReviewCamera::Side)
	{
		FCSPlaytestStep Step;
		Step.Label = Label;
		Step.Seconds = Seconds;
		Step.Camera = Camera;
		TSharedRef<float> Top = MakeShared<float>(0.f);
		Step.OnStart = [Gait, Top](const FCtx& C)
		{
			*Top = 0.f;
			if (ACSCharacter* Self = C.Player())
			{
				Self->SetGait(Gait);
			}
		};
		Step.OnTick = [Forward, Top](const FCtx& C, float, float)
		{
			C.Move(Forward, 0.f);
			TrackSpeed(C, *Top);
		};
		return Step;
	}

	TArray<FCSPlaytestStep> BuildLocomotion()
	{
		TArray<FCSPlaytestStep> Steps;
		// No dummies: the map's own would put the player in the combat stance.
		Steps.Add(SetUpRun(0, 500.f));
		Steps.Add(Locomotion(TEXT("idle"), 2.5f, ECSGait::Jog, 0.f));
		Steps.Add(Locomotion(TEXT("walk"), 3.5f, ECSGait::Walk, 1.f));
		Steps.Add(Locomotion(TEXT("jog"), 3.5f, ECSGait::Jog, 1.f));
		Steps.Add(Locomotion(TEXT("sprint"), 4.f, ECSGait::Sprint, 1.f));
		Steps.Add(Locomotion(TEXT("stop from a sprint"), 2.f, ECSGait::Jog, 0.f));
		{
			FCSPlaytestStep Step = Locomotion(TEXT("turn around at a jog"), 3.5f, ECSGait::Jog, 1.f);
			Step.OnTick = [](const FCtx& C, float Time, float) { C.Move(Time < 1.2f ? 1.f : -1.f, 0.f); };
			Steps.Add(Step);
		}
		Steps.Add(Locomotion(TEXT("crouch, still"), 2.f, ECSGait::Crouch, 0.f));
		Steps.Add(Locomotion(TEXT("crouch walk"), 3.f, ECSGait::Crouch, 1.f));
		{
			FCSPlaytestStep Step = Locomotion(TEXT("jump in place"), 2.5f, ECSGait::Jog, 0.f);
			Step.OnTick = [](const FCtx& C, float Time, float DeltaTime)
			{
				if (ACSCharacter* Self = C.Player(); Self && Time >= 0.5f && Time - DeltaTime < 0.5f)
				{
					Self->Jump();
				}
				if (ACSCharacter* Self = C.Player(); Self && Time >= 0.8f && Time - DeltaTime < 0.8f)
				{
					Self->StopJumping();
				}
			};
			Steps.Add(Step);
		}
		{
			FCSPlaytestStep Step = Locomotion(TEXT("running jump"), 3.f, ECSGait::Jog, 1.f);
			Step.OnTick = [](const FCtx& C, float Time, float DeltaTime)
			{
				C.Move(1.f, 0.f);
				if (ACSCharacter* Self = C.Player(); Self && Time >= 1.f && Time - DeltaTime < 1.f)
				{
					Self->Jump();
				}
				if (ACSCharacter* Self = C.Player(); Self && Time >= 1.3f && Time - DeltaTime < 1.3f)
				{
					Self->StopJumping();
				}
			};
			Steps.Add(Step);
		}
		Steps.Add(Locomotion(TEXT("jog, the player's camera"), 3.f, ECSGait::Jog, 1.f, ECSReviewCamera::Player));
		{
			FCSPlaytestStep Step = Locomotion(TEXT("jog, first person"), 3.f, ECSGait::Jog, 1.f, ECSReviewCamera::Player);
			Step.OnStart = [](const FCtx& C)
			{
				if (ACSCharacter* Self = C.Player())
				{
					Self->SetGait(ECSGait::Jog);
					Self->GetCameraRig()->SetView(ECSView::FirstPerson);
				}
			};
			Step.OnEnd = [](const FCtx& C)
			{
				if (ACSCharacter* Self = C.Player())
				{
					Self->GetCameraRig()->SetView(ECSView::ThirdPerson);
				}
			};
			Steps.Add(Step);
		}
		return Steps;
	}

	/** Facing the dummy while moving Forward and Right relative to it; notes how far the body is off facing it. */
	FCSPlaytestStep Stance(const TCHAR* Label, float Seconds, float Forward, float Right, ECSReviewCamera Camera = ECSReviewCamera::Side)
	{
		FCSPlaytestStep Step;
		Step.Label = Label;
		Step.Seconds = Seconds;
		Step.Camera = Camera;
		TSharedRef<TArray<float>> Errors = MakeShared<TArray<float>>();
		Step.OnStart = [Errors](const FCtx& C)
		{
			Errors->Reset();
			C.PlaceAtDummy(250.f, 0.f);
			if (ACSCharacter* Self = C.Player())
			{
				Self->SetGait(ECSGait::Jog);
			}
		};
		Step.OnTick = [Forward, Right, Errors](const FCtx& C, float Time, float)
		{
			C.AimAtDummy();
			C.Move(Forward, Right);
			const ACSCharacter* Self = C.Player();
			const ACSTargetDummy* Target = C.Dummy();
			if (Self && Target && Time > 0.6f)
			{
				const float Toward = float((Target->GetActorLocation() - Self->GetActorLocation()).Rotation().Yaw);
				Errors->Add(FMath::Abs(FRotator::NormalizeAxis(float(Self->GetActorRotation().Yaw) - Toward)));
				C.Note(TEXT("combat stance"), Self->IsInCombatStance() ? TEXT("yes") : TEXT("no"));
			}
		};
		Step.OnEnd = [Errors](const FCtx& C)
		{
			if (Errors->IsEmpty())
			{
				return;
			}
			float Sum = 0.f, Max = 0.f;
			for (const float Error : *Errors)
			{
				Sum += Error;
				Max = FMath::Max(Max, Error);
			}
			C.Note(TEXT("facing error mean deg"), Sum / float(Errors->Num()));
			C.Note(TEXT("facing error max deg"), Max);
		};
		return Step;
	}

	TArray<FCSPlaytestStep> BuildStance()
	{
		TArray<FCSPlaytestStep> Steps;
		Steps.Add(SetUpRun(1, 300.f));
		Steps.Add(Stance(TEXT("walk in, facing the dummy"), 2.f, 0.6f, 0.f));
		Steps.Add(Stance(TEXT("back off, facing the dummy"), 2.5f, -1.f, 0.f));
		Steps.Add(Stance(TEXT("circle right"), 3.f, 0.f, 1.f));
		Steps.Add(Stance(TEXT("circle left"), 3.f, 0.f, -1.f));
		Steps.Add(Stance(TEXT("back off diagonally"), 2.5f, -0.7f, 0.7f));
		Steps.Add(Stance(TEXT("back off, the player's camera"), 2.5f, -1.f, 0.f, ECSReviewCamera::Player));
		return Steps;
	}

	/** Counts the run's punches over a step into the notes (thrown, landed, the dummy's last hit). */
	void NotePunches(const FCtx& C, int32 ThrownBefore, int32 LandedBefore, double StartTime)
	{
		const UCSRunDirector* Director = C.Mode ? C.Mode->GetRunDirector() : nullptr;
		if (!Director)
		{
			return;
		}
		const FCSRunStats& Stats = Director->GetStats();
		const int32 Thrown = Stats.PunchesThrown - ThrownBefore;
		const int32 Landed = Stats.PunchesLanded - LandedBefore;
		C.Note(TEXT("punches"), FString::Printf(TEXT("%d of %d landed"), Landed, Thrown));
		if (const ACSTargetDummy* Target = C.Dummy())
		{
			const FCSContactRecord& Hit = Target->GetPhysicsBody()->GetLastContact();
			if (Hit.Time >= StartTime)
			{
				C.Note(TEXT("last hit"), FString::Printf(TEXT("%.0f cm/s on %s (%s)"), Hit.VelocityChange, *Hit.Bone.ToString(),
					Hit.Result == ECSContactResult::Ragdoll ? TEXT("knockdown") : Hit.Result == ECSContactResult::Stagger ? TEXT("stagger") : TEXT("no effect")));
			}
		}
		if (const ACSCharacter* Self = C.Player())
		{
			const UCSStrikeComponent* Strike = Self->GetStrike();
			C.Note(TEXT("target found"), Strike && Strike->GetTarget() ? TEXT("yes") : TEXT("no"));
		}
	}

	/** A punch case: stand at Distance and Side from the dummy (body turned by BodyYaw), then punch at each of the times. */
	FCSPlaytestStep Punch(const TCHAR* Label, float Distance, float Side, float BodyYaw, TArray<float> Times, ECSReviewCamera Camera = ECSReviewCamera::Side,
		bool bFirstPerson = false, float Forward = 0.f)
	{
		FCSPlaytestStep Step;
		Step.Label = Label;
		Step.Camera = Camera;
		Step.Seconds = (Times.IsEmpty() ? 0.f : Times.Last()) + 1.2f;
		struct FCounts { int32 Thrown = 0; int32 Landed = 0; double Start = 0.0; };
		TSharedRef<FCounts> Counts = MakeShared<FCounts>();
		Step.OnStart = [Distance, Side, BodyYaw, bFirstPerson, Counts](const FCtx& C)
		{
			C.PlaceAtDummy(Distance, Side, BodyYaw);
			if (ACSCharacter* Self = C.Player())
			{
				Self->SetGait(ECSGait::Jog);
				Self->GetCameraRig()->SetView(bFirstPerson ? ECSView::FirstPerson : ECSView::ThirdPerson);
			}
			const UCSRunDirector* Director = C.Mode ? C.Mode->GetRunDirector() : nullptr;
			Counts->Thrown = Director ? Director->GetStats().PunchesThrown : 0;
			Counts->Landed = Director ? Director->GetStats().PunchesLanded : 0;
			Counts->Start = C.World->GetTimeSeconds();
		};
		Step.OnTick = [Times, Forward](const FCtx& C, float Time, float DeltaTime)
		{
			if (Times.Num() > 1)
			{
				// A player throwing several punches keeps turning to the opponent as it staggers.
				C.AimAtDummy();
			}
			if (!FMath::IsNearlyZero(Forward) && Time < (Times.IsEmpty() ? 0.f : Times[0]) + 0.1f)
			{
				C.Move(Forward, 0.f);
			}
			for (const float At : Times)
			{
				if (Time >= At && Time - DeltaTime < At)
				{
					if (ACSCharacter* Self = C.Player())
					{
						Self->Attack();
					}
				}
			}
		};
		Step.OnEnd = [Counts](const FCtx& C)
		{
			NotePunches(C, Counts->Thrown, Counts->Landed, Counts->Start);
			if (ACSCharacter* Self = C.Player())
			{
				Self->GetCameraRig()->SetView(ECSView::ThirdPerson);
			}
		};
		return Step;
	}

	TArray<FCSPlaytestStep> BuildPunch()
	{
		TArray<FCSPlaytestStep> Steps;
		Steps.Add(SetUpRun(1, 250.f));
		Steps.Add(Punch(TEXT("punch from 70 cm"), 70.f, 0.f, 0.f, {0.6f}));
		Steps.Add(Punch(TEXT("punch from 100 cm"), 100.f, 0.f, 0.f, {0.6f}));
		Steps.Add(Punch(TEXT("punch from 150 cm: steps in"), 150.f, 0.f, 0.f, {0.6f}));
		Steps.Add(Punch(TEXT("punch, target 35 cm to the side"), 80.f, 35.f, 0.f, {0.6f}));
		Steps.Add(Punch(TEXT("punch with the body turned 90 degrees"), 80.f, 0.f, 90.f, {0.3f}));
		Steps.Add(Punch(TEXT("punch, the player's camera"), 80.f, 0.f, 0.f, {0.6f}, ECSReviewCamera::Player));
		Steps.Add(Punch(TEXT("punch, first person"), 80.f, 0.f, 0.f, {0.6f}, ECSReviewCamera::Player, true));
		Steps.Add(Punch(TEXT("four punches in a row"), 80.f, 0.f, 0.f, {0.5f, 1.05f, 1.6f, 2.15f}));
		Steps.Add(Punch(TEXT("jog in and punch"), 320.f, 0.f, 0.f, {0.65f}, ECSReviewCamera::Side, false, 1.f));
		{
			FCSPlaytestStep Step;
			Step.Label = TEXT("the run ends: the results open a moment later");
			Step.Seconds = 3.f;
			Step.Camera = ECSReviewCamera::Player;
			TSharedRef<double> OpenedAt = MakeShared<double>(-1.0);
			TSharedRef<double> EndedAt = MakeShared<double>(0.0);
			Step.OnStart = [OpenedAt, EndedAt](const FCtx& C)
			{
				*OpenedAt = -1.0;
				*EndedAt = FPlatformTime::Seconds();
				if (UCSRunDirector* Director = C.Mode ? C.Mode->GetRunDirector() : nullptr)
				{
					Director->EndRun(ECSRunEnd::Manual);
				}
			};
			Step.OnTick = [OpenedAt, EndedAt](const FCtx& C, float Time, float)
			{
				const ACSPlayerController* PC = Cast<ACSPlayerController>(C.Controller);
				if (PC && PC->IsPauseScreenOpen() && *OpenedAt < 0.0)
				{
					*OpenedAt = FPlatformTime::Seconds() - *EndedAt;
					C.Note(TEXT("results opened after s"), float(*OpenedAt));
				}
			};
			Step.OnEnd = [](const FCtx& C)
			{
				if (ACSPlayerController* PC = Cast<ACSPlayerController>(C.Controller))
				{
					PC->SetPauseScreenOpen(false);
				}
			};
			Steps.Add(Step);
		}
		return Steps;
	}

	/** Watches a character's body through a step: whether it went down and when it was back up. */
	void WatchBody(const FCtx& C, const ACSCharacter* Who, float Time, TSharedRef<TPair<float, float>> DownUp)
	{
		const UCSPhysicsBodyComponent* Physics = Who ? Who->GetPhysicsBody() : nullptr;
		if (!Physics)
		{
			return;
		}
		const ECSBodyState State = Physics->GetState();
		if (State == ECSBodyState::Ragdoll && DownUp->Key < 0.f)
		{
			DownUp->Key = Time;
			C.Note(TEXT("down at s"), Time);
		}
		if (DownUp->Key >= 0.f && DownUp->Value < 0.f && State == ECSBodyState::Normal)
		{
			DownUp->Value = Time;
			C.Note(TEXT("back up at s"), Time);
		}
	}

	TArray<FCSPlaytestStep> BuildKnockdown()
	{
		TArray<FCSPlaytestStep> Steps;
		Steps.Add(SetUpRun(1, 300.f));
		{
			FCSPlaytestStep Step;
			Step.Label = TEXT("a heavy ball knocks the dummy down; it gets up");
			Step.Seconds = 7.f;
			Step.Camera = ECSReviewCamera::SideOfDummy;
			TSharedRef<TPair<float, float>> DownUp = MakeShared<TPair<float, float>>(-1.f, -1.f);
			Step.OnStart = [DownUp](const FCtx& C)
			{
				*DownUp = TPair<float, float>(-1.f, -1.f);
				C.PlaceAtDummy(300.f, 0.f);
			};
			Step.OnTick = [DownUp](const FCtx& C, float Time, float DeltaTime)
			{
				ACSCharacter* Self = C.Player();
				const ACSTargetDummy* Target = C.Dummy();
				if (Self && Target && Time >= 0.6f && Time - DeltaTime < 0.6f)
				{
					const FVector From = Self->GetActorLocation() + FVector(0.f, 0.f, 40.f);
					const FVector Chest = Target->GetMesh()->GetSocketLocation(TEXT("spine_03"));
					const FVector Along = (Chest - From).GetSafeNormal();
					// Just over the knockdown threshold: the dummy goes down where it stands instead of flying off.
					ACSHitBall::Throw(C.World, Self, From + Along * 80.f, Along, 25.f, 2200.f);
				}
				WatchBody(C, Target, Time, DownUp);
			};
			Steps.Add(Step);
		}
		auto KnockPlayer = [](const TCHAR* Label, ECSReviewCamera Camera)
		{
			FCSPlaytestStep Step;
			Step.Label = Label;
			Step.Seconds = 6.f;
			Step.Camera = Camera;
			TSharedRef<TPair<float, float>> DownUp = MakeShared<TPair<float, float>>(-1.f, -1.f);
			Step.OnStart = [DownUp](const FCtx& C)
			{
				*DownUp = TPair<float, float>(-1.f, -1.f);
				C.PlaceAtDummy(450.f, 0.f);
			};
			Step.OnTick = [DownUp](const FCtx& C, float Time, float DeltaTime)
			{
				ACSCharacter* Self = C.Player();
				if (Self && Time >= 0.5f && Time - DeltaTime < 0.5f)
				{
					// A shove in the back, well over the knockdown threshold: the body falls forward.
					UCSPhysicsBodyComponent* Physics = Self->GetPhysicsBody();
					const float Size = Self->GetActiveTuning().RagdollImpulseThreshold * 1.6f;
					Physics->ApplyImpulseAtBone(TEXT("spine_03"), Self->GetActorForwardVector() * Size * Physics->GetBodyMass());
				}
				WatchBody(C, Self, Time, DownUp);
			};
			return Step;
		};
		Steps.Add(KnockPlayer(TEXT("the player is knocked down and gets up"), ECSReviewCamera::Side));
		Steps.Add(KnockPlayer(TEXT("the player is knocked down: from the front"), ECSReviewCamera::Front));
		Steps.Add(KnockPlayer(TEXT("the player is knocked down: the player's camera"), ECSReviewCamera::Player));
		return Steps;
	}
}

// ---------------------------------------------------------------- runner

UCSPlaytestRunner::UCSPlaytestRunner()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Before the characters, so the movement asked for is used this frame.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	// A step may open the pause screen (the results of a run); the script must go on past it.
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}

TArray<FString> UCSPlaytestRunner::ScenarioNames()
{
	return {TEXT("locomotion"), TEXT("stance"), TEXT("punch"), TEXT("knockdown")};
}

TArray<FCSPlaytestStep> UCSPlaytestRunner::BuildScenario(const FString& Name)
{
	using namespace CSPlaytestScenarios;
	if (Name == TEXT("locomotion")) { return BuildLocomotion(); }
	if (Name == TEXT("stance")) { return BuildStance(); }
	if (Name == TEXT("punch")) { return BuildPunch(); }
	if (Name == TEXT("knockdown")) { return BuildKnockdown(); }
	return {};
}

TArray<FString> UCSPlaytestRunner::ParseScenarios(const FString& Value, TArray<FString>& OutUnknown)
{
	TArray<FString> Out;
	TArray<FString> Parts;
	Value.ParseIntoArray(Parts, TEXT(","), true);
	const TArray<FString> Known = ScenarioNames();
	for (FString Part : Parts)
	{
		Part = Part.TrimStartAndEnd().ToLower();
		if (Part == TEXT("all"))
		{
			for (const FString& Name : Known)
			{
				Out.AddUnique(Name);
			}
		}
		else if (Known.Contains(Part))
		{
			Out.AddUnique(Part);
		}
		else if (!Part.IsEmpty())
		{
			OutUnknown.Add(Part);
		}
	}
	return Out;
}

FString UCSPlaytestRunner::MakeEventLine(const FString& Event, int32 Index, const FString& Label, const TMap<FString, FString>& Data, double RealSeconds)
{
	// JSON strings: only the backslash, the quote and control characters are escaped (FString's
	// own escaping also escapes the apostrophe, which JSON does not allow).
	auto Quote = [](const FString& Text)
	{
		FString Out = TEXT("\"");
		for (const TCHAR Char : Text)
		{
			switch (Char)
			{
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('"'): Out += TEXT("\\\""); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (Char < 0x20)
				{
					Out += FString::Printf(TEXT("\\u%04x"), int32(Char));
				}
				else
				{
					Out.AppendChar(Char);
				}
			}
		}
		return Out + TEXT("\"");
	};
	FString Line = FString::Printf(TEXT("{\"event\":%s,\"utc_ms\":%lld,\"t\":%.3f"), *Quote(Event),
		int64(FDateTime::UtcNow().ToUnixTimestampDecimal() * 1000.0), RealSeconds);
	if (Index >= 0)
	{
		Line += FString::Printf(TEXT(",\"index\":%d"), Index);
	}
	if (!Label.IsEmpty())
	{
		Line += TEXT(",\"label\":") + Quote(Label);
	}
	if (!Data.IsEmpty())
	{
		TArray<FString> Fields;
		for (const TPair<FString, FString>& Pair : Data)
		{
			Fields.Add(Quote(Pair.Key) + TEXT(":") + Quote(Pair.Value));
		}
		Line += TEXT(",\"data\":{") + FString::Join(Fields, TEXT(",")) + TEXT("}");
	}
	return Line + TEXT("}");
}

void UCSPlaytestRunner::BeginPlay()
{
	Super::BeginPlay();
	FString Value;
	// Not stopping at a comma: the value is a comma-separated list of scenarios.
	if (!FParse::Value(FCommandLine::Get(), TEXT("CSPlaytest="), Value, false))
	{
		SetComponentTickEnabled(false);
		return;
	}
	TArray<FString> Unknown;
	const TArray<FString> Scenarios = ParseScenarios(Value, Unknown);
	for (const FString& Name : Unknown)
	{
		UE_LOG(LogCombatSimulator, Warning, TEXT("UCSPlaytestRunner: no scenario %s (there are: %s)"), *Name, *FString::Join(ScenarioNames(), TEXT(", ")));
	}
	FString Path;
	if (!FParse::Value(FCommandLine::Get(), TEXT("CSPlaytestLog="), Path, false))
	{
		Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Playtest"), TEXT("events.jsonl"));
	}
	Begin(Scenarios, Path, true);
}

void UCSPlaytestRunner::Begin(const TArray<FString>& Scenarios, const FString& InLogPath, bool bInQuitWhenDone)
{
	Steps.Reset();
	for (const FString& Name : Scenarios)
	{
		TArray<FCSPlaytestStep> More = BuildScenario(Name);
		if (!More.IsEmpty())
		{
			// A heading step, so the log and the review say where each scenario starts.
			FCSPlaytestStep Heading;
			Heading.Seconds = 0.f;
			Heading.Camera = ECSReviewCamera::Player;
			Heading.OnStart = [Name](const FCSPlaytestContext& C) { C.Note(TEXT("scenario"), Name); };
			Steps.Add(Heading);
			Steps.Append(More);
		}
	}
	LogPath = InLogPath;
	bQuitWhenDone = bInQuitWhenDone;
	LogLines.Reset();
	StepIndex = INDEX_NONE;
	SegmentIndex = 0;
	WarmupSeconds = 2.f;
	StartRealSeconds = FPlatformTime::Seconds();
	if (!LogPath.IsEmpty())
	{
		IFileManager::Get().Delete(*LogPath, false, true, true);
	}
	bRunning = !Steps.IsEmpty();
	SetComponentTickEnabled(bRunning);
	if (!bRunning)
	{
		UE_LOG(LogCombatSimulator, Warning, TEXT("UCSPlaytestRunner: nothing to play"));
		Finish();
		return;
	}
	if (UWorld* World = GetWorld())
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		ReviewCamera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
		if (ReviewCamera)
		{
			ReviewCamera->GetCameraComponent()->SetFieldOfView(70.f);
			ReviewCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		}
	}
	HudHandle = AHUD::OnHUDPostRender.AddUObject(this, &UCSPlaytestRunner::DrawLabel);
	// Motion blur smears limbs in a recording and hides what is being reviewed.
	if (IConsoleVariable* MotionBlur = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality")))
	{
		MotionBlur->Set(0, ECVF_SetByCode);
	}
	Log(TEXT("start"), FString::Join(Scenarios, TEXT(",")), {});
	UE_LOG(LogCombatSimulator, Log, TEXT("UCSPlaytestRunner: playing %s (%d steps), log %s"), *FString::Join(Scenarios, TEXT(", ")), Steps.Num(), *LogPath);
}

void UCSPlaytestRunner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AHUD::OnHUDPostRender.Remove(HudHandle);
	HudHandle.Reset();
	if (ReviewCamera)
	{
		ReviewCamera->Destroy();
		ReviewCamera = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

FCSPlaytestContext UCSPlaytestRunner::MakeContext() const
{
	FCSPlaytestContext Context;
	Context.Runner = const_cast<UCSPlaytestRunner*>(this);
	Context.World = GetWorld();
	Context.Mode = Cast<ACSGameMode>(GetOwner());
	Context.Controller = Context.World ? Context.World->GetFirstPlayerController() : nullptr;
	return Context;
}

void UCSPlaytestRunner::Log(const FString& Event, const FString& Label, const TMap<FString, FString>& Data)
{
	const int32 Index = Event == TEXT("segment") || Event == TEXT("result") ? SegmentIndex : -1;
	const FString Line = MakeEventLine(Event, Index, Label, Data, FPlatformTime::Seconds() - StartRealSeconds);
	LogLines.Add(Line);
	if (!LogPath.IsEmpty())
	{
		FFileHelper::SaveStringToFile(Line + TEXT("\n"), *LogPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}
}

void UCSPlaytestRunner::StartStep(int32 Index)
{
	StepIndex = Index;
	StepSeconds = 0.f;
	Notes.Reset();
	const FCSPlaytestStep& Step = Steps[Index];
	const FCSPlaytestContext Context = MakeContext();
	CameraMode = Step.Camera;
	bCameraPlaced = false;
	if (Context.Controller)
	{
		CameraSide = FRotationMatrix(FRotator(0.f, Context.Controller->GetControlRotation().Yaw, 0.f)).GetUnitAxis(EAxis::Y);
	}
	if (!Step.Label.IsEmpty())
	{
		++SegmentIndex;
		CurrentLabel = Step.Label;
		const TCHAR* Camera = Step.Camera == ECSReviewCamera::Side ? TEXT("side") : Step.Camera == ECSReviewCamera::Front ? TEXT("front")
			: Step.Camera == ECSReviewCamera::SideOfDummy ? TEXT("side of the dummy") : TEXT("player");
		Log(TEXT("segment"), Step.Label, {{TEXT("camera"), Camera}});
		UE_LOG(LogCombatSimulator, Log, TEXT("UCSPlaytestRunner: %d %s"), SegmentIndex, *Step.Label);
	}
	if (Step.OnStart)
	{
		Step.OnStart(Context);
	}
}

void UCSPlaytestRunner::EndStep()
{
	const FCSPlaytestStep& Step = Steps[StepIndex];
	if (Step.OnEnd)
	{
		Step.OnEnd(MakeContext());
	}
	if (!Step.Label.IsEmpty())
	{
		Log(TEXT("result"), Step.Label, Notes);
		TArray<FString> Parts;
		for (const TPair<FString, FString>& Pair : Notes)
		{
			Parts.Add(Pair.Key + TEXT(": ") + Pair.Value);
		}
		LastResult = Parts.IsEmpty() ? FString() : Step.Label + TEXT(" -> ") + FString::Join(Parts, TEXT(", "));
	}
	else if (!Notes.IsEmpty())
	{
		Log(TEXT("note"), FString(), Notes);
	}
}

void UCSPlaytestRunner::Finish()
{
	bRunning = false;
	Log(TEXT("end"), FString(), {});
	SetComponentTickEnabled(false);
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC)
	{
		PC->SetViewTarget(PC->GetPawn());
	}
	if (bQuitWhenDone && World)
	{
		UE_LOG(LogCombatSimulator, Log, TEXT("UCSPlaytestRunner: done; quitting"));
		UKismetSystemLibrary::QuitGame(World, PC, EQuitPreference::Quit, false);
	}
}

void UCSPlaytestRunner::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bRunning)
	{
		return;
	}
	if (WarmupSeconds > 0.f)
	{
		WarmupSeconds -= DeltaTime;
		return;
	}
	if (StepIndex == INDEX_NONE)
	{
		StartStep(0);
	}
	const FCSPlaytestStep* Step = &Steps[StepIndex];
	StepSeconds += DeltaTime;
	if (Step->OnTick)
	{
		Step->OnTick(MakeContext(), StepSeconds, DeltaTime);
	}
	UpdateCamera(DeltaTime);
	while (StepSeconds >= Step->Seconds)
	{
		EndStep();
		if (StepIndex + 1 >= Steps.Num())
		{
			Finish();
			return;
		}
		StartStep(StepIndex + 1);
		Step = &Steps[StepIndex];
		if (Step->Seconds > 0.f)
		{
			break;
		}
	}
}

void UCSPlaytestRunner::UpdateCamera(float DeltaTime)
{
	const FCSPlaytestContext Context = MakeContext();
	APlayerController* PC = Context.Controller;
	ACSCharacter* Self = Context.Player();
	if (!PC || !Self)
	{
		return;
	}
	if (CameraMode == ECSReviewCamera::Player || !ReviewCamera)
	{
		if (PC->GetViewTarget() != Self)
		{
			PC->SetViewTarget(Self);
		}
		return;
	}
	// Frame the player and, when it is near, the dummy: the point between them, far enough back
	// to see both whole. The body's own position (the pelvis) is followed, so a ragdoll stays in view.
	FVector Focus = Self->GetMesh()->GetSocketLocation(TEXT("pelvis"));
	float Distance = 420.f;
	const ACSTargetDummy* FollowDummy = CameraMode == ECSReviewCamera::SideOfDummy ? Context.Dummy() : nullptr;
	if (FollowDummy)
	{
		Focus = FollowDummy->GetMesh()->GetSocketLocation(TEXT("pelvis"));
		Distance = 480.f;
	}
	else if (const ACSTargetDummy* Target = Context.Dummy())
	{
		const FVector Other = Target->GetMesh()->GetSocketLocation(TEXT("pelvis"));
		const float Apart = float(FVector::Dist2D(Other, Focus));
		if (Apart < 500.f)
		{
			Focus = (Focus + Other) * 0.5;
			Distance += Apart * 0.6f;
		}
	}
	Focus.Z += 10.f;
	FVector Wanted;
	if (CameraMode == ECSReviewCamera::Front)
	{
		const FVector Forward = FRotationMatrix(FRotator(0.f, PC->GetControlRotation().Yaw, 0.f)).GetUnitAxis(EAxis::X);
		Wanted = Focus + Forward * Distance + FVector(0.f, 0.f, 35.f);
	}
	else
	{
		Wanted = Focus + CameraSide * Distance + FVector(0.f, 0.f, 35.f);
	}
	const FVector Current = ReviewCamera->GetActorLocation();
	const float Alpha = bCameraPlaced ? 1.f - FMath::Exp(-DeltaTime / 0.15f) : 1.f;
	const FVector Location = FMath::Lerp(Current, Wanted, Alpha);
	ReviewCamera->SetActorLocationAndRotation(Location, (Focus - Location).Rotation());
	bCameraPlaced = true;
	if (PC->GetViewTarget() != ReviewCamera)
	{
		PC->SetViewTarget(ReviewCamera);
	}
}

void UCSPlaytestRunner::DrawLabel(AHUD* HUD, UCanvas* Canvas)
{
	if (!bRunning || !HUD || !Canvas || HUD->GetWorld() != GetWorld() || !GEngine || CurrentLabel.IsEmpty())
	{
		return;
	}
	const UFont* Large = GEngine->GetLargeFont();
	const UFont* Small = GEngine->GetSmallFont();
	const FString Title = FString::Printf(TEXT("PLAYTEST %d  %s"), SegmentIndex, *CurrentLabel);
	// Scaled with the screen, so it can be read in a contact sheet's small frames.
	const float Scale = FMath::Max(1.f, Canvas->ClipY / 450.f);
	auto DrawScaled = [Canvas, Scale](const UFont* Font, const FString& Text, float X, float Y, const FColor& Colour)
	{
		Canvas->SetDrawColor(FColor::Black);
		Canvas->DrawText(Font, Text, X + 2.f, Y + 2.f, Scale, Scale);
		Canvas->SetDrawColor(Colour);
		Canvas->DrawText(Font, Text, X, Y, Scale, Scale);
	};
	DrawScaled(Large, Title, 24.f, Canvas->ClipY - 40.f * Scale - 30.f, FColor(255, 255, 255));
	if (!LastResult.IsEmpty())
	{
		DrawScaled(Small, LastResult, 24.f, Canvas->ClipY - 14.f * Scale - 16.f, FColor(200, 230, 255));
	}
}
