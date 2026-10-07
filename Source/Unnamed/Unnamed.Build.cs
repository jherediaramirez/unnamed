using UnrealBuildTool;
using System.IO;

public class Unnamed : ModuleRules
{
    public Unnamed(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "InputCore", "Slate", "SlateCore", "WebBrowser", "HTTP", "Json", "ApplicationCore" });
        string WebRoot = Path.Combine(ModuleDirectory, "../../Content/Menu/Web");
        // CEF loads physical files, not assets inside the cooked container.
        foreach (string File in Directory.GetFiles(WebRoot, "*", SearchOption.AllDirectories))
        {
            RuntimeDependencies.Add(File, StagedFileType.NonUFS);
        }
    }
}
