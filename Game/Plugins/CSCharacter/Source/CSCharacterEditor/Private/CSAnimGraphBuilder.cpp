#include "CSAnimGraphBuilder.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "AnimGraphNode_BlendListByInt.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_CSLegYaw.h"
#include "AnimGraphNode_CSStrideScale.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AnimNodes/AnimNode_BlendListBase.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace CSAnimGraphBuilderPrivate
{
	FCSAnimBuildResult Fail(const FString& Message)
	{
		FCSAnimBuildResult Result;
		Result.bSuccess = false;
		Result.Message = Message;
		return Result;
	}

	UEdGraphPin* FirstOutput(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBlueprint)
	{
		for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
			{
				return Graph;
			}
		}
		return nullptr;
	}

	UK2Node_VariableGet* MakeGetter(UEdGraph& Graph, FName Variable, int32 X, int32 Y)
	{
		FGraphNodeCreator<UK2Node_VariableGet> Creator(Graph);
		UK2Node_VariableGet* Get = Creator.CreateNode();
		Get->VariableReference.SetSelfMember(Variable);
		Get->NodePosX = X;
		Get->NodePosY = Y;
		Creator.Finalize();
		return Get;
	}

	int32 CountPosePins(UEdGraphNode* Node)
	{
		int32 Count = 0;
		while (Node->FindPin(*FString::Printf(TEXT("BlendPose_%d"), Count), EGPD_Input))
		{
			++Count;
		}
		return Count;
	}
}

FCSAnimBuildResult UCSAnimGraphBuilder::SetBlendSpace1D(UBlendSpace* BlendSpace, const FString& AxisName, float AxisMax, const TArray<FCSBlendSample>& Samples)
{
	using namespace CSAnimGraphBuilderPrivate;
	if (!BlendSpace)
	{
		return Fail(TEXT("No blend space."));
	}
	if (Samples.Num() == 0 || AxisMax <= 0.f)
	{
		return Fail(TEXT("Need at least one sample and AxisMax above 0."));
	}

	// The axis settings and sample list are protected members with no setter; set them through
	// reflection, as the blend space editor's details panel does.
	FStructProperty* ParamsProp = FindFProperty<FStructProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters"));
	FArrayProperty* SamplesProp = FindFProperty<FArrayProperty>(UBlendSpace::StaticClass(), TEXT("SampleData"));
	if (!ParamsProp || !SamplesProp)
	{
		return Fail(TEXT("UBlendSpace has no BlendParameters or SampleData property (engine changed?)."));
	}

	BlendSpace->Modify();
	FBlendParameter* Axis = ParamsProp->ContainerPtrToValuePtr<FBlendParameter>(BlendSpace, 0);
	Axis->DisplayName = AxisName;
	Axis->Min = 0.f;
	Axis->Max = AxisMax;
	Axis->GridNum = 4;
	Axis->bSnapToGrid = false;
	Axis->bWrapInput = false;

	for (int32 Index = BlendSpace->GetNumberOfBlendSamples() - 1; Index >= 0; --Index)
	{
		BlendSpace->DeleteSample(Index);
	}
	for (const FCSBlendSample& Sample : Samples)
	{
		if (!Sample.Animation)
		{
			return Fail(TEXT("A sample has no animation."));
		}
		if (Sample.Position < 0.f || Sample.Position > AxisMax)
		{
			return Fail(FString::Printf(TEXT("Sample %s at %.1f is off the axis (0 to %.1f)."), *Sample.Animation->GetName(), Sample.Position, AxisMax));
		}
		const int32 Index = BlendSpace->AddSample(Sample.Animation, FVector(Sample.Position, 0.f, 0.f));
		if (Index == INDEX_NONE)
		{
			return Fail(FString::Printf(TEXT("Could not add sample %s (wrong skeleton, or two samples at %.1f?)."), *Sample.Animation->GetName(), Sample.Position));
		}
	}

	FScriptArrayHelper SampleArray(SamplesProp, SamplesProp->ContainerPtrToValuePtr<void>(BlendSpace));
	for (int32 Index = 0; Index < SampleArray.Num(); ++Index)
	{
		FBlendSample& Stored = *reinterpret_cast<FBlendSample*>(SampleArray.GetRawPtr(Index));
		for (const FCSBlendSample& Sample : Samples)
		{
			if (Stored.Animation == Sample.Animation && FMath::IsNearlyEqual(Stored.SampleValue.X, Sample.Position))
			{
				Stored.RateScale = FMath::Clamp(Sample.RateScale, 0.01f, 64.f);
			}
		}
	}

	// Samples must be validated and the runtime data resampled, or the blend space plays the
	// reference pose outside the editor.
	BlendSpace->ValidateSampleData();
	BlendSpace->ResampleData();
	BlendSpace->PostEditChange();
	BlendSpace->MarkPackageDirty();

	FCSAnimBuildResult Result;
	Result.bSuccess = BlendSpace->GetNumberOfBlendSamples() == Samples.Num();
	Result.Message = FString::Printf(TEXT("%s: %d samples on %s 0 to %.0f"), *BlendSpace->GetName(), BlendSpace->GetNumberOfBlendSamples(), *AxisName, AxisMax);
	return Result;
}

