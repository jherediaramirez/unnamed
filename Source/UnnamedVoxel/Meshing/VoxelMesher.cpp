#include "Meshing/VoxelMesher.h"
#include "Biome/VoxelBiomeStorage.h"
#include "Biome/VoxelBiomeTints.h"
#include "Blocks/VoxelBlockTable.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Storage/VoxelColumn.h"
#include "VoxelConstants.h"
#include "VoxelStats.h"

namespace
{
    constexpr int32 PaddedSize = FVoxelPaddedSection::Size;
    constexpr int32 SectionSize = FVoxelSection::Size;
    // Padded index step per axis (X, Y, Z).
    constexpr int32 AxisStep[3] = {1, PaddedSize, PaddedSize* PaddedSize};
    // Every corner open: level 3 in all four 2-bit slots.
    constexpr uint8 OpenCorners = 0xFF;
    constexpr uint8 AllFaces = 0x3F;

    // Greedy-merge keys: the tint above state and corner levels, plus one so 0 means "no face".
    FORCEINLINE uint64 MakeKey(uint16 State, uint8 Corners, uint32 Tint)
    {
        return uint64(Tint) << 32 | (((uint32(State) << 8) | Corners) + 1);
    }

    // Biome tints for one section, as 26.3's biome blend: a face takes the mean of its source's colour over the
    // (2 Radius + 1)^2 blocks around its block at the same height, channel by channel in integers. Each block takes
    // its own jittered biome (FVoxelBiomeStorage::GetBlock) from the column that holds it, so a face's colour does not
    // depend on which section meshes it. A block in a missing column (the edge of a finite area or of the edge-walls
    // view) takes the biome of the meshed column's nearest block; blocks without biomes take the state's plains tint.
    class FTintSampler
    {
    public:
        FTintSampler(const FVoxelTintTable& InTints, const FVoxelNeighborhood& InNeighborhood, int32 SectionIndex,
            int32 InRadius, bool bInShortcut)
            : Tints(InTints), Neighborhood(InNeighborhood), Radius(InRadius), bShortcut(bInShortcut)
        {
            check(Radius >= 0 && Radius <= Margin && Tints.Num() < int32(Unknown));
            const FVoxelColumn* Center = Neighborhood.Get(0, 0);
            Origin = Center ? Center->GetPosition() * VoxelConstants::ColumnSize : FIntPoint::ZeroValue;
            BaseZ = VoxelConstants::MinZ + SectionIndex * SectionSize;
            FMemory::Memset(Biomes, Unknown, sizeof(Biomes));
            FMemory::Memzero(Swamp, sizeof(Swamp));
            FMemory::Memzero(GetJitters().Filled, sizeof(FJitterScratch::Filled));
        }

        // The colour of a face of Cell (section coordinates) whose state takes Source; Fallback is its plains tint.
        uint32 Get(EVoxelTintSource Source, const FIntVector& Cell, uint32 Fallback)
        {
            if (bShortcut)
            {
                // When every quart corner the blend's block lookups can pick holds one biome, every block takes it,
                // and the mean of one colour is that colour.
                const int32 Uniform = FindUniformBiome(Cell);
                if (Uniform != INDEX_NONE && !Tints.VariesWithPosition(Source, Uniform))
                    return Tints.GetColor(Source, Uniform, 0, 0, Fallback);
            }
            uint32 Sum[3] = {};
            for (int32 Y = Cell.Y - Radius; Y <= Cell.Y + Radius; ++Y)
                for (int32 X = Cell.X - Radius; X <= Cell.X + Radius; ++X)
                {
                    const uint32 Color = GetBlockColor(Source, X, Y, Cell.Z, Fallback);
                    Sum[0] += Color >> 16;
                    Sum[1] += (Color >> 8) & 0xFF;
                    Sum[2] += Color & 0xFF;
                }
            const uint32 Count = uint32((2 * Radius + 1) * (2 * Radius + 1));
            return (Sum[0] / Count) << 16 | (Sum[1] / Count) << 8 | Sum[2] / Count;
        }

    private:
        static constexpr int32 Margin = FVoxelMeshOptions::MaxTintBlend;
        static constexpr int32 Footprint = SectionSize + 2 * Margin;
        static constexpr uint8 Unknown = 0xFE, NoBiome = 0xFF;
        static_assert(Margin <= VoxelConstants::ColumnSize, "The blend must stay within the 3 x 3 columns");
        static_assert(SectionSize == VoxelConstants::ColumnSize && SectionSize == 16,
            "Block coordinates split into columns with >> 4 and & 15");

        static int32 GetFootprintIndex(int32 X, int32 Y) { return (Y + Margin) * Footprint + X + Margin; }

