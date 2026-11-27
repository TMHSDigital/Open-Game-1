#pragma once

#include "CoreMinimal.h"
#include "CSPhysicsBodyComponent.h"

/** Why a run ended. */
enum class ECSRunEnd : uint8
{
	None,
	/** The player ended it. */
	Manual,
	TimeLimit,
	KnockdownGoal,
};

/** Who a hit belongs to, from the player's side. */
enum class ECSRunHitSide : uint8
{
	/** The player hit someone else: with something thrown, or by running into them. */
	Dealt,
	/** Something hit the player. */
	Taken,
	/** Neither: a launcher's ball on a dummy, one dummy falling into another. */
	Other,
};

/** The hits of one side of a run. */
struct COMBATSIMULATOR_API FCSRunHits
{
	/** Every hit, whatever it did. */
	int32 Hits = 0;
	int32 Staggers = 0;
	int32 Knockdowns = 0;
	/** Hits that changed nothing: the character was already down. */
	int32 NoEffect = 0;
	/** Hits taken on a guard (UCSGuardComponent). */
	int32 Blocked = 0;
	/** The largest hit, as the velocity change it stood for, cm/s. */
	float Biggest = 0.f;
	/** Sum of every hit's velocity change, cm/s. */
	float Total = 0.f;
	/** Hits per body part struck. */
	TMap<FName, int32> ByBone;

	void Record(const FCSContactRecord& Hit);

	float Average() const { return Hits > 0 ? Total / float(Hits) : 0.f; }

	/** Body parts by hit count, most first, ties by name: "spine_03 4, head 2". Empty without hits. */
	FString DescribeBones(int32 MaxParts = 4) const;
};

/**
 * What happened in a run. Filled by UCSRunDirector from the character plugin's hit and throw events;
 * holds no pointers, so it can be kept, compared and shown after the run's actors are gone.
 */
struct COMBATSIMULATOR_API FCSRunStats
{
	/** Game time the run has lasted, s (the pause screen stops it). */
	float Seconds = 0.f;
	/** Balls the player threw. */
	int32 BallsThrown = 0;
	/** Hits dealt by a ball the player threw (the rest of Dealt is the player's own body). */
	int32 BallHits = 0;
	/** Punches the player threw, and how many of them reached a character. */
	int32 PunchesThrown = 0;
	int32 PunchesLanded = 0;
	/** The player's punches that were taken on a guard. */
	int32 PunchesBlocked = 0;
	/** The player's dodge steps. */
	int32 Dodges = 0;
	/** The player's strikes by move ("jab", "kick", ...): thrown and landed. */
	TMap<FString, int32> MovesThrown;
	TMap<FString, int32> MovesLanded;
	/** The player's strikes thrown without enough stamina. */
	int32 TiredStrikes = 0;
	/** Times the player ran out of stamina. */
	int32 Exhausted = 0;
	/** Punches the run's opponents threw, and how many of them reached the player or its guard. */
	int32 OpponentPunches = 0;
	int32 OpponentPunchesLanded = 0;
	int32 OpponentPunchesBlocked = 0;
	FCSRunHits Dealt;
	FCSRunHits Taken;
	/** Hits between others, counted only. */
	int32 OtherHits = 0;

	/** bPlayerBall: the hit was made by a ball the player threw (only meaningful for Dealt). */
	void RecordHit(ECSRunHitSide Side, const FCSContactRecord& Hit, bool bPlayerBall = false);
	void RecordThrow() { ++BallsThrown; }
	void RecordPunch() { ++PunchesThrown; }
	void RecordPunchLanded() { ++PunchesLanded; }
	/** A strike of the player's by move (FCSStrikeMoveData::Describe), thrown or landed, and whether it was thrown tired. */
	void RecordMove(const FString& Move, bool bTired) { ++MovesThrown.FindOrAdd(Move); TiredStrikes += bTired ? 1 : 0; }
	void RecordMoveLanded(const FString& Move) { ++MovesLanded.FindOrAdd(Move); }
	/** "jab 3/5, straight 1/2, kick 1/1" (landed/thrown), in the order moves were first thrown; empty without strikes. */
	FString DescribeMoves() const;
	void RecordPunchBlocked() { ++PunchesBlocked; }
	void RecordDodge() { ++Dodges; }
	void RecordOpponentPunch() { ++OpponentPunches; }
	/** An opponent's punch reached the player: landed (Stagger, Ragdoll) or on the guard (Blocked). */
	void RecordOpponentPunchResult(ECSContactResult Result);

	/**
	 * Landed over thrown, 0 to 1; 0 with none thrown. The Punch counters count every strike, kicks
	 * included (they are older than the kick); the results call them strikes.
	 */
	float PunchAccuracy() const;

	/** Ball hits over balls thrown, 0 to 1 (at most 1: one ball can hit two). 0 without throws. Hits with the body do not count. */
	float Accuracy() const;

	/** "1:05" for 65 s. */
	static FString FormatTime(float Seconds);

	static FString DescribeEnd(ECSRunEnd End);

	/** The line drawn on screen while a run is on: "Run 0:34 / 1:00   hits 5   knockdowns 1 / 3   taken 2". */
	FString HudLine(float TimeLimit, int32 KnockdownGoal) const;

	/** The results, one fact per line, for the Run tab. */
	TArray<FString> SummaryLines() const;
};
