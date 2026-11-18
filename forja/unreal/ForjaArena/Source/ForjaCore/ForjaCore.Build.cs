using System.IO;
using UnrealBuildTool;
// Módulo que compila o MESMO núcleo C++ testado fora da engine (forja/core/forja/*.cpp).
public class ForjaCore : ModuleRules
{
	public ForjaCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = false;
		bUseRTTI = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
		PublicIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../core")));
	}
}
