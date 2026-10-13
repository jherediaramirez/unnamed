#include "Meshing/VoxelFarMesher.h"

#include "Blocks/VoxelBlockTable.h"
#include "Meshing/VoxelMeshData.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Storage/VoxelColumn.h"

// Inside VoxelFarMesher and named, so unqualified lookup in the functions below finds these before any other unity
// file's helpers.
namespace VoxelFarMesher::FarDetail
{
    constexpr int32 Size = VoxelConstants::ColumnSize;
    constexpr int32 Area = Size * Size;
    static_assert(VoxelConstants::SectionSize == 16 && Size == 16, "Shifts and masks divide by 16");

    // The state at local X and Y and world Z (MinZ to MaxZ), read straight from the section.
    FORCEINLINE uint16 GetState(const FVoxelColumn& Column, int32 X, int32 Y, int32 Z)
    {
        const int32 Local = Z - VoxelConstants::MinZ;
        return Column.GetSection(Local >> 4).Get(X, Y, Local & 15);
    }

    // A greedy merge key: a part's Z, state and tint. Never zero for a part (its Z is never NoZ), so zero is "none".
    FORCEINLINE uint64 MakeKey(int32 Z, uint16 State, uint32 Tint)
    {
        return uint64(uint16(Z) ^ 0x8000u) << 48 | uint64(State) << 32 | uint64(Tint);
    }

    FORCEINLINE int32 GetKeyZ(uint64 Key)
    {
        return int16(uint16(uint16(Key >> 48) ^ 0x8000u));
    }

    FORCEINLINE uint16 GetKeyState(uint64 Key)
    {
        return uint16(Key >> 32);
    }

    FORCEINLINE uint32 GetKeyTint(uint64 Key)
    {
        return uint32(Key);
    }

    // The state a part draws with: its look state (FVoxelBlockTable::GetLookState), equal to it in every field the
    // mesh reads, so leaves of every distance merge as the full mesher merges them.
    FORCEINLINE uint16 GetDrawState(const FVoxelBlockTable& Table, uint16 State)
    {
        const uint16 Look = Table.GetLookState(State);
        return Look ? Look : State;
    }

    // One quad, collected before any is appended so every layer's arrays grow once.
    struct FQuad
    {
        const FVoxelStateInfo* Info;
        float Plane, U0, U1, V0, V1;
        FIntVector Cell;
        uint32 Tint;
        uint8 Face;
        uint8 Layer;
    };

    // Merges a 16 x 16 grid of keys (U fastest) greedily: each run along U, then down V while the whole run repeats.
    // Clears the keys it emits; Emit(Key, U, V, Width, Height).
    template <typename EmitType> void MergeGreedy(uint64* Keys, EmitType&& Emit)
    {
        for (int32 V = 0; V < Size; ++V)
            for (int32 U = 0; U < Size; ++U)
            {
                const uint64 Key = Keys[V * Size + U];
                if (!Key) continue;
                int32 Width = 1, Height = 1;
                while (U + Width < Size && Keys[V * Size + U + Width] == Key)
                    ++Width;
                for (; V + Height < Size; ++Height)
                {
                    const uint64* Row = Keys + (V + Height) * Size + U;
                    int32 Offset = 0;
                    while (Offset < Width && Row[Offset] == Key)
                        ++Offset;
                    if (Offset < Width) break;
                }
                for (int32 Row = 0; Row < Height; ++Row)
                    FMemory::Memzero(Keys + (V + Row) * Size + U, Width * sizeof(uint64));
                Emit(Key, U, V, Width, Height);
            }
    }

    // A vertical quad growing along one line of cells: z from Z0 to Z1 (inclusive), cells [Start, End) along the line.
    struct FWallRun
    {
        int32 Z0, Z1;
        uint16 State;
        uint8 Layer;
        uint32 Tint;
        int32 Start, End;
    };

    struct FTreeHorizontal
    {
        int32 Plane, Cell;
        uint16 State;
        uint32 Tint;
        uint8 Face, Layer;
    };

