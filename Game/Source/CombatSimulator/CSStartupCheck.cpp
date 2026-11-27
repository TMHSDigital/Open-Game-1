#include "CSStartupCheck.h"

#include "CombatSimulator.h"
#include "Components/SkeletalMeshComponent.h"
#include "CSCharacter.h"
#include "CSPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "HAL/PlatformMisc.h"
#include "InputMappingContext.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/PhysicsAsset.h"

UCSStartupCheck::UCSStartupCheck()
{
	PrimaryComponentTick.bCanEverTick = true;
}

TArray<FString> UCSStartupCheck::Problems(const APlayerController* Controller)
{
	TArray<FString> Out;
	const ACSCharacter* Character = Controller ? Cast<ACSCharacter>(Controller->GetPawn()) : nullptr;
	if (!Character)
	{
		Out.Add(TEXT("the player has no ACSCharacter"));
		return Out;
	}
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		Out.Add(TEXT("the character has no skeletal mesh"));
	}
	else if (!Mesh->GetPhysicsAsset())
	{
		Out.Add(TEXT("the character's mesh has no physics asset"));
	}
	if (!Mesh || !Mesh->GetAnimInstance())
	{
		Out.Add(TEXT("the character has no animation instance"));
	}
	if (!Character->Tuning)
	{
		Out.Add(TEXT("the character has no movement tuning asset"));
	}
	const UInputMappingContext* Context = Character->InputMappingContext.Get();
	const ULocalPlayer* LocalPlayer = Controller->GetLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Context)
	{
		Out.Add(TEXT("the input mapping context is not loaded"));
	}
	else if (!Subsystem || !Subsystem->HasMappingContext(Context))
	{
		Out.Add(TEXT("the input mapping context is not added to the player"));
	}
	const ACSPlayerController* CSController = Cast<ACSPlayerController>(Controller);
	if (!CSController || !CSController->PauseAction.Get())
	{
		Out.Add(TEXT("the pause action is not loaded"));
	}
	return Out;
}

void UCSStartupCheck::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Value(FCommandLine::Get(), TEXT("CSSmokeTest="), ResultPath))
	{
		SetComponentTickEnabled(false);
		return;
	}
	UE_LOG(LogCombatSimulator, Display, TEXT("UCSStartupCheck: start check on, result to %s"), *ResultPath);
}

void UCSStartupCheck::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Waited += DeltaTime;
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	if (Controller && Controller->GetPawn())
	{
		SettledFor += DeltaTime;
		if (SettledFor >= SettleSeconds)
		{
			Finish(Problems(Controller));
		}
		return;
	}
	if (Waited >= TimeoutSeconds)
	{
		Finish({FString::Printf(TEXT("no player pawn after %.0f s"), TimeoutSeconds)});
	}
}

void UCSStartupCheck::Finish(const TArray<FString>& Found)
{
	SetComponentTickEnabled(false);
	const bool bPass = Found.IsEmpty();
	const FString Result = bPass ? FString(TEXT("PASS")) : TEXT("FAIL: ") + FString::Join(Found, TEXT("; "));
	if (!FFileHelper::SaveStringToFile(Result + TEXT("\n"), *ResultPath))
	{
		UE_LOG(LogCombatSimulator, Error, TEXT("UCSStartupCheck: could not write %s"), *ResultPath);
	}
	UE_LOG(LogCombatSimulator, Display, TEXT("UCSStartupCheck: %s"), *Result);
	FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1);
}
