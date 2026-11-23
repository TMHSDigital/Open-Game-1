// Map checks: the default map and game mode, the player spawning on L_Plane, and the movement gym's
// slopes. Run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Maps
//
// The maps are made by Game/Content/Python/create_movement_maps.py. The spawn and slope tests play the
// real maps in PIE (Play In Editor), so the game mode, PlayerStart, possession and input setup are the
// ones a player gets. They live in the game module because they load /Game maps.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CSCharacter.h"
#include "../CSGameMode.h"
#include "CSMovementTuning.h"
#include "Editor.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameMapsSettings.h"
#include "InputMappingContext.h"
#include "Misc/PackageName.h"
#include "Settings/ProjectPackagingSettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace CSMapTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const TCHAR* PlaneMap = TEXT("/Game/Maps/L_Plane");
	const TCHAR* GymMap = TEXT("/Game/Maps/L_MovementGym");
	const TCHAR* TuningPath = TEXT("/Game/Characters/Base/DA_MovementTuning.DA_MovementTuning");
	const TCHAR* MeshPath = TEXT("/Game/Characters/Base/SK_BaseCharacter.SK_BaseCharacter");
	const TCHAR* Regenerate = TEXT(" (run Game/Content/Python/create_movement_maps.py, see its docstring)");

	// Real seconds to wait for PIE to start and the player to spawn before giving up.
	constexpr double StartTimeoutSeconds = 120.0;

	UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	ACSCharacter* PlayerCharacter(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? Cast<ACSCharacter>(PC->GetPawn()) : nullptr;
	}

	AActor* FindTagged(UWorld* World, FName Tag)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(Tag))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/**
	 * Opens Map in the editor and plays it in PIE, then waits until the player's pawn exists and the
	 * world has run SettleSeconds of game time. Errors (and stops waiting) after StartTimeoutSeconds.
	 */
	void StartPlay(FAutomationTestBase* Test, const FString& Map, float SettleSeconds)
	{
		if (!FPackageName::DoesPackageExist(Map))
		{
			Test->AddError(FString::Printf(TEXT("%s does not exist%s"), *Map, Regenerate));
		}
		AutomationOpenMap(Map);
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		const double Start = FPlatformTime::Seconds();
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, Start, SettleSeconds, Map]()
		{
			UWorld* World = PlayWorld();
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			if (PC && PC->GetPawn() && World->GetTimeSeconds() >= SettleSeconds)
			{
				return true;
			}
			if (FPlatformTime::Seconds() - Start > StartTimeoutSeconds)
			{
				Test->AddError(FString::Printf(TEXT("%s: no player pawn after %.0f s of PIE (play world %s, controller %s)"),
					*Map, StartTimeoutSeconds, World ? TEXT("yes") : TEXT("no"), PC ? TEXT("yes") : TEXT("no")));
				return true;
			}
			return false;
		}));
	}

	void EndPlay()
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		// One more frame so the play world is torn down before the next test opens a map.
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	}

	/** Distance from the capsule bottom to the world's floor at z = 0 (positive above). */
	float FeetHeight(const ACharacter& Character)
	{
		return Character.GetActorLocation().Z - Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	/** One gym ramp: its tag, angle and whether the character must be able to walk up it. */
	struct FRampCase
	{
		FName Tag;
		float AngleDeg = 0.f;
		bool bWalkable = false;
	};

	/** State of the ramp walk, kept across frames. */
	struct FRampRun
	{
		int32 CaseIndex = 0;
		bool bStarted = false;
		double StartTime = 0.0;
		FVector Forward = FVector::ForwardVector;
		float TopZ = 0.f;
		float MaxFeet = -1000.f;
		bool bReachedTop = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMapsDefaultMapIsPlaneTest, "CombatSimulator.CS.Maps.DefaultMapIsPlane", CSMapTests::TestFlags)
bool FCSMapsDefaultMapIsPlaneTest::RunTest(const FString& Parameters)
{
	using namespace CSMapTests;
	const FString PlaneObject = FString(PlaneMap) + TEXT(".L_Plane");

	// GetGameDefaultMap returns the package name ("/Game/Maps/L_Plane") of the configured object path.
	TestEqual(TEXT("Game default map"), FSoftObjectPath(UGameMapsSettings::GetGameDefaultMap()).GetLongPackageName(), FString(PlaneMap));
	TestEqual(TEXT("Editor startup map"), GetDefault<UGameMapsSettings>()->EditorStartupMap.ToString(), PlaneObject);
	TestTrue(FString(TEXT("L_Plane exists")) + Regenerate, FPackageName::DoesPackageExist(PlaneMap));
	TestTrue(FString(TEXT("L_MovementGym exists")) + Regenerate, FPackageName::DoesPackageExist(GymMap));

	// The default game mode spawns ACSCharacter with the game's tuning asset.
	const FString ModePath = UGameMapsSettings::GetGlobalDefaultGameMode();
	TestEqual(TEXT("Global default game mode"), ModePath, FString(TEXT("/Script/CombatSimulator.CSGameMode")));
	const ACSGameMode* Mode = GetDefault<ACSGameMode>();
	TestTrue(TEXT("Default pawn is ACSCharacter"), Mode->DefaultPawnClass.Get() == ACSCharacter::StaticClass());
	TestTrue(TEXT("Game mode has a HUD class (the overlay draws through AHUD)"), Mode->HUDClass && Mode->HUDClass->IsChildOf(AHUD::StaticClass()));
	TestEqual(TEXT("Game mode tuning"), Mode->CharacterTuning.ToSoftObjectPath().ToString(), FString(TuningPath));

	// The default map must be cooked, or a packaged build has nothing to load.
	bool bPlaneCooked = false;
	for (const FFilePath& Map : GetDefault<UProjectPackagingSettings>()->MapsToCook)
	{
		bPlaneCooked |= Map.FilePath == PlaneMap;
	}
	TestTrue(TEXT("L_Plane is in MapsToCook"), bPlaneCooked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMapsPlaneSpawnsCharacterStandingTest, "CombatSimulator.CS.Maps.PlaneSpawnsCharacterStanding", CSMapTests::TestFlags)
bool FCSMapsPlaneSpawnsCharacterStandingTest::RunTest(const FString& Parameters)
{
	using namespace CSMapTests;
	StartPlay(this, PlaneMap, 1.5f);

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = PlayWorld();
		if (!World)
		{
			AddError(TEXT("No play world"));
			return true;
		}
		TestTrue(FString::Printf(TEXT("Playing L_Plane (map %s)"), *World->GetMapName()), World->GetMapName().EndsWith(TEXT("L_Plane")));
		TestTrue(TEXT("Game mode is ACSGameMode"), World->GetAuthGameMode() && World->GetAuthGameMode()->IsA<ACSGameMode>());

		APlayerController* PC = World->GetFirstPlayerController();
		ACSCharacter* Character = PlayerCharacter(World);
		if (!TestNotNull(FString::Printf(TEXT("Player pawn is an ACSCharacter (pawn %s)"),
			PC && PC->GetPawn() ? *PC->GetPawn()->GetClass()->GetName() : TEXT("none")), Character))
		{
			return true;
		}
		TestTrue(TEXT("Possessed by the player"), Character->IsPlayerControlled());
		TestEqual(TEXT("Tuning is DA_MovementTuning"), Character->Tuning ? Character->Tuning->GetPathName() : FString(TEXT("none")), FString(TuningPath));
		const USkeletalMesh* Mesh = Character->GetMesh()->GetSkeletalMeshAsset();
		TestEqual(TEXT("Mesh is SK_BaseCharacter"), Mesh ? Mesh->GetPathName() : FString(TEXT("none")), FString(MeshPath));
		TestNotNull(TEXT("Animation instance running"), Character->GetMesh()->GetAnimInstance());

		// Standing on the plane: walking, on a walkable floor, the capsule bottom just above z = 0
		// (the movement component keeps 1.9 to 2.4 cm of clearance), not under it.
		const UCharacterMovementComponent* Move = Character->GetCharacterMovement();
		TestTrue(TEXT("Moving on ground"), Move->IsMovingOnGround());
		TestTrue(TEXT("Floor is walkable"), Move->CurrentFloor.IsWalkableFloor());
		const float Feet = FeetHeight(*Character);
		TestTrue(FString::Printf(TEXT("Feet on the plane: capsule bottom %.2f cm above z 0"), Feet), Feet >= -0.5f && Feet <= 3.f);
		TestTrue(TEXT("Standing on the floor actor"), Move->CurrentFloor.HitResult.GetActor() && Move->CurrentFloor.HitResult.GetActor()->ActorHasTag(TEXT("Floor")));
		TestFalse(TEXT("Not ragdolled"), Character->IsRagdolled());

		// Input and the overlay's HUD come with possession on a real map.
		ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
		UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		const UInputMappingContext* Context = Character->InputMappingContext.Get();
		TestTrue(TEXT("Input mapping context added on possession"), Input && Context && Input->HasMappingContext(Context));
		TestNotNull(TEXT("Player has a HUD"), PC ? PC->GetHUD() : nullptr);

		AddInfo(FString::Printf(TEXT("Spawned %s at %s, feet %.2f cm above the plane"), *Character->GetName(), *Character->GetActorLocation().ToString(), Feet));
		return true;
	}));

	EndPlay();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMapsGymSlopesTest, "CombatSimulator.CS.Maps.GymSlopes", CSMapTests::TestFlags)
