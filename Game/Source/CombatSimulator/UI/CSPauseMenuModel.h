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
 * Keeps the key that closed the pause screen from opening it again.
 *
 * The keys that close the screen (FCSPauseKeyGuard::IsCloseKey) are also the pause action's keys. The
 * screen closes on key down and swallows that event, so the game never saw the key go down. While the
 * key is still held, its repeat events now reach the game, which would take the first one as a new
 * press of the pause action and open the screen again. The player controller arms this guard when the
 * screen closes from a key and asks it about every key event: presses and repeats of that key are
 * swallowed until the key is released (or MaxSeconds pass, in case the release was lost with focus).
 */
struct COMBATSIMULATOR_API FCSPauseKeyGuard
{
	static constexpr double MaxSeconds = 5.0;

	/** Escape, Tab, gamepad Start and the right face button. */
	static bool IsCloseKey(const FKey& Key);

	/** The screen was just closed by Key, at time Now (seconds). */
	void Arm(const FKey& Key, double Now);

	/**
	 * A key event is about to reach the game. True: swallow it (a press or repeat of the closing key
	 * while it is still held). The release of the closing key ends the guard and is let through.
	 */
	bool ShouldSwallow(const FKey& Key, bool bIsRelease, double Now);

private:
	FKey Armed;
	double ArmedAt = 0.0;
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
