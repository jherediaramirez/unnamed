#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockRegistry.h"

struct FFoundationBlockWorld;

// Random ticks and what they grow (survival B6a, behind foundation.Blocks.Growth, default 1; the authority's
// Options.bGrowth, which the pawn copies in): crops, pumpkin and melon stems, and farmland's moisture. The authority
// picks the cells (StepRandomTicks, the sections around each player) and writes the changes this file returns, settled
// as edits. Everything here reads the world and a roll stream only, so tests run it without an authority.
namespace FoundationGrowth
{
    UNNAMED_API bool IsOn();

    // A random tick's section edge (cells); the picks per section are FFoundationRules::RandomTickSpeed.
    constexpr int32 SectionCells = 16;
    // Plants grow at light 9 or more at their own cell (VALUES "Crop growth", R22: a verified constant, not swept).
    // The light is a proxy (provisional, SM-Blocks Q3): a cell that sees the sky has 15 at any time of day (wiki "Crop
    // farming": crops grow at night under the open sky), else any block emitting 9 or more within EmitterReach cells
    // (a box) lights it.
    constexpr int32 GrowthLight = 9;
    constexpr int32 SkyLight = 15;
    constexpr int32 EmitterReach = 4;
    // Farmland finds water within 4 cells on X and Y, at its level or one above (VALUES "Watering farmland").
    constexpr int32 HydrationReach = 4;
    constexpr int32 WetMoisture = 7;

    // The roll stream of one pick: SplitMix64 over a hash of (seed, section, tick, pick), so a pick and its rolls are
    // a pure function of where and when, whatever ticked before.
    struct UNNAMED_API FGrowthRoll
    {
        uint64 State = 0;
        // [0, Bound).
        int32 Next(int32 Bound);
    };

    // The cell of pick Index in Section (section coordinates: cell / 16) at game tick Tick, and its roll stream.
    UNNAMED_API FIntVector GetRandomTickCell(
        uint64 Seed, const FIntVector& Section, int64 Tick, int32 Index, FGrowthRoll& OutRoll);

    enum class EGrowthKind : uint8
    {
        None,
        Crop,
        Stem,
        Farmland,
    };

    // Per state id: what a random tick does there, and the facts the rules read nearby.
    struct FGrowthState
    {
        EGrowthKind Kind = EGrowthKind::None;
        uint8 Age = 0, MaxAge = 0;
        // Farmland's moisture (0-7).
        uint8 Moisture = 0;
        bool bFarmland = false;
        // Beetroots skip a third of their random ticks (wiki "Beetroot Seeds": "each growth tick has a 1/3 chance
        // of not advancing the growth stage").
        bool bSkipsThird = false;
        // The pitcher crop: both halves age together (placement writes both at age 0; the upper half's model is empty
        // below age 3, block_states.json).
        bool bTwoHalves = false;
        // #minecraft:maintains_farmland: keeps a dry farmland below from turning to dirt.
        bool bMaintainsFarmland = false;
        // #minecraft:supports_pumpkin_stem_fruit, #minecraft:supports_melon_stem_fruit.
        bool bPumpkinSoil = false, bMelonSoil = false;
        bool bWater = false;
        uint8 Light = 0;
    };

    // Built once per registry: a lookup per state id, so the many states that do not tick reject at once.
    struct UNNAMED_API FGrowthTable
    {
        explicit FGrowthTable(const FFoundationBlockRegistry& Registry);
        const FGrowthState& Get(FFoundationBlockState State) const
        {
            return States.IsValidIndex(State.Id) ? States[State.Id] : None;
        }
        // Whether this table was built from Registry.
        bool IsFor(const FFoundationBlockRegistry& Registry) const { return Source == &Registry; }

        TArray<FGrowthState> States;
        FGrowthState None;
        const FFoundationBlockRegistry* Source = nullptr;
        // States a rule writes: dirt (farmland's revert), the fruits.
        FFoundationBlockState Dirt, Pumpkin, Melon;
        int32 PumpkinStem = INDEX_NONE, MelonStem = INDEX_NONE;
    };


    // A crop or stem's growth points (vanilla's growth speed f; VALUES "Crop growth"): 1, plus the farmland below (1
    // dry, 3 wet) and a quarter of each of the 8 farmland cells around it; halved when the same block grows on both
    // axes beside it, or else on a diagonal.
    UNNAMED_API float GetGrowthPoints(
        const FFoundationBlockWorld& World, const FGrowthTable& Table, const FIntVector& Cell);
    // The chance a crop grows per random tick is 1 / GetChanceDenominator(f) = 1 / (floor(25 / f) + 1).
    UNNAMED_API int32 GetChanceDenominator(float Points);
    // The light proxy at Cell (see GrowthLight); SeesSky answers whether nothing above a cell blocks the sky, asked for
    // the plant's top cell.
    UNNAMED_API int32 GetLight(const FFoundationBlockWorld& World, const FGrowthTable& Table, const FIntVector& Cell,
        TFunctionRef<bool(const FIntVector&)> SeesSky);
    // Whether water lies within the farmland's reach (rain would hydrate too; no weather yet).
    UNNAMED_API bool IsHydrated(const FFoundationBlockWorld& World, const FGrowthTable& Table, const FIntVector& Cell);

    // One random tick at Cell: the writes it makes, in order (empty when nothing changes).
    UNNAMED_API void RandomTick(const FFoundationBlockWorld& World, const FGrowthTable& Table, const FIntVector& Cell,
        FGrowthRoll& Roll, TFunctionRef<bool(const FIntVector&)> SeesSky,
        TArray<TPair<FIntVector, FFoundationBlockState>, TInlineAllocator<2>>& OutWrites);
} // namespace FoundationGrowth
