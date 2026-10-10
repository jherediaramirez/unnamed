#include "Biome/VoxelBiomeClimate.h"
#include "Biome/VoxelBiomeStorage.h"
#include "Biome/VoxelBiomeTable.h"
#include "Biome/VoxelBiomeTints.h"
#include "Blocks/VoxelBlockTable.h"
#include "Dom/JsonObject.h"
#include "Meshing/VoxelMesher.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Storage/VoxelColumn.h"
#include "Surface/VoxelSurfaceEvaluator.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    constexpr uint64 TintTestSeed = 263;

    struct FTintTestWorld
    {
        FVoxelTerrainPreset Preset;
        FVoxelBiomeTable Biomes;
        FVoxelSurfaceInputs Surface;
    };

    bool TintTestRead(FAutomationTestBase& Test, FTintTestWorld& Out)
    {
        FString Error;
        const bool bRead = VoxelTerrainFactory::ReadInputs(
            VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), Out.Preset, Out.Biomes, Out.Surface, Error);
        return Test.TestTrue(FString::Printf(TEXT("overworld inputs read (%s)"), *Error), bRead);
    }

    bool TintTestLoadColors(FAutomationTestBase& Test, TSharedPtr<FJsonObject>& Out)
    {
        const FString Path =
            FPaths::ProjectDir() / TEXT("Docs/Records/2026-10-09-Voxel-Phase5-Scout-Evidence/blackbox/colors.json");
        FString Text;
        if (!Test.TestTrue(TEXT("colors.json read"), FFileHelper::LoadFileToString(Text, *Path))) return false;
        return Test.TestTrue(TEXT("colors.json parsed"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid());
    }

    // "#RRGGBB" to 0xRRGGBB.
    uint32 TintTestHex(const FString& Text)
    {
        return uint32(FCString::Strtoi(*Text.Mid(1), nullptr, 16));
    }

    FString TintTestHexText(uint32 Color)
    {
        return FString::Printf(TEXT("#%06X"), Color);
    }

    // Lookup of the literal rule: x = trunc((1 - t) * 255), y = trunc((1 - d * t) * 255).
    uint32 TintTestPixel(const TArray<uint32>& Map, double Temperature, double Downfall)
    {
        const int32 X = int32((1.0 - Temperature) * 255.0);
        const int32 Y = int32((1.0 - Downfall * Temperature) * 255.0);
        return Map[Y * 256 + X];
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTintColormapTest, "Unnamed.Voxel.Tints.Colormap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTintColormapTest::RunTest(const FString& Parameters)
{
    FTintTestWorld World;
    TSharedPtr<FJsonObject> Colors;
    if (!TintTestRead(*this, World) || !TintTestLoadColors(*this, Colors)) return false;
    TestEqual(TEXT("grass colormap pixels"), World.Biomes.GrassColormap.Num(), 65536);
    TestEqual(TEXT("foliage colormap pixels"), World.Biomes.FoliageColormap.Num(), 65536);
    if (World.Biomes.GrassColormap.Num() != 65536 || World.Biomes.FoliageColormap.Num() != 65536) return false;

    int32 Checked = 0, GrassMatches = 0, FoliageMatches = 0;
    for (const TSharedPtr<FJsonValue>& RowValue : Colors->GetArrayField(TEXT("grid")))
    {
        const TArray<TSharedPtr<FJsonValue>>& Row = RowValue->AsArray();
        const double Temperature = Row[0]->AsNumber(), Downfall = Row[1]->AsNumber();
        if (Temperature < 0 || Temperature > 1 || Downfall < 0 || Downfall > 1) continue;
        ++Checked;
        const uint32 Grass = FVoxelTintTable::LookupColormap(World.Biomes.GrassColormap, Temperature, Downfall);
        const uint32 Foliage = FVoxelTintTable::LookupColormap(World.Biomes.FoliageColormap, Temperature, Downfall);
        const uint32 WantGrass = TintTestHex(Row[2]->AsString()), WantFoliage = TintTestHex(Row[3]->AsString());
        GrassMatches += Grass == WantGrass;
        FoliageMatches += Foliage == WantFoliage;
        if (Grass != WantGrass)
            AddError(FString::Printf(TEXT("grass at (%g, %g): got %s, want %s"), Temperature, Downfall,
                *TintTestHexText(Grass), *TintTestHexText(WantGrass)));
        if (Foliage != WantFoliage)
            AddError(FString::Printf(TEXT("foliage at (%g, %g): got %s, want %s"), Temperature, Downfall,
                *TintTestHexText(Foliage), *TintTestHexText(WantFoliage)));
    }
    AddInfo(FString::Printf(TEXT("colormap grid rows checked %d, grass matches %d, foliage matches %d"), Checked,
        GrassMatches, FoliageMatches));
    TestTrue(TEXT("enough grid rows checked"), Checked >= 40);
    TestEqual(TEXT("grass matches"), GrassMatches, Checked);
    TestEqual(TEXT("foliage matches"), FoliageMatches, Checked);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTintBiomesTest, "Unnamed.Voxel.Tints.Biomes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTintBiomesTest::RunTest(const FString& Parameters)
{
    FTintTestWorld World;
    TSharedPtr<FJsonObject> Colors;
    if (!TintTestRead(*this, World) || !TintTestLoadColors(*this, Colors)) return false;
    FVoxelTintTable Tints;
    FString Error;
    const bool bInitialized = Tints.Initialize(World.Biomes, &Error);
    if (!TestTrue(FString::Printf(TEXT("tints initialized (%s)"), *Error), bInitialized)) return false;
    TestEqual(TEXT("one tint per biome"), Tints.Num(), World.Biomes.Biomes.Num());

    int32 Compared = 0, SkippedBiomes = 0, GrassCompared = 0;
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Colors->GetObjectField(TEXT("per_biome"))->Values)
    {
        const int32 Index = World.Biomes.FindBiome(FString(TEXT("minecraft:")) + Pair.Key);
        if (Index == INDEX_NONE)
        {
            ++SkippedBiomes;
            continue;
        }
        const TSharedPtr<FJsonObject> Want = Pair.Value->AsObject();
        const FVoxelBiomeTint& Tint = Tints.Get(Index);
        ++Compared;
        const uint32 WantWater = TintTestHex(Want->GetStringField(TEXT("water")));
        const uint32 WantFoliage = TintTestHex(Want->GetStringField(TEXT("foliage")));
        const uint32 WantGrass = TintTestHex(Want->GetStringField(TEXT("grass")));
        if (Tint.Water != WantWater)
            AddError(FString::Printf(
                TEXT("%s water %s, want %s"), *Pair.Key, *TintTestHexText(Tint.Water), *TintTestHexText(WantWater)));
        if (Tint.Foliage != WantFoliage)
            AddError(FString::Printf(TEXT("%s foliage %s, want %s"), *Pair.Key, *TintTestHexText(Tint.Foliage),
                *TintTestHexText(WantFoliage)));
        if (Tint.GrassModifier != EVoxelGrassColorModifier::Swamp)
        {
            ++GrassCompared;
            if (Tint.Grass != WantGrass)
                AddError(FString::Printf(TEXT("%s grass %s, want %s"), *Pair.Key, *TintTestHexText(Tint.Grass),
                    *TintTestHexText(WantGrass)));
        }
    }
    AddInfo(FString::Printf(TEXT("biomes compared %d (grass %d), skipped %d"), Compared, GrassCompared, SkippedBiomes));
    TestTrue(TEXT("most overworld biomes compared"), Compared >= 50);

    // Spot values from the jar, as literals: name, water, grass, foliage (-1: not checked).
    struct FSpot
    {
        const TCHAR* Name;
        int32 Water, Grass, Foliage;
    };
    const FSpot Spots[] = {
        {TEXT("plains"), 0x3F76E4, 0x91BD59, 0x77AB2F},
        {TEXT("dark_forest"), -1, 0x507A32, -1},
        {TEXT("swamp"), 0x617B64, -1, 0x6A7039},
        {TEXT("mangrove_swamp"), 0x3A7A6A, -1, 0x8DB127},
        {TEXT("badlands"), -1, 0x90814D, 0x9E814D},
        {TEXT("desert"), -1, 0xBFB755, 0xAEA42A},
        {TEXT("warm_ocean"), 0x43D5EE, -1, -1},
        {TEXT("lukewarm_ocean"), 0x45ADF2, -1, -1},
        {TEXT("cold_ocean"), 0x3D57D6, -1, -1},
        {TEXT("frozen_ocean"), 0x3938C9, 0x80B497, 0x60A17B},
        {TEXT("deep_frozen_ocean"), -1, 0x8EB971, -1},
        {TEXT("meadow"), 0x0E4ECF, -1, -1},
        {TEXT("cherry_grove"), -1, 0xB6DB61, 0xB6DB61},
    };
    int32 SpotsChecked = 0;
    for (const FSpot& Spot : Spots)
    {
        const int32 Index = World.Biomes.FindBiome(FString(TEXT("minecraft:")) + Spot.Name);
        if (!TestTrue(FString::Printf(TEXT("%s exists"), Spot.Name), Index != INDEX_NONE)) continue;
        const FVoxelBiomeTint& Tint = Tints.Get(Index);
        if (Spot.Water >= 0)
        {
            ++SpotsChecked;
            TestEqual(FString::Printf(TEXT("%s water"), Spot.Name), TintTestHexText(Tint.Water),
                TintTestHexText(uint32(Spot.Water)));
        }
        if (Spot.Grass >= 0)
        {
            ++SpotsChecked;
            TestEqual(FString::Printf(TEXT("%s grass"), Spot.Name), TintTestHexText(Tint.Grass),
                TintTestHexText(uint32(Spot.Grass)));
            TestEqual(FString::Printf(TEXT("%s GetColor grass"), Spot.Name),
                TintTestHexText(Tints.GetColor(EVoxelTintSource::Grass, Index, 5, -9, 0)),
                TintTestHexText(uint32(Spot.Grass)));
        }
        if (Spot.Foliage >= 0)
        {
            ++SpotsChecked;
            TestEqual(FString::Printf(TEXT("%s foliage"), Spot.Name), TintTestHexText(Tint.Foliage),
                TintTestHexText(uint32(Spot.Foliage)));
            TestEqual(FString::Printf(TEXT("%s GetColor foliage"), Spot.Name),
                TintTestHexText(Tints.GetColor(EVoxelTintSource::Foliage, Index, 5, -9, 0)),
                TintTestHexText(uint32(Spot.Foliage)));
        }
        if (Spot.Water >= 0)
            TestEqual(FString::Printf(TEXT("%s GetColor water"), Spot.Name),
                TintTestHexText(Tints.GetColor(EVoxelTintSource::Water, Index, 5, -9, 0)),
                TintTestHexText(uint32(Spot.Water)));
    }
    TestTrue(TEXT("spots checked"), SpotsChecked >= 20);

    // Sources that are not biome tints, and unknown biomes, give the fallback.
    TestEqual(TEXT("None gives the fallback"), Tints.GetColor(EVoxelTintSource::None, 0, 0, 0, 0x123456), 0x123456u);
    TestEqual(
        TEXT("Constant gives the fallback"), Tints.GetColor(EVoxelTintSource::Constant, 0, 0, 0, 0x123456), 0x123456u);
    TestEqual(TEXT("no biome gives the fallback"), Tints.GetColor(EVoxelTintSource::Grass, INDEX_NONE, 0, 0, 0x123456),
        0x123456u);

    // Dark forest: the modifier applied to the colormap colour at the biome's clamped climate.
    const int32 DarkForest = World.Biomes.FindBiome(TEXT("minecraft:dark_forest"));
    if (DarkForest != INDEX_NONE)
    {
        const FVoxelBiome& Biome = World.Biomes.Biomes[DarkForest];
        const uint32 Base = Biome.GrassColor != INDEX_NONE
            ? uint32(Biome.GrassColor)
            : TintTestPixel(World.Biomes.GrassColormap, FMath::Clamp(Biome.Temperature, 0.f, 1.f),
                  FMath::Clamp(Biome.Downfall, 0.f, 1.f));
        const uint32 Expected = ((Base & 0xFEFEFE) + 0x28340A) >> 1;
        TestEqual(TEXT("dark forest grass is the modifier applied to the colormap colour"),
            TintTestHexText(Tints.Get(DarkForest).Grass), TintTestHexText(Expected));
        TestEqual(TEXT("ApplyDarkForest formula"), FVoxelTintTable::ApplyDarkForest(0x91BD59), 0x5C7831u);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTintSwampTest, "Unnamed.Voxel.Tints.Swamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTintSwampTest::RunTest(const FString& Parameters)
{
    FTintTestWorld World;
    if (!TintTestRead(*this, World)) return false;
    FVoxelTintTable Tints;
    if (!TestTrue(TEXT("tints initialized"), Tints.Initialize(World.Biomes))) return false;
    const FVoxelBiomeClimate Climate;
    for (const TCHAR* Name : {TEXT("minecraft:swamp"), TEXT("minecraft:mangrove_swamp")})
    {
        const int32 Index = World.Biomes.FindBiome(Name);
        if (!TestTrue(FString::Printf(TEXT("%s exists"), Name), Index != INDEX_NONE)) continue;
        int32 Positions = 0, Dark = 0, Light = 0, Wrong = 0;
        for (int32 Y = -150; Y <= 150; Y += 2)
            for (int32 X = -150; X <= 150; X += 2)
            {
                const uint32 Expected = Climate.GetInfoNoise(X * 0.0225, Y * 0.0225) < -0.1 ? 0x4C763Cu : 0x6A7039u;
                const uint32 Got = Tints.GetColor(EVoxelTintSource::Grass, Index, X, Y, 0);
                ++Positions;
                Dark += Expected == 0x4C763Cu;
                Light += Expected == 0x6A7039u;
                if (Got != Expected && ++Wrong <= 3)
                    AddError(FString::Printf(TEXT("%s grass at (%d, %d): got %s, want %s"), Name, X, Y,
                        *TintTestHexText(Got), *TintTestHexText(Expected)));
            }
        AddInfo(FString::Printf(
            TEXT("%s: %d positions, %d dark, %d light, %d wrong"), Name, Positions, Dark, Light, Wrong));
        TestTrue(FString::Printf(TEXT("%s positions"), Name), Positions >= 20000);
        TestTrue(FString::Printf(TEXT("%s has dark grass"), Name), Dark > 0);
        TestTrue(FString::Printf(TEXT("%s has light grass"), Name), Light > 0);
        TestEqual(FString::Printf(TEXT("%s wrong"), Name), Wrong, 0);
        TestEqual(FString::Printf(TEXT("%s varies with position"), Name),
            Tints.VariesWithPosition(EVoxelTintSource::Grass, Index), true);
        TestEqual(FString::Printf(TEXT("%s foliage does not vary"), Name),
            Tints.VariesWithPosition(EVoxelTintSource::Foliage, Index), false);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTintBlocksTest, "Unnamed.Voxel.Tints.Blocks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTintBlocksTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    struct FExpect
    {
        const TCHAR* Name;
        EVoxelTintSource Source;
        int32 Tint;
    };
    const FExpect Expected[] = {
        {TEXT("grass_block"), EVoxelTintSource::Grass, 0x91BD59},
        {TEXT("oak_leaves"), EVoxelTintSource::Foliage, 0x77AB2F},
        {TEXT("spruce_leaves"), EVoxelTintSource::Constant, 0x619961},
        {TEXT("birch_leaves"), EVoxelTintSource::Constant, 0x80A755},
        {TEXT("water"), EVoxelTintSource::Water, -1},
        {TEXT("stone"), EVoxelTintSource::None, -1},
    };
    for (const FExpect& Want : Expected)
    {
        const uint16 State = Table->FindState(Want.Name);
        if (!TestTrue(FString::Printf(TEXT("%s exists"), Want.Name), State != 0)) continue;
        const FVoxelStateInfo& Info = Table->Get(State);
        TestEqual(FString::Printf(TEXT("%s tint source"), Want.Name), int32(Info.TintSource), int32(Want.Source));
        if (Want.Tint >= 0)
            TestEqual(FString::Printf(TEXT("%s tint"), Want.Name), TintTestHexText(Info.Tint),
                TintTestHexText(uint32(Want.Tint)));
    }
    const FVoxelStateInfo& Grass = Table->Get(Table->FindState(TEXT("grass_block")));
    TestTrue(TEXT("grass_block top carries the tint bit"),
        ((Grass.Faces[int32(EVoxelFace::Up)] | Grass.Faces[VoxelFaces::OverlayIndex]) & (1u << 13)) != 0);
    return true;
}

namespace
{
    using FTintColumns = TMap<FIntPoint, TSharedRef<FVoxelColumn>>;

    int32 TintFloorDiv16(int32 Value)
    {
        return Value >> 4;
    }

    // The colour of a face of the block at world (X, Y, Z), vanilla's biome blend: the mean of the per-block colours
    // over the (2R + 1)^2 blocks around it, read straight from the columns. A block in a column missing from Columns
    // takes the biome of the nearest block of the meshed column Centre.
    uint32 TintExpected(const FTintColumns& Columns, FIntPoint Centre, const FVoxelTintTable& Tints,
        EVoxelTintSource Source, int32 WorldX, int32 WorldY, int32 Z, int32 Radius, uint32 Fallback)
    {
        uint32 Sum[3] = {};
        for (int32 Y = WorldY - Radius; Y <= WorldY + Radius; ++Y)
            for (int32 X = WorldX - Radius; X <= WorldX + Radius; ++X)
            {
                const TSharedRef<FVoxelColumn>* Column = Columns.Find({TintFloorDiv16(X), TintFloorDiv16(Y)});
                int32 Biome;
                if (Column)
                    Biome = (*Column)->GetBiome(X - TintFloorDiv16(X) * 16, Y - TintFloorDiv16(Y) * 16, Z);
                else
                    Biome = Columns.FindChecked(Centre)->GetBiome(
                        FMath::Clamp(X - Centre.X * 16, 0, 15), FMath::Clamp(Y - Centre.Y * 16, 0, 15), Z);
                const uint32 Color = Tints.GetColor(Source, Biome, X, Y, Fallback);
                Sum[0] += Color >> 16;
                Sum[1] += (Color >> 8) & 0xFF;
                Sum[2] += Color & 0xFF;
            }
        const uint32 Count = uint32((2 * Radius + 1) * (2 * Radius + 1));
        return (Sum[0] / Count) << 16 | (Sum[1] / Count) << 8 | Sum[2] / Count;
    }

    struct FTintQuadResult
    {
        int32 Checked = 0, Mismatches = 0, Border = 0, Tinted = 0;
        // Checked cells whose blend reaches into a column missing from the map.
        int32 MissingReach = 0;
        FString FirstError;
    };

    // Checks every cell of every up quad of a tinted-source block in the column at Centre. Radius < 0: each state's
    // own plains tint. Border counts the checked cells whose blend-Radius colour differs from the block's own.
    FTintQuadResult TintCheckUpQuads(const TArray<FVoxelMeshData>& Meshes, const FTintColumns& Columns,
        FIntPoint Centre, const FVoxelTintTable& Tints, const FVoxelBlockTable& Table, int32 Radius)
    {
        FTintQuadResult Result;
        const FVoxelColumn& Column = *Columns.FindChecked(Centre);
        for (const FVoxelMeshData& Mesh : Meshes)
            for (const FVoxelMeshLayer& Layer : Mesh.Layers)
                for (int32 Quad = 0; Quad < Layer.GetQuadCount(); ++Quad)
                {
                    const int32 First = Quad * 4;
                    if (Layer.TintFaces[First].Y != 5.f) continue;
                    float MinX = Layer.Positions[First].X, MaxX = MinX, MinY = Layer.Positions[First].Y, MaxY = MinY;
                    for (int32 Corner = 1; Corner < 4; ++Corner)
                    {
                        MinX = FMath::Min(MinX, Layer.Positions[First + Corner].X);
                        MaxX = FMath::Max(MaxX, Layer.Positions[First + Corner].X);
                        MinY = FMath::Min(MinY, Layer.Positions[First + Corner].Y);
                        MaxY = FMath::Max(MaxY, Layer.Positions[First + Corner].Y);
                    }
                    const int32 Z = FMath::CeilToInt(Layer.Positions[First].Z) - 1;
                    for (int32 Y = FMath::RoundToInt(MinY); Y < FMath::RoundToInt(MaxY); ++Y)
                        for (int32 X = FMath::RoundToInt(MinX); X < FMath::RoundToInt(MaxX); ++X)
                        {
                            const FVoxelStateInfo& Info = Table.Get(Column.GetState(X, Y, Z));
                            if (Info.TintSource != EVoxelTintSource::Grass &&
                                Info.TintSource != EVoxelTintSource::Foliage &&
                                Info.TintSource != EVoxelTintSource::Water)
                                continue;
                            ++Result.Checked;
                            const bool bTinted =
                                ((Info.Faces[int32(EVoxelFace::Up)] | Info.Faces[VoxelFaces::OverlayIndex]) &
                                    VoxelFaces::TintBit) != 0;
                            Result.Tinted += bTinted;
                            const int32 WorldX = Centre.X * 16 + X, WorldY = Centre.Y * 16 + Y;
                            const uint32 Expected = Radius < 0 || !bTinted
                                ? Info.Tint
                                : TintExpected(
                                      Columns, Centre, Tints, Info.TintSource, WorldX, WorldY, Z, Radius, Info.Tint);
                            if (Radius > 0 && bTinted &&
                                Expected !=
                                    TintExpected(
                                        Columns, Centre, Tints, Info.TintSource, WorldX, WorldY, Z, 0, Info.Tint))
                                ++Result.Border;
                            if (Radius > 0 && bTinted)
                                for (const FIntPoint Corner : {FIntPoint(WorldX - Radius, WorldY - Radius),
                                         FIntPoint(WorldX + Radius, WorldY - Radius),
                                         FIntPoint(WorldX - Radius, WorldY + Radius),
                                         FIntPoint(WorldX + Radius, WorldY + Radius)})
                                    if (!Columns.Contains({TintFloorDiv16(Corner.X), TintFloorDiv16(Corner.Y)}))
                                    {
                                        ++Result.MissingReach;
                                        break;
                                    }
                            const uint32 Got = uint32(Layer.TintFaces[First].X);
                            if (Got != Expected)
                            {
                                if (!Result.Mismatches++)
                                    Result.FirstError = FString::Printf(
                                        TEXT("cell (%d, %d, %d) source %d got %s want %s"), WorldX, WorldY, Z,
                                        int32(Info.TintSource), *TintTestHexText(Got), *TintTestHexText(Expected));
                            }
                        }
                }
        return Result;
    }

    TArray<FVoxelMeshData> TintMeshColumn(const FTintColumns& Columns, FIntPoint Centre, const FVoxelBlockTable& Table,
        const FVoxelMeshOptions& Options, const FVoxelTintTable* Tints)
    {
        FVoxelNeighborhood Neighborhood;
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
            {
                const TSharedRef<FVoxelColumn>* Found = Columns.Find(Centre + FIntPoint(DX, DY));
                Neighborhood.Set(DX, DY, Found ? &Found->Get() : nullptr);
            }
        TArray<FVoxelMeshData> Meshes;
        Meshes.SetNum(VoxelConstants::SectionCount);
        TUniquePtr<FVoxelPaddedSection> Padded = MakeUnique<FVoxelPaddedSection>();
        for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
        {
            VoxelMesher::GatherPadded(Neighborhood, Section, *Padded);
            VoxelMesher::MeshSection(Table, *Padded, Section, Options, Meshes[Section], Tints, &Neighborhood);
        }
        return Meshes;
    }

    bool TintMeshesEqual(const TArray<FVoxelMeshData>& A, const TArray<FVoxelMeshData>& B)
    {
        if (A.Num() != B.Num()) return false;
        for (int32 Section = 0; Section < A.Num(); ++Section)
            for (int32 Layer = 0; Layer < int32(EVoxelLayer::Count); ++Layer)
            {
                const FVoxelMeshLayer& LA = A[Section].Layers[Layer];
                const FVoxelMeshLayer& LB = B[Section].Layers[Layer];
                if (LA.Positions != LB.Positions || LA.TintFaces != LB.TintFaces) return false;
            }
        return true;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTintMesherTest, "Unnamed.Voxel.Tints.Mesher",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTintMesherTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    FTintTestWorld World;
    if (!TintTestRead(*this, World)) return false;
    const FVoxelDensityTerrain Terrain(
        World.Preset, TintTestSeed, *Table, FVoxelDensityTerrainSettings(), &World.Biomes, &World.Surface);
    if (!TestTrue(FString::Printf(TEXT("terrain is valid (%s)"), *Terrain.GetError()), Terrain.IsValid())) return false;
    const FVoxelTintTable* Tints = Terrain.GetTints();
    if (!TestNotNull(TEXT("terrain tints"), Tints)) return false;

    FTintColumns Columns;
    const auto Ensure = [&](FIntPoint Position)
    {
        if (Columns.Contains(Position)) return;
        TSharedRef<FVoxelColumn> Column = MakeShared<FVoxelColumn>(Position);
        TArray<uint16> Dense;
        Terrain.Generate(Position, *Column, Dense);
        Columns.Add(Position, Column);
    };

    FVoxelMeshOptions Blend2, Blend0, Off, NoShortcut;
    Blend2.TintBlend = 2;
    Blend0.TintBlend = 0;
    Off.TintBlend = -1;
    NoShortcut.TintBlend = 2;
    NoShortcut.bTintShortcut = false;

    // Scan for a centre column with a biome border: at least 50 tinted up cells whose blend-2 colour differs.
    bool bFound = false;
    FIntPoint Centre = FIntPoint::ZeroValue;
    FTintQuadResult Found2;
    int32 Scanned = 0;
    for (int32 K = 0; K < 120 && !bFound; ++K)
    {
        const FIntPoint Candidate(K * 5 - 30, (K * 11) % 60 - 30);
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
                Ensure(Candidate + FIntPoint(DX, DY));
        ++Scanned;
        const TArray<FVoxelMeshData> Meshes = TintMeshColumn(Columns, Candidate, *Table, Blend2, Tints);
        const FTintQuadResult Result = TintCheckUpQuads(Meshes, Columns, Candidate, *Tints, *Table, 2);
        if (Result.Border >= 50)
        {
            bFound = true;
            Centre = Candidate;
            Found2 = Result;
        }
    }
    if (!TestTrue(TEXT("a biome border column found"), bFound)) return false;
    AddInfo(FString::Printf(TEXT("centre column (%d, %d) after %d candidates"), Centre.X, Centre.Y, Scanned));
    TestEqual(TEXT("centre position"), Columns.FindChecked(Centre)->GetPosition(), Centre);
    TestFalse(TEXT("centre has biomes"), Columns.FindChecked(Centre)->GetBiomes().IsEmpty());

    const TArray<FVoxelMeshData> Meshes2 = TintMeshColumn(Columns, Centre, *Table, Blend2, Tints);
    const TArray<FVoxelMeshData> Meshes0 = TintMeshColumn(Columns, Centre, *Table, Blend0, Tints);
    const TArray<FVoxelMeshData> MeshesOff = TintMeshColumn(Columns, Centre, *Table, Off, Tints);
    const TArray<FVoxelMeshData> MeshesDirect = TintMeshColumn(Columns, Centre, *Table, NoShortcut, Tints);

    const FTintQuadResult R2 = TintCheckUpQuads(Meshes2, Columns, Centre, *Tints, *Table, 2);
    const FTintQuadResult R0 = TintCheckUpQuads(Meshes0, Columns, Centre, *Tints, *Table, 0);
    const FTintQuadResult ROff = TintCheckUpQuads(MeshesOff, Columns, Centre, *Tints, *Table, -1);
    const FTintQuadResult RDirect = TintCheckUpQuads(MeshesDirect, Columns, Centre, *Tints, *Table, 2);
    AddInfo(FString::Printf(TEXT("cells checked: blend2 %d (tinted %d, border %d), blend0 %d, off %d, direct %d"),
        R2.Checked, R2.Tinted, R2.Border, R0.Checked, ROff.Checked, RDirect.Checked));
    TestTrue(TEXT("blend 2 border cells"), R2.Border >= 50);
    TestTrue(TEXT("blend 2 tinted cells checked"), R2.Tinted >= 50);
    TestTrue(TEXT("blend 0 cells checked"), R0.Checked >= 50);
    TestTrue(TEXT("blend off cells checked"), ROff.Checked >= 50);
    TestEqual(TEXT("blend 2 mismatches: ") + R2.FirstError, R2.Mismatches, 0);
    TestEqual(TEXT("blend 0 mismatches: ") + R0.FirstError, R0.Mismatches, 0);
    TestEqual(TEXT("blend off mismatches: ") + ROff.FirstError, ROff.Mismatches, 0);
    TestEqual(TEXT("no-shortcut mismatches: ") + RDirect.FirstError, RDirect.Mismatches, 0);
    TestTrue(TEXT("no shortcut gives identical meshes"), TintMeshesEqual(Meshes2, MeshesDirect));
    TestFalse(TEXT("blend 0 differs from blend 2 at a border"), TintMeshesEqual(Meshes2, Meshes0));

    // A missing neighbour (the edge of a finite area): blocks there take the centre column's nearest block.
    FTintColumns Partial = Columns;
    Partial.Remove(Centre + FIntPoint(1, 0));
    Partial.Remove(Centre + FIntPoint(0, -1));
    const TArray<FVoxelMeshData> MeshesEdge = TintMeshColumn(Partial, Centre, *Table, Blend2, Tints);
    const FTintQuadResult REdge = TintCheckUpQuads(MeshesEdge, Partial, Centre, *Tints, *Table, 2);
    AddInfo(FString::Printf(
        TEXT("missing east and north neighbours: %d cells checked, %d reach them"), REdge.Checked, REdge.MissingReach));
    TestTrue(TEXT("missing-neighbour cells checked"), REdge.Checked >= 50);
    TestTrue(TEXT("cells whose blend reaches a missing column"), REdge.MissingReach >= 16);
    TestEqual(TEXT("missing-neighbour mismatches: ") + REdge.FirstError, REdge.Mismatches, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTintDryFoliageTest, "Unnamed.Voxel.Tints.DryFoliage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTintDryFoliageTest::RunTest(const FString& Parameters)
{
    // Leaf litter's colours: the dry_foliage colormap read like grass (plains #A37546, the block reference's README),
    // and a biome's own dry_foliage_color where it sets one (swamp #7B5334 in its biome file).
    FTintTestWorld World;
    if (!TintTestRead(*this, World)) return false;
    if (!TestEqual(TEXT("dry foliage colormap pixels"), World.Biomes.DryFoliageColormap.Num(), 65536)) return false;
    TestEqual(TEXT("plains on the colormap"),
        TintTestHexText(FVoxelTintTable::LookupColormap(World.Biomes.DryFoliageColormap, .8, .4)),
        TintTestHexText(0xA37546));
    FVoxelTintTable Tints;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("the tint table builds (%s)"), *Error), Tints.Initialize(World.Biomes, &Error)))
        return false;
    const int32 Plains = World.Biomes.FindBiome(TEXT("minecraft:plains"));
    const int32 Swamp = World.Biomes.FindBiome(TEXT("minecraft:swamp"));
    if (!TestTrue(TEXT("plains and swamp are in the table"), Plains != INDEX_NONE && Swamp != INDEX_NONE)) return false;
    TestEqual(TEXT("plains"), TintTestHexText(Tints.GetColor(EVoxelTintSource::DryFoliage, Plains, 0, 0, 0)),
        TintTestHexText(0xA37546));
    TestEqual(TEXT("swamp's own colour"), TintTestHexText(Tints.GetColor(EVoxelTintSource::DryFoliage, Swamp, 0, 0, 0)),
        TintTestHexText(0x7B5334));
    // Without the colormap, a biome without its own colour keeps the fallback (the block's plains tint).
    World.Biomes.DryFoliageColormap.Reset();
    FVoxelTintTable Bare;
    Bare.Initialize(World.Biomes);
    TestEqual(TEXT("no colormap: the fallback"), Bare.GetColor(EVoxelTintSource::DryFoliage, Plains, 0, 0, 0x123456),
        uint32(0x123456));
    return true;
}

#endif
