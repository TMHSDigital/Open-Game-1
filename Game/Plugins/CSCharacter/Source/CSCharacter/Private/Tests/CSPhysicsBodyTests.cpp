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
		{TEXT("RagdollLinearDamping"), [](UCSMovementTuning& X) { X.RagdollLinearDamping = -0.1f; }},
		{TEXT("RagdollAngularDamping"), [](UCSMovementTuning& X) { X.RagdollAngularDamping = 50.f; }},
		{TEXT("RagdollFriction"), [](UCSMovementTuning& X) { X.RagdollFriction = NAN; }},
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSBodyContactVelocityChangeTest, "CombatSimulator.CS.Body.ContactVelocityChange", CSPhysicsBodyPluginTests::TestFlags)
bool FCSBodyContactVelocityChangeTest::RunTest(const FString& Parameters)
{
	const FVector Toward(1.f, 0.f, 0.f);
	// 10 kg at 10 m/s straight at an 80 kg character: 10 * 1000 / 80 = 125 cm/s.
	FVector Change = UCSPhysicsBodyComponent::ComputeContactVelocityChange(FVector(1000.f, 0.f, 0.f), 10.f, Toward, 80.f, 150.f, 1.f, 1500.f);
	TestNearlyEqual(TEXT("Momentum over the mass of the character"), float(Change.Size()), 125.f, 0.01f);
	TestTrue(TEXT("Along the motion of the other body"), Change.X > 0.f && FMath::IsNearlyZero(Change.Y) && FMath::IsNearlyZero(Change.Z));

	Change = UCSPhysicsBodyComponent::ComputeContactVelocityChange(FVector(1000.f, 0.f, 0.f), 10.f, Toward, 80.f, 150.f, 2.f, 1500.f);
	TestNearlyEqual(TEXT("The scale multiplies it"), float(Change.Size()), 250.f, 0.01f);

	Change = UCSPhysicsBodyComponent::ComputeContactVelocityChange(FVector(3000.f, 0.f, 0.f), 100.f, Toward, 80.f, 150.f, 1.f, 1500.f);
	TestNearlyEqual(TEXT("Never more than the limit"), float(Change.Size()), 1500.f, 0.01f);

	// A glancing body: only its speed toward the character counts, but the push follows its motion.
	const FVector Glancing = FVector(500.f, 866.f, 0.f);
	Change = UCSPhysicsBodyComponent::ComputeContactVelocityChange(Glancing, 10.f, Toward, 80.f, 150.f, 1.f, 1500.f);
	TestNearlyEqual(TEXT("A glancing hit counts its approach speed"), float(Change.Size()), 62.5f, 0.05f);
	TestTrue(TEXT("And pushes along its motion"), FVector::DotProduct(Change.GetSafeNormal(), Glancing.GetSafeNormal()) > 0.999);

	auto IsNothing = [&](const TCHAR* What, const FVector& Velocity, float Mass, float OurMass, float Scale)
	{
		TestTrue(What, UCSPhysicsBodyComponent::ComputeContactVelocityChange(Velocity, Mass, Toward, OurMass, 150.f, Scale, 1500.f).IsNearlyZero());
	};
	IsNothing(TEXT("Slower than the minimum speed: nothing"), FVector(149.f, 0.f, 0.f), 1000.f, 80.f, 1.f);
	IsNothing(TEXT("At rest (the character walked into it): nothing"), FVector::ZeroVector, 1000.f, 80.f, 1.f);
	IsNothing(TEXT("Moving away: nothing"), FVector(-1000.f, 0.f, 0.f), 10.f, 80.f, 1.f);
	IsNothing(TEXT("Sliding past: nothing"), FVector(0.f, 1000.f, 0.f), 10.f, 80.f, 1.f);
	IsNothing(TEXT("No mass: nothing"), FVector(1000.f, 0.f, 0.f), 0.f, 80.f, 1.f);
	IsNothing(TEXT("A character with no mass: nothing"), FVector(1000.f, 0.f, 0.f), 10.f, 0.f, 1.f);
	IsNothing(TEXT("Scale 0 turns contact hits off"), FVector(1000.f, 0.f, 0.f), 10.f, 80.f, 0.f);
	IsNothing(TEXT("A velocity that is not a number: nothing"), FVector(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f), 10.f, 80.f, 1.f);

	// The character's own motion. Something moving the same way as the character, slower than it
	// (a box or a ragdoll it is pushing along): they are not closing, so it is not a hit.
	auto WithOurs = [&](const FVector& Other, const FVector& Ours)
	{
		return UCSPhysicsBodyComponent::ComputeContactVelocityChange(Other, 40.f, Toward, 80.f, 150.f, 1.f, 1500.f, Ours);
	};
	TestTrue(TEXT("Pushed along the character's own path: nothing"), WithOurs(FVector(300.f, 0.f, 0.f), FVector(400.f, 0.f, 0.f)).IsNearlyZero());
	TestTrue(TEXT("Closing slower than the minimum: nothing"), WithOurs(FVector(300.f, 0.f, 0.f), FVector(200.f, 0.f, 0.f)).IsNearlyZero());
	TestNearlyEqual(TEXT("Closing at 200: counts 200, not its own 400"), float(WithOurs(FVector(400.f, 0.f, 0.f), FVector(200.f, 0.f, 0.f)).Size()), 100.f, 0.01f);
	// Walking into something that comes at the character: its own speed counts, not the sum.
	TestNearlyEqual(TEXT("The character's speed toward it adds nothing"), float(WithOurs(FVector(300.f, 0.f, 0.f), FVector(-400.f, 0.f, 0.f)).Size()), 150.f, 0.01f);
	TestTrue(TEXT("Walking into something at rest: nothing"), WithOurs(FVector::ZeroVector, FVector(-400.f, 0.f, 0.f)).IsNearlyZero());

	// No direction to the contact (the two are at the same point): the whole speed counts.
	Change = UCSPhysicsBodyComponent::ComputeContactVelocityChange(FVector(0.f, 1000.f, 0.f), 8.f, FVector::ZeroVector, 80.f, 150.f, 1.f, 1500.f);
	TestNearlyEqual(TEXT("Without a direction the whole speed counts"), float(Change.Size()), 100.f, 0.01f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
