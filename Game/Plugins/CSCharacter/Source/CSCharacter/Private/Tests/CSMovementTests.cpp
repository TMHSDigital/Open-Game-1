// Movement feel tests for ACSCharacter (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Movement).
//
// Every test builds its own small game world (a flat or tilted floor box and one character), ticks
// it at a fixed frame rate with scripted input, and measures what the character does. No assets
// are loaded: the tuning is a transient UCSMovementTuning, so the tests prove the code applies
// whatever values the asset holds.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CSCharacter.h"
#include "CSMovementTuning.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/AutomationCommon.h"

#include <limits>

namespace CSMovementTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	// A test world with a 200 m floor box (optionally tilted by FloorPitchDeg, rising toward +X)
	// and one ACSCharacter standing on it at the origin. Ticked manually at a fixed step.
	struct FMoveSim
	{
		FTestWorldWrapper World;
		ACSCharacter* Character = nullptr;
		float Dt = 1.f / 60.f;
		float Time = 0.f;

		bool Init(FAutomationTestBase& Test, float InDt, UCSMovementTuning* Tuning, float FloorPitchDeg = 0.f)
		{
			Dt = InDt;
			if (!World.CreateTestWorld(EWorldType::Game) || !World.BeginPlayInTestWorld())
			{
				World.ForwardErrorMessages(&Test);
				return false;
			}
			UWorld* W = World.GetTestWorld();

			AActor* Floor = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
			UBoxComponent* Box = NewObject<UBoxComponent>(Floor, TEXT("FloorBox"));
			Box->SetBoxExtent(FVector(10000.f, 10000.f, 50.f));
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Floor->SetRootComponent(Box);
			Box->RegisterComponent();
			// Top face at z = 0 at the origin.
			Box->SetWorldLocationAndRotation(FVector(0.f, 0.f, -50.f), FRotator(FloorPitchDeg, 0.f, 0.f));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.bDeferConstruction = true;
			const FTransform Spawn(FVector(0.f, 0.f, 120.f));
			Character = W->SpawnActor<ACSCharacter>(ACSCharacter::StaticClass(), Spawn, Params);
			if (!Character)
			{
				Test.AddError(TEXT("Could not spawn ACSCharacter"));
				return false;
			}
			Character->Tuning = Tuning;
			Character->FinishSpawning(Spawn);
			// No controller in a test world; let the movement component simulate anyway.
			// Possession would normally pick the default movement mode (Restart); do it by hand.
			Character->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			Character->GetCharacterMovement()->SetDefaultMovementMode();

			// Settle onto the floor.
			Run(FVector::ZeroVector, 0.5f);
			if (!Character->GetCharacterMovement()->IsMovingOnGround())
			{
				const UCharacterMovementComponent* M = Character->GetCharacterMovement();
				Test.AddError(FString::Printf(TEXT("Character did not land on the test floor (at %s, mode %d, velocity %s, begun play %d, ticked %.2f s)"),
					*Character->GetActorLocation().ToString(), int32(M->MovementMode.GetValue()), *M->Velocity.ToString(), int32(Character->HasActorBegunPlay()), Time));
				return false;
			}
			return true;
		}

		~FMoveSim()
		{
			if (World.GetTestWorld())
			{
				World.DestroyTestWorld(false);
			}
		}

		void Step(const FVector& Input)
		{
			if (!Input.IsNearlyZero())
			{
				Character->AddMovementInput(Input, 1.f);
			}
			World.TickTestWorld(Dt);
			Time += Dt;
		}

		void Run(const FVector& Input, float Seconds)
		{
			const int32 Steps = FMath::CeilToInt(Seconds / Dt);
			for (int32 i = 0; i < Steps; ++i)
			{
				Step(Input);
			}
		}

		float Speed2D() const { return Character->GetCharacterMovement()->Velocity.Size2D(); }
	};

	UCSMovementTuning* NewTuning()
	{
		return NewObject<UCSMovementTuning>(GetTransientPackage());
	}

	// Time from switching jog to sprint until the speed reaches 95 percent of sprint speed.
	float MeasureSprintRamp(FAutomationTestBase& Test, float Dt, const UCSMovementTuning& Values)
	{
		UCSMovementTuning* Tuning = NewTuning();
		Tuning->SprintRampSeconds = Values.SprintRampSeconds;
		FMoveSim Sim;
		if (!Sim.Init(Test, Dt, Tuning))
		{
			return -1.f;
		}
		Sim.Character->SetGait(ECSGait::Jog);
		Sim.Run(FVector::ForwardVector, 2.f);
		Test.TestNearlyEqual(TEXT("Steady jog speed before sprinting"), Sim.Speed2D(), Tuning->JogSpeed, 2.f);

		Sim.Character->SetGait(ECSGait::Sprint);
		const float Target = 0.95f * Tuning->SprintSpeed;
		float Elapsed = 0.f;
		while (Elapsed < 5.f)
		{
			Sim.Step(FVector::ForwardVector);
			Elapsed += Dt;
			if (Sim.Speed2D() >= Target)
			{
				return Elapsed;
			}
		}
		return -1.f;
	}

	// Jogs, is lifted by HeightCm (teleport, still jogging forward), and returns the lowest
	// horizontal speed in the 0.3 s after landing divided by the speed before the fall.
	float MeasureLandingKeep(FAutomationTestBase& Test, float HeightCm)
	{
		UCSMovementTuning* Tuning = NewTuning();
		FMoveSim Sim;
		if (!Sim.Init(Test, 1.f / 60.f, Tuning))
		{
			return -1.f;
		}
		Sim.Character->SetGait(ECSGait::Jog);
		Sim.Run(FVector::ForwardVector, 1.5f);
		const float Before = Sim.Speed2D();

		Sim.Character->SetActorLocation(Sim.Character->GetActorLocation() + FVector(0.f, 0.f, HeightCm), false, nullptr, ETeleportType::TeleportPhysics);
		UCharacterMovementComponent* Move = Sim.Character->GetCharacterMovement();
		bool bWasFalling = false;
		float Elapsed = 0.f;
		while (Elapsed < 5.f)
		{
			Sim.Step(FVector::ForwardVector);
			Elapsed += Sim.Dt;
			bWasFalling |= Move->IsFalling();
			if (bWasFalling && Move->IsMovingOnGround())
			{
				break;
			}
		}
		if (!bWasFalling || !Move->IsMovingOnGround())
		{
			Test.AddError(FString::Printf(TEXT("No fall and landing from %.0f cm"), HeightCm));
			return -1.f;
		}
		float Lowest = Sim.Speed2D();
		for (float T = 0.f; T < 0.3f; T += Sim.Dt)
		{
			Sim.Step(FVector::ForwardVector);
			Lowest = FMath::Min(Lowest, Sim.Speed2D());
		}
		return Before > 0.f ? Lowest / Before : -1.f;
	}
}

