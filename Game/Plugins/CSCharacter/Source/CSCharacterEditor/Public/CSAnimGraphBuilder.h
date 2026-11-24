#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CSAnimGraphBuilder.generated.h"

class UAnimBlueprint;
class UAnimSequence;
class UAnimationAsset;
class UBlendSpace;

/** One sample of a 1D blend space. */
USTRUCT(BlueprintType)
struct FCSBlendSample
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimSequence> Animation = nullptr;

	/** Where on the axis the sample sits (for a speed axis, cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	float Position = 0.f;

	/** Playback rate of this sample, for example axis speed divided by the clip's own stride speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	float RateScale = 1.f;
};

/** One pose of a locomotion graph: a blend space (its X axis driven by the speed variable) or a sequence. */
USTRUCT(BlueprintType)
struct FCSPoseSource
{
	GENERATED_BODY()

	/** A UBlendSpace (1D, X axis) or a UAnimSequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimationAsset> Asset = nullptr;

	/** Sequences only: loop, or hold the last frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bLoop = true;

	/** Blend time into this pose when it becomes active, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	float BlendInSeconds = 0.2f;
};

USTRUCT(BlueprintType)
struct FCSAnimBuildResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bSuccess = false;

	/** What was built, or why it failed. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FString Message;
};

/**
 * Builds animation assets that editor Python cannot: blend space samples and axes, and animation
 * Blueprint graphs (Python and the editor MCP cannot add AnimGraph nodes). Editor only; callable from
 * Python as unreal.CSAnimGraphBuilder.<snake_case_name>.
 */
UCLASS()
class CSCHARACTEREDITOR_API UCSAnimGraphBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Replaces the samples and X axis of a 1D blend space: axis AxisName from 0 to AxisMax, one
	 * sample per entry. No input smoothing, so the pose follows the axis value at once. The caller
	 * saves the asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Animation")
	static FCSAnimBuildResult SetBlendSpace1D(UBlendSpace* BlendSpace, const FString& AxisName, float AxisMax, const TArray<FCSBlendSample>& Samples);

	/**
	 * Rebuilds an animation Blueprint's AnimGraph as
	 *   Poses[i] -> Blend Poses by int (active child = int variable PoseIndexVariable) -> Slot SlotName -> Output Pose
	 * and compiles it. Every blend space's X input reads the float variable SpeedVariable and its
	 * Play Rate input the float variable PlayRateVariable. All three variables must be members of
	 * the Blueprint's parent class. Each pose restarts when it becomes
	 * active (so one-shot sequences play from their start) and blends in over its BlendInSeconds.
	 * Everything else in the AnimGraph is removed, so a rerun gives the same graph.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Animation")
	static FCSAnimBuildResult BuildLocomotionGraph(UAnimBlueprint* AnimBlueprint, const TArray<FCSPoseSource>& Poses,
		FName PoseIndexVariable, FName SpeedVariable, FName PlayRateVariable, FName SlotName);
};
