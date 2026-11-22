using UnrealBuildTool;
using System.Collections.Generic;

public class CombatSimulatorEditorTarget : TargetRules
{
	public CombatSimulatorEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("CombatSimulator");
		// Dev-only fixed-camera capture driver (UncookedOnly: never in the Game target or a packaged build).
		ExtraModuleNames.Add("CSCapture");
	}
}
