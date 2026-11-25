#include "CSRunDirector.h"

#include "../CSGameMode.h"
#include "../CSPlayerController.h"
#include "../CombatSimulator.h"
#include "CSBallLauncher.h"
#include "CSCharacter.h"
#include "CSHitBall.h"
#include "CSRunStore.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

UCSRunDirector::UCSRunDirector()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Run time is game time: the pause screen stops it.
	PrimaryComponentTick.bTickEvenWhenPaused = false;
}

ACSGameMode* UCSRunDirector::GetMode() const
{
	return Cast<ACSGameMode>(GetOwner());
}

ACSCharacter* UCSRunDirector::GetPlayerCharacter() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? Cast<ACSCharacter>(PC->GetPawn()) : nullptr;
}

void UCSRunDirector::BeginPlay()
{
	Super::BeginPlay();
	ContactHandle = UCSPhysicsBodyComponent::OnAnyContact.AddUObject(this, &UCSRunDirector::HandleContact);
	ThrowHandle = ACSHitBall::OnThrown.AddUObject(this, &UCSRunDirector::HandleThrow);
	HudHandle = AHUD::OnHUDPostRender.AddUObject(this, &UCSRunDirector::DrawHud);
}

void UCSRunDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UCSPhysicsBodyComponent::OnAnyContact.Remove(ContactHandle);
	ACSHitBall::OnThrown.Remove(ThrowHandle);
	AHUD::OnHUDPostRender.Remove(HudHandle);
	ContactHandle.Reset();
	ThrowHandle.Reset();
	HudHandle.Reset();
	Launchers.Reset();
	Super::EndPlay(EndPlayReason);
}

void UCSRunDirector::SetState(ECSRunState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		OnStateChanged.Broadcast(NewState);
	}
}

bool UCSRunDirector::StartRun(const FCSRunSetup& InSetup, FString& OutReason)
{
	UWorld* World = GetWorld();
	ACSGameMode* Mode = GetMode();
	if (!World || !Mode)
	{
		OutReason = TEXT("there is no game to run it in");
		return false;
	}
	const FString Current = FPackageName::GetShortName(UWorld::RemovePIEPrefix(World->GetPackage()->GetName()));
	if (InSetup.Map.IsEmpty() || InSetup.Map == Current)
	{
		FCSRunSetup Here = InSetup;
		Here.Map = Current;
		BeginRunHere(Here);
		return true;
	}
	const FString Package = Mode->FindRunMap(InSetup.Map);
	if (Package.IsEmpty())
	{
		OutReason = FString::Printf(TEXT("%s is not one of the game's run maps"), *InSetup.Map);
		return false;
	}
	UCSRunStore* Store = UCSRunStore::Get(this);
	if (!Store)
	{
		OutReason = TEXT("the run could not be carried to another map");
		return false;
	}
	UE_LOG(LogCombatSimulator, Log, TEXT("UCSRunDirector: opening %s for the run"), *Package);
	Store->SetPending(InSetup);
	UGameplayStatics::OpenLevel(this, FName(*Package));
	return true;
}

void UCSRunDirector::BeginRunHere(const FCSRunSetup& InSetup)
{
	ACSGameMode* Mode = GetMode();
	UWorld* World = GetWorld();
	Setup = InSetup;
	bHasSetup = true;
	bCheckPending = false;
	Stats = FCSRunStats();
	EndReason = ECSRunEnd::None;

	const FTransform Start = Mode->GetRunStart();
	RemoveLaunchers();
	// Props back, balls gone, the player at the start, and the setup's dummies in place of the map's.
	Mode->ResetPlaygroundWithDummies(Mode->PutOnFloor(Setup.DummySpots(Start), true));

	const float StandDistance = GetDefault<ACSBallLauncher>()->StandDistance;
	for (const FTransform& Spot : Mode->PutOnFloor(Setup.LauncherSpots(Start, StandDistance), false))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		if (ACSBallLauncher* Launcher = World->SpawnActor<ACSBallLauncher>(ACSBallLauncher::StaticClass(), Spot, Params))
		{
			Launcher->Tags.Add(TEXT("RunLauncher"));
			Launchers.Add(Launcher);
		}
	}
	UE_LOG(LogCombatSimulator, Log, TEXT("UCSRunDirector: run started: %s"), *Setup.Describe());
	// Leaving Ended for Running again must be announced too, so go through Idle when restarting.
	State = ECSRunState::Idle;
	SetState(ECSRunState::Running);
}

void UCSRunDirector::EndRun(ECSRunEnd Reason)
{
	if (State != ECSRunState::Running)
	{
		return;
	}
	EndReason = Reason;
	UE_LOG(LogCombatSimulator, Log, TEXT("UCSRunDirector: run over (%s) after %s: %d dealt, %d taken"),
		*FCSRunStats::DescribeEnd(Reason), *FCSRunStats::FormatTime(Stats.Seconds), Stats.Dealt.Hits, Stats.Taken.Hits);
	SetState(ECSRunState::Ended);
	OpenResults();
}