        // Each of the 3 x 3 columns' quart corner jitters, filled a layer at a time as lookups need them, so a
        // block's biome costs no hashes (FVoxelBiomeStorage::GetBlock with a cache gives the same answer). Scratch,
        // reused by every section a worker meshes: only one sampler may be alive per thread at a time (each lives for
        // one MeshSection, which never waits on other work).
        struct FJitterScratch
        {
            TArray<FVector3f> Columns[9];
            uint64 Filled[9][(FVoxelBiomeStorage::Layers + 63) / 64];
        };

        static FJitterScratch& GetJitters()
        {
            thread_local FJitterScratch Scratch;
            return Scratch;
        }

        static void FillLayer(const FVoxelBiomeStorage& Storage, FJitterScratch& Scratch, int32 Index, int32 Layer)
        {
            uint64& Bits = Scratch.Filled[Index][Layer >> 6];
            if (Bits & (1ull << (Layer & 63))) return;
            Bits |= 1ull << (Layer & 63);
            if (Scratch.Columns[Index].Num() != FVoxelBiomeStorage::Volume)
                Scratch.Columns[Index].SetNumUninitialized(FVoxelBiomeStorage::Volume);
            Storage.FillJitterLayer(Layer, Scratch.Columns[Index].GetData());
        }

        // A block's biome by section coordinates (X and Y may reach Margin into the neighbours), cached.
        int32 GetBiome(int32 X, int32 Y, int32 LocalZ)
        {
            uint8& Cached = Biomes[LocalZ][GetFootprintIndex(X, Y)];
            if (Cached == Unknown)
            {
                // Arithmetic shifts floor negative coordinates: -1, 0 or 1 column over.
                int32 DX = X >> 4, DY = Y >> 4, LocalX = X & 15, LocalY = Y & 15;
                const FVoxelColumn* Column = Neighborhood.Get(DX, DY);
                if (!Column)
                {
                    DX = DY = 0;
                    LocalX = FMath::Clamp(X, 0, SectionSize - 1);
                    LocalY = FMath::Clamp(Y, 0, SectionSize - 1);
                    Column = Neighborhood.Get(0, 0);
                }
                int32 Biome = INDEX_NONE;
                if (Column && !Column->GetBiomes().IsEmpty())
                {
                    const FVoxelBiomeStorage& Storage = Column->GetBiomes();
                    FJitterScratch& Scratch = GetJitters();
                    const int32 Index = (DY + 1) * 3 + DX + 1, Z = BaseZ + LocalZ;
                    const int32 Layer = (Z - VoxelConstants::MinZ) / FVoxelBiomeStorage::QuartSize;
                    FillLayer(Storage, Scratch, Index, Layer);
                    if (Layer + 1 < FVoxelBiomeStorage::Layers) FillLayer(Storage, Scratch, Index, Layer + 1);
                    Biome = Storage.GetBlock(LocalX, LocalY, Z, Scratch.Columns[Index].GetData());
                }
                Cached = Biome == INDEX_NONE ? NoBiome : uint8(Biome);
            }
            return Cached == NoBiome ? INDEX_NONE : int32(Cached);
        }

        uint32 GetBlockColor(EVoxelTintSource Source, int32 X, int32 Y, int32 LocalZ, uint32 Fallback)
        {
            const int32 Biome = GetBiome(X, Y, LocalZ);
            if (!Tints.VariesWithPosition(Source, Biome)) return Tints.GetColor(Source, Biome, 0, 0, Fallback);
            // Swamp grass depends on (x, y) alone; no colour is 0, so 0 marks "not yet".
            uint32& Cached = Swamp[GetFootprintIndex(X, Y)];
            if (!Cached) Cached = Tints.GetSwampGrass(Origin.X + X, Origin.Y + Y);
            return Cached;
        }

        // A quart's biome by section-relative quart coordinates (X and Y may reach into the neighbours); INDEX_NONE
        // without one.
        int32 GetQuart(int32 X, int32 Y, int32 Layer) const
        {
            const FVoxelColumn* Column = Neighborhood.Get(X >> 2, Y >> 2);
            if (!Column || Column->GetBiomes().IsEmpty()) return INDEX_NONE;
            const uint8 Biome = Column->GetBiomes().GetQuart(X & 3, Y & 3, Layer);
            return Biome == FVoxelBiomeStorage::None ? INDEX_NONE : int32(Biome);
        }

