#include "CSStrikeMove.h"

namespace
{
	FCSStrikeMoveData Row(ECSStrikeMove Move, bool bRight, bool bLeg, bool bHook, float Windup, float Strike, float Recover,
		float Reach, float Speed, float Mass, float Cost)
	{
		FCSStrikeMoveData Out;
		Out.Move = Move;
		Out.bRightSide = bRight;
		Out.bLeg = bLeg;
		Out.bHook = bHook;
		Out.WindupScale = Windup;
		Out.StrikeScale = Strike;
		Out.RecoverScale = Recover;
		Out.ReachScale = Reach;
		Out.SpeedScale = Speed;
		Out.MassScale = Mass;
		Out.StaminaCost = Cost;
		return Out;
	}

	FCSStrikeMoveData WithClip(FCSStrikeMoveData Row, const TCHAR* Name, float ActiveStart, float ActiveEnd)
	{
		Row.Clip = FSoftObjectPath(FString::Printf(TEXT("/Game/Characters/Base/Strikes/%s.%s"), Name, Name));
		Row.ActiveStart = ActiveStart;
		Row.ActiveEnd = ActiveEnd;
		return Row;
	}
}

TArray<FCSStrikeMoveData> FCSStrikeMoveData::Defaults()
{
	// The straight is the punch as tuned; the others are read against it. The jab trades weight for
	// speed and cost, the hooks trade reach for weight, and the kick is the slow, far, heavy one.
	// Each plays its authored clip (decision 0025); the active window is when, in the clip, the fist
	// or foot is on its way out to its peak (strikes.json from build_strikes.py).
	return {
		//                                   right  leg    hook   windup strike recover reach speed  mass  cost
		WithClip(Row(ECSStrikeMove::Jab,       false, false, false, 0.6f,  0.8f,  0.7f,   0.85f, 0.85f, 0.55f, 6.f),
			TEXT("A_Strike_Jab"), 0.1f, 0.2f),
		WithClip(Row(ECSStrikeMove::Straight,  true,  false, false, 1.0f,  1.0f,  1.0f,   1.05f, 1.0f,  1.0f,  12.f),
			TEXT("A_Strike_Cross"), 0.1333f, 0.2333f),
		WithClip(Row(ECSStrikeMove::HookLeft,  false, false, true,  1.1f,  1.3f,  1.1f,   0.85f, 1.0f,  1.3f,  16.f),
			TEXT("A_Strike_HookLead"), 0.1667f, 0.2667f),
		WithClip(Row(ECSStrikeMove::HookRight, true,  false, true,  1.1f,  1.3f,  1.1f,   0.85f, 1.0f,  1.3f,  16.f),
			TEXT("A_Strike_HookRear"), 0.1667f, 0.2667f),
		WithClip(Row(ECSStrikeMove::Kick,      true,  true,  false, 1.8f,  1.6f,  1.6f,   1.45f, 0.9f,  2.2f,  24.f),
			TEXT("A_Strike_FrontKick"), 0.3f, 0.4333f),
	};
}

FString FCSStrikeMoveData::Describe(ECSStrikeMove Move)
{
	switch (Move)
	{
	case ECSStrikeMove::Jab: return TEXT("jab");
	case ECSStrikeMove::Straight: return TEXT("straight");
	case ECSStrikeMove::HookLeft: return TEXT("left hook");
	case ECSStrikeMove::HookRight: return TEXT("right hook");
	case ECSStrikeMove::Kick: return TEXT("kick");
	default: return TEXT("strike");
	}
}