    // The tree path is separate from the original canopy path so Trees=0 keeps its mesh bytes and merge order.
    void MeshTrees(const FVoxelBlockTable& Table, const FVoxelFarSummary& Center,
        const FVoxelFarSummary* const Neighbors[4], const VoxelFarMesher::FOptions& Options,
        const FVoxelFarBorder* const Borders[4], TArray<FQuad, TInlineAllocator<256>>& Quads)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(VoxelFarMesher_Trees);
        const uint8 LeafLayer = uint8(Options.CanopyLayer), WoodLayer = uint8(EVoxelLayer::Opaque);
        const bool bOpaqueLeaves = Options.CanopyLayer == EVoxelLayer::Opaque;
        TArray<FTreeHorizontal> HorizontalFaces;
        HorizontalFaces.Reserve(Center.TreeRuns.Num() * 2);
        for (int32 CellIndex = 0; CellIndex < Area; ++CellIndex)
        {
            const FVoxelFarCell& Cell = Center.Cells[CellIndex];
            for (int32 Index = 0; Index < Cell.TreeRunCount; ++Index)
            {
                const FVoxelFarTreeRun& Run = Center.TreeRuns[Cell.TreeRunStart + Index];
                const uint8 Layer = Run.bLeaves ? LeafLayer : WoodLayer;
                auto Covers = [&Run, bOpaqueLeaves](const FVoxelFarTreeRun& Other)
                { return Run.bLeaves || !Other.bLeaves || bOpaqueLeaves; };
                const FVoxelFarTreeRun* Above = Index ? &Center.TreeRuns[Cell.TreeRunStart + Index - 1] : nullptr;
                const FVoxelFarTreeRun* Below =
                    Index + 1 < Cell.TreeRunCount ? &Center.TreeRuns[Cell.TreeRunStart + Index + 1] : nullptr;
                if (!Above || Above->Bottom != Run.Top + 1 || !Covers(*Above))
                    HorizontalFaces.Add(
                        {Run.Top + 1, CellIndex, Run.TopState, Run.TopTint, uint8(EVoxelFace::Up), Layer});
                const bool bGroundUnder = Cell.HasGround() && Cell.GroundZ == Run.Bottom - 1;
                if (!bGroundUnder && (!Below || Below->Top != Run.Bottom - 1 || !Covers(*Below)))
                    HorizontalFaces.Add(
                        {Run.Bottom, CellIndex, Run.State, Run.BottomTint, uint8(EVoxelFace::Down), Layer});
            }
        }
        HorizontalFaces.Sort(
            [](const FTreeHorizontal& A, const FTreeHorizontal& B)
            {
                if (A.Face != B.Face) return A.Face < B.Face;
                if (A.Layer != B.Layer) return A.Layer < B.Layer;
                if (A.Plane != B.Plane) return A.Plane < B.Plane;
                return A.Cell < B.Cell;
            });
        uint64 Keys[Area];
        for (int32 Start = 0; Start < HorizontalFaces.Num();)
        {
            const FTreeHorizontal First = HorizontalFaces[Start];
            FMemory::Memzero(Keys);
            int32 End = Start;
            while (End < HorizontalFaces.Num())
            {
                const FTreeHorizontal& Face = HorizontalFaces[End];
                if (Face.Face != First.Face || Face.Layer != First.Layer || Face.Plane != First.Plane) break;
                Keys[Face.Cell] = MakeKey(Face.Plane, GetDrawState(Table, Face.State), Face.Tint);
                ++End;
            }
            MergeGreedy(Keys,
                [&Table, &Quads, First](uint64 Key, int32 U, int32 V, int32 Width, int32 Height)
                {
                    FQuad& Quad = Quads.AddDefaulted_GetRef();
                    Quad.Info = &Table.Get(GetKeyState(Key));
                    Quad.Plane = float(First.Plane);
                    Quad.U0 = float(U);
                    Quad.U1 = float(U + Width);
                    Quad.V0 = float(V);
                    Quad.V1 = float(V + Height);
                    Quad.Cell = FIntVector(U, V, First.Plane - (First.Face == uint8(EVoxelFace::Up) ? 1 : 0));
                    Quad.Tint = GetKeyTint(Key);
                    Quad.Face = First.Face;
                    Quad.Layer = First.Layer;
                });
            Start = End;
        }

