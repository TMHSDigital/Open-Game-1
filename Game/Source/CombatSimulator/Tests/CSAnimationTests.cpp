// Checks the stand-in locomotion made by Game/Content/Python/setup_animation.py: the character gets
// SK_BaseCharacter and ABP_BaseCharacter from config, and the animation Blueprint reaches each
// locomotion state from the character's real movement.
// Run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Animation
//
// These tests live in the game module, not the CSCharacter plugin, because they load the game's
// assets (/Game/...); the plugin assumes no content paths.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CSAnimInstance.h"
#include "CSAnimNode_StrideScale.h"
#include "CSCharacter.h"
#include "CSMovementTuning.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/AutomationCommon.h"
#include "Tests/CSTestWorld.h"
#include "UObject/StrongObjectPtr.h"

namespace CSAnimationTests
{
	const TCHAR* MeshPath = TEXT("/Game/Characters/Base/SK_BaseCharacter.SK_BaseCharacter");
	const TCHAR* AnimBlueprintPath = TEXT("/Game/Characters/Base/ABP_BaseCharacter.ABP_BaseCharacter");
	const TCHAR* ClipFolder = TEXT("/Game/ThirdParty/Quaternius/UAL1/Retargeted");
	// The retargeted clips the animation Blueprint plays (setup_animation.py CLIPS).
	const TCHAR* Clips[] = {
		TEXT("A_Idle_Loop"), TEXT("A_Walk_Loop"), TEXT("A_Jog_Fwd_Loop"), TEXT("A_Sprint_Loop"),
		TEXT("A_Crouch_Idle_Loop"), TEXT("A_Crouch_Fwd_Loop"), TEXT("A_Jump_Start"), TEXT("A_Jump_Loop"), TEXT("A_Jump_Land"),
	};
	const TCHAR* Regenerate = TEXT(" (missing or stale: run Game/Content/Python/setup_animation.py, see its docstring)");

	// A test world with a 200 m floor and one ACSCharacter on it, ticked at a fixed step.
	struct FAnimSim : FCSTestWorld
	{
		bool Init(FAutomationTestBase& Test) { return FCSTestWorld::Init(Test, nullptr); }

		void Run(const FVector& Input, float Seconds) { FCSTestWorld::Run(Seconds, Input); }

		// Ticks until the animation reaches State or Seconds run out; returns whether it did.
		bool RunUntil(const FVector& Input, ECSLocomotionState State, float Seconds)
		{
			return FCSTestWorld::RunUntil(Seconds, [this, State] { return Anim() && Anim()->GetLocomotionState() == State; }, Input);
		}

		UCSAnimInstance* Anim() const { return Cast<UCSAnimInstance>(Character->GetMesh()->GetAnimInstance()); }
		float Speed2D() const { return Character->GetCharacterMovement()->Velocity.Size2D(); }
	};

