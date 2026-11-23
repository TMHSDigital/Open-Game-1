using UnrealBuildTool;

// Editor module of the CSCharacter plugin: Python- and Blueprint-callable builders for a human-range
// ragdoll physics asset (UCSRagdollBuilder) and for blend spaces and a locomotion AnimGraph
// (UCSAnimGraphBuilder). Never part of a packaged game.
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
			"AnimGraph",            // AnimGraph editor nodes (blend list, players, slot, output)
			"AnimGraphRuntime",     // the runtime anim nodes they wrap
			"AssetRegistry",
			"BlueprintGraph",       // K2Node_VariableGet
			"MeshUtilitiesCommon",  // FBoneVertInfo
			"MeshUtilitiesEngine",  // vertices weighted to each bone
			"PhysicsCore",
			"PhysicsUtilities",     // FPhysicsAssetUtils: bodies, capsule fitting, constraints
			"UnrealEd",
		});
	}
}
