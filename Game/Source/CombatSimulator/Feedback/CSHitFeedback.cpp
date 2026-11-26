#include "CSHitFeedback.h"

#include "CSCharacter.h"
#include "CSStaminaComponent.h"
#include "EngineUtils.h"
#include "CSStrikeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// The colour parameters a character material may have: the imported base material's, and the
	// common names a hand-made one would use (the dummy tint uses the same list).
	const FName ColourParameters[] = {TEXT("DiffuseColor"), TEXT("BaseColor"), TEXT("Tint")};
}

UCSHitFeedback::UCSHitFeedback()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Marks fade and the hit-stop ends in real time, also while the world is all but stopped.
	PrimaryComponentTick.bTickEvenWhenPaused = false;
}

void UCSHitFeedback::BeginPlay()
{
	Super::BeginPlay();
	ContactHandle = UCSPhysicsBodyComponent::OnAnyContact.AddUObject(this, &UCSHitFeedback::HandleContact);
	StrikeHandle = UCSStrikeComponent::OnStrikeLanded.AddUObject(this, &UCSHitFeedback::HandleStrikeLanded);
	HudHandle = AHUD::OnHUDPostRender.AddUObject(this, &UCSHitFeedback::DrawMarks);
}

void UCSHitFeedback::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UCSPhysicsBodyComponent::OnAnyContact.Remove(ContactHandle);
	UCSStrikeComponent::OnStrikeLanded.Remove(StrikeHandle);
	AHUD::OnHUDPostRender.Remove(HudHandle);
	EndHitStop();
	// Put every flashed body back to its colour.
	UpdateFlashes(TNumericLimits<double>::Max());
	Super::EndPlay(EndPlayReason);
}

ACSCharacter* UCSHitFeedback::GetPlayer() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? Cast<ACSCharacter>(PC->GetPawn()) : nullptr;
}

void UCSHitFeedback::HandleContact(ACSCharacter* Victim, AActor* Other, const FCSContactRecord& Hit)
{
	if (!Victim || Victim->GetWorld() != GetWorld()
		|| (Hit.Result != ECSContactResult::Stagger && Hit.Result != ECSContactResult::Ragdoll && Hit.Result != ECSContactResult::Blocked))
	{
		return;
	}
	if (Hit.Result != ECSContactResult::Blocked)
	{
		// A blocked hit did not land: no flash, only the mark.
		StartFlash(Victim);
	}
	const ACSCharacter* Player = GetPlayer();
	if (!Player)
	{
		return;
	}
	bool bDealt = false;
	for (const AActor* Actor = Other; Actor; Actor = Actor->GetOwner())
	{
		bDealt |= Actor == Player;
	}
	if (Victim != Player && !bDealt)
	{
		return; // between others: the flash is enough
	}
	FCSHitMark Mark;
	Mark.Point = Hit.Point;
	Mark.Size = Hit.VelocityChange;
	Mark.Result = Hit.Result;
	Mark.bDealt = Victim != Player;
	Mark.Time = FPlatformTime::Seconds();
	Marks.Add(Mark);
}

void UCSHitFeedback::HandleStrikeLanded(ACSCharacter* Attacker, AActor* Victim, ECSContactResult Result)
{
	UWorld* World = GetWorld();
	if (!Attacker || Attacker->GetWorld() != World || Attacker != GetPlayer() || HitStopSeconds <= 0.f
		|| (Result != ECSContactResult::Stagger && Result != ECSContactResult::Ragdoll))
	{
		return;
	}
	AWorldSettings* Settings = World->GetWorldSettings();
	if (!Settings)
	{
		return;
	}
	if (HitStopUntil <= 0.0)
	{
		DilationBefore = Settings->TimeDilation;
	}
	Settings->SetTimeDilation(HitStopDilation);
	HitStopUntil = FPlatformTime::Seconds() + HitStopSeconds;
}

