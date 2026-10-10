#include "Biome/VoxelBiomeSource.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2DArray.h"
#include "Foundation/FoundationBlockImport.h"
#include "Generation/VoxelArea.h"
#include "Materials/MaterialInterface.h"
#include "Meshing/VoxelMesher.h"
#include "Meshing/VoxelOreLooks.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "UnnamedProjectSettings.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    constexpr EAutomationTestFlags OreLookTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    TSharedPtr<FJsonObject> ReadOreLookManifest()
    {
        FString Text;
        TSharedPtr<FJsonObject> Manifest;
        if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir() / TEXT("Art/OreLooks/manifest.json"))))
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest);
        return Manifest;
    }

    // Every vertex's (overlay code, look word) across two meshings of one area, and the faces' total area per layer.
    struct FOreLookMeshStats
    {
        int64 Quads = 0;
        int64 LookVertices = 0;
        double Area[int32(EVoxelLayer::Count)] = {};
    };

    FOreLookMeshStats GetOreLookStats(const FVoxelArea& Area)
    {
        FOreLookMeshStats Stats;
        for (const FVoxelArea::FSectionMesh& Section : Area.Meshes)
            for (int32 Layer = 0; Layer < int32(EVoxelLayer::Count); ++Layer)
            {
                const FVoxelMeshLayer& Mesh = Section.Mesh.Layers[Layer];
                Stats.Quads += Mesh.GetQuadCount();
                for (int32 Quad = 0; Quad < Mesh.GetQuadCount(); ++Quad)
                {
                    const FVector3f* P = Mesh.Positions.GetData() + Quad * 4;
                    Stats.Area[Layer] += ((P[1] - P[0]) ^ (P[3] - P[0])).Size();
                }
                for (const FVector2f& Codes : Mesh.Codes)
                    Stats.LookVertices += (uint32(Codes.Y) & (3u << 14)) == VoxelOreLooks::LookBit;
            }
        return Stats;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelOreLookClassesTest, "Unnamed.Voxel.OreLooks.Classes", OreLookTestFlags)

bool FVoxelOreLookClassesTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    const auto Get = [&Table](const TCHAR* Block) { return Table->Get(Table->FindState(Block)); };
    TestEqual(TEXT("coal_ore: coal on stone"), int32(Get(TEXT("coal_ore")).Ore), 1);
    TestEqual(TEXT("deepslate_diamond_ore: diamond on deepslate"), int32(Get(TEXT("deepslate_diamond_ore")).Ore),
        1 + 7 * 2 + 1);
    TestEqual(TEXT("copper_ore: copper on stone"), int32(Get(TEXT("copper_ore")).Ore), 1 + 2 * 2);
    const TPair<const TCHAR*, int32> Rocks[] = {{TEXT("stone"), 1}, {TEXT("deepslate"), 1}, {TEXT("granite"), 2},
        {TEXT("diorite"), 3}, {TEXT("andesite"), 4}, {TEXT("tuff"), 5}, {TEXT("polished_granite"), 0},
        {TEXT("cobblestone"), 0}, {TEXT("coal_ore"), 0}};
    for (const TPair<const TCHAR*, int32>& Rock : Rocks)
        TestEqual(FString::Printf(TEXT("%s's rock class"), Rock.Key), int32(Get(Rock.Key).Rock), Rock.Value);
    // 16 ore blocks; the two redstone ores have a lit and an unlit state each. Every state of a classed block is.
    int32 OreStates = 0, LookLike = 0;
    TSet<int32> OreClasses;
    for (int32 State = 0; State < Table->GetStateCount(); ++State)
    {
        // The material tells a look word by bit 14 without the overlay's present bit 15: no real code may look so.
        LookLike += (Table->Get(uint16(State)).Faces[VoxelFaces::OverlayIndex] & (3u << 14)) == VoxelOreLooks::LookBit;
        if (const uint8 Ore = Table->Get(uint16(State)).Ore)
        {
            ++OreStates;
            OreClasses.Add(Ore);
            TestEqual(
                TEXT("an ore state has no overlay"), Table->Get(uint16(State)).Faces[VoxelFaces::OverlayIndex], 0u);
        }
    }
    TestEqual(TEXT("overlay codes that read as a look word"), LookLike, 0);
    TestEqual(TEXT("ore states"), OreStates, 18);
    TestEqual(TEXT("ore classes"), OreClasses.Num(), 16);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelOreLookHostTest, "Unnamed.Voxel.OreLooks.Host", OreLookTestFlags)

