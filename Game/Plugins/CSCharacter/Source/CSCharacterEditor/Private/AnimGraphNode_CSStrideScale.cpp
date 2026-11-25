#include "AnimGraphNode_CSStrideScale.h"

FText UAnimGraphNode_CSStrideScale::GetControllerDescription() const
{
	return FText::FromString(TEXT("CS Stride Scale"));
}

FText UAnimGraphNode_CSStrideScale::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GetControllerDescription();
}

FText UAnimGraphNode_CSStrideScale::GetTooltipText() const
{
	return FText::FromString(TEXT("Lengthens or shortens the steps of the pose: each foot is moved along the forward axis from under its hip and the leg bent to reach it."));
}
