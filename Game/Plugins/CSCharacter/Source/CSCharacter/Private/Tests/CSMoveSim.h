// Shared test world for the CSCharacter automation tests (movement, dev tools, and later camera and body).
//
// A test world with a 200 m floor box (optionally tilted) and one ACSCharacter standing on it at
// the origin, ticked manually at a fixed step. No assets are loaded: tests pass a transient
// UCSMovementTuning, so they prove the code applies whatever values the asset holds.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "CSCharacter.h"
#include "CSMovementTuning.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/AutomationCommon.h"

namespace CSTestSupport
{
	// A test world with a 200 m floor box (optionally tilted by FloorPitchDeg, rising toward +X)
	// and one ACSCharacter standing on it at the origin. Ticked manually at a fixed step.
	struct FMoveSim
	{
		FTestWorldWrapper World;
		ACSCharacter* Character = nullptr;
		float Dt = 1.f / 60.f;
		float Time = 0.f;

		bool Init(FAutomationTestBase& Test, float InDt, UCSMovementTuning* Tuning, float FloorPitchDeg = 0.f)
		{
			Dt = InDt;
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
			// Top face at z = 0 at the origin.
			Box->SetWorldLocationAndRotation(FVector(0.f, 0.f, -50.f), FRotator(FloorPitchDeg, 0.f, 0.f));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.bDeferConstruction = true;
			const FTransform Spawn(FVector(0.f, 0.f, 120.f));
			Character = W->SpawnActor<ACSCharacter>(ACSCharacter::StaticClass(), Spawn, Params);
			if (!Character)
			{
				Test.AddError(TEXT("Could not spawn ACSCharacter"));
				return false;
			}
			Character->Tuning = Tuning;
			Character->FinishSpawning(Spawn);
			// No controller in a test world; let the movement component simulate anyway.
			// Possession would normally pick the default movement mode (Restart); do it by hand.
			Character->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			Character->GetCharacterMovement()->SetDefaultMovementMode();

			// Settle onto the floor.
			Run(FVector::ZeroVector, 0.5f);
			if (!Character->GetCharacterMovement()->IsMovingOnGround())
			{
				const UCharacterMovementComponent* M = Character->GetCharacterMovement();
				Test.AddError(FString::Printf(TEXT("Character did not land on the test floor (at %s, mode %d, velocity %s, begun play %d, ticked %.2f s)"),
					*Character->GetActorLocation().ToString(), int32(M->MovementMode.GetValue()), *M->Velocity.ToString(), int32(Character->HasActorBegunPlay()), Time));
				return false;
			}
			return true;
		}

		~FMoveSim()
		{
			if (World.GetTestWorld())
			{
				World.DestroyTestWorld(false);
			}
		}

		void Step(const FVector& Input)
		{
			if (!Input.IsNearlyZero())
			{
				Character->AddMovementInput(Input, 1.f);
			}
			World.TickTestWorld(Dt);
			Time += Dt;
		}

		void Run(const FVector& Input, float Seconds)
		{
			const int32 Steps = FMath::CeilToInt(Seconds / Dt);
			for (int32 i = 0; i < Steps; ++i)
			{
				Step(Input);
			}
		}

		float Speed2D() const { return Character->GetCharacterMovement()->Velocity.Size2D(); }
	};

	inline UCSMovementTuning* NewTuning()
	{
		return NewObject<UCSMovementTuning>(GetTransientPackage());
	}

}

#endif // WITH_DEV_AUTOMATION_TESTS
