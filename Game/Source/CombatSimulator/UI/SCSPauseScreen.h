#pragma once

#include "CoreMinimal.h"
#include "CSPauseMenuModel.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ACSPlayerController;
class FCSTuningPanelModel;
class SBox;
class SButton;
class SHorizontalBox;

/**
 * The pause screen: Resume, Quit and the tabs Settings, Tuning (dev tools only) and Keys.
 *
 * Shown by ACSPlayerController while the game is paused. Everything on it is generated from data:
 * the tabs from FCSPauseMenuModel::Tabs, the settings rows from UCSGameSettings::Rows, the tuning
 * rows from CSTuning::Rows and the key list from the character's mapping context.
 *
 * Layout takes a share of whatever size the viewport is; the tab body scrolls. Usable with mouse,
 * keyboard or gamepad alone: focus starts on Resume, the D-pad or stick moves it, the bottom face
 * button activates, the right face button or Start resumes, the shoulder buttons (Page Up and Page
 * Down on a keyboard) change tab, and left and right change a focused slider.
 */
class SCSPauseScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCSPauseScreen) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ACSPlayerController>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetTab(ECSPauseTab Tab);
	ECSPauseTab GetTab() const { return ActiveTab; }

	/** Puts keyboard and gamepad focus on Resume. */
	void FocusFirstControl();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	// A click on the dimmed game or between controls stays with the screen. Left unhandled it would
	// reach the game viewport, which takes focus, and the keys above would stop working.
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return FReply::Handled(); }
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return FReply::Handled(); }

private:
	TSharedRef<SWidget> BuildSettingsTab();
	TSharedRef<SWidget> BuildTuningTab();
	TSharedRef<SWidget> BuildKeysTab();
	void StepTab(int32 Direction);
	FReply Resume();
	/** Margins that leave the panel a share of the viewport, whatever its size. */
	FMargin GetPanelPadding() const;

	TWeakObjectPtr<ACSPlayerController> Owner;
	TArray<ECSPauseTab> Tabs;
	ECSPauseTab ActiveTab = ECSPauseTab::Settings;
	TSharedPtr<SBox> Body;
	TSharedPtr<SButton> ResumeButton;
	TSharedPtr<FCSTuningPanelModel> TuningModel;
	FCSPauseKeyLatch CloseLatch;
};
