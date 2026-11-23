#include "CSGameMode.h"

#include "CSCharacter.h"
#include "CSMovementTuning.h"
#include "CombatSimulator.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"

ACSGameMode::ACSGameMode()
{
	DefaultPawnClass = ACSCharacter::StaticClass();
	// The plain HUD: the feel overlay and the reload line draw through AHUD::OnHUDPostRender.
	HUDClass = AHUD::StaticClass();
}

APawn* ACSGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	// As AGameModeBase does, but deferred, so the tuning is set before the character applies it
	// (PostInitializeComponents and BeginPlay run in FinishSpawning).
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient; // never save the spawned pawn into a map
	SpawnInfo.bDeferConstruction = true;
	APawn* Pawn = GetWorld()->SpawnActor<APawn>(PawnClass, SpawnTransform, SpawnInfo);
	if (!Pawn)
	{
		UE_LOG(LogCombatSimulator, Warning, TEXT("ACSGameMode: could not spawn %s at %s"), *GetNameSafe(PawnClass), *SpawnTransform.GetLocation().ToString());
		return nullptr;
	}

	ACSCharacter* Character = Cast<ACSCharacter>(Pawn);
	if (Character && !Character->Tuning && !CharacterTuning.IsNull())
	{
		Character->Tuning = CharacterTuning.LoadSynchronous();
		if (!Character->Tuning)
		{
			UE_LOG(LogCombatSimulator, Warning, TEXT("ACSGameMode: tuning %s not found; the character uses the class defaults"), *CharacterTuning.ToString());
		}
	}
	Pawn->FinishSpawning(SpawnTransform);
	return Pawn;
}
