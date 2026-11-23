// Camera rig checks that need the base character (SK_BaseCharacter: its eye socket, its head and neck
// bones). Run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Camera
//
// These tests live in the game module, not the CSCharacter plugin, because they load the game's
// assets (/Game/...); the plugin assumes no content paths. The other camera tests are in the plugin
// (Private/Tests/CSCameraTests.cpp).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimationRuntime.h"
#include "Camera/CameraTypes.h"
#include "Components/BoxComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CSCameraRig.h"
#include "CSCharacter.h"
#include "CSMovementTuning.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Tests/AutomationCommon.h"

namespace CSCameraAssetTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const TCHAR* MeshPath = TEXT("/Game/Characters/Base/SK_BaseCharacter.SK_BaseCharacter");
	const TCHAR* Regenerate = TEXT(" (missing or stale: run Game/Content/Python/import_base_character.py, see its docstring)");
	// Between the eyes, in mesh space (the mesh faces +Y, feet at the origin): the value
	// import_base_character.py places the socket at.
	const FVector ExpectedEyes(0.f, 9.5f, 168.f);

	// Owner-hidden in first person (spec 5.3).
	const TCHAR* HeadBones[] = {TEXT("neck_01"), TEXT("neck_02"), TEXT("head")};
	// Visible in both views: arms, hands, torso and legs.
	const TCHAR* BodyBones[] = {
		TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_03"), TEXT("spine_05"),
		TEXT("clavicle_l"), TEXT("clavicle_r"), TEXT("upperarm_l"), TEXT("upperarm_r"),
		TEXT("lowerarm_l"), TEXT("lowerarm_r"), TEXT("hand_l"), TEXT("hand_r"),
		TEXT("index_01_l"), TEXT("thumb_02_r"),
		TEXT("thigh_l"), TEXT("thigh_r"), TEXT("calf_l"), TEXT("calf_r"), TEXT("foot_l"), TEXT("foot_r"),
	};

	// The engine's per-view rule for owner visibility (FPrimitiveSceneProxy::IsShown): a primitive
	// with bOnlyOwnerSee is drawn only in a view whose view actor owns it, directly or through its
	// owner chain; one with bOwnerNoSee is drawn in every view except those.
	bool IsDrawnInViewOf(const UPrimitiveComponent& Component, const AActor* ViewActor)
	{
		if (!Component.IsVisible() || Component.bHiddenInGame)
		{
			return false;
		}
		bool bOwned = false;
		for (const AActor* Owner = Component.GetOwner(); Owner; Owner = Owner->GetOwner())
		{
			bOwned |= Owner == ViewActor;
		}
		return !(Component.bOnlyOwnerSee && !bOwned) && !(Component.bOwnerNoSee && bOwned);
	}

	// True if any primitive drawn in the view of ViewActor shows the bone.
	bool IsBoneSeenBy(TArray<USkinnedMeshComponent*> Meshes, FName Bone, const AActor* ViewActor)
	{
		for (USkinnedMeshComponent* Mesh : Meshes)
		{
			if (IsDrawnInViewOf(*Mesh, ViewActor) && Mesh->GetBoneIndex(Bone) != INDEX_NONE && !Mesh->IsBoneHiddenByName(Bone))
			{
				return true;
			}
		}
		return false;
	}

	// A test world with a 200 m floor, one ACSCharacter (mesh and animation from config) possessed by a
	// player controller, and a stand-in for another player's view.
	struct FCameraSim
	{
		FTestWorldWrapper World;
		ACSCharacter* Character = nullptr;
		APlayerController* Controller = nullptr;
		AActor* OtherPlayer = nullptr;
		float Dt = 1.f / 60.f;

		bool Init(FAutomationTestBase& Test, bool bPossess = true)
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
			OtherPlayer = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(500.f, 0.f, 100.f)), Params);
			if (!Character || !OtherPlayer)
			{
				Test.AddError(TEXT("Could not spawn the character"));
				return false;
			}
			Character->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			Character->GetCharacterMovement()->SetDefaultMovementMode();
			Run(0.5f);
			if (!bPossess)
			{
				return true;
			}
			Controller = W->SpawnActor<APlayerController>();
			if (!Controller)
			{
				Test.AddError(TEXT("Could not spawn a player controller"));
				return false;
			}
			Controller->Possess(Character);
			return true;
		}

		~FCameraSim()
		{
			if (World.GetTestWorld())
			{
				World.DestroyTestWorld(false);
			}
		}

		void Run(float Seconds)
		{
			for (int32 i = 0; i < FMath::CeilToInt(Seconds / Dt); ++i)
			{
				World.TickTestWorld(Dt);
			}
		}
	};
}

