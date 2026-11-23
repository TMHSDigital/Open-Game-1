// Camera rig tests for ACSCharacter (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Camera).
//
// Each test builds the shared test world (CSMoveSim.h), drives the camera key through
// ACSCharacter::ToggleCamera (the V handler) and reads the view the player would get from
// ACSCharacter::CalcCamera, the call the player camera manager makes. The ragdoll is driven through
// ACSCharacter::SetRagdolled, the contract Task 7's physics body fills in.
//
// The owner-only head hiding needs the base character's bones and eye socket, so
// CombatSimulator.CS.Camera.FirstPersonHidesHeadForOwnerOnly and EyeSocketOnHead live in the game
// module (Game/Source/CombatSimulator/Tests/CSCameraAssetTests.cpp).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "CSCameraRig.h"
#include "CSCharacter.h"
#include "CSMoveSim.h"
#include "CSDevTools.h"
#include "CSMovementTuning.h"
#include "GameFramework/PlayerController.h"

namespace CSCameraTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	using CSTestSupport::FMoveSim;
	using CSTestSupport::NewTuning;

	FMinimalViewInfo View(ACSCharacter& Character)
	{
		FMinimalViewInfo POV;
		Character.CalcCamera(0.f, POV);
		return POV;
	}

	// Distance from a point to the capsule's surface; negative inside.
	float CapsuleSignedDistance(const ACSCharacter& Character, const FVector& Point)
	{
		const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const float HalfSegment = Capsule->GetScaledCapsuleHalfHeight() - Radius;
		const FVector Centre = Capsule->GetComponentLocation();
		const FVector Up = Capsule->GetUpVector();
		const FVector A = Centre - Up * HalfSegment;
		const FVector B = Centre + Up * HalfSegment;
		return FMath::PointDistToSegment(Point, A, B) - Radius;
	}

	// Review Focus 2: the camera is never inside the visible body. It is either outside the capsule,
	// or close to the eyes with the head and neck hidden from the owner (the first-person position
	// and the last part of a blend toward it). Returns false on a failure.
	bool CheckCameraClear(FAutomationTestBase& Test, ACSCharacter& Character, const FString& When)
	{
		const UCSCameraRig* Rig = Character.GetCameraRig();
		const FVector Camera = View(Character).Location;
		const float CapsuleDistance = CapsuleSignedDistance(Character, Camera);
		if (CapsuleDistance >= 0.f)
		{
			return true;
		}
		const float EyeDistance = FVector::Dist(Camera, Rig->GetEyeLocation());
		if (Rig->IsHeadHiddenForOwner() && EyeDistance <= Rig->HeadHideRadius)
		{
			return true;
		}
		Test.AddError(FString::Printf(TEXT("%s: camera %.1f cm inside the capsule and %.1f cm from the eyes with the head %s"),
			*When, -CapsuleDistance, EyeDistance, Rig->IsHeadHiddenForOwner() ? TEXT("hidden") : TEXT("visible")));
		return false;
	}

	// Runs Seconds of simulation, checking the camera every frame.
	bool RunChecked(FAutomationTestBase& Test, FMoveSim& Sim, float Seconds, const FString& When)
	{
		const int32 Steps = FMath::CeilToInt(Seconds / Sim.Dt);
		for (int32 i = 0; i < Steps; ++i)
		{
			Sim.Step(FVector::ZeroVector);
			if (!CheckCameraClear(Test, *Sim.Character, FString::Printf(TEXT("%s, frame %d"), *When, i)))
			{
				return false;
			}
		}
		return true;
	}

	// A blend is complete one frame after CameraBlendSeconds.
	float BlendTime(const ACSCharacter& Character, float Dt)
	{
		return Character.GetActiveTuning().CameraBlendSeconds + Dt;
	}
}

