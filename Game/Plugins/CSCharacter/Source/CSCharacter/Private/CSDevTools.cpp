#include "CSDevTools.h"

#include "CSCharacterLog.h"
#include "CSMovementTuning.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"

namespace
{
	const TCHAR* GaitName(ECSGait Gait)
	{
		switch (Gait)
		{
		case ECSGait::Walk: return TEXT("Walk");
		case ECSGait::Sprint: return TEXT("Sprint");
		case ECSGait::Crouch: return TEXT("Crouch");
		case ECSGait::Jog:
		default: return TEXT("Jog");
		}
	}

	void RunReloadCommand(const TArray<FString>& Args, UWorld* World)
	{
		TArray<FString> Messages;
		UCSDevTools::ReloadTuningInWorld(World, Messages);
		if (Messages.Num() == 0)
		{
			Messages.Add(TEXT("cs.Tuning.Reload: no CS character in this world"));
			UE_LOG(LogCSCharacter, Warning, TEXT("%s"), *Messages[0]);
		}
		if (GEngine)
		{
			for (const FString& Message : Messages)
			{
				GEngine->AddOnScreenDebugMessage(INDEX_NONE, 5.f, FColor::Yellow, Message);
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ReloadCommand(
		TEXT("cs.Tuning.Reload"),
		TEXT("Validates and reapplies the movement tuning asset of every CS character (dev tools only). A bad asset is rejected and the last good values are kept."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunReloadCommand));
}

UCSDevTools::UCSDevTools()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// After the movement component (TG_PrePhysics) has moved the character this frame.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

ACSCharacter* UCSDevTools::GetCharacter() const
{
	return Cast<ACSCharacter>(GetOwner());
}

bool UCSDevTools::ReloadTuning(ACSCharacter& Character, FString& OutMessage)
{
	const UCSMovementTuning* Source = Character.Tuning ? Character.Tuning.Get() : GetDefault<UCSMovementTuning>();
	const FString AssetName = Character.Tuning ? Character.Tuning->GetPathName() : FString(TEXT("class defaults"));
	FString Reason;
	if (!Source->IsValid(Reason))
	{
		OutMessage = FString::Printf(TEXT("%s: tuning %s rejected, %s. Keeping the last good values."), *Character.GetName(), *AssetName, *Reason);
		UE_LOG(LogCSCharacter, Warning, TEXT("%s"), *OutMessage);
		return false;
	}
	Character.ApplyTuning();
	OutMessage = FString::Printf(TEXT("%s: tuning %s reloaded."), *Character.GetName(), *AssetName);
	UE_LOG(LogCSCharacter, Log, TEXT("%s"), *OutMessage);
	return true;
}

int32 UCSDevTools::ReloadTuningInWorld(UWorld* World, TArray<FString>& OutMessages)
{
	int32 Reloaded = 0;
	if (!World)
	{
		return 0;
	}
	for (TActorIterator<ACSCharacter> It(World); It; ++It)
	{
		ACSCharacter* Character = *It;
		const UCSDevTools* DevTools = Character->GetDevTools();
		if (!DevTools || !DevTools->AreEnabled())
		{
			FString Message = FString::Printf(TEXT("%s: dev tools are off (bDevTools), tuning not reloaded."), *Character->GetName());
			UE_LOG(LogCSCharacter, Log, TEXT("%s"), *Message);
			OutMessages.Add(MoveTemp(Message));
			continue;
		}
		FString Message;
		if (ReloadTuning(*Character, Message))
		{
			++Reloaded;
		}
		OutMessages.Add(MoveTemp(Message));
	}
	return Reloaded;
}

bool UCSDevTools::AreEnabled() const
{
	const ACSCharacter* Character = GetCharacter();
	return Character && Character->GetActiveTuning().bDevTools;
}

bool UCSDevTools::IsOverlayVisible() const
{
	return bOverlayVisible && AreEnabled();
}

void UCSDevTools::ToggleOverlay()
{
	if (!AreEnabled())
	{
		SetOverlayVisible(false);
		return;
	}
	SetOverlayVisible(!bOverlayVisible);
}

void UCSDevTools::SetOverlayVisible(bool bVisible)
{
	bOverlayVisible = bVisible;
	bHasPreviousVelocity = false;
	SetComponentTickEnabled(bVisible);
	if (bVisible && !HUDHandle.IsValid())
	{
		HUDHandle = AHUD::OnHUDPostRender.AddUObject(this, &UCSDevTools::DrawOverlay);
	}
	else if (!bVisible && HUDHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HUDHandle);
		HUDHandle.Reset();
	}
}

bool UCSDevTools::RequestRagdoll()
{
	if (!AreEnabled())
	{
		return false;
	}
	OnRagdollRequested.Broadcast();
	return true;
}

bool UCSDevTools::RequestPush()
{
	const ACSCharacter* Character = GetCharacter();
	if (!Character || !AreEnabled())
	{
		return false;
	}
	// The view direction: the controller's aim, or the actor's facing with no controller.
	const FVector Direction = Character->GetBaseAimRotation().Vector().GetSafeNormal();
	OnPushRequested.Broadcast(Direction.IsNearlyZero() ? Character->GetActorForwardVector() : Direction);
	return true;
}

void UCSDevTools::BeginPlay()
{
	Super::BeginPlay();
	if (const ACSCharacter* Character = GetCharacter())
	{
		// TG_PostPhysics already orders this after the movement component; the prerequisite keeps
		// that true if either tick group is changed. The movement component does not depend on
		// this component, so there is no cycle.
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			AddTickPrerequisiteComponent(Move);
		}
	}
}

void UCSDevTools::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetOverlayVisible(false);
	Super::EndPlay(EndPlayReason);
}

