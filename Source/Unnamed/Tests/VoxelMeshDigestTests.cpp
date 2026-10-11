#include "Biome/VoxelBiomeSource.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Generation/VoxelArea.h"
#include "Hash/CityHash.h"
#include "Misc/AutomationTest.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    const TCHAR* const MeshDigestPresets[3] = {TEXT("overworld"), TEXT("large_biomes"), TEXT("amplified")};

    // Every byte the mesher wrote for an area, in section order: each non-empty layer's index, place and arrays.
    uint64 MeshDigestOf(const FVoxelArea& Area, int64& OutQuads, SIZE_T& OutBytes)
    {
        uint64 Hash = 0;
        OutQuads = 0;
        OutBytes = 0;
        auto Add = [&Hash, &OutBytes](const void* Data, SIZE_T Size)
        {
            Hash = CityHash64WithSeed(static_cast<const char*>(Data), uint32(Size), Hash);
            OutBytes += Size;
        };
        for (const FVoxelArea::FSectionMesh& Section : Area.Meshes)
            for (int32 Index = 0; Index < int32(EVoxelLayer::Count); ++Index)
            {
                const FVoxelMeshLayer& Layer = Section.Mesh.Layers[Index];
                if (Layer.IsEmpty()) continue;
                const int32 Place[3] = {Index, Section.Column, Section.Section};
                Hash = CityHash64WithSeed(reinterpret_cast<const char*>(Place), sizeof(Place), Hash);
                Add(Layer.Positions.GetData(), Layer.Positions.Num() * sizeof(FVector3f));
                Add(Layer.BlockUVs.GetData(), Layer.BlockUVs.Num() * sizeof(FVector2f));
                Add(Layer.Codes.GetData(), Layer.Codes.Num() * sizeof(FVector2f));
                Add(Layer.TintFaces.GetData(), Layer.TintFaces.Num() * sizeof(FVector2f));
                Add(Layer.Colors.GetData(), Layer.Colors.Num() * sizeof(FColor));
                Add(Layer.Indices.GetData(), Layer.Indices.Num() * sizeof(uint32));
                OutQuads += Layer.GetQuadCount();
            }
        return Hash;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelMeshDigestTest, "Unnamed.Voxel.Mesher.Digest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelMeshDigestTest::RunTest(const FString& Parameters)
{
    // The decorated radius-4 area of seed 263 per preset, meshed with the default options and with voxel.Models 0: its
    // mesh bytes are pinned, so a mesher change that should not touch generated worlds proves it. Generated worlds hold
    // no model states yet, so both match the mesh from before the block models.
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    // Plants off: recorded at adee7a6 (before the block models), per preset; models on and off alike.
    constexpr uint64 Golden[3] = {0xa8ba8d0d0a39174full, 0xe49601da349c7c0dull, 0x7f9491318204f4b2ull};
    // Plants and trees on: per preset, models on then off. Re-recorded in S9b: full-cube faces merge by look
    // (FVoxelBlockTable::GetLookState), so leaves of different distances merge; the blocks are S9a's
    // (Decoration.Golden's third case is unchanged) and the trees-off case below is S8 part B's.
    // S9c re-recorded every pair but large_biomes models off (tree decorators, mega spruce and pine); S9d part 2 the
    // overworld and amplified pairs (fallen trees, huge mushrooms, trunk_vine at 2/3, the shelf mushrooms' one rule);
    // S9e part 1 the large_biomes pair (azalea trees with their root systems over a lush cave).
    constexpr uint64 PlantGolden[3][2] = {{0xdc44febc46d1c73aull, 0xa0ea785555d107d2ull},
        {0x0ea42a420b0a2f83ull, 0x8b29db68a9ad618dull}, {0x61c8745ba36e2004ull, 0x6fd8255fd190e1caull}};
    // Plants on, trees off: S8 part B's, which must hold whatever the trees change (Docs/VoxelWorld.md, "Plants and
    // patches").
    constexpr uint64 TreelessGolden[3][2] = {{0xd8f3b39ed6d66f5aull, 0x6b673e48bda8a0b7ull},
        {0xd8c297a251e3c4b1ull, 0x81cbb033a8f2c493ull}, {0x9719bc3f651df81bull, 0xa0fbd505d2c267f9ull}};
    // Cases: plants off; plants on, trees off; both on.
    for (int32 Case = 0; Case < 3; ++Case)
        for (int32 Preset = 0; Preset < 3; ++Preset)
        {
            const bool bPlants = Case > 0, bTrees = Case == 2;
            FString Error;
            FVoxelTerrainPreset TerrainPreset;
            FVoxelBiomeTable Biomes;
            FVoxelSurfaceInputs Surface;
            FVoxelFeatureGraph Graph;
            if (!TestTrue(FString::Printf(TEXT("%s inputs read (%s)"), MeshDigestPresets[Preset], *Error),
                    VoxelTerrainFactory::ReadInputs(VoxelWorldgen::GetReferenceRoot(), MeshDigestPresets[Preset],
                        TerrainPreset, Biomes, Surface, Error)) ||
                !TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error),
                    VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error)))
                continue;
            const TSharedRef<FVoxelDensityTerrain> Terrain = MakeShared<FVoxelDensityTerrain>(
                TerrainPreset, 263, *Table, FVoxelDensityTerrainSettings(), &Biomes, &Surface);
            FVoxelDecorator Decorator;
            FVoxelDecorationOptions DecorationOptions;
            DecorationOptions.bPlants = bPlants;
            DecorationOptions.bTrees = bTrees;
            if (!TestTrue(TEXT("the terrain is valid"), Terrain->IsValid()) ||
                !TestTrue(FString::Printf(TEXT("the decorator compiles (%s)"), *Error),
                    Decorator.Initialize(Graph, *Table, Terrain->GetBiomeSource()->GetTable(), Terrain->GetSeed(),
                        DecorationOptions, &Error)))
                continue;
            for (const bool bModels : {true, false})
            {
                FVoxelMeshOptions Options;
                Options.bModels = bModels;
                FVoxelArea Area;
                FVoxelArea::Build(*Table, *Terrain, FIntPoint::ZeroValue, 4, Options, false, Area, &Decorator);
                int64 Quads = 0;
                SIZE_T Bytes = 0;
                const uint64 Digest = MeshDigestOf(Area, Quads, Bytes);
                const uint64 Want = Case == 2 ? PlantGolden[Preset][bModels ? 0 : 1]
                    : Case == 1               ? TreelessGolden[Preset][bModels ? 0 : 1]
                                              : Golden[Preset];
                const TCHAR* Side = Case == 2 ? TEXT("plants and trees on")
                    : Case == 1               ? TEXT("plants on, trees off")
                                              : TEXT("plants off");
                AddInfo(FString::Printf(TEXT("%s, %s, models %s: mesh digest 0x%016llx, %lld quads, %llu bytes"),
                    MeshDigestPresets[Preset], Side, bModels ? TEXT("on") : TEXT("off"), Digest, Quads, uint64(Bytes)));
                TestTrue(FString::Printf(TEXT("%s, %s, models %s: mesh digest 0x%016llx is the golden 0x%016llx"),
                             MeshDigestPresets[Preset], Side, bModels ? TEXT("on") : TEXT("off"), Digest, Want),
                    Digest == Want);
            }
        }
    return true;
}

#endif
