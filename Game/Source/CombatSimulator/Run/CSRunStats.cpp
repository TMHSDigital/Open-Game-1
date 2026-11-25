#include "CSRunStats.h"

void FCSRunHits::Record(const FCSContactRecord& Hit)
{
	++Hits;
	switch (Hit.Result)
	{
	case ECSContactResult::Stagger:
		++Staggers;
		break;
	case ECSContactResult::Ragdoll:
		++Knockdowns;
		break;
	default:
		++NoEffect;
		break;
	}
	Biggest = FMath::Max(Biggest, Hit.VelocityChange);
	Total += Hit.VelocityChange;
	if (!Hit.Bone.IsNone())
	{
		++ByBone.FindOrAdd(Hit.Bone);
	}
}

FString FCSRunHits::DescribeBones(int32 MaxParts) const
{
	TArray<TPair<FName, int32>> Parts;
	for (const TPair<FName, int32>& Pair : ByBone)
	{
		Parts.Add(Pair);
	}
	Parts.Sort([](const TPair<FName, int32>& A, const TPair<FName, int32>& B)
	{
		return A.Value != B.Value ? A.Value > B.Value : A.Key.LexicalLess(B.Key);
	});
	TArray<FString> Out;
	for (int32 Index = 0; Index < Parts.Num() && Index < MaxParts; ++Index)
	{
		Out.Add(FString::Printf(TEXT("%s %d"), *Parts[Index].Key.ToString(), Parts[Index].Value));
	}
	if (Parts.Num() > MaxParts)
	{
		Out.Add(FString::Printf(TEXT("and %d more"), Parts.Num() - MaxParts));
	}
	return FString::Join(Out, TEXT(", "));
}

void FCSRunStats::RecordHit(ECSRunHitSide Side, const FCSContactRecord& Hit, bool bPlayerBall)
{
	if (Hit.Result == ECSContactResult::None)
	{
		return;
	}
	switch (Side)
	{
	case ECSRunHitSide::Dealt:
		Dealt.Record(Hit);
		BallHits += bPlayerBall ? 1 : 0;
		break;
	case ECSRunHitSide::Taken:
		Taken.Record(Hit);
		break;
	default:
		++OtherHits;
		break;
	}
}

float FCSRunStats::Accuracy() const
{
	return BallsThrown > 0 ? FMath::Min(1.f, float(BallHits) / float(BallsThrown)) : 0.f;
}

FString FCSRunStats::FormatTime(float Seconds)
{
	const int32 Whole = FMath::Max(0, FMath::FloorToInt(Seconds));
	return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}

FString FCSRunStats::DescribeEnd(ECSRunEnd End)
{
	switch (End)
	{
	case ECSRunEnd::Manual: return TEXT("ended by you");
	case ECSRunEnd::TimeLimit: return TEXT("time limit reached");
	case ECSRunEnd::KnockdownGoal: return TEXT("knockdown goal reached");
	default: return FString();
	}
}

FString FCSRunStats::HudLine(float TimeLimit, int32 KnockdownGoal) const
{
	FString Line = TEXT("Run ") + FormatTime(Seconds);
	if (TimeLimit > 0.f)
	{
		Line += TEXT(" / ") + FormatTime(TimeLimit);
	}
	Line += FString::Printf(TEXT("   hits %d   knockdowns %d"), Dealt.Hits, Dealt.Knockdowns);
	if (KnockdownGoal > 0)
	{
		Line += FString::Printf(TEXT(" / %d"), KnockdownGoal);
	}
	Line += FString::Printf(TEXT("   taken %d"), Taken.Hits);
	return Line;
}

TArray<FString> FCSRunStats::SummaryLines() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Time: %s"), *FormatTime(Seconds)));
	Lines.Add(FString::Printf(TEXT("Balls thrown: %d"), BallsThrown));
	if (BallsThrown > 0)
	{
		Lines.Add(FString::Printf(TEXT("Accuracy: %d%%"), FMath::RoundToInt(Accuracy() * 100.f)));
	}
	auto Side = [&Lines](const TCHAR* Name, const FCSRunHits& H)
	{
		Lines.Add(FString::Printf(TEXT("%s: %d (%d staggers, %d knockdowns, %d with no effect)"), Name, H.Hits, H.Staggers, H.Knockdowns, H.NoEffect));
		if (H.Hits > 0)
		{
			Lines.Add(FString::Printf(TEXT("  biggest %d cm/s, average %d cm/s"), FMath::RoundToInt(H.Biggest), FMath::RoundToInt(H.Average())));
			Lines.Add(TEXT("  where: ") + H.DescribeBones());
		}
	};
	Side(TEXT("Hits dealt"), Dealt);
	Side(TEXT("Hits taken"), Taken);
	if (OtherHits > 0)
	{
		Lines.Add(FString::Printf(TEXT("Hits between others: %d"), OtherHits));
	}
	return Lines;
}
