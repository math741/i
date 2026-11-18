using UnrealBuildTool;
// Servidor dedicado (requer engine compilada a partir do código-fonte para alvos Server).
public class ForjaArenaServerTarget : TargetRules
{
	public ForjaArenaServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "ForjaCore", "ForjaArena" });
	}
}
