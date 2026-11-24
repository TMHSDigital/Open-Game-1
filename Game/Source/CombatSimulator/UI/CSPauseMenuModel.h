#pragma once

#include "CoreMinimal.h"

class UInputMappingContext;

/** The tabs of the pause screen. */
enum class ECSPauseTab : uint8
{
	Settings,
	Tuning,
	Keys,
};

/** One line of the key list: what an action does and every key it is on. */
struct FCSKeyRow
{
	/** "Jump", "Toggle Camera", "Debug Overlay". */
	FString Action;
	/** "Space Bar / Gamepad Face Button Bottom". */
	FString Keys;
	/** A dev key (the action's name starts IA_Debug): listed only with dev tools on. */
	bool bDev = false;
};

/**
 * What the pause screen shows, worked out without any widget so it can be tested headless.
 */
struct COMBATSIMULATOR_API FCSPauseMenuModel
{
	/** Settings and Keys always; Tuning between them only with dev tools on. */
	static TArray<ECSPauseTab> Tabs(bool bDevTools);

	static FString TabLabel(ECSPauseTab Tab);

	/**
	 * One row per action of the mapping context, in the context's order, with every key the action
	 * is mapped to. Read from the context itself, so the list cannot drift from the real keys.
	 * Dev rows are left out unless bDevTools.
	 */
	static TArray<FCSKeyRow> KeyRows(const UInputMappingContext& Context, bool bDevTools);
};
