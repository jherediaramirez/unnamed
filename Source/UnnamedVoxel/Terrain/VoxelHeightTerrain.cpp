#include "Terrain/VoxelHeightTerrain.h"
#include "Blocks/VoxelBlockTable.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Random/VoxelRandom.h"
#include "Storage/VoxelColumn.h"
#include "VoxelStats.h"

namespace
{
    constexpr uint64 ContinentSalt = VoxelRandom::Salt("height-terrain/continents");
    constexpr uint64 HillSalt = VoxelRandom::Salt("height-terrain/hills");
    constexpr uint64 DetailSalt = VoxelRandom::Salt("height-terrain/detail");
    constexpr uint64 SoilSalt = VoxelRandom::Salt("height-terrain/soil");
    constexpr uint64 LayerSalt = VoxelRandom::Salt("height-terrain/layers");

    // Heights in blocks. Bare rock from RockLine up, a snow cap from SnowLine up.
    constexpr int32 RockLine = 110, SnowLine = 128;

    FVoxelNoiseSettings MakeTerrainNoise(EVoxelFractal Fractal, double Frequency, int32 Octaves)
    {
        FVoxelNoiseSettings Result;
        Result.Fractal = Fractal;
        Result.Frequency = Frequency;
        Result.Octaves = Octaves;
        return Result;
    }

    // Probability that a cell is the lower material where two layers blend: 1 at Bottom, 0 at Top.
    FORCEINLINE bool IsLowerLayer(int64 Seed, int32 X, int32 Y, int32 Z, int32 Bottom, int32 Top)
    {
        if (Z <= Bottom) return true;
        if (Z >= Top) return false;
        const float Chance = float(Top - Z) / float(Top - Bottom);
        return VoxelRandom::ToUnitFloat(VoxelRandom::Hash64(uint64(Seed), LayerSalt, X, Y, Z)) < Chance;
    }
}

FVoxelTerrainBlocks FVoxelTerrainBlocks::Find(const FVoxelBlockTable& Table)
{
    FVoxelTerrainBlocks Blocks;
    Blocks.Stone = Table.FindState(TEXT("stone"));
    Blocks.Deepslate = Table.FindState(TEXT("deepslate"));
    Blocks.Dirt = Table.FindState(TEXT("dirt"));
    Blocks.Grass = Table.FindState(TEXT("grass_block"));
    Blocks.Sand = Table.FindState(TEXT("sand"));
    Blocks.Gravel = Table.FindState(TEXT("gravel"));
    Blocks.Water = Table.FindState(TEXT("water"));
    Blocks.Bedrock = Table.FindState(TEXT("bedrock"));
    Blocks.Snow = Table.FindState(TEXT("snow_block"));
    return Blocks;
}

FVoxelHeightTerrain::FVoxelHeightTerrain(int64 InSeed, const FVoxelBlockTable& InTable)
    : Seed(InSeed),
      Table(InTable),
      Blocks(FVoxelTerrainBlocks::Find(InTable)),
      // Scales chosen so a 9 x 9-column test area (144 blocks) usually holds sea, shore, hills and a peak.
      Continents(uint64(InSeed), ContinentSalt, MakeTerrainNoise(EVoxelFractal::Fbm, 1. / 220, 4)),
      Hills(uint64(InSeed), HillSalt, MakeTerrainNoise(EVoxelFractal::Damped, 1. / 90, 5)),
      Detail(uint64(InSeed), DetailSalt, MakeTerrainNoise(EVoxelFractal::Fbm, 1. / 24, 3)),
      Soil(uint64(InSeed), SoilSalt, MakeTerrainNoise(EVoxelFractal::Fbm, 1. / 12, 2))
{
}

int32 FVoxelHeightTerrain::GetSurfaceHeight(int32 X, int32 Y) const
{
    const double CX = X + .5, CY = Y + .5;
    const float Continent = Continents.Sample(CX, CY);
    // 0 at sea, 1 well inland: hills only rise on land.
    const float Land = FMath::SmoothStep(-.05f, .3f, Continent);
    const float Hill = FMath::Max(Hills.Sample(CX, CY) * .5f + .5f, 0.f);
    const float Height =
        VoxelConstants::SeaLevel + Continent * 30 + Land * Hill * Hill * 90 + Detail.Sample(CX, CY) * 3;
    return FMath::Clamp(FMath::FloorToInt32(Height), VoxelConstants::MinZ + 8, VoxelConstants::MaxZ - 16);
}

bool FVoxelHeightTerrain::IsDry(int32 X, int32 Y) const
{
    return GetSurfaceHeight(X, Y) >= VoxelConstants::SeaLevel;
}

void FVoxelHeightTerrain::Generate(FIntPoint Position, FVoxelColumn& Out, TArray<uint16>& Dense) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelHeightTerrain_Generate);
    SCOPE_CYCLE_COUNTER(STAT_VoxelGenerateColumn);
    Dense.SetNumZeroed(FVoxelColumn::DenseVolume);
    constexpr int32 Size = VoxelConstants::ColumnSize, SeaTop = VoxelConstants::SeaLevel - 1;
    for (int32 LocalY = 0; LocalY < Size; ++LocalY)
        for (int32 LocalX = 0; LocalX < Size; ++LocalX)
        {
            const int32 X = Position.X * Size + LocalX, Y = Position.Y * Size + LocalY;
            const int32 Surface = GetSurfaceHeight(X, Y);
            // Soil 3-5 blocks deep; the top block and the soil under it depend on where the surface sits.
            const int32 SoilDepth =
                3 + FMath::Clamp(FMath::FloorToInt32((Soil.Sample(X + .5, Y + .5) + 1) * 1.5f), 0, 2);
            uint16 Top = Blocks.Grass, Under = Blocks.Dirt;
            if (Surface < SeaTop - 6)
                Top = Under = Blocks.Gravel;
            else if (Surface <= SeaTop + 2)
                Top = Under = Blocks.Sand;
            else if (Surface >= RockLine)
            {
                Top = Surface >= SnowLine ? Blocks.Snow : Blocks.Stone;
                Under = Blocks.Stone;
            }

            for (int32 Z = VoxelConstants::MinZ; Z <= FMath::Max(Surface, SeaTop); ++Z)
            {
                uint16 State;
                if (Z > Surface)
                    State = Blocks.Water;
                else if (IsLowerLayer(Seed, X, Y, Z, VoxelConstants::MinZ, VoxelConstants::MinZ + 5))
                    State = Blocks.Bedrock;
                else if (Z == Surface)
                    State = Top;
                else if (Z > Surface - SoilDepth)
                    State = Under;
                else if (IsLowerLayer(Seed, X, Y, Z, 0, 8))
                    State = Blocks.Deepslate;
                else
                    State = Blocks.Stone;
                Dense[FVoxelColumn::GetDenseIndex(LocalX, LocalY, Z)] = State;
            }
        }
    Out = FVoxelColumn(Position);
    Out.Build(Dense, Table);
}
