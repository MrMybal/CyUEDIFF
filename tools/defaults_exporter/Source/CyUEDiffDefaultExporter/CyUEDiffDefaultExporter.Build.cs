using UnrealBuildTool;
public class CyUEDiffDefaultExporter : ModuleRules {
    public CyUEDiffDefaultExporter(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json" });
    }
}
