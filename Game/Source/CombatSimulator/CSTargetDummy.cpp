#include "CSTargetDummy.h"

#include "CSGameMode.h"
#include "CSMovementTuning.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

ACSTargetDummy::ACSTargetDummy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Nothing drives a dummy: no player and no AI controller.
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;
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
