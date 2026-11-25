#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "CSAnimNode_StrideScale.h"
#include "AnimGraphNode_CSStrideScale.generated.h"

/** The AnimGraph node for FCSAnimNode_StrideScale: lengthens or shortens the steps of the pose. */
UCLASS(MinimalAPI)
class UAnimGraphNode_CSStrideScale : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FCSAnimNode_StrideScale Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

protected:
	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
