// Shared helpers for game-module tests that need a running game.
//
// CSPlayRig: plays a real map in PIE (Play In Editor), so the game mode, player controller,
// PlayerStart, possession and input setup are the ones a player gets. Used by the map tests and the
// pause and settings tests.
//
// FCSBareCharacterWorld: an empty test world with one ACSCharacter and no controller, for tests that
// only need the character's own objects (tuning, dev tools).

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CSCharacter.h"
#include "CSMovementTuning.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/PackageName.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/CSTestWorld.h"

namespace CSPlayRig
{
	inline const TCHAR* PlaneMap = TEXT("/Game/Maps/L_Plane");
	inline const TCHAR* GymMap = TEXT("/Game/Maps/L_MovementGym");
	inline const TCHAR* RegenerateMaps = TEXT(" (run Game/Content/Python/create_movement_maps.py, see its docstring)");

	// Real seconds to wait for PIE to start and the player to spawn before giving up.
	constexpr double StartTimeoutSeconds = 120.0;

	inline UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	inline ACSCharacter* PlayerCharacter(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? Cast<ACSCharacter>(PC->GetPawn()) : nullptr;
	}

	/**
	 * Opens Map in the editor and plays it in PIE, then waits until the player's pawn exists and the
	 * world has run SettleSeconds of game time. Errors (and stops waiting) after StartTimeoutSeconds.
	 */
	inline void StartPlay(FAutomationTestBase* Test, const FString& Map, float SettleSeconds)
	{
		if (!FPackageName::DoesPackageExist(Map))
		{
			Test->AddError(FString::Printf(TEXT("%s does not exist%s"), *Map, RegenerateMaps));
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

	inline void EndPlay()
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		// One more frame so the play world is torn down before the next test opens a map.
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	}
}

/** A test world holding one ACSCharacter with the given tuning, no floor and no controller. Not ticked. */
struct FCSBareCharacterWorld : FCSTestWorld
{
	bool Init(FAutomationTestBase& Test, UCSMovementTuning* Tuning)
	{
		FCSTestWorldOptions Options;
		Options.bFloor = false;
		Options.bSettle = false;
		return FCSTestWorld::Init(Test, Tuning, Options);
	}
};

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
