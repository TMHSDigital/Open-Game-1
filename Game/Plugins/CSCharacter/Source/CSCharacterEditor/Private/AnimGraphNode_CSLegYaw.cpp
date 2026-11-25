#include "AnimGraphNode_CSLegYaw.h"

FText UAnimGraphNode_CSLegYaw::GetControllerDescription() const
{
	return FText::FromString(TEXT("CS Leg Yaw"));
}

FText UAnimGraphNode_CSLegYaw::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GetControllerDescription();
}

FText UAnimGraphNode_CSLegYaw::GetTooltipText() const
{
	return FText::FromString(TEXT("Turns the pelvis and legs about the vertical by Yaw and keeps the first spine bone as it was, so the legs follow the movement and the upper body keeps facing the aim."));
}
