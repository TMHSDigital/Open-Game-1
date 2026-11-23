// Checks the base character assets made by Game/Content/Python/import_base_character.py.
// Run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Assets

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CSMovementTuning.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

namespace CSBaseCharacterAssetTests
{
	const TCHAR* MeshPath = TEXT("/Game/Characters/Base/SK_BaseCharacter.SK_BaseCharacter");
	const TCHAR* PhysicsPath = TEXT("/Game/Characters/Base/PHYS_BaseCharacter.PHYS_BaseCharacter");
	const TCHAR* TuningPath = TEXT("/Game/Characters/Base/DA_MovementTuning.DA_MovementTuning");
	// Must match RAGDOLL in import_base_character.py.
	constexpr int32 ExpectedBodies = 17;
	const TCHAR* Regenerate = TEXT(" (missing or stale: run Game/Content/Python/import_base_character.py, see its docstring)");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAssetsBaseCharacterTest, "CombatSimulator.CS.Assets.BaseCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCSAssetsBaseCharacterTest::RunTest(const FString& Parameters)
{
	using namespace CSBaseCharacterAssetTests;

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
	if (!TestNotNull(FString(TEXT("SK_BaseCharacter exists")) + Regenerate, Mesh))
	{
		return false;
	}
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	TestEqual(TEXT("89 bones (UE5 humanoid layout)"), Ref.GetNum(), 89);
	TestEqual(TEXT("First bone is root"), Ref.GetBoneName(0), FName(TEXT("root")));
	TestNotNull(TEXT("Mesh has a skeleton"), Mesh->GetSkeleton());

	// Imported at scale 1 from a centimetre scene: root at the origin with unit scale, 180 cm tall.
	const FTransform Root = Ref.GetRefBonePose()[0];
	TestTrue(TEXT("Root bone at the origin"), Root.GetLocation().IsNearlyZero(0.01));
	TestTrue(TEXT("Root bone scale 1"), Root.GetScale3D().Equals(FVector::OneVector, 0.001));
	const FBox Box = Mesh->GetImportedBounds().GetBox();
	TestNearlyEqual(TEXT("Mesh height 180 cm"), Box.Max.Z - Box.Min.Z, 180.0, 1.0);

	UPhysicsAsset* Physics = LoadObject<UPhysicsAsset>(nullptr, PhysicsPath);
	if (TestNotNull(FString(TEXT("PHYS_BaseCharacter exists")) + Regenerate, Physics))
	{
		TestEqual(FString(TEXT("Mesh uses PHYS_BaseCharacter")) + Regenerate, Mesh->GetPhysicsAsset(), Physics);
		TestEqual(TEXT("Body count"), Physics->SkeletalBodySetups.Num(), ExpectedBodies);
		TestEqual(TEXT("One constraint per non-root body"), Physics->ConstraintSetup.Num(), ExpectedBodies - 1);
		for (const USkeletalBodySetup* Body : Physics->SkeletalBodySetups)
		{
			TestTrue(FString::Printf(TEXT("Body %s is a capsule"), *Body->BoneName.ToString()), Body->AggGeom.SphylElems.Num() == 1);
		}
	}

	UCSMovementTuning* Tuning = LoadObject<UCSMovementTuning>(nullptr, TuningPath);
	if (TestNotNull(FString(TEXT("DA_MovementTuning exists")) + Regenerate, Tuning))
	{
		FString Reason;
		const bool bValid = Tuning->IsValid(Reason);
		TestTrue(FString::Printf(TEXT("DA_MovementTuning is valid (%s)"), *Reason), bValid);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
