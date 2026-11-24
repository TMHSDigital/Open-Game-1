#include "SCSValueRow.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

bool SCSValueRow::ParseNumber(const FString& Text, float& OutValue)
{
	FString Clean = Text.TrimStartAndEnd().Replace(TEXT(","), TEXT("."));
	// IsNumeric accepts digits with one optional sign and one decimal point: no letters, no exponent.
	// It also accepts "-", "." and "+" on their own, which are not numbers.
	const bool bHasDigit = Clean.FindLastCharByPredicate([](TCHAR C) { return FChar::IsDigit(C); }) != INDEX_NONE;
	if (!bHasDigit || !FCString::IsNumeric(*Clean))
	{
		return false;
	}
	const double Parsed = FCString::Atod(*Clean);
	if (!FMath::IsFinite(Parsed) || FMath::Abs(Parsed) > 1.e9)
	{
		return false;
	}
	OutValue = float(Parsed);
	return true;
}

FString SCSValueRow::FormatNumber(float InValue, float InStep)
{
	if (InStep >= 1.f)
	{
		return FString::Printf(TEXT("%.0f"), InValue);
	}
	return InStep >= 0.1f ? FString::Printf(TEXT("%.1f"), InValue) : FString::Printf(TEXT("%.2f"), InValue);
}

void SCSValueRow::Construct(const FArguments& InArgs)
{
	Min = InArgs._Min;
	Max = InArgs._Max;
	Step = FMath::Max(InArgs._Step, 1.e-4f);
	Value = InArgs._Value;
	OnValueChanged = InArgs._OnValueChanged;
	OnValueCommitted = InArgs._OnValueCommitted;
	OnReset = InArgs._OnReset;
	const TAttribute<bool> IsChanged = InArgs._IsChanged;

	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 11);
	const FLinearColor TextColor(0.9f, 0.9f, 0.9f);

	TSharedRef<SWidget> Editor = SNullWidget::NullWidget;
	if (InArgs._bIsBool)
	{
		Editor = SNew(SCheckBox)
			.IsChecked_Lambda([this] { return Value.Get() >= 0.5f ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
			{
				OnValueChanged.ExecuteIfBound(State == ECheckBoxState::Checked ? 1.f : 0.f);
				OnValueCommitted.ExecuteIfBound();
			});
	}
	else
	{
		Editor = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.MinValue(Min)
				.MaxValue(Max)
				.StepSize(Step)
				.MouseUsesStep(true)
				// Left and right change the value as soon as the slider has focus; no extra press to "enter" it.
				.RequiresControllerLock(false)
				.Value_Lambda([this] { return FMath::Clamp(Value.Get(), Min, Max); })
				.OnValueChanged(this, &SCSValueRow::HandleSlider)
				.OnMouseCaptureBegin_Lambda([this] { bDragging = true; })
				.OnMouseCaptureEnd_Lambda([this]
				{
					bDragging = false;
					OnValueCommitted.ExecuteIfBound();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(84.f)
				[
					SNew(SEditableTextBox)
					.Font(Font)
					.SelectAllTextWhenFocused(true)
					.Text_Lambda([this] { return FText::FromString(FormatNumber(Value.Get(), Step)); })
					.OnTextCommitted(this, &SCSValueRow::HandleText)
				]
			];
	}

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.42f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(Font).ColorAndOpacity(TextColor).Text(FText::FromString(InArgs._Label))
		]
		+ SHorizontalBox::Slot().FillWidth(0.58f).VAlign(VAlign_Center)
		[
			Editor
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(54.f)
			[
				SNew(STextBlock).Font(Font).ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f)).Text(FText::FromString(InArgs._Unit))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(16.f)
			[
				// The value is not the default (settings) or not the packaged one (tuning).
				SNew(STextBlock).Font(Font).ColorAndOpacity(FLinearColor(1.f, 0.75f, 0.2f)).Text(FText::FromString(TEXT("*")))
				.Visibility_Lambda([IsChanged] { return IsChanged.Get() ? EVisibility::Visible : EVisibility::Hidden; })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Reset")))
			.IsEnabled(IsChanged)
			.OnClicked_Lambda([this]
			{
				OnReset.ExecuteIfBound();
				return FReply::Handled();
			})
		]
	];
}

void SCSValueRow::HandleSlider(float NewValue)
{
	OnValueChanged.ExecuteIfBound(NewValue);
	if (!bDragging)
	{
		// A key or gamepad step: there is no release to wait for.
		OnValueCommitted.ExecuteIfBound();
	}
}

void SCSValueRow::HandleText(const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnCleared)
	{
		return;
	}
	float Parsed = 0.f;
	if (!ParseNumber(Text.ToString(), Parsed))
	{
		// Not a number: the box shows the value in use again on the next frame.
		return;
	}
	// Leaving the box without typing commits its own text; only a different number is a change.
	if (FormatNumber(Parsed, Step) == FormatNumber(Value.Get(), Step))
	{
		return;
	}
	OnValueChanged.ExecuteIfBound(FMath::Clamp(Parsed, Min, Max));
	OnValueCommitted.ExecuteIfBound();
}
