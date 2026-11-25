#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "CSAnimNode_StrideScale.generated.h"

/**
 * Makes the steps of a locomotion pose longer or shorter without changing how fast they come.
 *
 * A clip covers a fixed distance per cycle, so the only way to match it to a ground speed is its
 * play rate, and a clip far from the wanted speed then plays visibly fast or slow. This node is the
 * other lever: each foot is moved along the forward axis, away from or toward the point under its
 * hip, by StrideScale, and the leg is bent to reach it (two-bone IK, thigh and calf, with the knee
 * kept pointing forward and the foot's own rotation kept). A stride of 1.3 with the rate divided by
 * 1.3 covers the same ground with a calmer cadence. UCSAnimInstance splits a wanted speed between
 * the two (SplitStride).
 *
 * It works from the feet the animation gives (no IK bones are needed in the skeleton) and never
 * stretches a leg: a step longer than the leg reaches is cut short along the forward axis, with
 * the foot kept at its height, so large scales lose a little of their length and no foot is lifted
 * off the ground. At scale 1 the node does nothing.
 */
USTRUCT(BlueprintInternalUseOnly)
struct CSCHARACTER_API FCSAnimNode_StrideScale : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	/** 1 leaves the pose alone; above 1 lengthens the steps, below 1 shortens them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stride", meta = (PinShownByDefault))
	float StrideScale = 1.f;

	UPROPERTY(EditAnywhere, Category = "Stride")
	FBoneReference LeftThigh = FBoneReference(TEXT("thigh_l"));

	UPROPERTY(EditAnywhere, Category = "Stride")
	FBoneReference LeftCalf = FBoneReference(TEXT("calf_l"));

	UPROPERTY(EditAnywhere, Category = "Stride")
	FBoneReference LeftFoot = FBoneReference(TEXT("foot_l"));

	UPROPERTY(EditAnywhere, Category = "Stride")
	FBoneReference RightThigh = FBoneReference(TEXT("thigh_r"));

	UPROPERTY(EditAnywhere, Category = "Stride")
	FBoneReference RightCalf = FBoneReference(TEXT("calf_r"));

	UPROPERTY(EditAnywhere, Category = "Stride")
	FBoneReference RightFoot = FBoneReference(TEXT("foot_r"));

	/** The way the character walks, in the mesh's own space. +Y for a UE5 humanoid mesh. */
	UPROPERTY(EditAnywhere, Category = "Stride")
	FVector ForwardAxis = FVector(0.f, 1.f, 0.f);

	/**
	 * Where a foot goes: its distance ahead of or behind the hip along Forward (unit length) is
	 * multiplied by Scale; sideways and up it stays where it was.
	 */
	static FVector ScaleFoot(const FVector& Foot, const FVector& Hip, const FVector& Forward, float Scale);

	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;

private:
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	void SolveLeg(FComponentSpacePoseContext& Output, const FBoneReference& Thigh, const FBoneReference& Calf, const FBoneReference& Foot,
		const FVector& Forward, TArray<FBoneTransform>& OutBoneTransforms) const;
};
