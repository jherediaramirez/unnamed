#include "Foundation/FoundationMobFarm.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationItemEntities.h"
#include "Foundation/FoundationLoot.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarMobFarm(TEXT("foundation.Mobs.Farm"), 1,
        TEXT("1: farm extras (survival T6 20e1): sheep spawn in their colours, shears shear them, dyes recolour them, "
             "an "
             "empty bucket milks a cow, and cows, pigs and chickens spawn as their biome's cold or warm variant. 0: "
             "white unshearable sheep, temperate animals, and a use on a mob passes, as before."));

    // [jar loot_table/shearing/sheep.json] the colours in order.
    const TCHAR* const MobFarmColors[] = {TEXT("white"), TEXT("orange"), TEXT("magenta"), TEXT("light_blue"),
        TEXT("yellow"), TEXT("lime"), TEXT("pink"), TEXT("gray"), TEXT("light_gray"), TEXT("cyan"), TEXT("purple"),
        TEXT("blue"), TEXT("brown"), TEXT("green"), TEXT("red"), TEXT("black")};
    static_assert(UE_ARRAY_COUNT(MobFarmColors) == FoundationMobFarm::ColorCount);

    // minecraft.wiki "Dye" (provisional): each colour's texture colour, sRGB.
    constexpr uint32 MobFarmDyeColors[] = {0xF9FFFE, 0xF9801D, 0xC74EBD, 0x3AB3DA, 0xFED83D, 0x80C71F, 0xF38BAA,
        0x474F52, 0x9D9D97, 0x169C9C, 0x8932B8, 0x3C44AA, 0x835432, 0x5E7C16, 0xB02E26, 0x1D1D21};
    static_assert(UE_ARRAY_COUNT(MobFarmDyeColors) == FoundationMobFarm::ColorCount);

    // [jar tags/worldgen/biome/spawns_{cold,warm}_variant_farm_animals.json] with their #is_end, #is_jungle,
    // #is_savanna, #is_nether and #is_badlands members expanded.
    const TCHAR* const MobFarmColdBiomes[] = {TEXT("snowy_plains"), TEXT("ice_spikes"), TEXT("frozen_peaks"),
        TEXT("jagged_peaks"), TEXT("snowy_slopes"), TEXT("frozen_ocean"), TEXT("deep_frozen_ocean"), TEXT("grove"),
        TEXT("deep_dark"), TEXT("frozen_river"), TEXT("snowy_taiga"), TEXT("snowy_beach"), TEXT("the_end"),
        TEXT("end_highlands"), TEXT("end_midlands"), TEXT("small_end_islands"), TEXT("end_barrens"), TEXT("cold_ocean"),
        TEXT("deep_cold_ocean"), TEXT("old_growth_pine_taiga"), TEXT("old_growth_spruce_taiga"), TEXT("taiga"),
        TEXT("windswept_forest"), TEXT("windswept_gravelly_hills"), TEXT("windswept_hills"), TEXT("stony_peaks"),
        TEXT("dappled_forest")};
    const TCHAR* const MobFarmWarmBiomes[] = {TEXT("desert"), TEXT("warm_ocean"), TEXT("bamboo_jungle"), TEXT("jungle"),
        TEXT("sparse_jungle"), TEXT("savanna"), TEXT("savanna_plateau"), TEXT("windswept_savanna"),
        TEXT("nether_wastes"), TEXT("soul_sand_valley"), TEXT("crimson_forest"), TEXT("warped_forest"),
        TEXT("basalt_deltas"), TEXT("badlands"), TEXT("eroded_badlands"), TEXT("wooded_badlands"),
        TEXT("mangrove_swamp"), TEXT("deep_lukewarm_ocean"), TEXT("lukewarm_ocean")};

    bool MobFarmHasBiome(TConstArrayView<const TCHAR* const> Biomes, const FString& Name)
    {
        for (const TCHAR* Biome : Biomes)
            if (Name == Biome) return true;
        return false;
    }
}