bool UCSRunDirector::Rerun()
{
	if (!bHasSetup || State == ECSRunState::Idle || !GetMode() || !GetWorld())
	{
		return false;
	}
	BeginRunHere(Setup);
	return true;
}

void UCSRunDirector::LeaveRun()
{
	if (State == ECSRunState::Idle)
	{
		return;
	}
	RemoveLaunchers();
	if (ACSGameMode* Mode = GetMode())
	{
		Mode->ResetPlaygroundToMap();
	}
	EndReason = ECSRunEnd::None;
	UE_LOG(LogCombatSimulator, Log, TEXT("UCSRunDirector: run left; the map is as it was made"));
	SetState(ECSRunState::Idle);
}

void UCSRunDirector::RemoveLaunchers()
{
	for (ACSBallLauncher* Launcher : Launchers)
	{
		if (Launcher && !Launcher->IsActorBeingDestroyed())
		{
			Launcher->Destroy();
		}
	}
	Launchers.Reset();
}

void UCSRunDirector::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bCheckPending)
	{
		bCheckPending = false;
		FCSRunSetup Carried;
		UCSRunStore* Store = UCSRunStore::Get(this);
		if (Store && Store->TakePending(Carried))
		{
			FString Reason;
			// The map was opened for this run: whatever its name says, it happens here.
			Carried.Map.Reset();
			if (!StartRun(Carried, Reason))
			{
				UE_LOG(LogCombatSimulator, Warning, TEXT("UCSRunDirector: the run carried to this map did not start: %s"), *Reason);
			}
		}
		return;
	}
	if (State != ECSRunState::Running)
	{
		return;
	}
	Stats.Seconds += DeltaTime;
	if (Setup.KnockdownGoal > 0 && Stats.Dealt.Knockdowns >= Setup.KnockdownGoal)
	{
		EndRun(ECSRunEnd::KnockdownGoal);
	}
	else if (Setup.TimeLimit > 0.f && Stats.Seconds >= Setup.TimeLimit)
	{
		Stats.Seconds = Setup.TimeLimit;
		EndRun(ECSRunEnd::TimeLimit);
	}
}

ECSRunHitSide UCSRunDirector::Classify(const ACSCharacter* Victim, const AActor* Other) const
{
	const ACSCharacter* Player = GetPlayerCharacter();
	if (!Player || !Victim)
	{
		return ECSRunHitSide::Other;
	}
	if (Victim == Player)
	{
		return ECSRunHitSide::Taken;
	}
	// The player's own body, or anything it threw (a ball's Owner is its thrower).
	for (const AActor* Actor = Other; Actor; Actor = Actor->GetOwner())
	{
		if (Actor == Player)
		{
			return ECSRunHitSide::Dealt;
		}
	}
	return ECSRunHitSide::Other;
}

void UCSRunDirector::HandleContact(ACSCharacter* Victim, AActor* Other, const FCSContactRecord& Hit)
{
	if (State != ECSRunState::Running || !Victim || Victim->GetWorld() != GetWorld())
	{
		return;
	}
	Stats.RecordHit(Classify(Victim, Other), Hit);
}

void UCSRunDirector::HandleThrow(ACSHitBall* Ball, AActor* Thrower)
{
	if (State != ECSRunState::Running || !Ball || Ball->GetWorld() != GetWorld())
	{
		return;
	}
	if (Thrower && Thrower == GetPlayerCharacter())
	{
		Stats.RecordThrow();
	}
}

FString UCSRunDirector::GetStatusText() const
{
	switch (State)
	{
	case ECSRunState::Running:
		return TEXT("Run on: ") + Setup.Describe();
	case ECSRunState::Ended:
		return FString::Printf(TEXT("Run over (%s): %s"), *FCSRunStats::DescribeEnd(EndReason), *Setup.Describe());
	default:
		return TEXT("No run: free play on the map as it was made.");
	}
}

void UCSRunDirector::OpenResults()
{
	const UWorld* World = GetWorld();
	if (ACSPlayerController* PC = World ? Cast<ACSPlayerController>(World->GetFirstPlayerController()) : nullptr)
	{
		PC->OpenPauseScreenOnTab(ECSPauseTab::Run);
	}
}

void UCSRunDirector::DrawHud(AHUD* HUD, UCanvas* Canvas)
{
	if (State != ECSRunState::Running || !HUD || !Canvas || !GEngine || HUD->GetWorld() != GetWorld())
	{
		return;
	}
	// The pause screen's Run tab shows the same numbers; the line would lie over it.
	if (GetWorld()->IsPaused())
	{
		return;
	}
	const UFont* Font = GEngine->GetMediumFont();
	const FString Line = Stats.HudLine(Setup.TimeLimit, Setup.KnockdownGoal);
	float Width = 0.f;
	float Height = 0.f;
	Canvas->TextSize(Font, Line, Width, Height);
	const float X = FMath::Max(8.f, (Canvas->ClipX - Width) * 0.5f);
	const float Y = 24.f;
	Canvas->SetDrawColor(FColor::Black);
	Canvas->DrawText(Font, Line, X + 1.f, Y + 1.f);
	Canvas->SetDrawColor(FColor::White);
	Canvas->DrawText(Font, Line, X, Y);
}
