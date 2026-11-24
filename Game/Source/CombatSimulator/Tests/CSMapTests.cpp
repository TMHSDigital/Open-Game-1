// Map checks: the default map and game mode, the player spawning on L_Plane, respawning after a fall,
// the dev map switch (F2) and the movement gym's slopes. Run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Maps
//
// The maps are made by Game/Content/Python/create_movement_maps.py. The spawn and slope tests play the
// real maps in PIE (Play In Editor), so the game mode, PlayerStart, possession and input setup are the
// ones a player gets. They live in the game module because they load /Game maps.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CSCharacter.h"
#include "CSDevTools.h"
#include "../CSGameMode.h"
#include "CSMovementTuning.h"
#include "CSPlayTestRig.h"
#include "Editor.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameMapsSettings.h"
#include "InputMappingContext.h"
#include "Misc/PackageName.h"
#include "Settings/ProjectPackagingSettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace CSMapTests
{
	// PlaneMap, GymMap, PlayWorld, PlayerCharacter, StartPlay and EndPlay: the shared play rig.
	using namespace CSPlayRig;

	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const TCHAR* TuningPath = TEXT("/Game/Characters/Base/DA_MovementTuning.DA_MovementTuning");
	const TCHAR* MeshPath = TEXT("/Game/Characters/Base/SK_BaseCharacter.SK_BaseCharacter");
	const TCHAR* Regenerate = TEXT(" (run Game/Content/Python/create_movement_maps.py, see its docstring)");

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMapsMapSwitchTargetsTest, "CombatSimulator.CS.Maps.MapSwitchTargets", CSMapTests::TestFlags)
bool FCSMapsMapSwitchTargetsTest::RunTest(const FString& Parameters)
{
	using namespace CSMapTests;
	// F2 switches between the plane and the gym (decision 0013), from the game mode's config.
	const ACSGameMode* Mode = GetDefault<ACSGameMode>();
	TestEqual(TEXT("DevSwitchMaps (DefaultGame.ini)"), Mode->DevSwitchMaps, TArray<FString>{PlaneMap, GymMap});
	TestEqual(TEXT("From L_Plane: the gym"), Mode->GetMapSwitchTarget(PlaneMap), FString(GymMap));
	TestEqual(TEXT("From L_MovementGym: the plane"), Mode->GetMapSwitchTarget(GymMap), FString(PlaneMap));
	TestEqual(TEXT("From L_Plane in PIE (prefixed package)"), Mode->GetMapSwitchTarget(TEXT("/Game/Maps/UEDPIE_0_L_Plane")), FString(GymMap));
	TestEqual(TEXT("From a map not in the list: the first"), Mode->GetMapSwitchTarget(TEXT("/Game/Maps/L_CaptureStage")), FString(PlaneMap));

	// Every map F2 can open must be cooked, or the packaged build cannot open it.
	for (const FString& Map : Mode->DevSwitchMaps)
	{
		bool bCooked = false;
		for (const FFilePath& Cooked : GetDefault<UProjectPackagingSettings>()->MapsToCook)
		{
			bCooked |= Cooked.FilePath == Map;
		}
		TestTrue(FString::Printf(TEXT("%s is in MapsToCook"), *Map), bCooked);
		TestTrue(FString::Printf(TEXT("%s exists%s"), *Map, Regenerate), FPackageName::DoesPackageExist(Map));
	}
	return true;
}

namespace CSMapTests
{
	/** State of the respawn test, kept across frames. */
	struct FRespawnRun
	{
		int32 Stage = 0;
		double StageStart = 0.0;
		TWeakObjectPtr<APawn> OldPawn;
		FVector StartSpot = FVector::ZeroVector;
	};

