#include "CSHitCameraShake.h"

void UCSHitShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = FCameraShakeDuration(Duration);
}

void UCSHitShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
	Elapsed = 0.f;
	for (float& P : Phase)
	{
		P = FMath::FRandRange(0.f, 2.f * PI);
	}
}

void UCSHitShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	Elapsed += Params.DeltaTime;
	// Strongest at once, gone by Duration: the jolt of an impact, not a rumble.
	const float Left = FMath::Clamp(1.f - Elapsed / FMath::Max(Duration, 0.01f), 0.f, 1.f);
	const float A = Left * Left * Params.GetTotalScale();
	auto Wave = [this](int32 I, float Hz) { return FMath::Sin(Elapsed * Hz * 2.f * PI + Phase[I]); };
	OutResult.Rotation = FRotator(Wave(0, 23.f) * RotationAmplitude * A, Wave(1, 29.f) * RotationAmplitude * A, Wave(2, 17.f) * RotationAmplitude * 0.5f * A);
	OutResult.Location = FVector(0.f, Wave(3, 31.f) * LocationAmplitude * A, Wave(4, 27.f) * LocationAmplitude * A);
}

bool UCSHitShakePattern::IsFinishedImpl() const
{
	return Elapsed >= Duration;
}

UCSHitCameraShake::UCSHitCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSingleInstance = false;
	SetRootShakePattern(CreateDefaultSubobject<UCSHitShakePattern>(TEXT("Pattern")));
}
