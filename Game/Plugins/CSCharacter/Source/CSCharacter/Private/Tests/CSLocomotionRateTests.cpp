// Locomotion blend position and play rate (run with: pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS.Animation).
//
// Asset-free: the numbers below stand in for the stride table setup_animation.py writes. The tests
// that load the real animation Blueprint are in the game module (Source/CombatSimulator/Tests/CSAnimationTests.cpp).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CSAnimInstance.h"

namespace CSLocomotionRateTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FCSStrideSample Sample(float StrideSpeed, float Seconds)
	{
		FCSStrideSample Out;
		Out.StrideSpeed = StrideSpeed;
		Out.Seconds = Seconds;
		return Out;
	}

	// Idle, walk, jog, sprint.
	const TArray<FCSStrideSample>& Stride()
	{
		static const TArray<FCSStrideSample> Table = {Sample(0.f, 2.f), Sample(150.f, 1.f), Sample(400.f, 0.8f), Sample(600.f, 0.6f)};
		return Table;
	}

	const float Gaits[] = {180.f, 380.f, 620.f};
	constexpr float RateMin = 0.5f;
	constexpr float RateMax = 2.f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAnimationPlayRateAtGaitSpeedsTest, "CombatSimulator.CS.Animation.PlayRateAtGaitSpeeds", CSLocomotionRateTests::TestFlags)
