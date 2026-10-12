#if WITH_DEV_AUTOMATION_TESTS
#include "Blocks/VoxelBlockTable.h"
#include "Foundation/FoundationBlockMeshes.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "HAL/PlatformTime.h"
#include "Meshing/VoxelMesher.h"
#include "Misc/AutomationTest.h"
#include "Random/VoxelRandom.h"
#include "Storage/VoxelColumn.h"
#include "Storage/VoxelSection.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelLightSources.h"
#include "Voxel/VoxelLights.h"
#include "VoxelConstants.h"

namespace
{
    // A block's default state with some properties changed; 0 when the block does not exist.
    uint16 FindLightTestState(const FFoundationBlockRegistry& Registry, const TCHAR* BlockId,
        TConstArrayView<TPair<FName, FName>> Properties = {})
    {
        const FFoundationBlockType* Type = Registry.FindBlock(BlockId);
        if (!Type) return 0;
        FFoundationBlockState Result = Type->GetDefaultState();
        for (const TPair<FName, FName>& Property : Properties)
            Result = Registry.WithValue(Result, Property.Key, Property.Value);
        return Result.Id;
    }

    // The brute-force reference for VoxelLightSources::GatherSources: every cell of the column, read one by one through
    // world coordinates, its cube faces read from the 3 x 3 columns by their own lookup.
    struct FLightTestWorld
    {
        FIntPoint Position;
        const FVoxelColumn* Around[3][3] = {};

        uint16 Get(int32 X, int32 Y, int32 Z) const
        {
            const int32 LocalX = X - Position.X * 16, LocalY = Y - Position.Y * 16;
            const int32 ColumnX = LocalX < 0 ? 0 : LocalX < 16 ? 1 : 2;
            const int32 ColumnY = LocalY < 0 ? 0 : LocalY < 16 ? 1 : 2;
            const FVoxelColumn* Owner = Around[ColumnY][ColumnX];
            if (!Owner || Z < VoxelConstants::MinZ || Z > VoxelConstants::MaxZ) return 0;
            return Owner->GetState(LocalX - (ColumnX - 1) * 16, LocalY - (ColumnY - 1) * 16, Z);
        }

        FVoxelNeighborhood MakeNeighborhood() const
        {
            FVoxelNeighborhood Result;
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                    Result.Set(DX, DY, Around[DY + 1][DX + 1]);
            return Result;
        }

