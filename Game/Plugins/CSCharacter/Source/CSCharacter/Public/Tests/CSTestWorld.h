// The one test world for ACSCharacter automation tests, in the plugin and in the game module.
//
// A test world (no map) with a 200 m floor box, top at z = 0 at the origin (optionally tilted,
// rising toward +X), and one ACSCharacter spawned deferred with the given tuning, so its tuning is
// set before it applies it. Ticked by hand at a fixed step. A change to how a test character must
// be spawned is made here once; the test files wrap it (FMoveSim, FBodySim, FAnimSim, FCameraSim,
// FCSBareCharacterWorld) with their own helpers only.
//
// Test code only: it compiles to nothing without WITH_DEV_AUTOMATION_TESTS.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "CSCharacter.h"
#include "CSMovementTuning.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

struct FCSTestWorldOptions
{
	/** The floor's tilt, degrees, rising toward +X. */
	float FloorPitchDeg = 0.f;
	/** No floor at all (a character that is never ticked). */
	bool bFloor = true;
	FVector Spawn = FVector(0.f, 0.f, 120.f);
	/** Tick SettleSeconds after spawning, and fail unless the character then stands on the floor. */
	bool bSettle = true;
	float SettleSeconds = 0.5f;
};

struct FCSTestWorld
{
	FTestWorldWrapper World;
	ACSCharacter* Character = nullptr;
	APlayerController* Controller = nullptr;
	float Dt = 1.f / 60.f;
	float Time = 0.f;

	/** Tuning may be null: the character runs on the class defaults. */
	bool Init(FAutomationTestBase& Test, UCSMovementTuning* Tuning, const FCSTestWorldOptions& Options = FCSTestWorldOptions())
	{
		if (!World.CreateTestWorld(EWorldType::Game) || !World.BeginPlayInTestWorld())
		{
			World.ForwardErrorMessages(&Test);
			return false;
		}
		if (Options.bFloor)
		{
			AddBox(FVector(0.f, 0.f, -50.f), FVector(10000.f, 10000.f, 50.f), FRotator(Options.FloorPitchDeg, 0.f, 0.f));
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		const FTransform Spawn(Options.Spawn);
		Character = World.GetTestWorld()->SpawnActor<ACSCharacter>(ACSCharacter::StaticClass(), Spawn, Params);
		if (!Character)
		{
			Test.AddError(TEXT("Could not spawn ACSCharacter"));
			return false;
		}
		Character->Tuning = Tuning;
		Character->FinishSpawning(Spawn);
		// No controller in a test world; let the movement component simulate anyway. Possession
		// would normally pick the default movement mode (Restart); do it by hand.
		Character->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		Character->GetCharacterMovement()->SetDefaultMovementMode();
		if (!Options.bSettle)
		{
			return true;
		}
		Run(Options.SettleSeconds);
		const UCharacterMovementComponent* M = Character->GetCharacterMovement();
		if (!M->IsMovingOnGround())
		{
			Test.AddError(FString::Printf(TEXT("Character did not land on the test floor (at %s, mode %d, velocity %s, begun play %d, ticked %.2f s)"),
				*Character->GetActorLocation().ToString(), int32(M->MovementMode.GetValue()), *M->Velocity.ToString(), int32(Character->HasActorBegunPlay()), Time));
			return false;
		}
		return true;
	}

	~FCSTestWorld()
	{
		if (World.GetTestWorld())
		{
			World.DestroyTestWorld(false);
		}
	}

	/** A blocking box: Centre, half Extent, Rotation. */
	UBoxComponent* AddBox(const FVector& Centre, const FVector& Extent, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		UWorld* W = World.GetTestWorld();
		AActor* Actor = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		Box->SetWorldLocationAndRotation(Centre, Rotation);
		return Box;
	}

	/** A player controller possessing the character (Controller). False if it could not be spawned. */
	bool Possess()
	{
		Controller = World.GetTestWorld()->SpawnActor<APlayerController>();
		if (!Controller)
		{
			return false;
		}
		Controller->Possess(Character);
		return true;
	}

	/** One tick, with Input as movement input when it is not zero. */
	void Step(const FVector& Input = FVector::ZeroVector)
	{
		if (!Input.IsNearlyZero())
		{
			Character->AddMovementInput(Input, 1.f);
		}
		World.TickTestWorld(Dt);
		Time += Dt;
	}

	void Run(float Seconds, const FVector& Input = FVector::ZeroVector)
	{
		const int32 Steps = FMath::CeilToInt(Seconds / Dt);
		for (int32 i = 0; i < Steps; ++i)
		{
			Step(Input);
		}
	}

	/** Steps until Done returns true or Seconds pass. Returns whether Done became true. */
	template <typename F>
	bool RunUntil(float Seconds, F&& Done, const FVector& Input = FVector::ZeroVector)
	{
		const int32 Steps = FMath::CeilToInt(Seconds / Dt);
		for (int32 i = 0; i < Steps; ++i)
		{
			Step(Input);
			if (Done())
			{
				return true;
			}
		}
		return false;
	}

	float Speed2D() const { return Character->GetCharacterMovement()->Velocity.Size2D(); }
};

#endif // WITH_DEV_AUTOMATION_TESTS