bool FCSAnimationPlayRateAtGaitSpeedsTest::RunTest(const FString& Parameters)
{
	using namespace CSLocomotionRateTests;
	float Axis = -1.f;
	float Rate = UCSAnimInstance::ComputeLocomotion(180.f, Gaits, Stride(), RateMin, RateMax, Axis);
	TestNearlyEqual(TEXT("Walk speed sits on the walk sample"), Axis, 1.f, 0.001f);
	TestNearlyEqual(TEXT("Walk at 180 over a 150 stride plays x1.2"), Rate, 1.2f, 0.001f);

	Rate = UCSAnimInstance::ComputeLocomotion(380.f, Gaits, Stride(), RateMin, RateMax, Axis);
	TestNearlyEqual(TEXT("Jog speed sits on the jog sample"), Axis, 2.f, 0.001f);
	TestNearlyEqual(TEXT("Jog at 380 over a 400 stride plays x0.95"), Rate, 0.95f, 0.001f);

	Rate = UCSAnimInstance::ComputeLocomotion(620.f, Gaits, Stride(), RateMin, RateMax, Axis);
	TestNearlyEqual(TEXT("Sprint speed sits on the sprint sample"), Axis, 3.f, 0.001f);
	TestNearlyEqual(TEXT("Sprint at 620 over a 600 stride"), Rate, 620.f / 600.f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAnimationPlayRateBetweenGaitsTest, "CombatSimulator.CS.Animation.PlayRateBetweenGaits", CSLocomotionRateTests::TestFlags)
bool FCSAnimationPlayRateBetweenGaitsTest::RunTest(const FString& Parameters)
{
	using namespace CSLocomotionRateTests;
	float Axis = -1.f;
	const float Rate = UCSAnimInstance::ComputeLocomotion(280.f, Gaits, Stride(), RateMin, RateMax, Axis);
	TestNearlyEqual(TEXT("Halfway between walk and jog speed"), Axis, 1.5f, 0.001f);
	// Half of each clip: one blended cycle takes 0.5*1 + 0.5*0.8 s and covers 0.5*150*1 + 0.5*400*0.8 cm.
	const float Natural = (0.5f * 150.f * 1.f + 0.5f * 400.f * 0.8f) / (0.5f * 1.f + 0.5f * 0.8f);
	TestNearlyEqual(TEXT("The rate is the speed over the blended stride speed"), Rate, 280.f / Natural, 0.001f);

	// The feet match the ground: rate times the blended stride speed is the ground speed.
	for (float Speed = 190.f; Speed < 620.f; Speed += 37.f)
	{
		float A = 0.f;
		const float R = UCSAnimInstance::ComputeLocomotion(Speed, Gaits, Stride(), 0.01f, 100.f, A);
		const int32 Lower = FMath::Min(FMath::FloorToInt32(A), 2);
		const float Upper = A - float(Lower);
		const FCSStrideSample& S0 = Stride()[Lower];
		const FCSStrideSample& S1 = Stride()[Lower + 1];
		const float Blended = ((1.f - Upper) * S0.StrideSpeed * S0.Seconds + Upper * S1.StrideSpeed * S1.Seconds) / ((1.f - Upper) * S0.Seconds + Upper * S1.Seconds);
		TestNearlyEqual(FString::Printf(TEXT("Feet match the ground at %.0f cm/s"), Speed), R * Blended, Speed, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSAnimationPlayRateDegenerateTest, "CombatSimulator.CS.Animation.PlayRateDegenerate", CSLocomotionRateTests::TestFlags)
bool FCSAnimationPlayRateDegenerateTest::RunTest(const FString& Parameters)
{
	using namespace CSLocomotionRateTests;
	float Axis = -1.f;
	float Rate = UCSAnimInstance::ComputeLocomotion(0.f, Gaits, Stride(), RateMin, RateMax, Axis);
	TestEqual(TEXT("Standing still sits on idle"), Axis, 0.f);
	TestEqual(TEXT("Standing still plays at rate 1"), Rate, 1.f);

	Rate = UCSAnimInstance::ComputeLocomotion(5000.f, Gaits, Stride(), RateMin, RateMax, Axis);
	TestEqual(TEXT("Far above sprint stays on the sprint sample"), Axis, 3.f);
	TestEqual(TEXT("And the rate stops at the upper limit"), Rate, RateMax);

	Rate = UCSAnimInstance::ComputeLocomotion(20.f, Gaits, Stride(), RateMin, RateMax, Axis);
	TestTrue(TEXT("A crawl gives a finite rate inside the limits"), FMath::IsFinite(Rate) && Rate >= RateMin && Rate <= RateMax);

	const float Same[] = {300.f, 300.f, 300.f};
	Rate = UCSAnimInstance::ComputeLocomotion(300.f, Same, Stride(), RateMin, RateMax, Axis);
	TestTrue(TEXT("Equal gait speeds give a finite axis on the table"), FMath::IsFinite(Axis) && Axis >= 0.f && Axis <= 3.f);
	TestTrue(TEXT("Equal gait speeds give a finite rate inside the limits"), FMath::IsFinite(Rate) && Rate >= RateMin && Rate <= RateMax);

	Rate = UCSAnimInstance::ComputeLocomotion(300.f, Gaits, TConstArrayView<FCSStrideSample>(), RateMin, RateMax, Axis);
	TestEqual(TEXT("No stride table: rate 1"), Rate, 1.f);
	TestTrue(TEXT("No stride table: the axis still follows the speed"), Axis > 1.f && Axis < 2.f);

	Rate = UCSAnimInstance::ComputeLocomotion(std::numeric_limits<float>::quiet_NaN(), Gaits, Stride(), RateMin, RateMax, Axis);
	TestTrue(TEXT("A speed that is not a number is treated as standing still"), Axis == 0.f && Rate == 1.f);

	Rate = UCSAnimInstance::ComputeLocomotion(300.f, TConstArrayView<float>(), Stride(), RateMin, RateMax, Axis);
	TestTrue(TEXT("No gait speeds: idle at rate 1"), Axis == 0.f && Rate == 1.f);

	const TArray<FCSStrideSample> ZeroLength = {Sample(0.f, 0.f), Sample(150.f, 0.f)};
	const float OneGait[] = {160.f};
	Rate = UCSAnimInstance::ComputeLocomotion(80.f, OneGait, ZeroLength, RateMin, RateMax, Axis);
	TestTrue(TEXT("Clips of zero length do not divide by zero"), FMath::IsFinite(Rate) && Rate >= RateMin && Rate <= RateMax);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
