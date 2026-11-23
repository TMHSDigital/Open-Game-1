// Physics-driven body checks (stagger, ragdoll, recovery, joint limits) on the base character.
// Run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Body
//
// These tests live in the game module, not the CSCharacter plugin, because they need the game's
// assets: SK_BaseCharacter and its physics asset PHYS_BaseCharacter (loaded through the character's
// config) and ABP_BaseCharacter. The plugin assumes no content paths. Asset-free checks of the body
// component are in the plugin (Private/Tests/CSPhysicsBodyTests.cpp).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CSCameraRig.h"
#include "CSCharacter.h"
#include "CSDevTools.h"
#include "CSMovementTuning.h"
#include "CSPhysicsBodyComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

namespace CSPhysicsBodyTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	const TCHAR* Regenerate = TEXT(" (missing or stale: run Game/Content/Python/import_base_character.py, see its docstring)");

	// A test world with a 200 m floor box (top at z = 0, optionally tilted, rising toward +X), extra
	// blocks, and one ACSCharacter (mesh, physics asset and animation from config) on it, ticked at 60 Hz.
	struct FBodySim
	{
		FTestWorldWrapper World;
		ACSCharacter* Character = nullptr;
		UCSPhysicsBodyComponent* PhysicsBody = nullptr;
		APlayerController* Controller = nullptr;
		TStrongObjectPtr<UCSMovementTuning> Tuning;
		float Dt = 1.f / 60.f;
		float Time = 0.f;

		bool Init(FAutomationTestBase& Test, float FloorPitchDeg = 0.f, const FVector& Spawn = FVector(0.f, 0.f, 120.f), bool bSettle = true)
		{
			if (!World.CreateTestWorld(EWorldType::Game) || !World.BeginPlayInTestWorld())
			{
				World.ForwardErrorMessages(&Test);
				return false;
			}
			AddBox(FVector(0.f, 0.f, -50.f), FVector(10000.f, 10000.f, 50.f), FRotator(FloorPitchDeg, 0.f, 0.f));

			Tuning.Reset(NewObject<UCSMovementTuning>(GetTransientPackage()));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.bDeferConstruction = true;
			const FTransform SpawnTransform(Spawn);
			Character = World.GetTestWorld()->SpawnActor<ACSCharacter>(ACSCharacter::StaticClass(), SpawnTransform, Params);
			if (!Character)
			{
				Test.AddError(TEXT("Could not spawn the character"));
				return false;
			}
			Character->Tuning = Tuning.Get();
			Character->FinishSpawning(SpawnTransform);
			Character->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			Character->GetCharacterMovement()->SetDefaultMovementMode();
			PhysicsBody = Character->GetPhysicsBody();
			if (!Test.TestNotNull(TEXT("ACSCharacter has a physics body component"), PhysicsBody))
			{
				return false;
			}
			if (!Test.TestTrue(FString(TEXT("The character's mesh has a physics asset with bodies")) + Regenerate, PhysicsBody->HasPhysicsBody()))
			{
				return false;
			}
			if (bSettle)
			{
				Run(0.5f);
				if (!Test.TestTrue(TEXT("The character stands on the floor before the test"), Character->GetCharacterMovement()->IsMovingOnGround()))
				{
					return false;
				}
			}
			return true;
		}

		~FBodySim()
		{
			if (World.GetTestWorld())
			{
				World.DestroyTestWorld(false);
			}
		}

		UBoxComponent* AddBox(const FVector& Centre, const FVector& Extent, const FRotator& Rotation = FRotator::ZeroRotator)
		{
			UWorld* W = World.GetTestWorld();
			AActor* Actor = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Actor->SetRootComponent(Box);
			Box->RegisterComponent();
			Box->SetWorldLocationAndRotation(Centre, Rotation);
			return Box;
		}

		void Possess()
		{
			Controller = World.GetTestWorld()->SpawnActor<APlayerController>();
			Controller->Possess(Character);
		}

		void Step(const FVector& Input = FVector::ZeroVector)
		{
			if (!Input.IsNearlyZero())
			{
				Character->AddMovementInput(Input, 1.f);
			}
			World.TickTestWorld(Dt);
			Time += Dt;
		}

		void Run(float Seconds, const FVector& Input = FVector::ZeroVector)
		{
			for (int32 i = 0; i < FMath::CeilToInt(Seconds / Dt); ++i)
			{
				Step(Input);
			}
		}

		// Steps until Done returns true or Seconds pass. Returns whether Done became true.
		template <typename F>
		bool RunUntil(float Seconds, F&& Done)
		{
			for (int32 i = 0; i < FMath::CeilToInt(Seconds / Dt); ++i)
			{
				Step();
				if (Done())
				{
					return true;
				}
			}
			return false;
		}

		USkeletalMeshComponent* Mesh() const { return Character->GetMesh(); }
		UCapsuleComponent* Capsule() const { return Character->GetCapsuleComponent(); }
		ECSBodyState State() const { return PhysicsBody->GetState(); }
		float Mass() const { return PhysicsBody->GetBodyMass(); }

		// An impulse whose velocity change (impulse / mass) is Speed, cm/s, along Direction.
		FVector ImpulseFor(float Speed, const FVector& Direction) const { return Direction.GetSafeNormal() * Speed * Mass(); }

		float PelvisSpeed() const { return Mesh()->GetPhysicsLinearVelocity(TEXT("pelvis")).Size(); }

		// Rotates every body about Pivot and moves it by Offset (a teleport): puts a ragdoll in a bad spot.
		void MoveBodies(const FQuat& Rotation, const FVector& Pivot, const FVector& Offset)
		{
			for (FBodyInstance* B : Mesh()->Bodies)
			{
				if (B && B->IsValidBodyInstance())
				{
					const FTransform T = B->GetUnrealWorldTransform();
					const FTransform Moved(Rotation * T.GetRotation(), Pivot + Rotation.RotateVector(T.GetLocation() - Pivot) + Offset);
					B->SetBodyTransform(Moved, ETeleportType::TeleportPhysics);
					B->SetLinearVelocity(FVector::ZeroVector, false);
					B->SetAngularVelocityInRadians(FVector::ZeroVector, false);
				}
			}
		}
	};

	// The checks of Review Focus 1 on the final capsule: upright, on walkable ground, above the floor, not inside anything.
	void CheckStanding(FAutomationTestBase& Test, const FBodySim& Sim, const FString& Case)
	{
		const ACSCharacter* C = Sim.Character;
		const UCharacterMovementComponent* Move = C->GetCharacterMovement();
		const FRotator R = C->GetActorRotation();
		Test.AddInfo(FString::Printf(TEXT("%s: capsule at %s, rotation %s, mode %d, floor normal %s"), *Case,
			*C->GetActorLocation().ToString(), *R.ToString(), int32(Move->MovementMode.GetValue()), *Move->CurrentFloor.HitResult.ImpactNormal.ToString()));
		Test.TestTrue(Case + TEXT(": the capsule is upright (no pitch or roll)"), FMath::Abs(R.Pitch) < 0.5f && FMath::Abs(R.Roll) < 0.5f);
		Test.TestTrue(Case + TEXT(": the capsule's up is world up"), C->GetCapsuleComponent()->GetUpVector().Equals(FVector::UpVector, 0.01f));
		Test.TestTrue(Case + TEXT(": walking on the ground"), Move->IsMovingOnGround());
		Test.TestTrue(Case + TEXT(": the floor is walkable"), Move->CurrentFloor.IsWalkableFloor());

		// The ground straight below the capsule: the capsule bottom is above it (never under the floor).
		FHitResult Down;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CSBodyTestFloor), false, C);
		const FVector Centre = C->GetActorLocation();
		const float HalfHeight = C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const bool bFloor = C->GetWorld()->LineTraceSingleByChannel(Down, Centre, Centre - FVector(0.f, 0.f, 1000.f), ECC_WorldStatic, Params);
		if (Test.TestTrue(Case + TEXT(": there is ground below the capsule"), bFloor))
		{
			const float Bottom = Centre.Z - HalfHeight;
			Test.AddInfo(FString::Printf(TEXT("%s: capsule bottom %.2f, ground below the centre %.2f"), *Case, Bottom, Down.ImpactPoint.Z));
			Test.TestTrue(Case + TEXT(": the capsule bottom is not below the ground under it"), Bottom >= Down.ImpactPoint.Z - 1.f);
			Test.TestTrue(Case + TEXT(": the capsule centre is above the ground"), Centre.Z > Down.ImpactPoint.Z);
		}
		// The body stands on the capsule: the mesh is back in its place and the pose is the animated
		// standing pose, not the ragdoll's.
		const FVector Head = C->GetMesh()->GetBoneLocation(TEXT("head"));
		const FVector Pelvis = C->GetMesh()->GetBoneLocation(TEXT("pelvis"));
		Test.AddInfo(FString::Printf(TEXT("%s: head %.1f cm and pelvis %.1f cm above the capsule bottom; mesh relative rotation %s"), *Case,
			Head.Z - (Centre.Z - HalfHeight), Pelvis.Z - (Centre.Z - HalfHeight), *C->GetMesh()->GetRelativeRotation().ToString()));
		Test.TestTrue(Case + TEXT(": the head is at standing height (140 cm or more above the capsule bottom)"), Head.Z - (Centre.Z - HalfHeight) >= 140.f);
		Test.TestTrue(Case + TEXT(": the head is above the pelvis by 50 cm or more"), Head.Z - Pelvis.Z >= 50.f);
		Test.TestTrue(Case + TEXT(": the mesh is back in its place on the capsule"),
			C->GetMesh()->GetRelativeRotation().Equals(FRotator(0.f, -90.f, 0.f), 0.5f) && C->GetMesh()->GetRelativeLocation().Equals(FVector(0.f, 0.f, -HalfHeight), 0.5f));
		const bool bOverlaps = C->GetWorld()->OverlapBlockingTestByChannel(Centre, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(C->GetCapsuleComponent()->GetScaledCapsuleRadius() - 1.f, HalfHeight - 1.f), Params);
		Test.TestFalse(Case + TEXT(": the capsule is not inside geometry"), bOverlaps);
	}

	// Signed bend of a hinge from straight, degrees: positive is flexion. Parent and Child are the
	// world directions of the two bones; Front is the parent's anterior (the side flexion moves toward
	// for an elbow) or posterior (for a knee) direction.
	float HingeBendDeg(const FVector& Parent, const FVector& Child, const FVector& FlexSide)
	{
		const FVector Side = (FlexSide - Parent * FVector::DotProduct(FlexSide, Parent)).GetSafeNormal();
		return FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Child, Side), FVector::DotProduct(Child, Parent)));
	}

	// The character's input actions, filled with transient actions, and the component they are bound on
	// (as in the plugin's dev tools tests).
	struct FInputRig
	{
		TMap<FName, TStrongObjectPtr<UInputAction>> Actions;
		TStrongObjectPtr<UEnhancedInputComponent> Input;

		void Make(FName Name, EInputActionValueType Type, TSoftObjectPtr<UInputAction>& Slot)
		{
			UInputAction* Action = NewObject<UInputAction>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UInputAction::StaticClass(), Name));
			Action->ValueType = Type;
			Slot = Action;
			Actions.Add(Name, TStrongObjectPtr<UInputAction>(Action));
		}

		void Bind(ACSCharacter& Character)
		{
			Make(TEXT("Move"), EInputActionValueType::Axis2D, Character.MoveAction);
			Make(TEXT("Look"), EInputActionValueType::Axis2D, Character.LookAction);
			Make(TEXT("Jump"), EInputActionValueType::Boolean, Character.JumpAction);
			Make(TEXT("Sprint"), EInputActionValueType::Boolean, Character.SprintAction);
			Make(TEXT("Crouch"), EInputActionValueType::Boolean, Character.CrouchAction);
			Make(TEXT("Walk"), EInputActionValueType::Boolean, Character.WalkAction);
			Make(TEXT("ToggleCamera"), EInputActionValueType::Boolean, Character.ToggleCameraAction);
			Make(TEXT("DebugOverlay"), EInputActionValueType::Boolean, Character.DebugOverlayAction);
			Make(TEXT("DebugRagdoll"), EInputActionValueType::Boolean, Character.DebugRagdollAction);
			Make(TEXT("DebugPush"), EInputActionValueType::Boolean, Character.DebugPushAction);
			Make(TEXT("DebugReload"), EInputActionValueType::Boolean, Character.DebugReloadAction);
			Input.Reset(NewObject<UEnhancedInputComponent>(&Character));
			Character.SetupPlayerInputComponent(Input.Get());
		}

		// Executes every binding of the action for this trigger event (with a zero value). Returns how many ran.
		int32 Fire(FName Name, ETriggerEvent Event)
		{
			const TStrongObjectPtr<UInputAction>* Found = Actions.Find(Name);
			const UInputAction* Action = Found ? Found->Get() : nullptr;
			int32 Ran = 0;
			for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
			{
				if (Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event)
				{
					Binding->Execute(FInputActionInstance(Action));
					++Ran;
				}
			}
			return Ran;
		}

		int32 Press(FName Name) { return Fire(Name, ETriggerEvent::Started); }
	};
}

