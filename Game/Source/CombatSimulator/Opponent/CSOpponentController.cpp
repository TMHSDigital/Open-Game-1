#include "CSOpponentController.h"

#include "../CSTargetDummy.h"
#include "Components/SkeletalMeshComponent.h"
#include "CSCharacter.h"
#include "CSPhysicsBodyComponent.h"
#include "CSStrikeComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ACSOpponentController::ACSOpponentController()
{
	PrimaryActorTick.bCanEverTick = true;
	// The control rotation turns toward the focal point; the body follows it through the combat stance.
	bSetControlRotationFromPawnOrientation = false;
}

void ACSOpponentController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	Random.Initialize(InPawn ? int32(GetTypeHash(InPawn->GetName())) : 0);
	PatternIndex = 0;
	Enter(ECSOpponentState::Idle);
}

ACSCharacter* ACSOpponentController::FindOpponentTarget() const
{
	const APawn* Self = GetPawn();
	UWorld* World = GetWorld();
	if (!Self || !World)
	{
		return nullptr;
	}
	ACSCharacter* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (TActorIterator<ACSCharacter> It(World); It; ++It)
	{
		ACSCharacter* Other = *It;
		if (Other == Self || Other->IsActorBeingDestroyed() || !Other->IsPlayerControlled())
		{
			continue;
		}
		const double Distance = FVector::DistSquared2D(Other->GetActorLocation(), Self->GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Other;
		}
	}
	return Best;
}

void ACSOpponentController::Enter(ECSOpponentState NewState)
{
	OpponentState = NewState;
	StateSeconds = 0.f;
	if (NewState == ECSOpponentState::Circle)
	{
		CircleSign = Random.FRand() < 0.5f ? -1.f : 1.f;
		CircleFor = Random.FRandRange(CircleSecondsMin, FMath::Max(CircleSecondsMin, CircleSecondsMax));
	}
}

void ACSOpponentController::MoveAlong(ACSCharacter& Self, const FVector& Direction) const
{
	const FVector Flat = Direction.GetSafeNormal2D();
	if (!Flat.IsNearlyZero())
	{
		Self.AddMovementInput(Flat, 1.f);
	}
}

bool ACSOpponentController::Punch(ACSCharacter& Self)
{
	if (!Self.Attack())
	{
		return false;
	}
	--PunchesLeft;
	++PunchesThrown;
	return true;
}

void ACSOpponentController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	const ACSTargetDummy* Dummy = Cast<ACSTargetDummy>(Self);
	if (!Self)
	{
		return;
	}
	StateSeconds += DeltaSeconds;
	UCSStrikeComponent* Strike = Self->GetStrike();
	if (Strike && Dummy)
	{
		// The tell: this opponent's wind-up, whatever the punch tuning says.
		Strike->WindupSecondsOverride = Dummy->GetOpponentWindupSeconds();
	}

	ACSCharacter* Target = Dummy && Dummy->WantsToFight() ? FindOpponentTarget() : nullptr;
	const float Distance = Target ? float(FVector::Dist2D(Target->GetActorLocation(), Self->GetActorLocation())) : 0.f;
	if (!Target || Distance > FightRange)
	{
		ClearFocus(EAIFocusPriority::Gameplay);
		Enter(ECSOpponentState::Idle);
		return;
	}
	// The target's upper chest: the stance faces it and the punch's line of sight goes there.
	const USkeletalMeshComponent* TargetBody = Target->GetMesh();
	SetFocalPoint(TargetBody && TargetBody->GetBoneIndex(TEXT("spine_05")) != INDEX_NONE
		? TargetBody->GetSocketLocation(TEXT("spine_05")) : Target->GetActorLocation(), EAIFocusPriority::Gameplay);

	const UCSPhysicsBodyComponent* OwnBody = Self->GetPhysicsBody();
	const bool bSelfDown = Self->IsRagdolled()
		|| (OwnBody && (OwnBody->GetState() == ECSBodyState::Ragdoll || OwnBody->GetState() == ECSBodyState::Recovering));
	if (bSelfDown)
	{
		if (OpponentState != ECSOpponentState::Hurt)
		{
			Enter(ECSOpponentState::Hurt);
		}
		return;
	}
	if (OpponentState == ECSOpponentState::Hurt)
	{
		// Back on its feet: give the target a moment before coming in again.
		Enter(ECSOpponentState::BackOff);
	}

	const UCSPhysicsBodyComponent* TargetPhysics = Target->GetPhysicsBody();
	const bool bTargetDown = Target->IsRagdolled()
		|| (TargetPhysics && (TargetPhysics->GetState() == ECSBodyState::Ragdoll || TargetPhysics->GetState() == ECSBodyState::Recovering));
	const FVector Toward = (Target->GetActorLocation() - Self->GetActorLocation()).GetSafeNormal2D();
	const bool bStrikeBusy = Strike && Strike->GetPhase() != ECSStrikePhase::None;
	if (bTargetDown && !bStrikeBusy)
	{
		// Never on a target that is down: stand off and wait for it to get up.
		if (OpponentState != ECSOpponentState::Wait)
		{
			Enter(ECSOpponentState::Wait);
		}
		Self->SetGait(ECSGait::Walk);
		if (Distance < WaitDistance)
		{
			MoveAlong(*Self, -Toward);
		}
		return;
	}
	if (OpponentState == ECSOpponentState::Wait)
	{
		Enter(ECSOpponentState::Circle);
	}

	const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward) * CircleSign;
	switch (OpponentState)
	{
	case ECSOpponentState::Idle:
		Enter(ECSOpponentState::Close);
		[[fallthrough]];
	case ECSOpponentState::Close:
		if (Distance > AttackDistance)
		{
			Self->SetGait(Distance > JogDistance ? ECSGait::Jog : ECSGait::Walk);
			MoveAlong(*Self, Toward);
			break;
		}
		PunchesLeft = Pattern.IsEmpty() ? 1 : FMath::Max(1, Pattern[PatternIndex % Pattern.Num()]);
		++PatternIndex;
		Enter(ECSOpponentState::Attack);
		Punch(*Self);
		break;
	case ECSOpponentState::Attack:
		if (bStrikeBusy)
		{
			break;
		}
		if (PunchesLeft > 0 && Punch(*Self))
		{
			break;
		}
		Enter(ECSOpponentState::Recover);
		break;
	case ECSOpponentState::Recover:
		if (StateSeconds >= RecoverSeconds)
		{
			Enter(ECSOpponentState::BackOff);
		}
		break;
	case ECSOpponentState::BackOff:
		Self->SetGait(ECSGait::Walk);
		MoveAlong(*Self, -Toward);
		if (StateSeconds >= BackOffSeconds)
		{
			Enter(ECSOpponentState::Circle);
		}
		break;
	case ECSOpponentState::Circle:
	{
		Self->SetGait(ECSGait::Walk);
		// Around the target, drifting in or out to hold CircleDistance.
		const float Hold = FMath::Clamp((Distance - CircleDistance) / 60.f, -1.f, 1.f);
		MoveAlong(*Self, Side + Toward * Hold);
		if (StateSeconds >= CircleFor)
		{
			Enter(ECSOpponentState::Close);
		}
		break;
	}
	default:
		break;
	}
}
