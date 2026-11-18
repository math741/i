using UnrealBuildTool;
public class ForjaArenaTarget : TargetRules
{
	public ForjaArenaTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "ForjaCore", "ForjaArena" });
	}
}