FCSAnimBuildResult UCSAnimGraphBuilder::BuildLocomotionGraph(UAnimBlueprint* AnimBlueprint, const TArray<FCSPoseSource>& Poses,
	FName PoseIndexVariable, FName SpeedVariable, FName PlayRateVariable, FName SlotName, FName StrideScaleVariable, FName LegYawVariable)
{
	using namespace CSAnimGraphBuilderPrivate;
	if (!AnimBlueprint || Poses.Num() == 0)
	{
		return Fail(TEXT("Need an animation Blueprint and at least one pose."));
	}
	UClass* Parent = AnimBlueprint->ParentClass;
	if (!Parent || !FindFProperty<FIntProperty>(Parent, PoseIndexVariable) || !FindFProperty<FFloatProperty>(Parent, SpeedVariable)
		|| !FindFProperty<FFloatProperty>(Parent, PlayRateVariable))
	{
		return Fail(FString::Printf(TEXT("Parent class %s needs an int32 %s and floats %s and %s."),
			Parent ? *Parent->GetName() : TEXT("(none)"), *PoseIndexVariable.ToString(), *SpeedVariable.ToString(), *PlayRateVariable.ToString()));
	}
	UEdGraph* Graph = FindAnimGraph(AnimBlueprint);
	if (!Graph)
	{
		return Fail(TEXT("The Blueprint has no AnimGraph."));
	}

	// Keep only the Output Pose node.
	UAnimGraphNode_Root* Root = nullptr;
	TArray<UEdGraphNode*> ToRemove;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UAnimGraphNode_Root* AsRoot = Cast<UAnimGraphNode_Root>(Node))
		{
			Root = AsRoot;
		}
		else
		{
			ToRemove.Add(Node);
		}
	}
	if (!Root)
	{
		return Fail(TEXT("The AnimGraph has no Output Pose node."));
	}
	Graph->Modify();
	for (UEdGraphNode* Node : ToRemove)
	{
		Graph->RemoveNode(Node);
	}
	const UEdGraphSchema* Schema = Graph->GetSchema();
	const int32 X0 = Root->NodePosX;
	const int32 Y0 = Root->NodePosY;

	// Slot (montages play here) -> Output Pose.
	FGraphNodeCreator<UAnimGraphNode_Slot> SlotCreator(*Graph);
	UAnimGraphNode_Slot* Slot = SlotCreator.CreateNode();
	Slot->Node.SlotName = SlotName;
	Slot->NodePosX = X0 - 300;
	Slot->NodePosY = Y0;
	SlotCreator.Finalize();
	UEdGraphPin* SlotIn = Slot->FindPin(TEXT("Source"), EGPD_Input);
	UEdGraphPin* RootIn = Root->FindPin(TEXT("Result"), EGPD_Input);
	if (!SlotIn || !RootIn || !Schema->TryCreateConnection(FirstOutput(Slot), RootIn))
	{
		return Fail(TEXT("Could not connect the slot to the output pose."));
	}

	// Blend Poses by int, one child per pose; every child restarts when it becomes active.
	FGraphNodeCreator<UAnimGraphNode_BlendListByInt> ListCreator(*Graph);
	UAnimGraphNode_BlendListByInt* List = ListCreator.CreateNode();
	List->NodePosX = X0 - 650;
	List->NodePosY = Y0;
	ListCreator.Finalize();
	while (CountPosePins(List) < Poses.Num())
	{
		List->AddPinToBlendList();
	}
	if (CountPosePins(List) != Poses.Num())
	{
		return Fail(FString::Printf(TEXT("Blend list has %d pose pins; wanted %d."), CountPosePins(List), Poses.Num()));
	}
	if (FEnumProperty* Mode = FindFProperty<FEnumProperty>(FAnimNode_BlendListBase::StaticStruct(), TEXT("ChildUpateMode")))
	{
		Mode->GetUnderlyingProperty()->SetIntPropertyValue(Mode->ContainerPtrToValuePtr<void>(&List->Node),
			int64(EBlendListChildUpdateMode::ResetChildOnActivate));
	}
	else
	{
		return Fail(TEXT("FAnimNode_BlendListBase has no ChildUpateMode property (engine changed?)."));
	}
	if (StrideScaleVariable.IsNone())
	{
		if (!Schema->TryCreateConnection(FirstOutput(List), SlotIn))
		{
			return Fail(TEXT("Could not connect the blend list to the slot."));
		}
	}
	else
	{
		// Blend list -> component space -> stride scale -> local space -> slot.
		FGraphNodeCreator<UAnimGraphNode_LocalToComponentSpace> ToComponentCreator(*Graph);
		UAnimGraphNode_LocalToComponentSpace* ToComponent = ToComponentCreator.CreateNode();
		ToComponent->NodePosX = X0 - 560;
		ToComponent->NodePosY = Y0 + 160;
		ToComponentCreator.Finalize();
		FGraphNodeCreator<UAnimGraphNode_CSStrideScale> StrideCreator(*Graph);
		UAnimGraphNode_CSStrideScale* Stride = StrideCreator.CreateNode();
		Stride->NodePosX = X0 - 480;
		Stride->NodePosY = Y0 + 160;
		StrideCreator.Finalize();
		FGraphNodeCreator<UAnimGraphNode_ComponentToLocalSpace> ToLocalCreator(*Graph);
		UAnimGraphNode_ComponentToLocalSpace* ToLocal = ToLocalCreator.CreateNode();
		ToLocal->NodePosX = X0 - 380;
		ToLocal->NodePosY = Y0 + 160;
		ToLocalCreator.Finalize();
		UK2Node_VariableGet* GetStride = MakeGetter(*Graph, StrideScaleVariable, X0 - 700, Y0 + 260);

		UEdGraphPin* ToComponentIn = ToComponent->FindPin(TEXT("LocalPose"), EGPD_Input);
		UEdGraphPin* StrideIn = Stride->FindPin(TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* ScaleIn = Stride->FindPin(TEXT("StrideScale"), EGPD_Input);
		UEdGraphPin* ToLocalIn = ToLocal->FindPin(TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* ScaleOut = GetStride->FindPin(StrideScaleVariable, EGPD_Output);
		if (!ToComponentIn || !StrideIn || !ScaleIn || !ToLocalIn || !ScaleOut)
		{
			return Fail(TEXT("The stride scale nodes do not have the expected pins (engine changed?), or the stride variable is not a member of the parent class."));
		}
		if (!Schema->TryCreateConnection(FirstOutput(List), ToComponentIn) || !Schema->TryCreateConnection(FirstOutput(ToComponent), StrideIn)
			|| !Schema->TryCreateConnection(ScaleOut, ScaleIn) || !Schema->TryCreateConnection(FirstOutput(ToLocal), SlotIn))
		{
			return Fail(TEXT("Could not connect the blend list through the stride scale to the slot."));
		}
		UEdGraphNode* LastComponentNode = Stride;
		if (!LegYawVariable.IsNone())
		{
			FGraphNodeCreator<UAnimGraphNode_CSLegYaw> YawCreator(*Graph);
			UAnimGraphNode_CSLegYaw* Yaw = YawCreator.CreateNode();
			Yaw->NodePosX = X0 - 430;
			Yaw->NodePosY = Y0 + 320;
			YawCreator.Finalize();
			UK2Node_VariableGet* GetYaw = MakeGetter(*Graph, LegYawVariable, X0 - 700, Y0 + 400);
			UEdGraphPin* YawPoseIn = Yaw->FindPin(TEXT("ComponentPose"), EGPD_Input);
			UEdGraphPin* YawIn = Yaw->FindPin(TEXT("Yaw"), EGPD_Input);
			UEdGraphPin* YawOut = GetYaw->FindPin(LegYawVariable, EGPD_Output);
			if (!YawPoseIn || !YawIn || !YawOut || !Schema->TryCreateConnection(FirstOutput(Stride), YawPoseIn) || !Schema->TryCreateConnection(YawOut, YawIn))
			{
				return Fail(TEXT("Could not connect the leg yaw node (or the leg yaw variable is not a member of the parent class)."));
			}
			LastComponentNode = Yaw;
		}
		if (!Schema->TryCreateConnection(FirstOutput(LastComponentNode), ToLocalIn))
		{
			return Fail(TEXT("Could not connect the stride scale to local space."));
		}
	}
	UK2Node_VariableGet* GetIndex = MakeGetter(*Graph, PoseIndexVariable, X0 - 900, Y0 - 150);
	UEdGraphPin* IndexOut = GetIndex->FindPin(PoseIndexVariable, EGPD_Output);
	UEdGraphPin* ActiveIn = List->FindPin(TEXT("ActiveChildIndex"), EGPD_Input);
	if (!IndexOut || !ActiveIn || !Schema->TryCreateConnection(IndexOut, ActiveIn))
	{
		return Fail(TEXT("Could not connect the pose index variable to ActiveChildIndex."));
	}

	FString Built;
	for (int32 Index = 0; Index < Poses.Num(); ++Index)
	{
		const FCSPoseSource& Pose = Poses[Index];
		const int32 Y = Y0 + 200 * (Index - Poses.Num() / 2);
		UEdGraphNode* Player = nullptr;
		if (UBlendSpace* AsBlendSpace = Cast<UBlendSpace>(Pose.Asset))
		{
			FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> Creator(*Graph);
			UAnimGraphNode_BlendSpacePlayer* Node = Creator.CreateNode();
			Node->SetAnimationAsset(AsBlendSpace);
			Node->NodePosX = X0 - 1100;
			Node->NodePosY = Y;
			Creator.Finalize();
			UK2Node_VariableGet* GetSpeed = MakeGetter(*Graph, SpeedVariable, X0 - 1400, Y + 40);
			UEdGraphPin* SpeedOut = GetSpeed->FindPin(SpeedVariable, EGPD_Output);
			UEdGraphPin* XIn = Node->FindPin(TEXT("X"), EGPD_Input);
			if (!SpeedOut || !XIn || !Schema->TryCreateConnection(SpeedOut, XIn))
			{
				return Fail(FString::Printf(TEXT("Could not drive %s's X axis from %s."), *AsBlendSpace->GetName(), *SpeedVariable.ToString()));
			}
			// Play Rate is an optional pin: show it, then drive it.
			for (FOptionalPinFromProperty& Optional : Node->ShowPinForProperties)
			{
				if (Optional.PropertyName == TEXT("PlayRate"))
				{
					Optional.bShowPin = true;
				}
			}
			Node->ReconstructNode();
			UK2Node_VariableGet* GetRate = MakeGetter(*Graph, PlayRateVariable, X0 - 1400, Y + 110);
			UEdGraphPin* RateOut = GetRate->FindPin(PlayRateVariable, EGPD_Output);
			UEdGraphPin* RateIn = Node->FindPin(TEXT("PlayRate"), EGPD_Input);
			// ReconstructNode replaced the pins: the X link made above is on the new X pin.
			UEdGraphPin* XAfter = Node->FindPin(TEXT("X"), EGPD_Input);
			if (!RateOut || !RateIn || !Schema->TryCreateConnection(RateOut, RateIn) || !XAfter || XAfter->LinkedTo.Num() != 1)
			{
				return Fail(FString::Printf(TEXT("Could not drive %s's play rate from %s."), *AsBlendSpace->GetName(), *PlayRateVariable.ToString()));
			}
			Player = Node;
		}
		else if (UAnimSequence* AsSequence = Cast<UAnimSequence>(Pose.Asset))
		{
			FGraphNodeCreator<UAnimGraphNode_SequencePlayer> Creator(*Graph);
			UAnimGraphNode_SequencePlayer* Node = Creator.CreateNode();
			Node->SetAnimationAsset(AsSequence);
			Node->Node.SetLoopAnimation(Pose.bLoop);
			Node->NodePosX = X0 - 1100;
			Node->NodePosY = Y;
			Creator.Finalize();
			Player = Node;
		}
		else
		{
			return Fail(FString::Printf(TEXT("Pose %d is neither a blend space nor a sequence."), Index));
		}

		UEdGraphPin* PoseIn = List->FindPin(*FString::Printf(TEXT("BlendPose_%d"), Index), EGPD_Input);
		UEdGraphPin* TimeIn = List->FindPin(*FString::Printf(TEXT("BlendTime_%d"), Index), EGPD_Input);
		if (!PoseIn || !TimeIn || !Schema->TryCreateConnection(FirstOutput(Player), PoseIn))
		{
			return Fail(FString::Printf(TEXT("Could not connect pose %d to the blend list."), Index));
		}
		Schema->TrySetDefaultValue(*TimeIn, FString::SanitizeFloat(FMath::Max(0.f, Pose.BlendInSeconds)));
		Built += FString::Printf(TEXT("\n  pose %d: %s (blend in %.2f s%s)"), Index, *Pose.Asset->GetName(), Pose.BlendInSeconds,
			Cast<UAnimSequence>(Pose.Asset) ? (Pose.bLoop ? TEXT(", loops") : TEXT(", plays once")) : TEXT(""));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	if (AnimBlueprint->Status == BS_Error)
	{
		return Fail(TEXT("The animation Blueprint did not compile; open it in the editor to see the errors."));
	}

	FCSAnimBuildResult Result;
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("%s: Blend Poses by %s ->%s Slot %s -> Output Pose%s"),
		*AnimBlueprint->GetName(), *PoseIndexVariable.ToString(),
		StrideScaleVariable.IsNone() ? TEXT("") : *FString::Printf(TEXT(" CS Stride Scale by %s ->%s"), *StrideScaleVariable.ToString(),
			LegYawVariable.IsNone() ? TEXT("") : *FString::Printf(TEXT(" CS Leg Yaw by %s ->"), *LegYawVariable.ToString())),
		*SlotName.ToString(), *Built);
	return Result;
}
