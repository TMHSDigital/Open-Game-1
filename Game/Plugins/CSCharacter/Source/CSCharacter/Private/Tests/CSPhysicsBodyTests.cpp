// Asset-free checks of the physics body (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Body).
//
// The behaviour tests (stagger, ragdoll, recovery, joint limits) need the base character's physics
// asset and live in the game module (Source/CombatSimulator/Tests/CSPhysicsBodyTests.cpp).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CSMovementTuning.h"
#include "CSPhysicsBodyComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace CSPhysicsBodyPluginTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyTuningRejectsBadBodyValuesTest, "CombatSimulator.CS.Body.TuningRejectsBadBodyValues", CSPhysicsBodyPluginTests::TestFlags)
bool FCSBodyTuningRejectsBadBodyValuesTest::RunTest(const FString& Parameters)
{
	// Review Focus 3 for the body values: a bad profile or time is caught by IsValid (and so by a reload).
	FString Reason;
	TStrongObjectPtr<UCSMovementTuning> T(NewObject<UCSMovementTuning>(GetTransientPackage()));
	TestTrue(TEXT("The defaults are valid"), T->IsValid(Reason));
	TestFalse(TEXT("Normal is world space by default (keeps up with the capsule)"), bool(T->NormalProfile.bIsLocalSimulation));
	TestTrue(TEXT("Hit is weaker than Normal"), T->HitProfile.OrientationStrength < T->NormalProfile.OrientationStrength * T->NormalLimbStrengthScale);

	struct FCase { const TCHAR* Field; TFunction<void(UCSMovementTuning&)> Break; };
	const FCase Cases[] = {
		{TEXT("NormalProfile.OrientationStrength"), [](UCSMovementTuning& X) { X.NormalProfile.OrientationStrength = -1.f; }},
		{TEXT("HitProfile.PositionStrength"), [](UCSMovementTuning& X) { X.HitProfile.PositionStrength = NAN; }},
		{TEXT("NormalLimbStrengthScale"), [](UCSMovementTuning& X) { X.NormalLimbStrengthScale = 1.5f; }},
		{TEXT("HitHoldSeconds"), [](UCSMovementTuning& X) { X.HitHoldSeconds = -0.1f; }},
		{TEXT("HitRecoverSeconds"), [](UCSMovementTuning& X) { X.HitRecoverSeconds = 0.f; }},
		{TEXT("HitCapsuleVelocityShare"), [](UCSMovementTuning& X) { X.HitCapsuleVelocityShare = 2.f; }},
		{TEXT("RagdollSettleSpeed"), [](UCSMovementTuning& X) { X.RagdollSettleSpeed = 0.f; }},
		{TEXT("RagdollSettleSeconds"), [](UCSMovementTuning& X) { X.RagdollSettleSeconds = 9.f; }},
		{TEXT("DevPushSpeed"), [](UCSMovementTuning& X) { X.DevPushSpeed = -5.f; }},
	};
	for (const FCase& Case : Cases)
	{
		TStrongObjectPtr<UCSMovementTuning> Bad(NewObject<UCSMovementTuning>(GetTransientPackage()));
		Case.Break(*Bad);
		const bool bValid = Bad->IsValid(Reason);
		TestFalse(FString::Printf(TEXT("A bad %s is rejected"), Case.Field), bValid);
		TestTrue(FString::Printf(TEXT("The reason names %s (got: %s)"), Case.Field, *Reason), Reason.Contains(Case.Field));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyWithoutPhysicsBodyIsHarmlessTest, "CombatSimulator.CS.Body.WithoutPhysicsBodyIsHarmless", CSPhysicsBodyPluginTests::TestFlags)
bool FCSBodyWithoutPhysicsBodyIsHarmlessTest::RunTest(const FString& Parameters)
{
	// No mesh, no physics asset: every call is a logged no-op and the state stays Normal.
	AddExpectedMessagePlain(TEXT("ignored: no physics body"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TStrongObjectPtr<UCSPhysicsBodyComponent> Component(NewObject<UCSPhysicsBodyComponent>(GetTransientPackage()));
	TestFalse(TEXT("No physics body"), Component->HasPhysicsBody());
	TestEqual(TEXT("No mass"), Component->GetBodyMass(), 0.f);
	Component->ApplyImpulseAtBone(TEXT("spine_03"), FVector(1.0e6, 0.0, 0.0));
	Component->EnterRagdoll();
	Component->ToggleRagdoll();
	Component->Recover();
	TestEqual(TEXT("Still Normal"), Component->GetState(), ECSBodyState::Normal);
	TestEqual(TEXT("A torso bone's chain is the upper body"), Component->GetChainRoot(TEXT("spine_03")), Component->SimulatedRootBone);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