        // The one biome of every quart corner that the lookups of the blend's blocks can pick (each block's cell
        // corners, FVoxelBiomeStorage::GetBlock), or INDEX_NONE.
        int32 FindUniformBiome(const FIntVector& Cell) const
        {
            const int32 Layer = (BaseZ + Cell.Z - VoxelConstants::MinZ) / FVoxelBiomeStorage::QuartSize;
            const int32 TopLayer = FMath::Min(Layer + 1, FVoxelBiomeStorage::Layers - 1);
            const int32 MinX = (Cell.X - Radius) >> 2, MaxX = ((Cell.X + Radius) >> 2) + 1;
            const int32 MinY = (Cell.Y - Radius) >> 2, MaxY = ((Cell.Y + Radius) >> 2) + 1;
            const int32 Biome = GetQuart(MinX, MinY, Layer);
            if (Biome == INDEX_NONE) return INDEX_NONE;
            for (int32 Z = Layer; Z <= TopLayer; ++Z)
                for (int32 Y = MinY; Y <= MaxY; ++Y)
                    for (int32 X = MinX; X <= MaxX; ++X)
                        if (GetQuart(X, Y, Z) != Biome) return INDEX_NONE;
            return Biome;
        }

        const FVoxelTintTable& Tints;
        const FVoxelNeighborhood& Neighborhood;
        int32 Radius;
        bool bShortcut;
        FIntPoint Origin;
        int32 BaseZ;
        uint8 Biomes[SectionSize][Footprint * Footprint];
        uint32 Swamp[Footprint * Footprint];
    };

    // A face's tint: its state's own, or its biome's colour when the face is tinted from a biome and tints are on.
    struct FFaceTints
    {
        const FVoxelTintTable* Table = nullptr;
        const FVoxelNeighborhood* Neighborhood = nullptr;
        int32 SectionIndex = 0;
        int32 Radius = 0;
        bool bShortcut = true;
        TOptional<FTintSampler> Sampler;

        uint32 Get(const FVoxelStateInfo& Info, int32 Face, const FIntVector& Cell)
        {
            return GetForCode(Info, Info.Faces[Face] | Info.Faces[VoxelFaces::OverlayIndex], Cell);
        }

        // A face or model quad whose codes (or-ed together) are Codes.
        uint32 GetForCode(const FVoxelStateInfo& Info, uint32 Codes, const FIntVector& Cell)
        {
            if (!Table || Info.TintSource < EVoxelTintSource::Grass || !(Codes & VoxelFaces::TintBit)) return Info.Tint;
            if (!Sampler) Sampler.Emplace(*Table, *Neighborhood, SectionIndex, Radius, bShortcut);
            return Sampler->Get(Info.TintSource, Cell, Info.Tint);
        }
    };

