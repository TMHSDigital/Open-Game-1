#include "CSBallLauncher.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CSCharacter.h"
#include "CSHitBall.h"
#include "CSMovementTuning.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ACSBallLauncher::ACSBallLauncher()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// A plain block to show where the balls come from.
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Body->SetStaticMesh(Cube.Object);
	}
	Body->SetCollisionProfileName(TEXT("BlockAll"));

	StandBox = CreateDefaultSubobject<UBoxComponent>(TEXT("StandBox"));
	StandBox->SetupAttachment(Root);
	StandBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	StandBox->SetGenerateOverlapEvents(true);
}

void ACSBallLauncher::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// The block sits behind the muzzle so a ball never starts inside it.
	Body->SetRelativeScale3D(FVector(0.6f, 0.6f, MuzzleHeight / 100.f + 0.3f));
	Body->SetRelativeLocation(FVector(-60.f, 0.f, 0.5f * (MuzzleHeight + 30.f)));
	StandBox->SetBoxExtent(FVector(StandHalfSize, StandHalfSize, 120.f));
	StandBox->SetRelativeLocation(FVector(StandDistance, 0.f, 120.f));
}

ACSCharacter* ACSBallLauncher::FindTarget() const
{
	TArray<AActor*> Overlapping;
	StandBox->GetOverlappingActors(Overlapping, ACSCharacter::StaticClass());
	for (AActor* Actor : Overlapping)
	{
		if (ACSCharacter* Character = Cast<ACSCharacter>(Actor))
		{
			return Character;
		}
	}
	return nullptr;
}

bool ACSBallLauncher::FireOnce()
{
	const ACSCharacter* Target = FindTarget();
	if (!Target)
	{
		return false;
	}
	const UCSMovementTuning& T = Target->GetActiveTuning();
	const FVector Muzzle = GetActorLocation() + FVector(0.f, 0.f, MuzzleHeight);
	// Aim up by what the ball falls on the way, so it arrives at muzzle height on the stand square
	// (at 20 m/s over 8 m it would otherwise drop about 80 cm and pass between the legs).
	const float Speed = FMath::Max(T.DevBallSpeed, 1.f);
	const float Gravity = GetWorld() ? FMath::Abs(GetWorld()->GetGravityZ()) : 980.f;
	const FVector Aim = GetActorForwardVector() * Speed + FVector(0.f, 0.f, 0.5f * Gravity * StandDistance / Speed);
	if (!ACSHitBall::Throw(GetWorld(), this, Muzzle, Aim, T.DevBallMass, Speed))
	{
		return false;
	}
	++FiredCount;
	return true;
}

void ACSBallLauncher::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SecondsSinceFire += DeltaSeconds;
	if (SecondsSinceFire >= IntervalSeconds && FireOnce())
	{
		SecondsSinceFire = 0.f;
	}
}
