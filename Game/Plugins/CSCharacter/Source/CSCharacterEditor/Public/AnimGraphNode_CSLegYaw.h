#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "CSAnimNode_LegYaw.h"
#include "AnimGraphNode_CSLegYaw.generated.h"

/** The AnimGraph node for FCSAnimNode_LegYaw: turns the legs toward the movement, the upper body stays. */
UCLASS(MinimalAPI)
class UAnimGraphNode_CSLegYaw : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FCSAnimNode_LegYaw Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

protected:
	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
