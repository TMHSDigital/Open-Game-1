#include "SCSPauseScreen.h"

#include "../CSGameSettings.h"
#include "../CSPlayerController.h"
#include "CSCharacter.h"
#include "CSTuningPanelModel.h"
#include "CSTuningRanges.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "SCSValueRow.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor PanelColor(0.02f, 0.02f, 0.03f, 0.94f);
	const FLinearColor DimColor(0.f, 0.f, 0.f, 0.55f);
	const FLinearColor TextColor(0.9f, 0.9f, 0.9f);
	const FLinearColor FaintColor(0.6f, 0.6f, 0.6f);
	const FLinearColor ErrorColor(1.f, 0.38f, 0.38f);
	const FLinearColor GoodColor(0.45f, 0.9f, 0.45f);

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	TSharedRef<STextBlock> Label(const FString& Text, int32 Size = 11, const FLinearColor& Color = TextColor, bool bBold = false)
	{
		return SNew(STextBlock).Font(Font(Size, bBold)).ColorAndOpacity(Color).Text(FText::FromString(Text));
	}
}

void SCSPauseScreen::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	const ACSPlayerController* Controller = Owner.Get();
	Tabs = FCSPauseMenuModel::Tabs(Controller && Controller->AreDevToolsOn());

	TSharedRef<SHorizontalBox> TitleRow = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 24.f, 0.f)
		[
			Label(TEXT("Paused"), 18, TextColor, true)
		];
	for (const ECSPauseTab Tab : Tabs)
	{
		TitleRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SButton)
			.OnClicked_Lambda([this, Tab]
			{
				SetTab(Tab);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font_Lambda([this, Tab] { return Font(12, ActiveTab == Tab); })
				.ColorAndOpacity_Lambda([this, Tab] { return FSlateColor(ActiveTab == Tab ? FLinearColor::White : FaintColor); })
				.Text(FText::FromString(FCSPauseMenuModel::TabLabel(Tab)))
			]
		];
	}
	TitleRow->AddSlot().FillWidth(1.f)[SNullWidget::NullWidget];
	TitleRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
	[
		SAssignNew(ResumeButton, SButton)
		.Text(FText::FromString(TEXT("Resume")))
		.OnClicked(this, &SCSPauseScreen::Resume)
	];
	TitleRow->AddSlot().AutoWidth().VAlign(VAlign_Center)
	[
		SNew(SButton)
		.Text(FText::FromString(TEXT("Quit to desktop")))
		.OnClicked_Lambda([this]
		{
			if (ACSPlayerController* C = Owner.Get())
			{
				C->QuitToDesktop();
			}
			return FReply::Handled();
		})
	];

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			// Dims the game behind the screen.
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(DimColor)
		]
		+ SOverlay::Slot().Padding(TAttribute<FMargin>::CreateSP(this, &SCSPauseScreen::GetPanelPadding))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(PanelColor)
			.Padding(18.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					TitleRow
				]
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 14.f, 0.f, 10.f)
				[
					SAssignNew(Body, SBox)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					Label(TEXT("Escape or Start: resume.  Page Up / Page Down or shoulder buttons: change tab.  Left / Right: change a value."), 9, FaintColor)
				]
			]
		]
	];
	SetTab(Tabs.Num() > 0 ? Tabs[0] : ECSPauseTab::Settings);
}

FMargin SCSPauseScreen::GetPanelPadding() const
{
	// A wide panel on a small viewport, a narrower share of a large one; never a fixed pixel size.
	const FVector2D Size = GetTickSpaceGeometry().GetLocalSize();
	const float Side = Size.X > 1500.f ? 0.2f : (Size.X > 1000.f ? 0.12f : 0.03f);
	const float Top = Size.Y > 800.f ? 0.1f : 0.04f;
	return FMargin(Size.X * Side, Size.Y * Top);
}

