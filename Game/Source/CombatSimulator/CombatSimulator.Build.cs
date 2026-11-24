using UnrealBuildTool;

public class CombatSimulator : ModuleRules
{
	public CombatSimulator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// A module listed here is linked into CombatSimulator; a missing one shows up as LNK2019 link errors.
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"PhysicsCore",  // physical materials in the body tests (CSPhysicsBodyTests.cpp)
			"EnhancedInput",  // input asset tests (CSInputAssetTests.cpp)
			"CSCharacter"  // Game/Plugins/CSCharacter: ACSCharacter, UCSMovementTuning
		});

		// The pause screen (UI/): Slate widgets, and the clipboard for "Copy values".
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate",
			"SlateCore",
			"ApplicationCore"
		});

		// The map tests play the real maps in PIE and read the packaging settings (CSMapTests.cpp).
		// Editor builds only: a packaged game never links these.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd",
				"DeveloperToolSettings",
				"EngineSettings"
			});
		}
	}
}
