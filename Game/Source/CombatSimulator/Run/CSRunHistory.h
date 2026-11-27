#pragma once

#include "CoreMinimal.h"

struct FCSRunSetup;
struct FCSRunStats;
enum class ECSRunEnd : uint8;

/** One finished run, as the history keeps it. */
struct COMBATSIMULATOR_API FCSRunRecord
{
	/** When it ended, local time, "2026-11-27 14:05". */
	FString When;
	/** The game's version (ProjectVersion), so runs on different builds can be told apart. */
	FString Build;
	/** FCSRunSetup::Describe of the run's setup: runs with the same text had the same setup. */
	FString Setup;
	/** FCSRunStats::DescribeEnd. */
	FString End;
	float Seconds = 0.f;
	int32 Dealt = 0;
	int32 Knockdowns = 0;
	int32 Taken = 0;
	int32 StrikesThrown = 0;
	int32 StrikesLanded = 0;

	static FCSRunRecord Make(const FCSRunSetup& Setup, const FCSRunStats& Stats, ECSRunEnd End, const FDateTime& When);

	/** One line of JSON (no line breaks), as History.jsonl holds it. */
	FString ToJson() const;
	/** False when Line is not a record (broken JSON, or a field missing or of the wrong type). */
	static bool FromJson(const FString& Line, FCSRunRecord& Out);

	/** "2026-11-27 14:05  1:05  6 hits, 2 knockdowns, 1 taken, strikes 6/9 (time up)". */
	FString Describe() const;

	/** Better: more knockdowns, then more hits dealt, then fewer taken, then a shorter time. */
	bool IsBetterThan(const FCSRunRecord& Other) const;
};

/**
 * The finished runs on this machine, oldest first, capped at MaxRuns, kept in
 * <ProjectSavedDir>/Runs/History.jsonl (one record per line). A line that cannot be read is skipped
 * and counted, never fatal: the history is a convenience, the game runs without it.
 */
struct COMBATSIMULATOR_API FCSRunHistory
{
	static constexpr int32 MaxRuns = 200;

	TArray<FCSRunRecord> Runs;

	static FString GetFilePath();

	/** Replaces Runs with the file's records (the newest MaxRuns). Returns a note for the log when lines were skipped or the file could not be read; empty otherwise (also with no file). */
	FString Load(const FString& Path);

	/** Adds a run, drops the oldest beyond MaxRuns. With a Path, writes the whole file (false if that failed). */
	bool Add(const FCSRunRecord& Record, const FString& Path = FString());

	/** The newest Count runs, newest first. */
	TArray<FCSRunRecord> Recent(int32 Count) const;

	/** The best run with this setup text, or null. */
	const FCSRunRecord* Best(const FString& Setup) const;

	/** Parses the text of a history file (what Load reads); returns how many lines were skipped. */
	int32 Parse(const FString& Text);
};
