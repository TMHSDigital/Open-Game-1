#include "SCSPauseScreen.h"

#include "../CSGameMode.h"
#include "../CSGameSettings.h"
#include "../CSPlayerController.h"
#include "../Run/CSRunDirector.h"
#include "../Run/CSRunStore.h"
#include "CSCharacter.h"
#include "CSDevTools.h"
#include "CSTuningPanelModel.h"
#include "CSTuningRanges.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Misc/PackageName.h"
#include "SCSChoiceRow.h"
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
	const ECSPauseTab First = Tabs.Num() > 0 ? Tabs[0] : ECSPauseTab::Settings;
	SetTab(InArgs._InitialTab.IsSet() && Tabs.Contains(InArgs._InitialTab.GetValue()) ? InArgs._InitialTab.GetValue() : First);
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
	case ECSPauseTab::Run:
		Body->SetContent(BuildRunTab());
		break;
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
	// The two dev keys that have no gamepad button (T and Backspace): resume, then do it.
	const TWeakObjectPtr<ACSPlayerController> WeakOwner = Owner;
	auto DevButton = [WeakOwner](const TCHAR* Text, TFunction<void(UCSDevTools&)> Action)
	{
		return SNew(SButton)
			.Text(FText::FromString(Text))
			.OnClicked_Lambda([WeakOwner, Action]
			{
				if (ACSPlayerController* C = WeakOwner.Get())
				{
					ACSCharacter* Pawn = Cast<ACSCharacter>(C->GetPawn());
					C->SetPauseScreenOpen(false);
					if (UCSDevTools* DevTools = Pawn ? Pawn->GetDevTools() : nullptr)
					{
						Action(*DevTools);
					}
				}
				return FReply::Handled();
			});
	};
	List->AddSlot().Padding(0.f, 0.f, 0.f, 10.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			DevButton(TEXT("Throw a ball (T)"), [](UCSDevTools& DevTools) { DevTools.RequestThrow(); })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			DevButton(TEXT("Reset dummy, props and player (Backspace)"), [](UCSDevTools& DevTools) { DevTools.RequestReset(); })
		]
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

