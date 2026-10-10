#include "Surface/VoxelFreezePass.h"
#include "Biome/VoxelBiomeClimate.h"
#include "Biome/VoxelBiomeStorage.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Storage/VoxelColumn.h"
#include "VoxelConstants.h"

bool FVoxelFreezePass::Initialize(const FVoxelBlockTable& InTable, FString* OutError)
{
    *this = FVoxelFreezePass();
    const uint16 IceState = InTable.FindState(TEXT("ice")), SnowState = InTable.FindState(TEXT("snow")),
                 Water = InTable.FindState(TEXT("water"));
    if (!IceState || !SnowState || !Water)
    {
        if (OutError) *OutError = TEXT("the freeze pass needs ice, snow and water blocks");
        return false;
    }
    Ice = IceState;
    Snow = SnowState;
    WaterBlock = InTable.Get(Water).Block;
    // 26.3's block tags cannot_support_snow_layer and support_override_snow_layer, plus powder snow (no collision to
    // stand on in 26.3; solid in our table for now).
    for (const TCHAR* Name : {TEXT("ice"), TEXT("packed_ice"), TEXT("barrier"), TEXT("powder_snow")})
        if (const uint16 State = InTable.FindState(Name)) NeverHolds.Add(InTable.Get(State).Block);
    for (const TCHAR* Name : {TEXT("honey_block"), TEXT("soul_sand"), TEXT("mud")})
        if (const uint16 State = InTable.FindState(Name)) AlwaysHolds.Add(InTable.Get(State).Block);
    Table = &InTable;
    return true;
}

bool FVoxelFreezePass::CanHoldSnow(uint16 State) const
{
    const FVoxelStateInfo& Info = Table->Get(State);
    if (AlwaysHolds.Contains(Info.Block)) return true;
    if (NeverHolds.Contains(Info.Block)) return false;
    // A full top face: full cubes and top slabs (eight snow layers would count too; generation makes none).
    return Info.Shape == EVoxelShape::Full || Info.Shape == EVoxelShape::SlabTop;
}

void FVoxelFreezePass::Apply(FIntPoint Position, TArrayView<uint16> Dense, const FVoxelBiomeStorage& Biomes,
    const FVoxelBiomeTable& BiomeTable, const FVoxelBiomeClimate& Climate, int32 SeaLevel,
    TConstArrayView<int32> Heights) const
{
    check(IsInitialized());
    if (Biomes.IsEmpty()) return;
    constexpr int32 Size = VoxelConstants::ColumnSize;
    for (int32 LocalY = 0; LocalY < Size; ++LocalY)
        for (int32 LocalX = 0; LocalX < Size; ++LocalX)
        {
            // The top: above the highest block that blocks movement or holds fluid (26.3's MOTION_BLOCKING).
            int32 Top = VoxelConstants::MinZ;
            const int32 From = Heights.IsEmpty() ? VoxelConstants::MaxZ
                                                 : FMath::Min(Heights[LocalY * Size + LocalX], VoxelConstants::MaxZ);
            for (int32 Z = From; Z >= VoxelConstants::MinZ; --Z)
            {
                const FVoxelStateInfo& Info = Table->Get(Dense[FVoxelColumn::GetDenseIndex(LocalX, LocalY, Z)]);
                if (Info.bSolid || Info.IsFluid())
                {
                    Top = Z + 1;
                    break;
                }
            }
            if (Top == VoxelConstants::MinZ) continue;
            const int32 Biome = Biomes.GetBlock(LocalX, LocalY, FMath::Min(Top, VoxelConstants::MaxZ));
            if (!BiomeTable.Biomes.IsValidIndex(Biome)) continue;
            const FVoxelBiome& Data = BiomeTable.Biomes[Biome];
            const int32 X = Position.X * Size + LocalX, Y = Position.Y * Size + LocalY;

            // Ice: still water (a source) just below the top.
            const int32 Below = Top - 1;
            uint16& BelowState = Dense[FVoxelColumn::GetDenseIndex(LocalX, LocalY, Below)];
            const FVoxelStateInfo& BelowInfo = Table->Get(BelowState);
            if (BelowInfo.IsFluid() && BelowInfo.Block == WaterBlock && BelowInfo.FluidAmount == 8 &&
                FVoxelBiomeClimate::IsColdEnoughToSnow(Climate.GetTemperature(Data, X, Y, Below, SeaLevel)))
                BelowState = Ice;

            // Snow: one layer on the top, inside the world.
            if (Top > VoxelConstants::MaxZ) continue;
            uint16& TopState = Dense[FVoxelColumn::GetDenseIndex(LocalX, LocalY, Top)];
            if (TopState != 0 || !CanHoldSnow(BelowState) ||
                !FVoxelBiomeClimate::IsColdEnoughToSnow(Climate.GetTemperature(Data, X, Y, Top, SeaLevel)))
                continue;
            TopState = Snow;
            BelowState = Table->GetSnowyState(BelowState);
        }
}

