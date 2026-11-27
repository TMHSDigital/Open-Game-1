#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "CSHitCameraShake.generated.h"

/**
 * A short, sharp jolt of the camera for a hit (decision 0025): the view kicks in a random
 * direction and settles within Duration, its size given by the shake's scale (UCSHitFeedback
 * scales it by the size of the hit). Two frequencies per axis so it never repeats exactly.
 */
UCLASS()
class COMBATSIMULATOR_API UCSHitShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	/** Seconds the jolt lasts. */
	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "0.01", Units = "s"))
	float Duration = 0.22f;

	/** Most the view turns at scale 1, degrees. */
	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "0", Units = "deg"))
	float RotationAmplitude = 1.6f;

	/** Most the view moves at scale 1, cm. */
	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "0", Units = "cm"))
	float LocationAmplitude = 2.5f;

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override;

	float Elapsed = 0.f;
	/** Random phases, one per channel, picked when the shake starts. */
	float Phase[5] = {};
};

UCLASS()
class COMBATSIMULATOR_API UCSHitCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UCSHitCameraShake(const FObjectInitializer& ObjectInitializer);
};
