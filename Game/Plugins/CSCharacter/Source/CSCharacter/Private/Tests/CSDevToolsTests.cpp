// Dev tools and input binding tests for ACSCharacter (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.DevTools).
//
// Input is driven through the character's real bindings: each test gives the character transient
// input actions, calls SetupPlayerInputComponent on a fresh UEnhancedInputComponent and executes
// the bindings it made, as the Enhanced Input subsystem would on a key press. No project assets
// are needed; the project's mapping context is checked by CombatSimulator.CS.DevTools.InputContextHasAllActions
// in the game module.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CSCharacter.h"
#include "CSDevTools.h"
#include "CSMoveSim.h"
#include "CSMovementTuning.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "UObject/StrongObjectPtr.h"

namespace CSDevToolsTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	using CSTestSupport::FMoveSim;
	using CSTestSupport::NewTuning;

	// The character's input actions, filled with transient actions, and the component they are bound on.
	struct FInputRig
	{
		// Strong pointers: nothing else references these, and the test world ticks can collect garbage.
		TMap<FName, TStrongObjectPtr<UInputAction>> Actions;
		TStrongObjectPtr<UEnhancedInputComponent> Input;

		UInputAction* Make(FName Name, EInputActionValueType Type, TSoftObjectPtr<UInputAction>& Slot)
		{
			UInputAction* Action = NewObject<UInputAction>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UInputAction::StaticClass(), Name));
			Action->ValueType = Type;
			Slot = Action;
			Actions.Add(Name, TStrongObjectPtr<UInputAction>(Action));
			return Action;
		}

		void Bind(ACSCharacter& Character)
		{
			Make(TEXT("Move"), EInputActionValueType::Axis2D, Character.MoveAction);
			Make(TEXT("Look"), EInputActionValueType::Axis2D, Character.LookAction);
			Make(TEXT("Jump"), EInputActionValueType::Boolean, Character.JumpAction);
			Make(TEXT("Sprint"), EInputActionValueType::Boolean, Character.SprintAction);
			Make(TEXT("Crouch"), EInputActionValueType::Boolean, Character.CrouchAction);
			Make(TEXT("Walk"), EInputActionValueType::Boolean, Character.WalkAction);
			Make(TEXT("ToggleCamera"), EInputActionValueType::Boolean, Character.ToggleCameraAction);
			Make(TEXT("DebugOverlay"), EInputActionValueType::Boolean, Character.DebugOverlayAction);
			Make(TEXT("DebugRagdoll"), EInputActionValueType::Boolean, Character.DebugRagdollAction);
			Make(TEXT("DebugPush"), EInputActionValueType::Boolean, Character.DebugPushAction);
			Make(TEXT("DebugReload"), EInputActionValueType::Boolean, Character.DebugReloadAction);
			Input.Reset(NewObject<UEnhancedInputComponent>(&Character));
			Character.SetupPlayerInputComponent(Input.Get());
		}

		// Executes every binding of the action for this trigger event. Returns how many ran.
		int32 Fire(FName Name, ETriggerEvent Event)
		{
			const TStrongObjectPtr<UInputAction>* Found = Actions.Find(Name);
			const UInputAction* Action = Found ? Found->Get() : nullptr;
			int32 Ran = 0;
			for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
			{
				if (Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event)
				{
					Binding->Execute(FInputActionInstance(Action));
					++Ran;
				}
			}
			return Ran;
		}

		// A key press (Started) and release (Completed). Return how many bindings ran.
		int32 Press(FName Name) { return Fire(Name, ETriggerEvent::Started); }
		int32 Release(FName Name) { return Fire(Name, ETriggerEvent::Completed); }
	};

	// Counts the dev tool requests that reach the extension points (Task 7 binds the real ragdoll and push here).
	struct FDevToolsProbe
	{
		int32 Ragdolls = 0;
		int32 Pushes = 0;
		FVector LastPushDirection = FVector::ZeroVector;

		void Listen(UCSDevTools& DevTools)
		{
			DevTools.OnRagdollRequested.AddLambda([this]() { ++Ragdolls; });
			DevTools.OnPushRequested.AddLambda([this](const FVector& Direction) { ++Pushes; LastPushDirection = Direction; });
		}
	};
}