using namespace CSMovementTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMovementTuningAppliedTest, "CombatSimulator.CS.Movement.TuningApplied", TestFlags)
bool FCSMovementTuningAppliedTest::RunTest(const FString& Parameters)
{
	// Values different from the defaults, so the test proves they come from the asset.
	UCSMovementTuning* Tuning = NewTuning();
	Tuning->WalkSpeed = 150.f;
	Tuning->JogSpeed = 300.f;
	Tuning->SprintSpeed = 500.f;
	Tuning->CrouchSpeed = 120.f;
	Tuning->MaxWalkableSlopeDeg = 40.f;
	Tuning->AirControl = 0.3f;
	Tuning->BrakingFriction = 1.5f;

	FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, Tuning))
	{
		return false;
	}
	UCharacterMovementComponent* Move = Sim.Character->GetCharacterMovement();

	TestNearlyEqual(TEXT("Walkable slope"), Move->GetWalkableFloorAngle(), 40.f, 0.01f);
	TestNearlyEqual(TEXT("Air control"), Move->AirControl, 0.3f, 1e-4f);
	TestNearlyEqual(TEXT("Braking friction"), Move->BrakingFriction, 1.5f, 1e-4f);
	TestTrue(TEXT("Separate braking friction is used"), Move->bUseSeparateBrakingFriction);
	TestNearlyEqual(TEXT("Crouch speed"), Move->MaxWalkSpeedCrouched, 120.f, 0.01f);

	Sim.Character->SetGait(ECSGait::Walk);
	Sim.Step(FVector::ZeroVector);
	TestEqual(TEXT("Gait is walk"), Sim.Character->GetGait(), ECSGait::Walk);
	TestNearlyEqual(TEXT("Walk speed"), Move->MaxWalkSpeed, 150.f, 0.01f);

	Sim.Character->SetGait(ECSGait::Jog);
	Sim.Step(FVector::ZeroVector);
	TestEqual(TEXT("Gait is jog"), Sim.Character->GetGait(), ECSGait::Jog);
	TestNearlyEqual(TEXT("Jog speed"), Move->MaxWalkSpeed, 300.f, 0.01f);

	Sim.Character->SetGait(ECSGait::Crouch);
	Sim.Run(FVector::ZeroVector, 0.2f);
	TestEqual(TEXT("Gait is crouch"), Sim.Character->GetGait(), ECSGait::Crouch);
	TestTrue(TEXT("Crouch gait crouches the character"), Sim.Character->bIsCrouched);
	Sim.Run(FVector::ForwardVector, 2.f);
	TestNearlyEqual(TEXT("Crouched top speed"), Sim.Speed2D(), 120.f, 1.f);

	// Sprint ramps up to its speed rather than snapping, so hold it long enough to arrive.
	Sim.Character->SetGait(ECSGait::Sprint);
	Sim.Run(FVector::ForwardVector, 3.f);
	TestEqual(TEXT("Gait is sprint"), Sim.Character->GetGait(), ECSGait::Sprint);
	TestFalse(TEXT("Sprint stands the character up"), Sim.Character->bIsCrouched);
	TestNearlyEqual(TEXT("Sprint speed cap"), Move->MaxWalkSpeed, 500.f, 0.5f);
	TestNearlyEqual(TEXT("Sprint top speed"), Sim.Speed2D(), 500.f, 1.f);

	// Swapping the asset and reapplying takes effect immediately.
	UCSMovementTuning* Other = NewTuning();
	Other->MaxWalkableSlopeDeg = 30.f;
	Sim.Character->Tuning = Other;
	Sim.Character->ApplyTuning();
	TestNearlyEqual(TEXT("Slope after reapply"), Move->GetWalkableFloorAngle(), 30.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMovementSprintRampTimeTest, "CombatSimulator.CS.Movement.SprintRampTime", TestFlags)
