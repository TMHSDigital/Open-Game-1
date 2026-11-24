#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/**
 * One labelled value on the pause screen: a slider with a number box (or a check box for a switch),
 * the unit, a mark when the value is not the default, and a reset button.
 *
 * The row holds no value of its own: it shows Value and reports what the player asked for through
 * OnValueChanged. The owner decides what the value becomes (clamping, rejecting), and the row shows
 * that on the next frame, so a rejected change snaps back by itself.
 *
 * OnValueChanged fires for every step of a drag; OnValueCommitted fires once when the change is
 * finished (mouse released, key or gamepad step, text entered, check box clicked), which is when
 * the owner saves.
 */
class SCSValueRow : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnValue, float);

	SLATE_BEGIN_ARGS(SCSValueRow)
		: _Min(0.f), _Max(1.f), _Step(0.1f), _bIsBool(false), _Value(0.f), _IsChanged(false)
	{}
		SLATE_ARGUMENT(FString, Label)
		SLATE_ARGUMENT(FString, Unit)
		SLATE_ARGUMENT(float, Min)
		SLATE_ARGUMENT(float, Max)
		SLATE_ARGUMENT(float, Step)
		SLATE_ARGUMENT(bool, bIsBool)
		SLATE_ATTRIBUTE(float, Value)
		SLATE_ATTRIBUTE(bool, IsChanged)
		SLATE_EVENT(FOnValue, OnValueChanged)
		SLATE_EVENT(FSimpleDelegate, OnValueCommitted)
		SLATE_EVENT(FSimpleDelegate, OnReset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Reads a number the way a player types one: "1.5", "1,5", "-20", with spaces around it.
	 * False for empty text, letters, "nan", "inf" and exponents.
	 */
	static bool ParseNumber(const FString& Text, float& OutValue);

	/** A value as the number box shows it: as many decimals as the step needs. */
	static FString FormatNumber(float Value, float Step);

private:
	void HandleSlider(float NewValue);
	void HandleText(const FText& Text, ETextCommit::Type CommitType);

	float Min = 0.f;
	float Max = 1.f;
	float Step = 0.1f;
	TAttribute<float> Value;
	FOnValue OnValueChanged;
	FSimpleDelegate OnValueCommitted;
	FSimpleDelegate OnReset;
	/** The mouse holds the slider: the change is committed when it lets go. */
	bool bDragging = false;
};