	/** The player's new character once it differs from the old pawn; null before. Checks where it stands. */
	ACSCharacter* RespawnedCharacter(FAutomationTestBase& Test, UWorld* World, const FRespawnRun& Run, const TCHAR* Case)
	{
		ACSCharacter* Character = PlayerCharacter(World);
		if (!Character || Character == Run.OldPawn.Get())
		{
			return nullptr;
		}
		const float Dist = FVector::Dist2D(Character->GetActorLocation(), Run.StartSpot);
		Test.TestTrue(FString::Printf(TEXT("%s: respawned at the PlayerStart (%.0f cm from it)"), Case, Dist), Dist < 100.f);
		Test.TestTrue(FString::Printf(TEXT("%s: respawned character is player controlled"), Case), Character->IsPlayerControlled());
		Test.TestFalse(FString::Printf(TEXT("%s: old pawn is gone"), Case), Run.OldPawn.IsValid() && !Run.OldPawn->IsActorBeingDestroyed());
		return Character;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMapsRespawnTest, "CombatSimulator.CS.Maps.Respawn", CSMapTests::TestFlags)
bool FCSMapsRespawnTest::RunTest(const FString& Parameters)
{
	using namespace CSMapTests;
	StartPlay(this, PlaneMap, 1.f);
	TSharedRef<FRespawnRun> Run = MakeShared<FRespawnRun>();

	// 0: destroy the player's character; 1: wait for the respawn; 2: put it past the edge of the
	// plane; 3: wait for it to fall below the kill height and respawn.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UWorld* World = PlayWorld();
		ACSGameMode* Mode = World ? Cast<ACSGameMode>(World->GetAuthGameMode()) : nullptr;
		if (!World || !Mode)
		{
			AddError(TEXT("No play world or ACSGameMode"));
			return true;
		}
		const double Now = World->GetTimeSeconds();
		switch (Run->Stage)
		{
		case 0:
		{
			TActorIterator<APlayerStart> Start(World);
			if (!TestTrue(TEXT("L_Plane has a PlayerStart"), bool(Start)))
			{
				return true;
			}
			Run->StartSpot = Start->GetActorLocation();
			ACSCharacter* Character = PlayerCharacter(World);
			if (!TestNotNull(TEXT("Player character before the destroy"), Character))
			{
				return true;
			}
			Run->OldPawn = Character;
			Character->Destroy();
			Run->Stage = 1;
			Run->StageStart = Now;
			return false;
		}
		case 1:
		case 3:
		{
			const TCHAR* Case = Run->Stage == 1 ? TEXT("Destroyed") : TEXT("Fell off the plane");
			if (ACSCharacter* Character = RespawnedCharacter(*this, World, *Run, Case))
			{
				AddInfo(FString::Printf(TEXT("%s: respawned as %s at %s after %.2f s"), Case, *Character->GetName(), *Character->GetActorLocation().ToString(), Now - Run->StageStart));
				if (Run->Stage == 3)
				{
					return true;
				}
				Run->Stage = 2;
				Run->StageStart = Now;
				return false;
			}
			// Falling 50 m takes about 3.3 s.
			if (Now - Run->StageStart > 10.0)
			{
				const APawn* Pawn = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
				AddError(FString::Printf(TEXT("%s: no respawn after %.0f s (pawn %s at %s)"), Case, Now - Run->StageStart,
					*GetNameSafe(Pawn), Pawn ? *Pawn->GetActorLocation().ToString() : TEXT("-")));
				return true;
			}
			return false;
		}
		case 2:
		{
			// Past the plane's edge (it ends 500 m out), falling.
			ACSCharacter* Character = PlayerCharacter(World);
			if (!TestNotNull(TEXT("Player character before the fall"), Character))
			{
				return true;
			}
			Run->OldPawn = Character;
			TestTrue(TEXT("Kill height is below the plane"), Mode->RespawnKillZ < -1000.f);
			Character->SetActorLocation(FVector(55000.f, 0.f, 200.f), false, nullptr, ETeleportType::TeleportPhysics);
			Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			Run->Stage = 3;
			Run->StageStart = Now;
			return false;
		}
		default:
			return true;
		}
	}));

	EndPlay();
	return true;
}

namespace CSMapTests
{
	/** State of the map switch test, kept across frames. */
	struct FSwitchRun
	{
		int32 Index = 0;
		bool bRequested = false;
		double RequestTime = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSMapsMapSwitchKeyTest, "CombatSimulator.CS.Maps.MapSwitchKey", CSMapTests::TestFlags)
bool FCSMapsMapSwitchKeyTest::RunTest(const FString& Parameters)
{
	using namespace CSMapTests;
	StartPlay(this, PlaneMap, 0.5f);
	TSharedRef<FSwitchRun> Run = MakeShared<FSwitchRun>();
	Run->RequestTime = FPlatformTime::Seconds();

	// The dev tools' map switch request (what F2 sends) opens the gym from the plane, then the plane
	// from the gym, each time in a new world with a new player character.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		const TCHAR* From[] = {TEXT("L_Plane"), TEXT("L_MovementGym")};
		const TCHAR* To[] = {GymMap, PlaneMap};
		UWorld* World = PlayWorld();
		ACSCharacter* Character = PlayerCharacter(World);
		const double Waited = FPlatformTime::Seconds() - Run->RequestTime;
		if (!Run->bRequested)
		{
			if (!World || !Character || !World->GetMapName().EndsWith(From[Run->Index]))
			{
				if (Waited > StartTimeoutSeconds)
				{
					AddError(FString::Printf(TEXT("Map switch %d: not playing %s (playing %s)"), Run->Index, From[Run->Index], World ? *World->GetMapName() : TEXT("nothing")));
					return true;
				}
				return false;
			}
			ACSGameMode* Mode = Cast<ACSGameMode>(World->GetAuthGameMode());
			if (!TestNotNull(TEXT("ACSGameMode"), Mode))
			{
				return true;
			}
			TestTrue(TEXT("Dev tools on (DA_MovementTuning)"), Character->GetDevTools()->AreEnabled());
			TestTrue(FString::Printf(TEXT("F2 request accepted in %s"), From[Run->Index]), Character->GetDevTools()->RequestMapSwitch());
			TestEqual(FString::Printf(TEXT("F2 from %s targets"), From[Run->Index]), Mode->GetLastMapSwitchTarget(), FString(To[Run->Index]));
			Run->bRequested = true;
			Run->RequestTime = FPlatformTime::Seconds();
			return false;
		}
		// Wait for the other map to load with a player character in it.
		const FString Arrive = FPackageName::GetShortName(To[Run->Index]);
		if (World && Character && World->GetMapName().EndsWith(Arrive))
		{
			AddInfo(FString::Printf(TEXT("Map switch %d: arrived in %s after %.2f s"), Run->Index, *World->GetMapName(), Waited));
			Run->bRequested = false;
			Run->RequestTime = FPlatformTime::Seconds();
			return ++Run->Index >= 2;
		}
		if (Waited > StartTimeoutSeconds)
		{
			AddError(FString::Printf(TEXT("Map switch %d: %s did not open (playing %s)"), Run->Index, *Arrive, World ? *World->GetMapName() : TEXT("nothing")));
			return true;
		}
		return false;
	}));

	EndPlay();
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
		{TEXT("Ramp44"), 43.9f, true}, // built 0.1 deg under the 44 deg limit (create_movement_maps.py)
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
			// The gym's ramps are measured against the tuning's walkable limit of 44 deg.
			TestNearlyEqual(TEXT("Walkable slope limit"), Move->GetWalkableFloorAngle(), 44.f, 0.001f);
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