bool FCSMovementSprintRampTimeTest::RunTest(const FString& Parameters)
{
	// Review Focus 4: the time to sprint speed must not depend on the frame rate.
	const UCSMovementTuning* Defaults = GetDefault<UCSMovementTuning>();
	const float Ramp = Defaults->SprintRampSeconds;
	const float At30 = MeasureSprintRamp(*this, 1.f / 30.f, *Defaults);
	const float At144 = MeasureSprintRamp(*this, 1.f / 144.f, *Defaults);
	AddInfo(FString::Printf(TEXT("Jog to 95%% sprint: %.3f s at 30 fps, %.3f s at 144 fps (configured %.2f s)"), At30, At144, Ramp));
	TestTrue(TEXT("Reached sprint speed at 30 fps"), At30 > 0.f);
	TestTrue(TEXT("Reached sprint speed at 144 fps"), At144 > 0.f);
	TestNearlyEqual(TEXT("Ramp time at 30 fps"), At30, Ramp, 0.1f);
	TestNearlyEqual(TEXT("Ramp time at 144 fps"), At144, Ramp, 0.1f);

	// A different configured time is honoured too.
	UCSMovementTuning* Slow = NewTuning();
	Slow->SprintRampSeconds = 1.2f;
	const float Slow60 = MeasureSprintRamp(*this, 1.f / 60.f, *Slow);
	AddInfo(FString::Printf(TEXT("Configured 1.2 s: %.3f s at 60 fps"), Slow60));
	TestNearlyEqual(TEXT("Ramp time follows SprintRampSeconds"), Slow60, 1.2f, 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMovementJumpHeightTest, "CombatSimulator.CS.Movement.JumpHeight", TestFlags)
bool FCSMovementJumpHeightTest::RunTest(const FString& Parameters)
{
	for (const float Dt : {1.f / 30.f, 1.f / 144.f})
	{
		UCSMovementTuning* Tuning = NewTuning();
		FMoveSim Sim;
		if (!Sim.Init(*this, Dt, Tuning))
		{
			return false;
		}
		const float StartZ = Sim.Character->GetActorLocation().Z;
		float ApexZ = StartZ;
		Sim.Character->Jump();
		for (float T = 0.f; T < 1.5f; T += Dt)
		{
			Sim.Step(FVector::ZeroVector);
			ApexZ = FMath::Max(ApexZ, Sim.Character->GetActorLocation().Z);
		}
		Sim.Character->StopJumping();
		const float Height = ApexZ - StartZ;
		AddInfo(FString::Printf(TEXT("Jump apex %.2f cm at %.0f fps"), Height, 1.f / Dt));
		TestNearlyEqual(FString::Printf(TEXT("Jump height at %.0f fps"), 1.f / Dt), Height, Tuning->JumpHeightCm, 5.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMovementBrakingSlidesTest, "CombatSimulator.CS.Movement.BrakingSlides", TestFlags)
bool FCSMovementBrakingSlidesTest::RunTest(const FString& Parameters)
{
	UCSMovementTuning* Tuning = NewTuning();
	FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, Tuning))
	{
		return false;
	}
	Sim.Character->SetGait(ECSGait::Sprint);
	Sim.Run(FVector::ForwardVector, 3.f);
	TestNearlyEqual(TEXT("At sprint speed before stopping"), Sim.Speed2D(), Tuning->SprintSpeed, 2.f);

	const FVector Start = Sim.Character->GetActorLocation();
	float Elapsed = 0.f;
	while (Sim.Speed2D() > 1.f && Elapsed < 5.f)
	{
		Sim.Step(FVector::ZeroVector);
		Elapsed += Sim.Dt;
	}
	const float Slide = FVector::Dist2D(Start, Sim.Character->GetActorLocation());
	AddInfo(FString::Printf(TEXT("Stopped from sprint in %.2f s over %.1f cm"), Elapsed, Slide));
	TestTrue(TEXT("Came to a stop"), Sim.Speed2D() <= 1.f);
	TestTrue(TEXT("Braking leaves a slide (more than 20 cm)"), Slide > 20.f);
	TestTrue(TEXT("Slide is short (less than 200 cm)"), Slide < 200.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMovementLandingCostsSpeedTest, "CombatSimulator.CS.Movement.LandingCostsSpeed", TestFlags)
bool FCSMovementLandingCostsSpeedTest::RunTest(const FString& Parameters)
{
	const float KeepHigh = MeasureLandingKeep(*this, 300.f);
	const float KeepLow = MeasureLandingKeep(*this, 50.f);
	AddInfo(FString::Printf(TEXT("Horizontal speed kept after landing: %.0f%% from 3 m, %.0f%% from 0.5 m"), KeepHigh * 100.f, KeepLow * 100.f));
	TestTrue(TEXT("Landing from 3 m costs speed (keeps under 80 percent)"), KeepHigh > 0.f && KeepHigh < 0.8f);
	TestTrue(TEXT("Landing from 0.5 m barely does (keeps 95 percent or more)"), KeepLow >= 0.95f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMovementSlopeChangesSpeedTest, "CombatSimulator.CS.Movement.SlopeChangesSpeed", TestFlags)
bool FCSMovementSlopeChangesSpeedTest::RunTest(const FString& Parameters)
{
	// Spec 5.2: uphill slows, downhill speeds up slightly. The floor rises toward +X by 20 degrees.
	float Speeds[2] = {0.f, 0.f};
	const FVector Directions[2] = {FVector::ForwardVector, -FVector::ForwardVector};
	for (int32 i = 0; i < 2; ++i)
	{
		UCSMovementTuning* Tuning = NewTuning();
		FMoveSim Sim;
		if (!Sim.Init(*this, 1.f / 60.f, Tuning, 20.f))
		{
			return false;
		}
		Sim.Character->SetGait(ECSGait::Jog);
		Sim.Run(Directions[i], 1.5f);
		TestTrue(TEXT("Still walking on the slope"), Sim.Character->GetCharacterMovement()->IsMovingOnGround());
		Speeds[i] = Sim.Speed2D();
	}
	const float Jog = GetDefault<UCSMovementTuning>()->JogSpeed;
	AddInfo(FString::Printf(TEXT("Jog on a 20 degree slope: %.0f cm/s uphill, %.0f cm/s downhill (flat %.0f)"), Speeds[0], Speeds[1], Jog));
	TestTrue(TEXT("Uphill is slower than flat"), Speeds[0] < 0.95f * Jog);
	TestTrue(TEXT("Downhill is a little faster than flat"), Speeds[1] > 1.01f * Jog && Speeds[1] < 1.15f * Jog);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMovementTuningValidationTest, "CombatSimulator.CS.Movement.TuningValidation", TestFlags)
bool FCSMovementTuningValidationTest::RunTest(const FString& Parameters)
{
	FString Reason;
	TestTrue(TEXT("Defaults are valid"), GetDefault<UCSMovementTuning>()->IsValid(Reason));

	auto ExpectInvalid = [this](const TCHAR* What, TFunctionRef<void(UCSMovementTuning&)> Break)
	{
		UCSMovementTuning* T = NewTuning();
		Break(*T);
		FString Why;
		const bool bValid = T->IsValid(Why);
		TestFalse(FString::Printf(TEXT("Rejects %s"), What), bValid);
		TestFalse(FString::Printf(TEXT("Gives a reason for %s"), What), Why.IsEmpty());
	};
	ExpectInvalid(TEXT("zero walk speed"), [](UCSMovementTuning& T) { T.WalkSpeed = 0.f; });
	ExpectInvalid(TEXT("negative jog speed"), [](UCSMovementTuning& T) { T.JogSpeed = -10.f; });
	ExpectInvalid(TEXT("zero sprint speed"), [](UCSMovementTuning& T) { T.SprintSpeed = 0.f; });
	ExpectInvalid(TEXT("negative crouch speed"), [](UCSMovementTuning& T) { T.CrouchSpeed = -1.f; });
	ExpectInvalid(TEXT("sprint slower than walk"), [](UCSMovementTuning& T) { T.SprintSpeed = 100.f; });
	ExpectInvalid(TEXT("zero sprint ramp"), [](UCSMovementTuning& T) { T.SprintRampSeconds = 0.f; });
	ExpectInvalid(TEXT("zero camera blend"), [](UCSMovementTuning& T) { T.CameraBlendSeconds = 0.f; });
	ExpectInvalid(TEXT("negative ragdoll recovery"), [](UCSMovementTuning& T) { T.RagdollRecoverSeconds = -0.5f; });
	ExpectInvalid(TEXT("slope of 0"), [](UCSMovementTuning& T) { T.MaxWalkableSlopeDeg = 0.f; });
	ExpectInvalid(TEXT("slope of 90"), [](UCSMovementTuning& T) { T.MaxWalkableSlopeDeg = 90.f; });
	ExpectInvalid(TEXT("third-person FOV below 60"), [](UCSMovementTuning& T) { T.ThirdPersonFOV = 59.f; });
	ExpectInvalid(TEXT("first-person FOV above 120"), [](UCSMovementTuning& T) { T.FirstPersonFOV = 121.f; });
	ExpectInvalid(TEXT("NaN jump height"), [](UCSMovementTuning& T) { T.JumpHeightCm = std::numeric_limits<float>::quiet_NaN(); });

	UCSMovementTuning* Edge = NewTuning();
	Edge->ThirdPersonFOV = 60.f;
	Edge->FirstPersonFOV = 120.f;
	TestTrue(TEXT("FOV limits 60 and 120 are allowed"), Edge->IsValid(Reason));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
