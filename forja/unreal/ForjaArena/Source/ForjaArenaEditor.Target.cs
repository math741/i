using UnrealBuildTool;
public class ForjaArenaEditorTarget : TargetRules
{
	public ForjaArenaEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "ForjaCore", "ForjaArena" });
	}
}
