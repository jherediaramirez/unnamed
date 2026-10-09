#include "Meshing/VoxelMesher.h"
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

    // Greedy-merge keys: state and corner levels, plus one so 0 means "no face".
    FORCEINLINE uint32 MakeKey(uint16 State, uint8 Corners)
    {
        return ((uint32(State) << 8) | Corners) + 1;
    }

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

        // A quad on Face: Plane along the face's axis, [U0, U1] x [V0, V1] across it (section blocks; U and V are the
        // next two axes after it), Cell the section cell whose corner is the UV origin. Corners holds the occlusion
        // levels at (-,-), (+,-), (+,+), (-,+).
        void Quad(const FVoxelStateInfo& Info, int32 Face, float Plane, float U0, float U1, float V0, float V1,
            const FIntVector& Cell, uint8 Corners) const
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
            const FVector2f TintFace(float(Info.Tint), float(Face));
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
            Quad(Info, Face, Plane, Min[AxisU], Max[AxisU], Min[AxisV], Max[AxisV], Cell, OpenCorners);
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
    const FVoxelMeshOptions& Options, FVoxelMeshData& Out)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelMesher_MeshSection);
    SCOPE_CYCLE_COUNTER(STAT_VoxelMeshSection);
    Out.Reset();
    const uint16* States = Padded.States;

    // Per padded cell: the faces it covers opaquely, and whether it darkens corners. Runs of one state reuse the
    // lookup.
    uint8 Opaque[FVoxelPaddedSection::Volume], Casts[FVoxelPaddedSection::Volume];
    {
        uint16 Last = States[0];
        const FVoxelStateInfo* Info = &Table.Get(Last);
        for (int32 Index = 0; Index < FVoxelPaddedSection::Volume; ++Index)
        {
            if (States[Index] != Last) Info = &Table.Get(Last = States[Index]);
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

    const FEmitter Emit{Out, VoxelConstants::MinZ + SectionIndex * SectionSize};
    for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
    {
        const int32 Axis = VoxelFaces::GetAxis(Face), AxisU = (Axis + 1) % 3, AxisV = (Axis + 2) % 3;
        const bool bPositive = VoxelFaces::IsPositive(Face);
        const int32 Toward = bPositive ? AxisStep[Axis] : -AxisStep[Axis];
        const int32 Back = VoxelFaces::GetOpposite(Face);
        for (int32 Slice = 0; Slice < SectionSize; ++Slice)
        {
            // This slice's mergeable faces, [V][U].
            uint32 Keys[SectionSize * SectionSize] = {};
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
                        Keys[V * SectionSize + U] = MakeKey(State, Corners);
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
                    case EVoxelShape::Fluid:
                    {
                        if (bCovered || Table.Get(States[Neighbor]).IsFluid()) break;
                        // Under more fluid the cell is full; otherwise its surface is at the amount's height.
                        const bool bFluidAbove = Table.Get(States[Index + AxisStep[2]]).IsFluid();
                        if (Axis == 2)
                        {
                            // The top face only shows with no fluid above, so its height follows from the state.
                            Keys[V * SectionSize + U] = MakeKey(State, OpenCorners);
                            bAnyKey = true;
                        }
                        else
                            Emit.CellFace(Info, Face, Cell, 0, bFluidAbove ? 1.f : Info.GetFluidHeight());
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
                    const uint32 Key = Keys[V * SectionSize + U];
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

                    const uint16 State = uint16((Key - 1) >> 8);
                    const uint8 Corners = uint8((Key - 1) & 0xFF);
                    const FVoxelStateInfo& Info = Table.Get(State);
                    float Plane = float(bPositive ? Slice + 1 : Slice);
                    if (Info.IsFluid() && Face == int32(EVoxelFace::Up)) Plane = Slice + Info.GetFluidHeight();
                    int32 Coordinates[3];
                    Coordinates[Axis] = Slice;
                    Coordinates[AxisU] = U;
                    Coordinates[AxisV] = V;
                    Emit.Quad(Info, Face, Plane, float(U), float(U + Width), float(V), float(V + Height),
                        FIntVector(Coordinates[0], Coordinates[1], Coordinates[2]), Corners);
                }
        }
    }
}