        void GatherBruteForce(const FVoxelLightTable& Lights, TArray<FVoxelLightSource>& Out) const
        {
            static const FIntVector FaceSteps[6] = {
                {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
            for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
                for (int32 Y = Position.Y * 16; Y < Position.Y * 16 + 16; ++Y)
                    for (int32 X = Position.X * 16; X < Position.X * 16 + 16; ++X)
                    {
                        const FVoxelLightInfo& Info = Lights.Get(Get(X, Y, Z));
                        if (!Info.Level) continue;
                        uint8 OpenFaces = 0;
                        for (int32 Face = 0; Face < 6; ++Face)
                            if (!Lights.Get(Get(X + FaceSteps[Face].X, Y + FaceSteps[Face].Y, Z + FaceSteps[Face].Z))
                                    .bOpaqueCube)
                                OpenFaces |= uint8(1 << Face);
                        FVector Local;
                        if (!VoxelLightSources::GetLocalPoint(Info, OpenFaces, Local)) continue;
                        FVoxelLightSource& Source = Out.AddDefaulted_GetRef();
                        Source.Cell = FIntVector(X, Y, Z);
                        Source.Point = FVector(X, Y, Z) + Local;
                        Source.Level = Info.Level;
                        Source.Key = VoxelLightSources::MakeKey(Source.Cell);
                    }
        }
    };

    bool SameLightSources(TConstArrayView<FVoxelLightSource> A, TConstArrayView<FVoxelLightSource> B)
    {
        if (A.Num() != B.Num()) return false;
        for (int32 Index = 0; Index < A.Num(); ++Index)
            if (A[Index].Cell != B[Index].Cell || A[Index].Point != B[Index].Point ||
                A[Index].Level != B[Index].Level || A[Index].Key != B[Index].Key)
                return false;
        return true;
    }

    bool SameLightPicks(TConstArrayView<FVoxelLightPick> A, TConstArrayView<FVoxelLightPick> B)
    {
        if (A.Num() != B.Num()) return false;
        for (int32 Index = 0; Index < A.Num(); ++Index)
            if (A[Index].Centroid != B[Index].Centroid || A[Index].SeedCell != B[Index].SeedCell ||
                A[Index].Weight != B[Index].Weight || A[Index].Level != B[Index].Level ||
                A[Index].Members != B[Index].Members || A[Index].Key != B[Index].Key)
                return false;
        return true;
    }

    FVoxelLightSource MakeLightTestSource(const FVector& Point, uint8 Level)
    {
        FVoxelLightSource Source;
        Source.Cell = FIntVector(FMath::FloorToInt(Point.X), FMath::FloorToInt(Point.Y), FMath::FloorToInt(Point.Z));
        Source.Point = Point;
        Source.Level = Level;
        Source.Key = VoxelLightSources::MakeKey(Source.Cell);
        return Source;
    }

    const FVoxelLightSource* FindLightTestSource(
        TConstArrayView<FVoxelLightSource> LightSources, const FIntVector& Cell)
    {
        for (const FVoxelLightSource& Source : LightSources)
            if (Source.Cell == Cell) return &Source;
        return nullptr;
    }

    // A generated-like column for the scan timing: deepslate with ores, tuff, caves and lava below 0; stone with ores,
    // gravel, caves and water up to 63; a varied surface section; air above. Torches and glowstone when asked.
    void MakeLightTestTerrain(
        const FFoundationBlockRegistry& Registry, bool bEmitters, FVoxelXoroshiro& Stream, FVoxelColumn& Out)
    {
        auto FindTerrainState = [&Registry](const TCHAR* Id) { return FindLightTestState(Registry, Id); };
        const uint16 DeepStates[] = {FindTerrainState(TEXT("deepslate")), FindTerrainState(TEXT("tuff")),
            FindTerrainState(TEXT("deepslate_iron_ore")), FindTerrainState(TEXT("deepslate_diamond_ore")),
            FindTerrainState(TEXT("deepslate_redstone_ore")), FindTerrainState(TEXT("lava")), 0};
        const uint16 UpperStates[] = {FindTerrainState(TEXT("stone")), FindTerrainState(TEXT("granite")),
            FindTerrainState(TEXT("diorite")), FindTerrainState(TEXT("andesite")), FindTerrainState(TEXT("coal_ore")),
            FindTerrainState(TEXT("iron_ore")), FindTerrainState(TEXT("copper_ore")), FindTerrainState(TEXT("gravel")),
            FindTerrainState(TEXT("dirt")), FindTerrainState(TEXT("water")), 0};
        const uint16 SurfaceStates[] = {FindTerrainState(TEXT("stone")), FindTerrainState(TEXT("dirt")),
            FindTerrainState(TEXT("grass_block")), FindTerrainState(TEXT("short_grass")),
            FindTerrainState(TEXT("sand")), FindTerrainState(TEXT("water")), FindTerrainState(TEXT("oak_log")),
            FindTerrainState(TEXT("oak_leaves")), FindTerrainState(TEXT("birch_log")),
            FindTerrainState(TEXT("birch_leaves")), FindTerrainState(TEXT("poppy")),
            FindTerrainState(TEXT("dandelion")), FindTerrainState(TEXT("gravel")), FindTerrainState(TEXT("clay")),
            FindTerrainState(TEXT("coarse_dirt")), FindTerrainState(TEXT("podzol")), FindTerrainState(TEXT("fern")),
            FindTerrainState(TEXT("sugar_cane")), FindTerrainState(TEXT("andesite")), 0};
        TArray<uint16> States;
        States.SetNumUninitialized(FVoxelSection::Volume);
        for (int32 Section = 0; Section <= 8; ++Section)
        {
            const TConstArrayView<uint16> Pool = Section < 4 ? TConstArrayView<uint16>(DeepStates)
                : Section < 8                                ? TConstArrayView<uint16>(UpperStates)
                                                             : TConstArrayView<uint16>(SurfaceStates);
            for (int32 Index = 0; Index < FVoxelSection::Volume; ++Index)
                // Mostly the host rock, the rest spread over the pool.
                States[Index] = Stream.NextInt(4) ? Pool[0] : Pool[Stream.NextInt(Pool.Num())];
            if (bEmitters && (Section == 2 || Section == 5 || Section == 6))
            {
                States[FVoxelSection::GetIndex(3, 4, 5)] = FindTerrainState(TEXT("torch"));
                States[FVoxelSection::GetIndex(9, 2, 8)] = FindTerrainState(TEXT("torch"));
                States[FVoxelSection::GetIndex(12, 12, 12)] = FindTerrainState(TEXT("glowstone"));
                States[FVoxelSection::GetIndex(12, 12, 13)] = 0;
            }
            Out.SetSectionStates(Section, States);
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FVoxelLightsTest, "Unnamed.Voxel.Lights", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLightsTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FFoundationBlockRegistry> RegistryRef = FFoundationBlockRegistry::GetDefault();
    const FFoundationBlockRegistry& Registry = *RegistryRef;
    const TSharedRef<const FVoxelBlockTable> BlocksRef = VoxelBlocks::GetDefault();
    const FVoxelBlockTable& BlockTable = *BlocksRef;
    if (!BlockTable.FindState(TEXT("stone")))
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    int32 Rows = 0, Passed = 0;
    auto Row = [this, &Rows, &Passed](const FString& What, bool bOk)
    {
        ++Rows;
        Passed += bOk;
        TestTrue(What, bOk);
    };
    auto Near = [](const FVector& A, const FVector& B) { return A.Equals(B, 1e-9); };
    const TSharedRef<const FVoxelLightTable> LightsRef = VoxelLightSources::MakeTable(Registry, BlockTable);
    const FVoxelLightTable& Lights = *LightsRef;
    auto StateOf = [&Registry](const TCHAR* Id, TConstArrayView<TPair<FName, FName>> Properties = {})
    { return FindLightTestState(Registry, Id, Properties); };
    using FLightTestProperty = TPair<FName, FName>;

    // --- Table ---------------------------------------------------------------------------------------------------
    {
        Row(TEXT("Table: one entry per state"), Lights.GetStateCount() == BlockTable.GetStateCount());
        const FFoundationBlockType* TorchType = Registry.FindBlock(TEXT("torch"));
        const FFoundationBlockType* WallType = Registry.FindBlock(TEXT("wall_torch"));
        Row(TEXT("Table: torch and wall_torch exist with the torch rule"),
            TorchType && WallType && TorchType->bTorchRule && WallType->bTorchRule);
        bool bTorchStates = TorchType != nullptr;
        for (int32 Id = TorchType ? TorchType->MinStateId : 0;
            TorchType && Id < TorchType->MinStateId + TorchType->StateCount; ++Id)
            bTorchStates &=
                Lights.Get(uint16(Id)).Shape == EVoxelLightShape::FloorTorch && Lights.Get(uint16(Id)).Level == 14;
        Row(TEXT("Table: torch is a FloorTorch at 14"), bTorchStates);
        const TCHAR* FacingNames[4] = {TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south")};
        for (int32 Face = 0; Face < 4; ++Face)
        {
            const FVoxelLightInfo& Info =
                Lights.Get(StateOf(TEXT("wall_torch"), {FLightTestProperty(TEXT("facing"), FacingNames[Face])}));
            Row(FString::Printf(TEXT("Table: wall_torch facing %s is a WallTorch at 14 facing it"), FacingNames[Face]),
                Info.Shape == EVoxelLightShape::WallTorch && Info.Level == 14 && Info.Facing == Face);
        }
        const FVoxelLightInfo& GlowstoneInfo = Lights.Get(StateOf(TEXT("glowstone")));
        Row(TEXT("Table: glowstone is a Cube at 15"),
            GlowstoneInfo.Shape == EVoxelLightShape::Cube && GlowstoneInfo.Level == 15 && GlowstoneInfo.bOpaqueCube);
        const FVoxelLightInfo& CryingInfo = Lights.Get(StateOf(TEXT("crying_obsidian")));
        Row(TEXT("Table: crying_obsidian is a Cube at 10"),
            CryingInfo.Shape == EVoxelLightShape::Cube && CryingInfo.Level == 10);
        const uint16 LitOre = StateOf(TEXT("redstone_ore"), {FLightTestProperty(TEXT("lit"), TEXT("true"))});
        // lit=true took effect: not the default (unlit) state. Its emission is not asserted: per-state light of full
        // cubes is not imported, so it reads 0 today (26.3: 9).
        Row(TEXT("Table: lit redstone_ore (not the default state) gets no light"),
            LitOre && LitOre != StateOf(TEXT("redstone_ore")) && Lights.Get(LitOre).Level == 0);
        AddInfo(FString::Printf(
            TEXT("lit redstone_ore glows at %d in the voxel table (26.3: 9)"), BlockTable.Get(LitOre).LightEmission));
        // Sea pickles vary by state: waterlogged, 2 pickles are 9 and 3 are 12.
        const uint16 Pickles2 = StateOf(TEXT("sea_pickle"),
            {FLightTestProperty(TEXT("pickles"), TEXT("2")), FLightTestProperty(TEXT("waterlogged"), TEXT("true"))});
        const uint16 Pickles3 = StateOf(TEXT("sea_pickle"),
            {FLightTestProperty(TEXT("pickles"), TEXT("3")), FLightTestProperty(TEXT("waterlogged"), TEXT("true"))});
        Row(TEXT("Table: sea_pickle x2 (9) gets no light"),
            BlockTable.Get(Pickles2).LightEmission == 9 && Lights.Get(Pickles2).Level == 0);
        Row(TEXT("Table: sea_pickle x3 (12) is a Model at 12"),
            Lights.Get(Pickles3).Shape == EVoxelLightShape::Model && Lights.Get(Pickles3).Level == 12);
        const uint16 Berries = StateOf(TEXT("cave_vines"), {FLightTestProperty(TEXT("berries"), TEXT("true"))});
        Row(TEXT("Table: cave_vines with berries is a Model at 14"),
            Lights.Get(Berries).Shape == EVoxelLightShape::Model && Lights.Get(Berries).Level == 14);
        const FFoundationBlockType* LavaType = Registry.FindBlock(TEXT("lava"));
        bool bLava = LavaType != nullptr;
        for (int32 Id = LavaType ? LavaType->MinStateId : 0;
            LavaType && Id < LavaType->MinStateId + LavaType->StateCount; ++Id)
            bLava &= BlockTable.Get(uint16(Id)).LightEmission == 15 && Lights.Get(uint16(Id)).Level == 0;
        Row(TEXT("Table: lava glows at 15 but gets no light"), bLava);
        const FVoxelLightInfo& StoneInfo = Lights.Get(StateOf(TEXT("stone")));
        Row(TEXT("Table: air and stone get none"),
            Lights.Get(0).Level == 0 && Lights.Get(0).Shape == EVoxelLightShape::None && StoneInfo.Level == 0 &&
                StoneInfo.Shape == EVoxelLightShape::None && StoneInfo.bOpaqueCube && !Lights.Get(0).bOpaqueCube);
        Row(TEXT("Table: unknown ids read as air"), Lights.Get(uint16(Lights.GetStateCount() + 5)).Level == 0);

        // Every emitting state at 10 or more has a light, every other none; model states by data alone.
        int32 Wrong = 0, ByShape[5] = {};
        TSet<FName> CubeBlocks, ModelBlocks;
        for (int32 Id = 0; Id < Lights.GetStateCount(); ++Id)
        {
            const FVoxelLightInfo& Info = Lights.Get(uint16(Id));
            const FVoxelStateInfo& Voxel = BlockTable.Get(uint16(Id));
            const bool bLavaState = Registry.GetBlock(FFoundationBlockState{uint16(Id)}).IsLava();
            Wrong += (Info.Level != 0) != (Voxel.LightEmission >= 10 && !bLavaState);
            Wrong += Info.Level != 0 && Info.Level != Voxel.LightEmission;
            Wrong += Info.Shape == EVoxelLightShape::Model && Voxel.Shape == EVoxelShape::Full &&
                Voxel.Layer == EVoxelLayer::Opaque;
            ++ByShape[int32(Info.Shape)];
            const FName BlockName = Registry.GetBlock(FFoundationBlockState{uint16(Id)}).Id;
            if (Info.Shape == EVoxelLightShape::Cube) CubeBlocks.Add(BlockName);
            if (Info.Shape == EVoxelLightShape::Model) ModelBlocks.Add(BlockName);
        }
        Row(TEXT("Table: every state at 10+ (not lava) has its own level, none below"), Wrong == 0);
        auto JoinNames = [](const TSet<FName>& Names)
        {
            TArray<FString> Sorted;
            for (const FName Name : Names)
                Sorted.Add(Name.ToString());
            Sorted.Sort();
            return FString::Join(Sorted, TEXT(" "));
        };
        AddInfo(FString::Printf(TEXT("Census: FloorTorch %d, WallTorch %d, Cube %d, Model %d states"), ByShape[1],
            ByShape[2], ByShape[3], ByShape[4]));
        AddInfo(TEXT("Cube blocks: ") + JoinNames(CubeBlocks));
        AddInfo(TEXT("Model blocks: ") + JoinNames(ModelBlocks));

        const TSharedRef<const FVoxelLightTable> Min11 = VoxelLightSources::MakeTable(Registry, BlockTable, 11);
        const TSharedRef<const FVoxelLightTable> Min15 = VoxelLightSources::MakeTable(Registry, BlockTable, 15);
        const TSharedRef<const FVoxelLightTable> Min16 = VoxelLightSources::MakeTable(Registry, BlockTable, 16);
        const uint16 GlowstoneState = StateOf(TEXT("glowstone")), TorchState = StateOf(TEXT("torch"));
        Row(TEXT("Table: MinLevel 11 drops crying obsidian, keeps torches"),
            Min11->Get(StateOf(TEXT("crying_obsidian"))).Level == 0 && Min11->Get(TorchState).Level == 14 &&
                Min11->GetMinLevel() == 11);
        Row(TEXT("Table: MinLevel 15 drops torches, keeps glowstone"),
            Min15->Get(TorchState).Level == 0 && Min15->Get(GlowstoneState).Level == 15);
        Row(TEXT("Table: MinLevel 16 drops everything"), Min16->Get(GlowstoneState).Level == 0);
    }

    // --- Torch glow (voxel.TorchGlow: the mesh's table) -----------------------------------------------------------
    {
        auto TorchLookSameButGlow = [](const FVoxelStateInfo& A, const FVoxelStateInfo& B)
        {
            return A.Shape == B.Shape && A.Layer == B.Layer && A.OpaqueFaces == B.OpaqueFaces && A.bSolid == B.bSolid &&
                A.bCastsOcclusion == B.bCastsOcclusion && A.bLeaves == B.bLeaves && A.FluidAmount == B.FluidAmount &&
                A.Layers == B.Layers && A.Block == B.Block && A.Tint == B.Tint && A.TintSource == B.TintSource &&
                A.Ore == B.Ore && A.Rock == B.Rock && FMemory::Memcmp(A.Faces, B.Faces, sizeof(A.Faces)) == 0 &&
                A.ModelParts == B.ModelParts && A.ModelPartCount == B.ModelPartCount && A.HeldFluid == B.HeldFluid &&
                A.Offset == B.Offset && A.OffsetHorizontal == B.OffsetHorizontal && A.OffsetDown == B.OffsetDown;
        };
        auto IsTorchRule = [&Registry](int32 Id)
        {
            const FFoundationBlockState State{uint16(Id)};
            return Registry.IsKnown(State) && Registry.GetBlock(State).bTorchRule;
        };
        const TSharedRef<const FVoxelBlockTable> Glow1 = VoxelLightSources::MakeTorchGlowTable(Registry, BlocksRef, 1);
        const TSharedRef<const FVoxelBlockTable> Glow14 =
            VoxelLightSources::MakeTorchGlowTable(Registry, BlocksRef, 14);
        int32 TorchRuleStates = 0, Lowered = 0, OtherChanged = 0, FieldsChanged = 0, Same14 = 0;
        for (int32 Id = 0; Id < BlockTable.GetStateCount(); ++Id)
        {
            const FVoxelStateInfo& Base = BlockTable.Get(uint16(Id));
            const FVoxelStateInfo& One = Glow1->Get(uint16(Id));
            FieldsChanged += !TorchLookSameButGlow(Base, One);
            Same14 += TorchLookSameButGlow(Base, Glow14->Get(uint16(Id))) &&
                Base.LightEmission == Glow14->Get(uint16(Id)).LightEmission;
            if (IsTorchRule(Id))
            {
                ++TorchRuleStates;
                Lowered += Base.LightEmission == 14 && One.LightEmission == 1;
            }
            else
                OtherChanged += Base.LightEmission != One.LightEmission;
        }
        Row(FString::Printf(TEXT("Torch glow: glow 1 lowers every torch-rule state from 14 to 1 (%d of %d)"), Lowered,
                TorchRuleStates),
            TorchRuleStates > 0 && Lowered == TorchRuleStates);
        Row(FString::Printf(TEXT("Torch glow: with glow 1 only torch-rule states differ from the base table (%d other "
                                 "states' glow and %d states' other fields changed)"),
                OtherChanged, FieldsChanged),
            OtherChanged == 0 && FieldsChanged == 0 && Glow1->GetStateCount() == BlockTable.GetStateCount() &&
                Glow1->GetModelCount() == BlockTable.GetModelCount());
        for (const TCHAR* Id :
            {TEXT("glow_lichen"), TEXT("glowstone"), TEXT("sea_pickle"), TEXT("cave_vines"), TEXT("lava")})
        {
            const FFoundationBlockType* Type = Registry.FindBlock(Id);
            bool bKept = Type != nullptr;
            int32 Brightest = 0;
            for (int32 State = Type ? Type->MinStateId : 0; Type && State < Type->MinStateId + Type->StateCount;
                ++State)
            {
                bKept &= Glow1->Get(uint16(State)).LightEmission == BlockTable.Get(uint16(State)).LightEmission;
                Brightest = FMath::Max(Brightest, int32(BlockTable.Get(uint16(State)).LightEmission));
            }
            Row(FString::Printf(TEXT("Torch glow: %s keeps its glow with glow 1 (brightest state %d)"), Id, Brightest),
                bKept && Brightest > 0);
        }
        // The world makes its light table from the unlowered table: torches still give level-14 lights.
        const TSharedRef<const FVoxelLightTable> AfterGlow1 = VoxelLightSources::MakeTable(Registry, BlockTable);
        const FVoxelLightInfo& WallInfo =
            AfterGlow1->Get(StateOf(TEXT("wall_torch"), {FLightTestProperty(TEXT("facing"), TEXT("north"))}));
        Row(TEXT("Torch glow: the light table still says torch 14 and wall torch 14"),
            AfterGlow1->Get(StateOf(TEXT("torch"))).Level == 14 && WallInfo.Level == 14 &&
                WallInfo.Shape == EVoxelLightShape::WallTorch &&
                BlockTable.Get(StateOf(TEXT("torch"))).LightEmission == 14);
        Row(TEXT("Torch glow: 14 gives a table equal to the base (the base itself)"),
            &*Glow14 == &BlockTable && Same14 == BlockTable.GetStateCount());
    }

    // --- Torch flame (T3b; Jesus: only the flame glows) ----------------------------------------------------------
    {
        using namespace FoundationBlockMeshes;
        Row(TEXT("Torch flame: voxel.TorchGlow defaults to 1 (warm A), and the flame glows at 1 on its own"),
            FVoxelLights::DefaultTorchGlow == 1 && TorchFlameLight == 1 && TorchFlameV == .5f);
        // The drawn quads: every model but the torch's as baked; a torch model's four sides cut at the flame line,
        // covering what they covered (the same area), and nothing else moved.
        auto SameQuad = [](const FFoundationModelQuad& A, const FFoundationModelQuad& B)
        {
            for (int32 Corner = 0; Corner < 4; ++Corner)
                if (A.Positions[Corner] != B.Positions[Corner] || A.UVs[Corner] != B.UVs[Corner]) return false;
            return A.Texture == B.Texture && A.TintIndex == B.TintIndex && A.CullFace == B.CullFace &&
                A.Facing == B.Facing && A.Shade == B.Shade && A.Light == B.Light;
        };
        auto Area = [](const FFoundationModelQuad& Quad)
        {
            return .5f *
                FVector3f::CrossProduct(Quad.Positions[2] - Quad.Positions[0], Quad.Positions[3] - Quad.Positions[1])
                    .Size();
        };
        int32 Unchanged = 0, Cut = 0, CutQuads = 0, AreaKept = 0, SharedCuts = 0;
        for (int32 Model = 0; Model < Registry.GetModelCount(); ++Model)
        {
            const TConstArrayView<FFoundationModelQuad> Baked = Registry.GetModelQuads(Model);
            const TArray<FFoundationModelQuad> Drawn = GetDrawnModelQuads(Registry, Model);
            bool bSame = Drawn.Num() == Baked.Num();
            for (int32 Index = 0; bSame && Index < Baked.Num(); ++Index)
                bSame = SameQuad(Drawn[Index], Baked[Index]);
            if (bSame)
            {
                ++Unchanged;
                continue;
            }
            ++Cut;
            CutQuads += Drawn.Num() - Baked.Num();
            float BakedArea = 0, DrawnArea = 0;
            for (const FFoundationModelQuad& Quad : Baked)
                BakedArea += Area(Quad);
            for (const FFoundationModelQuad& Quad : Drawn)
                DrawnArea += Area(Quad);
            AreaKept += FMath::IsNearlyEqual(BakedArea, DrawnArea, 1e-5f);
            // Each cut's flame and stick pieces share exactly the two corners of the cut (same position and UV).
            for (int32 Index = 0; Index < Drawn.Num(); ++Index)
                for (int32 Other = Index + 1; Other < Drawn.Num(); ++Other)
                {
                    if (Drawn[Index].Light == Drawn[Other].Light) continue;
                    int32 Shared = 0;
                    for (int32 Corner = 0; Corner < 4; ++Corner)
                        for (int32 OtherCorner = 0; OtherCorner < 4; ++OtherCorner)
                            Shared += Drawn[Index].Positions[Corner] == Drawn[Other].Positions[OtherCorner] &&
                                Drawn[Index].UVs[Corner] == Drawn[Other].UVs[OtherCorner];
                    SharedCuts += Shared == 2;
                }
        }
        Row(FString::Printf(
                TEXT("Torch flame: only the 5 torch models are cut (%d cut, %d as baked of %d), 4 quads more "
                     "each (%d), covering the same area (%d)"),
                Cut, Unchanged, Registry.GetModelCount(), CutQuads, AreaKept),
            Cut == 5 && Unchanged == Registry.GetModelCount() - 5 && CutQuads == 20 && AreaKept == 5);
        Row(FString::Printf(
                TEXT("Torch flame: each of the 20 cuts' two pieces share the cut's two corners exactly (%d)"),
                SharedCuts),
            SharedCuts == 20);

        // One row per torch-rule state: with voxel.TorchGlow 0 (only the flame glowing) it meshes at glow 0 and draws 5
        // flame quads at 1 (the four cut sides and the top, all above the line) and 5 stick quads at 0 (the four sides
        // and the bottom, all below it).
        const TSharedRef<const FVoxelBlockTable> Default =
            VoxelLightSources::MakeTorchGlowTable(Registry, BlocksRef, 0);
        int32 TorchRuleStates = 0;
        for (int32 Id = 1; Id < BlockTable.GetStateCount(); ++Id)
        {
            const FFoundationBlockState State{uint16(Id)};
            if (!Registry.IsKnown(State) || !Registry.GetBlock(State).bTorchRule) continue;
            ++TorchRuleStates;
            const FVoxelStateInfo& Info = Default->Get(uint16(Id));
            int32 Flame = 0, Stick = 0, Other = 0;
            const int32* Part = Default->GetModelParts(Info);
            for (int32 Index = 0; Index < Info.ModelPartCount; ++Index, Part += 1 + Part[0])
                for (int32 Alternative = 0; Alternative < Part[0]; ++Alternative)
                    for (const FVoxelModelQuad& Quad : Default->GetModelQuads(Part[1 + Alternative]))
                    {
                        float MinV = 1, MaxV = 0;
                        for (const FVector2f& UV : Quad.UVs)
                        {
                            MinV = FMath::Min(MinV, UV.Y);
                            MaxV = FMath::Max(MaxV, UV.Y);
                        }
                        const bool bFlame = Quad.Light == TorchFlameLight && MaxV <= TorchFlameV;
                        const bool bStick = !bFlame && Quad.Light == 0 && MinV >= TorchFlameV;
                        Flame += bFlame;
                        Stick += bStick;
                        Other += !bFlame && !bStick;
                    }
            Row(FString::Printf(TEXT("Torch flame: %s meshes at glow %d with %d flame quads at %d, %d stick quads at 0 "
                                     "and %d others; the Playground draws it at glow %d"),
                    *Registry.ToString(State), Info.LightEmission, Flame, TorchFlameLight, Stick, Other,
                    GetDrawnLight(Registry, State)),
                Info.LightEmission == 0 && Info.ModelPartCount == 1 && Flame == 5 && Stick == 5 && Other == 0 &&
                    GetDrawnLight(Registry, State) == FVoxelLights::ReadTorchGlow());
        }
        // The standing torch's stick (0 to 10/16 high, v 1 to 6/16) is cut 8/16 high, where the texture's row 8 starts.
        {
            const FVoxelStateInfo& Standing = Default->Get(StateOf(TEXT("torch")));
            float FlameLow = 2, StickHigh = -1;
            const int32* Part = Default->GetModelParts(Standing);
            for (const FVoxelModelQuad& Quad : Default->GetModelQuads(Part[1]))
                for (const FVector3f& Position : Quad.Positions)
                {
                    if (Quad.Light)
                        FlameLow = FMath::Min(FlameLow, Position.Z);
                    else
                        StickHigh = FMath::Max(StickHigh, Position.Z);
                }
            Row(FString::Printf(TEXT("Torch flame: the standing torch's flame starts at %.5f blocks, its stick ends at "
                                     "%.5f (8/16)"),
                    FlameLow, StickHigh),
                FMath::IsNearlyEqual(FlameLow, .5f, 1e-5f) && FMath::IsNearlyEqual(StickHigh, .5f, 1e-5f));
        }
        Row(FString::Printf(TEXT("Torch flame: the torch rule covers 5 states (%d): torch and wall_torch's 4 facings"),
                TorchRuleStates),
            TorchRuleStates == 5);
        // The Playground: every other state glows at its own light (glow lichen 7, glowstone 15).
        int32 OthersOwn = 0, Others = 0;
        for (int32 Id = 1; Id < Registry.GetStateLimit(); ++Id)
        {
            const FFoundationBlockState State{uint16(Id)};
            if (!Registry.IsKnown(State) || Registry.GetBlock(State).bTorchRule) continue;
            ++Others;
            OthersOwn += GetDrawnLight(Registry, State) == Registry.GetLightEmission(State);
        }
        Row(FString::Printf(TEXT("Torch flame: every other state draws at its own light in the Playground (%d of %d)"),
                OthersOwn, Others),
            Others > 4000 && OthersOwn == Others);
    }

    // --- Points --------------------------------------------------------------------------------------------------
    {
        FVoxelLightInfo Info;
        FVector Point;
        Info.Level = 14;
        Info.Shape = EVoxelLightShape::FloorTorch;
        Row(TEXT("Points: floor torch (0.5, 0.5, 0.6875)"),
            VoxelLightSources::GetLocalPoint(Info, 0, Point) && Point == FVector(0.5, 0.5, 0.6875));
        Info.Shape = EVoxelLightShape::WallTorch;
        const double Out = 0.26309485975, Up = 0.85391717860;
        const FVector Expected[4] = {
            FVector(1 - Out, 0.5, Up), FVector(Out, 0.5, Up), FVector(0.5, 1 - Out, Up), FVector(0.5, Out, Up)};
        const TCHAR* FacingNames[4] = {TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south")};
        for (int32 Face = 0; Face < 4; ++Face)
        {
            Info.Facing = uint8(Face);
            Row(FString::Printf(TEXT("Points: wall torch facing %s"), FacingNames[Face]),
                VoxelLightSources::GetLocalPoint(Info, 0, Point) && Point.Equals(Expected[Face], 1e-10));
        }
        Row(TEXT("Points: wall torch 0.263 out and 0.854 up"),
            FMath::Abs(VoxelLightSources::WallTorchOut - 0.263) < 5e-4 &&
                FMath::Abs(VoxelLightSources::WallTorchHeight - 0.854) < 5e-4);
        Info.Shape = EVoxelLightShape::Cube;
        constexpr uint8 UpBit = 1 << int32(EVoxelFace::Up), EastBit = 1 << int32(EVoxelFace::East),
                        WestBit = 1 << int32(EVoxelFace::West), NorthBit = 1 << int32(EVoxelFace::North);
        // Per axis: 0.6 from the centre toward the one open face of an axis, none for both or neither.
        Row(TEXT("Points: cube, top open"),
            VoxelLightSources::GetLocalPoint(Info, UpBit, Point) && Near(Point, FVector(0.5, 0.5, 1.1)));
        Row(TEXT("Points: cube, west open"),
            VoxelLightSources::GetLocalPoint(Info, WestBit, Point) && Near(Point, FVector(-0.1, 0.5, 0.5)));
        Row(TEXT("Points: cube, top and east open, 0.6 on both axes"),
            VoxelLightSources::GetLocalPoint(Info, UpBit | EastBit, Point) && Near(Point, FVector(1.1, 0.5, 1.1)));
        Row(TEXT("Points: cube, east, north and top open, 0.6 on each axis"),
            VoxelLightSources::GetLocalPoint(Info, UpBit | EastBit | NorthBit, Point) &&
                Near(Point, FVector(1.1, -0.1, 1.1)));
        Row(TEXT("Points: cube, east and west with the top, only the top's axis moves"),
            VoxelLightSources::GetLocalPoint(Info, UpBit | EastBit | WestBit, Point) &&
                Near(Point, FVector(0.5, 0.5, 1.1)));
        Row(TEXT("Points: cube, east and west open take the first"),
            VoxelLightSources::GetLocalPoint(Info, EastBit | WestBit, Point) && Near(Point, FVector(-0.1, 0.5, 0.5)));
        Row(TEXT("Points: cube, all open take the top"),
            VoxelLightSources::GetLocalPoint(Info, 0x3F, Point) && Near(Point, FVector(0.5, 0.5, 1.1)));
        Row(TEXT("Points: buried cube gets none"), !VoxelLightSources::GetLocalPoint(Info, 0, Point));
        Info.Shape = EVoxelLightShape::Model;
        Row(TEXT("Points: model at the cell centre"),
            VoxelLightSources::GetLocalPoint(Info, 0, Point) && Point == FVector(0.5));
        Info.Shape = EVoxelLightShape::None;
        Row(TEXT("Points: no shape, no point"), !VoxelLightSources::GetLocalPoint(Info, 0x3F, Point));
    }

    // --- Scan ----------------------------------------------------------------------------------------------------
    {
        const uint16 StoneState = StateOf(TEXT("stone")), GlowstoneState = StateOf(TEXT("glowstone"));
        const uint16 TorchState = StateOf(TEXT("torch")), CryingState = StateOf(TEXT("crying_obsidian"));
        const uint16 LavaState = StateOf(TEXT("lava")),
                     LitOre = StateOf(TEXT("redstone_ore"), {FLightTestProperty(TEXT("lit"), TEXT("true"))});
        const uint16 Pickles3 = StateOf(TEXT("sea_pickle"),
            {FLightTestProperty(TEXT("pickles"), TEXT("3")), FLightTestProperty(TEXT("waterlogged"), TEXT("true"))});
        uint16 WallStates[4];
        const TCHAR* FacingNames[4] = {TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south")};
        for (int32 Face = 0; Face < 4; ++Face)
            WallStates[Face] = StateOf(TEXT("wall_torch"), {FLightTestProperty(TEXT("facing"), FacingNames[Face])});
        // Section 4 and the dark column hold the lit ore: it must be the lit state, not the default one.
        const bool bLitOre = LitOre && LitOre != StateOf(TEXT("redstone_ore"));
        Row(TEXT("Scan: the lit redstone_ore fixture is not the default state"), bLitOre);
        // States without light, for the busy sections.
        TArray<uint16> Plain;
        for (int32 Id = 1; Id < Lights.GetStateCount() && Plain.Num() < 300; ++Id)
            if (BlockTable.Get(uint16(Id)).LightEmission == 0) Plain.Add(uint16(Id));

        const FIntPoint Position(2, -3);
        FVoxelColumn Column(Position), StoneColumn;
        TArray<uint16> States;
        States.SetNumUninitialized(FVoxelSection::Volume);
        auto Fill = [&States](uint16 State)
        {
            for (uint16& Cell : States)
                Cell = State;
        };
        auto At = [&States](int32 X, int32 Y, int32 Z) -> uint16& { return States[FVoxelSection::GetIndex(X, Y, Z)]; };
        Fill(StoneState);
        for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
            StoneColumn.SetSectionStates(Section, States);

        // Section 0, 16-bit: 300 states without light, a torch, a wall torch, glowstone at the column's west edge and
        // at the world's floor, crying obsidian at the section's top east corner.
        for (int32 Index = 0; Index < FVoxelSection::Volume; ++Index)
            States[Index] = Plain[Index % Plain.Num()];
        At(0, 5, 3) = GlowstoneState;
        At(4, 4, 0) = GlowstoneState;
        At(7, 7, 7) = TorchState;
        At(8, 8, 8) = WallStates[1];
        At(15, 3, 15) = CryingState;
        Column.SetSectionStates(0, States);
        // Section 1: uniform stone.
        Fill(StoneState);
        Column.SetSectionStates(1, States);
        // Section 2, 4-bit: stone with glowstone buried, under one open face and under two, a torch, wall torches.
        At(5, 5, 5) = GlowstoneState;
        At(6, 6, 6) = GlowstoneState;
        At(6, 6, 7) = 0;
        At(9, 9, 9) = GlowstoneState;
        At(9, 9, 10) = 0;
        At(10, 9, 9) = 0;
        At(2, 2, 2) = TorchState;
        for (int32 Face = 0; Face < 4; ++Face)
            At(12, 2 + Face * 3, 4) = WallStates[Face];
        Column.SetSectionStates(2, States);
        // Section 3, 8-bit: 40 states without light, sea pickles, lava, and edge cubes walled in by stone: crying
        // obsidian at the east edge (no column there: open) and glowstone at the west edge (stone column: buried).
        FVoxelXoroshiro Stream(0x7031);
        for (int32 Index = 0; Index < FVoxelSection::Volume; ++Index)
            States[Index] = Plain[Stream.NextInt(40)];
        At(3, 3, 3) = Pickles3;
        At(4, 3, 3) = LavaState;
        for (const int32 EdgeX : {15, 0})
        {
            const int32 InnerX = EdgeX == 15 ? 14 : 1;
            At(EdgeX, 8, 8) = EdgeX == 15 ? CryingState : GlowstoneState;
            At(InnerX, 8, 8) = At(EdgeX, 7, 8) = At(EdgeX, 9, 8) = At(EdgeX, 8, 7) = At(EdgeX, 8, 9) = StoneState;
        }
        Column.SetSectionStates(3, States);
        // Section 4, 4-bit: no light in the palette (lava and lit redstone ore glow below the table's level).
        for (int32 Index = 0; Index < FVoxelSection::Volume; ++Index)
            States[Index] = Index % 3 == 0 ? LavaState : Index % 3 == 1 ? LitOre : StoneState;
        Column.SetSectionStates(4, States);
        // Section 5: uniform glowstone; 6 and above air, but a torch on the world's top cell.
        Fill(GlowstoneState);
        Column.SetSectionStates(5, States);
        Fill(0);
        At(3, 3, 15) = TorchState;
        At(4, 4, 14) = WallStates[3];
        Column.SetSectionStates(VoxelConstants::SectionCount - 1, States);

        Row(TEXT("Scan: the fixture's sections are 16-bit, uniform, 4-bit, 8-bit, 4-bit, uniform glowstone"),
            Column.GetSection(0).GetBitsPerIndex() == 16 && Column.GetSection(1).IsUniform() &&
                Column.GetSection(2).GetBitsPerIndex() == 4 && Column.GetSection(3).GetBitsPerIndex() == 8 &&
                Column.GetSection(4).GetBitsPerIndex() == 4 && Column.GetSection(5).IsUniform() &&
                Column.GetSection(5).GetUniformState() == GlowstoneState);

        FLightTestWorld World;
        World.Position = Position;
        World.Around[1][1] = &Column;
        World.Around[1][0] = &StoneColumn; // west
        World.Around[0][1] = &StoneColumn; // north
        const FVoxelNeighborhood Neighborhood = World.MakeNeighborhood();
        TArray<FVoxelLightSource> Found, Reference;
        VoxelLightSources::GatherSources(Column, Position, Neighborhood, Lights, Found);
        World.GatherBruteForce(Lights, Reference);
        Row(FString::Printf(TEXT("Scan: matches the brute-force scan of every cell (%d sources)"), Found.Num()),
            Found.Num() > 0 && SameLightSources(Found, Reference));
        int32 BySection[VoxelConstants::SectionCount] = {};
        for (const FVoxelLightSource& Source : Found)
            ++BySection[(Source.Cell.Z - VoxelConstants::MinZ) / 16];
        Row(TEXT("Scan: sources in the 16-bit, 4-bit, 8-bit, uniform and top sections, none in the others"),
            BySection[0] >= 4 && BySection[1] == 0 && BySection[2] == 7 && BySection[3] == 2 && bLitOre &&
                BySection[4] == 0 && BySection[5] > 0 && BySection[VoxelConstants::SectionCount - 1] == 2);
        const FIntVector Base(Position.X * 16, Position.Y * 16, VoxelConstants::MinZ);
        auto Cell = [&Base](int32 Section, int32 X, int32 Y, int32 Z)
        { return Base + FIntVector(X, Y, Section * 16 + Z); };
        const FVoxelLightSource* OneOpen = FindLightTestSource(Found, Cell(2, 6, 6, 6));
        const FVoxelLightSource* TwoOpen = FindLightTestSource(Found, Cell(2, 9, 9, 9));
        Row(TEXT("Scan: glowstone under one open face"),
            OneOpen && Near(OneOpen->Point, FVector(Cell(2, 6, 6, 6)) + FVector(0.5, 0.5, 1.1)) &&
                OneOpen->Level == 15);
        Row(TEXT("Scan: glowstone with two open faces, 0.6 out on both axes"),
            TwoOpen && Near(TwoOpen->Point, FVector(Cell(2, 9, 9, 9)) + FVector(1.1, 0.5, 1.1)));
        Row(TEXT("Scan: buried glowstone gets none"), !FindLightTestSource(Found, Cell(2, 5, 5, 5)));
        const FVoxelLightSource* EastEdge = FindLightTestSource(Found, Cell(3, 15, 8, 8));
        Row(TEXT("Scan: a cube at the column's east edge opens toward the missing column"),
            EastEdge && Near(EastEdge->Point, FVector(Cell(3, 15, 8, 8)) + FVector(1.1, 0.5, 0.5)) &&
                EastEdge->Level == 10);
        Row(TEXT("Scan: a cube at the west edge is buried by the neighbour column"),
            !FindLightTestSource(Found, Cell(3, 0, 8, 8)));
        const FVoxelLightSource* Floor = FindLightTestSource(Found, Cell(2, 2, 2, 2));
        Row(TEXT("Scan: a torch's point and key"),
            Floor && Floor->Point == FVector(Cell(2, 2, 2, 2)) + FVector(0.5, 0.5, 0.6875) &&
                Floor->Key == VoxelLightSources::MakeKey(Cell(2, 2, 2, 2)));
        Row(TEXT("Scan: the world's bottom reads as air below glowstone"),
            FindLightTestSource(Found, Cell(0, 4, 4, 0)) != nullptr);
        bool bOrdered = true;
        for (int32 Index = 1; Index < Found.Num(); ++Index)
        {
            const FIntVector& A = Found[Index - 1].Cell;
            const FIntVector& B = Found[Index].Cell;
            bOrdered &= A.Z < B.Z || (A.Z == B.Z && (A.Y < B.Y || (A.Y == B.Y && A.X < B.X)));
        }
        Row(TEXT("Scan: output in cell order (Z, then Y, then X)"), bOrdered);
        TArray<FVoxelLightSource> Again;
        VoxelLightSources::GatherSources(Column, Position, Neighborhood, Lights, Again);
        Row(TEXT("Scan: repeatable"), SameLightSources(Found, Again));
        TArray<FVoxelLightSource> Appended = Found;
        VoxelLightSources::GatherSources(Column, Position, Neighborhood, Lights, Appended);
        Row(TEXT("Scan: appends to its output"), Appended.Num() == Found.Num() * 2);

        FVoxelColumn Dark(Position);
        for (int32 Index = 0; Index < FVoxelSection::Volume; ++Index)
            States[Index] = Index % 5 == 0 ? LavaState : Index % 5 == 1 ? LitOre : Plain[Index % 40];
        for (int32 Section = 0; Section < 8; ++Section)
            Dark.SetSectionStates(Section, States);
        TArray<FVoxelLightSource> DarkSources;
        VoxelLightSources::GatherSources(Dark, Position, FVoxelNeighborhood(), Lights, DarkSources);
        Row(TEXT("Scan: a column without emitters (lit redstone_ore among them) gives none"),
            bLitOre && DarkSources.IsEmpty());

        // Timing: a generated-like column with and without emitters (for the mesh-per-column gate).
        FVoxelXoroshiro TerrainStream(0x5EED);
        FVoxelColumn Plainland, Lit;
        MakeLightTestTerrain(Registry, false, TerrainStream, Plainland);
        MakeLightTestTerrain(Registry, true, TerrainStream, Lit);
        auto TimeScan = [&Lights](const FVoxelColumn& Timed, int32 Runs, int32& OutSources)
        {
            TArray<FVoxelLightSource> Scratch;
            const double Start = FPlatformTime::Seconds();
            for (int32 Run = 0; Run < Runs; ++Run)
            {
                Scratch.Reset();
                VoxelLightSources::GatherSources(Timed, FIntPoint::ZeroValue, FVoxelNeighborhood(), Lights, Scratch);
            }
            OutSources = Scratch.Num();
            return (FPlatformTime::Seconds() - Start) * 1e6 / Runs;
        };
        int32 PlainSources = 0, LitSources = 0;
        TimeScan(Plainland, 200, PlainSources);
        const double PlainUs = TimeScan(Plainland, 5000, PlainSources);
        const double LitUs = TimeScan(Lit, 2000, LitSources);
        int32 Paletted = 0;
        for (int32 Section = 0; Section <= 8; ++Section)
            Paletted +=
                Lit.GetSection(Section).GetBitsPerIndex() == 4 || Lit.GetSection(Section).GetBitsPerIndex() == 8;
        AddInfo(FString::Printf(TEXT("Scan timing: %.2f us per column without emitters (%d sources), %.2f us with "
                                     "emitters in 3 sections (%d sources); %d paletted sections"),
            PlainUs, PlainSources, LitUs, LitSources, Paletted));
        Row(TEXT("Scan timing: the generated-like columns give 0 and 9 sources"), PlainSources == 0 && LitSources == 9);
    }

    // --- Pick ----------------------------------------------------------------------------------------------------
    {
        const FVector Camera = FVector::ZeroVector;
        TArray<FVoxelLightPick> Picks;
        // The R cut: within means at most R.
        {
            const FVoxelLightSource Cut[] = {MakeLightTestSource(FVector(9.9, 0, 0), 14),
                MakeLightTestSource(FVector(0, 10, 0), 14), MakeLightTestSource(FVector(0, 0, -10.1), 14)};
            VoxelLightSources::PickLights(Cut, Camera, 10, 10, 2, Picks);
            Row(TEXT("Pick: R keeps 9.9 and 10, drops 10.1"),
                Picks.Num() == 2 && Picks[0].Key == Cut[0].Key && Picks[1].Key == Cut[1].Key);
        }
        // The K cap and its clamp: 150 sources 3 blocks apart.
        TArray<FVoxelLightSource> Row150;
        for (int32 Index = 1; Index <= 150; ++Index)
            Row150.Add(MakeLightTestSource(FVector(3.0 * Index + 0.5, 0.5, 0.5), 14));
        VoxelLightSources::PickLights(Row150, Camera, 5, 1000, 2, Picks);
        bool bNearest = Picks.Num() == 5;
        for (int32 Index = 0; bNearest && Index < 5; ++Index)
            bNearest &= Picks[Index].Key == Row150[Index].Key && Picks[Index].Members == 1;
        Row(TEXT("Pick: K = 5 keeps the 5 nearest, nearest first"), bNearest);
        VoxelLightSources::PickLights(Row150, Camera, 200, 1000, 2, Picks);
        Row(TEXT("Pick: K = 200 is clamped to 100"), Picks.Num() == 100 && Picks.Last().Key == Row150[99].Key);
        VoxelLightSources::PickLights(Row150, Camera, -3, 1000, 2, Picks);
        Row(TEXT("Pick: K below 0 gives none"), Picks.IsEmpty());
        // Merging: under 2 blocks but not at exactly 2.
        {
            const FVoxelLightSource Close[] = {
                MakeLightTestSource(FVector(0, 0, 5), 14), MakeLightTestSource(FVector(1.99, 0, 5), 14)};
            VoxelLightSources::PickLights(Close, Camera, 10, 100, 2, Picks);
            Row(TEXT("Pick: 1.99 blocks apart merge"), Picks.Num() == 1 && Picks[0].Members == 2);
            const FVoxelLightSource Apart[] = {
                MakeLightTestSource(FVector(0, 0, 5), 14), MakeLightTestSource(FVector(2, 0, 5), 14)};
            VoxelLightSources::PickLights(Apart, Camera, 10, 100, 2, Picks);
            Row(TEXT("Pick: exactly 2 blocks apart do not merge"), Picks.Num() == 2);
            // A merge needs no free slot: with K = 1 the second source still joins.
            VoxelLightSources::PickLights(Close, Camera, 1, 100, 2, Picks);
            Row(TEXT("Pick: a merge needs no free slot"), Picks.Num() == 1 && Picks[0].Members == 2);
        }
        // The weighted centroid, summed weight and max level: a torch (weight 1) and glowstone (15/14), then a second
        // torch.
        {
            const FVoxelLightSource Group[] = {MakeLightTestSource(FVector(0, 0, 5), 14),
                MakeLightTestSource(FVector(1, 0, 5), 15), MakeLightTestSource(FVector(0, 1, 5.5), 14)};
            VoxelLightSources::PickLights(TConstArrayView<FVoxelLightSource>(Group, 2), Camera, 10, 100, 2, Picks);
            Row(TEXT("Pick: torch + glowstone: centroid 15/29 toward the glowstone, weight 29/14, level 15"),
                Picks.Num() == 1 && Picks[0].Centroid.Equals(FVector(15.0 / 29, 0, 5), 1e-6) &&
                    FMath::IsNearlyEqual(Picks[0].Weight, 29.f / 14, 1e-5f) && Picks[0].Level == 15 &&
                    Picks[0].Members == 2 && Picks[0].Key == Group[0].Key && Picks[0].SeedPoint == Group[0].Point);
            VoxelLightSources::PickLights(Group, Camera, 10, 100, 2, Picks);
            Row(TEXT("Pick: three members: weight 43/14, centroid weighted"),
                Picks.Num() == 1 && Picks[0].Members == 3 && FMath::IsNearlyEqual(Picks[0].Weight, 43.f / 14, 1e-5f) &&
                    Picks[0].Centroid.Equals(FVector(15.0 / 43, 14.0 / 43, (5 * 29.0 + 5.5 * 14) / 43), 1e-6));
        }
        // Determinism under shuffled input: mirrored points (equal distances from the camera at the origin), every
        // level 10-15, close pairs that merge, and an exact duplicate.
        {
            TArray<FVoxelLightSource> Scatter;
            FVoxelXoroshiro ScatterStream(0xF1E1D);
            for (int32 Index = 0; Index < 150; ++Index)
            {
                const FVector Point(ScatterStream.NextInt(4000) / 100.0 - 20, ScatterStream.NextInt(4000) / 100.0 - 20,
                    ScatterStream.NextInt(4000) / 100.0 - 20);
                const uint8 Level = uint8(10 + ScatterStream.NextInt(6));
                Scatter.Add(MakeLightTestSource(Point, Level));
                Scatter.Add(MakeLightTestSource(-Point, Level));
                if (Index % 5 == 0) Scatter.Add(MakeLightTestSource(Point + FVector(0.7, 0.3, 0), 14));
            }
            const FVoxelLightSource Duplicate = Scatter[0];
            Scatter.Add(Duplicate);
            TArray<FVoxelLightPick> Baseline;
            VoxelLightSources::PickLights(Scatter, Camera, 40, 25, 2, Baseline);
            bool bSame = Baseline.Num() == 40;
            for (int32 Shuffle = 0; Shuffle < 8; ++Shuffle)
            {
                for (int32 Index = Scatter.Num() - 1; Index > 0; --Index)
                    Scatter.Swap(Index, ScatterStream.NextInt(Index + 1));
                VoxelLightSources::PickLights(Scatter, Camera, 40, 25, 2, Picks);
                bSame &= SameLightPicks(Baseline, Picks);
            }
            int32 Merged = 0;
            for (const FVoxelLightPick& Pick : Baseline)
                Merged += Pick.Members > 1;
            Row(FString::Printf(TEXT("Pick: the same picks for 8 shuffles (40 picks, %d merged)"), Merged),
                bSame && Merged > 0);
            // Stable keys: each pick's key is its seed cell's, and they are unique.
            TSet<uint64> Keys;
            bool bKeys = true;
            for (const FVoxelLightPick& Pick : Baseline)
            {
                bKeys &= Pick.Key == VoxelLightSources::MakeKey(Pick.SeedCell);
                Keys.Add(Pick.Key);
            }
            Row(TEXT("Pick: keys are the seed cells' and unique"), bKeys && Keys.Num() == Baseline.Num());
            bool bOrdered = true;
            for (int32 Index = 1; Index < Baseline.Num(); ++Index)
                bOrdered &= Baseline[Index - 1].SeedPoint.SizeSquared() <= Baseline[Index].SeedPoint.SizeSquared();
            Row(TEXT("Pick: picks in seed order, nearest first"), bOrdered);
        }
        // Stable keys while the camera moves: separate sources keep their picks' keys.
        {
            TArray<FVoxelLightPick> Before, After;
            VoxelLightSources::PickLights(
                TConstArrayView<FVoxelLightSource>(Row150.GetData(), 20), Camera, 20, 1000, 2, Before);
            VoxelLightSources::PickLights(
                TConstArrayView<FVoxelLightSource>(Row150.GetData(), 20), FVector(40.5, 3, 1), 20, 1000, 2, After);
            TSet<uint64> BeforeKeys, AfterKeys;
            for (const FVoxelLightPick& Pick : Before)
                BeforeKeys.Add(Pick.Key);
            for (const FVoxelLightPick& Pick : After)
                AfterKeys.Add(Pick.Key);
            Row(TEXT("Pick: the camera moving reorders the picks but keeps their keys"),
                Before.Num() == 20 && After.Num() == 20 && Before[0].Key != After[0].Key &&
                    BeforeKeys.Difference(AfterKeys).IsEmpty());
            const FIntVector FarCell(30000000, -30000000, VoxelConstants::MaxZ),
                CloseCell(-1, -1, VoxelConstants::MinZ);
            Row(TEXT("Pick: keys tell neighbouring and distant cells apart"),
                VoxelLightSources::MakeKey(FarCell) != VoxelLightSources::MakeKey(CloseCell) &&
                    VoxelLightSources::MakeKey(CloseCell) !=
                        VoxelLightSources::MakeKey(CloseCell + FIntVector(1, 0, 0)) &&
                    VoxelLightSources::MakeKey(CloseCell) !=
                        VoxelLightSources::MakeKey(CloseCell + FIntVector(0, 1, 0)) &&
                    VoxelLightSources::MakeKey(CloseCell) !=
                        VoxelLightSources::MakeKey(CloseCell + FIntVector(0, 0, 1)));
        }
    }

    AddInfo(FString::Printf(TEXT("Voxel.Lights: %d of %d rows pass"), Passed, Rows));
    return true;
}
#endif
