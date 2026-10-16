#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationItems.h"

// Block entities (T1 step 8a, the furnace): the slots and timers a block keeps besides its state, UObject-free. The
// authority owns the store, keyed by world cell, so an entity outlives its column's unload; like every block edit it
// lasts for the session (saved worlds keep it from the voxel plan's phase 10, Docs/VoxelWorld.md "Edits"). The barrel
// and the chest (steps 8b, 9) add their kinds to the same store.

// The cooking recipe types (the 26.3 recipe types); the furnace cooks Smelting, the smoker and blast furnace (8b)
// theirs.
enum class EFoundationCookingKind : uint8
{
    Smelting,
    Blasting,
    Smoking,
    Campfire
};

// One cooking recipe (FoundationCookingData.inl), resolved against the rules' items.
struct FFoundationCookingRecipe
{
    FName Id;
    EFoundationCookingKind Kind = EFoundationCookingKind::Smelting;
    TArray<int32> Ingredients;
    int32 Result = INDEX_NONE;
    int32 Count = 1;
    int32 CookTicks = 200;
    float Experience = 0;
};

// The generated cooking recipes and fuels (Scripts/GenerateCookingData.py: the 26.3 jar's recipes, the value report's
// survival.r3.fuels), resolved by item id. Recipes and fuels naming an item the rules lack are left out.
class UNNAMED_API FFoundationCooking
{
public:
    void Build(TFunctionRef<int32(FName Item)> FindItem);
    bool IsBuilt() const { return bBuilt; }
    // A block's burn ticks for an item (survival.r3.fuels: the furnace's for Smelting; the smoker's for Smoking and the
    // blast furnace's for Blasting, already halved by their speed 2); 0 for no fuel.
    int32 GetBurnTicks(int32 Item, EFoundationCookingKind Kind = EFoundationCookingKind::Smelting) const;
    bool IsFuel(int32 Item) const { return GetBurnTicks(Item) > 0; }
    // The recipe of Kind that takes Item, or INDEX_NONE.
    int32 Find(EFoundationCookingKind Kind, int32 Item) const;
    const FFoundationCookingRecipe* Get(int32 Recipe) const;
    int32 GetRecipeCount() const { return Recipes.Num(); }
    int32 GetFuelCount() const { return Fuels.Num(); }

private:
    bool bBuilt = false;
    TArray<FFoundationCookingRecipe> Recipes;
    // Item -> burn ticks in the furnace, the smoker and the blast furnace (X, Y, Z).
    TMap<int32, FIntVector> Fuels;
    TMap<TPair<uint8, int32>, int32> ByIngredient;
};

enum class EFoundationBlockEntityKind : uint8
{
    Furnace,
    // A barrel (T1 step 9): 27 slots, no ticks.
    Barrel,
    // A chest or trapped chest half (T1 step 18-chest): 27 slots, no ticks; a double chest is two.
    Chest
};

// One block's entity: its kind, the block id it belongs to, its slots and (a furnace) its timers.
struct FFoundationBlockEntity
{
    // A furnace's slots, as its menu numbers them.
    static constexpr int32 FurnaceInput = 0, FurnaceFuel = 1, FurnaceResult = 2, FurnaceSlots = 3;
    // A barrel's slots (vanilla's ChestMenu with 3 rows).
    static constexpr int32 BarrelSlots = 27;
    static int32 GetSlots(EFoundationBlockEntityKind Kind)
    {
        return Kind == EFoundationBlockEntityKind::Barrel || Kind == EFoundationBlockEntityKind::Chest ? BarrelSlots
                                                                                                       : FurnaceSlots;
    }

    EFoundationBlockEntityKind Kind = EFoundationBlockEntityKind::Furnace;
    FName Block;
    TArray<FFoundationItemInstance> Items;
    // A furnace (vanilla's litTimeRemaining, litTotalTime, cookingTimer, cookingTotalTime): fuel ticks left and the
    // current fuel's total, cooking progress and the input's cook time.
    int32 LitTime = 0, LitDuration = 0, CookProgress = 0, CookTotal = 0;
    // Recipe index (FFoundationCooking, stable while the rules' items are) -> items cooked since the output was last
    // taken; their XP is paid then (or on a break).
    TMap<int32, int32> RecipesUsed;
    // A chest half's viewers when last counted (FFoundationAuthority::UpdateContainerOpen): a double chest's count is
    // both halves' and kept on each.
    int32 Viewers = 0;

    bool IsLit() const { return LitTime > 0; }
};

// What a furnace tick needs from the rules.
struct FFoundationFurnaceRules
{
    const FFoundationCooking* Cooking = nullptr;
    TFunction<int32(int32 Item)> GetMaxStack;
    // Fuel remainders and the sponge: the empty bucket, the water bucket, the lava bucket, the wet sponge.
    int32 Bucket = INDEX_NONE, WaterBucket = INDEX_NONE, LavaBucket = INDEX_NONE, WetSponge = INDEX_NONE;
};

