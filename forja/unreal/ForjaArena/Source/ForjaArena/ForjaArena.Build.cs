using UnrealBuildTool;
public class ForjaArena : ModuleRules
{
	public ForjaArena(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "NetCore",
			"GameplayAbilities", "GameplayTags", "GameplayTasks", "Niagara", "ForjaCore" });
	}
}
