using UnrealBuildTool;

// Editor module of the CSCharacter plugin: UCSRagdollBuilder, a Python- and Blueprint-callable
// builder for a human-range ragdoll physics asset. Never part of a packaged game.
public class CSCharacterEditor : ModuleRules
{
	public CSCharacterEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"AssetRegistry",
			"MeshUtilitiesCommon",  // FBoneVertInfo
			"MeshUtilitiesEngine",  // vertices weighted to each bone
			"PhysicsCore",
			"PhysicsUtilities",     // FPhysicsAssetUtils: bodies, capsule fitting, constraints
			"UnrealEd",
		});
	}
}