void FVoxelFreezePass::ApplyToColumn(
    FVoxelColumn& Column, const FVoxelBiomeTable& BiomeTable, const FVoxelBiomeClimate& Climate, int32 SeaLevel) const
{
    check(IsInitialized());
    const FVoxelBiomeStorage& Biomes = Column.GetBiomes();
    if (Biomes.IsEmpty()) return;
    constexpr int32 Size = VoxelConstants::ColumnSize;
    const FIntPoint Position = Column.GetPosition();
    // Sections a change reaches are expanded once, written, then packed again.
    TArray<uint16> Dense[VoxelConstants::SectionCount];
    const auto Write = [&](int32 LocalX, int32 LocalY, int32 Z, uint16 State)
    {
        const int32 Local = Z - VoxelConstants::MinZ;
        TArray<uint16>& States = Dense[Local >> 4];
        if (States.IsEmpty())
        {
            States.SetNumUninitialized(FVoxelSection::Volume);
            Column.GetSection(Local >> 4).Expand(States);
        }
        States[FVoxelSection::GetIndex(LocalX, LocalY, Local & 15)] = State;
    };
    uint32 Changed[Size] = {};
    for (int32 LocalY = 0; LocalY < Size; ++LocalY)
        for (int32 LocalX = 0; LocalX < Size; ++LocalX)
        {
            // As Apply: the top lies above the highest block that blocks movement or holds fluid, searched down from
            // the highest block that is not air. Each block column reads and writes only its own cells.
            int32 Top = VoxelConstants::MinZ;
            const int32 From =
                FMath::Min(Column.GetHeight(EVoxelHeightmap::WorldSurface, LocalX, LocalY), VoxelConstants::MaxZ);
            for (int32 Z = From; Z >= VoxelConstants::MinZ; --Z)
            {
                const FVoxelStateInfo& Info = Table->Get(Column.GetState(LocalX, LocalY, Z));
                if (Info.bSolid || Info.IsFluid())
                {
                    Top = Z + 1;
                    break;
                }
            }
            if (Top == VoxelConstants::MinZ) continue;
            const int32 Biome = Biomes.GetBlock(LocalX, LocalY, FMath::Min(Top, VoxelConstants::MaxZ));
            if (!BiomeTable.Biomes.IsValidIndex(Biome)) continue;
            const FVoxelBiome& Data = BiomeTable.Biomes[Biome];
            const int32 X = Position.X * Size + LocalX, Y = Position.Y * Size + LocalY;

            const int32 Below = Top - 1;
            const uint16 Original = Column.GetState(LocalX, LocalY, Below);
            uint16 BelowState = Original;
            const FVoxelStateInfo& BelowInfo = Table->Get(BelowState);
            if (BelowInfo.IsFluid() && BelowInfo.Block == WaterBlock && BelowInfo.FluidAmount == 8 &&
                FVoxelBiomeClimate::IsColdEnoughToSnow(Climate.GetTemperature(Data, X, Y, Below, SeaLevel)))
                BelowState = Ice;
            if (Top <= VoxelConstants::MaxZ && Column.GetState(LocalX, LocalY, Top) == 0 && CanHoldSnow(BelowState) &&
                FVoxelBiomeClimate::IsColdEnoughToSnow(Climate.GetTemperature(Data, X, Y, Top, SeaLevel)))
            {
                Write(LocalX, LocalY, Top, Snow);
                BelowState = Table->GetSnowyState(BelowState);
                Changed[LocalY] |= 1u << LocalX;
            }
            if (BelowState != Original)
            {
                Write(LocalX, LocalY, Below, BelowState);
                Changed[LocalY] |= 1u << LocalX;
            }
        }
    for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
        if (!Dense[Section].IsEmpty()) Column.SetSectionStates(Section, Dense[Section]);
    for (int32 LocalY = 0; LocalY < Size; ++LocalY)
        for (int32 LocalX = 0; LocalX < Size; ++LocalX)
            if (Changed[LocalY] & (1u << LocalX)) Column.UpdateHeights(LocalX, LocalY, *Table);
}