void UCSDevTools::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsOverlayVisible())
	{
		// Dev tools were turned off by a reload while the overlay was up.
		SetOverlayVisible(false);
		return;
	}
	TakeSample(DeltaTime);
}

void UCSDevTools::TakeSample(float DeltaTime)
{
	const ACSCharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	const FVector Velocity = Move->Velocity;
	Sample.Speed = Velocity.Size2D();
	Sample.VerticalSpeed = Velocity.Z;
	Sample.Acceleration = (bHasPreviousVelocity && DeltaTime > 0.f)
		? (FVector(Velocity.X, Velocity.Y, 0.f) - FVector(PreviousVelocity.X, PreviousVelocity.Y, 0.f)).Size() / DeltaTime
		: 0.f;
	PreviousVelocity = Velocity;
	bHasPreviousVelocity = true;

	Sample.bGrounded = Move->IsMovingOnGround();
	Sample.SlopeAngleDeg = 0.f;
	if (Sample.bGrounded && Move->CurrentFloor.bBlockingHit)
	{
		const float NormalZ = FMath::Clamp(float(Move->CurrentFloor.HitResult.ImpactNormal.Z), -1.f, 1.f);
		Sample.SlopeAngleDeg = FMath::RadiansToDegrees(FMath::Acos(NormalZ));
	}
	Sample.Gait = Character->GetGait();
	Sample.Camera = Character->GetActiveCameraName();
}

FString UCSDevTools::GetOverlayText() const
{
	return FString::Printf(
		TEXT("Speed     %6.0f cm/s\n")
		TEXT("Accel     %6.0f cm/s2\n")
		TEXT("Vertical  %6.0f cm/s\n")
		TEXT("Grounded  %s\n")
		TEXT("Slope     %6.1f deg\n")
		TEXT("Gait      %s\n")
		TEXT("Camera    %s"),
		Sample.Speed,
		Sample.Acceleration,
		Sample.VerticalSpeed,
		Sample.bGrounded ? TEXT("yes") : TEXT("no"),
		Sample.SlopeAngleDeg,
		GaitName(Sample.Gait),
		Sample.Camera.IsNone() ? TEXT("n/a") : *Sample.Camera.ToString());
}

void UCSDevTools::DrawOverlay(AHUD* HUD, UCanvas* Canvas)
{
	const ACSCharacter* Character = GetCharacter();
	if (!HUD || !Canvas || !Character || HUD->GetOwningPawn() != Character || !IsOverlayVisible() || !GEngine)
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	float X = 24.f;
	float Y = 96.f;
	Canvas->SetDrawColor(FColor::Black);
	TArray<FString> Lines;
	GetOverlayText().ParseIntoArrayLines(Lines);
	const float LineHeight = Font ? Font->GetMaxCharHeight() + 2.f : 14.f;
	for (const FString& Line : Lines)
	{
		// A one-pixel shadow keeps the text readable on any background.
		Canvas->SetDrawColor(FColor::Black);
		Canvas->DrawText(Font, Line, X + 1.f, Y + 1.f);
		Canvas->SetDrawColor(FColor::White);
		Canvas->DrawText(Font, Line, X, Y);
		Y += LineHeight;
	}
}
