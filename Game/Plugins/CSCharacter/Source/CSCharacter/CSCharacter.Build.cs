using UnrealBuildTool;

// Runtime module of the CSCharacter plugin: ACSCharacter, UCSMovementTuning and UCSDevTools.
// Self-contained: no game module, content path or project setting is assumed.
public class CSCharacter : ModuleRules
{
	public CSCharacter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"EnhancedInput",
			"InputCore",
		});
	}
}