void SCSPauseScreen::SetTab(ECSPauseTab Tab)
{
	if (!Tabs.Contains(Tab) || !Body.IsValid())
	{
		return;
	}
	ActiveTab = Tab;
	switch (Tab)
	{
	case ECSPauseTab::Settings:
		Body->SetContent(BuildSettingsTab());
		break;
	case ECSPauseTab::Tuning:
		Body->SetContent(BuildTuningTab());
		break;
	case ECSPauseTab::Keys:
		Body->SetContent(BuildKeysTab());
		break;
	}
}

void SCSPauseScreen::StepTab(int32 Direction)
{
	const int32 Index = Tabs.IndexOfByKey(ActiveTab);
	if (Index == INDEX_NONE || Tabs.Num() < 2)
	{
		return;
	}
	SetTab(Tabs[(Index + Direction + Tabs.Num()) % Tabs.Num()]);
	// The focused control went away with the old tab.
	FocusFirstControl();
}

void SCSPauseScreen::FocusFirstControl()
{
	if (ResumeButton.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(ResumeButton, EFocusCause::SetDirectly);
	}
}

FReply SCSPauseScreen::Resume()
{
	if (ACSPlayerController* C = Owner.Get())
	{
		C->SetPauseScreenOpen(false);
	}
	return FReply::Handled();
}

