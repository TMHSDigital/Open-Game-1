#include "CSAnimNode_StrideScale.h"

#include "Animation/AnimInstanceProxy.h"
#include "TwoBoneIK.h"

FVector FCSAnimNode_StrideScale::ScaleFoot(const FVector& Foot, const FVector& Hip, const FVector& Forward, float Scale)
{
	const double Ahead = FVector::DotProduct(Foot - Hip, Forward);
	return Foot + Forward * (Ahead * (double(Scale) - 1.0));
}

void FCSAnimNode_StrideScale::GatherDebugData(FNodeDebugData& DebugData)
{
	DebugData.AddDebugItem(FString::Printf(TEXT("%s stride x%.2f"), *DebugData.GetNodeName(this), StrideScale));
	ComponentPose.GatherDebugData(DebugData);
}

void FCSAnimNode_StrideScale::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	LeftThigh.Initialize(RequiredBones);
	LeftCalf.Initialize(RequiredBones);
	LeftFoot.Initialize(RequiredBones);
	RightThigh.Initialize(RequiredBones);
	RightCalf.Initialize(RequiredBones);
	RightFoot.Initialize(RequiredBones);
}

bool FCSAnimNode_StrideScale::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	// At scale 1 there is nothing to do, and the pose passes through untouched.
	return !FMath::IsNearlyEqual(StrideScale, 1.f, 0.005f) && StrideScale > 0.f && !ForwardAxis.IsNearlyZero()
		&& LeftThigh.IsValidToEvaluate(RequiredBones) && LeftCalf.IsValidToEvaluate(RequiredBones) && LeftFoot.IsValidToEvaluate(RequiredBones)
		&& RightThigh.IsValidToEvaluate(RequiredBones) && RightCalf.IsValidToEvaluate(RequiredBones) && RightFoot.IsValidToEvaluate(RequiredBones);
}

void FCSAnimNode_StrideScale::SolveLeg(FComponentSpacePoseContext& Output, const FBoneReference& Thigh, const FBoneReference& Calf,
	const FBoneReference& Foot, const FVector& Forward, TArray<FBoneTransform>& OutBoneTransforms) const
{
	const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex ThighIndex = Thigh.GetCompactPoseIndex(Bones);
	const FCompactPoseBoneIndex CalfIndex = Calf.GetCompactPoseIndex(Bones);
	const FCompactPoseBoneIndex FootIndex = Foot.GetCompactPoseIndex(Bones);
	FTransform Upper = Output.Pose.GetComponentSpaceTransform(ThighIndex);
	FTransform Lower = Output.Pose.GetComponentSpaceTransform(CalfIndex);
	FTransform End = Output.Pose.GetComponentSpaceTransform(FootIndex);
	const FQuat FootRotation = End.GetRotation();

	const FVector Target = ScaleFoot(End.GetLocation(), Upper.GetLocation(), Forward, StrideScale);
	// The knee keeps pointing the way the character walks.
	const FVector KneeTarget = Lower.GetLocation() + Forward * 100.0;
	AnimationCore::SolveTwoBoneIK(Upper, Lower, End, KneeTarget, Target, false, 1.0, 1.0);
	// The foot stays flat as the clip had it; only where it is has changed.
	End.SetRotation(FootRotation);

	OutBoneTransforms.Add(FBoneTransform(ThighIndex, Upper));
	OutBoneTransforms.Add(FBoneTransform(CalfIndex, Lower));
	OutBoneTransforms.Add(FBoneTransform(FootIndex, End));
}

void FCSAnimNode_StrideScale::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	const FVector Forward = ForwardAxis.GetSafeNormal();
	SolveLeg(Output, LeftThigh, LeftCalf, LeftFoot, Forward, OutBoneTransforms);
	SolveLeg(Output, RightThigh, RightCalf, RightFoot, Forward, OutBoneTransforms);
	// The base class applies them parents first.
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}
