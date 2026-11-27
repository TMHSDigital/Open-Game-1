#pragma once

#include "CoreMinimal.h"
#include "CSPhysicsBodyComponent.h"
#include "Components/ActorComponent.h"
#include "CSHitFeedback.generated.h"

class ACSCharacter;
class AHUD;
class UCanvas;
class UMaterialInstanceDynamic;
class USoundBase;

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
 * - Hit-stop: a punch the player lands freezes the world (real time), longer for a bigger hit:
 *   from HitStopSeconds up to HitStopSecondsMax at HitReferenceSize.
 * - Sound: every hit between characters is heard where it landed: a heavy punch from
 *   HeavyHitSize up, a lighter one below, a soft one on the guard; louder for a bigger hit
 *   (CC0 Kenney Impact Sounds, decision 0025).
 * - Shake: a hit the player deals or takes jolts the camera, by the hit's size (UCSHitCameraShake).
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

	/** The hit-stop of a hit at HitReferenceSize or bigger, real seconds. */
	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "0", Units = "s"))
	float HitStopSecondsMax = 0.12f;

	/** A hit this size (velocity change, cm/s) or bigger gets the full hit-stop, shake and loudness. */
	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "1"))
	float HitReferenceSize = 350.f;

	/** From this size up a hit sounds heavy. */
	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "0"))
	float HeavyHitSize = 180.f;

	/** Below this size a contact makes no sound (bodies brushing). */
	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "0"))
	float MinSoundSize = 25.f;

	/** Camera shake at full size for a hit the player deals, and for one it takes. 0: none. */
	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "0"))
	float DealtShakeScale = 0.6f;
	UPROPERTY(EditAnywhere, Category = "CS|Feedback", meta = (ClampMin = "0"))
	float TakenShakeScale = 1.2f;

	/** The sounds: hits landed heavy and medium, and hits on the guard. */
	UPROPERTY(EditAnywhere, Category = "CS|Feedback")
	TArray<TSoftObjectPtr<USoundBase>> HeavyHitSounds;
	UPROPERTY(EditAnywhere, Category = "CS|Feedback")
	TArray<TSoftObjectPtr<USoundBase>> HitSounds;
	UPROPERTY(EditAnywhere, Category = "CS|Feedback")
	TArray<TSoftObjectPtr<USoundBase>> BlockedSounds;

	/** The sound a hit of this size and result plays (null: none), and how loud. */
	USoundBase* PickSound(ECSContactResult Result, float Size, float& OutVolume) const;

	/** The hit-stop for a hit of this size, real seconds. */
	float HitStopFor(float Size) const;

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
	/**
	 * Stamina bars: the player's under the middle of the screen, and one over the head of each
	 * character that fights back. Each is drawn while it is not full or the player is in the
	 * combat stance; red while exhausted (the guard is down).
	 */
	void DrawStamina(UCanvas* Canvas);

	TMap<TWeakObjectPtr<ACSCharacter>, FFlash> Flashes;
	TArray<FCSHitMark> Marks;
	/** Real time the hit-stop ends; 0 when none. */
	double HitStopUntil = 0.0;
	/** Size of the last hit the player dealt, for the hit-stop that follows it. */
	float LastDealtSize = 0.f;
	void Shake(float Size, float Scale);
	float DilationBefore = 1.f;

	FDelegateHandle ContactHandle;
	FDelegateHandle StrikeHandle;
	FDelegateHandle HudHandle;
};