        for (int32 Face = 0; Face < 4; ++Face)
        {
            const bool bAlongY = VoxelFaces::GetAxis(Face) == 0, bPositive = VoxelFaces::IsPositive(Face);
            const int32 Step = bPositive ? 1 : -1;
            for (int32 Line = 0; Line < Size; ++Line)
            {
                // Unlike the single-canopy path, any number of disjoint tiers can reach this line.
                TArray<FWallRun, TInlineAllocator<16>> Pending;
                auto Flush = [&Table, &Quads, Face, Line, bPositive, bAlongY](const FWallRun& Run)
                {
                    FQuad& Quad = Quads.AddDefaulted_GetRef();
                    Quad.Info = &Table.Get(Run.State);
                    Quad.Plane = float(Line + (bPositive ? 1 : 0));
                    Quad.U0 = float(bAlongY ? Run.Start : Run.Z0);
                    Quad.U1 = float(bAlongY ? Run.End : Run.Z1 + 1);
                    Quad.V0 = float(bAlongY ? Run.Z0 : Run.Start);
                    Quad.V1 = float(bAlongY ? Run.Z1 + 1 : Run.End);
                    Quad.Cell = bAlongY ? FIntVector(Line, Run.Start, Run.Z0) : FIntVector(Run.Start, Line, Run.Z0);
                    Quad.Tint = Run.Tint;
                    Quad.Face = uint8(Face);
                    Quad.Layer = Run.Layer;
                };
                for (int32 Along = 0; Along < Size; ++Along)
                {
                    const int32 X = bAlongY ? Line : Along, Y = bAlongY ? Along : Line;
                    const int32 NX = X + (bAlongY ? Step : 0), NY = Y + (bAlongY ? 0 : Step);
                    const bool bAcross = NX < 0 || NX >= Size || NY < 0 || NY >= Size;
                    const FVoxelFarSummary* NextSummary = bAcross ? (Neighbors ? Neighbors[Face] : nullptr) : &Center;
                    const FVoxelFarCell* Next =
                        NextSummary ? &NextSummary->Get(NX & (Size - 1), NY & (Size - 1)) : nullptr;
                    const FVoxelFarBorder* Border = bAcross && Borders ? Borders[Face] : nullptr;
                    const FVoxelFarCell& Cell = Center.Get(X, Y);
                    if (Cell.TreeRunCount)
                    {
                        // Bits: opaque coverage (1), leaf-shell coverage (2). A world-height mask avoids quadratic
                        // interval subtraction on pathological edited columns without imposing a tier limit.
                        uint8 Covered[VoxelConstants::Height] = {};
                        if (Next)
                        {
                            if (Next->HasGround())
                                for (int32 Z = VoxelConstants::MinZ; Z <= Next->GroundZ; ++Z)
                                    if (!Border || !Border->IsOpen(Along, Z)) Covered[Z - VoxelConstants::MinZ] = 1;
                            if (bAcross)
                            {
                                // A wanted-near neighbour may still display an old far mesh. Its actual wood and
                                // lower tiers therefore cannot hide a boundary face yet. The legacy upper canopy
                                // exists in both far modes; leaves can still join it without adding a visible seam.
                                if (Next->HasCanopy())
                                    for (int32 Z = Next->CanopyBottom; Z <= Next->CanopyTop; ++Z)
                                        Covered[Z - VoxelConstants::MinZ] |= 2;
                            }
                            else
                                for (const FVoxelFarTreeRun& Run : NextSummary->GetTreeRuns(NX, NY))
                                    for (int32 Z = Run.Bottom; Z <= Run.Top; ++Z)
                                        Covered[Z - VoxelConstants::MinZ] |= Run.bLeaves ? (bOpaqueLeaves ? 3 : 2) : 1;
                        }
                        auto Offer = [&Pending, Along](
                                         const FVoxelFarTreeRun& TreeRun, int32 Bottom, int32 Top, uint8 Layer)
                        {
                            for (FWallRun& Run : Pending)
                                if (Run.End == Along && Run.Z0 == Bottom && Run.Z1 == Top &&
                                    Run.State == TreeRun.State && Run.Layer == Layer && Run.Tint == TreeRun.Tint)
                                {
                                    Run.End = Along + 1;
                                    return;
                                }
                            Pending.Add({Bottom, Top, TreeRun.State, Layer, TreeRun.Tint, Along, Along + 1});
                        };
                        for (const FVoxelFarTreeRun& Run : Center.GetTreeRuns(X, Y))
                        {
                            const uint8 Mask = Run.bLeaves ? 3 : 1, Layer = Run.bLeaves ? LeafLayer : WoodLayer;
                            for (int32 Z = Run.Bottom; Z <= Run.Top; ++Z)
                            {
                                if (Covered[Z - VoxelConstants::MinZ] & Mask) continue;
                                const int32 Bottom = Z;
                                while (Z < Run.Top && !(Covered[Z + 1 - VoxelConstants::MinZ] & Mask))
                                    ++Z;
                                Offer(Run, Bottom, Z, Layer);
                            }
                        }
                    }
                    int32 Kept = 0;
                    for (const FWallRun& Run : Pending)
                        if (Run.End == Along + 1)
                            Pending[Kept++] = Run;
                        else
                            Flush(Run);
                    Pending.SetNum(Kept, EAllowShrinking::No);
                }
                for (const FWallRun& Run : Pending)
                    Flush(Run);
            }
        }
    }
}

