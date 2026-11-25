#include "CSAnimNode_LegYaw.h"

#include "Animation/AnimInstanceProxy.h"

FTransform FCSAnimNode_LegYaw::TurnPelvis(const FTransform& InPelvis, float InYaw)
{
	// About the component's vertical, which is the character's: the mesh only differs by a yaw.
	FTransform Out = InPelvis;
	Out.SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(InYaw)) * InPelvis.GetRotation());
	return Out;
}

void FCSAnimNode_LegYaw::GatherDebugData(FNodeDebugData& DebugData)
{
	DebugData.AddDebugItem(FString::Printf(TEXT("%s legs %.0f deg"), *DebugData.GetNodeName(this), Yaw));
	ComponentPose.GatherDebugData(DebugData);
}

void FCSAnimNode_LegYaw::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	Pelvis.Initialize(RequiredBones);
	Spine.Initialize(RequiredBones);
}

bool FCSAnimNode_LegYaw::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return FMath::Abs(Yaw) > 0.25f && Pelvis.IsValidToEvaluate(RequiredBones) && Spine.IsValidToEvaluate(RequiredBones);
}

void FCSAnimNode_LegYaw::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex PelvisIndex = Pelvis.GetCompactPoseIndex(Bones);
	const FCompactPoseBoneIndex SpineIndex = Spine.GetCompactPoseIndex(Bones);
	// Read before anything changes: the spine keeps the component-space transform the clip gave it.
	const FTransform SpineBefore = Output.Pose.GetComponentSpaceTransform(SpineIndex);
	OutBoneTransforms.Add(FBoneTransform(PelvisIndex, TurnPelvis(Output.Pose.GetComponentSpaceTransform(PelvisIndex), Yaw)));
	OutBoneTransforms.Add(FBoneTransform(SpineIndex, SpineBefore));
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}