TSharedRef<SWidget> SCSPauseScreen::BuildRunTab()
{
	const ACSPlayerController* Controller = Owner.Get();
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	ACSGameMode* Mode = World ? Cast<ACSGameMode>(World->GetAuthGameMode()) : nullptr;
	UCSRunDirector* Director = Mode ? Mode->GetRunDirector() : nullptr;
	UCSRunStore* Store = UCSRunStore::Get(Controller);
	if (!Director || !Store)
	{
		return Label(TEXT("Runs are not available on this map."));
	}
	const TWeakObjectPtr<ACSPlayerController> WeakOwner = Owner;
	const TWeakObjectPtr<UCSRunDirector> WeakDirector = Director;
	const TWeakObjectPtr<UCSRunStore> WeakStore = Store;
	const FString CurrentMap = FPackageName::GetShortName(UWorld::RemovePIEPrefix(World->GetPackage()->GetName()));
	const TArray<FString> Maps = Mode->GetRunMapNames();

	auto StateIs = [WeakDirector](TFunction<bool(ECSRunState)> Test)
	{
		return TAttribute<EVisibility>::CreateLambda([WeakDirector, Test]
		{
			const UCSRunDirector* D = WeakDirector.Get();
			return D && Test(D->GetState()) ? EVisibility::Visible : EVisibility::Collapsed;
		});
	};
	const TAttribute<EVisibility> WhenRunning = StateIs([](ECSRunState S) { return S == ECSRunState::Running; });
	const TAttribute<EVisibility> WhenNotIdle = StateIs([](ECSRunState S) { return S != ECSRunState::Idle; });

	// A button that resumes the game and then acts on the director, so the run begins unpaused.
	auto ResumeThen = [WeakOwner, WeakDirector](TFunction<void(UCSRunDirector&)> Action)
	{
		return FOnClicked::CreateLambda([WeakOwner, WeakDirector, Action]
		{
			if (ACSPlayerController* C = WeakOwner.Get())
			{
				C->SetPauseScreenOpen(false);
			}
			if (UCSRunDirector* D = WeakDirector.Get())
			{
				Action(*D);
			}
			return FReply::Handled();
		});
	};

	TSharedRef<SScrollBox> List = SNew(SScrollBox);
	List->AddSlot().Padding(0.f, 0.f, 0.f, 8.f)
	[
		SNew(STextBlock)
		.Font(Font(12, true))
		.AutoWrapText(true)
		.Text_Lambda([WeakDirector] { return FText::FromString(WeakDirector.IsValid() ? WeakDirector->GetStatusText() : FString()); })
		.ColorAndOpacity_Lambda([WeakDirector]
		{
			return FSlateColor(WeakDirector.IsValid() && WeakDirector->GetState() == ECSRunState::Running ? GoodColor : TextColor);
		})
	];
	List->AddSlot().Padding(0.f, 0.f, 0.f, 10.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SButton)
			.Text_Lambda([WeakDirector]
			{
				const bool bIdle = !WeakDirector.IsValid() || WeakDirector->GetState() == ECSRunState::Idle;
				return FText::FromString(bIdle ? TEXT("Start run") : TEXT("Start a new run with the setup below"));
			})
			.OnClicked(ResumeThen([WeakStore](UCSRunDirector& D)
			{
				if (const UCSRunStore* S = WeakStore.Get())
				{
					FString Reason;
					D.StartRun(S->GetDraft(), Reason);
				}
			}))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SButton)
			.Visibility(WhenNotIdle)
			.Text(FText::FromString(TEXT("Run it again (Backspace)")))
			.OnClicked(ResumeThen([](UCSRunDirector& D) { D.Rerun(); }))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SButton)
			.Visibility(WhenRunning)
			.Text(FText::FromString(TEXT("End run")))
			.OnClicked_Lambda([WeakDirector]
			{
				// Stays on this tab: the results appear below.
				if (UCSRunDirector* D = WeakDirector.Get())
				{
					D->EndRun(ECSRunEnd::Manual);
				}
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.Visibility(WhenNotIdle)
			.Text(FText::FromString(TEXT("Leave run")))
			.ToolTipText(FText::FromString(TEXT("Back to free play on the map as it was made.")))
			.OnClicked(ResumeThen([](UCSRunDirector& D) { D.LeaveRun(); }))
		]
	];
	List->AddSlot().Padding(0.f, 0.f, 0.f, 12.f)
	[
		SNew(SVerticalBox)
		.Visibility(WhenNotIdle)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(Font(11, true))
			.ColorAndOpacity(TextColor)
			.Text_Lambda([WeakDirector]
			{
				const bool bOver = WeakDirector.IsValid() && WeakDirector->GetState() == ECSRunState::Ended;
				return FText::FromString(bOver ? TEXT("Results") : TEXT("So far"));
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(10.f, 4.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Font(Font(11))
			.ColorAndOpacity(TextColor)
			.Text_Lambda([WeakDirector]
			{
				return FText::FromString(WeakDirector.IsValid() ? FString::Join(WeakDirector->GetStats().SummaryLines(), TEXT("\n")) : FString());
			})
		]
	];

	List->AddSlot().Padding(0.f, 0.f, 0.f, 4.f)
	[
		Label(TEXT("Setup of the next run"), 12, TextColor, true)
	];
	if (!Store->GetLastLoadNote().IsEmpty())
	{
		List->AddSlot().Padding(0.f, 0.f, 0.f, 4.f)
		[
			Label(Store->GetLastLoadNote(), 9, ErrorColor)
		];
	}
	List->AddSlot().Padding(0.f, 0.f, 0.f, 6.f)
	[
		Label(TEXT("Kept on this machine. A run on another map opens that map."), 9, FaintColor)
	];
	if (!Maps.IsEmpty())
	{
		List->AddSlot().Padding(0.f, 3.f)
		[
			SNew(SCSChoiceRow)
			.Label(TEXT("Map"))
			.Choices(Maps)
			.Index_Lambda([WeakStore, Maps, CurrentMap]
			{
				// A setup that names no map, or one that is gone, means the map being played.
				const FString& Wanted = WeakStore.IsValid() ? WeakStore->GetDraft().Map : CurrentMap;
				const int32 Found = Maps.IndexOfByKey(Wanted);
				return Found != INDEX_NONE ? Found : FMath::Max(0, Maps.IndexOfByKey(CurrentMap));
			})
			.OnChosen_Lambda([WeakStore, Maps](int32 Chosen)
			{
				if (UCSRunStore* S = WeakStore.Get(); S && Maps.IsValidIndex(Chosen))
				{
					S->SetDraftMap(Maps[Chosen], true);
				}
			})
		];
	}
	for (const FCSRunRow& Row : FCSRunSetup::Rows())
	{
		const FName Key = Row.Key;
		const float Default = Row.Default;
		if (!Row.Choices.IsEmpty())
		{
			List->AddSlot().Padding(0.f, 3.f)
			[
				SNew(SCSChoiceRow)
				.Label(Row.Label)
				.Choices(Row.Choices)
				.Index_Lambda([WeakStore, Key] { return WeakStore.IsValid() ? FMath::RoundToInt(WeakStore->GetDraft().GetValue(Key)) : 0; })
				.OnChosen_Lambda([WeakStore, Key](int32 Chosen)
				{
					if (UCSRunStore* S = WeakStore.Get())
					{
						S->SetDraftValue(Key, float(Chosen), true);
					}
				})
			];
		}
		else
		{
			List->AddSlot().Padding(0.f, 3.f)
			[
				SNew(SCSValueRow)
				.Label(Row.Label)
				.Unit(Row.Unit)
				.Min(Row.Min)
				.Max(Row.Max)
				.Step(Row.Step)
				.Value_Lambda([WeakStore, Key, Default] { return WeakStore.IsValid() ? WeakStore->GetDraft().GetValue(Key) : Default; })
				.IsChanged_Lambda([WeakStore, Key] { return WeakStore.IsValid() && WeakStore->GetDraft().IsChanged(Key); })
				.OnValueChanged_Lambda([WeakStore, Key](float NewValue)
				{
					if (UCSRunStore* S = WeakStore.Get())
					{
						S->SetDraftValue(Key, NewValue, false);
					}
				})
				.OnValueCommitted_Lambda([WeakStore]
				{
					if (const UCSRunStore* S = WeakStore.Get())
					{
						S->SaveDraft();
					}
				})
				.OnReset_Lambda([WeakStore, Key, Default]
				{
					if (UCSRunStore* S = WeakStore.Get())
					{
						S->SetDraftValue(Key, Default, true);
					}
				})
			];
		}
		if (!Row.Help.IsEmpty())
		{
			List->AddSlot().Padding(0.f, 0.f, 0.f, 4.f)
			[
				Label(Row.Help, 9, FaintColor)
			];
		}
	}
	List->AddSlot().Padding(0.f, 12.f, 0.f, 0.f).HAlign(HAlign_Left)
	[
		SNew(SButton)
		.Text(FText::FromString(TEXT("Reset the setup")))
		.OnClicked_Lambda([WeakStore]
		{
			if (UCSRunStore* S = WeakStore.Get())
			{
				// Every value back to its default; the map stays.
				FCSRunSetup Fresh;
				Fresh.Map = S->GetDraft().Map;
				S->SetDraft(Fresh);
			}
			return FReply::Handled();
		})
	];
	return List;
}
