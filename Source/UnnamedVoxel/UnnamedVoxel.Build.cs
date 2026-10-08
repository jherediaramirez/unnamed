using UnrealBuildTool;

public class UnnamedVoxel : ModuleRules
{
    public UnnamedVoxel(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Headers are included by their path from this folder, e.g. "Random/VoxelRandom.h".
        PublicIncludePaths.Add(ModuleDirectory);
        // The voxel core is plain C++ that any thread and any test can run. Core is its only dependency, so UObjects,
        // actors and render state cannot be included here; the Unreal side lives in Source/Unnamed/Voxel.
        PublicDependencyModuleNames.Add("Core");
        // The same seed must build the same blocks everywhere. By default the editor compiles /fp:precise but a game
        // target /fp:fast, which would let a packaged build round differently from the editor.
        FPSemantics = FPSemanticsMode.Precise;
    }
}