bool FCSMapsGymSlopesTest::RunTest(const FString& Parameters)
{
	using namespace CSMapTests;
	StartPlay(this, GymMap, 1.f);

	const TArray<FRampCase> Cases = {
		{TEXT("Ramp15"), 15.f, true},
		{TEXT("Ramp30"), 30.f, true},
		{TEXT("Ramp44"), 44.f, true},
		{TEXT("Ramp50"), 50.f, false},
	};
	TSharedRef<FRampRun> Run = MakeShared<FRampRun>();

	// Walks the player's character at each ramp in turn, straight up the slope at a jog.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Cases, Run]()
	{
		UWorld* World = PlayWorld();
		ACSCharacter* Character = PlayerCharacter(World);
		if (!World || !Character)
		{
			AddError(TEXT("No play world or player character"));
			return true;
		}
		const FRampCase& Case = Cases[Run->CaseIndex];
		AActor* Ramp = FindTagged(World, Case.Tag);
		if (!Ramp)
		{
			AddError(FString::Printf(TEXT("No actor tagged %s in L_MovementGym%s"), *Case.Tag.ToString(), Regenerate));
			return true;
		}
		UCharacterMovementComponent* Move = Character->GetCharacterMovement();

		if (!Run->bStarted)
		{
			// The ramp is a scaled 1 m engine cube pitched up toward its +X: the top face runs from
			// local (-50, 0, 50) at the ground to (50, 0, 50) at the top.
			const FTransform& T = Ramp->GetActorTransform();
			const FVector Low = T.TransformPosition(FVector(-50.f, 0.f, 50.f));
			const FVector High = T.TransformPosition(FVector(50.f, 0.f, 50.f));
			Run->Forward = (High - Low).GetSafeNormal2D();
			Run->TopZ = High.Z;
			const float MeasuredDeg = FMath::RadiansToDegrees(FMath::Atan2(High.Z - Low.Z, FVector::Dist2D(High, Low)));
			TestNearlyEqual(FString::Printf(TEXT("%s angle"), *Case.Tag.ToString()), MeasuredDeg, Case.AngleDeg, 0.01f);
			TestNearlyEqual(FString::Printf(TEXT("%s starts on the floor"), *Case.Tag.ToString()), float(Low.Z), 0.f, 0.5f);

			// The surface itself, as the movement component judges it.
			FHitResult Hit;
			const FVector Mid = (Low + High) * 0.5f;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(CSMapTestsRamp), false, Character);
			if (TestTrue(FString::Printf(TEXT("%s surface found"), *Case.Tag.ToString()),
				World->LineTraceSingleByChannel(Hit, Mid + FVector(0.f, 0.f, 100.f), Mid - FVector(0.f, 0.f, 100.f), ECC_Visibility, Query)) && Hit.GetActor() == Ramp)
			{
				const float NormalDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Hit.ImpactNormal.Z, -1.f, 1.f)));
				TestEqual(FString::Printf(TEXT("%s walkable (surface %.3f deg, limit %.1f deg)"), *Case.Tag.ToString(), NormalDeg, Move->GetWalkableFloorAngle()),
					Move->IsWalkable(Hit), Case.bWalkable);
				AddInfo(FString::Printf(TEXT("%s surface %.4f deg, walkable limit %.4f deg: %s"), *Case.Tag.ToString(), NormalDeg, Move->GetWalkableFloorAngle(),
					Move->IsWalkable(Hit) ? TEXT("walkable") : TEXT("not walkable")));
			}

			// Start 2.5 m before the foot of the ramp, facing up it, standing still.
			const FVector Start = Low - Run->Forward * 250.f + FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3.f);
			Character->SetActorLocationAndRotation(Start, Run->Forward.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
			Character->GetController()->SetControlRotation(Run->Forward.Rotation());
			Move->Velocity = FVector::ZeroVector;
			Move->SetMovementMode(MOVE_Falling);
			Run->StartTime = World->GetTimeSeconds();
			Run->MaxFeet = -1000.f;
			Run->bReachedTop = false;
			Run->bStarted = true;
			return false;
		}

		const double Elapsed = World->GetTimeSeconds() - Run->StartTime;
		Run->MaxFeet = FMath::Max(Run->MaxFeet, FeetHeight(*Character));
		// On the platform: standing on it, the capsule bottom about 2 cm above its top. (On the slope
		// just below the edge the rounded capsule bottom is lower than that.)
		Run->bReachedTop |= Move->IsMovingOnGround() && FeetHeight(*Character) >= Run->TopZ;
		// 0.3 s to land after the teleport, then push up the slope for 5 s or until the top is reached.
		if (Elapsed > 0.3 && !Run->bReachedTop)
		{
			Character->AddMovementInput(Run->Forward, 1.f);
		}
		if (Elapsed < 5.3 && !Run->bReachedTop)
		{
			return false;
		}

		if (Case.bWalkable)
		{
			TestTrue(FString::Printf(TEXT("%s: walked up to the top at %.0f cm (highest feet %.1f cm, %.2f s)"), *Case.Tag.ToString(), Run->TopZ, Run->MaxFeet, Elapsed), Run->bReachedTop);
		}
		else
		{
			// Too steep: the movement component treats it as a wall. Stepping onto the lip is allowed,
			// standing on the slope is not.
			TestTrue(FString::Printf(TEXT("%s: could not climb (highest feet %.1f cm of %.0f cm)"), *Case.Tag.ToString(), Run->MaxFeet, Run->TopZ), !Run->bReachedTop && Run->MaxFeet < 60.f);
		}
		AddInfo(FString::Printf(TEXT("%s: top %.0f cm, highest feet %.1f cm, reached top %s, %.2f s"), *Case.Tag.ToString(), Run->TopZ, Run->MaxFeet, Run->bReachedTop ? TEXT("yes") : TEXT("no"), Elapsed));

		Run->bStarted = false;
		return ++Run->CaseIndex >= Cases.Num();
	}));

	EndPlay();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