using namespace CSCameraTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSCameraToggleSwitchesActiveCameraTest, "CombatSimulator.CS.Camera.ToggleSwitchesActiveCamera", CSCameraTests::TestFlags)
bool FCSCameraToggleSwitchesActiveCameraTest::RunTest(const FString& Parameters)
{
	for (const bool bCustom : {false, true})
	{
		UCSMovementTuning* Tuning = NewTuning();
		if (bCustom)
		{
			// Other values prove the rig reads the applied tuning, not constants.
			Tuning->ThirdPersonFOV = 70.f;
			Tuning->FirstPersonFOV = 95.f;
			Tuning->CameraBlendSeconds = 0.5f;
		}
		const FString Label = bCustom ? TEXT("custom tuning") : TEXT("default tuning");
		FMoveSim Sim;
		if (!Sim.Init(*this, 1.f / 60.f, Tuning))
		{
			return false;
		}
		ACSCharacter* Character = Sim.Character;
		UCSCameraRig* Rig = Character->GetCameraRig();
		if (!TestNotNull(TEXT("The character has a camera rig"), Rig))
		{
			return false;
		}
		const float TPFov = Tuning->ThirdPersonFOV;
		const float FPFov = Tuning->FirstPersonFOV;

		TestEqual(Label + TEXT(": starts in third person"), Rig->GetView(), ECSView::ThirdPerson);
		TestEqual(Label + TEXT(": active camera name"), Character->GetActiveCameraName(), FName(TEXT("ThirdPerson")));
		FMinimalViewInfo POV = View(*Character);
		TestNearlyEqual(Label + TEXT(": third-person FOV"), POV.FOV, TPFov, 0.01f);
		TestTrue(Label + TEXT(": third-person camera is behind the character"), CapsuleSignedDistance(*Character, POV.Location) > 100.f);
		TestTrue(Label + TEXT(": third-person view uses the engine's near plane"), POV.PerspectiveNearClipPlane <= 0.f);

		// V: first person, blended over CameraBlendSeconds.
		Character->ToggleCamera();
		TestEqual(Label + TEXT(": V chose first person"), Rig->GetView(), ECSView::FirstPerson);
		TestEqual(Label + TEXT(": first person is active at once"), Character->GetActiveCameraName(), FName(TEXT("FirstPerson")));
		Sim.Run(FVector::ZeroVector, Tuning->CameraBlendSeconds * 0.5f);
		POV = View(*Character);
		AddInfo(FString::Printf(TEXT("%s: FOV %.2f half way through the blend"), *Label, POV.FOV));
		TestTrue(Label + TEXT(": FOV is between the two half way through the blend"), POV.FOV > TPFov + 1.f && POV.FOV < FPFov - 1.f);
		Sim.Run(FVector::ZeroVector, BlendTime(*Character, Sim.Dt) - Tuning->CameraBlendSeconds * 0.5f);
		POV = View(*Character);
		TestNearlyEqual(Label + TEXT(": first-person FOV after the blend time"), POV.FOV, FPFov, 0.01f);
		TestNearlyEqual(Label + TEXT(": the camera component holds the same FOV"), Rig->GetCamera()->FieldOfView, FPFov, 0.01f);
		TestTrue(Label + TEXT(": first-person camera is at the eyes"), FVector::Dist(POV.Location, Rig->GetEyeLocation()) < 1.f);
		TestTrue(Label + TEXT(": first-person near plane is set and close"), POV.PerspectiveNearClipPlane > 0.f && POV.PerspectiveNearClipPlane <= 5.f);
		TestEqual(Label + TEXT(": one camera component is the view"), POV.Location, Rig->GetCamera()->GetComponentLocation());

		// V again: back to third person.
		Character->ToggleCamera();
		TestEqual(Label + TEXT(": V chose third person"), Rig->GetView(), ECSView::ThirdPerson);
		Sim.Run(FVector::ZeroVector, BlendTime(*Character, Sim.Dt));
		POV = View(*Character);
		TestNearlyEqual(Label + TEXT(": third-person FOV after the blend time"), POV.FOV, TPFov, 0.01f);
		TestEqual(Label + TEXT(": active camera name"), Character->GetActiveCameraName(), FName(TEXT("ThirdPerson")));
		TestTrue(Label + TEXT(": third-person camera is behind the character again"), CapsuleSignedDistance(*Character, POV.Location) > 100.f);

		// SetView and the extension point.
		int32 Requests = 0;
		Character->OnToggleCameraRequested.AddLambda([&Requests]() { ++Requests; });
		Character->ToggleCamera();
		TestEqual(Label + TEXT(": V still reaches OnToggleCameraRequested"), Requests, 1);
		Rig->SetView(ECSView::ThirdPerson);
		TestEqual(Label + TEXT(": SetView"), Rig->GetView(), ECSView::ThirdPerson);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSCameraKeepsAimAcrossSwitchTest, "CombatSimulator.CS.Camera.KeepsAimAcrossSwitch", CSCameraTests::TestFlags)
bool FCSCameraKeepsAimAcrossSwitchTest::RunTest(const FString& Parameters)
{
	FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, NewTuning()))
	{
		return false;
	}
	ACSCharacter* Character = Sim.Character;
	APlayerController* PC = Sim.World.GetTestWorld()->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Player controller"), PC))
	{
		return false;
	}
	PC->Possess(Character);
	const FRotator Aim(-25.f, 40.f, 0.f);
	PC->SetControlRotation(Aim);
	Sim.Run(FVector::ZeroVector, 0.3f);

	int32 Frames = 0;
	float WorstAngle = 0.f;
	float WorstRoll = 0.f;
	auto Watch = [&](float Seconds)
	{
		const int32 Steps = FMath::CeilToInt(Seconds / Sim.Dt);
		for (int32 i = 0; i < Steps; ++i)
		{
			Sim.Step(FVector::ZeroVector);
			const FMinimalViewInfo POV = View(*Character);
			WorstAngle = FMath::Max(WorstAngle, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(POV.Rotation.Vector() | Aim.Vector(), -1.f, 1.f))));
			WorstRoll = FMath::Max(WorstRoll, FMath::Abs(POV.Rotation.Roll));
			++Frames;
		}
	};
	Character->ToggleCamera();
	Watch(0.4f);
	TestEqual(TEXT("In first person"), Character->GetCameraRig()->GetEffectiveView(), ECSView::FirstPerson);
	Character->ToggleCamera();
	Watch(0.4f);
	// Switch back half way through a blend too.
	Character->ToggleCamera();
	Watch(0.1f);
	Character->ToggleCamera();
	Watch(0.4f);

	AddInfo(FString::Printf(TEXT("%d frames: worst view angle from the aim %.4f deg, worst roll %.4f deg"), Frames, WorstAngle, WorstRoll));
	TestTrue(TEXT("Control rotation unchanged by the switches"), PC->GetControlRotation().Equals(Aim, 0.001f));
	TestTrue(TEXT("The view looks along the aim on every frame of every blend"), WorstAngle < 0.01f);
	TestTrue(TEXT("The view is never rolled"), WorstRoll < 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSCameraSwitchDuringRagdollUsesThirdPersonTest, "CombatSimulator.CS.Camera.SwitchDuringRagdollUsesThirdPerson", CSCameraTests::TestFlags)
