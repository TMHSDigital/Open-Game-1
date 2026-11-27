#include "CSGameMode.h"

#include "CSCharacter.h"
#include "CSDevTools.h"
#include "CSHitBall.h"
#include "CSTargetDummy.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Feedback/CSHitFeedback.h"
#include "CSStartupCheck.h"
#include "Playtest/CSPlaytestRunner.h"
#include "Run/CSRunDirector.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "CSMovementTuning.h"
#include "CSPlayerController.h"
#include "CombatSimulator.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

ACSGameMode::ACSGameMode()
{
	DefaultPawnClass = ACSCharacter::StaticClass();
	// Pause, the pause screen and the player's look settings.
	PlayerControllerClass = ACSPlayerController::StaticClass();
	// The plain HUD: the feel overlay and the reload line draw through AHUD::OnHUDPostRender.
	HUDClass = AHUD::StaticClass();
	// Tick checks the players' pawns against RespawnKillZ.
	PrimaryActorTick.bCanEverTick = true;
	RunDirector = CreateDefaultSubobject<UCSRunDirector>(TEXT("RunDirector"));
	HitFeedback = CreateDefaultSubobject<UCSHitFeedback>(TEXT("HitFeedback"));
	PlaytestRunner = CreateDefaultSubobject<UCSPlaytestRunner>(TEXT("PlaytestRunner"));
	StartupCheck = CreateDefaultSubobject<UCSStartupCheck>(TEXT("StartupCheck"));
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
		DevTools->OnResetRequested.AddUObject(this, &ACSGameMode::HandleResetRequested);
	}
	if (Character && NewPlayer && NewPlayer->IsPlayerController())
	{
		// The dummies run on whatever the player runs on, from now and after every change.
		Character->OnTuningApplied.AddWeakLambda(this, [this, WeakCharacter = TWeakObjectPtr<ACSCharacter>(Character)]
		{
			SyncDummies(WeakCharacter.Get());
		});
		SyncDummies(Character);
	}
	return Pawn;
}

void ACSGameMode::BeginPlay()
{
	Super::BeginPlay();
	WorldTickStartHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ACSGameMode::HandleWorldTickStart);

	// Remember where the physics props start, for ResetPlayground.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(It->GetRootComponent());
		if (Root && Root->IsSimulatingPhysics() && !It->IsA<ACharacter>() && !It->IsA<ACSHitBall>())
		{
			PropHomes.Add(*It, It->GetActorTransform());
		}
	}
	// And the dummies the map was made with, for leaving a run.
	for (TActorIterator<ACSTargetDummy> It(GetWorld()); It; ++It)
	{
		FDummyRespawn Respawn;
		Respawn.Class = It->GetClass();
		Respawn.Home = It->GetActorTransform();
		Respawn.Tuning = It->Tuning;
		Respawn.Tags = It->Tags;
		Respawn.Tint = It->Tint;
		MapDummies.Add(MoveTemp(Respawn));
	}
	PlaygroundDummies = MapDummies;
}

void ACSGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FWorldDelegates::OnWorldTickStart.Remove(WorldTickStartHandle);
	WorldTickStartHandle.Reset();
	PendingRespawns.Reset();
	PendingDummies.Reset();
	MapDummies.Reset();
	PlaygroundDummies.Reset();
	PropHomes.Reset();
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
	if (World != GetWorld() || (PendingRespawns.IsEmpty() && PendingDummies.IsEmpty()))
	{
		return;
	}
	// Dummies first, so the players' tuning reaches them when the players spawn below.
	const TArray<FDummyRespawn> Dummies = MoveTemp(PendingDummies);
	PendingDummies.Reset();
	for (const FDummyRespawn& Respawn : Dummies)
	{
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnInfo.bDeferConstruction = true;
		ACSTargetDummy* Dummy = World->SpawnActor<ACSTargetDummy>(Respawn.Class, Respawn.Home, SpawnInfo);
		if (!Dummy)
		{
			UE_LOG(LogCombatSimulator, Warning, TEXT("ACSGameMode: could not put a dummy back at %s"), *Respawn.Home.GetLocation().ToString());
			continue;
		}
		Dummy->Tuning = Respawn.Tuning.Get();
		Dummy->Tags = Respawn.Tags;
		Dummy->Tint = Respawn.Tint;
		Dummy->FinishSpawning(Respawn.Home);
	}
	if (!Dummies.IsEmpty())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			SyncDummies(Cast<ACSCharacter>(PC->GetPawn()));
		}
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

void ACSGameMode::SyncDummies(const ACSCharacter* Player)
{
	UWorld* World = GetWorld();
	if (!Player || !World)
	{
		return;
	}
	for (TActorIterator<ACSTargetDummy> It(World); It; ++It)
	{
		if (!It->HasActorBegunPlay() || It->IsActorBeingDestroyed())
		{
			continue;
		}
		FString Reason;
		if (!It->TryApplyTuning(Player->GetTuningOverrides(), Reason))
		{
			UE_LOG(LogCombatSimulator, Warning, TEXT("ACSGameMode: %s could not take the player's tuning changes: %s"), *It->GetName(), *Reason);
		}
	}
}

