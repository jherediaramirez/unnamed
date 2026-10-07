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
        string WebRoot = Path.Combine(ModuleDirectory, "../../Content/Menu/Web");
        // Regenerate build/staging metadata when the native preference contract changes.
        ExternalDependencies.Add(Path.Combine(WebRoot, "preferences-schema.menuprefs"));
        // CEF loads physical files, not assets inside the cooked container.
        foreach (string File in Directory.GetFiles(WebRoot, "*", SearchOption.AllDirectories))
        {
            RuntimeDependencies.Add(File, StagedFileType.NonUFS);
        }
    }
}