bool FCSCameraSwitchDuringRagdollUsesThirdPersonTest::RunTest(const FString& Parameters)
{
	// Review Focus 2: first person is never active during a ragdoll, whatever V does.
	FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, NewTuning()))
	{
		return false;
	}
	ACSCharacter* Character = Sim.Character;
	UCSCameraRig* Rig = Character->GetCameraRig();
	const float TPFov = Character->GetActiveTuning().ThirdPersonFOV;

	int32 Frames = 0;
	int32 FirstPersonFrames = 0;
	int32 RisingFrames = 0;
	auto WatchRagdoll = [&](float Seconds)
	{
		const int32 Steps = FMath::CeilToInt(Seconds / Sim.Dt);
		for (int32 i = 0; i < Steps; ++i)
		{
			const float Before = Rig->GetFirstPersonWeight();
			Sim.Step(FVector::ZeroVector);
			++Frames;
			if (Rig->GetEffectiveView() != ECSView::ThirdPerson || Character->GetActiveCameraName() != FName(TEXT("ThirdPerson")))
			{
				++FirstPersonFrames;
			}
			if (Rig->GetFirstPersonWeight() > Before)
			{
				++RisingFrames;
			}
		}
	};

	// Ragdoll from third person, then V: the choice is remembered, the view stays third person.
	Character->SetRagdolled(true);
	TestTrue(TEXT("Ragdolled"), Character->IsRagdolled());
	Character->ToggleCamera();
	TestEqual(TEXT("V during a ragdoll still records the choice"), Rig->GetView(), ECSView::FirstPerson);
	TestEqual(TEXT("V during a ragdoll keeps third person"), Rig->GetEffectiveView(), ECSView::ThirdPerson);
	WatchRagdoll(0.5f);
	TestNearlyEqual(TEXT("Third-person FOV during the ragdoll"), View(*Character).FOV, TPFov, 0.01f);
	TestFalse(TEXT("Head visible to the owner in third person"), Rig->IsHeadHiddenForOwner());
	CheckCameraClear(*this, *Character, TEXT("ragdoll after V"));
	Character->ToggleCamera();
	Character->ToggleCamera();
	WatchRagdoll(0.5f);
	Character->SetRagdolled(false);
	Sim.Run(FVector::ZeroVector, BlendTime(*Character, Sim.Dt));
	TestEqual(TEXT("Recovered into the chosen first person"), Rig->GetEffectiveView(), ECSView::FirstPerson);

	// Ragdoll while in first person: third person at once, blending out; V cannot bring first person back.
	Character->SetRagdolled(true);
	TestEqual(TEXT("A ragdoll leaves first person at once"), Rig->GetEffectiveView(), ECSView::ThirdPerson);
	TestEqual(TEXT("The chosen view is kept"), Rig->GetView(), ECSView::FirstPerson);
	WatchRagdoll(0.1f);
	Character->ToggleCamera();
	WatchRagdoll(0.05f);
	Character->ToggleCamera();
	WatchRagdoll(BlendTime(*Character, Sim.Dt));
	TestNearlyEqual(TEXT("Fully third person after the blend time"), Rig->GetFirstPersonWeight(), 0.f, 0.0001f);
	TestNearlyEqual(TEXT("Third-person FOV"), View(*Character).FOV, TPFov, 0.01f);
	TestTrue(TEXT("Third-person camera is outside the capsule"), CapsuleSignedDistance(*Character, View(*Character).Location) > 0.f);

	AddInfo(FString::Printf(TEXT("%d ragdoll frames: %d in first person, %d with the first-person weight rising"), Frames, FirstPersonFrames, RisingFrames));
	TestEqual(TEXT("No ragdoll frame is in first person"), FirstPersonFrames, 0);
	TestEqual(TEXT("No ragdoll frame moves toward first person"), RisingFrames, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSCameraReturnsToChosenViewAfterRecoveryTest, "CombatSimulator.CS.Camera.ReturnsToChosenViewAfterRecovery", CSCameraTests::TestFlags)