FReply SCSPauseScreen::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Seen before any focused control, so these keys always work.
	const FKey Key = InKeyEvent.GetKey();
	if (FCSPauseKeyGuard::IsCloseKey(Key))
	{
		// These are also the pause action's keys. A repeat here is the press that opened the screen,
		// still held: swallowed. A new press closes, and the controller then ignores the key until
		// it is released, so the held key cannot open the screen again.
		if (!InKeyEvent.IsRepeat())
		{
			if (ACSPlayerController* C = Owner.Get())
			{
				C->ClosePauseScreenFromKey(Key);
			}
		}
		return FReply::Handled();
	}
	if (InKeyEvent.IsRepeat())
	{
		return FReply::Unhandled();
	}
	if (Key == EKeys::PageUp || Key == EKeys::Gamepad_LeftShoulder)
	{
		StepTab(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::PageDown || Key == EKeys::Gamepad_RightShoulder)
	{
		StepTab(1);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> SCSPauseScreen::BuildSettingsTab()
{
	TSharedRef<SScrollBox> List = SNew(SScrollBox);
	for (const FCSSettingRow& Row : UCSGameSettings::Rows())
	{
		const FName Key = Row.Key;
		const float Default = Row.Default;
		List->AddSlot().Padding(0.f, 3.f)
		[
			SNew(SCSValueRow)
			.Label(Row.Label)
			.Min(Row.Min)
			.Max(Row.Max)
			.Step(Row.Step)
			.bIsBool(Row.bIsBool)
			.Value_Lambda([Key] { return UCSGameSettings::Get()->GetValue(Key); })
			.IsChanged_Lambda([Key, Default] { return !FMath::IsNearlyEqual(UCSGameSettings::Get()->GetValue(Key), Default, 1.e-4f); })
			// Update saves and applies at once, so there is nothing left to do on commit.
			.OnValueChanged_Lambda([Key](float NewValue) { UCSGameSettings::Get()->Update(Key, NewValue); })
			.OnReset_Lambda([Key, Default] { UCSGameSettings::Get()->Update(Key, Default); })
		];
	}
	List->AddSlot().Padding(0.f, 12.f, 0.f, 0.f).HAlign(HAlign_Left)
	[
		SNew(SButton)
		.Text(FText::FromString(TEXT("Reset all settings")))
		.OnClicked_Lambda([]
		{
			UCSGameSettings::Get()->ResetAll();
			return FReply::Handled();
		})
	];
	return List;
}

TSharedRef<SWidget> SCSPauseScreen::BuildTuningTab()
{
	const ACSPlayerController* Controller = Owner.Get();
	ACSCharacter* Character = Controller ? Cast<ACSCharacter>(Controller->GetPawn()) : nullptr;
	if (!Character || !Controller->AreDevToolsOn())
	{
		TuningModel.Reset();
		return Label(TEXT("There is no character to tune."));
	}
	TuningModel = MakeShared<FCSTuningPanelModel>(*Character);
	const TSharedPtr<FCSTuningPanelModel> Model = TuningModel;

	TSharedRef<SScrollBox> List = SNew(SScrollBox);
	List->AddSlot().Padding(0.f, 0.f, 0.f, 8.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			// Which values are in play, or why the last change was refused.
			SNew(STextBlock)
			.Font(Font(11))
			.AutoWrapText(true)
			.Text_Lambda([Model] { return FText::FromString(Model->StatusText()); })
			.ColorAndOpacity_Lambda([Model] { return FSlateColor(Model->IsStatusError() ? ErrorColor : GoodColor); })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Copy values")))
			.ToolTipText(FText::FromString(TEXT("Puts the changed values on the clipboard as text, to hand back for the next build.")))
			.OnClicked_Lambda([Model]
			{
				FPlatformApplicationMisc::ClipboardCopy(*Model->CopyText());
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Reset all")))
			.OnClicked_Lambda([Model]
			{
				FString Reason;
				Model->ResetAll(Reason);
				return FReply::Handled();
			})
		]
	];
	List->AddSlot().Padding(0.f, 0.f, 0.f, 8.f)
	[
		Label(TEXT("Changes apply at once and are kept on this machine. The game is paused here: resume to feel a change."), 9, FaintColor)
	];

	// One collapsible section per group, in the table's order.
	// The first section starts open; the rest are closed so the list is not sixty rows long.
	FName Group;
	int32 Sections = 0;
	TSharedPtr<SVerticalBox> Section;
	for (const FCSTuningRow& Row : CSTuning::Rows())
	{
		if (Row.Group != Group || !Section.IsValid())
		{
			Group = Row.Group;
			List->AddSlot().Padding(0.f, 2.f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(Sections++ > 0)
				.AreaTitle(FText::FromName(Group))
				.AreaTitleFont(Font(12, true))
				.BodyContent()
				[
					SAssignNew(Section, SVerticalBox)
				]
			];
		}
		const FString Path = Row.Path;
		Section->AddSlot().AutoHeight().Padding(10.f, 2.f, 0.f, 2.f)
		[
			SNew(SCSValueRow)
			.Label(Row.Label)
			.Unit(Row.Unit)
			.Min(Row.Min)
			.Max(Row.Max)
			.Step(Row.Step)
			.Value_Lambda([Model, Path] { return Model->GetValue(Path); })
			.IsChanged_Lambda([Model, Path] { return Model->IsChanged(Path); })
			.OnValueChanged_Lambda([Model, Path](float NewValue)
			{
				FString Reason;
				Model->SetValue(Path, NewValue, Reason);
			})
			.OnValueCommitted_Lambda([Model]
			{
				FString Reason;
				Model->Commit(Reason);
			})
			.OnReset_Lambda([Model, Path]
			{
				FString Reason;
				Model->ResetRow(Path, Reason);
			})
		];
	}
	return List;
}

TSharedRef<SWidget> SCSPauseScreen::BuildKeysTab()
{
	const ACSPlayerController* Controller = Owner.Get();
	const ACSCharacter* Character = Controller ? Cast<ACSCharacter>(Controller->GetPawn()) : nullptr;
	if (!Character)
	{
		Character = GetDefault<ACSCharacter>();
	}
	const UInputMappingContext* Context = Character->InputMappingContext.LoadSynchronous();
	if (!Context)
	{
		return Label(TEXT("The key list is not available."));
	}
	TSharedRef<SScrollBox> List = SNew(SScrollBox);
	for (const FCSKeyRow& Row : FCSPauseMenuModel::KeyRows(*Context, Controller && Controller->AreDevToolsOn()))
	{
		List->AddSlot().Padding(0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.32f)
			[
				Label(Row.Action, 11, Row.bDev ? FaintColor : TextColor)
			]
			+ SHorizontalBox::Slot().FillWidth(0.68f)
			[
				Label(Row.Keys, 11, Row.bDev ? FaintColor : TextColor)
			]
		];
	}
	return List;
}