using namespace CSDevToolsTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSDevToolsReloadRejectsBadValuesTest, "CombatSimulator.CS.DevTools.ReloadRejectsBadValues", CSDevToolsTests::TestFlags)
bool FCSDevToolsReloadRejectsBadValuesTest::RunTest(const FString& Parameters)
{
	// Review Focus 3: bad tuning values reloaded are rejected with a message naming the field, and
	// the character keeps moving on the last good values.
	// Every rejection is logged as a warning: six reloads below, one console command and one F5 press.
	AddExpectedMessagePlain(TEXT("rejected, "), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 8);
	UCSMovementTuning* Tuning = NewTuning();
	FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, Tuning))
	{
		return false;
	}
	ACSCharacter* Character = Sim.Character;
	UCharacterMovementComponent* Move = Character->GetCharacterMovement();

	// A good change is applied, and only when reloaded.
	Tuning->JogSpeed = 350.f;
	Sim.Run(FVector::ForwardVector, 1.5f);
	TestNearlyEqual(TEXT("An edit to the asset does nothing until it is reloaded"), Sim.Speed2D(), 380.f, 2.f);
	FString Message;
	TestTrue(TEXT("A good tuning reloads"), UCSDevTools::ReloadTuning(*Character, Message));
	AddInfo(FString::Printf(TEXT("Good reload: %s"), *Message));
	// The result is shown on screen (HUD hook, so also in Shipping) whether or not the overlay is up.
	UCSDevTools* DevTools = Character->GetDevTools();
	TestFalse(TEXT("The overlay is off"), DevTools->IsOverlayVisible());
	TestEqual(TEXT("Last reload message on accept"), DevTools->GetLastReloadMessage(), Message);
	TestTrue(TEXT("Last reload accepted"), DevTools->WasLastReloadAccepted());
	TestTrue(TEXT("Accepted reload is on screen"), DevTools->IsReloadMessageVisible());
	Sim.Run(FVector::ForwardVector, 1.5f);
	TestNearlyEqual(TEXT("Reloaded jog speed is in use"), Sim.Speed2D(), 350.f, 2.f);

	struct FBadCase
	{
		const TCHAR* What;
		const TCHAR* Field;
		float UCSMovementTuning::* Member;
		float Value;
	};
	const FBadCase Cases[] = {
		{TEXT("sprint slower than walk"), TEXT("SprintSpeed"), &UCSMovementTuning::SprintSpeed, 100.f},
		{TEXT("zero walk speed"), TEXT("WalkSpeed"), &UCSMovementTuning::WalkSpeed, 0.f},
		{TEXT("negative jog speed"), TEXT("JogSpeed"), &UCSMovementTuning::JogSpeed, -50.f},
		{TEXT("zero sprint speed"), TEXT("SprintSpeed"), &UCSMovementTuning::SprintSpeed, 0.f},
		{TEXT("negative crouch speed"), TEXT("CrouchSpeed"), &UCSMovementTuning::CrouchSpeed, -1.f},
		{TEXT("zero crouch speed"), TEXT("CrouchSpeed"), &UCSMovementTuning::CrouchSpeed, 0.f},
	};
	for (const FBadCase& Case : Cases)
	{
		const float Good = Tuning->*Case.Member;
		Tuning->*Case.Member = Case.Value;

		const float CapBefore = Move->MaxWalkSpeed;
		FString Reason;
		const bool bReloaded = UCSDevTools::ReloadTuning(*Character, Reason);
		AddInfo(FString::Printf(TEXT("%s: %s"), Case.What, *Reason));
		TestFalse(FString::Printf(TEXT("Rejected: %s"), Case.What), bReloaded);
		TestTrue(FString::Printf(TEXT("Message for %s names %s (was: %s)"), Case.What, Case.Field, *Reason), Reason.Contains(Case.Field));
		TestEqual(FString::Printf(TEXT("Last reload message on reject: %s"), Case.What), DevTools->GetLastReloadMessage(), Reason);
		TestFalse(FString::Printf(TEXT("Last reload rejected: %s"), Case.What), DevTools->WasLastReloadAccepted());
		TestTrue(FString::Printf(TEXT("Rejection is on screen: %s"), Case.What), DevTools->IsReloadMessageVisible());
		TestNearlyEqual(FString::Printf(TEXT("Speed cap unchanged after %s"), Case.What), Move->MaxWalkSpeed, CapBefore, 0.01f);
		const UCSMovementTuning& Active = Character->GetActiveTuning();
		TestNearlyEqual(FString::Printf(TEXT("Walk speed kept after %s"), Case.What), Active.WalkSpeed, 180.f, 0.01f);
		TestNearlyEqual(FString::Printf(TEXT("Jog speed kept after %s"), Case.What), Active.JogSpeed, 350.f, 0.01f);
		TestNearlyEqual(FString::Printf(TEXT("Sprint speed kept after %s"), Case.What), Active.SprintSpeed, 620.f, 0.01f);
		TestNearlyEqual(FString::Printf(TEXT("Crouch speed kept after %s"), Case.What), Active.CrouchSpeed, 160.f, 0.01f);

		// Still moves on the last good values, at every gait, with the bad values in the asset.
		Character->SetGait(ECSGait::Sprint);
		Sim.Run(FVector::ForwardVector, 2.f);
		TestNearlyEqual(FString::Printf(TEXT("Sprints at the kept speed after %s"), Case.What), Sim.Speed2D(), 620.f, 2.f);
		Character->SetGait(ECSGait::Jog);
		Sim.Run(FVector::ForwardVector, 1.5f);
		TestNearlyEqual(FString::Printf(TEXT("Jogs at the kept speed after %s"), Case.What), Sim.Speed2D(), 350.f, 2.f);

		Tuning->*Case.Member = Good;
	}

	// Once fixed, the asset reloads again.
	TestTrue(TEXT("The fixed tuning reloads"), UCSDevTools::ReloadTuning(*Character, Message));

	// The console command reports the same rejection and keeps the values.
	Tuning->SprintSpeed = 100.f;
	TArray<FString> Messages;
	const int32 Reloaded = UCSDevTools::ReloadTuningInWorld(Sim.World.GetTestWorld(), Messages);
	TestEqual(TEXT("cs.Tuning.Reload reloads nothing when the values are bad"), Reloaded, 0);
	TestTrue(TEXT("cs.Tuning.Reload reports the character"), Messages.Num() == 1 && Messages[0].Contains(TEXT("SprintSpeed")));
	TestNearlyEqual(TEXT("Sprint speed kept after the console command"), Character->GetActiveTuning().SprintSpeed, 620.f, 0.01f);
	TestTrue(TEXT("Console rejection is on screen"), DevTools->IsReloadMessageVisible() && DevTools->GetLastReloadMessage().Contains(TEXT("SprintSpeed")));

	// F5, the reload key (the console is compiled out of Shipping), rejects the same way.
	FInputRig Rig;
	Rig.Bind(*Character);
	TestEqual(TEXT("F5 (reload) is bound"), Rig.Press(TEXT("DebugReload")), 1);
	TestFalse(TEXT("F5 rejection"), DevTools->WasLastReloadAccepted());
	TestTrue(TEXT("F5 rejection names the field"), DevTools->GetLastReloadMessage().Contains(TEXT("SprintSpeed")));
	TestNearlyEqual(TEXT("Sprint speed kept after F5"), Character->GetActiveTuning().SprintSpeed, 620.f, 0.01f);

	// The result leaves the screen after ReloadMessageSeconds.
	Sim.Run(FVector::ZeroVector, UCSDevTools::ReloadMessageSeconds + 0.5f);
	TestFalse(TEXT("Reload result times out"), DevTools->IsReloadMessageVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSDevToolsOffDisablesKeysTest, "CombatSimulator.CS.DevTools.DevToolsOffDisablesKeys", CSDevToolsTests::TestFlags)