FVoxelFarClasses::FVoxelFarClasses(const FVoxelBlockTable& Table)
{
    // Logs and stems by block: every state of a block whose id is a log's.
    TBitArray<> LogBlocks(false, Table.GetBlockLimit());
    for (const TPair<FName, uint16>& Pair : Table.GetDefaultStates())
        if (IsLogId(Pair.Key.ToString()))
        {
            const int32 Block = Table.Get(Pair.Value).Block;
            if (Block > 0 && Block < LogBlocks.Num()) LogBlocks[Block] = true;
        }

    Classes.SetNumUninitialized(Table.GetStateCount());
    Logs.Init(false, Table.GetStateCount());
    EVoxelFarClass* Data = Classes.GetData();
    for (int32 State = 0; State < Table.GetStateCount(); ++State)
    {
        const FVoxelStateInfo& Info = Table.Get(uint16(State));
        // The suffix also matches crop stems (baked models). Only full wood cubes belong in tree runs; keep the
        // broader legacy skip rule below unchanged.
        Logs[State] = Info.Shape == EVoxelShape::Full && Info.Block < LogBlocks.Num() && LogBlocks[Info.Block];
        EVoxelFarClass Class = EVoxelFarClass::Ground;
        if (Info.Shape == EVoxelShape::Air || (Info.Block < LogBlocks.Num() && LogBlocks[Info.Block]))
            Class = EVoxelFarClass::Skip;
        // A model holding water (seagrass, kelp, waterlogged models) is that water's surface when it is the top
        // fluid cell; other models are looked through.
        else if (Info.Shape == EVoxelShape::Model)
            Class = Info.HeldFluid ? EVoxelFarClass::Fluid : EVoxelFarClass::Skip;
        else if (Info.bLeaves)
            Class = EVoxelFarClass::Leaves;
        else if (Info.Shape == EVoxelShape::Fluid)
            Class = EVoxelFarClass::Fluid;
        else if (Info.Shape == EVoxelShape::Layers)
            Class = EVoxelFarClass::SnowLayer;
        Data[State] = Class;
    }
}

bool FVoxelFarClasses::IsLogId(FStringView Id)
{
    const FStringView Prefix = TEXTVIEW("minecraft:");
    if (Id.StartsWith(Prefix, ESearchCase::CaseSensitive)) Id.RightChopInline(Prefix.Len());
    return Id.EndsWith(TEXTVIEW("_log"), ESearchCase::CaseSensitive) ||
        Id.EndsWith(TEXTVIEW("_wood"), ESearchCase::CaseSensitive) ||
        Id.EndsWith(TEXTVIEW("_stem"), ESearchCase::CaseSensitive) ||
        Id.EndsWith(TEXTVIEW("_hyphae"), ESearchCase::CaseSensitive);
}