bool FVoxelOreLookHostTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    const uint16 Ore = Table->FindState(TEXT("iron_ore")), Stone = Table->FindState(TEXT("stone")),
                 Granite = Table->FindState(TEXT("granite")), Diorite = Table->FindState(TEXT("diorite")),
                 Tuff = Table->FindState(TEXT("tuff")), Deepslate = Table->FindState(TEXT("deepslate"));
    constexpr int32 Size = FVoxelPaddedSection::Size;
    const int32 Center = FVoxelPaddedSection::GetIndex(8, 8, 8);
    const int32 Faces[6] = {-1, 1, -Size, Size, -Size * Size, Size * Size};
    struct FCase
    {
        const TCHAR* Name;
        uint16 FaceStates[6];
        // Applied to every edge and corner neighbour when nonzero, after the face states.
        uint16 Others;
        int32 Expected;
    };
    const FCase Cases[] = {
        {TEXT("six granite"), {Granite, Granite, Granite, Granite, Granite, Granite}, 0, 1},
        {TEXT("three granite, three stone: a tie keeps vanilla"), {Granite, Stone, Granite, Stone, Granite, Stone}, 0,
            0},
        {TEXT("two granite, two diorite: a tie keeps vanilla"), {Granite, Diorite, Granite, Diorite, 0, 0}, 0, 0},
        {TEXT("four stone, two granite: the base wins"), {Stone, Stone, Stone, Stone, Granite, Granite}, 0, 0},
        {TEXT("deepslate counts as the base"), {Deepslate, Deepslate, Tuff, 0, 0, 0}, 0, 0},
        {TEXT("two granite, one diorite, three air"), {Granite, Granite, Diorite, 0, 0, 0}, 0, 1},
        {TEXT("one granite among ores"), {Granite, Ore, Ore, Ore, Ore, Ore}, 0, 1},
        {TEXT("no rock on the faces: the edges decide"), {Ore, Ore, 0, 0, Ore, 0}, Tuff, 4},
        {TEXT("no rock anywhere"), {0, 0, 0, 0, 0, 0}, 0, 0},
        {TEXT("a rock face beats the edges"), {Diorite, 0, 0, 0, 0, 0}, Tuff, 2},
    };
    for (const FCase& Case : Cases)
    {
        FVoxelPaddedSection Padded;
        FMemory::Memzero(Padded.States);
        if (Case.Others)
            for (int32 DZ = -1; DZ <= 1; ++DZ)
                for (int32 DY = -1; DY <= 1; ++DY)
                    for (int32 DX = -1; DX <= 1; ++DX)
                        Padded.States[Center + (DZ * Size + DY) * Size + DX] = Case.Others;
        Padded.States[Center] = Ore;
        for (int32 Face = 0; Face < 6; ++Face)
            Padded.States[Center + Faces[Face]] = Case.FaceStates[Face];
        TestEqual(Case.Name, VoxelOreLooks::PickHost(*Table, Padded.States, Center), Case.Expected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelOreLookWordsTest, "Unnamed.Voxel.OreLooks.Words", OreLookTestFlags)

bool FVoxelOreLookWordsTest::RunTest(const FString& Parameters)
{
    using namespace VoxelOreLooks;
    // Iron on deepslate: class 1 + 1 * 2 + 1.
    const uint8 Iron = 4;
    const int32 OreBase = Iron - 1;
    for (int32 Host = 0; Host < HostCount; ++Host)
        TestEqual(FString::Printf(TEXT("both switches 0, host %d: vanilla"), Host), GetLookWord(Iron, Host, 0, 0), 0u);
    TestEqual(TEXT("not an ore: vanilla"), GetLookWord(0, 1, 1, 1), 0u);
    TestEqual(TEXT("OreHost 1 on the base: vanilla"), GetLookWord(Iron, 0, 1, 0), 0u);
    TestEqual(
        TEXT("OreHost 1 in tuff"), GetLookWord(Iron, 4, 1, 0), LookBit | uint32(GetSlot(EMode::Composite, 4, OreBase)));
    TestEqual(
        TEXT("OreHost 2 in tuff"), GetLookWord(Iron, 4, 2, 0), LookBit | uint32(GetSlot(EMode::Swap, 4, OreBase)));
    TestEqual(TEXT("OreGlint 1 alone: the base with ore mips"), GetLookWord(Iron, 4, 0, 1),
        LookBit | 1u << LevelShift | uint32(GetSlot(EMode::CompositeOreMips, 0, OreBase)));
    TestEqual(TEXT("OreHost 1 and OreGlint 1"), GetLookWord(Iron, 4, 1, 1),
        LookBit | 1u << LevelShift | uint32(GetSlot(EMode::CompositeOreMips, 4, OreBase)));
    TestEqual(TEXT("OreHost 2 and OreGlint 1"), GetLookWord(Iron, 2, 2, 1),
        LookBit | 1u << LevelShift | uint32(GetSlot(EMode::SwapOreMips, 2, OreBase)));
    TestEqual(TEXT("the debug mask"), GetLookWord(Iron, 0, 0, DebugMask) >> LevelShift & 3u, uint32(DebugMask));
    TestEqual(TEXT("the unbuilt level 2 reads as 0"), GetLookWord(Iron, 0, 0, 2), 0u);
    TestTrue(TEXT("words stay below 2^15 (interpolated exactly enough as a float) and off the overlay bits"),
        (GetLookWord(16, 4, 2, 1) | GetLookWord(16, 4, 2, DebugMask)) < (1u << 15) &&
            !((GetLookWord(16, 4, 2, 1) | GetLookWord(16, 4, 2, DebugMask)) & (1u << 15 | 0x3Fu << 16)));

    // The generator's manifest names the same slots.
    const TSharedPtr<FJsonObject> Manifest = ReadOreLookManifest();
    if (!TestValid(TEXT("Art/OreLooks/manifest.json"), Manifest)) return false;
    const TArray<TSharedPtr<FJsonValue>>& Slots = Manifest->GetArrayField(TEXT("slots"));
    TestEqual(TEXT("slot count"), Slots.Num(), SlotCount);
    const auto IndexOf = [&Manifest](const TCHAR* Field, const FString& Value) -> int32
    {
        const TArray<TSharedPtr<FJsonValue>>& Names = Manifest->GetArrayField(Field);
        for (int32 Index = 0; Index < Names.Num(); ++Index)
            if (Names[Index]->AsString() == Value) return Index;
        return INDEX_NONE;
    };
    int32 Mismatched = 0;
    for (const TSharedPtr<FJsonValue>& Value : Slots)
    {
        const TSharedPtr<FJsonObject> Slot = Value->AsObject();
        const int32 Base = IndexOf(TEXT("bases"), Slot->GetStringField(TEXT("base")));
        const FString HostName = Slot->GetStringField(TEXT("host"));
        const int32 Host = HostName == Slot->GetStringField(TEXT("base")) ? 0 : IndexOf(TEXT("hosts"), HostName) + 1;
        const int32 Ore = IndexOf(TEXT("ores"), Slot->GetStringField(TEXT("ore")));
        const int32 Mode = IndexOf(TEXT("modes"), Slot->GetStringField(TEXT("mode")));
        const bool bOreMips = Slot->GetStringField(TEXT("mips")) == TEXT("explicit");
        Mismatched += GetSlot(EMode(Mode), Host, Ore * 2 + Base) != int32(Slot->GetNumberField(TEXT("slot"))) ||
            FString(OreNames[Ore]) != Slot->GetStringField(TEXT("ore")) ||
            bOreMips != (Mode >= int32(EMode::CompositeOreMips));
    }
    TestEqual(TEXT("manifest slots that disagree with GetSlot"), Mismatched, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelOreLookMeshTest, "Unnamed.Voxel.OreLooks.Mesh", OreLookTestFlags)

bool FVoxelOreLookMeshTest::RunTest(const FString& Parameters)
{
    // The decorated radius-4 area of seed 263 (overworld), as Unnamed.Voxel.Mesher.Digest meshes it, under each look.
    // Both switches at 0 are that test's golden digest; here the looks must change only what they claim.
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    FString Error;
    FVoxelTerrainPreset TerrainPreset;
    FVoxelBiomeTable Biomes;
    FVoxelSurfaceInputs Surface;
    FVoxelFeatureGraph Graph;
    if (!TestTrue(FString::Printf(TEXT("inputs read (%s)"), *Error),
            VoxelTerrainFactory::ReadInputs(
                VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), TerrainPreset, Biomes, Surface, Error)) ||
        !TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error),
            VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error)))
        return false;
    const TSharedRef<FVoxelDensityTerrain> Terrain =
        MakeShared<FVoxelDensityTerrain>(TerrainPreset, 263, *Table, FVoxelDensityTerrainSettings(), &Biomes, &Surface);
    FVoxelDecorator Decorator;
    if (!TestTrue(TEXT("the terrain is valid"), Terrain->IsValid()) ||
        !TestTrue(FString::Printf(TEXT("the decorator compiles (%s)"), *Error),
            Decorator.Initialize(Graph, *Table, Terrain->GetBiomeSource()->GetTable(), Terrain->GetSeed(),
                FVoxelDecorationOptions(), &Error)))
        return false;
    const auto Mesh = [&](int32 OreHost, int32 OreGlint, FVoxelArea& Area)
    {
        FVoxelMeshOptions Options;
        Options.OreHost = OreHost;
        Options.OreGlint = OreGlint;
        FVoxelArea::Build(*Table, *Terrain, FIntPoint::ZeroValue, 4, Options, true, Area, &Decorator);
    };
    FVoxelArea Vanilla, Glint, Host, Swap;
    Mesh(0, 0, Vanilla);
    Mesh(0, 1, Glint);
    Mesh(1, 0, Host);
    Mesh(2, 1, Swap);
    const FOreLookMeshStats VanillaStats = GetOreLookStats(Vanilla);
    TestEqual(TEXT("both switches 0: no look word"), VanillaStats.LookVertices, int64(0));

    // OreGlint alone keeps every merge: the same vertices, with only the overlay channel of ore faces changed.
    int64 Changed = 0, Wrong = 0;
    if (TestEqual(TEXT("OreGlint 1: the same sections"), Glint.Meshes.Num(), Vanilla.Meshes.Num()))
        for (int32 Index = 0; Index < Vanilla.Meshes.Num(); ++Index)
            for (int32 Layer = 0; Layer < int32(EVoxelLayer::Count); ++Layer)
            {
                const FVoxelMeshLayer& A = Vanilla.Meshes[Index].Mesh.Layers[Layer];
                const FVoxelMeshLayer& B = Glint.Meshes[Index].Mesh.Layers[Layer];
                if (A.Positions != B.Positions || A.BlockUVs != B.BlockUVs || A.TintFaces != B.TintFaces ||
                    A.Colors != B.Colors || A.Indices != B.Indices || A.Codes.Num() != B.Codes.Num())
                {
                    ++Wrong;
                    continue;
                }
                for (int32 Vertex = 0; Vertex < A.Codes.Num(); ++Vertex)
                {
                    if (A.Codes[Vertex] == B.Codes[Vertex]) continue;
                    ++Changed;
                    const uint32 Word = uint32(B.Codes[Vertex].Y);
                    Wrong += A.Codes[Vertex].X != B.Codes[Vertex].X || A.Codes[Vertex].Y != 0.f ||
                        !(Word & VoxelOreLooks::LookBit) || (Word >> VoxelOreLooks::LevelShift & 3u) != 1u ||
                        int32(Word & 1023u) / 16 !=
                            int32(VoxelOreLooks::EMode::CompositeOreMips) * VoxelOreLooks::HostCount;
                }
            }
    TestTrue(FString::Printf(TEXT("OreGlint 1 marks ore faces (%lld vertices)"), Changed), Changed > 0);
    TestEqual(TEXT("OreGlint 1: layers or vertices changed beyond an ore face's overlay channel"), Wrong, int64(0));

    // OreHost splits merges where hosts differ, so it can only add quads; the faces' area stays the same.
    for (const FVoxelArea* Area : {&Host, &Swap})
    {
        const TCHAR* Name = Area == &Host ? TEXT("OreHost 1") : TEXT("OreHost 2 with OreGlint 1");
        const FOreLookMeshStats Stats = GetOreLookStats(*Area);
        TestTrue(
            FString::Printf(TEXT("%s: %lld quads, at least vanilla's %lld"), Name, Stats.Quads, VanillaStats.Quads),
            Stats.Quads >= VanillaStats.Quads);
        for (int32 Layer = 0; Layer < int32(EVoxelLayer::Count); ++Layer)
            TestTrue(FString::Printf(TEXT("%s: layer %d's face area %.1f is vanilla's %.1f"), Name, Layer,
                         Stats.Area[Layer], VanillaStats.Area[Layer]),
                FMath::IsNearlyEqual(Stats.Area[Layer], VanillaStats.Area[Layer], 1e-3));
        TestTrue(
            FString::Printf(TEXT("%s: ores on host rocks drawn with a look (%lld vertices)"), Name, Stats.LookVertices),
            Stats.LookVertices > 0);
    }
    // OreHost 1 marks ores only with a rock host's composite slot; OreHost 2 with OreGlint 1 marks every ore with a
    // swap slot with ore mips (the base's where the host is stone or deepslate). A quad's four vertices share a word.
    const auto CountWrong = [](const FVoxelArea& Area, int32 FirstGroup, int32 LastGroup, uint32 Level, int64& OutRock)
    {
        int64 Wrong = 0;
        OutRock = 0;
        for (const FVoxelArea::FSectionMesh& Section : Area.Meshes)
        {
            const TArray<FVector2f>& Codes = Section.Mesh.Layers[int32(EVoxelLayer::Opaque)].Codes;
            for (int32 Vertex = 0; Vertex < Codes.Num(); ++Vertex)
            {
                const uint32 Word = uint32(Codes[Vertex].Y);
                if ((Word & (3u << 14)) != VoxelOreLooks::LookBit) continue;
                const int32 Group = int32(Word & 1023u) / 16;
                OutRock += Group % VoxelOreLooks::HostCount != 0;
                Wrong += (Word >> VoxelOreLooks::LevelShift & 3u) != Level || Group < FirstGroup || Group > LastGroup ||
                    Codes[Vertex - Vertex % 4] != Codes[Vertex];
            }
        }
        return Wrong;
    };
    constexpr int32 HostGroups = VoxelOreLooks::HostCount;
    int64 Rock = 0;
    TestEqual(TEXT("OreHost 1: look words that are not a rock host's composite"),
        CountWrong(Host, 1, HostGroups - 1, 0u, Rock), int64(0));
    const int32 SwapFirst = int32(VoxelOreLooks::EMode::SwapOreMips) * HostGroups;
    TestEqual(TEXT("OreHost 2 with OreGlint 1: look words that are not a swap slot with ore mips"),
        CountWrong(Swap, SwapFirst, SwapFirst + HostGroups - 1, 1u, Rock), int64(0));
    TestTrue(
        FString::Printf(TEXT("OreHost 2 with OreGlint 1: some ores on a rock host (%lld vertices)"), Rock), Rock > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelOreLookMergeTest, "Unnamed.Voxel.OreLooks.Merge", OreLookTestFlags)

