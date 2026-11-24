#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

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
 * Decides when a key closes the pause screen.
 *
 * The keys that close the screen are also the pause action's keys. If the screen closed on key down,
 * the same key, still held and repeating, would reach the game as a new press and open the screen
 * again. So the screen swallows every key down of a closing key (repeats too) and closes on the
 * release, and only for a press that began while the screen was up: the release of the press that
 * opened the screen does nothing.
 */
struct COMBATSIMULATOR_API FCSPauseKeyLatch
{
	/** Escape, Tab, gamepad Start and the right face button. */
	static bool IsCloseKey(const FKey& Key);

	/** A closing key went down on the screen. A first press arms the latch; a repeat does not. */
	void KeyDown(const FKey& Key, bool bIsRepeat);

	/** A key was released. True when it ends a press that began on the screen: close now. */
	bool KeyUp(const FKey& Key);

private:
	FKey Armed;
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
