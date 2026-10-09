using UnrealBuildTool;
using System.IO;

public class Unnamed : ModuleRules
{
    public Unnamed(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Project headers are included by their path from this folder, e.g. "Menu/MenuApplication.h".
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "EnhancedInput", "InputCore", "GameplayTags" });
        PrivateDependencyModuleNames.AddRange(new[] { "DeveloperSettings", "Slate", "SlateCore", "WebBrowser", "HTTP", "Json", "ApplicationCore", "RHI", "RenderCore" });
        // Block meshes are built at runtime; the editor-only block import decodes PNGs and registers new assets.
        PrivateDependencyModuleNames.AddRange(new[] { "MeshDescription", "StaticMeshDescription", "ImageWrapper", "AssetRegistry" });
        // The voxel world: its plain C++ core (Source/UnnamedVoxel) and the vendored chunk mesh component.
        PrivateDependencyModuleNames.AddRange(new[] { "UnnamedVoxel", "RealtimeMeshComponent" });
        string WebRoot = Path.Combine(ModuleDirectory, "../../Content/Menu/Web");
        // Regenerate build/staging metadata when the native preference contract changes.
        ExternalDependencies.Add(Path.Combine(WebRoot, "preferences-schema.menuprefs"));
        // CEF loads physical files, not assets inside the cooked container.
        foreach (string File in Directory.GetFiles(WebRoot, "*", SearchOption.AllDirectories))
        {
            RuntimeDependencies.Add(File, StagedFileType.NonUFS);
        }
        // The terrain reads the 26.3 worldgen data at runtime (VoxelWorldgen::GetReferenceRoot), so packages stage it at
        // the same place under the project folder: data/, and the biome parameter list the jar builds in code
        // (VoxelWorldgen::ReadBiomeParameters). The other facts and the tools are for tests and rebuilding.
        RuntimeDependencies.Add("$(ProjectDir)/Reference/minecraft-worldgen-26.3/data/...", StagedFileType.NonUFS);
        RuntimeDependencies.Add(
            "$(ProjectDir)/Reference/minecraft-worldgen-26.3/facts/biome_parameters.json", StagedFileType.NonUFS);
    }
}
