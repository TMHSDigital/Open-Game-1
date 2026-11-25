#include "SCSChoiceRow.h"

#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

int32 SCSChoiceRow::Step(int32 InIndex, int32 Direction, int32 Count)
{
	if (Count <= 0)
	{
		return 0;
	}
	const int32 From = FMath::Clamp(InIndex, 0, Count - 1);
	return ((From + Direction) % Count + Count) % Count;
}

void SCSChoiceRow::Construct(const FArguments& InArgs)
{
	Choices = InArgs._Choices;
	Index = InArgs._Index;
	OnChosen = InArgs._OnChosen;

	const FSlateFontInfo RowFont = FCoreStyle::GetDefaultFontStyle("Regular", 11);
	const FLinearColor RowTextColor(0.9f, 0.9f, 0.9f);

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.42f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(RowFont).ColorAndOpacity(RowTextColor).Text(FText::FromString(InArgs._Label))
		]
		+ SHorizontalBox::Slot().FillWidth(0.58f).VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("<")))
				.ToolTipText(FText::FromString(TEXT("Previous")))
				.OnClicked(this, &SCSChoiceRow::HandleStep, -1)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).HAlign(HAlign_Center).Padding(8.f, 0.f)
			[
				SNew(STextBlock)
				.Font(RowFont)
				.ColorAndOpacity(RowTextColor)
				.Text_Lambda([this]
				{
					const int32 Current = Index.Get();
					return FText::FromString(Choices.IsValidIndex(Current) ? Choices[Current] : FString(TEXT("-")));
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT(">")))
				.ToolTipText(FText::FromString(TEXT("Next")))
				.OnClicked(this, &SCSChoiceRow::HandleStep, 1)
			]
		]
	];
}

FReply SCSChoiceRow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left)
	{
		return HandleStep(-1);
	}
	if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right)
	{
		return HandleStep(1);
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SCSChoiceRow::HandleStep(int32 Direction)
{
	if (!Choices.IsEmpty())
	{
		OnChosen.ExecuteIfBound(Step(Index.Get(), Direction, Choices.Num()));
	}
	return FReply::Handled();
}