void ACSGameMode::ResetPlayground()
{
	ResetPlaygroundInternal(nullptr);
}

void ACSGameMode::ResetPlaygroundWithDummies(const TArray<FTransform>& Spots)
{
	TArray<FDummyRespawn> Replacement;
	for (const FTransform& Spot : Spots)
	{
		FDummyRespawn Respawn;
		Respawn.Class = ACSTargetDummy::StaticClass();
		Respawn.Home = Spot;
		Respawn.Tags = {FName(TEXT("Dummy")), FName(TEXT("RunDummy"))};
		Replacement.Add(MoveTemp(Respawn));
	}
	ResetPlaygroundInternal(&Replacement);
}

void ACSGameMode::ResetPlaygroundToMap()
{
	const TArray<FDummyRespawn> Replacement = MapDummies;
	ResetPlaygroundInternal(&Replacement);
}

TArray<FString> ACSGameMode::GetRunMapNames() const
{
	TArray<FString> Names;
	for (const FString& Map : RunMaps)
	{
		Names.Add(FPackageName::GetShortName(Map));
	}
	return Names;
}

FString ACSGameMode::FindRunMap(const FString& ShortName) const
{
	for (const FString& Map : RunMaps)
	{
		if (FPackageName::GetShortName(Map) == ShortName)
		{
			return Map;
		}
	}
	return FString();
}

FTransform ACSGameMode::GetRunStart()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (const AActor* Start = PC ? FindPlayerStart(PC) : nullptr)
	{
		return Start->GetActorTransform();
	}
	if (World)
	{
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			return It->GetActorTransform();
		}
	}
	if (const APawn* Pawn = PC ? PC->GetPawn() : nullptr)
	{
		return Pawn->GetActorTransform();
	}
	return FTransform(FVector(0.f, 0.f, 100.f));
}

TArray<FTransform> ACSGameMode::PutOnFloor(const TArray<FTransform>& Spots, bool bCharacters) const
{
	const UWorld* World = GetWorld();
	const float HalfHeight = GetDefault<ACSCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	TArray<FTransform> Out;
	for (const FTransform& Spot : Spots)
	{
		FVector Location = Spot.GetLocation();
		FHitResult Hit;
		// Static geometry only: a prop, a ball or a character under the spot is not the floor.
		if (World && World->LineTraceSingleByObjectType(Hit, Location + FVector(0.f, 0.f, 300.f), Location - FVector(0.f, 0.f, 3000.f),
			FCollisionObjectQueryParams(ECC_WorldStatic)))
		{
			Location.Z = Hit.ImpactPoint.Z + (bCharacters ? HalfHeight + 2.f : 0.f);
		}
		else if (!bCharacters)
		{
			// The spot's height is a standing character's centre; a launcher's origin is at its feet.
			Location.Z -= HalfHeight;
		}
		Out.Add(FTransform(Spot.GetRotation(), Location));
	}
	return Out;
}

void ACSGameMode::ResetPlaygroundInternal(const TArray<FDummyRespawn>* Replacement)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const int32 Balls = ACSHitBall::RemoveAll(World);

	int32 Props = 0;
	for (const TPair<TWeakObjectPtr<AActor>, FTransform>& Home : PropHomes)
	{
		AActor* Prop = Home.Key.Get();
		UPrimitiveComponent* Root = Prop ? Cast<UPrimitiveComponent>(Prop->GetRootComponent()) : nullptr;
		if (!Root)
		{
			continue;
		}
		Prop->SetActorTransform(Home.Value, false, nullptr, ETeleportType::ResetPhysics);
		Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Root->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		++Props;
	}

	// A dummy may be mid-ragdoll or half way up: a fresh one on its spot is simpler and always right.
	TArray<ACSTargetDummy*> Dummies;
	for (TActorIterator<ACSTargetDummy> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed())
		{
			Dummies.Add(*It);
		}
	}
	if (Replacement)
	{
		PlaygroundDummies = *Replacement;
	}
	// Whatever an earlier reset in this frame still had to bring back is replaced too.
	PendingDummies = PlaygroundDummies;
	for (ACSTargetDummy* Dummy : Dummies)
	{
		Dummy->Destroy();
	}

	// Destroying a player's character respawns the player at a PlayerStart (HandlePawnDestroyed).
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (APawn* Pawn = PC ? PC->GetPawn() : nullptr)
		{
			Pawn->Destroy();
		}
	}
	UE_LOG(LogCombatSimulator, Log, TEXT("ACSGameMode: reset: %d prop(s) put back, %d dummy(ies) removed and %d to come, %d ball(s) removed"), Props, Dummies.Num(), PendingDummies.Num(), Balls);
}

void ACSGameMode::HandleResetRequested()
{
	// With a run on or over, putting things back means starting that run again.
	if (RunDirector && RunDirector->GetState() != ECSRunState::Idle && RunDirector->Rerun())
	{
		return;
	}
	ResetPlayground();
}

void ACSGameMode::HandleMapSwitchRequested()
{
	SwitchMap();
}