bool FoundationMobFarm::IsOn()
{
    return CVarMobFarm.GetValueOnAnyThread() != 0;
}

FName FoundationMobFarm::GetColorName(int32 Color)
{
    return FName(MobFarmColors[FMath::Clamp(Color, 0, ColorCount - 1)]);
}

int32 FoundationMobFarm::FindDyeColor(FName Item)
{
    FString Name = Item.ToString();
    if (!Name.RemoveFromEnd(TEXT("_dye"))) return INDEX_NONE;
    for (int32 Color = 0; Color < ColorCount; ++Color)
        if (Name == MobFarmColors[Color]) return Color;
    return INDEX_NONE;
}

int32 FoundationMobFarm::RollSheepColor(FRandomStream& Random)
{
    const int32 Roll = Random.RandHelper(100);
    if (Roll < 5) return 15; // black
    if (Roll < 10) return 7; // gray
    if (Roll < 15) return 8; // light gray
    if (Roll < 18) return 12; // brown
    return Random.RandHelper(500) == 0 ? 6 : 0; // pink, else white
}

FLinearColor FoundationMobFarm::GetWoolTint(int32 Color)
{
    Color = FMath::Clamp(Color, 0, ColorCount - 1);
    const uint32 Rgb = Color == 0 ? WhiteWool : MobFarmDyeColors[Color];
    const float Shade = Color == 0 ? 1.f : WoolShade;
    // Shaded in sRGB bytes, then made linear for the material.
    return FLinearColor(FColor(uint8(FMath::FloorToInt32(((Rgb >> 16) & 255) * Shade)),
        uint8(FMath::FloorToInt32(((Rgb >> 8) & 255) * Shade)), uint8(FMath::FloorToInt32((Rgb & 255) * Shade))));
}

EFoundationMobVariant FoundationMobFarm::GetVariant(FName Biome)
{
    FString Name = Biome.ToString();
    Name.RemoveFromStart(TEXT("minecraft:"));
    if (MobFarmHasBiome(MobFarmColdBiomes, Name)) return EFoundationMobVariant::Cold;
    if (MobFarmHasBiome(MobFarmWarmBiomes, Name)) return EFoundationMobVariant::Warm;
    return EFoundationMobVariant::Temperate;
}

bool FoundationMobFarm::HasVariants(EFoundationMobType Type)
{
    return Type == EFoundationMobType::Cow || Type == EFoundationMobType::Pig || Type == EFoundationMobType::Chicken;
}

FName FoundationMobFarm::GetModelId(EFoundationMobType Type, EFoundationMobVariant Variant)
{
    const FName Id = FoundationMobs::GetKind(Type).Id;
    if (Variant == EFoundationMobVariant::Temperate || !HasVariants(Type)) return Id;
    return FName(FString::Printf(
        TEXT("%s_%s"), Variant == EFoundationMobVariant::Cold ? TEXT("cold") : TEXT("warm"), *Id.ToString()));
}

int32 FoundationMobFarm::MakeSeed(int32 MobId, int32 LootSeed)
{
    // The salt is "farm" in ASCII.
    return int32(HashCombine(HashCombine(GetTypeHash(MobId), GetTypeHash(LootSeed)), 0x6661726Du));
}