void VoxelFarMesher::Summarize(const FVoxelFarClasses& Classes, const FVoxelBlockTable& Table,
    const FVoxelColumn& Column, FVoxelFarSummary& Out, const FVoxelTintTable* Tints,
    const FVoxelNeighborhood* Neighborhood, const FVoxelMeshOptions& MeshOptions, bool bTrees)
{
    using namespace FarDetail;
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelFarMesher_Summarize);

    Out.Position = Column.GetPosition();
    Out.LowestGround = FVoxelFarCell::NoZ;
    Out.bTrees = bTrees;
    Out.TreeRuns.Reset();
    FVoxelFarCell* const Cells = Out.Cells;
    int32 Lowest = MAX_int32;
    for (int32 Y = 0; Y < Size; ++Y)
        for (int32 X = 0; X < Size; ++X)
        {
            FVoxelFarCell& Cell = Cells[Y * Size + X];
            Cell = FVoxelFarCell();
            Cell.TreeRunStart = Out.TreeRuns.Num();
            // The last snow layer seen: it tops the leaves or ground cell right under it.
            int32 SnowZ = MIN_int32;
            uint16 SnowState = 0;
            // Inside the topmost leaves run; or past it (or past a fluid), when later leaves are ignored.
            bool bInCanopy = false, bCanopyClosed = false;
            const int32 Top = FMath::Min(Column.GetHeight(EVoxelHeightmap::WorldSurface, X, Y), VoxelConstants::MaxZ);
            for (int32 Z = Top; Z >= VoxelConstants::MinZ; --Z)
            {
                const uint16 State = GetState(Column, X, Y, Z);
                const EVoxelFarClass Class = Classes.Get(State);
                if (bTrees && (Class == EVoxelFarClass::Leaves || Classes.IsLog(State)))
                {
                    const uint16 DrawState = GetDrawState(Table, State);
                    FVoxelFarTreeRun* Previous = Cell.TreeRunCount ? &Out.TreeRuns.Last() : nullptr;
                    if (Previous && Previous->Bottom == Z + 1 && Previous->State == DrawState)
                        Previous->Bottom = int16(Z);
                    else
                    {
                        FVoxelFarTreeRun& Run = Out.TreeRuns.AddDefaulted_GetRef();
                        Run.Bottom = Run.Top = int16(Z);
                        Run.State = DrawState;
                        Run.TopState = SnowZ == Z + 1 ? SnowState : DrawState;
                        Run.bLeaves = Class == EVoxelFarClass::Leaves;
                        ++Cell.TreeRunCount;
                    }
                }
                if (bInCanopy && Class != EVoxelFarClass::Leaves)
                {
                    bInCanopy = false;
                    bCanopyClosed = true;
                }
                if (Class == EVoxelFarClass::Ground)
                {
                    Cell.GroundZ = int16(Z);
                    Cell.SideState = State;
                    Cell.TopState = SnowZ == Z + 1 ? SnowState : State;
                    const uint16 Below = Z > VoxelConstants::MinZ ? GetState(Column, X, Y, Z - 1) : 0;
                    Cell.FillState = Classes.Get(Below) == EVoxelFarClass::Ground ? Below : State;
                    Lowest = FMath::Min(Lowest, Z);
                    break;
                }
                switch (Class)
                {
                case EVoxelFarClass::SnowLayer:
                    SnowZ = Z;
                    SnowState = State;
                    break;
                case EVoxelFarClass::Leaves:
                    if (bInCanopy)
                        Cell.CanopyBottom = int16(Z);
                    else if (!bCanopyClosed)
                    {
                        bInCanopy = true;
                        Cell.CanopyTop = Cell.CanopyBottom = int16(Z);
                        Cell.CanopyState = State;
                        Cell.CanopyTopState = SnowZ == Z + 1 ? SnowState : State;
                    }
                    break;
                case EVoxelFarClass::Fluid:
                    if (!Cell.HasFluid())
                    {
                        const FVoxelStateInfo& Info = Table.Get(State);
                        Cell.FluidZ = int16(Z);
                        Cell.FluidState = Info.Shape == EVoxelShape::Fluid ? State : Info.HeldFluid;
                    }
                    // Leaves under a fluid are not a canopy.
                    bCanopyClosed = true;
                    break;
                default:
                    break;
                }
            }
        }
    if (Lowest != MAX_int32) Out.LowestGround = int16(Lowest);

    if (!Tints || !Neighborhood)
    {
        for (int32 Index = 0; Index < Area; ++Index)
        {
            FVoxelFarCell& Cell = Cells[Index];
            if (Cell.HasGround())
            {
                Cell.TopTint = Table.Get(Cell.TopState).Tint;
                Cell.SideTint = Table.Get(Cell.SideState).Tint;
                Cell.FillTint = Table.Get(Cell.FillState).Tint;
            }
            if (Cell.HasCanopy())
            {
                Cell.CanopyTint = Table.Get(Cell.CanopyState).Tint;
                Cell.CanopyTopTint = Table.Get(Cell.CanopyTopState).Tint;
            }
            if (Cell.HasFluid()) Cell.FluidTint = Table.Get(Cell.FluidState).Tint;
        }
        for (FVoxelFarTreeRun& Run : Out.TreeRuns)
        {
            Run.Tint = Run.BottomTint = Table.Get(Run.State).Tint;
            Run.TopTint = Table.Get(Run.TopState).Tint;
        }
        return;
    }

    // Every face's tint as the full mesher gives it, in one batch.
    TArray<VoxelMesher::FTintQuery> Queries;
    TArray<uint32*> Targets;
    Queries.Reserve(Area * 3);
    Targets.Reserve(Area * 3);
    auto Ask = [&Table, &Queries, &Targets](uint16 State, EVoxelFace Face, int32 X, int32 Y, int32 Z, uint32& Target)
    {
        VoxelMesher::FTintQuery& Query = Queries.AddDefaulted_GetRef();
        Query.Info = &Table.Get(State);
        Query.Face = int32(Face);
        Query.Cell = FIntVector(X, Y, Z);
        Targets.Add(&Target);
    };
    for (int32 Y = 0; Y < Size; ++Y)
        for (int32 X = 0; X < Size; ++X)
        {
            FVoxelFarCell& Cell = Cells[Y * Size + X];
            if (Cell.HasGround())
            {
                // A snow layer's top is its own cell's, one above the ground.
                const int32 TopZ = Cell.TopState != Cell.SideState ? Cell.GroundZ + 1 : Cell.GroundZ;
                Ask(Cell.TopState, EVoxelFace::Up, X, Y, TopZ, Cell.TopTint);
                Ask(Cell.SideState, EVoxelFace::West, X, Y, Cell.GroundZ, Cell.SideTint);
                Ask(Cell.FillState, EVoxelFace::West, X, Y, Cell.GroundZ - 1, Cell.FillTint);
            }
            if (Cell.HasCanopy())
            {
                const int32 TopZ = Cell.CanopyTopState != Cell.CanopyState ? Cell.CanopyTop + 1 : Cell.CanopyTop;
                Ask(Cell.CanopyState, EVoxelFace::West, X, Y, Cell.CanopyTop, Cell.CanopyTint);
                Ask(Cell.CanopyTopState, EVoxelFace::Up, X, Y, TopZ, Cell.CanopyTopTint);
            }
            if (Cell.HasFluid()) Ask(Cell.FluidState, EVoxelFace::Up, X, Y, Cell.FluidZ, Cell.FluidTint);
            for (int32 Index = Cell.TreeRunStart; Index < Cell.TreeRunStart + Cell.TreeRunCount; ++Index)
            {
                FVoxelFarTreeRun& Run = Out.TreeRuns[Index];
                Ask(Run.State, EVoxelFace::West, X, Y, Run.Top, Run.Tint);
                Ask(Run.State, EVoxelFace::Down, X, Y, Run.Bottom, Run.BottomTint);
                Ask(Run.TopState, EVoxelFace::Up, X, Y, Run.TopState != Run.State ? Run.Top + 1 : Run.Top, Run.TopTint);
            }
        }
    TArray<uint32> Results;
    Results.SetNumUninitialized(Queries.Num());
    VoxelMesher::GetFaceTints(Tints, Neighborhood, MeshOptions, Queries, Results);
    uint32* const* TargetData = Targets.GetData();
    const uint32* ResultData = Results.GetData();
    for (int32 Index = 0; Index < Results.Num(); ++Index)
        *TargetData[Index] = ResultData[Index];
}

