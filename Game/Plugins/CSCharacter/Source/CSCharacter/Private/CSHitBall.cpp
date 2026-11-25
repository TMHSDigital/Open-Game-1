#include "CSHitBall.h"

#include "Materials/MaterialInstanceDynamic.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

FCSBallThrown ACSHitBall::OnThrown;

ACSHitBall::ACSHitBall()
{
	PrimaryActorTick.bCanEverTick = false;
	InitialLifeSpan = LifeSeconds;

	Ball = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ball"));
	SetRootComponent(Ball);
	// The engine's 100 cm sphere, scaled to the ball's size. An engine asset, so the plugin still
	// assumes no project content.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Ball->SetStaticMesh(Sphere.Object);
	}
	Ball->SetWorldScale3D(FVector(2.f * RadiusCm / 100.f));
	Ball->SetMobility(EComponentMobility::Movable);
	Ball->SetCollisionProfileName(TEXT("PhysicsActor"));
	// Through the movement capsule, onto the body inside it.
	Ball->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Ball->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Ball->SetSimulatePhysics(true);
	// Small and fast: without continuous collision it would pass through a limb between two frames.
	Ball->BodyInstance.bUseCCD = true;
	Ball->SetCanEverAffectNavigation(false);
}

ACSHitBall* ACSHitBall::Throw(UWorld* World, AActor* Thrower, const FVector& From, const FVector& Direction, float Mass, float Speed)
{
	const FVector Along = Direction.GetSafeNormal();
	if (!World || Along.IsNearlyZero())
	{
		return nullptr;
	}
	// Keep the count bounded: the oldest ball makes room.
	TArray<ACSHitBall*> Balls;
	for (TActorIterator<ACSHitBall> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed())
		{
			Balls.Add(*It);
		}
	}
	Balls.Sort([](const ACSHitBall& A, const ACSHitBall& B) { return A.CreationTime < B.CreationTime; });
	for (int32 Index = 0; Index + MaxBalls <= Balls.Num(); ++Index)
	{
		Balls[Index]->Destroy();
	}

	FActorSpawnParameters Params;
	Params.Owner = Thrower;
	Params.Instigator = Cast<APawn>(Thrower);
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	ACSHitBall* NewBall = World->SpawnActor<ACSHitBall>(ACSHitBall::StaticClass(), FTransform(Along.Rotation(), From), Params);
	if (!NewBall)
	{
		return nullptr;
	}
	NewBall->Ball->SetMassOverrideInKg(NAME_None, FMath::Max(Mass, 0.01f), true);
	NewBall->Ball->SetPhysicsLinearVelocity(Along * FMath::Max(Speed, 0.f));
	// Bright, so it can be followed in flight against the grey floor and sky. The engine's basic
	// shape material has a Color parameter; with another material this does nothing.
	if (UMaterialInstanceDynamic* Paint = NewBall->Ball->CreateDynamicMaterialInstance(0))
	{
		Paint->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.55f, 0.f));
	}
	OnThrown.Broadcast(NewBall, Thrower);
	return NewBall;
}

int32 ACSHitBall::RemoveAll(UWorld* World)
{
	int32 Removed = 0;
	if (World)
	{
		for (TActorIterator<ACSHitBall> It(World); It; ++It)
		{
			It->Destroy();
			++Removed;
		}
	}
	return Removed;
}