using namespace CSPhysicsBodyTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyNormalFollowsPoseTest, "CombatSimulator.CS.Body.NormalFollowsPose", CSPhysicsBodyTests::TestFlags)
bool FCSBodyNormalFollowsPoseTest::RunTest(const FString& Parameters)
{
	// Spec 5.4 Normal: the upper body is simulated and follows its pose strongly; pelvis and legs stay animated.
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	USkeletalMeshComponent* Mesh = Sim.Mesh();
	TestEqual(TEXT("State is Normal"), Sim.State(), ECSBodyState::Normal);
	AddInfo(FString::Printf(TEXT("Body mass %.1f kg"), Sim.Mass()));
	TestTrue(TEXT("Body mass is a person's (40 to 150 kg)"), Sim.Mass() > 40.f && Sim.Mass() < 150.f);
	for (const TCHAR* Bone : {TEXT("spine_03"), TEXT("spine_05"), TEXT("head"), TEXT("upperarm_l"), TEXT("lowerarm_r"), TEXT("hand_l")})
	{
		TestTrue(FString::Printf(TEXT("%s is simulated in Normal"), Bone), Mesh->IsSimulatingPhysics(Bone));
	}
	for (const TCHAR* Bone : {TEXT("pelvis"), TEXT("thigh_l"), TEXT("calf_r"), TEXT("foot_l")})
	{
		TestFalse(FString::Printf(TEXT("%s stays animated in Normal"), Bone), Mesh->IsSimulatingPhysics(Bone));
	}

	// Gap between the simulated pose and the animated pose: read the blended pose, then the same
	// frame with the physics weight at 0 (animation only).
	auto Gaps = [&](const TArray<FName>& Bones)
	{
		TArray<FVector> Sim1;
		for (FName B : Bones) { Sim1.Add(Mesh->GetBoneLocation(B, EBoneSpaces::ComponentSpace)); }
		Mesh->SetAllBodiesPhysicsBlendWeight(0.f);
		Mesh->RefreshBoneTransforms();
		TArray<float> Out;
		for (int32 i = 0; i < Bones.Num(); ++i) { Out.Add(FVector::Dist(Sim1[i], Mesh->GetBoneLocation(Bones[i], EBoneSpaces::ComponentSpace))); }
		Mesh->SetAllBodiesBelowPhysicsBlendWeight(TEXT("spine_01"), 1.f);
		return Out;
	};
	const TArray<FName> Bones = {TEXT("spine_05"), TEXT("head"), TEXT("hand_l"), TEXT("hand_r")};
	Sim.Run(1.f);
	const TArray<float> Idle = Gaps(Bones);
	AddInfo(FString::Printf(TEXT("Idle: gap to the animated pose spine_05 %.2f, head %.2f, hand_l %.2f, hand_r %.2f cm"), Idle[0], Idle[1], Idle[2], Idle[3]));
	TestTrue(TEXT("Idle: the chest follows the pose within 2 cm"), Idle[0] < 2.f);
	TestTrue(TEXT("Idle: the hands follow the pose within 5 cm"), Idle[2] < 5.f && Idle[3] < 5.f);

	// Jog then stop: arms and head sway more than the chest, but nothing comes loose.
	Sim.Run(1.5f, FVector::ForwardVector);
	float MaxChest = 0.f, MaxHand = 0.f;
	for (int32 i = 0; i < 20; ++i)
	{
		Sim.Step();
		const TArray<float> G = Gaps(Bones);
		MaxChest = FMath::Max(MaxChest, G[0]);
		MaxHand = FMath::Max3(MaxHand, G[2], G[3]);
	}
	AddInfo(FString::Printf(TEXT("Stopping from a jog: largest gap chest %.2f cm, hands %.2f cm"), MaxChest, MaxHand));
	TestTrue(TEXT("Stopping: the hands sway more than the chest (lighter arms)"), MaxHand > MaxChest);
	TestTrue(TEXT("Stopping: the hands stay within 25 cm of the pose"), MaxHand < 25.f);
	TestTrue(TEXT("Stopping: the chest stays within 5 cm of the pose"), MaxChest < 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodySmallImpulseStaggersTest, "CombatSimulator.CS.Body.SmallImpulseStaggers", CSPhysicsBodyTests::TestFlags)
bool FCSBodySmallImpulseStaggersTest::RunTest(const FString& Parameters)
{
	// Below the threshold: state Hit, then Normal; the capsule never leaves the ground.
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	const UCSMovementTuning& T = *Sim.Tuning;
	USkeletalMeshComponent* Mesh = Sim.Mesh();

	for (const TCHAR* Bone : {TEXT("spine_03"), TEXT("hand_l")})
	{
		const FString Case = FString::Printf(TEXT("Hit at %s"), Bone);
		const FVector Before = Mesh->GetBoneLocation(TEXT("spine_05"), EBoneSpaces::ComponentSpace);
		const FVector HandBefore = Mesh->GetBoneLocation(TEXT("hand_l"), EBoneSpaces::ComponentSpace);
		Sim.PhysicsBody->ApplyImpulseAtBone(Bone, Sim.ImpulseFor(0.6f * T.RagdollImpulseThreshold, FVector(1.f, 0.3f, 0.f)));
		TestEqual(Case + TEXT(": state is Hit"), Sim.State(), ECSBodyState::Hit);
		TestFalse(Case + TEXT(": not ragdolled"), Sim.Character->IsRagdolled());
		const FName Chain = Sim.PhysicsBody->GetChainRoot(Bone);
		AddInfo(FString::Printf(TEXT("%s: chain %s, hit weight of spine_05 %.2f, of hand_l %.2f"), *Case, *Chain.ToString(),
			Sim.PhysicsBody->GetHitWeight(TEXT("spine_05")), Sim.PhysicsBody->GetHitWeight(TEXT("hand_l"))));
		TestTrue(Case + TEXT(": the hit bone's chain is weakened"), Sim.PhysicsBody->GetHitWeight(Bone) > 0.99f);
		if (FName(Bone) == TEXT("hand_l"))
		{
			TestEqual(Case + TEXT(": the chain is the left arm"), Chain, FName(TEXT("upperarm_l")));
			TestTrue(Case + TEXT(": the chest is not weakened by a hit on the hand"), Sim.PhysicsBody->GetHitWeight(TEXT("spine_05")) < 0.01f);
		}

		float MaxChest = 0.f, MaxHand = 0.f, NormalAt = -1.f;
		bool bAlwaysGrounded = true, bEverRagdolled = false;
		const float Start = Sim.Time;
		for (int32 i = 0; i < FMath::CeilToInt(2.5f / Sim.Dt); ++i)
		{
			Sim.Step();
			bAlwaysGrounded &= Sim.Character->GetCharacterMovement()->IsMovingOnGround();
			bEverRagdolled |= Sim.Character->IsRagdolled();
			MaxChest = FMath::Max(MaxChest, FVector::Dist(Before, Mesh->GetBoneLocation(TEXT("spine_05"), EBoneSpaces::ComponentSpace)));
			MaxHand = FMath::Max(MaxHand, FVector::Dist(HandBefore, Mesh->GetBoneLocation(TEXT("hand_l"), EBoneSpaces::ComponentSpace)));
			if (NormalAt < 0.f && Sim.State() == ECSBodyState::Normal)
			{
				NormalAt = Sim.Time - Start;
			}
		}
		AddInfo(FString::Printf(TEXT("%s: back to Normal after %.2f s; largest move chest %.1f cm, left hand %.1f cm"), *Case, NormalAt, MaxChest, MaxHand));
		TestTrue(Case + TEXT(": back to Normal after the hold and the ramp"), NormalAt > 0.f && NormalAt <= T.HitHoldSeconds + T.HitRecoverSeconds + 0.1f);
		TestTrue(Case + TEXT(": the capsule never left the ground"), bAlwaysGrounded);
		TestFalse(Case + TEXT(": never ragdolled"), bEverRagdolled);
		TestTrue(Case + TEXT(": the hit visibly moved the body (5 cm or more)"), (FName(Bone) == TEXT("hand_l") ? MaxHand : MaxChest) >= 5.f);
		TestTrue(Case + TEXT(": the hit weights are back to 0"), Sim.PhysicsBody->GetHitWeight(Bone) < 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyLargeImpulseRagdollsAndRecoversTest, "CombatSimulator.CS.Body.LargeImpulseRagdollsAndRecovers", CSPhysicsBodyTests::TestFlags)
bool FCSBodyLargeImpulseRagdollsAndRecoversTest::RunTest(const FString& Parameters)
{
	// Above the threshold: Ragdoll, then Recovering, then Normal within the settle time plus
	// RagdollRecoverSeconds plus 1 s. SetRagdolled(true) on entry, false only when control returns.
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	const UCSMovementTuning& T = *Sim.Tuning;
	int32 Recovered = 0;
	TArray<ECSBodyState> Seen;
	Sim.PhysicsBody->OnRecovered.AddLambda([&]() { ++Recovered; });
	Sim.PhysicsBody->OnStateChanged.AddLambda([&](ECSBodyState, ECSBodyState New) { Seen.Add(New); });

	const FVector StartLocation = Sim.Character->GetActorLocation();
	Sim.PhysicsBody->ApplyImpulseAtBone(TEXT("spine_03"), Sim.ImpulseFor(1.5f * T.RagdollImpulseThreshold, FVector(1.f, 0.f, 0.2f)));
	TestEqual(TEXT("State is Ragdoll at once"), Sim.State(), ECSBodyState::Ragdoll);
	TestTrue(TEXT("The character is ragdolled (camera contract)"), Sim.Character->IsRagdolled());
	TestTrue(TEXT("Every body simulates, the pelvis too"), Sim.Mesh()->IsSimulatingPhysics(TEXT("pelvis")) && Sim.Mesh()->IsSimulatingPhysics(TEXT("foot_r")));
	TestEqual(TEXT("Movement stops"), Sim.Character->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	TestEqual(TEXT("The capsule stops colliding"), Sim.Capsule()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	// Independent settle clock: the first time the pelvis has been slower than the settle speed for
	// the settle time.
	float SlowFor = 0.f, SettledAt = -1.f, RecoveringAt = -1.f, NormalAt = -1.f, MaxFollowGap = 0.f;
	FVector LastPelvis = Sim.Mesh()->GetBoneLocation(TEXT("pelvis"));
	bool bRagdolledThroughRecovery = true;
	const float Start = Sim.Time;
	const float Limit = T.RagdollMaxSeconds + T.RagdollRecoverSeconds + 2.f;
	for (int32 i = 0; i < FMath::CeilToInt(Limit / Sim.Dt) && NormalAt < 0.f; ++i)
	{
		Sim.Step();
		const float Now = Sim.Time - Start;
		if (Sim.State() == ECSBodyState::Ragdoll)
		{
			SlowFor = Sim.PelvisSpeed() < T.RagdollSettleSpeed ? SlowFor + Sim.Dt : 0.f;
			if (SettledAt < 0.f && SlowFor >= T.RagdollSettleSeconds)
			{
				SettledAt = Now;
			}
			// The capsule follows the pelvis sideways: before the physics step it moves to where the
			// pelvis ended the last step.
			MaxFollowGap = FMath::Max(MaxFollowGap, FVector::Dist2D(LastPelvis, Sim.Character->GetActorLocation()));
		}
		LastPelvis = Sim.Mesh()->GetBoneLocation(TEXT("pelvis"));
		if (RecoveringAt < 0.f && Sim.State() == ECSBodyState::Recovering)
		{
			RecoveringAt = Now;
		}
		if (Sim.State() == ECSBodyState::Recovering)
		{
			bRagdolledThroughRecovery &= Sim.Character->IsRagdolled();
		}
		if (NormalAt < 0.f && Sim.State() == ECSBodyState::Normal)
		{
			NormalAt = Now;
		}
	}
	AddInfo(FString::Printf(TEXT("Settled (pelvis below %.0f cm/s for %.2f s) at %.2f s, Recovering at %.2f s, Normal at %.2f s; moved %.0f cm; largest capsule-pelvis gap %.1f cm"),
		T.RagdollSettleSpeed, T.RagdollSettleSeconds, SettledAt, RecoveringAt, NormalAt,
		FVector::Dist2D(StartLocation, Sim.Character->GetActorLocation()), MaxFollowGap));
	TestTrue(TEXT("The body settled"), SettledAt > 0.f);
	TestTrue(TEXT("Recovering started"), RecoveringAt > 0.f);
	TestTrue(TEXT("Normal again"), NormalAt > 0.f);
	TestTrue(TEXT("Normal within the settle time + RagdollRecoverSeconds + 1 s"), NormalAt > 0.f && NormalAt <= SettledAt + T.RagdollRecoverSeconds + 1.f);
	TestTrue(TEXT("The recovery blend takes about RagdollRecoverSeconds"), FMath::Abs((NormalAt - RecoveringAt) - T.RagdollRecoverSeconds) <= 2.f * Sim.Dt);
	TestTrue(TEXT("The capsule followed the pelvis (within 5 cm sideways)"), MaxFollowGap < 5.f);
	TestTrue(TEXT("Still ragdolled (third person, no input) while Recovering"), bRagdolledThroughRecovery);
	TestFalse(TEXT("Not ragdolled once control returns"), Sim.Character->IsRagdolled());
	TestEqual(TEXT("OnRecovered fired once"), Recovered, 1);
	TestTrue(TEXT("States in order Ragdoll, Recovering, Normal"), Seen == TArray<ECSBodyState>{ECSBodyState::Ragdoll, ECSBodyState::Recovering, ECSBodyState::Normal});
	TestTrue(TEXT("The pelvis is animated again"), !Sim.Mesh()->IsSimulatingPhysics(TEXT("pelvis")) && Sim.Mesh()->IsSimulatingPhysics(TEXT("spine_03")));
	TestEqual(TEXT("The capsule collides again"), Sim.Capsule()->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
	CheckStanding(*this, Sim, TEXT("After recovery"));

	// Control is back: the character jogs.
	Sim.Run(1.f, FVector::ForwardVector);
	TestTrue(TEXT("Moves again after recovery"), Sim.Character->GetCharacterMovement()->Velocity.Size2D() > 300.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyHighLandingRagdollsTest, "CombatSimulator.CS.Body.HighLandingRagdolls", CSPhysicsBodyTests::TestFlags)
bool FCSBodyHighLandingRagdollsTest::RunTest(const FString& Parameters)
{
	// A landing faster than RagdollLandingSpeedThreshold ragdolls; a 2 m drop does not.
	struct FCase { float Height; bool bRagdoll; };
	for (const FCase& Case : {FCase{1100.f, true}, FCase{200.f, false}})
	{
		FBodySim Sim;
		if (!Sim.Init(*this, 0.f, FVector(0.f, 0.f, 90.f + Case.Height), false))
		{
			return false;
		}
		bool bLanded = false, bRagdolled = false;
		Sim.RunUntil(4.f, [&]() { bLanded |= Sim.Character->GetLastLandingSpeed() > 0.f; bRagdolled |= Sim.State() == ECSBodyState::Ragdoll; return bLanded; });
		Sim.Run(0.1f);
		bRagdolled |= Sim.State() == ECSBodyState::Ragdoll;
		AddInfo(FString::Printf(TEXT("Drop of %.0f cm: landing speed %.0f cm/s (threshold %.0f), state %d"), Case.Height,
			Sim.Character->GetLastLandingSpeed(), Sim.Tuning->RagdollLandingSpeedThreshold, int32(Sim.State())));
		TestTrue(FString::Printf(TEXT("Drop of %.0f cm landed"), Case.Height), bLanded);
		TestEqual(FString::Printf(TEXT("Drop of %.0f cm ragdolls"), Case.Height), bRagdolled, Case.bRagdoll);
		if (Case.bRagdoll)
		{
			TestTrue(TEXT("The landing was above the threshold"), Sim.Character->GetLastLandingSpeed() > Sim.Tuning->RagdollLandingSpeedThreshold);
			const bool bBack = Sim.RunUntil(Sim.Tuning->RagdollMaxSeconds + 2.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
			TestTrue(TEXT("Recovers after a high landing"), bBack);
			CheckStanding(*this, Sim, TEXT("After a high landing"));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyRecoveryOnSlopeOrUpsideDownIsUprightTest, "CombatSimulator.CS.Body.RecoveryOnSlopeOrUpsideDownIsUpright", CSPhysicsBodyTests::TestFlags)
bool FCSBodyRecoveryOnSlopeOrUpsideDownIsUprightTest::RunTest(const FString& Parameters)
{
	// Review Focus 1: a ragdoll that ends on a slope or upside down stands up upright on walkable ground.
	{
		FBodySim Sim;
		if (!Sim.Init(*this, 25.f))
		{
			return false;
		}
		Sim.PhysicsBody->EnterRagdoll();
		const bool bBack = Sim.RunUntil(Sim.Tuning->RagdollMaxSeconds + 2.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
		TestTrue(TEXT("25 degree slope: recovered"), bBack);
		CheckStanding(*this, Sim, TEXT("25 degree slope"));
		Sim.Run(0.5f);
		CheckStanding(*this, Sim, TEXT("25 degree slope, 0.5 s later"));
	}
	{
		FBodySim Sim;
		if (!Sim.Init(*this))
		{
			return false;
		}
		Sim.PhysicsBody->EnterRagdoll();
		// Head down: turn the whole ragdoll over (180 degrees about the facing axis) and lift it clear of the floor.
		const FVector Pelvis = Sim.Mesh()->GetBoneLocation(TEXT("pelvis"));
		Sim.MoveBodies(FQuat(FVector::ForwardVector, PI), Pelvis, FVector(0.f, 0.f, 120.f));
		Sim.Step();
		AddInfo(FString::Printf(TEXT("Upside down: head at %.0f cm, pelvis at %.0f cm"), Sim.Mesh()->GetBoneLocation(TEXT("head")).Z, Sim.Mesh()->GetBoneLocation(TEXT("pelvis")).Z));
		TestTrue(TEXT("Upside down: the head is below the pelvis"), Sim.Mesh()->GetBoneLocation(TEXT("head")).Z < Sim.Mesh()->GetBoneLocation(TEXT("pelvis")).Z);
		// Recover while still upside down (R before it settles) and after it lands.
		Sim.PhysicsBody->Recover();
		TestEqual(TEXT("Upside down: R starts the recovery"), Sim.State(), ECSBodyState::Recovering);
		const bool bBack = Sim.RunUntil(Sim.Tuning->RagdollRecoverSeconds + 1.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
		TestTrue(TEXT("Upside down: recovered"), bBack);
		CheckStanding(*this, Sim, TEXT("Upside down in the air"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyRecoveryNeverBelowFloorTest, "CombatSimulator.CS.Body.RecoveryNeverBelowFloor", CSPhysicsBodyTests::TestFlags)
bool FCSBodyRecoveryNeverBelowFloorTest::RunTest(const FString& Parameters)
{
	// Review Focus 1: a ragdoll partly inside geometry recovers above the floor, never under it or inside a block.
	{
		FBodySim Sim;
		if (!Sim.Init(*this))
		{
			return false;
		}
		Sim.PhysicsBody->EnterRagdoll();
		Sim.Run(2.f);
		// Push the settled body half into the floor: the pelvis ends below the floor's surface.
		Sim.MoveBodies(FQuat::Identity, FVector::ZeroVector, FVector(0.f, 0.f, -Sim.Mesh()->GetBoneLocation(TEXT("pelvis")).Z - 10.f));
		AddInfo(FString::Printf(TEXT("Sunk: pelvis body at z %.1f"), Sim.Mesh()->GetBodyInstance(TEXT("pelvis"))->GetUnrealWorldTransform().GetLocation().Z));
		Sim.PhysicsBody->Recover();
		const bool bBack = Sim.RunUntil(Sim.Tuning->RagdollRecoverSeconds + 1.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
		TestTrue(TEXT("Sunk into the floor: recovered"), bBack);
		CheckStanding(*this, Sim, TEXT("Sunk into the floor"));
		TestTrue(TEXT("Sunk into the floor: capsule bottom at or above the floor"), Sim.Character->GetActorLocation().Z - Sim.Capsule()->GetScaledCapsuleHalfHeight() >= -0.5f);
	}
	{
		FBodySim Sim;
		if (!Sim.Init(*this))
		{
			return false;
		}
		// A 60 cm block just ahead; the ragdoll is thrown (over it) and then pushed into it, pelvis 10 cm inside.
		Sim.AddBox(FVector(120.f, 0.f, 30.f), FVector(50.f, 200.f, 30.f));
		Sim.PhysicsBody->ApplyImpulseAtBone(TEXT("spine_05"), Sim.ImpulseFor(2.f * Sim.Tuning->RagdollImpulseThreshold, FVector::ForwardVector));
		Sim.Run(1.5f);
		Sim.MoveBodies(FQuat::Identity, FVector::ZeroVector, FVector(70.f + 10.f - Sim.Mesh()->GetBoneLocation(TEXT("pelvis")).X, 0.f, 0.f));
		AddInfo(FString::Printf(TEXT("Into the block: pelvis body at %s"), *Sim.Mesh()->GetBodyInstance(TEXT("pelvis"))->GetUnrealWorldTransform().GetLocation().ToString()));
		Sim.PhysicsBody->Recover();
		const bool bBack = Sim.RunUntil(Sim.Tuning->RagdollRecoverSeconds + 1.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
		TestTrue(TEXT("Inside a block: recovered"), bBack);
		CheckStanding(*this, Sim, TEXT("Inside a block"));
		Sim.Run(0.5f);
		CheckStanding(*this, Sim, TEXT("Inside a block, 0.5 s later"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyInputIgnoredWhileRagdolledTest, "CombatSimulator.CS.Body.InputIgnoredWhileRagdolled", CSPhysicsBodyTests::TestFlags)
bool FCSBodyInputIgnoredWhileRagdolledTest::RunTest(const FString& Parameters)
{
	// Move, jump and crouch do nothing from the ragdoll until control returns; held gait keys apply then.
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	FInputRig Rig;
	Rig.Bind(*Sim.Character);
	UCharacterMovementComponent* Move = Sim.Character->GetCharacterMovement();

	TestTrue(TEXT("R ragdolls (dev tools on)"), Rig.Press(TEXT("DebugRagdoll")) == 1 && Sim.State() == ECSBodyState::Ragdoll);
	bool bMoved = false, bCrouched = false;
	while (Sim.State() == ECSBodyState::Ragdoll || Sim.State() == ECSBodyState::Recovering)
	{
		Rig.Fire(TEXT("Move"), ETriggerEvent::Triggered);
		Sim.Character->AddMovementInput(FVector::ForwardVector, 1.f);
		Rig.Press(TEXT("Jump"));
		Rig.Press(TEXT("Crouch"));
		Rig.Press(TEXT("Crouch"));
		Rig.Press(TEXT("Crouch"));
		Sim.Step();
		bMoved |= Move->MovementMode != MOVE_None && Move->Velocity.Size2D() > 1.f;
		bCrouched |= Sim.Character->bIsCrouched || Move->bWantsToCrouch;
		if (Sim.Time > 30.f)
		{
			AddError(TEXT("Never recovered"));
			return false;
		}
	}
	TestFalse(TEXT("No movement from input while ragdolled or recovering"), bMoved);
	TestFalse(TEXT("Crouch presses while ragdolled do nothing"), bCrouched);
	Rig.Fire(TEXT("Jump"), ETriggerEvent::Completed);
	Sim.Run(0.3f);
	TestTrue(TEXT("No jump queued from the ragdoll: still on the ground after recovery"), Move->IsMovingOnGround() && FMath::Abs(Move->Velocity.Z) < 1.f);
	TestEqual(TEXT("Gait is still Jog"), Sim.Character->GetGait(), ECSGait::Jog);

	// Held sprint during the ragdoll applies when control returns.
	Rig.Press(TEXT("DebugRagdoll"));
	Rig.Press(TEXT("Sprint"));
	TestEqual(TEXT("Sprint pressed while ragdolled does not change gait yet"), Sim.Character->GetGait(), ECSGait::Jog);
	Rig.Press(TEXT("DebugRagdoll"));
	TestEqual(TEXT("R from the ragdoll starts the recovery"), Sim.State(), ECSBodyState::Recovering);
	Sim.RunUntil(2.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
	TestEqual(TEXT("Held sprint applies once control returns"), Sim.Character->GetGait(), ECSGait::Sprint);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyPushKeyStaggersTest, "CombatSimulator.CS.Body.PushKeyStaggers", CSPhysicsBodyTests::TestFlags)
bool FCSBodyPushKeyStaggersTest::RunTest(const FString& Parameters)
{
	// P pushes along the view at DevPushSpeed: a stagger by default, a ragdoll when set above the threshold.
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	FInputRig Rig;
	Rig.Bind(*Sim.Character);
	Rig.Press(TEXT("DebugPush"));
	TestEqual(TEXT("P at the default DevPushSpeed staggers"), Sim.State(), ECSBodyState::Hit);
	Sim.Run(1.f);
	Sim.Tuning->DevPushSpeed = 2.f * Sim.Tuning->RagdollImpulseThreshold;
	FString Message;
	TestTrue(TEXT("Reload with a ragdoll-strength push"), UCSDevTools::ReloadTuning(*Sim.Character, Message));
	Rig.Press(TEXT("DebugPush"));
	TestEqual(TEXT("P above the threshold ragdolls"), Sim.State(), ECSBodyState::Ragdoll);

	Sim.Tuning->bDevTools = false;
	TestTrue(TEXT("Reload with dev tools off"), UCSDevTools::ReloadTuning(*Sim.Character, Message));
	Sim.RunUntil(Sim.Tuning->RagdollMaxSeconds + 2.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
	Rig.Press(TEXT("DebugPush"));
	Rig.Press(TEXT("DebugRagdoll"));
	TestEqual(TEXT("With dev tools off, P and R do nothing"), Sim.State(), ECSBodyState::Normal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyJointLimitsHumanRangeTest, "CombatSimulator.CS.Body.JointLimitsHumanRange", CSPhysicsBodyTests::TestFlags)
bool FCSBodyJointLimitsHumanRangeTest::RunTest(const FString& Parameters)
{
	// PHYS_BaseCharacter's cones, checked against human ranges by turning a bone from its rest pose
	// and testing the result against the constraint's swing and twist limits.
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	USkeletalMeshComponent* Mesh = Sim.Mesh();
	const UPhysicsAsset* PA = Mesh->GetPhysicsAsset();
	const FReferenceSkeleton& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	auto RefCS = [&](int32 Bone)
	{
		FTransform T = FTransform::Identity;
		for (int32 B = Bone; B != INDEX_NONE; B = Ref.GetParentIndex(B))
		{
			T = T * Ref.GetRefBonePose()[B];
		}
		return T;
	};
	// Mesh space: +Y forward, +Z up, the character's left at +X.
	auto Turn = [](const FVector& Axis, float Degrees) { return FQuat(Axis.GetSafeNormal(), FMath::DegreesToRadians(Degrees)); };
	auto AllowedTurn = [&](FName Bone, const FQuat& Turned) -> bool
	{
		const TObjectPtr<UPhysicsConstraintTemplate>* Found = PA->ConstraintSetup.FindByPredicate([&](const TObjectPtr<UPhysicsConstraintTemplate>& C) { return C->DefaultInstance.ConstraintBone1 == Bone; });
		if (!Found)
		{
			AddError(FString::Printf(TEXT("No constraint on %s"), *Bone.ToString()));
			return false;
		}
		const FConstraintInstance& CI = (*Found)->DefaultInstance;
		const FTransform Child = RefCS(Ref.FindBoneIndex(CI.ConstraintBone1));
		const FTransform Parent = RefCS(Ref.FindBoneIndex(CI.ConstraintBone2));
		// The child turned about the joint by Degrees about Axis (mesh space).
		FTransform ChildTurned = Child;
		ChildTurned.SetRotation(Turned * Child.GetRotation());
		const FQuat F1 = (CI.GetRefFrame(EConstraintFrame::Frame1) * ChildTurned).GetRotation();
		const FQuat F2 = (CI.GetRefFrame(EConstraintFrame::Frame2) * Parent).GetRotation();
		const FQuat Rel = F2.Inverse() * F1;
		FQuat Swing, Twist;
		Rel.ToSwingTwist(FVector::XAxisVector, Swing, Twist);
		FVector SwingAxis;
		float SwingAngle;
		Swing.ToAxisAndAngle(SwingAxis, SwingAngle);
		const float S1 = FMath::RadiansToDegrees(SwingAngle * SwingAxis.Z);
		const float S2 = FMath::RadiansToDegrees(SwingAngle * SwingAxis.Y);
		const float L1 = FMath::Max(CI.GetAngularSwing1Limit(), 0.01f), L2 = FMath::Max(CI.GetAngularSwing2Limit(), 0.01f);
		return FMath::Square(S1 / L1) + FMath::Square(S2 / L2) <= 1.f;
	};
	auto Allowed = [&](FName Bone, const FVector& Axis, float Degrees) { return AllowedTurn(Bone, Turn(Axis, Degrees)); };
	const FVector Left(1.f, 0.f, 0.f), Fwd(0.f, 1.f, 0.f), Up(0.f, 0.f, 1.f);

	// Rotation about +X (left) by a positive angle turns +Y toward +Z, so a hanging thigh (-Z) swings
	// forward (+Y): flexion. About +Y (forward), positive turns +Z toward +X. About +Z, +X toward +Y.
	for (const TCHAR* Side : {TEXT("l"), TEXT("r")})
	{
		const FName Thigh(*FString::Printf(TEXT("thigh_%s"), Side));
		const FName Upper(*FString::Printf(TEXT("upperarm_%s"), Side));
		const float Out = FString(Side) == TEXT("l") ? 1.f : -1.f;
		// Hip: flexion to 100 deg, extension to 10 deg allowed; 60 deg of extension and 60 deg of adduction are not.
		TestTrue(FString::Printf(TEXT("%s: 100 deg of hip flexion is allowed"), *Thigh.ToString()), Allowed(Thigh, Left, 100.f));
		TestTrue(FString::Printf(TEXT("%s: 10 deg of hip extension is allowed"), *Thigh.ToString()), Allowed(Thigh, Left, -10.f));
		TestFalse(FString::Printf(TEXT("%s: 60 deg of hip extension is not"), *Thigh.ToString()), Allowed(Thigh, Left, -60.f));
		TestTrue(FString::Printf(TEXT("%s: 35 deg of abduction is allowed"), *Thigh.ToString()), Allowed(Thigh, Fwd, -35.f * Out));
		TestFalse(FString::Printf(TEXT("%s: 60 deg of adduction (across the other leg) is not"), *Thigh.ToString()), Allowed(Thigh, Fwd, 60.f * Out));
		// Shoulder: the arm (55 deg below horizontal, out to the side) can go overhead and forward, not far behind.
		TestTrue(FString::Printf(TEXT("%s: raised overhead (sideways, 140 deg up) is allowed"), *Upper.ToString()), Allowed(Upper, Fwd, -140.f * Out));
		TestTrue(FString::Printf(TEXT("%s: hanging straight down is allowed"), *Upper.ToString()), Allowed(Upper, Fwd, 30.f * Out));
		TestTrue(FString::Printf(TEXT("%s: swung 80 deg forward is allowed"), *Upper.ToString()), Allowed(Upper, Up, 80.f * Out));
		TestTrue(FString::Printf(TEXT("%s: hanging and swung 40 deg back (a running arm) is allowed"), *Upper.ToString()), AllowedTurn(Upper, Turn(Left, -40.f) * Turn(Fwd, 35.f * Out)));
		TestFalse(FString::Printf(TEXT("%s: reaching 140 deg behind (across the back) is not"), *Upper.ToString()), Allowed(Upper, Up, -140.f * Out));
		TestTrue(FString::Printf(TEXT("%s: horizontal and 30 deg across the chest is allowed"), *Upper.ToString()), AllowedTurn(Upper, Turn(Up, 120.f * Out) * Turn(Fwd, -55.f * Out)));
		TestFalse(FString::Printf(TEXT("%s: horizontal and straight back (90 deg behind the side) is not"), *Upper.ToString()), AllowedTurn(Upper, Turn(Up, -90.f * Out) * Turn(Fwd, -55.f * Out)));
	}
	// Spine and neck stay small.
	TestFalse(TEXT("spine_03: 60 deg of bend is not allowed"), Allowed(TEXT("spine_03"), Left, 60.f));
	TestTrue(TEXT("head: 35 deg nod forward is allowed"), Allowed(TEXT("head"), Left, -35.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyHingesBendTheRightWayTest, "CombatSimulator.CS.Body.HingesBendTheRightWay", CSPhysicsBodyTests::TestFlags)
bool FCSBodyHingesBendTheRightWayTest::RunTest(const FString& Parameters)
{
	// In a simulated ragdoll (dropped, thrown forward, backward and sideways) knees and elbows bend only
	// the anatomical way: hyperextension stays within the limit plus a little solver slack.
	struct FHinge { const TCHAR* Parent; const TCHAR* Child; const TCHAR* End; float FlexSideSign; float ExtendLimit; };
	// Knees flex toward the back (-forward), elbows toward the front (+forward).
	const FHinge Hinges[] = {
		{TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), -1.f, 2.f},
		{TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), -1.f, 2.f},
		{TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), 1.f, 5.f},
		{TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"), 1.f, 5.f},
	};
	const FVector Throws[] = {FVector::ZeroVector, FVector(800.f, 0.f, 200.f), FVector(-800.f, 0.f, 200.f), FVector(0.f, 800.f, 300.f), FVector(300.f, -300.f, 900.f)};
	float WorstExtend[4] = {-180.f, -180.f, -180.f, -180.f};
	float MostFlex[4] = {-180.f, -180.f, -180.f, -180.f};
	for (const FVector& Throw : Throws)
	{
		FBodySim Sim;
		if (!Sim.Init(*this))
		{
			return false;
		}
		USkeletalMeshComponent* Mesh = Sim.Mesh();
		// Each parent bone's anterior direction in its own space, from the reference pose (mesh +Y is forward).
		const FReferenceSkeleton& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		Sim.PhysicsBody->EnterRagdoll();
		if (!Throw.IsZero())
		{
			Mesh->SetAllPhysicsLinearVelocity(Throw, true);
			Mesh->SetPhysicsAngularVelocityInDegrees(FVector(Throw.Y, -Throw.X, 0.f) * 0.5f, true, TEXT("pelvis"));
		}
		for (int32 i = 0; i < 180; ++i)
		{
			Sim.Step();
			for (int32 h = 0; h < 4; ++h)
			{
				const FHinge& H = Hinges[h];
				const FTransform P = Mesh->GetSocketTransform(H.Parent);
				const FVector PDir = (Mesh->GetBoneLocation(H.Child) - P.GetLocation()).GetSafeNormal();
				const FVector CDir = (Mesh->GetBoneLocation(H.End) - Mesh->GetBoneLocation(H.Child)).GetSafeNormal();
				// The parent's forward in its bone space, carried along with it.
				FTransform RestCS = FTransform::Identity;
				for (int32 B = Ref.FindBoneIndex(H.Parent); B != INDEX_NONE; B = Ref.GetParentIndex(B)) { RestCS = RestCS * Ref.GetRefBonePose()[B]; }
				const FVector LocalFwd = RestCS.GetRotation().UnrotateVector(FVector(0.f, 1.f, 0.f));
				const FVector Fwd = P.GetRotation().RotateVector(LocalFwd);
				const float Bend = HingeBendDeg(PDir, CDir, Fwd * H.FlexSideSign);
				WorstExtend[h] = FMath::Max(WorstExtend[h], -Bend);
				MostFlex[h] = FMath::Max(MostFlex[h], Bend);
			}
		}
	}
	for (int32 h = 0; h < 4; ++h)
	{
		AddInfo(FString::Printf(TEXT("%s: most flexion %.1f deg, most hyperextension %.1f deg (limit %.0f)"), Hinges[h].Child, MostFlex[h], WorstExtend[h], Hinges[h].ExtendLimit));
		TestTrue(FString::Printf(TEXT("%s never bends backward past its limit (+8 deg slack)"), Hinges[h].Child), WorstExtend[h] <= Hinges[h].ExtendLimit + 8.f);
		TestTrue(FString::Printf(TEXT("%s does bend the anatomical way in some fall (20 deg or more)"), Hinges[h].Child), MostFlex[h] >= 20.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyFirstPersonCopyFollowsPhysicsTest, "CombatSimulator.CS.Body.FirstPersonCopyFollowsPhysics", CSPhysicsBodyTests::TestFlags)
bool FCSBodyFirstPersonCopyFollowsPhysicsTest::RunTest(const FString& Parameters)
{
	// Task 6 follow-up: the owner-only first-person body copies the body's pose, physics included;
	// no bone gap with physical animation active, and nothing breaks while ragdolled (third person then).
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	Sim.Possess();
	UCSCameraRig* Rig = Sim.Character->GetCameraRig();
	Rig->SetView(ECSView::FirstPerson);
	Sim.Run(0.5f);
	USkeletalMeshComponent* Mesh = Sim.Mesh();
	UPoseableMeshComponent* Copy = Rig->GetFirstPersonBody();
	const TArray<FName> Bones = {TEXT("spine_05"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("hand_r"), TEXT("thigh_r"), TEXT("foot_l")};
	auto MaxGap = [&]()
	{
		float Gap = 0.f;
		for (FName B : Bones) { Gap = FMath::Max(Gap, FVector::Dist(Mesh->GetBoneLocation(B), Copy->GetBoneLocation(B))); }
		return Gap;
	};
	float Worst = 0.f;
	Sim.PhysicsBody->ApplyImpulseAtBone(TEXT("hand_l"), Sim.ImpulseFor(0.8f * Sim.Tuning->RagdollImpulseThreshold, FVector::RightVector));
	for (int32 i = 0; i < 60; ++i)
	{
		Sim.Step(FVector::ForwardVector);
		TestTrue(TEXT("First person: the copy is shown"), Copy->IsVisible());
		Worst = FMath::Max(Worst, MaxGap());
	}
	AddInfo(FString::Printf(TEXT("First person, jogging with a hit arm: largest bone gap between the body and its first-person copy %.3f cm"), Worst));
	TestTrue(TEXT("The first-person copy matches the physics body within 0.5 cm"), Worst < 0.5f);

	// Entering the ragdoll forces third person; the camera blends out over CameraBlendSeconds, and
	// while it is still near the eyes the copy is shown, with the ragdoll's pose.
	Sim.PhysicsBody->EnterRagdoll();
	const float RagdollStart = Sim.Time;
	bool bCopyShownLate = false, bBodyHiddenLate = false;
	float WorstDuringBlend = 0.f;
	while (Sim.State() != ECSBodyState::Normal && Sim.Time < RagdollStart + 30.f)
	{
		Sim.Step();
		const bool bLate = Sim.Time - RagdollStart > Sim.Tuning->CameraBlendSeconds + Sim.Dt;
		if (Copy->IsVisible())
		{
			WorstDuringBlend = FMath::Max(WorstDuringBlend, MaxGap());
		}
		bCopyShownLate |= bLate && Copy->IsVisible();
		bBodyHiddenLate |= bLate && Mesh->bOwnerNoSee;
	}
	AddInfo(FString::Printf(TEXT("Ragdoll, while the camera blends out: largest bone gap %.3f cm"), WorstDuringBlend));
	TestTrue(TEXT("While the camera blends out the copy matches the ragdoll within 0.5 cm"), WorstDuringBlend < 0.5f);
	TestFalse(TEXT("Ragdoll and recovery, after the blend out: the first-person copy is not shown"), bCopyShownLate);
	TestFalse(TEXT("Ragdoll and recovery, after the blend out: the body is not hidden from the owner"), bBodyHiddenLate);
	Sim.Run(0.5f);
	TestEqual(TEXT("After recovery the chosen first-person view returns"), Rig->GetEffectiveView(), ECSView::FirstPerson);
	TestTrue(TEXT("After recovery the copy follows again"), MaxGap() < 0.5f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
