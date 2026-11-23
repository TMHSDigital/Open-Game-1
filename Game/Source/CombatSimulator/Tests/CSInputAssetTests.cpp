// Checks the Enhanced Input assets made by Game/Content/Python/create_input_assets.py and that
// ACSCharacter is configured to use them (Game/Config/DefaultGame.ini).
// Run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.DevTools

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CSCharacter.h"
#include "GameFramework/InputSettings.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace CSInputAssetTests
{
	const TCHAR* ContextPath = TEXT("/Game/Input/IMC_Character.IMC_Character");
	const TCHAR* Regenerate = TEXT(" (missing or stale: run Game/Content/Python/create_input_assets.py, see its docstring)");

	struct FExpectedAction
	{
		const TCHAR* Name;
		EInputActionValueType Type;
		TArray<FKey> KeyboardMouse;
		FKey Gamepad;
		TSoftObjectPtr<UInputAction> ACSCharacter::* Slot;
	};

	// Global Constraints keys; gamepad per spec 5.2 (sticks, L3 sprint, face buttons).
	TArray<FExpectedAction> ExpectedActions()
	{
		using T = EInputActionValueType;
		return {
			{TEXT("IA_Move"), T::Axis2D, {EKeys::W, EKeys::A, EKeys::S, EKeys::D}, EKeys::Gamepad_Left2D, &ACSCharacter::MoveAction},
			{TEXT("IA_Look"), T::Axis2D, {EKeys::Mouse2D}, EKeys::Gamepad_Right2D, &ACSCharacter::LookAction},
			{TEXT("IA_Jump"), T::Boolean, {EKeys::SpaceBar}, EKeys::Gamepad_FaceButton_Bottom, &ACSCharacter::JumpAction},
			{TEXT("IA_Sprint"), T::Boolean, {EKeys::LeftShift}, EKeys::Gamepad_LeftThumbstick, &ACSCharacter::SprintAction},
			{TEXT("IA_Crouch"), T::Boolean, {EKeys::LeftControl}, EKeys::Gamepad_FaceButton_Right, &ACSCharacter::CrouchAction},
			{TEXT("IA_Walk"), T::Boolean, {EKeys::LeftAlt}, EKeys::Gamepad_LeftShoulder, &ACSCharacter::WalkAction},
			{TEXT("IA_ToggleCamera"), T::Boolean, {EKeys::V}, EKeys::Gamepad_RightThumbstick, &ACSCharacter::ToggleCameraAction},
			{TEXT("IA_DebugOverlay"), T::Boolean, {EKeys::F1}, EKeys::Gamepad_Special_Left, &ACSCharacter::DebugOverlayAction},
			{TEXT("IA_DebugRagdoll"), T::Boolean, {EKeys::R}, EKeys::Gamepad_DPad_Down, &ACSCharacter::DebugRagdollAction},
			{TEXT("IA_DebugPush"), T::Boolean, {EKeys::P}, EKeys::Gamepad_DPad_Up, &ACSCharacter::DebugPushAction},
			{TEXT("IA_DebugReload"), T::Boolean, {EKeys::F5}, EKeys::Gamepad_DPad_Left, &ACSCharacter::DebugReloadAction},
			{TEXT("IA_DebugMapSwitch"), T::Boolean, {EKeys::F2}, EKeys::Gamepad_DPad_Right, &ACSCharacter::DebugMapSwitchAction},
		};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSDevToolsInputContextHasAllActionsTest, "CombatSimulator.CS.DevTools.InputContextHasAllActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCSDevToolsInputContextHasAllActionsTest::RunTest(const FString& Parameters)
{
	using namespace CSInputAssetTests;

	UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, ContextPath);
	if (!TestNotNull(FString(TEXT("IMC_Character exists")) + Regenerate, Context))
	{
		return false;
	}
	const ACSCharacter* Defaults = GetDefault<ACSCharacter>();
	TestEqual(TEXT("ACSCharacter uses IMC_Character (DefaultGame.ini)"), Defaults->InputMappingContext.ToSoftObjectPath(), FSoftObjectPath(ContextPath));

	const TArray<FEnhancedActionKeyMapping>& Mappings = Context->GetMappings();
	for (const FExpectedAction& Expected : ExpectedActions())
	{
		const FString Path = FString::Printf(TEXT("/Game/Input/%s.%s"), Expected.Name, Expected.Name);
		const UInputAction* Action = LoadObject<UInputAction>(nullptr, *Path);
		if (!TestNotNull(FString::Printf(TEXT("%s exists%s"), Expected.Name, Regenerate), Action))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s value type"), Expected.Name), Action->ValueType, Expected.Type);
		TestEqual(FString::Printf(TEXT("ACSCharacter uses %s (DefaultGame.ini)"), Expected.Name), (Defaults->*Expected.Slot).ToSoftObjectPath(), FSoftObjectPath(Path));

		TArray<FKey> Keys;
		for (const FEnhancedActionKeyMapping& Mapping : Mappings)
		{
			if (Mapping.Action == Action)
			{
				Keys.Add(Mapping.Key);
			}
		}
		const int32 Gamepad = Keys.FilterByPredicate([](const FKey& Key) { return Key.IsGamepadKey(); }).Num();
		TestTrue(FString::Printf(TEXT("%s has a keyboard or mouse key"), Expected.Name), Keys.Num() - Gamepad >= 1);
		TestTrue(FString::Printf(TEXT("%s has a gamepad key"), Expected.Name), Gamepad >= 1);
		for (const FKey& Key : Expected.KeyboardMouse)
		{
			TestTrue(FString::Printf(TEXT("%s is on %s"), Expected.Name, *Key.ToString()), Keys.Contains(Key));
		}
		TestTrue(FString::Printf(TEXT("%s is on %s"), Expected.Name, *Expected.Gamepad.ToString()), Keys.Contains(Expected.Gamepad));
	}

	// Modifiers that make the values mean what ACSCharacter expects.
	auto HasModifier = [&Mappings](const FKey& Key, const UClass* ModifierClass)
	{
		for (const FEnhancedActionKeyMapping& Mapping : Mappings)
		{
			if (Mapping.Key == Key)
			{
				for (const UInputModifier* Modifier : Mapping.Modifiers)
				{
					if (Modifier && Modifier->IsA(ModifierClass))
					{
						return true;
					}
				}
			}
		}
		return false;
	};
	TestTrue(TEXT("W is forward (swizzled to Y)"), HasModifier(EKeys::W, UInputModifierSwizzleAxis::StaticClass()));
	TestTrue(TEXT("S is backward (negated)"), HasModifier(EKeys::S, UInputModifierNegate::StaticClass()));
	TestTrue(TEXT("A is left (negated)"), HasModifier(EKeys::A, UInputModifierNegate::StaticClass()));
	TestTrue(TEXT("Mouse look is scaled to degrees"), HasModifier(EKeys::Mouse2D, UInputModifierScalar::StaticClass()));
	TestTrue(TEXT("Stick look is per second, not per frame"), HasModifier(EKeys::Gamepad_Right2D, UInputModifierScaleByDeltaTime::StaticClass()));

	// Look values are degrees with +Y up only when the legacy pitch scale (-2.5) is off.
	TestFalse(TEXT("Legacy input scales are off (DefaultInput.ini)"), GetDefault<UInputSettings>()->bEnableLegacyInputScales);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