    // Occlusion levels at a face's four corners, (-,-), (+,-), (+,+), (-,+) across it, from the cells around the one
    // in front of the face: two darkening sides make the darkest corner whatever the diagonal holds.
    uint8 GetCorners(const uint8* Casts, int32 Front, int32 StepU, int32 StepV)
    {
        constexpr int32 SignU[4] = {-1, 1, 1, -1}, SignV[4] = {-1, -1, 1, 1};
        uint8 Corners = 0;
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            const int32 DU = SignU[Corner] * StepU, DV = SignV[Corner] * StepV;
            const int32 SideU = Casts[Front + DU], SideV = Casts[Front + DV], Diagonal = Casts[Front + DU + DV];
            const int32 Level = SideU && SideV ? 0 : 3 - (SideU + SideV + Diagonal);
            Corners |= uint8(Level << (2 * Corner));
        }
        return Corners;
    }

    struct FEmitter
    {
        FVoxelMeshData& Out;
        // World Z of the section's first block row.
        int32 BaseZ;
        FFaceTints& Tints;
        // World X and Y of the column's minimum corner: model offsets and alternatives read world cells.
        FIntPoint ColumnOrigin = FIntPoint::ZeroValue;

        // A quad on Face: Plane along the face's axis, [U0, U1] x [V0, V1] across it (section blocks; U and V are the
        // next two axes after it), Cell the section cell whose corner is the UV origin. Corners holds the occlusion
        // levels at (-,-), (+,-), (+,+), (-,+); Tint is every vertex's packed sRGB colour.
        void Quad(const FVoxelStateInfo& Info, int32 Face, float Plane, float U0, float U1, float V0, float V1,
            const FIntVector& Cell, uint8 Corners, uint32 Tint) const
        {
            const int32 Axis = VoxelFaces::GetAxis(Face), AxisU = (Axis + 1) % 3, AxisV = (Axis + 2) % 3;
            // Corner order turns against the face normal, which RealtimeMesh draws as the front: positive faces walk
            // V first, negative faces U first.
            const bool bPositive = VoxelFaces::IsPositive(Face);
            const float CornerU[4] = {U0, bPositive ? U0 : U1, U1, bPositive ? U1 : U0};
            const float CornerV[4] = {V0, bPositive ? V1 : V0, V1, bPositive ? V0 : V1};
            const int32 Occlusion[4] = {0, bPositive ? 3 : 1, 2, bPositive ? 1 : 3};

            FVoxelMeshLayer& Layer = Out.Layers[int32(Info.Layer)];
            const uint32 First = uint32(Layer.Positions.Num());
            const FVector3f Origin(float(Cell.X), float(Cell.Y), float(Cell.Z));
            const FVector2f Codes(float(Info.Faces[Face]), float(Info.Faces[VoxelFaces::OverlayIndex]));
            const FVector2f TintFace{float(Tint), float(Face)};
            int32 Levels[4];
            for (int32 Corner = 0; Corner < 4; ++Corner)
            {
                float Coordinates[3];
                Coordinates[Axis] = Plane;
                Coordinates[AxisU] = CornerU[Corner];
                Coordinates[AxisV] = CornerV[Corner];
                const FVector3f Local(Coordinates[0], Coordinates[1], Coordinates[2]);
                Layer.Positions.Add(FVector3f(Local.X, Local.Y, Local.Z + BaseZ));
                Layer.BlockUVs.Add(VoxelMesher::GetCanonicalUV(Face, Local - Origin));
                Layer.Codes.Add(Codes);
                Layer.TintFaces.Add(TintFace);
                Levels[Corner] = (Corners >> (2 * Occlusion[Corner])) & 3;
                Layer.Colors.Add(FColor(uint8(Levels[Corner] * 85), uint8(Info.LightEmission * 17), 255, 255));
            }
            // Split along the brighter diagonal, so shading interpolates without a dark streak (both splits keep the
            // corner order, so the quad still faces the same way).
            if (Levels[0] + Levels[2] < Levels[1] + Levels[3])
                Layer.Indices.Append({First + 1, First + 2, First + 3, First + 1, First + 3, First});
            else
                Layer.Indices.Append({First, First + 1, First + 2, First, First + 2, First + 3});
        }

        // A model state's quads at Cell (section coordinates; Index its padded index): one alternative of each part,
        // moved by the block's offset. A quad is hidden when its cull face's neighbour covers that side. Normals come
        // from the shade direction (the face index), light from the state or the element, whichever is brighter; a
        // quad lying near a cell side, in a model with ambient occlusion, takes the corner occlusion of the full face
        // there (FVoxelModelQuad::OcclusionFront).
        void Model(const FVoxelBlockTable& Table, const FVoxelStateInfo& Info, const FVoxelMeshOptions& Options,
            const uint8* Opaque, const uint8* Casts, int32 Index, const FIntVector& Cell) const
        {
            const FIntVector World(ColumnOrigin.X + Cell.X, ColumnOrigin.Y + Cell.Y, BaseZ + Cell.Z);
            const FVector3f Base = FVector3f(float(Cell.X), float(Cell.Y), float(Cell.Z + BaseZ)) +
                VoxelModels::GetOffset(Info, World.X, World.Y);
            FVoxelMeshLayer& Layer = Out.Layers[int32(EVoxelLayer::Model)];
            // Every tinted quad of the cell takes the same colour: looked up once, on the first.
            TOptional<uint32> CellTint;
            const int32* Part = Table.GetModelParts(Info);
            for (int32 PartIndex = 0; PartIndex < Info.ModelPartCount; ++PartIndex)
            {
                const int32 Alternatives = *Part++;
                const int32 ModelIndex =
                    Part[VoxelModels::PickAlternative(World.X, World.Y, World.Z, PartIndex, Alternatives)];
                Part += Alternatives;
                for (const FVoxelModelQuad& ModelQuad : Table.GetModelQuads(ModelIndex))
                {
                    if (ModelQuad.CullFace >= 0)
                    {
                        const int32 Axis = VoxelFaces::GetAxis(ModelQuad.CullFace);
                        const int32 Neighbor =
                            Index + (VoxelFaces::IsPositive(ModelQuad.CullFace) ? AxisStep[Axis] : -AxisStep[Axis]);
                        if ((Opaque[Neighbor] >> VoxelFaces::GetOpposite(ModelQuad.CullFace)) & 1) continue;
                    }
                    // Occlusion levels 0-3 at the quad's corners, read off the four cell corners of the full face it
                    // lies on.
                    float Levels[4] = {3, 3, 3, 3};
                    if (ModelQuad.OcclusionFront >= 0 && Options.bAmbientOcclusion)
                    {
                        const int32 Face = ModelQuad.Facing, Axis = VoxelFaces::GetAxis(Face);
                        const int32 AxisU = (Axis + 1) % 3, AxisV = (Axis + 2) % 3;
                        const int32 Front = ModelQuad.OcclusionFront == FVoxelModelQuad::OwnCell
                            ? Index
                            : Index + (VoxelFaces::IsPositive(Face) ? AxisStep[Axis] : -AxisStep[Axis]);
                        const uint8 Corners = GetCorners(Casts, Front, AxisStep[AxisU], AxisStep[AxisV]);
                        auto Level = [Corners](int32 Corner) { return float((Corners >> (2 * Corner)) & 3); };
                        for (int32 Vertex = 0; Vertex < 4; ++Vertex)
                        {
                            const float U = FMath::Clamp(ModelQuad.Positions[Vertex][AxisU], 0.f, 1.f);
                            const float V = FMath::Clamp(ModelQuad.Positions[Vertex][AxisV], 0.f, 1.f);
                            // Corners (-,-), (+,-), (+,+), (-,+) across the face.
                            Levels[Vertex] =
                                FMath::Lerp(FMath::Lerp(Level(0), Level(1), U), FMath::Lerp(Level(3), Level(2), U), V);
                        }
                    }
                    const uint8 Shade = Options.bModelShadeUnturned && ModelQuad.Shade != ModelQuad.Facing
                        ? uint8(EVoxelFace::Up)
                        : ModelQuad.Shade;
                    const FVector2f Codes(float(ModelQuad.Code), 0.f);
                    if ((ModelQuad.Code & VoxelFaces::TintBit) && !CellTint)
                        CellTint = Tints.GetForCode(Info, ModelQuad.Code, Cell);
                    const uint32 Tint = ModelQuad.Code & VoxelFaces::TintBit ? *CellTint : Info.Tint;
                    const FVector2f TintFace{float(Tint), float(Shade)};
                    const uint8 Light = uint8(FMath::Max<int32>(Info.LightEmission, ModelQuad.Light) * 17);
                    const uint32 First = uint32(Layer.Positions.Num());
                    for (int32 Vertex = 0; Vertex < 4; ++Vertex)
                    {
                        Layer.Positions.Add(Base + ModelQuad.Positions[Vertex]);
                        Layer.BlockUVs.Add(ModelQuad.UVs[Vertex]);
                        Layer.Codes.Add(Codes);
                        Layer.TintFaces.Add(TintFace);
                        Layer.Colors.Add(FColor(uint8(FMath::RoundToInt32(Levels[Vertex] * 85)), Light, 255, 255));
                    }
                    // FaceInfo order, split along the brighter diagonal as the cube faces are.
                    if (Levels[0] + Levels[2] < Levels[1] + Levels[3])
                        Layer.Indices.Append({First + 1, First + 2, First + 3, First + 1, First + 3, First});
                    else
                        Layer.Indices.Append({First, First + 1, First + 2, First, First + 2, First + 3});
                }
            }
        }

        // A face of a partial block: Low and High are the block's vertical extent within the cell (slabs, fluids).
        void CellFace(const FVoxelStateInfo& Info, int32 Face, const FIntVector& Cell, float Low, float High) const
        {
            const int32 Axis = VoxelFaces::GetAxis(Face);
            const bool bPositive = VoxelFaces::IsPositive(Face);
            float Min[3], Max[3];
            for (int32 Index = 0; Index < 3; ++Index)
            {
                const float Base = float(Cell[Index]);
                Min[Index] = Index == 2 ? Base + Low : Base;
                Max[Index] = Index == 2 ? Base + High : Base + 1;
            }
            const float Plane = bPositive ? Max[Axis] : Min[Axis];
            const int32 AxisU = (Axis + 1) % 3, AxisV = (Axis + 2) % 3;
            Quad(Info, Face, Plane, Min[AxisU], Max[AxisU], Min[AxisV], Max[AxisV], Cell, OpenCorners,
                Tints.Get(Info, Face, Cell));
        }
    };

    // Fills a padded region with one state.
    void FillPadded(FVoxelPaddedSection& Out, const int32 Start[3], const int32 Count[3], uint16 State)
    {
        for (int32 Z = Start[2]; Z < Start[2] + Count[2]; ++Z)
            for (int32 Y = Start[1]; Y < Start[1] + Count[1]; ++Y)
                for (int32 X = Start[0]; X < Start[0] + Count[0]; ++X)
                    Out.States[FVoxelPaddedSection::GetIndex(X, Y, Z)] = State;
    }
}