class UNNAMED_API FFoundationBlockEntities
{
public:
    FFoundationBlockEntity* Find(const FIntVector& Cell) { return Entities.Find(Cell); }
    const FFoundationBlockEntity* Find(const FIntVector& Cell) const { return Entities.Find(Cell); }
    // The entity at Cell, made for Block when there is none (or when the one there belongs to another block).
    FFoundationBlockEntity& FindOrAdd(const FIntVector& Cell, EFoundationBlockEntityKind Kind, FName Block);
    bool Remove(const FIntVector& Cell, FFoundationBlockEntity* OutRemoved = nullptr);
    void Clear()
    {
        Entities.Empty();
        NextInstanceId = 1 << 30;
    }
    int32 Num() const { return Entities.Num(); }
    TMap<FIntVector, FFoundationBlockEntity>& GetAll() { return Entities; }
    const TMap<FIntVector, FFoundationBlockEntity>& GetAll() const { return Entities; }
    // A new stack of the store's own (instance ids from 2^30 up, apart from the inventories').
    FFoundationItemInstance MakeStack(int32 Item, int32 Count);

private:
    TMap<FIntVector, FFoundationBlockEntity> Entities;
    int32 NextInstanceId = 1 << 30;
};

namespace FoundationFurnace
{
    // foundation.Furnace: 1 furnaces, smokers and blast furnaces open their menus and, in loaded columns, tick (fuel,
    // cooking, cooling, the lit state); 0 as before T1 step 8a: no block use (a right-click places against them) and
    // no ticks.
    UNNAMED_API bool IsOn();
    // foundation.Furnace.Ambience: 1 a lit furnace, smoker or blast furnace plays its sound (T1 step 8b) and shows its
    // smoke and flame (8b2b); 0 neither.
    UNNAMED_API bool IsAmbienceOn();
    // foundation.Furnace.AmbienceForce (dev, a cheat variable, default 0): 1 every lit block gets a display callback on
    // every ambience tick (the sound still rolls its 0.1), so a shot or a probe does not wait on the 1 in 50.
    UNNAMED_API bool IsAmbienceForced();
    // The ambience's 20 Hz clock (UFoundationInteractionView::PlayFurnaceAmbience): adds DeltaTime and gives the ticks
    // to roll this frame, at most 4. Paused (bRunning false: the pawn's simulation is off, as in the pause menu; a
    // container screen keeps it on) it rolls none and starts over (Codex review 25, finding 1; vanilla stops sounds
    // while paused, MC-118081).
    UNNAMED_API int32 StepAmbienceClock(float& Clock, float DeltaTime, bool bRunning);
    // Whether a block entity's smoke, flame and crackle play: a lit furnace, smoker or blast furnace, with
    // foundation.Furnace (bFurnaceSwitch) on.
    UNNAMED_API bool HasAmbience(const FFoundationBlockEntity& Entity, bool bFurnaceSwitch);
    // One lit block's display callbacks over Ticks of that clock: each tick one with chance DisplayChance, Roll's
    // U[0, 1) draw (provisional scheduling, T1 8b1); every tick with foundation.Furnace.AmbienceForce 1; none with
    // foundation.Furnace.Ambience 0. Each callback spawns the block's smoke and flame and rolls its sound (T1 8b2b).
    constexpr float DisplayChance = 1.f / 50;
    UNNAMED_API int32 RollDisplayCallbacks(int32 Ticks, TFunctionRef<float()> Roll);

    // What a block cooks: the smoker Smoking, the blast furnace Blasting, anything else (the furnace) Smelting.
    UNNAMED_API EFoundationCookingKind GetCookingKind(FName Block);
    // The cooking speed (survival.r3.fuels, survival.r3.recipes): 2 for the smoker and blast furnace, 1 for the
    // furnace. It halves their recipes' loaded cook times (200 -> 100); their fuel ticks come halved from the data.
    constexpr int32 GetSpeed(EFoundationCookingKind Kind)
    {
        return Kind == EFoundationCookingKind::Smoking || Kind == EFoundationCookingKind::Blasting ? 2 : 1;
    }
    // A recipe's cook time in Kind's block (the default 200 without one), divided by the block's speed.
    UNNAMED_API int32 GetCookTotal(const FFoundationCookingRecipe* Recipe, EFoundationCookingKind Kind);

    // One furnace tick (the value report's survival.r3.furnace_ticks): a lit furnace spends a fuel tick; an unlit one
    // with input it can cook and fuel lights (fuel ticks, the item used up, a lava bucket leaving its bucket); a lit
    // one that can cook advances one tick and at the cook total makes the result (the input used up, the recipe
    // counted for XP); one that cannot cook resets its progress; unlit without both input and fuel, unfinished progress
    // cools 2 a tick, clamped at 0. True when anything changed.
    UNNAMED_API bool Tick(FFoundationBlockEntity& Furnace, const FFoundationFurnaceRules& Rules,
        FFoundationBlockEntities& Store, EFoundationCookingKind Kind = EFoundationCookingKind::Smelting);
    // After a click put another item in the input slot (vanilla's setItem for slot 0): its cook time, progress 0.
    UNNAMED_API void OnInputChanged(
        FFoundationBlockEntity& Furnace, const FFoundationCooking& Cooking, EFoundationCookingKind Kind);
    // The XP owed for the recipes used (survival.r3.xp_rule): per recipe floor(items x xp) plus 1 with the fraction's
    // chance (none for a whole total).
    UNNAMED_API int32 RollExperience(
        const TMap<int32, int32>& RecipesUsed, const FFoundationCooking& Cooking, FRandomStream& Random);
}

namespace FoundationBarrel
{
    // foundation.Barrel: 1 a barrel opens its menu (T1 step 9); 0 as before: no block use (a right-click places
    // against it).
    UNNAMED_API bool IsOn();
    // The barrel's open and close sounds (value-swaps.md "Chest / barrel sounds", survival.r5.play_calls): volume 0.5,
    // pitch 0.9 + 0.1U, the event's attenuation (16).
    constexpr float SoundVolume = .5f;
}
