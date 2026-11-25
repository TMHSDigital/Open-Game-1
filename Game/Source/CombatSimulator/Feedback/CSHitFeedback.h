#pragma once

#include "CoreMinimal.h"
#include "CSPhysicsBodyComponent.h"
#include "Components/ActorComponent.h"
#include "CSHitFeedback.generated.h"

class ACSCharacter;
class AHUD;
class UCanvas;
class UMaterialInstanceDynamic;

/** One mark on screen where a hit landed. */
struct FCSHitMark
{
	FVector Point = FVector::ZeroVector;
	/** The velocity change the hit stood for, cm/s. */
	float Size = 0.f;
	ECSContactResult Result = ECSContactResult::None;
	/** The player dealt it (true) or took it (false); hits between others are not marked. */
	bool bDealt = false;
	/** World time it appeared (real seconds, so a hit-stop does not hold it). */
	double Time = 0.0;
};

/**
 * Makes hits visible: a component of ACSGameMode that listens to every contact hit and every
 * punch that lands, and knows nothing about what threw them.
 *
 * - Flash: a struck character's body turns toward FlashColor and back over FlashSeconds (through a
 *   colour parameter of its material; a material without one is left alone).
 * - Mark: a hit the player deals or takes is marked on screen where it landed, with its size and
 *   what it did, for MarkSeconds.
 * - Hit-stop: a punch the player lands freezes the world for HitStopSeconds (real time), so the
 *   hit is felt.
 *
 * No sound or particles: those wait for assets (decision 0018).
 */
UCLASS()
class COMBATSIMULATOR_API UCSHitFeedback : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSHitFeedback();

	UPROPERTY(EditAnywhere, Category = "CS|Feedback")
	FLinearColor FlashColor = FLinearColor(1.f, 0.12f, 0.06f);

	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "0", Units = "s"))
	float FlashSeconds = 0.2f;

	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "0", Units = "s"))
	float MarkSeconds = 0.7f;

	/** Real seconds the world stands still when the player lands a punch. 0: no hit-stop. */
	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "0", Units = "s"))
	float HitStopSeconds = 0.06f;

	/** Time dilation during the hit-stop. */
	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "0.0001", ClampMax = "1"))
	float HitStopDilation = 0.05f;

	/** The marks on screen now. */
	const TArray<FCSHitMark>& GetMarks() const { return Marks; }

	/** A hit-stop is holding the world now. */
	bool IsHitStopActive() const { return HitStopUntil > 0.0; }

	/** How far a character's flash has come back, 1 at the hit, 0 when over or never hit. */
	float GetFlashWeight(const ACSCharacter* Character) const;

	/** One line for a mark: "153 stagger", "620 knockdown". */
	static FString DescribeMark(const FCSHitMark& Mark);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FFlash
	{
		double Time = 0.0;
		TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
		/** Each material's colour before the flash, per parameter it had. */
		TArray<TArray<TPair<FName, FLinearColor>>> Original;
	};

	ACSCharacter* GetPlayer() const;
	void HandleContact(ACSCharacter* Victim, AActor* Other, const FCSContactRecord& Hit);
	void HandleStrikeLanded(ACSCharacter* Attacker, AActor* Victim, ECSContactResult Result);
	void StartFlash(ACSCharacter* Victim);
	void UpdateFlashes(double Now);
	void EndHitStop();
	void DrawMarks(AHUD* HUD, UCanvas* Canvas);

	TMap<TWeakObjectPtr<ACSCharacter>, FFlash> Flashes;
	TArray<FCSHitMark> Marks;
	/** Real time the hit-stop ends; 0 when none. */
	double HitStopUntil = 0.0;
	float DilationBefore = 1.f;

	FDelegateHandle ContactHandle;
	FDelegateHandle StrikeHandle;
	FDelegateHandle HudHandle;
};
