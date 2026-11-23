#include "CSGameMode.h"

#include "CSCharacter.h"
#include "CSDevTools.h"
#include "CSMovementTuning.h"
#include "CombatSimulator.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

ACSGameMode::ACSGameMode()
{
	DefaultPawnClass = ACSCharacter::StaticClass();
	// The plain HUD: the feel overlay and the reload line draw through AHUD::OnHUDPostRender.
	HUDClass = AHUD::StaticClass();
	// Tick checks the players' pawns against RespawnKillZ.
	PrimaryActorTick.bCanEverTick = true;
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

	if (NewPlayer && NewPlayer->IsPlayerController())
	{
		PawnControllers.Add(Pawn, NewPlayer);
		Pawn->OnDestroyed.AddDynamic(this, &ACSGameMode::HandlePawnDestroyed);
	}
	if (UCSDevTools* DevTools = Character ? Character->GetDevTools() : nullptr)
	{
		DevTools->OnMapSwitchRequested.AddUObject(this, &ACSGameMode::HandleMapSwitchRequested);
	}
	return Pawn;
}

void ACSGameMode::BeginPlay()
{
	Super::BeginPlay();
	WorldTickStartHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ACSGameMode::HandleWorldTickStart);
}

void ACSGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FWorldDelegates::OnWorldTickStart.Remove(WorldTickStartHandle);
	WorldTickStartHandle.Reset();
	PendingRespawns.Reset();
	Super::EndPlay(EndPlayReason);
}

void ACSGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Pawn && !Pawn->IsActorBeingDestroyed() && Pawn->GetActorLocation().Z < RespawnKillZ)
		{
			// Destroying it respawns the player (HandlePawnDestroyed).
			UE_LOG(LogCombatSimulator, Log, TEXT("ACSGameMode: %s fell below %.0f cm; respawning"), *Pawn->GetName(), RespawnKillZ);
			Pawn->Destroy();
		}
	}
}

void ACSGameMode::HandlePawnDestroyed(AActor* DestroyedActor)
{
	TWeakObjectPtr<AController> Controller;
	PawnControllers.RemoveAndCopyValue(DestroyedActor, Controller);
	const UWorld* World = GetWorld();
	if (!Controller.IsValid() || !World || World->bIsTearingDown || IsActorBeingDestroyed())
	{
		return;
	}
	// At the start of the next frame (HandleWorldTickStart): the pawn is still being torn down now.
	PendingRespawns.AddUnique(Controller);
}

void ACSGameMode::HandleWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld() || PendingRespawns.IsEmpty())
	{
		return;
	}
	const TArray<TWeakObjectPtr<AController>> Respawns = MoveTemp(PendingRespawns);
	PendingRespawns.Reset();
	for (const TWeakObjectPtr<AController>& Controller : Respawns)
	{
		AController* C = Controller.Get();
		if (!C || C->GetPawn())
		{
			continue;
		}
		APlayerController* PC = Cast<APlayerController>(C);
		if (PC && !PlayerCanRestart(PC))
		{
			continue;
		}
		RestartPlayer(C);
		UE_LOG(LogCombatSimulator, Log, TEXT("ACSGameMode: respawned %s as %s"), *C->GetName(), *GetNameSafe(C->GetPawn()));
	}
}

FString ACSGameMode::GetMapSwitchTarget(const FString& CurrentMap) const
{
	if (DevSwitchMaps.IsEmpty())
	{
		return FString();
	}
	const FString Current = FPackageName::GetShortName(UWorld::RemovePIEPrefix(CurrentMap));
	for (int32 i = 0; i < DevSwitchMaps.Num(); ++i)
	{
		if (FPackageName::GetShortName(DevSwitchMaps[i]) == Current)
		{
			return DevSwitchMaps[(i + 1) % DevSwitchMaps.Num()];
		}
	}
	return DevSwitchMaps[0];
}

FString ACSGameMode::SwitchMap()
{
	const FString Target = GetMapSwitchTarget(GetWorld()->GetPackage()->GetName());
	if (Target.IsEmpty())
	{
		UE_LOG(LogCombatSimulator, Warning, TEXT("ACSGameMode: no DevSwitchMaps configured; the map switch does nothing"));
		return Target;
	}
	UE_LOG(LogCombatSimulator, Log, TEXT("ACSGameMode: switching map to %s"), *Target);
	LastMapSwitchTarget = Target;
	UGameplayStatics::OpenLevel(this, FName(*Target));
	return Target;
}

void ACSGameMode::HandleMapSwitchRequested()
{
	SwitchMap();
}
