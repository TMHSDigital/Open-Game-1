#pragma once

#include "CoreMinimal.h"

/** One value of a run setup as the Run tab shows it: a number with a range, or one of a few named choices. */
struct FCSRunRow
{
	/** Stable id: what GetValue and SetValue take, and the field's name in the saved file. */
	FName Key;
	FString Label;
	FString Unit;
	float Min = 0.f;
	float Max = 1.f;
	float Step = 1.f;
	float Default = 0.f;
	/** Not empty: the value is an index into these names and the row is shown as a choice. */
	TArray<FString> Choices;
	/** What the row means, shown under it. */
	FString Help;
};

/** How the dummies of a run stand. */
enum class ECSRunFormation : uint8
{
	/** A row across the player's view, Distance ahead. */
	Line,
	/** Evenly around the player, Distance away. */
	Ring,
};

/**
 * What a run is made of: where it happens, who is in it and when it ends. Plain data, so it can be
 * shown as rows, saved as text, compared and started again unchanged.
 *
 * Every field has a row in Rows (its range, step and default). SetValue is the only way a field
 * changes: it clamps to the row and rounds to its step, so a setup is always valid. The map is kept
 * by name, not by index, so a saved setup survives a change to the list of maps.
 *
 * Saved form (ToJson, FromJson): {"version": 1, "map": "L_Plane", "values": {"Dummies": 3, ...}}.
 * FromJson takes what it can: a missing field keeps its default, a value out of range is clamped,
 * and a field it does not know is reported in OutNotes and left out. Text that is not JSON, or has
 * a version newer than Version, is refused whole.
 */
struct COMBATSIMULATOR_API FCSRunSetup
{
	static constexpr int32 Version = 1;
	static constexpr int32 MaxDummies = 8;
	static constexpr int32 MaxLaunchers = 4;

	/** Short package name of the map the run happens on ("L_Plane"). Empty: the map being played. */
	FString Map;

	int32 Dummies = 3;
	ECSRunFormation Formation = ECSRunFormation::Line;
	/** From the player's start to the dummies, cm. */
	float Distance = 500.f;
	/** Between neighbours in a line, cm. */
	float Spacing = 200.f;
	/** Ball launchers around the player's start, aimed at it. */
	int32 Launchers = 0;
	/** The run ends after this long, s. 0: no limit. */
	float TimeLimit = 60.f;
	/** The run ends when the player has knocked down this many. 0: no goal. */
	int32 KnockdownGoal = 0;

	/** Every field but the map, in the order the Run tab lists them. */
	static const TArray<FCSRunRow>& Rows();
	static const FCSRunRow* FindRow(FName Key);

	/** A field as a number (a choice reads as its index). 0 for an unknown key. */
	float GetValue(FName Key) const;

	/** Sets a field, clamped to its row and rounded to its step. False for an unknown key or a value that is not a number. */
	bool SetValue(FName Key, float Value);

	/** The field differs from its row's default. */
	bool IsChanged(FName Key) const;

	/** A field as the player reads it: "3", "500 cm", "Ring", "none". */
	FString DescribeValue(FName Key) const;

	/** One line for the status text and the results: "L_Plane, 3 dummies in a line at 500 cm, 60 s". */
	FString Describe() const;

	FString ToJson() const;
	static bool FromJson(const FString& Json, FCSRunSetup& OutSetup, TArray<FString>& OutNotes);

	bool operator==(const FCSRunSetup& Other) const;
	bool operator!=(const FCSRunSetup& Other) const { return !(*this == Other); }

	/**
	 * Where the dummies stand, relative to where the player starts (Start: location and facing).
	 * Line: a row Distance ahead of the player, centred on the player's facing, Spacing apart.
	 * Ring: evenly around the player at Distance, the first straight ahead. Every dummy faces the
	 * player's start. The height is Start's; the caller puts each on the floor.
	 */
	TArray<FTransform> DummySpots(const FTransform& Start) const;

	/**
	 * Where the launchers stand: StandDistance from the player's start, facing it, so the start is
	 * the square they fire at. The first is behind the player, then right, left, and last ahead.
	 */
	TArray<FTransform> LauncherSpots(const FTransform& Start, float StandDistance) const;
};