// The authority's side of the farm extras (declared in FoundationAuthority.h; kept here so the hot file only calls it).
bool FFoundationAuthority::UseOnMob(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationMob& Mob,
    EFoundationPlayerMode Mode, FFoundationResult& Result)
{
    using namespace FoundationMobFarm;
    static const FName ShearsId(TEXT("shears")), BucketId(TEXT("bucket")), MilkId(TEXT("milk_bucket"));
    const FFoundationItemInstance Stack = Player.Inventory.GetHand(Command.Hand);
    if (Stack.IsEmpty() || !Mob.IsAlive()) return false;
    const FFoundationItemDefinition& Item = Rules.GetItem(Stack.Item);
    const bool bCreative = Mode == EFoundationPlayerMode::Creative;
    const FIntVector Cell = FoundationGrid::CellAt(World.OriginCm + Mob.LocalCm, World.OriginCm);
    if (Mob.Type == EFoundationMobType::Sheep && Item.Id == ShearsId)
    {
        // [jar loot_table/shearing/sheep.json] a sheep with its wool drops 1-3 of its colour; the shears wear.
        if (Mob.bSheared) return false;
        Mob.bSheared = true;
        ++MobShears;
        AddMobSound(Mob, TEXT("shear"), 1);
        if (const FFoundationLootTable* Table = Rules.EntityLootTables.Find(FName(ShearTable)))
        {
            FFoundationLootContext Context{Rules.Hand};
            Context.SheepColor = GetColorName(Mob.Color);
            FRandomStream Random(FoundationLoot::MakeSeed(Rules.LootSeed, Cell, Mob.Id ^ (MobShears << 20)));
            TArray<FFoundationLootDrop> Drops;
            FoundationLoot::Roll(*Table, Rules.LootPredicates, Context, Random, Drops);
            for (const FFoundationLootDrop& Drop : Drops)
            {
                if (!Rules.Items.IsValidIndex(Drop.Item)) continue;
                const FVector Velocity(Random.FRandRange(-ShearSidewaysCm, ShearSidewaysCm),
                    Random.FRandRange(-ShearSidewaysCm, ShearSidewaysCm), Random.FRandRange(0, ShearPopCm));
                SpawnItem(Drop.Item, Drop.Count, Mob.LocalCm + FVector(0, 0, ShearUpCm), Velocity,
                    FoundationItemEntities::BlockDropPickupDelay);
            }
        }
        Result.Outcome = EFoundationOutcome::Committed;
        Result.EntityId = Mob.Id;
        if (!bCreative && Options.bToolWear && Item.MaxDurability > 0)
        {
            Result.bToolWorn = true;
            Result.bToolBroke = Player.Inventory.Wear(Command.Hand, ShearWear, Item.MaxDurability);
            if (Result.bToolBroke)
            {
                FFoundationEvent Event;
                Event.Kind = EFoundationEventKind::ToolBroke;
                Event.Player = Player.Id;
                Event.Item = Stack.Item;
                Event.Step = Player.Facts.Step;
                AddEvent(Event);
            }
        }
        return true;
    }
    if (Mob.Type == EFoundationMobType::Cow && Item.Id == BucketId)
    {
        // minecraft.wiki "Cow" (provisional): an empty bucket milks an adult cow, with the bucket's swap rules.
        const int32 Milk = Rules.FindItem(MilkId);
        if (Milk == INDEX_NONE) return false;
        if (!bCreative && Stack.Count > 1 && !Options.bItemEntities &&
            Player.Inventory.GetSpaceFor(Milk, Rules.GetItem(Milk).MaxStack) < 1)
        {
            Reject(Result, EFoundationReason::InventoryFull);
            return true;
        }
        SwapBucket(Player, Command.Hand, Stack, Milk, Cell, bCreative);
        ++MobMilks;
        AddMobSound(Mob, TEXT("milk"), 1);
        Result.Outcome = EFoundationOutcome::Committed;
        Result.EntityId = Mob.Id;
        Result.bFilled = true;
        return true;
    }
    if (const int32 Dye = FindDyeColor(Item.Id); Dye != INDEX_NONE && Mob.Type == EFoundationMobType::Sheep)
    {
        // minecraft.wiki "Dye" (provisional): a dye recolours a sheep that has its wool and another colour; creative
        // keeps the dye.
        if (Mob.bSheared || Mob.Color == Dye) return false;
        Mob.Color = uint8(Dye);
        ++MobDyes;
        if (!bCreative)
        {
            Player.Inventory.ConsumeOne(Command.Hand);
            Result.bItemConsumed = true;
        }
        Result.Outcome = EFoundationOutcome::Committed;
        Result.EntityId = Mob.Id;
        return true;
    }
    return false;
}
