#include "CSStaminaComponent.h"

#include "CSCharacter.h"
#include "CSMovementTuning.h"
#include "Engine/World.h"

FCSStaminaExhausted UCSStaminaComponent::OnExhausted;

UCSStaminaComponent::UCSStaminaComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

const UCSMovementTuning& UCSStaminaComponent::GetTuning() const
{
	const ACSCharacter* Character = Cast<ACSCharacter>(GetOwner());
	return Character ? Character->GetActiveTuning() : *GetDefault<UCSMovementTuning>();
}

float UCSStaminaComponent::GetMax() const
{
	return FMath::Max(1.f, GetTuning().StaminaMax);
}

float UCSStaminaComponent::GetShare() const
{
	return FMath::Clamp(GetCurrent() / GetMax(), 0.f, 1.f);
}

void UCSStaminaComponent::BeginPlay()
{
	Super::BeginPlay();
	Refill();
}

void UCSStaminaComponent::Refill()
{
	Current = GetMax();
	bExhausted = false;
}

void UCSStaminaComponent::SetCurrent(float Value)
{
	Current = FMath::Clamp(Value, 0.f, GetMax());
	if (const UWorld* World = GetWorld())
	{
		LastSpend = World->GetTimeSeconds();
	}
	if (Current <= 0.f && !bExhausted)
	{
		bExhausted = true;
		OnExhausted.Broadcast(Cast<ACSCharacter>(GetOwner()));
	}
}

void UCSStaminaComponent::SetEnabled(bool bInEnabled)
{
	bEnabled = bInEnabled;
	Refill();
}

bool UCSStaminaComponent::Spend(float Cost)
{
	if (!bEnabled || Cost <= 0.f)
	{
		return true;
	}
	const bool bEnough = Current >= Cost;
	SetCurrent(Current - Cost);
	return bEnough;
}

void UCSStaminaComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorld* World = GetWorld();
	if (!bEnabled || !World)
	{
		return;
	}
	const UCSMovementTuning& T = GetTuning();
	const float Max = GetMax();
	if (Current < Max && World->GetTimeSeconds() - LastSpend >= T.StaminaRegenDelay)
	{
		Current = FMath::Min(Max, Current + T.StaminaRegen * RegenScale * DeltaTime);
	}
	// Back up only at a share of the maximum: a guard that came back at the first point would drop
	// again at the next hit.
	if (bExhausted && Current >= Max * FMath::Clamp(T.GuardReturnShare, 0.f, 1.f))
	{
		bExhausted = false;
	}
}
