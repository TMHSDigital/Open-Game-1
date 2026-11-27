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
}

TArray<FCSStrikeMoveData> FCSStrikeMoveData::Defaults()
{
	// The straight is the punch as tuned; the others are read against it. The jab trades weight for
	// speed and cost, the hooks trade reach for weight, and the kick is the slow, far, heavy one.
	return {
		//                          right  leg    hook   windup strike recover reach speed  mass  cost
		Row(ECSStrikeMove::Jab,       false, false, false, 0.6f,  0.8f,  0.7f,   0.85f, 0.85f, 0.55f, 6.f),
		Row(ECSStrikeMove::Straight,  true,  false, false, 1.0f,  1.0f,  1.0f,   1.05f, 1.0f,  1.0f,  12.f),
		Row(ECSStrikeMove::HookLeft,  false, false, true,  1.1f,  1.3f,  1.1f,   0.85f, 1.0f,  1.3f,  16.f),
		Row(ECSStrikeMove::HookRight, true,  false, true,  1.1f,  1.3f,  1.1f,   0.85f, 1.0f,  1.3f,  16.f),
		Row(ECSStrikeMove::Kick,      true,  true,  false, 1.8f,  1.6f,  1.6f,   1.45f, 0.9f,  2.2f,  24.f),
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
