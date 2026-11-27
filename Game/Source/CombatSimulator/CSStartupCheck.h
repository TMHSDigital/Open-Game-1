#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CSStartupCheck.generated.h"

class APlayerController;

/**
 * The release's start check on a packaged build (tools/steam/release.ps1, gate 11).
 *
 * Idle unless the game is started with -CSSmokeTest=<result file>. Then, once the first player has a
 * pawn and SettleSeconds of game time have passed, it checks that what a cook rule could have lost
 * is there (Problems), writes "PASS" or "FAIL: <problems>" to the file, and quits with exit code 0
 * or 1. With no player pawn after TimeoutSeconds it fails the same way. It only reads state: it
 * ships in the public build.
 */
UCLASS()
class COMBATSIMULATOR_API UCSStartupCheck : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSStartupCheck();

	/**
	 * What is missing from the player: not an ACSCharacter, no skeletal mesh, no animation instance,
	 * no physics asset, no tuning, the mapping context not loaded or not added, no pause action.
	 * Empty when the player can play.
	 */
	static TArray<FString> Problems(const APlayerController* Controller);

	static constexpr float SettleSeconds = 2.f;
	static constexpr float TimeoutSeconds = 60.f;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void Finish(const TArray<FString>& Found);

	FString ResultPath;
	float Waited = 0.f;
	float SettledFor = 0.f;
};
