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
    // Plants on (S8, the decided change), trees on (S9a): per preset, models on then off; recorded on S9a's first run
    // with trees in the world (S8 part B's, trees off: Docs/VoxelWorld.md, "Plants and patches").
    constexpr uint64 PlantGolden[3][2] = {{0xa2464eaac18b22fdull, 0x7b5243fd5b004f3cull},
        {0x66086c88ed3ef1edull, 0xd81de3b6e4744f0dull}, {0xf2ca4f1f2aeabd6dull, 0xca664062ca4022dcull}};
    for (const bool bPlants : {false, true})
        for (int32 Preset = 0; Preset < 3; ++Preset)
        {
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
                const uint64 Want = bPlants ? PlantGolden[Preset][bModels ? 0 : 1] : Golden[Preset];
                const TCHAR* Side = bPlants ? TEXT("plants on") : TEXT("plants off");
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
