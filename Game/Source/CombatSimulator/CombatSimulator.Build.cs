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
			"InputCore"
		});
	}
}