bool FVoxelOreLookMergeTest::RunTest(const FString& Parameters)
{
    // Two iron ores side by side, one walled in granite and one in diorite, open above: vanilla merges their tops into
    // one quad; with OreHost 1 they split into two quads, each with its own host's word.
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    const uint16 Ore = Table->FindState(TEXT("iron_ore")), Granite = Table->FindState(TEXT("granite")),
                 Diorite = Table->FindState(TEXT("diorite"));
    FVoxelPaddedSection Padded;
    FMemory::Memzero(Padded.States);
    const auto Set = [&Padded](int32 X, int32 Y, int32 Z, uint16 State)
    { Padded.States[FVoxelPaddedSection::GetIndex(X, Y, Z)] = State; };
    Set(8, 8, 8, Ore);
    Set(9, 8, 8, Ore);
    for (const FIntVector& Cell : {FIntVector(7, 8, 8), FIntVector(8, 7, 8), FIntVector(8, 9, 8), FIntVector(8, 8, 7)})
        Set(Cell.X, Cell.Y, Cell.Z, Granite);
    for (const FIntVector& Cell : {FIntVector(10, 8, 8), FIntVector(9, 7, 8), FIntVector(9, 9, 8), FIntVector(9, 8, 7)})
        Set(Cell.X, Cell.Y, Cell.Z, Diorite);
    const uint32 TopCode = Table->Get(Ore).Faces[int32(EVoxelFace::Up)];
    const auto OreTops = [&](int32 OreHost)
    {
        FVoxelMeshOptions Options;
        Options.OreHost = OreHost;
        FVoxelMeshData Mesh;
        VoxelMesher::MeshSection(*Table, Padded, 5, Options, Mesh);
        TArray<uint32> Words;
        const FVoxelMeshLayer& Layer = Mesh.Layers[int32(EVoxelLayer::Opaque)];
        for (int32 Quad = 0; Quad < Layer.GetQuadCount(); ++Quad)
            if (int32(Layer.TintFaces[Quad * 4].Y) == int32(EVoxelFace::Up) &&
                uint32(Layer.Codes[Quad * 4].X) == TopCode)
                Words.Add(uint32(Layer.Codes[Quad * 4].Y));
        return Words;
    };
    TestEqual(TEXT("vanilla: one merged top"), OreTops(0).Num(), 1);
    const TArray<uint32> Words = OreTops(1);
    const uint32 Iron = Table->Get(Ore).Ore - 1;
    TArray<uint32> Expected = {
        VoxelOreLooks::LookBit | uint32(VoxelOreLooks::GetSlot(VoxelOreLooks::EMode::Composite, 1, Iron)),
        VoxelOreLooks::LookBit | uint32(VoxelOreLooks::GetSlot(VoxelOreLooks::EMode::Composite, 2, Iron))};
    TArray<uint32> Sorted = Words;
    Sorted.Sort();
    FString Got;
    for (const uint32 Word : Sorted)
        Got += FString::Printf(TEXT(" 0x%x"), Word);
    TestTrue(FString::Printf(TEXT("OreHost 1: two tops, granite's and diorite's (got%s)"), *Got), Sorted == Expected);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelOreLookTextureTest, "Unnamed.Voxel.OreLooks.Texture", OreLookTestFlags)

