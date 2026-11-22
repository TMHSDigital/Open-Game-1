using UnrealBuildTool;

// Dev-only fixed-camera capture driver (tools/ue/capture.ps1, docs/captures/cameras.json).
// The module is "UncookedOnly" in CombatSimulator.uproject and is listed only in the Editor target:
// it loads in the editor and in uncooked -game runs of the editor binary, and is never part of a
// cooked, packaged or Shipping build. Gameplay code must never depend on it.
public class CSCapture : ModuleRules
{
	public CSCapture(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Json",            // Reading capture jobs and writing the result file
			"JsonUtilities",   // FJsonObjectConverter: JSON to USTRUCT
			"ImageWriteQueue", // Waiting for queued screenshot PNG writes before exit
		});
	}
}
