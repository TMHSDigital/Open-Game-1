using UnrealBuildTool;

// Runtime module of the CSCharacter plugin: ACSCharacter, UCSMovementTuning, UCSDevTools, UCSCameraRig and UCSPhysicsBodyComponent.
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
			"PhysicsCore",  // FBodyInstanceCore (physics body states)
			"Json",  // the tuning overrides file (CSTuningOverrides.cpp)
			"AnimGraphRuntime",  // FAnimNode_SkeletalControlBase (CSAnimNode_StrideScale)
			"AnimationCore",  // two-bone IK (CSAnimNode_StrideScale)
		});
	}
}
