using UnrealBuildTool;
public class CyUEDiffReference : ModuleRules {
    public CyUEDiffReference(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "Json", "RenderCore", "RHI", "MeshDescription", "StaticMeshDescription" });
    }
}