void VoxelMesher::GetSectionsTouching(const FIntVector& Cell, TArray<FVoxelSectionKey, TInlineAllocator<8>>& Out)
{
    static_assert(VoxelConstants::ColumnSize == 16 && VoxelConstants::SectionSize == 16, "Shifts divide by 16");
    Out.Reset();
    if (Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) return;
    // Per axis: the cell's own index, and the neighbour on the side where the cell is that neighbour's border.
    auto Indices = [](int32 Index, int32 Local, int32 First, int32 Last)
    {
        TArray<int32, TInlineAllocator<2>> Result = {Index};
        if (Local == 0 && Index > First) Result.Add(Index - 1);
        if (Local == 15 && Index < Last) Result.Add(Index + 1);
        return Result;
    };
    const int32 LocalZ = Cell.Z - VoxelConstants::MinZ;
    // Columns are unbounded; arithmetic shifts floor negative coordinates.
    for (const int32 Y : Indices(Cell.Y >> 4, Cell.Y & 15, MIN_int32, MAX_int32))
        for (const int32 X : Indices(Cell.X >> 4, Cell.X & 15, MIN_int32, MAX_int32))
            for (const int32 Section : Indices(LocalZ >> 4, LocalZ & 15, 0, VoxelConstants::SectionCount - 1))
                Out.Add({FIntPoint(X, Y), Section});
}

