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
#include "CSBallLauncher.h"
#include "CSDevTools.h"
#include "CSHitBall.h"
#include "EngineUtils.h"
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
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Tests/AutomationCommon.h"
#include "Tests/CSInputRig.h"
#include "Tests/CSTestWorld.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace CSPhysicsBodyTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	const TCHAR* Regenerate = TEXT(" (missing or stale: run Game/Content/Python/import_base_character.py, see its docstring)");

	// A test world with a 200 m floor box (top at z = 0, optionally tilted, rising toward +X), extra
	// blocks, and one ACSCharacter (mesh, physics asset and animation from config) on it, ticked at 60 Hz.
	struct FBodySim : FCSTestWorld
	{
		UCSPhysicsBodyComponent* PhysicsBody = nullptr;
		TStrongObjectPtr<UCSMovementTuning> Tuning;

		bool Init(FAutomationTestBase& Test, float FloorPitchDeg = 0.f, const FVector& Spawn = FVector(0.f, 0.f, 120.f), bool bSettle = true)
		{
			Tuning.Reset(NewObject<UCSMovementTuning>(GetTransientPackage()));
			FCSTestWorldOptions Options;
			Options.FloorPitchDeg = FloorPitchDeg;
			Options.Spawn = Spawn;
			Options.bSettle = false;
			if (!FCSTestWorld::Init(Test, Tuning.Get(), Options))
			{
				return false;
			}
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

		// A 50 cm thick ramp rising toward +X at PitchDeg, its top surface starting at Foot and Length
		// long. Returns the top surface's normal.
		FVector AddRamp(const FVector& Foot, float PitchDeg, float Length)
		{
			const float P = FMath::DegreesToRadians(PitchDeg);
			const FVector Along(FMath::Cos(P), 0.f, FMath::Sin(P));
			const FVector Normal(-FMath::Sin(P), 0.f, FMath::Cos(P));
			AddBox(Foot + Along * (0.5f * Length) - Normal * 50.f, FVector(0.5f * Length, 500.f, 50.f), FRotator(PitchDeg, 0.f, 0.f));
			return Normal;
		}

		// Keeps the ragdoll down while a test arranges it (no automatic recovery for 6 s).
		void HoldRagdollDown()
		{
			Tuning->RagdollSettleSeconds = 6.f;
			Tuning->RagdollMaxSeconds = 8.f;
			FString Message;
			UCSDevTools::ReloadTuning(*Character, Message);
		}

		FVector PelvisBody() const { return Mesh()->GetBodyInstance(TEXT("pelvis"))->GetUnrealWorldTransform().GetLocation(); }

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

	// The character's real bindings, driven by key presses (Public/Tests/CSInputRig.h).
	using FInputRig = FCSInputRig;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyNormalFollowsPoseTest, "CombatSimulator.CS.Body.NormalFollowsPose", CSPhysicsBodyTests::TestFlags)
bool FCSBodyNormalFollowsPoseTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
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
	using namespace CSPhysicsBodyTests;
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
	using namespace CSPhysicsBodyTests;
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
	const float Limit = T.RagdollMaxSeconds + Sim.PhysicsBody->GetRecoverySeconds() + 2.f;
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
	TestTrue(TEXT("Normal within the settle time + the recovery time + 1 s"), NormalAt > 0.f && NormalAt <= SettledAt + Sim.PhysicsBody->GetRecoverySeconds() + 1.f);
	TestTrue(TEXT("The recovery takes about GetRecoverySeconds"), FMath::Abs((NormalAt - RecoveringAt) - Sim.PhysicsBody->GetRecoverySeconds()) <= 2.f * Sim.Dt);
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
	using namespace CSPhysicsBodyTests;
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
	using namespace CSPhysicsBodyTests;
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
		const bool bBack = Sim.RunUntil(Sim.PhysicsBody->GetRecoverySeconds() + 1.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
		TestTrue(TEXT("Upside down: recovered"), bBack);
		CheckStanding(*this, Sim, TEXT("Upside down in the air"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyRecoveryNeverBelowFloorTest, "CombatSimulator.CS.Body.RecoveryNeverBelowFloor", CSPhysicsBodyTests::TestFlags)
bool FCSBodyRecoveryNeverBelowFloorTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	// Review Focus 1: a ragdoll partly inside geometry recovers above the floor, never under it or inside a block.
	{
		FBodySim Sim;
		if (!Sim.Init(*this))
		{
			return false;
		}
		Sim.HoldRagdollDown();
		Sim.PhysicsBody->EnterRagdoll();
		Sim.Run(2.f);
		TestEqual(TEXT("Sunk into the floor: still in Ragdoll before the test moves it"), Sim.State(), ECSBodyState::Ragdoll);
		// Push the lying body half into the floor: the pelvis ends below the floor's surface.
		Sim.MoveBodies(FQuat::Identity, FVector::ZeroVector, FVector(0.f, 0.f, -Sim.Mesh()->GetBoneLocation(TEXT("pelvis")).Z - 10.f));
		AddInfo(FString::Printf(TEXT("Sunk: pelvis body at z %.1f"), Sim.Mesh()->GetBodyInstance(TEXT("pelvis"))->GetUnrealWorldTransform().GetLocation().Z));
		Sim.PhysicsBody->Recover();
		const bool bBack = Sim.RunUntil(Sim.PhysicsBody->GetRecoverySeconds() + 1.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
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
		Sim.HoldRagdollDown();
		Sim.PhysicsBody->ApplyImpulseAtBone(TEXT("spine_05"), Sim.ImpulseFor(2.f * Sim.Tuning->RagdollImpulseThreshold, FVector::ForwardVector));
		Sim.Run(1.5f);
		TestEqual(TEXT("Inside a block: still in Ragdoll before the test moves it"), Sim.State(), ECSBodyState::Ragdoll);
		Sim.MoveBodies(FQuat::Identity, FVector::ZeroVector, FVector(70.f + 10.f - Sim.Mesh()->GetBoneLocation(TEXT("pelvis")).X, 0.f, 0.f));
		AddInfo(FString::Printf(TEXT("Into the block: pelvis body at %s"), *Sim.Mesh()->GetBodyInstance(TEXT("pelvis"))->GetUnrealWorldTransform().GetLocation().ToString()));
		Sim.PhysicsBody->Recover();
		const bool bBack = Sim.RunUntil(Sim.PhysicsBody->GetRecoverySeconds() + 1.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
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
	using namespace CSPhysicsBodyTests;
	// Move, jump and crouch pressed while ragdolled or recovering reach the character's real bindings
	// with real values and do nothing: no movement input is queued, no jump is pressed, no crouch.
	// Jump stays held across the moment control returns and must not start a jump. Held gait keys
	// apply when control returns.
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	FInputRig Rig;
	Rig.Bind(*Sim.Character);
	ACSCharacter* C = Sim.Character;
	UCharacterMovementComponent* Move = C->GetCharacterMovement();
	const FInputActionValue Forward(FVector2D(0.0, 1.0));

	// The binding works: before the ragdoll, a forward Move queues movement input.
	TestEqual(TEXT("Move is bound"), Rig.Fire(TEXT("Move"), ETriggerEvent::Triggered, Forward), 1);
	TestFalse(TEXT("Before the ragdoll, Move queues movement input"), C->GetPendingMovementInputVector().IsNearlyZero());
	Sim.Step();

	TestTrue(TEXT("R ragdolls (dev tools on)"), Rig.Press(TEXT("DebugRagdoll")) == 1 && Sim.State() == ECSBodyState::Ragdoll);
	int32 Frames = 0, RecoveringFrames = 0, MoveQueued = 0, JumpPressed = 0, Crouched = 0;
	while (Sim.State() == ECSBodyState::Ragdoll || Sim.State() == ECSBodyState::Recovering)
	{
		++Frames;
		RecoveringFrames += Sim.State() == ECSBodyState::Recovering ? 1 : 0;
		Rig.Fire(TEXT("Move"), ETriggerEvent::Triggered, Forward);
		MoveQueued += C->GetPendingMovementInputVector().IsNearlyZero() ? 0 : 1;
		// Jump goes down (Started) during the ragdoll and is never released before control returns.
		Rig.Press(TEXT("Jump"));
		JumpPressed += C->bPressedJump ? 1 : 0;
		// Crouch is a toggle: one press while down must not leave the character crouched afterwards.
		if (Frames == 1)
		{
			Rig.Press(TEXT("Crouch"));
		}
		Crouched += (C->bIsCrouched || Move->bWantsToCrouch) ? 1 : 0;
		Sim.Step();
		if (Sim.Time > 30.f)
		{
			AddError(TEXT("Never recovered"));
			return false;
		}
	}
	AddInfo(FString::Printf(TEXT("%d frames ragdolled (%d recovering): Move queued input on %d, jump pressed on %d, crouched on %d"),
		Frames, RecoveringFrames, MoveQueued, JumpPressed, Crouched));
	TestTrue(TEXT("Both Ragdoll and Recovering were covered"), RecoveringFrames > 0 && Frames > RecoveringFrames);
	TestEqual(TEXT("Move through the binding queues no movement input while ragdolled or recovering"), MoveQueued, 0);
	TestEqual(TEXT("Jump through the binding presses no jump while ragdolled or recovering"), JumpPressed, 0);
	TestEqual(TEXT("Crouch through the binding does nothing while ragdolled or recovering"), Crouched, 0);

	// Control is back with Jump still held: no jump starts from the press made during the ragdoll.
	bool bJumped = false;
	for (int32 i = 0; i < 20; ++i)
	{
		Sim.Step();
		bJumped |= C->bPressedJump || !Move->IsMovingOnGround() || Move->Velocity.Z > 1.f;
	}
	TestFalse(TEXT("Jump held across the return starts no jump"), bJumped);
	Rig.Fire(TEXT("Jump"), ETriggerEvent::Completed);
	TestEqual(TEXT("Gait is still Jog (the crouch press while down did not toggle crouch)"), C->GetGait(), ECSGait::Jog);
	TestFalse(TEXT("Not crouched after control returns"), C->bIsCrouched || Move->bWantsToCrouch);
	TestEqual(TEXT("Move works again once control returns"), Rig.Fire(TEXT("Move"), ETriggerEvent::Triggered, Forward), 1);
	TestFalse(TEXT("Move queues input again once control returns"), C->GetPendingMovementInputVector().IsNearlyZero());

	// Held sprint during the ragdoll applies when control returns.
	Rig.Press(TEXT("DebugRagdoll"));
	Rig.Press(TEXT("Sprint"));
	TestEqual(TEXT("Sprint pressed while ragdolled does not change gait yet"), C->GetGait(), ECSGait::Jog);
	Rig.Press(TEXT("DebugRagdoll"));
	TestEqual(TEXT("R from the ragdoll starts the recovery"), Sim.State(), ECSBodyState::Recovering);
	Sim.RunUntil(2.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
	TestEqual(TEXT("Held sprint applies once control returns"), C->GetGait(), ECSGait::Sprint);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyRecoveryOffSteepSlopeTest, "CombatSimulator.CS.Body.RecoveryOffSteepSlope", CSPhysicsBodyTests::TestFlags)
bool FCSBodyRecoveryOffSteepSlopeTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	// Review Focus 1: a ragdoll that ends on a slope too steep to stand on (50 deg) still recovers
	// upright on walkable ground within a few seconds, without help.
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	const FVector RampFoot(200.f, 0.f, 0.f);
	const FVector Up = Sim.AddRamp(RampFoot, 50.f, 1200.f);
	const FVector Along(FMath::Cos(FMath::DegreesToRadians(50.f)), 0.f, FMath::Sin(FMath::DegreesToRadians(50.f)));
	Sim.PhysicsBody->EnterRagdoll();
	// Lay the ragdoll on the ramp, pelvis 150 cm up it.
	const FVector Pelvis = Sim.PelvisBody();
	Sim.MoveBodies(FQuat::Identity, FVector::ZeroVector, RampFoot + Along * 150.f + Up * 20.f - Pelvis);
	AddInfo(FString::Printf(TEXT("On the ramp: pelvis body at %s"), *Sim.PelvisBody().ToString()));
	const float Start = Sim.Time;
	const bool bBack = Sim.RunUntil(6.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
	AddInfo(FString::Printf(TEXT("50 deg ramp: back to Normal after %.2f s"), Sim.Time - Start));
	TestTrue(TEXT("50 deg ramp: recovered within 6 s"), bBack);
	CheckStanding(*this, Sim, TEXT("50 deg ramp"));
	Sim.Run(1.f);
	CheckStanding(*this, Sim, TEXT("50 deg ramp, 1 s later"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyRecoveryWithNoSpotNearPelvisTest, "CombatSimulator.CS.Body.RecoveryWithNoSpotNearPelvis", CSPhysicsBodyTests::TestFlags)
bool FCSBodyRecoveryWithNoSpotNearPelvisTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	// Nothing standable within RecoverySearchRadius of the pelvis (high on a long 50 deg ramp): the
	// recovery uses the last clear spot of this ragdoll (here where it started) instead of putting
	// the capsule in the air over the slope.
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	const FVector StartSpot = Sim.Character->GetActorLocation();
	const FVector RampFoot(200.f, 0.f, 0.f);
	const FVector Up = Sim.AddRamp(RampFoot, 50.f, 3000.f);
	const FVector Along(FMath::Cos(FMath::DegreesToRadians(50.f)), 0.f, FMath::Sin(FMath::DegreesToRadians(50.f)));
	Sim.PhysicsBody->EnterRagdoll();
	Sim.MoveBodies(FQuat::Identity, FVector::ZeroVector, RampFoot + Along * 1000.f + Up * 20.f - Sim.PelvisBody());
	AddInfo(FString::Printf(TEXT("High on the ramp: pelvis body at %s"), *Sim.PelvisBody().ToString()));
	Sim.PhysicsBody->Recover();
	TestEqual(TEXT("The recovery starts"), Sim.State(), ECSBodyState::Recovering);
	const bool bBack = Sim.RunUntil(Sim.PhysicsBody->GetRecoverySeconds() + 1.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
	TestTrue(TEXT("No spot near the pelvis: recovered"), bBack);
	CheckStanding(*this, Sim, TEXT("No spot near the pelvis"));
	AddInfo(FString::Printf(TEXT("Stood up at %s; the ragdoll started at %s"), *Sim.Character->GetActorLocation().ToString(), *StartSpot.ToString()));
	TestTrue(TEXT("No spot near the pelvis: stands at the last clear spot (where the ragdoll started)"),
		FVector::Dist2D(Sim.Character->GetActorLocation(), StartSpot) < 50.f);
	Sim.Run(1.f);
	CheckStanding(*this, Sim, TEXT("No spot near the pelvis, 1 s later"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyRagdollSlideIsBoundedTest, "CombatSimulator.CS.Body.RagdollSlideIsBounded", CSPhysicsBodyTests::TestFlags)
bool FCSBodyRagdollSlideIsBoundedTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	// A 750 cm/s shove on flat ground: after the body first touches down it travels 1.5 to 3 m on
	// the ground before it settles (the flight before touchdown is not damped). Ground damping and
	// friction come from the tuning: the damping is on only on the ground, the friction material is
	// on the bodies while ragdolled, both follow a reload and both are undone after the recovery.
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	UCSMovementTuning& T = *Sim.Tuning;
	USkeletalMeshComponent* Mesh = Sim.Mesh();
	FBodyInstance* Spine = Mesh->GetBodyInstance(TEXT("spine_03"));
	const float BaseLinear = Spine->LinearDamping;
	const UPhysicalMaterial* BaseMaterial = Spine->GetSimplePhysicalMaterial();
	const FVector Start = Sim.PelvisBody();
	Sim.PhysicsBody->ApplyImpulseAtBone(TEXT("spine_03"), Sim.ImpulseFor(750.f, FVector::ForwardVector));
	TestEqual(TEXT("A 750 cm/s shove ragdolls"), Sim.State(), ECSBodyState::Ragdoll);
	TestNearlyEqual(TEXT("In the air: the bodies keep their own linear damping"), Spine->LinearDamping, BaseLinear, 0.001f);
	TestNearlyEqual(TEXT("Ragdoll angular damping applied on entry"), Mesh->GetBodyInstance(TEXT("hand_l"))->AngularDamping, T.RagdollAngularDamping, 0.001f);
	const UPhysicalMaterial* Material = Spine->GetSimplePhysicalMaterial();
	TestTrue(TEXT("Ragdoll friction material applied (friction RagdollFriction, combined by Max)"), Material && Material != BaseMaterial
		&& FMath::IsNearlyEqual(Material->Friction, T.RagdollFriction) && Material->bOverrideFrictionCombineMode
		&& Material->FrictionCombineMode == EFrictionCombineMode::Max);

	FVector Touchdown = FVector::ZeroVector;
	bool bTouched = false;
	bool bGroundDampingOnGround = true;
	const bool bRecovering = Sim.RunUntil(T.RagdollMaxSeconds + 1.f, [&]()
	{
		if (!bTouched && Sim.PhysicsBody->IsRagdollOnGround())
		{
			bTouched = true;
			Touchdown = Sim.PelvisBody();
		}
		if (Sim.PhysicsBody->IsRagdollOnGround())
		{
			bGroundDampingOnGround &= FMath::IsNearlyEqual(Spine->LinearDamping, T.RagdollLinearDamping);
		}
		return Sim.State() == ECSBodyState::Recovering;
	});
	const float Flight = FVector::Dist2D(Start, Touchdown);
	const float Ground = FVector::Dist2D(Touchdown, Sim.PelvisBody());
	AddInfo(FString::Printf(TEXT("750 cm/s shove (ground linear damping %.2f, angular %.2f, friction %.2f): %.0f cm before touchdown, %.0f cm on the ground"),
		T.RagdollLinearDamping, T.RagdollAngularDamping, T.RagdollFriction, Flight, Ground));
	TestTrue(TEXT("Settled"), bRecovering);
	TestTrue(TEXT("Touched down"), bTouched);
	TestTrue(TEXT("On the ground the ragdoll linear damping is on"), bGroundDampingOnGround);
	TestTrue(TEXT("After touchdown the body travels 1.5 to 3 m on the ground"), Ground >= 150.f && Ground <= 300.f);
	Sim.RunUntil(2.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
	TestNearlyEqual(TEXT("The body's own damping is back after the recovery"), Spine->LinearDamping, BaseLinear, 0.001f);
	TestTrue(TEXT("The body's own material is back after the recovery"), Spine->GetSimplePhysicalMaterial() == BaseMaterial);

	// A reload while ragdolled on the ground changes the damping and the friction at once: in the
	// same frame, through ACSCharacter::OnTuningApplied (issue #34), not on the next tick.
	Sim.PhysicsBody->EnterRagdoll();
	Sim.RunUntil(3.f, [&]() { return Sim.PhysicsBody->IsRagdollOnGround(); });
	TestTrue(TEXT("Lying on the ground"), Sim.PhysicsBody->IsRagdollOnGround());
	T.RagdollLinearDamping = 4.f;
	T.RagdollFriction = 1.7f;
	FString Message;
	TestTrue(TEXT("Reload with new ragdoll damping and friction"), UCSDevTools::ReloadTuning(*Sim.Character, Message));
	TestNearlyEqual(TEXT("A reload while ragdolled applies the new damping"), Spine->LinearDamping, 4.f, 0.001f);
	TestTrue(TEXT("A reload while ragdolled applies the new friction"), FMath::IsNearlyEqual(Spine->GetSimplePhysicalMaterial()->Friction, 1.7f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyRagdollFallsFreelyTest, "CombatSimulator.CS.Body.RagdollFallsFreely", CSPhysicsBodyTests::TestFlags)
bool FCSBodyRagdollFallsFreelyTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	// A ragdoll dropped from about 5 m falls like a body: before it hits the floor it reaches at
	// least 90 percent of sqrt(2 g h) for the height it fell (ground damping does not apply in the air).
	FBodySim Sim;
	if (!Sim.Init(*this, 0.f, FVector(0.f, 0.f, 90.f + 500.f), false))
	{
		return false;
	}
	Sim.PhysicsBody->EnterRagdoll();
	const float StartZ = Sim.PelvisBody().Z;
	const float G = FMath::Abs(Sim.World.GetTestWorld()->GetGravityZ());
	float MaxSpeed = 0.f, DropAtMax = 0.f;
	for (int32 i = 0; i < 120; ++i)
	{
		Sim.Step();
		const float Down = -Sim.Mesh()->GetPhysicsLinearVelocity(TEXT("pelvis")).Z;
		if (Down > MaxSpeed)
		{
			MaxSpeed = Down;
			DropAtMax = StartZ - Sim.PelvisBody().Z;
		}
	}
	const float Free = FMath::Sqrt(2.f * G * DropAtMax);
	AddInfo(FString::Printf(TEXT("Fastest fall %.0f cm/s after %.0f cm; free fall from that height %.0f cm/s (%.0f percent)"),
		MaxSpeed, DropAtMax, Free, Free > 0.f ? 100.f * MaxSpeed / Free : 0.f));
	TestTrue(TEXT("Fell at least 3 m before the impact"), DropAtMax >= 300.f);
	TestTrue(TEXT("Reached at least 90 percent of free-fall speed before the impact"), MaxSpeed >= 0.9f * Free);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyRecoveryWaitsForSpaceTest, "CombatSimulator.CS.Body.RecoveryWaitsForSpace", CSPhysicsBodyTests::TestFlags)
bool FCSBodyRecoveryWaitsForSpaceTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	// Lying under a low ceiling with nowhere clear to stand (the ceiling covers the search radius and
	// the spot where the ragdoll started): the recovery does not stand the capsule up inside the
	// ceiling. It stays Ragdoll with the capsule not colliding, keeps trying, and stands up once the
	// ceiling is gone.
	AddExpectedMessagePlain(TEXT("nowhere clear to stand up"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	Sim.HoldRagdollDown();
	Sim.PhysicsBody->EnterRagdoll();
	Sim.Run(1.5f);
	TestEqual(TEXT("Lying down"), Sim.State(), ECSBodyState::Ragdoll);
	const float Top = Sim.Mesh()->Bounds.GetBox().Max.Z;
	AddInfo(FString::Printf(TEXT("Lying body top at %.0f cm; ceiling from %.0f cm"), Top, Top + 10.f));
	// A 20 m square slab from just above the body to 4 m.
	UBoxComponent* Ceiling = Sim.AddBox(FVector(0.f, 0.f, Top + 10.f + 200.f), FVector(1000.f, 1000.f, 200.f));

	Sim.PhysicsBody->Recover();
	TestEqual(TEXT("R under the ceiling: still Ragdoll"), Sim.State(), ECSBodyState::Ragdoll);
	TestEqual(TEXT("R under the ceiling: the capsule is still not colliding"), Sim.Capsule()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	// Normal settle times again: the automatic recovery keeps trying and keeps failing.
	Sim.Tuning->RagdollSettleSeconds = 0.5f;
	FString Message;
	UCSDevTools::ReloadTuning(*Sim.Character, Message);
	bool bLeftRagdoll = false;
	for (int32 i = 0; i < FMath::CeilToInt(3.f / Sim.Dt); ++i)
	{
		Sim.Step();
		bLeftRagdoll |= Sim.State() != ECSBodyState::Ragdoll || Sim.Capsule()->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
	}
	TestFalse(TEXT("Under the ceiling for 3 s: never stood up, capsule never colliding"), bLeftRagdoll);

	Ceiling->GetOwner()->Destroy();
	const bool bBack = Sim.RunUntil(Sim.Tuning->RagdollSettleSeconds + Sim.PhysicsBody->GetRecoverySeconds() + 1.f, [&]() { return Sim.State() == ECSBodyState::Normal; });
	TestTrue(TEXT("Ceiling gone: the next try stands up"), bBack);
	CheckStanding(*this, Sim, TEXT("Ceiling gone"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyPushKeyStaggersTest, "CombatSimulator.CS.Body.PushKeyStaggers", CSPhysicsBodyTests::TestFlags)
bool FCSBodyPushKeyStaggersTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
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
	using namespace CSPhysicsBodyTests;
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
	using namespace CSPhysicsBodyTests;
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
	using namespace CSPhysicsBodyTests;
	// The owner-only first-person body copies the body's pose, physics included;
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

// A character spawned in the middle of a frame (here from a timer, as an AI spawner or a respawn
// would) must come up with a working physics body. Without the deferred start in
// UCSPhysicsBodyComponent this crashed in the physical animation component on the new
// character's first frame.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodySpawnMidFrameTest, "CombatSimulator.CS.Body.SpawnMidFrameIsSafe", CSPhysicsBodyTests::TestFlags)
bool FCSBodySpawnMidFrameTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	UWorld* World = Sim.World.GetTestWorld();
	TWeakObjectPtr<ACSCharacter> Spawned;
	bool bSpawnedInTick = false;
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([World, &Spawned, &bSpawnedInTick, Tuning = Sim.Tuning.Get()]()
	{
		bSpawnedInTick = World->bInTick;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		const FTransform SpawnTransform(FVector(400.f, 0.f, 120.f));
		ACSCharacter* Character = World->SpawnActor<ACSCharacter>(ACSCharacter::StaticClass(), SpawnTransform, Params);
		if (Character)
		{
			Character->Tuning = Tuning;
			Character->FinishSpawning(SpawnTransform);
			Character->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			Character->GetCharacterMovement()->SetDefaultMovementMode();
		}
		Spawned = Character;
	}));
	Sim.Run(1.f);

	if (!TestTrue(TEXT("The timer spawned a second character"), Spawned.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("The spawn happened inside the world tick"), bSpawnedInTick);
	UCSPhysicsBodyComponent* Body = Spawned->GetPhysicsBody();
	if (!TestNotNull(TEXT("The spawned character has a physics body component"), Body))
	{
		return false;
	}
	TestTrue(TEXT("Its physics body is ready after the first frames"), Body->HasPhysicsBody());
	TestEqual(TEXT("Its body is in the Normal state"), Body->GetState(), ECSBodyState::Normal);
	TestTrue(TEXT("It stands on the floor"), Spawned->GetCharacterMovement()->IsMovingOnGround());

	// The body works like any other: a large hit ragdolls it and it gets back up.
	Body->ApplyImpulseAtBone(TEXT("spine_03"), FVector(Body->GetBodyMass() * 900.f, 0.f, 0.f));
	Sim.Run(0.2f);
	TestTrue(TEXT("A hit after a mid-frame spawn moves the body out of Normal"), Body->GetState() != ECSBodyState::Normal);
	return true;
}

namespace CSPhysicsBodyTests
{
	/** A plain actor to stand for whatever hit the character. */
	AActor* SpawnHitter(FBodySim& Sim, AActor* Owner = nullptr)
	{
		FActorSpawnParameters Params;
		Params.Owner = Owner;
		return Sim.World.GetTestWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	}

	/** A contact on the chest from behind (+X), sized as a share of the ragdoll threshold. */
	ECSContactResult ChestContact(FBodySim& Sim, float ShareOfThreshold, AActor* Other = nullptr, float Speed = 1000.f)
	{
		const UCSMovementTuning& T = *Sim.Tuning;
		const FVector Chest = Sim.Mesh()->GetBoneLocation(TEXT("spine_03"));
		const float Mass = Sim.PhysicsBody->GetBodyMass() * ShareOfThreshold * T.RagdollImpulseThreshold / (Speed * T.ContactImpulseScale);
		return Sim.PhysicsBody->ReceiveContact(Other, TEXT("spine_03"), FVector(Speed, 0.f, 0.f), Mass, Chest - FVector(100.f, 0.f, 0.f), Chest);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyContactStaggersAndRagdollsTest, "CombatSimulator.CS.Body.ContactStaggersAndRagdolls", CSPhysicsBodyTests::TestFlags)
bool FCSBodyContactStaggersAndRagdollsTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	const UCSMovementTuning& T = *Sim.Tuning;
	TestTrue(TEXT("No contact yet"), Sim.PhysicsBody->GetLastContact().Time < 0.0);

	// A body of known mass and speed at the chest, below the ragdoll threshold: a stagger.
	TestEqual(TEXT("Below the threshold staggers"), ChestContact(Sim, 0.6f), ECSContactResult::Stagger);
	TestEqual(TEXT("State is Hit"), Sim.State(), ECSBodyState::Hit);
	const FCSContactRecord First = Sim.PhysicsBody->GetLastContact();
	TestEqual(TEXT("The contact is recorded on the chest"), First.Bone, FName(TEXT("spine_03")));
	TestEqual(TEXT("As a stagger"), First.Result, ECSContactResult::Stagger);
	TestNearlyEqual(TEXT("With its size"), First.VelocityChange, 0.6f * T.RagdollImpulseThreshold, 1.f);
	TestTrue(TEXT("Back to Normal after the stagger"), Sim.RunUntil(3.f, [&Sim] { return Sim.State() == ECSBodyState::Normal; }));

	// Above the threshold: a ragdoll.
	TestEqual(TEXT("Above the threshold ragdolls"), ChestContact(Sim, 1.5f), ECSContactResult::Ragdoll);
	TestEqual(TEXT("State is Ragdoll"), Sim.State(), ECSBodyState::Ragdoll);
	TestEqual(TEXT("Recorded as a ragdoll"), Sim.PhysicsBody->GetLastContact().Result, ECSContactResult::Ragdoll);

	// Already down: the physics engine moves the bodies; the contact adds nothing.
	TestEqual(TEXT("A contact on a ragdoll changes nothing"), ChestContact(Sim, 1.5f), ECSContactResult::Ignored);
	TestEqual(TEXT("Still Ragdoll"), Sim.State(), ECSBodyState::Ragdoll);

	// The clamp: a huge body cannot give more than ContactImpulseMax.
	FBodySim Other;
	if (!Other.Init(*this))
	{
		return false;
	}
	ChestContact(Other, 100.f);
	TestNearlyEqual(TEXT("A huge hit is clamped to ContactImpulseMax"), Other.PhysicsBody->GetLastContact().VelocityChange, Other.Tuning->ContactImpulseMax, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyContactFiltersTest, "CombatSimulator.CS.Body.ContactFilters", CSPhysicsBodyTests::TestFlags)
bool FCSBodyContactFiltersTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	const UCSMovementTuning& T = *Sim.Tuning;
	const FVector Chest = Sim.Mesh()->GetBoneLocation(TEXT("spine_03"));
	const FVector Behind = Chest - FVector(100.f, 0.f, 0.f);

	// Slower than MinContactSpeed, however heavy: resting on the character, leaning, being pushed.
	TestEqual(TEXT("A slow contact is not a hit"),
		Sim.PhysicsBody->ReceiveContact(nullptr, TEXT("spine_03"), FVector(T.MinContactSpeed - 10.f, 0.f, 0.f), 5000.f, Behind, Chest), ECSContactResult::None);
	// Something the character walked into: it is at rest, or moving away.
	TestEqual(TEXT("A body at rest is not a hit"),
		Sim.PhysicsBody->ReceiveContact(nullptr, TEXT("spine_03"), FVector::ZeroVector, 5000.f, Behind, Chest), ECSContactResult::None);
	TestEqual(TEXT("A body moving away is not a hit"),
		Sim.PhysicsBody->ReceiveContact(nullptr, TEXT("spine_03"), FVector(-1000.f, 0.f, 0.f), 50.f, Behind, Chest), ECSContactResult::None);
	TestEqual(TEXT("None of them changed the state"), Sim.State(), ECSBodyState::Normal);
	TestTrue(TEXT("Or were recorded"), Sim.PhysicsBody->GetLastContact().Time < 0.0);

	// The ball a character threw never hits that character.
	AActor* OwnBall = SpawnHitter(Sim, Sim.Character);
	TestEqual(TEXT("Something the character threw is not a hit"), ChestContact(Sim, 0.6f, OwnBall), ECSContactResult::None);
	TestEqual(TEXT("State still Normal"), Sim.State(), ECSBodyState::Normal);

	// The same object cannot hit again at once.
	AActor* Box = SpawnHitter(Sim);
	TestEqual(TEXT("The first contact of an object hits"), ChestContact(Sim, 0.3f, Box), ECSContactResult::Stagger);
	TestEqual(TEXT("The same object straight after does not"), ChestContact(Sim, 0.3f, Box), ECSContactResult::None);
	AActor* AnotherBox = SpawnHitter(Sim);
	TestEqual(TEXT("Another object does"), ChestContact(Sim, 0.3f, AnotherBox), ECSContactResult::Stagger);
	Sim.Run(T.ContactRepeatSeconds + 0.1f);
	TestEqual(TEXT("The first object can hit again after ContactRepeatSeconds"), ChestContact(Sim, 0.3f, Box), ECSContactResult::Stagger);

	// No bone given (what the capsule reports): the body nearest the contact takes it.
	FBodySim Head;
	if (!Head.Init(*this))
	{
		return false;
	}
	const FVector HeadAt = Head.Mesh()->GetBoneLocation(TEXT("head"));
	const float Mass = Head.PhysicsBody->GetBodyMass() * 0.3f;
	TestEqual(TEXT("A contact with no bone still hits"),
		Head.PhysicsBody->ReceiveContact(nullptr, NAME_None, FVector(Head.Tuning->RagdollImpulseThreshold, 0.f, 0.f), Mass, HeadAt - FVector(60.f, 0.f, 0.f), HeadAt + FVector(0.f, 0.f, 8.f)),
		ECSContactResult::Stagger);
	const FName Bone = Head.PhysicsBody->GetLastContact().Bone;
	AddInfo(FString::Printf(TEXT("A contact above the head bone landed on %s"), *Bone.ToString()));
	TestEqual(TEXT("It lands on the chain of the head"), Head.PhysicsBody->GetChainRoot(Bone), FName(TEXT("head")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyThrownBallHitsABodyPartTest, "CombatSimulator.CS.Body.ThrownBallHitsABodyPart", CSPhysicsBodyTests::TestFlags)
bool FCSBodyThrownBallHitsABodyPartTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	UWorld* World = Sim.World.GetTestWorld();
	const FVector Chest = Sim.Mesh()->GetBoneLocation(TEXT("spine_03"));

	// The ball of the character itself, thrown the way the T key throws it, flies off and never hits it.
	ACSHitBall* Own = Sim.Character->GetDevTools()->RequestThrow();
	if (!TestNotNull(TEXT("The throw key makes a ball"), Own))
	{
		return false;
	}
	TestEqual(TEXT("The thrower owns it"), Own->GetOwner(), static_cast<AActor*>(Sim.Character));
	Sim.Run(0.5f);
	TestTrue(TEXT("The thrower is not hit by its own ball"), Sim.PhysicsBody->GetLastContact().Time < 0.0);
	TestTrue(TEXT("The ball left"), FVector::Dist(Own->GetActorLocation(), Sim.Character->GetActorLocation()) > 300.f);

	// A ball from somewhere else, at the chest: it passes the capsule and strikes a body.
	ACSHitBall* Ball = ACSHitBall::Throw(World, nullptr, Chest - FVector(300.f, 0.f, 0.f), FVector(1.f, 0.f, 0.f), 10.f, 2000.f);
	if (!TestNotNull(TEXT("A ball is thrown at the chest"), Ball))
	{
		return false;
	}
	const bool bHit = Sim.RunUntil(1.5f, [&Sim] { return Sim.PhysicsBody->GetLastContact().Time >= 0.0; });
	if (!TestTrue(TEXT("The ball hits the character"), bHit))
	{
		AddInfo(FString::Printf(TEXT("Ball now at %s moving %s; chest at %s; body state %d; mesh profile %s, notify %d"),
			*Ball->GetActorLocation().ToString(), *Ball->GetBall()->GetPhysicsLinearVelocity().ToString(), *Chest.ToString(), int32(Sim.State()),
			*Sim.Mesh()->GetCollisionProfileName().ToString(), int32(Sim.Mesh()->BodyInstance.bNotifyRigidBodyCollision)));
		return false;
	}
	const FCSContactRecord& Contact = Sim.PhysicsBody->GetLastContact();
	AddInfo(FString::Printf(TEXT("Ball hit %s for %.0f cm/s: %d"), *Contact.Bone.ToString(), Contact.VelocityChange, int32(Contact.Result)));
	TestTrue(TEXT("On a body part"), Contact.Bone != NAME_None);
	TestTrue(TEXT("On the trunk, where it was aimed"), Contact.Bone.ToString().StartsWith(TEXT("spine")) || Contact.Bone == TEXT("pelvis"));
	TestTrue(TEXT("As a stagger or a ragdoll"), Contact.Result == ECSContactResult::Stagger || Contact.Result == ECSContactResult::Ragdoll);
	// 10 kg at 2000 cm/s on this body, scale 1: the expected size, give or take the angle of the contact.
	const float Expected = 10.f * 2000.f / Sim.PhysicsBody->GetBodyMass() * Sim.Tuning->ContactImpulseScale;
	TestTrue(FString::Printf(TEXT("About the momentum of the ball over the mass of the body (%.0f, expected %.0f)"), Contact.VelocityChange, Expected),
		Contact.VelocityChange > 0.5f * Expected && Contact.VelocityChange <= 1.05f * Expected);

	// Never more than MaxBalls.
	for (int32 i = 0; i < ACSHitBall::MaxBalls + 5; ++i)
	{
		ACSHitBall::Throw(World, nullptr, FVector(5000.f + 100.f * i, 5000.f, 200.f), FVector(0.f, 1.f, 0.f), 1.f, 100.f);
	}
	Sim.Step();
	int32 Alive = 0;
	for (TActorIterator<ACSHitBall> It(World); It; ++It)
	{
		Alive += It->IsActorBeingDestroyed() ? 0 : 1;
	}
	TestTrue(FString::Printf(TEXT("At most %d balls exist (%d)"), ACSHitBall::MaxBalls, Alive), Alive <= ACSHitBall::MaxBalls);
	TestEqual(TEXT("RemoveAll removes them"), ACSHitBall::RemoveAll(World), Alive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyWalkingIntoARestingBoxIsNotAHitTest, "CombatSimulator.CS.Body.WalkingIntoARestingBoxIsNotAHit", CSPhysicsBodyTests::TestFlags)
bool FCSBodyWalkingIntoARestingBoxIsNotAHitTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	// A 20 kg box resting on the floor two metres ahead; the character jogs into it and shoves it.
	UBoxComponent* Box = Sim.AddBox(FVector(200.f, 0.f, 30.f), FVector(30.f, 30.f, 30.f));
	Box->SetMobility(EComponentMobility::Movable);
	Box->SetCollisionProfileName(TEXT("PhysicsActor"));
	Box->SetSimulatePhysics(true);
	Box->SetMassOverrideInKg(NAME_None, 20.f, true);
	Sim.Run(0.5f);
	const FVector BoxStart = Box->GetComponentLocation();
	Sim.Run(3.f, FVector(1.f, 0.f, 0.f));
	AddInfo(FString::Printf(TEXT("Character at x %.0f, box moved %.0f cm, state %d"), Sim.Character->GetActorLocation().X,
		FVector::Dist(BoxStart, Box->GetComponentLocation()), int32(Sim.State())));
	TestTrue(TEXT("The character reached the box"), Sim.Character->GetActorLocation().X > 100.f);
	TestTrue(TEXT("Walking into a resting box is not a hit"), Sim.PhysicsBody->GetLastContact().Time < 0.0);
	TestEqual(TEXT("The body stayed Normal"), Sim.State(), ECSBodyState::Normal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyLauncherFiresAtWhoStandsInFrontTest, "CombatSimulator.CS.Body.LauncherFiresAtWhoStandsInFront", CSPhysicsBodyTests::TestFlags)
bool FCSBodyLauncherFiresAtWhoStandsInFrontTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	FBodySim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	UWorld* World = Sim.World.GetTestWorld();
	// Facing -X from x = 800: its stand box (800 cm in front) is where the character stands.
	const FTransform At(FRotator(0.f, 180.f, 0.f), FVector(800.f, 0.f, 0.f));
	ACSBallLauncher* Launcher = World->SpawnActor<ACSBallLauncher>(ACSBallLauncher::StaticClass(), At);
	if (!TestNotNull(TEXT("Launcher"), Launcher))
	{
		return false;
	}
	Launcher->IntervalSeconds = 1.f;
	const bool bFired = Sim.RunUntil(3.f, [Launcher] { return Launcher->GetFiredCount() > 0; });
	TestTrue(TEXT("It fires at a character standing in front"), bFired);
	const bool bHit = Sim.RunUntil(2.f, [&Sim] { return Sim.PhysicsBody->GetLastContact().Time >= 0.0; });
	TestTrue(TEXT("And the ball hits it"), bHit);
	if (bHit)
	{
		AddInfo(FString::Printf(TEXT("Launcher ball hit %s for %.0f cm/s"), *Sim.PhysicsBody->GetLastContact().Bone.ToString(), Sim.PhysicsBody->GetLastContact().VelocityChange));
	}

	// With nobody on the square it is idle.
	Sim.Character->SetActorLocation(FVector(0.f, 3000.f, 120.f), false, nullptr, ETeleportType::TeleportPhysics);
	Sim.Run(0.5f);
	TestNull(TEXT("Nobody on the square: no target"), Launcher->FindTarget());
	const int32 Before = Launcher->GetFiredCount();
	Sim.Run(2.5f);
	TestEqual(TEXT("Nobody on the square: it does not fire"), Launcher->GetFiredCount(), Before);
	TestFalse(TEXT("FireOnce does nothing either"), Launcher->FireOnce());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyGetsUpAgainstTheFloorTest, "CombatSimulator.CS.Body.GetsUpAgainstTheFloor", CSPhysicsBodyTests::TestFlags)
bool FCSBodyGetsUpAgainstTheFloorTest::RunTest(const FString& Parameters)
{
	using namespace CSPhysicsBodyTests;
	// Getting up is physical: from lying, the body is pulled to its feet by motors with gravity and
	// the floor still acting, by way of the crouch, and only then blended onto the pose.
	{
		FBodySim Sim;
		if (!Sim.Init(*this))
		{
			return false;
		}
		auto PelvisZ = [&Sim]() { return float(Sim.Mesh()->GetBodyInstance(TEXT("pelvis"))->GetUnrealWorldTransform().GetLocation().Z); };
		const float StandingPelvis = float(Sim.Mesh()->GetBoneLocation(TEXT("pelvis")).Z);
		Sim.PhysicsBody->EnterRagdoll();
		if (!TestTrue(TEXT("The ragdoll settles and starts to recover"), Sim.RunUntil(Sim.Tuning->RagdollMaxSeconds + 1.f, [&]() { return Sim.State() == ECSBodyState::Recovering; })))
		{
			return false;
		}
		const float LyingPelvis = PelvisZ();
		TestTrue(TEXT("The recovery starts by getting up"), Sim.PhysicsBody->IsGettingUp());
		TestTrue(TEXT("Toward the crouch first"), Sim.PhysicsBody->IsGetUpCrouch());
		TestTrue(TEXT("From lying on the floor"), LyingPelvis < 0.5f * StandingPelvis);

		float GettingUpSeconds = 0.f, CrouchSeconds = 0.f, LowestBody = 1000.f, PelvisAtEnd = LyingPelvis;
		bool bSimulatedThroughout = true;
		while (Sim.PhysicsBody->IsGettingUp() && GettingUpSeconds < 10.f)
		{
			Sim.Step();
			if (!Sim.PhysicsBody->IsGettingUp())
			{
				break;
			}
			GettingUpSeconds += Sim.Dt;
			CrouchSeconds += Sim.PhysicsBody->IsGetUpCrouch() ? Sim.Dt : 0.f;
			PelvisAtEnd = PelvisZ();
			for (const FBodyInstance* B : Sim.Mesh()->Bodies)
			{
				if (B && B->IsValidBodyInstance())
				{
					LowestBody = FMath::Min(LowestBody, float(B->GetUnrealWorldTransform().GetLocation().Z));
					bSimulatedThroughout &= B->IsInstanceSimulatingPhysics();
				}
			}
		}
		AddInfo(FString::Printf(TEXT("Got up for %.2f s (%.2f s toward the crouch): pelvis from %.0f to %.0f cm (standing %.0f); lowest body centre %.1f cm"),
			GettingUpSeconds, CrouchSeconds, LyingPelvis, PelvisAtEnd, StandingPelvis, LowestBody));
		TestTrue(TEXT("Getting up takes GetUpCrouchSeconds + GetUpStandSeconds"), FMath::Abs(GettingUpSeconds - Sim.PhysicsBody->GetGetUpSeconds()) <= 3.f * Sim.Dt);
		TestTrue(TEXT("The crouch part takes GetUpCrouchSeconds"), FMath::Abs(CrouchSeconds - Sim.Tuning->GetUpCrouchSeconds) <= 3.f * Sim.Dt);
		TestTrue(TEXT("Every body stays simulated while getting up"), bSimulatedThroughout);
		TestTrue(TEXT("The motors lift the pelvis most of the way to standing"), PelvisAtEnd > StandingPelvis - 25.f);
		TestTrue(TEXT("No body goes under the floor on the way"), LowestBody > -2.f);
		TestEqual(TEXT("The closing blend follows"), Sim.State(), ECSBodyState::Recovering);
		TestTrue(TEXT("Control returns after the blend"), Sim.RunUntil(Sim.Tuning->RagdollRecoverSeconds + 0.5f, [&]() { return Sim.State() == ECSBodyState::Normal; }));
		CheckStanding(*this, Sim, TEXT("After getting up"));
	}
	// With the pull at 0 the recovery is the blend alone, as it was before there was a get-up.
	{
		FBodySim Sim;
		if (!Sim.Init(*this))
		{
			return false;
		}
		Sim.Tuning->GetUpStrength = 0.f;
		Sim.Character->ApplyTuning();
		Sim.PhysicsBody->EnterRagdoll();
		Sim.Run(0.2f);
		Sim.PhysicsBody->Recover();
		TestEqual(TEXT("No pull: the recovery starts"), Sim.State(), ECSBodyState::Recovering);
		TestFalse(TEXT("No pull: there is no getting up"), Sim.PhysicsBody->IsGettingUp());
		TestEqual(TEXT("No pull: the recovery is the blend"), Sim.PhysicsBody->GetRecoverySeconds(), Sim.Tuning->RagdollRecoverSeconds);
		TestTrue(TEXT("No pull: control returns"), Sim.RunUntil(Sim.Tuning->RagdollRecoverSeconds + 0.5f, [&]() { return Sim.State() == ECSBodyState::Normal; }));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