void VoxelFarMesher::SummarizeBorder(
    const FVoxelBlockTable& Table, const FVoxelColumn& Neighbor, int32 Face, FVoxelFarBorder& Out)
{
    using namespace FarDetail;
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelFarMesher_SummarizeBorder);

    const bool bAlongY = VoxelFaces::GetAxis(Face) == 0;
    // The neighbour's line of cells touching Center: its last one when it lies on Center's negative side.
    const int32 Edge = VoxelFaces::IsPositive(Face) ? 0 : Size - 1;
    const int32 Toward = VoxelFaces::GetOpposite(Face);
    FMemory::Memzero(Out.Open);
    for (int32 Along = 0; Along < Size; ++Along)
    {
        const int32 X = bAlongY ? Edge : Along, Y = bAlongY ? Along : Edge;
        uint64* const Open = Out.Open[Along];
        for (int32 Local = 0; Local < VoxelConstants::Height; ++Local)
            if (!((Table.Get(GetState(Neighbor, X, Y, Local + VoxelConstants::MinZ)).OpaqueFaces >> Toward) & 1))
                Open[Local >> 6] |= uint64(1) << (Local & 63);
    }
}

void VoxelFarMesher::Mesh(const FVoxelBlockTable& Table, const FVoxelFarSummary& Center,
    const FVoxelFarSummary* const Neighbors[4], const FOptions& Options, FVoxelMeshData& Out,
    const FVoxelFarBorder* const Borders[4])
{
    using namespace FarDetail;
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelFarMesher_Mesh);

    const FVoxelFarCell* const Cells = Center.Cells;
    constexpr uint8 OpaqueLayer = uint8(EVoxelLayer::Opaque);
    const uint8 CanopyLayer = uint8(Options.CanopyLayer);
    TArray<FQuad, TInlineAllocator<256>> Quads;
    uint64 Keys[Area];

    // One greedy pass over Keys on a horizontal face: the plane is the key's Z plus PlaneOffset, or for Layer below 0
    // (fluids, on their own layer) the key's Z plus the fluid's surface height.
    auto Horizontal = [&Table, &Quads, &Keys](EVoxelFace Face, int32 PlaneOffset, int32 Layer)
    {
        MergeGreedy(Keys,
            [&Table, &Quads, Face, PlaneOffset, Layer](uint64 Key, int32 U, int32 V, int32 Width, int32 Height)
            {
                const FVoxelStateInfo& Info = Table.Get(GetKeyState(Key));
                const int32 Z = GetKeyZ(Key);
                FQuad& Quad = Quads.AddDefaulted_GetRef();
                Quad.Info = &Info;
                Quad.Plane = Layer < 0 ? float(Z) + Info.GetFluidHeight() : float(Z + PlaneOffset);
                Quad.U0 = float(U);
                Quad.U1 = float(U + Width);
                Quad.V0 = float(V);
                Quad.V1 = float(V + Height);
                Quad.Cell = FIntVector(U, V, Z);
                Quad.Tint = GetKeyTint(Key);
                Quad.Face = uint8(Face);
                Quad.Layer = Layer < 0 ? uint8(Info.Layer) : uint8(Layer);
            });
    };

    // Ground tops.
    for (int32 Index = 0; Index < Area; ++Index)
    {
        const FVoxelFarCell& Cell = Cells[Index];
        Keys[Index] = Cell.HasGround() ? MakeKey(Cell.GroundZ, GetDrawState(Table, Cell.TopState), Cell.TopTint) : 0;
    }
    Horizontal(EVoxelFace::Up, 1, OpaqueLayer);

    // Ground walls and canopy sides, face by face, merged along each line of cells.
    auto GetNeighborCell = [&Center, Neighbors](int32 X, int32 Y, int32 Face) -> const FVoxelFarCell*
    {
        if (X >= 0 && X < Size && Y >= 0 && Y < Size) return &Center.Cells[Y * Size + X];
        const FVoxelFarSummary* Neighbor = Neighbors ? Neighbors[Face] : nullptr;
        return Neighbor ? &Neighbor->Cells[(Y & (Size - 1)) * Size + (X & (Size - 1))] : nullptr;
    };
    // A missing neighbour (or a neighbour cell without ground) is ground at the skirt's height.
    const int32 Skirt = int32(Center.LowestGround) - 1;
    for (int32 Face = 0; Face < 4; ++Face)
    {
        // West and East walls lie along Y (their U); North and South along X (their V).
        const bool bAlongY = VoxelFaces::GetAxis(Face) == 0;
        const bool bPositive = VoxelFaces::IsPositive(Face);
        const int32 Step = bPositive ? 1 : -1;
        const int32 StepX = bAlongY ? Step : 0, StepY = bAlongY ? 0 : Step;
        for (int32 Line = 0; Line < Size; ++Line)
        {
            const float Plane = float(Line + (bPositive ? 1 : 0));
            auto Flush = [&Table, &Quads, Face, Line, Plane, bAlongY](const FWallRun& Run)
            {
                FQuad& Quad = Quads.AddDefaulted_GetRef();
                Quad.Info = &Table.Get(Run.State);
                Quad.Plane = Plane;
                if (bAlongY)
                {
                    Quad.U0 = float(Run.Start);
                    Quad.U1 = float(Run.End);
                    Quad.V0 = float(Run.Z0);
                    Quad.V1 = float(Run.Z1 + 1);
                    Quad.Cell = FIntVector(Line, Run.Start, Run.Z0);
                }
                else
                {
                    Quad.U0 = float(Run.Z0);
                    Quad.U1 = float(Run.Z1 + 1);
                    Quad.V0 = float(Run.Start);
                    Quad.V1 = float(Run.End);
                    Quad.Cell = FIntVector(Run.Start, Line, Run.Z0);
                }
                Quad.Tint = Run.Tint;
                Quad.Face = uint8(Face);
                Quad.Layer = Run.Layer;
            };

            // At most 4 runs reach a cell (2 ground, 2 canopy) and 4 start there.
            FWallRun Pending[8];
            int32 PendingCount = 0;
            for (int32 Along = 0; Along < Size; ++Along)
            {
                const int32 X = bAlongY ? Line : Along, Y = bAlongY ? Along : Line;
                const FVoxelFarCell& Cell = Cells[Y * Size + X];
                if (Cell.HasGround() || Cell.HasCanopy())
                {
                    const FVoxelFarCell* Next = GetNeighborCell(X + StepX, Y + StepY, Face);
                    // Extends the run that reached this cell with the same quad, or starts one.
                    auto Offer = [&Pending, &PendingCount, Along](
                                     int32 Z0, int32 Z1, uint16 State, uint8 Layer, uint32 Tint)
                    {
                        for (int32 Index = 0; Index < PendingCount; ++Index)
                        {
                            FWallRun& Run = Pending[Index];
                            if (Run.End == Along && Run.Z0 == Z0 && Run.Z1 == Z1 && Run.State == State &&
                                Run.Layer == Layer && Run.Tint == Tint)
                            {
                                Run.End = Along + 1;
                                return;
                            }
                        }
                        check(PendingCount < int32(UE_ARRAY_COUNT(Pending)));
                        Pending[PendingCount++] = {Z0, Z1, State, Layer, Tint, Along, Along + 1};
                    };
                    if (Cell.HasGround())
                    {
                        const int32 GroundZ = Cell.GroundZ;
                        const int32 NextZ = Next && Next->HasGround() ? int32(Next->GroundZ) : Skirt;
                        if (GroundZ > NextZ)
                            Offer(GroundZ, GroundZ, GetDrawState(Table, Cell.SideState), OpaqueLayer, Cell.SideTint);
                        if (GroundZ - 1 > NextZ)
                            Offer(NextZ + 1, GroundZ - 1, GetDrawState(Table, Cell.FillState), OpaqueLayer,
                                Cell.FillTint);
                        // Across the edge, a neighbour drawn in full leaves the wall open below its ground where its
                        // cells are open: each open run up to what the walls above cover, in this cell alone.
                        const bool bAcross = X + StepX < 0 || X + StepX >= Size || Y + StepY < 0 || Y + StepY >= Size;
                        if (const FVoxelFarBorder* Border = bAcross && Borders ? Borders[Face] : nullptr)
                        {
                            const int32 Top = FMath::Min(GroundZ, NextZ);
                            for (int32 Z = VoxelConstants::MinZ; Z <= Top; ++Z)
                            {
                                if (!Border->IsOpen(Along, Z)) continue;
                                int32 End = Z;
                                while (End < Top && Border->IsOpen(Along, End + 1))
                                    ++End;
                                if (End == GroundZ)
                                    Flush({GroundZ, GroundZ, GetDrawState(Table, Cell.SideState), OpaqueLayer,
                                        Cell.SideTint, Along, Along + 1});
                                if (Z <= FMath::Min(End, GroundZ - 1))
                                    Flush({Z, FMath::Min(End, GroundZ - 1), GetDrawState(Table, Cell.FillState),
                                        OpaqueLayer, Cell.FillTint, Along, Along + 1});
                                Z = End;
                            }
                        }
                    }
                    if (!Options.bTrees && Cell.HasCanopy())
                    {
                        // [Bottom, Top] minus what the neighbour's ground and canopy cover; a missing one covers
                        // nothing.
                        int32 Low = Cell.CanopyBottom;
                        const int32 High = Cell.CanopyTop;
                        if (Next && Next->HasGround()) Low = FMath::Max(Low, Next->GroundZ + 1);
                        if (Low <= High)
                        {
                            const uint16 State = GetDrawState(Table, Cell.CanopyState);
                            if (Next && Next->HasCanopy() && Next->CanopyBottom <= High && Next->CanopyTop >= Low)
                            {
                                if (Next->CanopyBottom > Low)
                                    Offer(Low, Next->CanopyBottom - 1, State, CanopyLayer, Cell.CanopyTint);
                                if (Next->CanopyTop < High)
                                    Offer(Next->CanopyTop + 1, High, State, CanopyLayer, Cell.CanopyTint);
                            }
                            else
                                Offer(Low, High, State, CanopyLayer, Cell.CanopyTint);
                        }
                    }
                }
                // Runs this cell did not extend end here.
                int32 Kept = 0;
                for (int32 Index = 0; Index < PendingCount; ++Index)
                {
                    if (Pending[Index].End == Along + 1)
                        Pending[Kept++] = Pending[Index];
                    else
                        Flush(Pending[Index]);
                }
                PendingCount = Kept;
            }
            for (int32 Index = 0; Index < PendingCount; ++Index)
                Flush(Pending[Index]);
        }
    }

    if (Options.bTrees)
    {
        MeshTrees(Table, Center, Neighbors, Options, Borders, Quads);
    }
    else
    {
        // Legacy canopy tops, and bottoms unless ground lies right under them.
        for (int32 Index = 0; Index < Area; ++Index)
        {
            const FVoxelFarCell& Cell = Cells[Index];
            Keys[Index] = Cell.HasCanopy()
                ? MakeKey(Cell.CanopyTop, GetDrawState(Table, Cell.CanopyTopState), Cell.CanopyTopTint)
                : 0;
        }
        Horizontal(EVoxelFace::Up, 1, CanopyLayer);
        for (int32 Index = 0; Index < Area; ++Index)
        {
            const FVoxelFarCell& Cell = Cells[Index];
            const bool bGroundUnder = Cell.HasGround() && Cell.GroundZ == Cell.CanopyBottom - 1;
            Keys[Index] = Cell.HasCanopy() && !bGroundUnder
                ? MakeKey(Cell.CanopyBottom, GetDrawState(Table, Cell.CanopyState), Cell.CanopyTint)
                : 0;
        }
        Horizontal(EVoxelFace::Down, 0, CanopyLayer);
    }

    // Fluid tops, on each fluid's own layer.
    for (int32 Index = 0; Index < Area; ++Index)
    {
        const FVoxelFarCell& Cell = Cells[Index];
        Keys[Index] = Cell.HasFluid() ? MakeKey(Cell.FluidZ, GetDrawState(Table, Cell.FluidState), Cell.FluidTint) : 0;
    }
    Horizontal(EVoxelFace::Up, 0, -1);

    // Every layer's arrays grow once, then the quads go in, in the order they were made.
    int32 LayerQuads[int32(EVoxelLayer::Count)] = {};
    for (const FQuad& Quad : Quads)
        ++LayerQuads[Quad.Layer];
    for (int32 Layer = 0; Layer < int32(EVoxelLayer::Count); ++Layer)
    {
        if (!LayerQuads[Layer]) continue;
        FVoxelMeshLayer& Target = Out.Layers[Layer];
        const int32 Vertices = Target.Positions.Num() + 4 * LayerQuads[Layer];
        Target.Positions.Reserve(Vertices);
        Target.BlockUVs.Reserve(Vertices);
        Target.Codes.Reserve(Vertices);
        Target.TintFaces.Reserve(Vertices);
        Target.Colors.Reserve(Vertices);
        Target.Indices.Reserve(Target.Indices.Num() + 6 * LayerQuads[Layer]);
    }
    for (const FQuad& Quad : Quads)
        VoxelMesher::AppendQuad(Out.Layers[Quad.Layer], *Quad.Info, Quad.Face, Quad.Plane, Quad.U0, Quad.U1, Quad.V0,
            Quad.V1, Quad.Cell, 0, 0xFF, Quad.Tint);
}