FVector2f VoxelMesher::GetCanonicalUV(int32 Face, const FVector3f& P)
{
    switch (EVoxelFace(Face))
    {
    case EVoxelFace::Down:
        return {P.X, 1 - P.Y};
    case EVoxelFace::Up:
        return {P.X, P.Y};
    case EVoxelFace::North:
        return {1 - P.X, 1 - P.Z};
    case EVoxelFace::South:
        return {P.X, 1 - P.Z};
    case EVoxelFace::West:
        return {P.Y, 1 - P.Z};
    case EVoxelFace::East:
        return {1 - P.Y, 1 - P.Z};
    }
    return {0, 0};
}

void VoxelMesher::GatherPadded(const FVoxelNeighborhood& Neighborhood, int32 SectionIndex, FVoxelPaddedSection& Out)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelMesher_GatherPadded);
    SCOPE_CYCLE_COUNTER(STAT_VoxelGatherSection);
    // Per axis, the three source ranges: the neighbour's last cell, the section itself, the next neighbour's first.
    struct FRange
    {
        int32 Offset, PaddedStart, LocalStart, Count;
    };
    constexpr FRange Ranges[3] = {{-1, 0, SectionSize - 1, 1}, {0, 1, 0, SectionSize}, {1, SectionSize + 1, 0, 1}};
    uint16 Dense[FVoxelSection::Volume];
    for (const FRange& RZ : Ranges)
        for (const FRange& RY : Ranges)
            for (const FRange& RX : Ranges)
            {
                const int32 Start[3] = {RX.PaddedStart, RY.PaddedStart, RZ.PaddedStart};
                const int32 Count[3] = {RX.Count, RY.Count, RZ.Count};
                const FVoxelColumn* Column = Neighborhood.Get(RX.Offset, RY.Offset);
                const int32 Section = SectionIndex + RZ.Offset;
                if (!Column || Section < 0 || Section >= VoxelConstants::SectionCount)
                {
                    FillPadded(Out, Start, Count, 0);
                    continue;
                }
                const FVoxelSection& Source = Column->GetSection(Section);
                if (Source.IsUniform())
                {
                    FillPadded(Out, Start, Count, Source.GetUniformState());
                    continue;
                }
                if (RX.Count == SectionSize && RY.Count == SectionSize && RZ.Count == SectionSize)
                {
                    // The section itself: unpack once, then copy rows.
                    Source.Expand(Dense);
                    for (int32 Z = 0; Z < SectionSize; ++Z)
                        for (int32 Y = 0; Y < SectionSize; ++Y)
                            FMemory::Memcpy(&Out.States[FVoxelPaddedSection::GetIndex(1, Y + 1, Z + 1)],
                                &Dense[FVoxelSection::GetIndex(0, Y, Z)], SectionSize * sizeof(uint16));
                    continue;
                }
                for (int32 Z = 0; Z < RZ.Count; ++Z)
                    for (int32 Y = 0; Y < RY.Count; ++Y)
                        for (int32 X = 0; X < RX.Count; ++X)
                            Out.States[FVoxelPaddedSection::GetIndex(Start[0] + X, Start[1] + Y, Start[2] + Z)] =
                                Source.Get(RX.LocalStart + X, RY.LocalStart + Y, RZ.LocalStart + Z);
            }
}

