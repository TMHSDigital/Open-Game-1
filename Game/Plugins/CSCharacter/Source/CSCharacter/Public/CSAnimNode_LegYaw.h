#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "CSAnimNode_LegYaw.generated.h"

/**
 * Turns the legs toward the way the character moves while the upper body keeps facing the aim.
 *
 * The stand-in clips only walk and run forward. Facing an opponent while circling or backing off,
 * the character moves sideways or backward, so the legs of a forward clip are turned toward the
 * movement instead (UCSAnimInstance::ComputeLegYaw; backward the clip also plays in reverse). The
 * pelvis is turned about the vertical by Yaw, which carries the legs and the spine with it, and
 * the first spine bone is put back where the clip had it, so the chest, arms and head do not turn.
 * At 0 the node does nothing.
 */
USTRUCT(BlueprintInternalUseOnly)
struct CSCHARACTER_API FCSAnimNode_LegYaw : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	/** Degrees about the vertical, positive to the character's right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Legs", meta = (PinShownByDefault))
	float Yaw = 0.f;

	UPROPERTY(EditAnywhere, Category = "Legs")
	FBoneReference Pelvis = FBoneReference(TEXT("pelvis"));

	/** The bone above the pelvis that is kept as the clip had it. */
	UPROPERTY(EditAnywhere, Category = "Legs")
	FBoneReference Spine = FBoneReference(TEXT("spine_01"));

	/** The pelvis turned by Yaw degrees about the vertical, in place. */
	static FTransform TurnPelvis(const FTransform& Pelvis, float Yaw);

	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;

private:
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
};