using namespace CSCameraAssetTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSCameraEyeSocketOnHeadTest, "CombatSimulator.CS.Camera.EyeSocketOnHead", CSCameraAssetTests::TestFlags)
bool FCSCameraEyeSocketOnHeadTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
	if (!TestNotNull(FString(TEXT("SK_BaseCharacter")) + Regenerate, Mesh))
	{
		return false;
	}
	const USkeletalMeshSocket* Socket = Mesh->FindSocket(TEXT("eyes"));
	if (!TestNotNull(FString(TEXT("Socket eyes")) + Regenerate, Socket))
	{
		return false;
	}
	TestEqual(TEXT("The eye socket is on the head bone"), Socket->BoneName, FName(TEXT("head")));
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 Head = Ref.FindBoneIndex(TEXT("head"));
	if (!TestTrue(TEXT("Head bone exists"), Head != INDEX_NONE))
	{
		return false;
	}
	const FTransform HeadCS = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Head);
	const FTransform EyesCS = Socket->GetSocketLocalTransform() * HeadCS;
	AddInfo(FString::Printf(TEXT("Eyes in mesh space (rest pose): %s; head bone at %s"), *EyesCS.GetLocation().ToString(), *HeadCS.GetLocation().ToString()));
	TestTrue(FString(TEXT("Eyes between the eyes, 168 cm up, 9.5 cm forward")) + Regenerate, EyesCS.GetLocation().Equals(ExpectedEyes, 0.5f));
	TestTrue(TEXT("Eyes are in front of the head bone"), EyesCS.GetLocation().Y > HeadCS.GetLocation().Y);
	TestTrue(TEXT("The socket's X axis looks forward (+Y in mesh space)"), EyesCS.GetUnitAxis(EAxis::X).Equals(FVector(0.f, 1.f, 0.f), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSCameraFirstPersonHidesHeadForOwnerOnlyTest, "CombatSimulator.CS.Camera.FirstPersonHidesHeadForOwnerOnly", CSCameraAssetTests::TestFlags)
bool FCSCameraFirstPersonHidesHeadForOwnerOnlyTest::RunTest(const FString& Parameters)
{
	// Spec 5.3 and Review Focus 5: in first person the head and neck are hidden from the owner only;
	// another player still sees them. Arms, hands, torso and legs are seen in both views by both.
	FCameraSim Sim;
	if (!Sim.Init(*this))
	{
		return false;
	}
	ACSCharacter* Character = Sim.Character;
	UCSCameraRig* Rig = Character->GetCameraRig();
	USkeletalMeshComponent* Body = Character->GetMesh();
	UPoseableMeshComponent* OwnerBody = Rig->GetFirstPersonBody();
	if (!TestNotNull(TEXT("Body mesh"), Body->GetSkeletalMeshAsset()) || !TestNotNull(TEXT("Owner-only first-person body"), OwnerBody))
	{
		return false;
	}
	TestTrue(TEXT("The owner-only body is the same mesh"), OwnerBody->GetSkinnedAsset() == Body->GetSkeletalMeshAsset());
	TestTrue(TEXT("The owner-only body is seen only by its owner"), OwnerBody->bOnlyOwnerSee);
	TestFalse(TEXT("The owner-only body casts no second shadow"), OwnerBody->CastShadow);
	TestTrue(TEXT("The body keeps its shadow while hidden from the owner"), Body->bCastHiddenShadow);
	const TArray<USkinnedMeshComponent*> Meshes = {Body, OwnerBody};
	const AActor* Owner = Character;
	const AActor* Other = Sim.OtherPlayer;
	TestTrue(TEXT("The player's view actor is the character"), Sim.Controller->GetViewTarget() == Character);

	auto CheckView = [&](const TCHAR* ViewName, bool bOwnerSeesHead)
	{
		for (const TCHAR* Bone : HeadBones)
		{
			TestEqual(FString::Printf(TEXT("%s: owner sees %s"), ViewName, Bone), IsBoneSeenBy(Meshes, Bone, Owner), bOwnerSeesHead);
			TestTrue(FString::Printf(TEXT("%s: another player sees %s"), ViewName, Bone), IsBoneSeenBy(Meshes, Bone, Other));
			// Hiding a bone on the shared body would hide it from everyone; it is never done.
			TestFalse(FString::Printf(TEXT("%s: %s is not hidden on the shared body"), ViewName, Bone), Body->IsBoneHiddenByName(Bone));
		}
		for (const TCHAR* Bone : BodyBones)
		{
			TestTrue(FString::Printf(TEXT("%s: owner sees %s"), ViewName, Bone), IsBoneSeenBy(Meshes, Bone, Owner));
			TestTrue(FString::Printf(TEXT("%s: another player sees %s"), ViewName, Bone), IsBoneSeenBy(Meshes, Bone, Other));
		}
		TestTrue(FString::Printf(TEXT("%s: another player sees the body mesh"), ViewName), IsDrawnInViewOf(*Body, Other));
		TestFalse(FString::Printf(TEXT("%s: another player never sees the owner-only body"), ViewName), IsDrawnInViewOf(*OwnerBody, Other));
	};

	TestEqual(TEXT("Starts in third person"), Rig->GetEffectiveView(), ECSView::ThirdPerson);
	CheckView(TEXT("Third person"), true);
	TestFalse(TEXT("Third person: head shown to the owner"), Rig->IsHeadHiddenForOwner());

	Character->ToggleCamera();
	Sim.Run(Character->GetActiveTuning().CameraBlendSeconds + Sim.Dt);
	TestEqual(TEXT("First person"), Rig->GetEffectiveView(), ECSView::FirstPerson);
	TestTrue(TEXT("First person: head hidden from the owner"), Rig->IsHeadHiddenForOwner());
	CheckView(TEXT("First person"), false);
	for (const TCHAR* Bone : HeadBones)
	{
		TestTrue(FString::Printf(TEXT("First person: %s hidden on the owner-only body"), Bone), OwnerBody->IsBoneHiddenByName(Bone));
	}

	// The owner-only body shows this frame's pose of the body (animation, later physics).
	Sim.Run(0.25f);
	float WorstBoneGap = 0.f;
	for (const TCHAR* Bone : BodyBones)
	{
		WorstBoneGap = FMath::Max(WorstBoneGap, FVector::Dist(Body->GetBoneLocation(Bone), OwnerBody->GetBoneLocation(Bone)));
	}
	AddInfo(FString::Printf(TEXT("Largest bone gap between the body and the owner-only body: %.3f cm"), WorstBoneGap));
	TestTrue(TEXT("The owner-only body follows the body's pose"), WorstBoneGap < 0.1f);

	// Looking straight down (Review Focus 5; the player camera manager's limit is -89.9): hands and
	// feet are in the frame, beyond the near plane. The arms hang beside the hips, a little behind
	// the eyes, so they leave the bottom of the frame when the view is tilted forward of straight down.
	Sim.Controller->SetControlRotation(FRotator(-89.f, 0.f, 0.f));
	Sim.Run(0.3f);
	FMinimalViewInfo POV;
	Character->CalcCamera(0.f, POV);
	const float Aspect = 16.f / 9.f;
	const float TanHalfH = FMath::Tan(FMath::DegreesToRadians(POV.FOV * 0.5f));
	const float TanHalfV = TanHalfH / Aspect;
	const FRotationMatrix Axes(POV.Rotation);
	for (const TCHAR* Bone : {TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r")})
	{
		const FVector Rel = OwnerBody->GetBoneLocation(Bone) - POV.Location;
		const float Fwd = Rel | Axes.GetUnitAxis(EAxis::X);
		const float Right = Rel | Axes.GetUnitAxis(EAxis::Y);
		const float Up = Rel | Axes.GetUnitAxis(EAxis::Z);
		AddInfo(FString::Printf(TEXT("Looking down: %s at %.0f cm, %.1f%% right, %.1f%% up of the half frame"),
			Bone, Rel.Size(), 100.f * Right / (Fwd * TanHalfH), 100.f * Up / (Fwd * TanHalfV)));
		TestTrue(FString::Printf(TEXT("Looking down: %s in the frame"), Bone),
			Fwd > 0.f && FMath::Abs(Right) < Fwd * TanHalfH && FMath::Abs(Up) < Fwd * TanHalfV);
		TestTrue(FString::Printf(TEXT("Looking down: %s well beyond the near plane"), Bone), Fwd > POV.GetFinalPerspectiveNearClipPlane() + 10.f);
		TestTrue(FString::Printf(TEXT("Looking down: owner sees %s"), Bone), IsBoneSeenBy(Meshes, Bone, Owner));
	}

	Character->ToggleCamera();
	Sim.Run(Character->GetActiveTuning().CameraBlendSeconds + Sim.Dt);
	TestEqual(TEXT("Back in third person"), Rig->GetEffectiveView(), ECSView::ThirdPerson);
	CheckView(TEXT("Third person again"), true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSCameraNoHeadBobRunningTest, "CombatSimulator.CS.Camera.NoHeadBobRunning", CSCameraAssetTests::TestFlags)
bool FCSCameraNoHeadBobRunningTest::RunTest(const FString& Parameters)
{
	// Spec 5.3: no head bob. Jogging and sprinting in first person with the real animation, the
	// camera's height above the capsule varies by less than 1 cm (the head itself bounces).
	FCameraSim Sim;
	if (!Sim.Init(*this, false))
	{
		return false;
	}
	ACSCharacter* Character = Sim.Character;
	UCSCameraRig* Rig = Character->GetCameraRig();
	USkeletalMeshComponent* Body = Character->GetMesh();
	Rig->SetView(ECSView::FirstPerson);
	Sim.Run(0.5f);
	for (const ECSGait Gait : {ECSGait::Jog, ECSGait::Sprint})
	{
		const TCHAR* Name = Gait == ECSGait::Jog ? TEXT("Jog") : TEXT("Sprint");
		Character->SetGait(Gait);
		auto Step = [&]()
		{
			Character->AddMovementInput(FVector::ForwardVector, 1.f);
			Sim.World.TickTestWorld(Sim.Dt);
		};
		// Reach a steady speed and let the filters settle.
		for (int32 i = 0; i < FMath::CeilToInt(2.5f / Sim.Dt); ++i)
		{
			Step();
		}
		float CamMin = TNumericLimits<float>::Max(), CamMax = -CamMin, HeadMin = CamMin, HeadMax = -CamMin;
		TArray<float> Heads;
		for (int32 i = 0; i < FMath::CeilToInt(2.f / Sim.Dt); ++i)
		{
			Step();
			const float Capsule = Character->GetActorLocation().Z;
			FMinimalViewInfo POV;
			Character->CalcCamera(0.f, POV);
			const float Cam = POV.Location.Z - Capsule;
			const float Head = Body->GetSocketLocation(TEXT("eyes")).Z - Capsule;
			CamMin = FMath::Min(CamMin, Cam);
			CamMax = FMath::Max(CamMax, Cam);
			HeadMin = FMath::Min(HeadMin, Head);
			HeadMax = FMath::Max(HeadMax, Head);
			Heads.Add(Head);
		}
		// Bounces of the head per second: rising crossings of its mean height.
		float Mean = 0.f;
		for (const float H : Heads) { Mean += H / Heads.Num(); }
		int32 Crossings = 0;
		for (int32 i = 1; i < Heads.Num(); ++i) { Crossings += Heads[i - 1] < Mean && Heads[i] >= Mean; }
		const float Speed = Character->GetCharacterMovement()->Velocity.Size2D();
		AddInfo(FString::Printf(TEXT("%s at %.0f cm/s over 2 s: eye socket height varies %.2f cm (%.1f bounces/s), camera height %.2f cm"),
			Name, Speed, HeadMax - HeadMin, Crossings / 2.f, CamMax - CamMin));
		TestTrue(FString::Printf(TEXT("%s: moving at speed"), Name), Speed > 300.f);
		TestTrue(FString::Printf(TEXT("%s: in first person"), Name), Rig->GetEffectiveView() == ECSView::FirstPerson);
		TestTrue(FString::Printf(TEXT("%s: the camera bounces less than 1 cm"), Name), CamMax - CamMin < 1.f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
