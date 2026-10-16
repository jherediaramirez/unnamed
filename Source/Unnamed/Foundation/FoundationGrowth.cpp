#include "Foundation/FoundationGrowth.h"

#include "Foundation/FoundationBlocks.h"
#include "HAL/IConsoleManager.h"
#include "Random/VoxelRandom.h"

namespace
{
    TAutoConsoleVariable<int32> CVarBlocksGrowth(TEXT("foundation.Blocks.Growth"), 1,
        TEXT("1: random ticks grow crops and pumpkin and melon stems, and wet or dry farmland, in the sections around "
             "each player (survival B6a). 0: nothing random-ticks, as before B6a."));

    constexpr uint64 GrowthPickSalt = VoxelRandom::Salt("foundation.growth.random_tick");

    // The crops (CropBlock and its kin) and the stems by id; their age property's values give the maximum age.
    bool IsGrowthCrop(FName Id)
    {
        return Id == TEXT("wheat") || Id == TEXT("carrots") || Id == TEXT("potatoes") || Id == TEXT("beetroots") ||
            Id == TEXT("torchflower_crop") || Id == TEXT("pitcher_crop");
    }

    int32 GetGrowthAge(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
    {
        return FCString::Atoi(*Registry.GetValue(State, TEXT("age")).ToString());
    }

    FFoundationBlockState WithGrowthAge(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState State, int32 Age)
    {
        return Registry.WithValue(State, TEXT("age"), FName(*FString::FromInt(Age)));
    }

    // The four horizontal neighbours in the order a stem's fruit roll picks them (R22: uniform over the four).
    const FIntVector GrowthSides[4] = {
        FIntVector(0, -1, 0), FIntVector(0, 1, 0), FIntVector(-1, 0, 0), FIntVector(1, 0, 0)};
} // namespace

bool FoundationGrowth::IsOn()
{
    return CVarBlocksGrowth.GetValueOnAnyThread() != 0;
}

int32 FoundationGrowth::FGrowthRoll::Next(int32 Bound)
{
    return VoxelRandom::ToIndex(VoxelRandom::SplitMix64(State), Bound);
}

FIntVector FoundationGrowth::GetRandomTickCell(
    uint64 Seed, const FIntVector& Section, int64 Tick, int32 Index, FGrowthRoll& OutRoll)
{
    // The tick and the pick are folded in by separate rounds, so no (tick, pick) pair repeats another's stream.
    const uint64 Where = VoxelRandom::Hash64(Seed, GrowthPickSalt, Section.X, Section.Y, Section.Z);
    OutRoll.State = VoxelRandom::Mix64(VoxelRandom::Mix64(Where ^ uint64(Tick)) ^ uint64(Index));
    const int32 Packed = OutRoll.Next(SectionCells * SectionCells * SectionCells);
    return Section * SectionCells +
        FIntVector(Packed % SectionCells, Packed / SectionCells % SectionCells, Packed / (SectionCells * SectionCells));
}

FoundationGrowth::FGrowthTable::FGrowthTable(const FFoundationBlockRegistry& Registry) : Source(&Registry)
{
    const FGameplayTag Maintains = FGameplayTag::RequestGameplayTag(TEXT("Block.MaintainsFarmland"), false);
    const FGameplayTag PumpkinSoil = FGameplayTag::RequestGameplayTag(TEXT("Block.SupportsPumpkinStemFruit"), false);
    const FGameplayTag MelonSoil = FGameplayTag::RequestGameplayTag(TEXT("Block.SupportsMelonStemFruit"), false);
    auto Find = [&Registry](const TCHAR* Id)
    {
        const FFoundationBlockType* Block = Registry.FindBlock(Id);
        return Block ? Block->GetDefaultState() : FFoundationBlockState();
    };
    Dirt = Find(TEXT("dirt"));
    Pumpkin = Find(TEXT("pumpkin"));
    Melon = Find(TEXT("melon"));
    if (const FFoundationBlockType* Block = Registry.FindBlock(TEXT("pumpkin_stem"))) PumpkinStem = Block->ProtocolId;
    if (const FFoundationBlockType* Block = Registry.FindBlock(TEXT("melon_stem"))) MelonStem = Block->ProtocolId;
    States.SetNum(Registry.GetStateLimit());
    for (int32 Id = 1; Id < States.Num(); ++Id)
    {
        const FFoundationBlockState State{uint16(Id)};
        if (!Registry.IsKnown(State)) continue;
        const FFoundationBlockType& Block = Registry.GetBlock(State);
        FGrowthState& Out = States[Id];
        Out.bMaintainsFarmland = Maintains.IsValid() && Block.Tags.HasTag(Maintains);
        Out.bPumpkinSoil = PumpkinSoil.IsValid() && Block.Tags.HasTag(PumpkinSoil);
        Out.bMelonSoil = MelonSoil.IsValid() && Block.Tags.HasTag(MelonSoil);
        Out.bWater = Registry.GetFluid(State).bWater;
        Out.Light = uint8(FMath::Clamp(Registry.GetLightEmission(State), 0, 15));
        const int32 AgeProperty = Block.FindProperty(TEXT("age"));
        if (Block.Id == TEXT("farmland"))
        {
            Out.Kind = EGrowthKind::Farmland;
            Out.bFarmland = true;
            Out.Moisture = uint8(FCString::Atoi(*Registry.GetValue(State, TEXT("moisture")).ToString()));
        }
        else if (AgeProperty != INDEX_NONE &&
            (IsGrowthCrop(Block.Id) || Block.Id == TEXT("pumpkin_stem") || Block.Id == TEXT("melon_stem")))
        {
            Out.bTwoHalves = Block.Id == TEXT("pitcher_crop");
            // Only the lower half of a two-half crop ticks; it grows both.
            if (Out.bTwoHalves && Registry.GetValue(State, TEXT("half")) != TEXT("lower")) continue;
            Out.Kind = IsGrowthCrop(Block.Id) ? EGrowthKind::Crop : EGrowthKind::Stem;
            Out.Age = uint8(GetGrowthAge(Registry, State));
            Out.MaxAge = uint8(Block.Properties[AgeProperty].Values.Num() - 1);
            Out.bSkipsThird = Block.Id == TEXT("beetroots");
        }
    }
}

float FoundationGrowth::GetGrowthPoints(
    const FFoundationBlockWorld& World, const FGrowthTable& Table, const FIntVector& Cell)
{
    auto StateAt = [&World](const FIntVector& At)
    { return World.IsKnown(At) ? World.Get(At).State : FFoundationBlockState(); };
    float Points = 1;
    for (int32 DY = -1; DY <= 1; ++DY)
        for (int32 DX = -1; DX <= 1; ++DX)
        {
            const FGrowthState& Soil = Table.Get(StateAt(Cell + FIntVector(DX, DY, -1)));
            if (!Soil.bFarmland) continue;
            const float Gain = Soil.Moisture > 0 ? 3.f : 1.f;
            Points += DX || DY ? Gain / 4 : Gain;
        }
    // Crowding: the same block beside it on both axes, or else on a diagonal.
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    const FFoundationBlockType& Block = Registry.GetBlock(StateAt(Cell));
    auto IsSame = [&](int32 DX, int32 DY) { return Block.Contains(StateAt(Cell + FIntVector(DX, DY, 0))); };
    const bool bAlongX = IsSame(-1, 0) || IsSame(1, 0), bAlongY = IsSame(0, -1) || IsSame(0, 1);
    if ((bAlongX && bAlongY) || IsSame(-1, -1) || IsSame(1, -1) || IsSame(-1, 1) || IsSame(1, 1)) Points /= 2;
    return Points;
}

int32 FoundationGrowth::GetChanceDenominator(float Points)
{
    return FMath::FloorToInt32(25.f / Points) + 1;
}

int32 FoundationGrowth::GetLight(const FFoundationBlockWorld& World, const FGrowthTable& Table, const FIntVector& Cell,
    TFunctionRef<bool(const FIntVector&)> SeesSky)
{
    // The sky is looked for above the plant's top cell (a pitcher crop's upper half is the same block).
    FIntVector Top = Cell;
    const FFoundationBlockType& Plant = World.GetRegistry().GetBlock(World.Get(Cell).State);
    while (World.IsKnown(Top + FIntVector(0, 0, 1)) && !Plant.IsAir() &&
        Plant.Contains(World.Get(Top + FIntVector(0, 0, 1)).State))
        ++Top.Z;
    if (SeesSky(Top)) return SkyLight;
    int32 Light = 0;
    for (int32 DZ = -EmitterReach; DZ <= EmitterReach; ++DZ)
        for (int32 DY = -EmitterReach; DY <= EmitterReach; ++DY)
            for (int32 DX = -EmitterReach; DX <= EmitterReach; ++DX)
            {
                const FIntVector At = Cell + FIntVector(DX, DY, DZ);
                if (World.IsKnown(At)) Light = FMath::Max<int32>(Light, Table.Get(World.Get(At).State).Light);
            }
    return Light >= GrowthLight ? Light : 0;
}

bool FoundationGrowth::IsHydrated(const FFoundationBlockWorld& World, const FGrowthTable& Table, const FIntVector& Cell)
{
    for (int32 DZ = 0; DZ <= 1; ++DZ)
        for (int32 DY = -HydrationReach; DY <= HydrationReach; ++DY)
            for (int32 DX = -HydrationReach; DX <= HydrationReach; ++DX)
            {
                const FIntVector At = Cell + FIntVector(DX, DY, DZ);
                if (World.IsKnown(At) && Table.Get(World.Get(At).State).bWater) return true;
            }
    return false;
}

void FoundationGrowth::RandomTick(const FFoundationBlockWorld& World, const FGrowthTable& Table, const FIntVector& Cell,
    FGrowthRoll& Roll, TFunctionRef<bool(const FIntVector&)> SeesSky,
    TArray<TPair<FIntVector, FFoundationBlockState>, TInlineAllocator<2>>& OutWrites)
{
    if (!World.IsKnown(Cell)) return;
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    const FFoundationBlockState State = World.Get(Cell).State;
    const FGrowthState& Growth = Table.Get(State);
    switch (Growth.Kind)
    {
    case EGrowthKind::None:
        return;
    case EGrowthKind::Farmland:
    {
        // VALUES "Watering farmland": wet sets 7; dry loses 1; dry at 0 turns to dirt unless the block above keeps it.
        const FIntVector Above = Cell + FIntVector(0, 0, 1);
        if (IsHydrated(World, Table, Cell))
        {
            if (Growth.Moisture < WetMoisture)
                OutWrites.Emplace(Cell, Registry.WithValue(State, TEXT("moisture"), FName(TEXT("7"))));
        }
        else if (Growth.Moisture > 0)
            OutWrites.Emplace(
                Cell, Registry.WithValue(State, TEXT("moisture"), FName(*FString::FromInt(Growth.Moisture - 1))));
        else if (!(World.IsKnown(Above) && Table.Get(World.Get(Above).State).bMaintainsFarmland) && !Table.Dirt.IsAir())
            OutWrites.Emplace(Cell, Table.Dirt);
        return;
    }
    case EGrowthKind::Crop:
    case EGrowthKind::Stem:
        break;
    }
    if (Growth.Kind == EGrowthKind::Crop && Growth.Age >= Growth.MaxAge) return;
    if (GetLight(World, Table, Cell, SeesSky) < GrowthLight) return;
    if (Growth.bSkipsThird && Roll.Next(3) == 0) return;
    if (Roll.Next(GetChanceDenominator(GetGrowthPoints(World, Table, Cell))) != 0) return;
    if (Growth.Age < Growth.MaxAge)
    {
        const int32 Age = Growth.Age + 1;
        if (Growth.bTwoHalves)
        {
            // The pitcher crop grows its upper half with it: the cell above must hold it or be air (provisional).
            const FIntVector Above = Cell + FIntVector(0, 0, 1);
            if (!World.IsKnown(Above)) return;
            const FFoundationBlockState Upper = World.Get(Above).State;
            if (!Upper.IsAir() && !Registry.GetBlock(State).Contains(Upper)) return;
            OutWrites.Emplace(Cell, WithGrowthAge(Registry, State, Age));
            OutWrites.Emplace(
                Above, Registry.WithValue(WithGrowthAge(Registry, State, Age), TEXT("half"), TEXT("upper")));
            return;
        }
        OutWrites.Emplace(Cell, WithGrowthAge(Registry, State, Age));
        return;
    }
    // A grown stem (R22): one of four sides, not retried; the fruit needs air there and its soil below. Without the
    // attached stems in the block table the stem stays at age 7 (QC-KNOWN-BUGS; B6b adds them).
    const bool bPumpkin = Registry.GetBlock(State).ProtocolId == Table.PumpkinStem;
    const FIntVector Fruit = Cell + GrowthSides[Roll.Next(4)];
    const FIntVector Soil = Fruit + FIntVector(0, 0, -1);
    if (!World.IsKnown(Fruit) || !World.IsKnown(Soil) || !World.Get(Fruit).State.IsAir()) return;
    const FGrowthState& Below = Table.Get(World.Get(Soil).State);
    if (!(bPumpkin ? Below.bPumpkinSoil : Below.bMelonSoil)) return;
    const FFoundationBlockState FruitState = bPumpkin ? Table.Pumpkin : Table.Melon;
    if (!FruitState.IsAir()) OutWrites.Emplace(Fruit, FruitState);
}
