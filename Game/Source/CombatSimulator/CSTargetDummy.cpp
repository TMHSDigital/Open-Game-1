#include "CSTargetDummy.h"

#include "CSGameMode.h"
#include "CSMovementTuning.h"
#include "Opponent/CSOpponentController.h"
#include "Run/CSRunDirector.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

ACSTargetDummy::ACSTargetDummy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Nothing drives a dummy that stands still. One that fights back gets its controller when it
	// starts to (EnsureController), so a dummy that never fights stays exactly the test bed it was.
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = ACSOpponentController::StaticClass();
	// Without a controller the movement component would not run at all: no gravity, no stagger shove.
	GetCharacterMovement()->bRunPhysicsWithNoController = true;
}

void ACSTargetDummy::BeginPlay()
{
	// Before Super: the character applies its tuning in BeginPlay.
	if (!Tuning)
	{
		if (const ACSGameMode* Mode = GetWorld() ? Cast<ACSGameMode>(GetWorld()->GetAuthGameMode()) : nullptr)
		{
			Tuning = Mode->CharacterTuning.LoadSynchronous();
		}
	}
	Super::BeginPlay();
	HomeTransform = GetActorTransform();
	// A run's dummy is spawned once the run is set up: if the run fights back, it needs its controller.
	const UCSRunDirector* Director = GetRunDirector();
	if (bFightsBack || (Director && Director->GetSetup().Opponents == ECSRunOpponents::FightBack))
	{
		EnsureController();
	}
	// Several dummies on the same idle in perfect step look like copies: each plays a little
	// faster or slower, so they drift apart within a few seconds.
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->GlobalAnimRateScale = FMath::FRandRange(0.9f, 1.1f);
	}
	// Possession would pick the movement mode; do it by hand.
	GetCharacterMovement()->SetDefaultMovementMode();

	// Tint every material that has a colour parameter; one that has none is left as it is.
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		for (int32 Index = 0; Index < Body->GetNumMaterials(); ++Index)
		{
			if (UMaterialInstanceDynamic* Material = Body->CreateDynamicMaterialInstance(Index))
			{
				// DiffuseColor is the parameter of the imported base character material; the others
				// cover a material made by hand later.
				Material->SetVectorParameterValue(TEXT("DiffuseColor"), Tint);
				Material->SetVectorParameterValue(TEXT("BaseColor"), Tint);
				Material->SetVectorParameterValue(TEXT("Tint"), Tint);
			}
		}
	}
}

const UCSRunDirector* ACSTargetDummy::GetRunDirector() const
{
	const UWorld* World = GetWorld();
	const ACSGameMode* Mode = World ? Cast<ACSGameMode>(World->GetAuthGameMode()) : nullptr;
	return Mode && ActorHasTag(TEXT("RunDummy")) ? Mode->GetRunDirector() : nullptr;
}

void ACSTargetDummy::EnsureController()
{
	if (!GetController() && HasActorBegunPlay())
	{
		SpawnDefaultController();
	}
}

void ACSTargetDummy::SetFightsBack(bool bInFightsBack)
{
	bFightsBack = bInFightsBack;
	if (bFightsBack)
	{
		EnsureController();
	}
}

bool ACSTargetDummy::WantsToFight() const
{
	if (bFightsBack)
	{
		return true;
	}
	const UCSRunDirector* Director = GetRunDirector();
	return Director && Director->GetState() == ECSRunState::Running && Director->GetSetup().Opponents == ECSRunOpponents::FightBack;
}

float ACSTargetDummy::GetOpponentWindupSeconds() const
{
	const UCSRunDirector* Director = GetRunDirector();
	return Director && Director->GetSetup().Opponents == ECSRunOpponents::FightBack ? Director->GetSetup().OpponentWindup : OpponentWindupSeconds;
}