	FString StateName(ECSLocomotionState State)
	{
		return StaticEnum<ECSLocomotionState>()->GetNameStringByValue(int64(State));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAnimationAnimBPAssignedTest, "CombatSimulator.CS.Animation.AnimBPAssigned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCSAnimationAnimBPAssignedTest::RunTest(const FString& Parameters)
{
	using namespace CSAnimationTests;

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, AnimBlueprintPath);
	if (!TestNotNull(TEXT("SK_BaseCharacter exists"), Mesh) || !TestNotNull(FString(TEXT("ABP_BaseCharacter exists")) + Regenerate, AnimBP))
	{
		return false;
	}
	TestEqual(TEXT("ABP_BaseCharacter targets the base skeleton"), AnimBP->TargetSkeleton.Get(), Mesh->GetSkeleton());
	TestTrue(TEXT("ABP_BaseCharacter derives from UCSAnimInstance"),
		AnimBP->GeneratedClass && AnimBP->GeneratedClass->IsChildOf(UCSAnimInstance::StaticClass()));

	for (const TCHAR* Clip : Clips)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), ClipFolder, Clip, Clip);
		const UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *Path);
		if (TestNotNull(FString::Printf(TEXT("Retargeted clip %s exists%s"), Clip, Regenerate), Seq))
		{
			TestEqual(FString::Printf(TEXT("%s plays on the base skeleton"), Clip), Seq->GetSkeleton(), Mesh->GetSkeleton());
		}
	}

	FAnimSim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	USkeletalMeshComponent* MeshComp = Sim.Character->GetMesh();
	TestEqual(TEXT("Character mesh is SK_BaseCharacter (DefaultGame.ini [/Script/CSCharacter.CSCharacter] CharacterMesh)"),
		MeshComp->GetSkeletalMeshAsset(), Mesh);
	TestEqual(TEXT("Character uses ABP_BaseCharacter (DefaultGame.ini [/Script/CSCharacter.CSCharacter] CharacterAnimClass)"),
		MeshComp->GetAnimClass(), static_cast<UClass*>(AnimBP->GeneratedClass.Get()));
	TestNotNull(TEXT("An ABP_BaseCharacter instance runs on the mesh"), Sim.Anim());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAnimationLocomotionStatesReachableTest, "CombatSimulator.CS.Animation.LocomotionStatesReachable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCSAnimationLocomotionStatesReachableTest::RunTest(const FString& Parameters)
{
	using namespace CSAnimationTests;

	FAnimSim Sim;
	if (!Sim.Init(*this) || !TestNotNull(FString(TEXT("ABP_BaseCharacter instance on the character")) + Regenerate, Sim.Anim()))
	{
		return false;
	}
	ACSCharacter* C = Sim.Character;
	const FVector Forward(1.f, 0.f, 0.f);

	auto Expect = [this, &Sim](const TCHAR* What, ECSLocomotionState Want, float ExpectedSpeed)
	{
		const UCSAnimInstance* Anim = Sim.Anim();
		TestEqual(FString::Printf(TEXT("%s: state %s (speed %.0f cm/s, anim reads %.0f)"), What,
			*StateName(Anim->GetLocomotionState()), Sim.Speed2D(), Anim->GetGroundSpeed()), Anim->GetLocomotionState(), Want);
		TestNearlyEqual(FString::Printf(TEXT("%s: speed"), What), Sim.Speed2D(), ExpectedSpeed, 5.f);
		TestNearlyEqual(FString::Printf(TEXT("%s: the animation reads the movement speed"), What), Anim->GetGroundSpeed(), Sim.Speed2D(), 1.f);
	};

	// Speeds 0, 180, 380 and 620 cm/s (the gait speeds) select idle, walk, jog and sprint.
	Sim.Run(FVector::ZeroVector, 0.5f);
	Expect(TEXT("Standing still"), ECSLocomotionState::Idle, 0.f);

	C->SetGait(ECSGait::Walk);
	Sim.Run(Forward, 1.5f);
	Expect(TEXT("Walk"), ECSLocomotionState::Walk, C->GetGaitSpeed(ECSGait::Walk));

	C->SetGait(ECSGait::Jog);
	Sim.Run(Forward, 1.5f);
	Expect(TEXT("Jog"), ECSLocomotionState::Jog, C->GetGaitSpeed(ECSGait::Jog));

	C->SetGait(ECSGait::Sprint);
	Sim.Run(Forward, 2.f);
	Expect(TEXT("Sprint"), ECSLocomotionState::Sprint, C->GetGaitSpeed(ECSGait::Sprint));

	// Crouch: crouch walk while moving, crouch idle once stopped.
	C->SetGait(ECSGait::Crouch);
	Sim.Run(Forward, 2.f);
	TestTrue(TEXT("Character is crouched"), C->bIsCrouched);
	Expect(TEXT("Crouch walk"), ECSLocomotionState::CrouchWalk, C->GetGaitSpeed(ECSGait::Crouch));
	Sim.Run(FVector::ZeroVector, 1.5f);
	Expect(TEXT("Crouch idle"), ECSLocomotionState::CrouchIdle, 0.f);

	// Jump from standing: jump start, then the fall loop, then land, then idle.
	C->SetGait(ECSGait::Jog);
	Sim.Run(FVector::ZeroVector, 1.f);
	C->Jump();
	TestTrue(TEXT("Jump start reached after jumping"), Sim.RunUntil(FVector::ZeroVector, ECSLocomotionState::JumpStart, 0.2f));
	C->StopJumping();
	TestTrue(TEXT("Fall loop reached in the air"), Sim.RunUntil(FVector::ZeroVector, ECSLocomotionState::Fall, 1.f));
	TestTrue(TEXT("Land reached on touchdown"), Sim.RunUntil(FVector::ZeroVector, ECSLocomotionState::Land, 1.5f));
	TestTrue(TEXT("Back to idle after landing"), Sim.RunUntil(FVector::ZeroVector, ECSLocomotionState::Idle, 2.f));

	// Every state maps to one AnimGraph pose (the blend list child the graph shows).
	TestEqual(TEXT("Idle uses the ground pose"), UCSAnimInstance::PoseForState(ECSLocomotionState::Idle), ECSLocomotionPose::Ground);
	TestEqual(TEXT("Sprint uses the ground pose"), UCSAnimInstance::PoseForState(ECSLocomotionState::Sprint), ECSLocomotionPose::Ground);
	TestEqual(TEXT("Crouch walk uses the crouch pose"), UCSAnimInstance::PoseForState(ECSLocomotionState::CrouchWalk), ECSLocomotionPose::Crouch);
	TestEqual(TEXT("Fall uses the fall pose"), UCSAnimInstance::PoseForState(ECSLocomotionState::Fall), ECSLocomotionPose::Fall);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAnimationStrideTableFilledTest, "CombatSimulator.CS.Animation.StrideTableFilled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCSAnimationStrideTableFilledTest::RunTest(const FString& Parameters)
{
	using namespace CSAnimationTests;
	const UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, AnimBlueprintPath);
	const UClass* Class = Blueprint ? Blueprint->GeneratedClass.Get() : nullptr;
	const UCSAnimInstance* Defaults = Class ? Cast<UCSAnimInstance>(Class->GetDefaultObject()) : nullptr;
	if (!TestNotNull(FString(TEXT("ABP_BaseCharacter derives from UCSAnimInstance")) + Regenerate, Defaults))
	{
		return false;
	}
	auto Check = [this](const TCHAR* Name, const TArray<FCSStrideSample>& Table, int32 Count)
	{
		if (!TestEqual(FString::Printf(TEXT("%s has one entry per blend sample%s"), Name, Regenerate), Table.Num(), Count))
		{
			return;
		}
		TestEqual(FString::Printf(TEXT("%s: the idle clip stays in place"), Name), Table[0].StrideSpeed, 0.f);
		for (int32 Index = 0; Index < Table.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("%s[%d] has a clip length"), Name, Index), Table[Index].Seconds > 0.1f);
			if (Index > 0)
			{
				// A human stride: between a slow shuffle and a flat-out sprint.
				TestTrue(FString::Printf(TEXT("%s[%d] has a believable stride speed (%.0f cm/s)"), Name, Index, Table[Index].StrideSpeed),
					Table[Index].StrideSpeed > 30.f && Table[Index].StrideSpeed < 1500.f);
			}
		}
	};
	Check(TEXT("GroundStride"), Defaults->GroundStride, 4);
	Check(TEXT("CrouchStride"), Defaults->CrouchStride, 2);
	if (Defaults->GroundStride.Num() == 4)
	{
		TestTrue(TEXT("Walk, jog and sprint clips cover more ground in that order"),
			Defaults->GroundStride[1].StrideSpeed < Defaults->GroundStride[2].StrideSpeed && Defaults->GroundStride[2].StrideSpeed < Defaults->GroundStride[3].StrideSpeed);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAnimationBlendSamplesUnscaledTest, "CombatSimulator.CS.Animation.BlendSamplesUnscaled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCSAnimationBlendSamplesUnscaledTest::RunTest(const FString& Parameters)
{
	using namespace CSAnimationTests;
	struct FSpace { const TCHAR* Path; int32 Samples; };
	const FSpace Spaces[] = {
		{TEXT("/Game/Characters/Base/BS_BaseCharacter_Ground.BS_BaseCharacter_Ground"), 4},
		{TEXT("/Game/Characters/Base/BS_BaseCharacter_Crouch.BS_BaseCharacter_Crouch"), 2},
	};
	for (const FSpace& Space : Spaces)
	{
		const UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, Space.Path);
		if (!TestNotNull(FString(Space.Path) + Regenerate, BlendSpace))
		{
			continue;
		}
		const TArray<FBlendSample>& Samples = BlendSpace->GetBlendSamples();
		if (!TestEqual(FString::Printf(TEXT("%s: sample count%s"), Space.Path, Regenerate), Samples.Num(), Space.Samples))
		{
			continue;
		}
		// One sample on each whole number of the gait axis, each at rate 1: the play rate is worked out at run time.
		TArray<float> Positions;
		for (const FBlendSample& Sample : Samples)
		{
			Positions.Add(float(Sample.SampleValue.X));
			TestEqual(FString::Printf(TEXT("%s: %s plays at rate 1%s"), Space.Path, *GetNameSafe(Sample.Animation), Regenerate), Sample.RateScale, 1.f);
		}
		Positions.Sort();
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("%s: a sample sits at gait %d%s"), Space.Path, Index, Regenerate), Positions[Index], float(Index));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAnimationFeetMatchGroundAfterSpeedChangeTest, "CombatSimulator.CS.Animation.FeetMatchGroundAfterSpeedChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCSAnimationFeetMatchGroundAfterSpeedChangeTest::RunTest(const FString& Parameters)
{
	using namespace CSAnimationTests;
	FAnimSim Sim;
	if (!Sim.Init(*this) || !TestNotNull(FString(TEXT("ABP_BaseCharacter instance on the character")) + Regenerate, Sim.Anim()))
	{
		return false;
	}
	const UCSAnimInstance* Anim = Sim.Anim();
	if (!TestEqual(FString(TEXT("The stride table is filled")) + Regenerate, Anim->GroundStride.Num(), 4))
	{
		return false;
	}
	// A jog speed the clips were never baked for, as a slider on the tuning tab would set it.
	TStrongObjectPtr<UCSMovementTuning> Tuning(NewObject<UCSMovementTuning>(GetTransientPackage()));
	for (const float JogSpeed : {300.f, 500.f})
	{
		Tuning->JogSpeed = JogSpeed;
		Sim.Character->Tuning = Tuning.Get();
		Sim.Character->ApplyTuning();
		Sim.Character->SetGait(ECSGait::Jog);
		Sim.Run(FVector(1.f, 0.f, 0.f), 3.f);
		const float Speed = Sim.Speed2D();
		TestNearlyEqual(FString::Printf(TEXT("Jogs at the tuned %.0f cm/s"), JogSpeed), Speed, JogSpeed, 5.f);
		TestNearlyEqual(FString::Printf(TEXT("At %.0f cm/s the blend sits on the jog clip"), JogSpeed), Anim->GetLocomotionAxis(), 2.f, 0.05f);
		// What the feet cover: the clip's own stride speed, played at the rate, with steps of the stride scale.
		const float FootSpeed = Anim->GetLocomotionPlayRate() * Anim->GetStrideScale() * Anim->GroundStride[2].StrideSpeed;
		AddInfo(FString::Printf(TEXT("Jog at %.0f cm/s: clip stride %.0f cm/s, play rate %.2f, stride x%.2f"),
			Speed, Anim->GroundStride[2].StrideSpeed, Anim->GetLocomotionPlayRate(), Anim->GetStrideScale()));
		TestTrue(FString::Printf(TEXT("At %.0f cm/s the feet cover the ground speed (feet %.0f, ground %.0f)"), JogSpeed, FootSpeed, Speed),
			FMath::Abs(FootSpeed - Speed) <= 0.02f * Speed);
		// The stride takes its share before the rate leaves its band.
		const bool bRateInBand = Anim->GetLocomotionPlayRate() >= Tuning->StrideRateMin - 0.01f && Anim->GetLocomotionPlayRate() <= Tuning->StrideRateMax + 0.01f;
		const bool bStrideAtLimit = Anim->GetStrideScale() <= Tuning->StrideScaleMin + 0.01f || Anim->GetStrideScale() >= Tuning->StrideScaleMax - 0.01f;
		TestTrue(FString::Printf(TEXT("At %.0f cm/s the rate stays in its band unless the stride is at its limit"), JogSpeed), bRateInBand || bStrideAtLimit);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAnimationStrideSplitTest, "CombatSimulator.CS.Animation.StrideTakesWhatTheRateShouldNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCSAnimationStrideSplitTest::RunTest(const FString& Parameters)
{
	// Band 0.9 to 1.15 for the rate, 0.75 to 1.35 for the stride.
	float Stride = 0.f;
	float Rate = UCSAnimInstance::SplitStride(1.f, 0.9f, 1.15f, 0.75f, 1.35f, Stride);
	TestTrue(TEXT("A clip that fits is left alone"), FMath::IsNearlyEqual(Rate, 1.f) && FMath::IsNearlyEqual(Stride, 1.f));
	Rate = UCSAnimInstance::SplitStride(1.1f, 0.9f, 1.15f, 0.75f, 1.35f, Stride);
	TestTrue(TEXT("Inside the band the rate does it all"), FMath::IsNearlyEqual(Rate, 1.1f) && FMath::IsNearlyEqual(Stride, 1.f));
	Rate = UCSAnimInstance::SplitStride(1.38f, 0.9f, 1.15f, 0.75f, 1.35f, Stride);
	TestTrue(TEXT("Past the band the steps get longer and the rate stays at its limit"), FMath::IsNearlyEqual(Rate, 1.15f, 0.001f) && FMath::IsNearlyEqual(Stride, 1.2f, 0.001f));
	Rate = UCSAnimInstance::SplitStride(0.72f, 0.9f, 1.15f, 0.75f, 1.35f, Stride);
	TestTrue(TEXT("Below the band the steps get shorter"), FMath::IsNearlyEqual(Rate, 0.9f, 0.001f) && FMath::IsNearlyEqual(Stride, 0.8f, 0.001f));
	Rate = UCSAnimInstance::SplitStride(1.85f, 0.9f, 1.15f, 0.75f, 1.35f, Stride);
	TestTrue(TEXT("A stride at its limit hands the rest back to the rate"), FMath::IsNearlyEqual(Stride, 1.35f) && FMath::IsNearlyEqual(Rate * Stride, 1.85f, 0.001f));
	TestTrue(TEXT("And that rate is less hurried than without the stride"), Rate < 1.85f && Rate > 1.15f);
	Rate = UCSAnimInstance::SplitStride(1.5f, 1.f, 1.f, 1.f, 1.f, Stride);
	TestTrue(TEXT("With the stride off the rate is what the feet need"), FMath::IsNearlyEqual(Rate, 1.5f) && FMath::IsNearlyEqual(Stride, 1.f));
	Rate = UCSAnimInstance::SplitStride(0.f, 0.9f, 1.15f, 0.75f, 1.35f, Stride);
	TestTrue(TEXT("No rate, no stride"), Rate == 0.f && FMath::IsNearlyEqual(Stride, 1.f));

	// The node: a foot moves along the forward axis only, from under the hip.
	const FVector Hip(10.f, 0.f, 90.f);
	const FVector Forward(0.f, 1.f, 0.f);
	TestTrue(TEXT("A foot ahead of the hip goes farther ahead"), FCSAnimNode_StrideScale::ScaleFoot(FVector(12.f, 40.f, 5.f), Hip, Forward, 1.25f).Equals(FVector(12.f, 50.f, 5.f), 0.001));
	TestTrue(TEXT("A foot behind the hip goes farther behind"), FCSAnimNode_StrideScale::ScaleFoot(FVector(12.f, -40.f, 5.f), Hip, Forward, 1.25f).Equals(FVector(12.f, -50.f, 5.f), 0.001));
	TestTrue(TEXT("A foot under the hip stays"), FCSAnimNode_StrideScale::ScaleFoot(FVector(12.f, 0.f, 5.f), Hip, Forward, 1.25f).Equals(FVector(12.f, 0.f, 5.f), 0.001));
	TestTrue(TEXT("A shorter stride pulls the foot in"), FCSAnimNode_StrideScale::ScaleFoot(FVector(12.f, 40.f, 5.f), Hip, Forward, 0.75f).Equals(FVector(12.f, 30.f, 5.f), 0.001));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
