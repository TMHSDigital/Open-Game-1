// The movement tests' view of the shared test world (Public/Tests/CSTestWorld.h): a fixed step
// chosen per test, and Run(Input, Seconds). No assets are loaded: tests pass a transient
// UCSMovementTuning, so they prove the code applies whatever values the asset holds.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/CSTestWorld.h"

namespace CSTestSupport
{
	// A test world with a 200 m floor box (optionally tilted by FloorPitchDeg, rising toward +X)
	// and one ACSCharacter standing on it at the origin. Ticked manually at a fixed step.
	struct FMoveSim : FCSTestWorld
	{
		bool Init(FAutomationTestBase& Test, float InDt, UCSMovementTuning* Tuning, float FloorPitchDeg = 0.f)
		{
			Dt = InDt;
			FCSTestWorldOptions Options;
			Options.FloorPitchDeg = FloorPitchDeg;
			return FCSTestWorld::Init(Test, Tuning, Options);
		}

		void Run(const FVector& Input, float Seconds) { FCSTestWorld::Run(Seconds, Input); }
	};

	inline UCSMovementTuning* NewTuning()
	{
		return NewObject<UCSMovementTuning>(GetTransientPackage());
	}

}

#endif // WITH_DEV_AUTOMATION_TESTS