void UCSHitFeedback::EndHitStop()
{
	if (HitStopUntil <= 0.0)
	{
		return;
	}
	HitStopUntil = 0.0;
	if (AWorldSettings* Settings = GetWorld() ? GetWorld()->GetWorldSettings() : nullptr)
	{
		Settings->SetTimeDilation(DilationBefore);
	}
}

void UCSHitFeedback::StartFlash(ACSCharacter* Victim)
{
	USkeletalMeshComponent* Body = Victim->GetMesh();
	if (!Body)
	{
		return;
	}
	FFlash& Flash = Flashes.FindOrAdd(Victim);
	if (Flash.Materials.IsEmpty())
	{
		for (int32 Index = 0; Index < Body->GetNumMaterials(); ++Index)
		{
			UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(Index));
			if (!Material)
			{
				Material = Body->CreateDynamicMaterialInstance(Index);
			}
			if (!Material)
			{
				continue;
			}
			TArray<TPair<FName, FLinearColor>> Colours;
			for (const FName Parameter : ColourParameters)
			{
				FLinearColor Value;
				if (Material->GetVectorParameterValue(FHashedMaterialParameterInfo(Parameter), Value))
				{
					Colours.Emplace(Parameter, Value);
				}
			}
			Flash.Materials.Add(Material);
			Flash.Original.Add(MoveTemp(Colours));
		}
	}
	Flash.Time = FPlatformTime::Seconds();
}