bool FCSDevToolsOffDisablesKeysTest::RunTest(const FString& Parameters)
{
	for (const bool bDevTools : {false, true})
	{
		const FString Label = bDevTools ? TEXT("dev tools on") : TEXT("dev tools off");
		UCSMovementTuning* Tuning = NewTuning();
		Tuning->bDevTools = bDevTools;
		FMoveSim Sim;
		if (!Sim.Init(*this, 1.f / 60.f, Tuning))
		{
			return false;
		}
		ACSCharacter* Character = Sim.Character;
		UCSDevTools* DevTools = Character->GetDevTools();
		if (!TestNotNull(TEXT("Character has dev tools"), DevTools))
		{
			return false;
		}
		FDevToolsProbe Probe;
		Probe.Listen(*DevTools);
		FInputRig Rig;
		Rig.Bind(*Character);

		// The keys are bound either way; the flag decides whether they do anything.
		TestEqual(Label + TEXT(": R (ragdoll) is bound"), Rig.Fire(TEXT("DebugRagdoll"), ETriggerEvent::Started), 1);
		TestEqual(Label + TEXT(": P (push) is bound"), Rig.Fire(TEXT("DebugPush"), ETriggerEvent::Started), 1);
		TestEqual(Label + TEXT(": F1 (overlay) is bound"), Rig.Fire(TEXT("DebugOverlay"), ETriggerEvent::Started), 1);
		Sim.Run(FVector::ForwardVector, 0.5f);

		const int32 Expected = bDevTools ? 1 : 0;
		TestEqual(Label + TEXT(": ragdoll requests"), Probe.Ragdolls, Expected);
		TestEqual(Label + TEXT(": push requests"), Probe.Pushes, Expected);
		TestEqual(Label + TEXT(": overlay visible"), DevTools->IsOverlayVisible(), bDevTools);

		// The reload key (F5) is a dev tool: with it off the tuning is unchanged and nothing is shown.
		Tuning->JogSpeed = 390.f;
		TestEqual(Label + TEXT(": F5 (reload) is bound"), Rig.Fire(TEXT("DebugReload"), ETriggerEvent::Started), 1);
		TestNearlyEqual(Label + TEXT(": F5 applies"), Character->GetActiveTuning().JogSpeed, bDevTools ? 390.f : 380.f, 0.01f);
		TestEqual(Label + TEXT(": F5 result on screen"), DevTools->IsReloadMessageVisible(), bDevTools);
		TestEqual(Label + TEXT(": F5 result accepted"), DevTools->WasLastReloadAccepted(), bDevTools);

		// The reload command is a dev tool too.
		Tuning->JogSpeed = 400.f;
		TArray<FString> Messages;
		UCSDevTools::ReloadTuningInWorld(Sim.World.GetTestWorld(), Messages);
		TestNearlyEqual(Label + TEXT(": cs.Tuning.Reload applies"), Character->GetActiveTuning().JogSpeed, bDevTools ? 400.f : 380.f, 0.01f);

		if (bDevTools)
		{
			TestTrue(TEXT("Push goes along the view direction"), Probe.LastPushDirection.IsNormalized());

			// The overlay shows the feel values, read after the movement component has moved.
			Sim.Run(FVector::ForwardVector, 1.f);
			const FCSFeelSample& Sample = DevTools->GetFeelSample();
			TestNearlyEqual(TEXT("Overlay speed is the moved speed"), Sample.Speed, Sim.Speed2D(), 1.f);
			TestTrue(TEXT("Overlay sees the character on the ground"), Sample.bGrounded);
			TestNearlyEqual(TEXT("Overlay slope angle on flat ground"), Sample.SlopeAngleDeg, 0.f, 0.1f);
			TestEqual(TEXT("Overlay gait"), Sample.Gait, ECSGait::Jog);
			const FString Text = DevTools->GetOverlayText();
			AddInfo(Text.Replace(TEXT("\n"), TEXT(" | ")));
			for (const TCHAR* Field : {TEXT("Speed"), TEXT("Accel"), TEXT("Grounded"), TEXT("Slope"), TEXT("Gait"), TEXT("Camera")})
			{
				TestTrue(FString::Printf(TEXT("Overlay shows %s"), Field), Text.Contains(Field));
			}
			TestTrue(TEXT("Overlay camera reads the camera rig's view (third person by default)"), Text.Contains(TEXT("ThirdPerson")));

			// Acceleration: about Acceleration while speeding up from rest, about 0 at a steady speed.
			Sim.Run(FVector::ZeroVector, 2.f);
			Sim.Step(FVector::ForwardVector);
			Sim.Step(FVector::ForwardVector);
			TestNearlyEqual(TEXT("Overlay acceleration while speeding up"), DevTools->GetFeelSample().Acceleration, Tuning->Acceleration, 0.1f * Tuning->Acceleration);
			Sim.Run(FVector::ForwardVector, 2.f);
			TestNearlyEqual(TEXT("Overlay acceleration at a steady jog"), DevTools->GetFeelSample().Acceleration, 0.f, 5.f);

			// F1 again hides it.
			TestEqual(TEXT("F1 is still bound"), Rig.Press(TEXT("DebugOverlay")), 1);
			TestFalse(TEXT("F1 toggles the overlay off"), DevTools->IsOverlayVisible());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSDevToolsGaitKeysTest, "CombatSimulator.CS.DevTools.GaitKeys", CSDevToolsTests::TestFlags)
bool FCSDevToolsGaitKeysTest::RunTest(const FString& Parameters)
{
	// Spec 5.2: Shift held sprints, Alt held walks, Ctrl toggles crouch, otherwise jog.
	FMoveSim Sim;
	if (!Sim.Init(*this, 1.f / 60.f, NewTuning()))
	{
		return false;
	}
	ACSCharacter* Character = Sim.Character;
	FInputRig Rig;
	Rig.Bind(*Character);

	TestEqual(TEXT("Default gait is jog"), Character->GetGait(), ECSGait::Jog);
	Rig.Press(TEXT("Sprint"));
	TestEqual(TEXT("Shift held: sprint"), Character->GetGait(), ECSGait::Sprint);
	Rig.Release(TEXT("Sprint"));
	TestEqual(TEXT("Shift released: jog"), Character->GetGait(), ECSGait::Jog);
	Rig.Press(TEXT("Walk"));
	TestEqual(TEXT("Alt held: walk"), Character->GetGait(), ECSGait::Walk);
	Rig.Press(TEXT("Sprint"));
	TestEqual(TEXT("Shift and Alt held: sprint wins"), Character->GetGait(), ECSGait::Sprint);
	Rig.Release(TEXT("Sprint"));
	TestEqual(TEXT("Shift released with Alt held: walk"), Character->GetGait(), ECSGait::Walk);
	Rig.Release(TEXT("Walk"));

	Rig.Press(TEXT("Crouch"));
	Rig.Release(TEXT("Crouch"));
	Sim.Run(FVector::ZeroVector, 0.2f);
	TestEqual(TEXT("Ctrl pressed: crouch"), Character->GetGait(), ECSGait::Crouch);
	TestTrue(TEXT("Ctrl pressed: crouched"), Character->bIsCrouched);
	Rig.Press(TEXT("Crouch"));
	Rig.Release(TEXT("Crouch"));
	Sim.Run(FVector::ZeroVector, 0.2f);
	TestEqual(TEXT("Ctrl pressed again: jog"), Character->GetGait(), ECSGait::Jog);
	TestFalse(TEXT("Ctrl pressed again: standing"), Character->bIsCrouched);

	Rig.Press(TEXT("Crouch"));
	Rig.Press(TEXT("Sprint"));
	TestEqual(TEXT("Shift while crouched: stands up and sprints"), Character->GetGait(), ECSGait::Sprint);
	Rig.Release(TEXT("Sprint"));
	TestEqual(TEXT("Crouch toggle was cleared by the sprint"), Character->GetGait(), ECSGait::Jog);

	Rig.Fire(TEXT("Jump"), ETriggerEvent::Started);
	TestTrue(TEXT("Space jumps"), Character->bPressedJump);
	Rig.Fire(TEXT("Jump"), ETriggerEvent::Completed);
	TestFalse(TEXT("Space released"), Character->bPressedJump);

	// V switches the camera rig's view and still reaches the extension point; it is not a dev tool.
	int32 Toggles = 0;
	Character->OnToggleCameraRequested.AddLambda([&Toggles]() { ++Toggles; });
	TestEqual(TEXT("Active camera before V"), Character->GetActiveCameraName(), FName(TEXT("ThirdPerson")));
	TestEqual(TEXT("V is bound"), Rig.Fire(TEXT("ToggleCamera"), ETriggerEvent::Started), 1);
	TestEqual(TEXT("V reaches ToggleCamera"), Toggles, 1);
	TestEqual(TEXT("Active camera after V"), Character->GetActiveCameraName(), FName(TEXT("FirstPerson")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