bool FCSCameraReturnsToChosenViewAfterRecoveryTest::RunTest(const FString& Parameters)
{
	// Review Focus 2: after recovery the view is the one the player chose, including a choice made
	// during the ragdoll.
	struct FCase
	{
		const TCHAR* What;
		ECSView Before;
		int32 PressesDuringRagdoll;
		ECSView After;
	};
	const FCase Cases[] = {
		{TEXT("first person, no key"), ECSView::FirstPerson, 0, ECSView::FirstPerson},
		{TEXT("third person, no key"), ECSView::ThirdPerson, 0, ECSView::ThirdPerson},
		{TEXT("first person, V once"), ECSView::FirstPerson, 1, ECSView::ThirdPerson},
		{TEXT("third person, V once"), ECSView::ThirdPerson, 1, ECSView::FirstPerson},
		{TEXT("first person, V twice"), ECSView::FirstPerson, 2, ECSView::FirstPerson},
	};
	for (const FCase& Case : Cases)
	{
		FMoveSim Sim;
		if (!Sim.Init(*this, 1.f / 60.f, NewTuning()))
		{
			return false;
		}
		ACSCharacter* Character = Sim.Character;
		UCSCameraRig* Rig = Character->GetCameraRig();
		const UCSMovementTuning& T = Character->GetActiveTuning();
		Rig->SetView(Case.Before);
		Sim.Run(FVector::ZeroVector, BlendTime(*Character, Sim.Dt));

		Character->SetRagdolled(true);
		Sim.Run(FVector::ZeroVector, 0.2f);
		for (int32 i = 0; i < Case.PressesDuringRagdoll; ++i)
		{
			Character->ToggleCamera();
			Sim.Run(FVector::ZeroVector, 0.1f);
		}
		Sim.Run(FVector::ZeroVector, 0.5f);
		TestEqual(FString::Printf(TEXT("%s: third person while ragdolled"), Case.What), Rig->GetEffectiveView(), ECSView::ThirdPerson);
		TestNearlyEqual(FString::Printf(TEXT("%s: third-person FOV while ragdolled"), Case.What), View(*Character).FOV, T.ThirdPersonFOV, 0.01f);

		Character->SetRagdolled(false);
		TestFalse(FString::Printf(TEXT("%s: recovered"), Case.What), Character->IsRagdolled());
		TestEqual(FString::Printf(TEXT("%s: chosen view on recovery"), Case.What), Rig->GetEffectiveView(), Case.After);
		Sim.Run(FVector::ZeroVector, BlendTime(*Character, Sim.Dt));
		const bool bFirst = Case.After == ECSView::FirstPerson;
		TestNearlyEqual(FString::Printf(TEXT("%s: FOV after recovery"), Case.What), View(*Character).FOV, bFirst ? T.FirstPersonFOV : T.ThirdPersonFOV, 0.01f);
		TestEqual(FString::Printf(TEXT("%s: head hidden from the owner only in first person"), Case.What), Rig->IsHeadHiddenForOwner(), bFirst);
		CheckCameraClear(*this, *Character, FString::Printf(TEXT("%s: after recovery"), Case.What));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSCameraSwitchMidAirAndCrouchedTest, "CombatSimulator.CS.Camera.SwitchMidAirAndCrouched", CSCameraTests::TestFlags)
bool FCSCameraSwitchMidAirAndCrouchedTest::RunTest(const FString& Parameters)
{
	// Review Focus 2: switching in the air or crouched never puts the camera inside the body, on any frame.
	FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, NewTuning()))
	{
		return false;
	}
	ACSCharacter* Character = Sim.Character;
	UCSCameraRig* Rig = Character->GetCameraRig();
	UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	const UCSMovementTuning& T = Character->GetActiveTuning();

	// Standing first-person eye height, for comparison with crouched.
	Rig->SetView(ECSView::FirstPerson);
	if (!RunChecked(*this, Sim, 0.5f, TEXT("standing, to first person")))
	{
		return false;
	}
	const float StandingEyeZ = Rig->GetEyeLocation().Z;

	// Mid-air: jump in first person, switch to third person on the way up and back on the way down.
	Character->Jump();
	Sim.Step(FVector::ZeroVector);
	Character->StopJumping();
	TestTrue(TEXT("Jumped"), Move->IsFalling());
	Character->ToggleCamera();
	if (!RunChecked(*this, Sim, 0.15f, TEXT("mid-air, to third person")))
	{
		return false;
	}
	Character->ToggleCamera();
	if (!RunChecked(*this, Sim, 0.1f, TEXT("mid-air, back to first person")))
	{
		return false;
	}
	TestTrue(TEXT("Still in the air after both switches"), Move->IsFalling());
	if (!RunChecked(*this, Sim, 1.f, TEXT("landing in first person")))
	{
		return false;
	}
	TestTrue(TEXT("Landed"), Move->IsMovingOnGround());
	TestNearlyEqual(TEXT("First-person FOV after landing"), View(*Character).FOV, T.FirstPersonFOV, 0.01f);

	// Off a ledge in third person, switching while falling.
	Rig->SetView(ECSView::ThirdPerson);
	RunChecked(*this, Sim, 0.3f, TEXT("standing, to third person"));
	Character->LaunchCharacter(FVector(0.f, 0.f, 600.f), false, true);
	Sim.Step(FVector::ZeroVector);
	Character->ToggleCamera();
	if (!RunChecked(*this, Sim, 0.3f, TEXT("launched, to first person")))
	{
		return false;
	}
	Character->ToggleCamera();
	if (!RunChecked(*this, Sim, 1.5f, TEXT("falling, to third person")))
	{
		return false;
	}

	// Crouched: both directions, and the first-person eye drops with the crouch.
	Character->SetGait(ECSGait::Crouch);
	if (!RunChecked(*this, Sim, 0.5f, TEXT("crouching in third person")))
	{
		return false;
	}
	TestTrue(TEXT("Crouched"), Character->bIsCrouched);
	Character->ToggleCamera();
	if (!RunChecked(*this, Sim, 0.6f, TEXT("crouched, to first person")))
	{
		return false;
	}
	TestEqual(TEXT("Crouched in first person"), Rig->GetEffectiveView(), ECSView::FirstPerson);
	const float CrouchedEyeZ = Rig->GetEyeLocation().Z;
	AddInfo(FString::Printf(TEXT("First-person eye height above the floor: standing %.1f cm, crouched %.1f cm"), StandingEyeZ, CrouchedEyeZ));
	TestTrue(TEXT("The crouched eye is lower than the standing eye"), CrouchedEyeZ < StandingEyeZ - 10.f);
	TestTrue(TEXT("Crouched first-person camera is at the eyes"), FVector::Dist(View(*Character).Location, Rig->GetEyeLocation()) < 1.f);
	Character->ToggleCamera();
	if (!RunChecked(*this, Sim, 0.6f, TEXT("crouched, to third person")))
	{
		return false;
	}
	TestTrue(TEXT("Crouched third-person camera is outside the capsule"), CapsuleSignedDistance(*Character, View(*Character).Location) > 100.f);

	// Standing up in first person.
	Character->ToggleCamera();
	RunChecked(*this, Sim, 0.3f, TEXT("crouched, to first person again"));
	Character->SetGait(ECSGait::Jog);
	RunChecked(*this, Sim, 0.6f, TEXT("standing up in first person"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSCameraTuningReloadChangesRigTest, "CombatSimulator.CS.Camera.TuningReloadChangesRig", CSCameraTests::TestFlags)
bool FCSCameraTuningReloadChangesRigTest::RunTest(const FString& Parameters)
{
	// Spec 5.2: camera feel lives in the tuning asset; a reload (F5, cs.Tuning.Reload) changes the
	// rig at once, without a restart.
	UCSMovementTuning* Tuning = NewTuning();
	FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, Tuning))
	{
		return false;
	}
	ACSCharacter* Character = Sim.Character;
	UCSCameraRig* Rig = Character->GetCameraRig();
	const UCSSpringArmComponent* Arm = Rig->GetArm();
	auto CheckArm = [&](const TCHAR* When, const UCSMovementTuning& T)
	{
		TestNearlyEqual(FString::Printf(TEXT("%s: arm length"), When), Arm->TargetArmLength, T.ThirdPersonArmLength, 0.001f);
		TestTrue(FString::Printf(TEXT("%s: shoulder offset"), When), Arm->SocketOffset.Equals(T.ThirdPersonShoulderOffset, 0.001f));
		TestNearlyEqual(FString::Printf(TEXT("%s: lag speed"), When), Arm->CameraLagSpeed, T.ThirdPersonLagSpeed, 0.001f);
		TestNearlyEqual(FString::Printf(TEXT("%s: lag distance"), When), Arm->CameraLagMaxDistance, T.ThirdPersonLagMaxDistance, 0.001f);
		// Standing still the lag has settled: the camera is the arm's length and offset from the eyes.
		const float Expected = (FVector(-T.ThirdPersonArmLength, 0.f, 0.f) + T.ThirdPersonShoulderOffset).Size();
		TestNearlyEqual(FString::Printf(TEXT("%s: camera distance from the eyes"), When), float(FVector::Dist(View(*Character).Location, Rig->GetEyeLocation())), Expected, 1.f);
	};
	CheckArm(TEXT("Start"), *Tuning);

	Tuning->ThirdPersonArmLength = 400.f;
	Tuning->ThirdPersonShoulderOffset = FVector(0.f, -40.f, 25.f);
	Tuning->ThirdPersonLagSpeed = 6.f;
	Tuning->ThirdPersonLagMaxDistance = 30.f;
	Tuning->FirstPersonSmoothSeconds = 0.3f;
	Sim.Run(FVector::ZeroVector, 0.2f);
	TestNearlyEqual(TEXT("An edit does nothing until it is reloaded"), Arm->TargetArmLength, 260.f, 0.001f);
	FString Message;
	TestTrue(TEXT("Reload accepted"), UCSDevTools::ReloadTuning(*Character, Message));
	Sim.Run(FVector::ZeroVector, 2.f);
	CheckArm(TEXT("After reload"), *Tuning);
	TestNearlyEqual(TEXT("After reload: first-person smoothing in use"), Character->GetActiveTuning().FirstPersonSmoothSeconds, 0.3f, 0.0001f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
