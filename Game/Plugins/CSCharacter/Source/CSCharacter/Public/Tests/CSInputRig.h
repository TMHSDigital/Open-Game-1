// Drives ACSCharacter's real input bindings in automation tests, in the plugin and in the game module.
//
// Bind gives every input action slot of the character (ACSCharacter::InputActionProperties) a
// transient action, named after the property without "Action" ("Move", "DebugReset"), then calls
// SetupPlayerInputComponent on a fresh UEnhancedInputComponent. Fire executes the bindings it made,
// as the Enhanced Input subsystem would on a key press, so a new action needs no change here.
//
// Test code only: it compiles to nothing without WITH_DEV_AUTOMATION_TESTS.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "CSCharacter.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

struct FCSInputRig
{
	// Strong pointers: nothing else references these, and the test world ticks can collect garbage.
	TMap<FName, TStrongObjectPtr<UInputAction>> Actions;
	TStrongObjectPtr<UEnhancedInputComponent> Input;

	void Bind(ACSCharacter& Character)
	{
		for (FSoftObjectProperty* Slot : ACSCharacter::InputActionProperties())
		{
			FString Name = Slot->GetName();
			Name.RemoveFromEnd(TEXT("Action"));
			const bool bAxis = Name == TEXT("Move") || Name == TEXT("Look") || Name == TEXT("LookStick");
			UInputAction* Action = NewObject<UInputAction>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UInputAction::StaticClass(), FName(*Name)));
			Action->ValueType = bAxis ? EInputActionValueType::Axis2D : EInputActionValueType::Boolean;
			Slot->SetObjectPropertyValue_InContainer(&Character, Action);
			Actions.Add(FName(*Name), TStrongObjectPtr<UInputAction>(Action));
		}
		Input.Reset(NewObject<UEnhancedInputComponent>(&Character));
		Character.SetupPlayerInputComponent(Input.Get());
	}

	// An action instance carrying a value, as the Enhanced Input subsystem builds one for a key
	// press (the value and trigger event are protected members of FInputActionInstance).
	struct FValuedInstance : FInputActionInstance
	{
		FValuedInstance(const UInputAction* InAction, ETriggerEvent Event, const FInputActionValue& InValue)
			: FInputActionInstance(InAction)
		{
			TriggerEvent = Event;
			Value = InValue;
		}
	};

	// Executes every binding of the action for this trigger event with Value (zero by default).
	// Returns how many ran.
	int32 Fire(FName Name, ETriggerEvent Event, const FInputActionValue& Value = FInputActionValue())
	{
		const TStrongObjectPtr<UInputAction>* Found = Actions.Find(Name);
		const UInputAction* Action = Found ? Found->Get() : nullptr;
		int32 Ran = 0;
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
		{
			if (Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event)
			{
				Binding->Execute(FValuedInstance(Action, Event, Value));
				++Ran;
			}
		}
		return Ran;
	}

	// A key press (Started) and release (Completed). Return how many bindings ran.
	int32 Press(FName Name) { return Fire(Name, ETriggerEvent::Started); }
	int32 Release(FName Name) { return Fire(Name, ETriggerEvent::Completed); }
};

#endif // WITH_DEV_AUTOMATION_TESTS
