#include "Voxel/VoxelLightSources.h"
#include "Blocks/VoxelBlockTable.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Meshing/VoxelMesher.h"
#include "Storage/VoxelColumn.h"
#include "Storage/VoxelSection.h"
#include "VoxelConstants.h"

namespace
{
    // An EVoxelFace's outward normal.
    FVector GetVoxelLightFaceNormal(int32 Face)
    {
        FVector Normal = FVector::ZeroVector;
        Normal[VoxelFaces::GetAxis(Face)] = VoxelFaces::IsPositive(Face) ? 1 : -1;
        return Normal;
    }

    // Whether any cell of a section can hold a light: one lookup for a uniform section, one per palette entry for a
    // paletted one. 16-bit sections hold state ids and are scanned.
    bool HasSectionLight(const FVoxelSection& Section, const FVoxelLightTable& Table)
    {
        switch (Section.GetBitsPerIndex())
        {
        case 0:
            return Table.Get(Section.GetUniformState()).Level != 0;
        case 16:
            return true;
        default:
            for (const uint16 State : Section.GetPalette())
                if (Table.Get(State).Level) return true;
            return false;
        }
    }

    // A cell of the column or, past its edges, of the neighbourhood; missing columns read as air, as does everything
    // above and below the world (FVoxelColumn::GetState).
    uint16 GetLightNeighborState(
        const FVoxelColumn& Column, const FVoxelNeighborhood& Neighborhood, int32 X, int32 Y, int32 Z)
    {
        constexpr int32 Width = VoxelConstants::ColumnSize;
        const int32 DX = X < 0 ? -1 : X >= Width ? 1 : 0;
        const int32 DY = Y < 0 ? -1 : Y >= Width ? 1 : 0;
        const FVoxelColumn* Source = DX == 0 && DY == 0 ? &Column : Neighborhood.Get(DX, DY);
        return Source ? Source->GetState(X - DX * Width, Y - DY * Width, Z) : 0;
    }