void VoxelMesher::MeshSection(const FVoxelBlockTable& Table, const FVoxelPaddedSection& Padded, int32 SectionIndex,
    const FVoxelMeshOptions& Options, FVoxelMeshData& Out, const FVoxelTintTable* Tints,
    const FVoxelNeighborhood* Neighborhood)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelMesher_MeshSection);
    SCOPE_CYCLE_COUNTER(STAT_VoxelMeshSection);
    Out.Reset();
    const uint16* States = Padded.States;

    // Per padded cell: the faces it covers opaquely, and whether it darkens corners. Runs of one state reuse the
    // lookup.
    uint8 Opaque[FVoxelPaddedSection::Volume], Casts[FVoxelPaddedSection::Volume];
    // Whether any cell (border included) holds a model state, so sections without any skip the model pass.
    bool bAnyModel = false;
    {
        uint16 Last = States[0];
        const FVoxelStateInfo* Info = &Table.Get(Last);
        bAnyModel = Info->Shape == EVoxelShape::Model;
        for (int32 Index = 0; Index < FVoxelPaddedSection::Volume; ++Index)
        {
            if (States[Index] != Last)
            {
                Info = &Table.Get(Last = States[Index]);
                bAnyModel |= Info->Shape == EVoxelShape::Model;
            }
            Opaque[Index] = Info->OpaqueFaces;
            Casts[Index] = Info->bCastsOcclusion;
        }
    }

    // Quick exits: nothing inside, or a solid block whose every side is covered (most deep sections).
    bool bAnyBlock = false, bSealed = true;
    for (int32 Z = 1; Z <= SectionSize; ++Z)
        for (int32 Y = 1; Y <= SectionSize; ++Y)
            for (int32 X = 1; X <= SectionSize; ++X)
            {
                const int32 Index = FVoxelPaddedSection::GetIndex(X, Y, Z);
                bAnyBlock |= States[Index] != 0;
                bSealed &= Opaque[Index] == AllFaces;
            }
    if (!bAnyBlock) return;
    if (bSealed)
    {
        bool bBuried = true;
        for (int32 Face = 0; Face < VoxelFaces::Count && bBuried; ++Face)
        {
            const int32 Axis = VoxelFaces::GetAxis(Face), AxisU = (Axis + 1) % 3, AxisV = (Axis + 2) % 3;
            const int32 Back = VoxelFaces::GetOpposite(Face);
            for (int32 V = 1; V <= SectionSize && bBuried; ++V)
                for (int32 U = 1; U <= SectionSize; ++U)
                {
                    int32 Coordinates[3];
                    Coordinates[Axis] = VoxelFaces::IsPositive(Face) ? SectionSize + 1 : 0;
                    Coordinates[AxisU] = U;
                    Coordinates[AxisV] = V;
                    const int32 Index = FVoxelPaddedSection::GetIndex(Coordinates[0], Coordinates[1], Coordinates[2]);
                    if (!((Opaque[Index] >> Back) & 1))
                    {
                        bBuried = false;
                        break;
                    }
                }
        }
        if (bBuried) return;
    }

    FFaceTints FaceTints;
    if (Tints && Neighborhood && Options.TintBlend >= 0)
    {
        FaceTints.Table = Tints;
        FaceTints.Neighborhood = Neighborhood;
        FaceTints.SectionIndex = SectionIndex;
        FaceTints.Radius = FMath::Min(Options.TintBlend, FVoxelMeshOptions::MaxTintBlend);
        FaceTints.bShortcut = Options.bTintShortcut;
    }
    // Model offsets and alternatives read world cells: without a neighbourhood the column counts as the origin's.
    const FVoxelColumn* Center = Neighborhood ? Neighborhood->Get(0, 0) : nullptr;
    const FEmitter Emit{Out, VoxelConstants::MinZ + SectionIndex * SectionSize, FaceTints,
        Center ? Center->GetPosition() * VoxelConstants::ColumnSize : FIntPoint::ZeroValue};
    for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
    {
        const int32 Axis = VoxelFaces::GetAxis(Face), AxisU = (Axis + 1) % 3, AxisV = (Axis + 2) % 3;
        const bool bPositive = VoxelFaces::IsPositive(Face);
        const int32 Toward = bPositive ? AxisStep[Axis] : -AxisStep[Axis];
        const int32 Back = VoxelFaces::GetOpposite(Face);
        for (int32 Slice = 0; Slice < SectionSize; ++Slice)
        {
            // This slice's mergeable faces, [V][U].
            uint64 Keys[SectionSize * SectionSize] = {};
            bool bAnyKey = false;
            for (int32 V = 0; V < SectionSize; ++V)
                for (int32 U = 0; U < SectionSize; ++U)
                {
                    int32 Coordinates[3];
                    Coordinates[Axis] = Slice;
                    Coordinates[AxisU] = U;
                    Coordinates[AxisV] = V;
                    const int32 Index =
                        FVoxelPaddedSection::GetIndex(Coordinates[0] + 1, Coordinates[1] + 1, Coordinates[2] + 1);
                    const uint16 State = States[Index];
                    if (!State) continue;
                    const FVoxelStateInfo& Info = Table.Get(State);
                    const int32 Neighbor = Index + Toward;
                    const bool bCovered = (Opaque[Neighbor] >> Back) & 1;
                    const FIntVector Cell(Coordinates[0], Coordinates[1], Coordinates[2]);
                    switch (Info.Shape)
                    {
                    case EVoxelShape::Full:
                    {
                        if ((Info.Faces[Face] & VoxelFaces::CullBit) && bCovered) break;
                        // A see-through block hides the faces it shares with itself (glass beside glass).
                        if (Info.Layer == EVoxelLayer::Translucent && States[Neighbor])
                        {
                            const FVoxelStateInfo& Other = Table.Get(States[Neighbor]);
                            if (Other.Block == Info.Block && Other.Shape == EVoxelShape::Full) break;
                        }
                        const uint8 Corners = Options.bAmbientOcclusion
                            ? GetCorners(Casts, Neighbor, AxisStep[AxisU], AxisStep[AxisV])
                            : OpenCorners;
                        Keys[V * SectionSize + U] = MakeKey(State, Corners, FaceTints.Get(Info, Face, Cell));
                        bAnyKey = true;
                        break;
                    }
                    case EVoxelShape::SlabBottom:
                    case EVoxelShape::SlabTop:
                    {
                        if ((Info.Faces[Face] & VoxelFaces::CullBit) && bCovered) break;
                        const float Low = Info.Shape == EVoxelShape::SlabTop ? .5f : 0.f;
                        Emit.CellFace(Info, Face, Cell, Low, Low + .5f);
                        break;
                    }
                    case EVoxelShape::Layers:
                    {
                        // The top never culls (it sits below the cell's top); the rest cull like a slab's.
                        if ((Info.Faces[Face] & VoxelFaces::CullBit) && bCovered) break;
                        Emit.CellFace(Info, Face, Cell, 0, Info.Layers / 8.f);
                        break;
                    }
                    case EVoxelShape::Fluid:
                    case EVoxelShape::Model:
                    {
                        // A model state holding water draws that water around its quads, as the fluid state would.
                        const uint16 FluidState = Info.Shape == EVoxelShape::Model ? Info.HeldFluid : State;
                        if (!FluidState) break;
                        const FVoxelStateInfo& Fluid = Table.Get(FluidState);
                        if (bCovered || Table.Get(States[Neighbor]).HoldsFluid()) break;
                        // Under more fluid the cell is full; otherwise its surface is at the amount's height.
                        const bool bFluidAbove = Table.Get(States[Index + AxisStep[2]]).HoldsFluid();
                        if (Axis == 2)
                        {
                            // The top face only shows with no fluid above, so its height follows from the state.
                            Keys[V * SectionSize + U] =
                                MakeKey(FluidState, OpenCorners, FaceTints.Get(Fluid, Face, Cell));
                            bAnyKey = true;
                        }
                        else
                            Emit.CellFace(Fluid, Face, Cell, 0, bFluidAbove ? 1.f : Fluid.GetFluidHeight());
                        break;
                    }
                    default:
                        break;
                    }
                }
            if (!bAnyKey) continue;

            // Grow each face along U while the key repeats, then along V while the whole row repeats.
            for (int32 V = 0; V < SectionSize; ++V)
                for (int32 U = 0; U < SectionSize; ++U)
                {
                    const uint64 Key = Keys[V * SectionSize + U];
                    if (!Key) continue;
                    int32 Width = 1, Height = 1;
                    if (Options.bGreedy)
                    {
                        while (U + Width < SectionSize && Keys[V * SectionSize + U + Width] == Key)
                            ++Width;
                        for (bool bRowMatches = true; V + Height < SectionSize && bRowMatches; Height += bRowMatches)
                            for (int32 Offset = 0; Offset < Width && bRowMatches; ++Offset)
                                bRowMatches = Keys[(V + Height) * SectionSize + U + Offset] == Key;
                    }
                    for (int32 Row = 0; Row < Height; ++Row)
                        for (int32 Offset = 0; Offset < Width; ++Offset)
                            Keys[(V + Row) * SectionSize + U + Offset] = 0;

                    const uint32 Low = uint32(Key) - 1;
                    const uint16 State = uint16(Low >> 8);
                    const uint8 Corners = uint8(Low & 0xFF);
                    const FVoxelStateInfo& Info = Table.Get(State);
                    float Plane = float(bPositive ? Slice + 1 : Slice);
                    if (Info.IsFluid() && Face == int32(EVoxelFace::Up)) Plane = Slice + Info.GetFluidHeight();
                    int32 Coordinates[3];
                    Coordinates[Axis] = Slice;
                    Coordinates[AxisU] = U;
                    Coordinates[AxisV] = V;
                    Emit.Quad(Info, Face, Plane, float(U), float(U + Width), float(V), float(V + Height),
                        FIntVector(Coordinates[0], Coordinates[1], Coordinates[2]), Corners, uint32(Key >> 32));
                }
        }
    }

    if (!bAnyModel || !Options.bModels) return;
    for (int32 Z = 0; Z < SectionSize; ++Z)
        for (int32 Y = 0; Y < SectionSize; ++Y)
            for (int32 X = 0; X < SectionSize; ++X)
            {
                const int32 Index = FVoxelPaddedSection::GetIndex(X + 1, Y + 1, Z + 1);
                const FVoxelStateInfo& Info = Table.Get(States[Index]);
                if (Info.Shape == EVoxelShape::Model)
                    Emit.Model(Table, Info, Options, Opaque, Casts, Index, FIntVector(X, Y, Z));
            }
}