float UCSHitFeedback::GetFlashWeight(const ACSCharacter* Character) const
{
	const FFlash* Flash = Flashes.Find(Character);
	if (!Flash || FlashSeconds <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp(1.f - float(FPlatformTime::Seconds() - Flash->Time) / FlashSeconds, 0.f, 1.f);
}

void UCSHitFeedback::UpdateFlashes(double Now)
{
	for (auto It = Flashes.CreateIterator(); It; ++It)
	{
		FFlash& Flash = It.Value();
		const float Weight = FlashSeconds > 0.f ? FMath::Clamp(1.f - float(Now - Flash.Time) / FlashSeconds, 0.f, 1.f) : 0.f;
		for (int32 Index = 0; Index < Flash.Materials.Num(); ++Index)
		{
			UMaterialInstanceDynamic* Material = Flash.Materials[Index].Get();
			if (!Material)
			{
				continue;
			}
			for (const TPair<FName, FLinearColor>& Colour : Flash.Original[Index])
			{
				Material->SetVectorParameterValue(Colour.Key, FMath::Lerp(Colour.Value, FlashColor, Weight * Weight));
			}
		}
		if (Weight <= 0.f || !It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void UCSHitFeedback::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const double Now = FPlatformTime::Seconds();
	if (HitStopUntil > 0.0 && Now >= HitStopUntil)
	{
		EndHitStop();
	}
	UpdateFlashes(Now);
	Marks.RemoveAll([this, Now](const FCSHitMark& Mark) { return Now - Mark.Time > MarkSeconds; });
}

FString UCSHitFeedback::DescribeMark(const FCSHitMark& Mark)
{
	const TCHAR* What = Mark.Result == ECSContactResult::Ragdoll ? TEXT("knockdown")
		: Mark.Result == ECSContactResult::Blocked ? TEXT("blocked") : TEXT("stagger");
	return FString::Printf(TEXT("%d %s"), FMath::RoundToInt(Mark.Size), What);
}

void UCSHitFeedback::DrawStamina(UCanvas* Canvas)
{
	const ACSCharacter* Player = GetPlayer();
	auto Bar = [Canvas](float X, float Y, float Width, float Height, const UCSStaminaComponent& Stamina)
	{
		const FLinearColor Fill = Stamina.IsExhausted() ? FLinearColor(0.9f, 0.15f, 0.1f) : FLinearColor(0.95f, 0.85f, 0.3f);
		Canvas->K2_DrawBox(FVector2D(X - 1.f, Y - 1.f), FVector2D(Width + 2.f, Height + 2.f), 1.f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Canvas->K2_DrawTexture(nullptr, FVector2D(X, Y), FVector2D(Width * Stamina.GetShare(), Height), FVector2D::ZeroVector,
			FVector2D::UnitVector, Fill);
	};
	const bool bFighting = Player && Player->IsInCombatStance();
	if (const UCSStaminaComponent* Mine = Player ? Player->GetStamina() : nullptr;
		Mine && Mine->IsEnabled() && (bFighting || Mine->GetShare() < 0.999f))
	{
		const float Scale = FMath::Max(1.f, Canvas->ClipY / 720.f);
		const float Width = 240.f * Scale;
		Bar((Canvas->ClipX - Width) * 0.5f, Canvas->ClipY * 0.86f, Width, 8.f * Scale, *Mine);
	}
	for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
	{
		const ACSCharacter* Other = *It;
		const UCSStaminaComponent* Theirs = Other->GetStamina();
		// Only a character that fights (one with a controller of its own): a dummy standing still spends nothing.
		if (Other == Player || !Theirs || !Theirs->IsEnabled() || !Other->GetController() || Other->IsRagdolled()
			|| !(bFighting || Theirs->GetShare() < 0.999f))
		{
			continue;
		}
		const USkeletalMeshComponent* Body = Other->GetMesh();
		const FVector Over = (Body ? Body->GetSocketLocation(TEXT("head")) : Other->GetActorLocation()) + FVector(0.f, 0.f, 30.f);
		const FVector Screen = Canvas->Project(Over);
		if (Screen.Z <= 0.f)
		{
			continue;
		}
		Bar(float(Screen.X) - 30.f, float(Screen.Y), 60.f, 5.f, *Theirs);
	}
}

void UCSHitFeedback::DrawMarks(AHUD* HUD, UCanvas* Canvas)
{
	if (!HUD || !Canvas || HUD->GetWorld() != GetWorld() || !GEngine || GetWorld()->IsPaused())
	{
		return;
	}
	DrawStamina(Canvas);
	if (Marks.IsEmpty())
	{
		return;
	}
	const UFont* Font = GEngine->GetMediumFont();
	const double Now = FPlatformTime::Seconds();
	for (const FCSHitMark& Mark : Marks)
	{
		const FVector Screen = Canvas->Project(Mark.Point);
		if (Screen.Z <= 0.f)
		{
			continue; // behind the camera
		}
		const float Age = float(Now - Mark.Time) / FMath::Max(MarkSeconds, 0.01f);
		const uint8 Alpha = uint8(255.f * FMath::Clamp(1.f - Age, 0.f, 1.f));
		// Dealt: yellow, knockdowns orange; taken: red; on a guard, either way: light blue. Rises a little as it fades.
		FColor Colour = Mark.Result == ECSContactResult::Blocked ? FColor(120, 200, 255)
			: Mark.bDealt ? (Mark.Result == ECSContactResult::Ragdoll ? FColor(255, 140, 20) : FColor(255, 230, 60)) : FColor(255, 70, 60);
		Colour.A = Alpha;
		const float X = float(Screen.X);
		const float Y = float(Screen.Y) - 30.f * Age;
		// A cross where it landed.
		const float Arm = 9.f;
		Canvas->K2_DrawLine(FVector2D(X - Arm, Y - Arm), FVector2D(X + Arm, Y + Arm), 3.f, FLinearColor(Colour));
		Canvas->K2_DrawLine(FVector2D(X - Arm, Y + Arm), FVector2D(X + Arm, Y - Arm), 3.f, FLinearColor(Colour));
		const FString Text = DescribeMark(Mark);
		Canvas->SetDrawColor(FColor(0, 0, 0, Alpha));
		Canvas->DrawText(Font, Text, X + 15.f, Y - 21.f);
		Canvas->SetDrawColor(Colour);
		Canvas->DrawText(Font, Text, X + 14.f, Y - 22.f);
	}
}
