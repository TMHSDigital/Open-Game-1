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
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CSAnimInstance.h"
#include "CSCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/AutomationCommon.h"

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
	struct FAnimSim
	{
		FTestWorldWrapper World;
		ACSCharacter* Character = nullptr;
		float Dt = 1.f / 60.f;

		bool Init(FAutomationTestBase& Test)
		{
			if (!World.CreateTestWorld(EWorldType::Game) || !World.BeginPlayInTestWorld())
			{
				World.ForwardErrorMessages(&Test);
				return false;
			}
			UWorld* W = World.GetTestWorld();
			AActor* Floor = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
			UBoxComponent* Box = NewObject<UBoxComponent>(Floor, TEXT("FloorBox"));
			Box->SetBoxExtent(FVector(10000.f, 10000.f, 50.f));
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Floor->SetRootComponent(Box);
			Box->RegisterComponent();
			Box->SetWorldLocation(FVector(0.f, 0.f, -50.f));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Character = W->SpawnActor<ACSCharacter>(ACSCharacter::StaticClass(), FTransform(FVector(0.f, 0.f, 120.f)), Params);
			if (!Character)
			{
				Test.AddError(TEXT("Could not spawn ACSCharacter"));
				return false;
			}
			Character->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			Character->GetCharacterMovement()->SetDefaultMovementMode();
			Run(FVector::ZeroVector, 0.5f);
			return true;
		}

		~FAnimSim()
		{
			if (World.GetTestWorld())
			{
				World.DestroyTestWorld(false);
			}
		}

		void Run(const FVector& Input, float Seconds)
		{
			const int32 Steps = FMath::CeilToInt(Seconds / Dt);
			for (int32 i = 0; i < Steps; ++i)
			{
				if (!Input.IsNearlyZero())
				{
					Character->AddMovementInput(Input, 1.f);
				}
				World.TickTestWorld(Dt);
			}
		}

		// Ticks until the animation reaches State or Seconds run out; returns whether it did.
		bool RunUntil(const FVector& Input, ECSLocomotionState State, float Seconds)
		{
			const int32 Steps = FMath::CeilToInt(Seconds / Dt);
			for (int32 i = 0; i < Steps; ++i)
			{
				if (!Input.IsNearlyZero())
				{
					Character->AddMovementInput(Input, 1.f);
				}
				World.TickTestWorld(Dt);
				if (Anim() && Anim()->GetLocomotionState() == State)
				{
					return true;
				}
			}
			return false;
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

#endif // WITH_DEV_AUTOMATION_TESTS