bool FVoxelOreLookTextureTest::RunTest(const FString& Parameters)
{
    // T_OreLooks holds the manifest's slots: mip 0 by hash, the ore-aware mips as the strip draws them; M_ChunkOpaque
    // samples it.
    UTexture2DArray* Array =
        Cast<UTexture2DArray>(UUnnamedProjectSettings::GetBlockAssetPath(TEXT("T_OreLooks")).TryLoad());
    const TSharedPtr<FJsonObject> Manifest = ReadOreLookManifest();
    if (!TestNotNull(TEXT("T_OreLooks (ImportOreLooks.py)"), Array) || !TestValid(TEXT("manifest"), Manifest))
        return false;
    TestTrue(TEXT("settings: uncompressed BGRA8, sRGB, nearest, own mips, 16 x 16 with 5 levels"),
        Array->CompressionSettings == TC_EditorIcon && Array->SRGB && Array->Filter == TF_Nearest &&
            Array->MipGenSettings == TMGS_LeaveExistingMips && Array->Source.GetFormat() == TSF_BGRA8 &&
            Array->Source.GetSizeX() == 16 && Array->Source.GetNumMips() == 5);
    const TArray<TSharedPtr<FJsonValue>>& Slots = Manifest->GetArrayField(TEXT("slots"));
    if (!TestEqual(TEXT("layers"), Array->Source.GetNumSlices(), Slots.Num())) return false;
    TArray<uint8> Strip;
    int32 Width = 0, Height = 0;
    if (!TestTrue(TEXT("the strip decodes"),
            UFoundationBlockImport::DecodePngFile(
                FPaths::ProjectDir() / TEXT("Art/OreLooks/T_OreLooks.png"), Strip, Width, Height)))
        return false;
    int32 HashDiffers = 0, MipDiffers = 0, Explicit = 0;
    for (const TSharedPtr<FJsonValue>& Value : Slots)
    {
        const TSharedPtr<FJsonObject> Slot = Value->AsObject();
        const int32 Layer = int32(Slot->GetNumberField(TEXT("slot")));
        HashDiffers +=
            UFoundationBlockImport::HashLayer(Array, Layer).ToLower() != Slot->GetStringField(TEXT("rgba_sha256"));
        if (Slot->GetStringField(TEXT("mips")) != TEXT("explicit")) continue;
        ++Explicit;
        for (int32 Mip = 1; Mip < 5; ++Mip)
        {
            const int32 Size = 16 >> Mip, Top = (Layer * 5 + Mip) * 16;
            TArray<uint8> Expected, Actual;
            for (int32 Row = 0; Row < Size; ++Row)
                Expected.Append(Strip.GetData() + (Top + Row) * Width * 4, Size * 4);
            MipDiffers += !UFoundationBlockImport::ReadLayerMip(Array, Layer, Mip, Actual) || Actual != Expected;
        }
    }
    TestEqual(TEXT("layers whose mip 0 differs from the manifest"), HashDiffers, 0);
    TestEqual(TEXT("ore-aware slots"), Explicit, Slots.Num() / 2);
    TestEqual(TEXT("ore-aware mips that differ from the strip"), MipDiffers, 0);
    UMaterialInterface* Material =
        Cast<UMaterialInterface>(UUnnamedProjectSettings::GetBlockAssetPath(TEXT("M_ChunkOpaque")).TryLoad());
    UTexture* Looks = nullptr;
    TestTrue(TEXT("M_ChunkOpaque samples T_OreLooks"),
        Material && Material->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("OreLookTextures")), Looks) &&
            Looks == Array);
    return true;
}

#endif
