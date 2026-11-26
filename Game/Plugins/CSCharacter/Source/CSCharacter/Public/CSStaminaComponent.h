#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CSStaminaComponent.generated.h"

class ACSCharacter;
class UCSMovementTuning;

DECLARE_MULTICAST_DELEGATE_OneParam(FCSStaminaExhausted, ACSCharacter* /* Who */);

/**
 * The stamina of a CS character (decision 0020). Strikes, dodges and hits taken on the guard spend
 * it; it recovers at StaminaRegen per second once StaminaRegenDelay has passed since the last spend.
 *
 * Spending more than is left takes it to zero and reports that it was not enough, so the strike
 * that asked is thrown tired (slower and weaker, UCSStrikeComponent). At zero the character is
 * exhausted: its guard drops (UCSGuardComponent) and stays down until stamina is back to
 * GuardReturnShare of the maximum.
 *
 * A run can switch it off (SetEnabled false: nothing is spent, it stays full) and scale the
 * recovery (SetRegenScale). Tuning (UCSMovementTuning, group Stamina): StaminaMax, StaminaRegen,
 * StaminaRegenDelay, TiredStrikeScale, BlockStaminaCost, DodgeStaminaCost, GuardReturnShare.
 */
UCLASS(ClassGroup = CS, meta = (BlueprintSpawnableComponent))
class CSCHARACTER_API UCSStaminaComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSStaminaComponent();

	UFUNCTION(BlueprintPure, Category = "CS|Stamina")
	float GetCurrent() const { return bEnabled ? Current : GetMax(); }

	UFUNCTION(BlueprintPure, Category = "CS|Stamina")
	float GetMax() const;

	/** 0 to 1. */
	UFUNCTION(BlueprintPure, Category = "CS|Stamina")
	float GetShare() const;

	/** Takes Cost (at most what is left). True when there was enough; false: it ran short. */
	bool Spend(float Cost);

	/** Enough is left for Cost. */
	bool CanAfford(float Cost) const { return !bEnabled || Current >= Cost; }

	/** Ran out, and not yet back to GuardReturnShare: the guard is down. */
	UFUNCTION(BlueprintPure, Category = "CS|Stamina")
	bool IsExhausted() const { return bEnabled && bExhausted; }

	/** Back to the maximum at once (a run starts, a reset). */
	void Refill();

	/** For play-tests and tests: sets the current value (clamped), as if it had been spent to there. */
	void SetCurrent(float Value);

	void SetEnabled(bool bInEnabled);
	bool IsEnabled() const { return bEnabled; }

	/** Times StaminaRegen; 0 stops the recovery. */
	void SetRegenScale(float Scale) { RegenScale = FMath::Max(0.f, Scale); }
	float GetRegenScale() const { return RegenScale; }

	/** Every time anyone runs out. */
	static FCSStaminaExhausted OnExhausted;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	const UCSMovementTuning& GetTuning() const;

	float Current = 100.f;
	double LastSpend = -1000.0;
	bool bExhausted = false;
	bool bEnabled = true;
	float RegenScale = 1.f;
};
