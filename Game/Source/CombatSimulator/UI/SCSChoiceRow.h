#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/**
 * One labelled choice on the pause screen: the name of the chosen option between a previous and a
 * next button. Laid out like SCSValueRow, so the two line up in one list.
 *
 * The row holds no value: it shows Index into Choices and reports the index the player asked for
 * through OnChosen (wrapping at both ends). The owner decides what the choice becomes.
 */
class SCSChoiceRow : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnChosen, int32);

	SLATE_BEGIN_ARGS(SCSChoiceRow) : _Index(0) {}
		SLATE_ARGUMENT(FString, Label)
		SLATE_ARGUMENT(TArray<FString>, Choices)
		SLATE_ATTRIBUTE(int32, Index)
		SLATE_EVENT(FOnChosen, OnChosen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The index Direction (-1 or 1) steps away from Index among Count choices, wrapping. 0 when there are none. */
	static int32 Step(int32 Index, int32 Direction, int32 Count);

private:
	FReply HandleStep(int32 Direction);

	TArray<FString> Choices;
	TAttribute<int32> Index;
	FOnChosen OnChosen;
};