    // The faces of a cube emitter whose neighbour is not an opaque full cube, a bit per EVoxelFace.
    uint8 GetLightOpenFaces(const FVoxelColumn& Column, const FVoxelNeighborhood& Neighborhood,
        const FVoxelLightTable& Table, int32 X, int32 Y, int32 Z)
    {
        static constexpr int32 Steps[VoxelFaces::Count][3] = {
            {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
        uint8 Open = 0;
        for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
        {
            const uint16 Neighbor =
                GetLightNeighborState(Column, Neighborhood, X + Steps[Face][0], Y + Steps[Face][1], Z + Steps[Face][2]);
            if (!Table.Get(Neighbor).bOpaqueCube) Open |= uint8(1 << Face);
        }
        return Open;
    }

    bool ParseLightFacing(FName Value, uint8& OutFace)
    {
        static const FName Names[4] = {TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south")};
        for (int32 Face = 0; Face < 4; ++Face)
            if (Value == Names[Face])
            {
                OutFace = uint8(Face);
                return true;
            }
        return false;
    }
}

TSharedRef<const FVoxelLightTable> VoxelLightSources::MakeTable(
    const FFoundationBlockRegistry& Registry, const FVoxelBlockTable& Blocks, int32 MinLevel)
{
    TSharedRef<FVoxelLightTable> Table = MakeShared<FVoxelLightTable>(Blocks.GetStateCount(), MinLevel);
    // The torch rule's wall blocks: the wall block a standing torch-rule block names (torch: wall_torch).
    TSet<FName> WallBlocks;
    for (int32 Index = 1; Index < Registry.GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry.GetBlockAt(Index);
        if (Block.bTorchRule && !Block.ItemWallBlock.IsNone()) WallBlocks.Add(Block.ItemWallBlock);
    }
    const FName FacingName(TEXT("facing"));
    for (int32 Id = 0; Id < Blocks.GetStateCount(); ++Id)
    {
        const FVoxelStateInfo& State = Blocks.Get(uint16(Id));
        const FFoundationBlockState RegistryState{uint16(Id)};
        FVoxelLightInfo Info;
        Info.bOpaqueCube = State.OpaqueFaces == 0x3F;
        // The level the mesher glows the state with (FVoxelStateInfo::LightEmission: the registry's per-state light).
        // Lava glows but gets no lights: its surfaces would need a light per cell.
        if (State.LightEmission > 0 && State.LightEmission >= MinLevel && Registry.IsKnown(RegistryState) &&
            !Registry.GetBlock(RegistryState).IsLava())
        {
            const FFoundationBlockType& Block = Registry.GetBlock(RegistryState);
            Info.Level = State.LightEmission;
            if (Block.bTorchRule && WallBlocks.Contains(Block.Id))
                // A wall block without a horizontal facing has no wall to lean from: its light takes the cell centre.
                Info.Shape = ParseLightFacing(Registry.GetValue(RegistryState, FacingName), Info.Facing)
                    ? EVoxelLightShape::WallTorch
                    : EVoxelLightShape::Model;
            else if (Block.bTorchRule)
                Info.Shape = EVoxelLightShape::FloorTorch;
            else if (Info.bOpaqueCube)
                Info.Shape = EVoxelLightShape::Cube;
            else
                Info.Shape = EVoxelLightShape::Model;
        }
        Table->SetState(uint16(Id), Info);
    }
    return Table;
}

bool VoxelLightSources::GetLocalPoint(const FVoxelLightInfo& Info, uint8 OpenFaces, FVector& OutPoint)
{
    switch (Info.Shape)
    {
    case EVoxelLightShape::FloorTorch:
        OutPoint = FVector(0.5, 0.5, FloorTorchHeight);
        return true;
    case EVoxelLightShape::WallTorch:
    {
        // The wall is behind the facing; the stick leans from it toward the facing, centred along the wall.
        const int32 Axis = VoxelFaces::GetAxis(Info.Facing);
        if (Axis > 1) return false;
        OutPoint = FVector(0.5, 0.5, WallTorchHeight);
        OutPoint[Axis] = VoxelFaces::IsPositive(Info.Facing) ? WallTorchOut : 1 - WallTorchOut;
        return true;
    }
    case EVoxelLightShape::Cube:
    {
        OpenFaces &= 0x3F;
        if (!OpenFaces) return false;
        // Per axis: exactly one of its two faces open nets that face's direction, both or neither net zero. The
        // point moves CubeOffset blocks from the centre along every netted direction, so it is outside the cube on
        // each of them (not the normalised sum, which put two or three open axes inside at 0.42 or 0.35 per axis).
        FVector Net = FVector::ZeroVector;
        for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
            if (OpenFaces & (1 << Face)) Net += GetVoxelLightFaceNormal(Face);
        // Every axis nets zero (open faces only in opposite pairs): above the top when it is open, else past the
        // first open face.
        if (Net.IsZero())
            Net = GetVoxelLightFaceNormal(OpenFaces & (1 << int32(EVoxelFace::Up))
                    ? int32(EVoxelFace::Up)
                    : FMath::CountTrailingZeros(OpenFaces));
        OutPoint = FVector(0.5) + Net * CubeOffset;
        return true;
    }
    case EVoxelLightShape::Model:
        OutPoint = FVector(0.5);
        return true;
    default:
        return false;
    }
}

uint64 VoxelLightSources::MakeKey(const FIntVector& Cell)
{
    // 27 bits each of X and Y (two's complement), 9 of Z above MinZ (the world is 384 cells high).
    static_assert(VoxelConstants::Height <= 512, "Z must fit 9 bits");
    return (uint64(uint32(Cell.X) & 0x7FFFFFFu) << 36) | (uint64(uint32(Cell.Y) & 0x7FFFFFFu) << 9) |
        uint64(uint32(Cell.Z - VoxelConstants::MinZ) & 0x1FFu);
}

void VoxelLightSources::GatherSources(const FVoxelColumn& Column, FIntPoint Position,
    const FVoxelNeighborhood& Neighborhood, const FVoxelLightTable& Table, TArray<FVoxelLightSource>& Out)
{
    constexpr int32 Width = VoxelConstants::ColumnSize;
    static_assert(FVoxelSection::Size == Width, "Sections span the column");
    const FIntVector Origin(Position.X * Width, Position.Y * Width, 0);
    // A section that may hold a light is unpacked at once, which is several times faster than reading cell by cell.
    uint16 Cells[FVoxelSection::Volume];
    for (int32 SectionIndex = 0; SectionIndex < VoxelConstants::SectionCount; ++SectionIndex)
    {
        const FVoxelSection& Section = Column.GetSection(SectionIndex);
        if (!HasSectionLight(Section, Table)) continue;
        Section.Expand(Cells);
        const int32 BaseZ = VoxelConstants::MinZ + SectionIndex * FVoxelSection::Size;
        for (int32 Index = 0; Index < FVoxelSection::Volume; ++Index)
        {
            const FVoxelLightInfo& Info = Table.Get(Cells[Index]);
            if (!Info.Level) continue;
            // Section indices run X fastest, then Y, then Z (FVoxelSection::GetIndex).
            const int32 X = Index & (Width - 1), Y = (Index / Width) & (Width - 1), Z = BaseZ + Index / (Width * Width);
            const uint8 OpenFaces =
                Info.Shape == EVoxelLightShape::Cube ? GetLightOpenFaces(Column, Neighborhood, Table, X, Y, Z) : 0;
            FVector Local;
            if (!GetLocalPoint(Info, OpenFaces, Local)) continue;
            FVoxelLightSource& Source = Out.AddDefaulted_GetRef();
            Source.Cell = Origin + FIntVector(X, Y, Z);
            Source.Point = FVector(Source.Cell) + Local;
            Source.Level = Info.Level;
            Source.Key = MakeKey(Source.Cell);
        }
    }
}

void VoxelLightSources::PickLights(TConstArrayView<FVoxelLightSource> LightSources, const FVector& Camera, int32 Count,
    double Radius, double MergeDistance, TArray<FVoxelLightPick>& Out)
{
    Out.Reset();
    const int32 Limit = FMath::Clamp(Count, 0, MaxLights);
    if (Limit == 0 || Radius < 0) return;

    // The sources in reach (a source without light is none), ordered by distance, then cell; the level and the point
    // break the last ties, so equal-ordered sources are interchangeable and any input order gives the same picks.
    struct FLightCandidate
    {
        double DistanceSquared;
        int32 Index;
    };
    TArray<FLightCandidate> Candidates;
    Candidates.Reserve(LightSources.Num());
    const double RadiusSquared = Radius * Radius;
    for (int32 Index = 0; Index < LightSources.Num(); ++Index)
    {
        const double DistanceSquared = FVector::DistSquared(LightSources[Index].Point, Camera);
        if (LightSources[Index].Level && DistanceSquared <= RadiusSquared) Candidates.Add({DistanceSquared, Index});
    }
    Candidates.Sort(
        [LightSources](const FLightCandidate& A, const FLightCandidate& B)
        {
            if (A.DistanceSquared != B.DistanceSquared) return A.DistanceSquared < B.DistanceSquared;
            const FVoxelLightSource& SA = LightSources[A.Index];
            const FVoxelLightSource& SB = LightSources[B.Index];
            if (SA.Cell.X != SB.Cell.X) return SA.Cell.X < SB.Cell.X;
            if (SA.Cell.Y != SB.Cell.Y) return SA.Cell.Y < SB.Cell.Y;
            if (SA.Cell.Z != SB.Cell.Z) return SA.Cell.Z < SB.Cell.Z;
            if (SA.Level != SB.Level) return SA.Level < SB.Level;
            if (SA.Point.X != SB.Point.X) return SA.Point.X < SB.Point.X;
            if (SA.Point.Y != SB.Point.Y) return SA.Point.Y < SB.Point.Y;
            return SA.Point.Z < SB.Point.Z;
        });

    // Greedy: each source joins the nearest pick whose seed point is strictly closer than the merge distance (the
    // earliest of equals), else seeds a pick while there is room.
    const double MergeSquared = MergeDistance > 0 ? MergeDistance * MergeDistance : 0;
    for (const FLightCandidate& Candidate : Candidates)
    {
        const FVoxelLightSource& Source = LightSources[Candidate.Index];
        const float Weight = GetWeight(Source.Level);
        int32 Nearest = INDEX_NONE;
        double NearestSquared = MergeSquared;
        for (int32 Pick = 0; Pick < Out.Num(); ++Pick)
        {
            const double DistanceSquared = FVector::DistSquared(Out[Pick].SeedPoint, Source.Point);
            if (DistanceSquared < NearestSquared)
            {
                Nearest = Pick;
                NearestSquared = DistanceSquared;
            }
        }
        if (Nearest != INDEX_NONE)
        {
            FVoxelLightPick& Pick = Out[Nearest];
            const double Total = double(Pick.Weight) + Weight;
            Pick.Centroid = (Pick.Centroid * double(Pick.Weight) + Source.Point * double(Weight)) / Total;
            Pick.Weight = float(Total);
            Pick.Level = FMath::Max(Pick.Level, Source.Level);
            ++Pick.Members;
        }
        else if (Out.Num() < Limit)
        {
            FVoxelLightPick& Pick = Out.AddDefaulted_GetRef();
            Pick.Centroid = Source.Point;
            Pick.SeedPoint = Source.Point;
            Pick.SeedCell = Source.Cell;
            Pick.Weight = Weight;
            Pick.Level = Source.Level;
            Pick.Members = 1;
            Pick.Key = Source.Key;
        }
    }
}
