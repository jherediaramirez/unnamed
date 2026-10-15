#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockPlacement.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationContainer.h"
#include "Foundation/FoundationContainers.h"
#include "Foundation/FoundationExperienceOrbs.h"
#include "Foundation/FoundationItemEntities.h"
#include "Foundation/FoundationItems.h"
#include "Foundation/FoundationLoot.h"
#include "Foundation/FoundationMobs.h"
#include "Foundation/FoundationRecipes.h"
#include "Foundation/FoundationTargeting.h"
#include "Foundation/FoundationVitals.h"

enum class EFoundationCommandType : uint8
{
    SelectSlot,
    SwapHands,
    Drop,
    // Use against a block-like target: its interaction, then the hand item's use on it (an axe strips; step 14), then
    // its place capability.
    UseOn,
    // Use against an entity: its interaction (the training targets have none).
    UseEntity,
    MineStart,
    MineContinue,
    MineAbort,
    Attack,
    // An attack at nothing in reach: it still restarts attack recovery.
    Whiff,
    // The creative inventory writes a stack into an inventory slot (Slot: the Inventory number, 0-8 the hotbar, 9-35
    // the main grid, 36-39 the armor, 40 the offhand), as vanilla's ServerboundSetCreativeModeSlotPacket does.
    // Allowed only while the lab's creative inventory rule is on.
    SetCreativeSlot,
    // A click in an open container (vanilla's container click): ContainerId, Slot (the menu's slot number, -999
    // outside the window), Button and ClickType, run by FFoundationContainerMenu with the player's cursor stack.
    ContainerClick,
    // The container screen closes: the cursor stack goes back into the inventory.
    ContainerClose,
    // Hold-to-use (step 10, eating; foundation.Items.Use): start using the hand's consumable, advance it one reference
    // step, or let go. A release, a slot or hand change, death or a step without a continue cancels it; nothing is
    // consumed until it finishes.
    UseItemStart,
    UseItemContinue,
    UseItemRelease
};

// Pass lets the caller continue (next hand); Committed changed authoritative state; Rejected changed
// nothing; Canceled ended an operation in progress. Work is synchronous, so nothing is pending.
enum class EFoundationOutcome : uint8
{
    Pass,
    Committed,
    Rejected,
    Canceled
};

enum class EFoundationReason : uint8
{
    None,
    UnknownPlayer,
    StaleSession,
    ExpiredSequence,
    InvalidSlot,
    Dead,
    ItemChanged,
    NoItem,
    NoTarget,
    TargetMismatch,
    OutOfRange,
    StaleRevision,
    NotAnchor,
    OutsideBounds,
    DestinationOccupied,
    CannotSurvive,
    NoSupply,
    ObstructedByStatic,
    ObstructedByEntity,
    ObstructedBySelf,
    StepLimit,
    UnknownBlock,
    ImmutableGeometry,
    Unbreakable,
    NoMiningSession,
    TargetChanged,
    InventoryFull,
    TargetDead,
    TargetCooldown,
    DropDeferred,
    Released,
    Expired,
    Canceled,
    // A creative player's item that breaks no blocks in creative (swords, spears).
    CreativeCannotBreak,
    // A creative write's damage outside 0 to the item's max damage (0 for an item that never wears).
    InvalidDamage,
    // Eating at full hunger (wiki "Food"); invulnerable modes may always eat.
    NotHungry,
    // A continue or release without an item use in progress.
    NotUsing,
    // A use in progress ended because foundation.Items.Use turned 0.
    ItemUseOff,
    Count
};

UNNAMED_API const TCHAR* LexToString(EFoundationReason Reason);
UNNAMED_API const TCHAR* LexToString(EFoundationOutcome Outcome);
UNNAMED_API const TCHAR* LexToString(EFoundationCommandType Type);

class FVoxelPlantSurvival;

// One request across the authority boundary. Target, destination, revisions and item are the caller's
// claims; the authority re-resolves and re-validates everything from its own state.
struct FFoundationCommand
{
    int32 SessionId = 0;
    // Unique per player and session, from 1. A retransmission repeats the sequence.
    uint32 Sequence = 0;
    EFoundationCommandType Type = EFoundationCommandType::SelectSlot;
    EFoundationHand Hand = EFoundationHand::Main;
    FFoundationItemRef Item;
    FFoundationHit Target;
    FIntVector Destination = FIntVector::ZeroValue;
    int64 DestinationRevision = 0;
    int32 Slot = 0;
    bool bWholeStack = false;
    // SetCreativeSlot: the item (None clears the slot), count and damage (the stack's own, as vanilla's creative slot
    // packet carries the whole stack).
    FName CreativeItem;
    int32 CreativeCount = 0;
    int32 CreativeDamage = 0;
    // ContainerClick: the container (0 the player's own menu), the button and the click type; Slot is the menu slot.
    int32 ContainerId = 0;
    int32 Button = 0;
    EFoundationClickType ClickType = EFoundationClickType::Pickup;
};

struct FFoundationResult
{
    uint32 Sequence = 0;
    EFoundationCommandType Type = EFoundationCommandType::SelectSlot;
    EFoundationOutcome Outcome = EFoundationOutcome::Rejected;
    EFoundationReason Reason = EFoundationReason::None;
    EFoundationHand Hand = EFoundationHand::Main;
    // World revision after the command.
    int64 WorldRevision = 0;
    // A retransmitted sequence returned this stored result and applied nothing.
    bool bDuplicate = false;
    // Effects, set only by what actually happened.
    FIntVector Cell = FIntVector::ZeroValue;
    // The state placed or broken.
    FFoundationBlockState State;
    bool bPlaced = false, bBroken = false, bInstant = false, bInteracted = false, bReset = false;
    double Progress = 0;
    // Progress steps of the break this result completed or continued.
    int32 MiningSteps = 0;
    int32 EntityId = 0;
    float Damage = 0, DamageApplied = 0, Strength = 0, TargetHealth = 0;
    bool bCritical = false, bTargetDied = false, bChargeReset = false;
    // Melee (items S7): the hit knocked its target back (a landed hit outside the target's hurt window); a
    // sprint-knockback attack landed (extra knockback; the attacker's sprint ends); a sweep was performed, and how many
    // nearby targets it hurt.
    bool bKnockback = false, bSprintKnockback = false, bSweep = false;
    int32 SweptTargets = 0;
    bool bItemConsumed = false, bItemReturned = false, bToolWorn = false, bToolBroke = false;
    // A block's use opened a container (survival step 7): its id (never 0) and menu; the owner opens the screen.
    int32 OpenedContainer = 0;
    EFoundationMenuKind OpenedMenu = EFoundationMenuKind::Inventory;
    // Item use (step 10): the reference steps used so far and the item's total; bAte when this result finished eating.
    int32 UseTicks = 0, UseDuration = 0;
    bool bAte = false;
    // An axe stripped the block at Cell (step 14); State is the stripped state.
    bool bStripped = false;
    // A bucket (step 17) took the source at Cell (bFilled) or poured one there (bEmptied); State is the cell's new
    // state.
    bool bFilled = false, bEmptied = false;
    // What a survival break returned to the inventory (items S5b): one entry per stack rolled.
    TArray<FFoundationLootDrop> Drops;
    FName Interaction;

    bool IsCommitted() const { return Outcome == EFoundationOutcome::Committed; }
};

// The movement owner's authoritative facts for one reference step, published before interaction runs.
// Validation, mining and combat read these, never caller claims.
struct FFoundationPlayerFacts
{
    FVector FeetCm = FVector::ZeroVector;
    double HeightCm = 180, WidthCm = 60;
    // The gameplay eye and aim: no camera offset, bob, hand or animation.
    FVector EyeCm = FVector(0, 0, 162);
    FVector Aim = FVector::ForwardVector;
    double FallDistanceCm = 0;
    // Horizontal distance moved over the last reference step (the sweep's movement rule).
    double StepMovedCm = 0;
    bool bGrounded = true, bSprinting = false, bBodyInWater = false, bEyesInWater = false;
    // No ladders, mounts or effects yet; kept so the critical rule reads like the reference.
    bool bClimbing = false, bPassenger = false, bMobilityRestricted = false;
    bool bAlive = true;
    // Crouch intent: with something in hand it bypasses block interactions.
    bool bSecondaryUse = false;
    int64 Step = 0;

    FBox GetBody() const;
};

// A separate damageable entity. It reuses the player's health owner and its Hurt path (damage cooldown,
// armor, absorption, death) but only its damage cooldown counts down: no food, air or regeneration.
// Once down it stays down until an explicit lab reset.
struct FFoundationTrainingTarget
{
    int32 Id = 0;
    FString Label;
    FBox Box = FBox(ForceInit);
    FFoundationVitals Health;
    int32 Hits = 0;
    float DamageTaken = 0;
};

// Lab supply rules. Infinite supply is its own rule: it implies no flight, reach, invulnerability,
// instant mining or creative HUD. The finite fixture runs the identical commit path.
struct FFoundationLabOptions
{
    bool bInfiniteSupply = true;
    bool bToolWear = false;
    int32 FiniteBlockCount = 64;
    // The placement timing preset players adopt (input pacing only; validation never reads it).
    FName PlacementPreset;
    // Hold-to-use items (step 10, eating): food starts, continues and finishes through the UseItem commands. Off, they
    // pass, as before. The pawn copies foundation.Items.Use in before each step; tests set it themselves.
    bool bItemUse = false;
    // The creative inventory may write any item into the hotbar and offhand (the Playground's block picker).
    bool bCreativeInventory = true;
    // Plant rules (phase 6, S7): double plants as one unit, and pops after an edit. Off only for A/B runs.
    bool bPlantRules = true;
    // Item entities (items S8a): a survival break drops its loot into the world, and a full inventory no longer
    // refuses it. Off, the loot goes straight to the inventory, as before. The pawn copies foundation.Items.Entities
    // in before each step; tests set it themselves. Items S8b: on, Q and Ctrl+Q throw, and stacks merge and despawn.
    bool bItemEntities = false;
    // The full inventory (survival step 5a): 27 main and 4 armor slots besides the hotbar and offhand. Off, the lab
    // inventory, the hotbar and offhand only. The pawn copies foundation.Inventory.Full in before each step; tests
    // set it themselves.
    bool bFullInventory = false;
    // Crafting (survival step 6): the crafting grid's result slot. Off, the grid takes and holds items and the result
    // stays empty. The pawn copies foundation.Crafting in before each step.
    bool bCrafting = true;
    // Experience orbs (survival step 12): a survival break of an ore pays orbs. Off, none spawn, as before. The pawn
    // copies foundation.Items.Xp in before each step; tests set it themselves.
    bool bExperienceOrbs = false;
    // Block use (survival step 7): right-clicking a block with a use handler (FFoundationAuthority::RegisterBlockUse;
    // the crafting table) runs it instead of placing. Off, right-click always places, as before. The pawn copies
    // foundation.Blocks.Use in before each step; tests set it themselves.
    bool bBlockUse = false;
    // Mobs (survival T6 20a): mobs step and can be hit. Off, none spawn and Resolve sees training targets only. The
    // pawn copies foundation.Mobs in before each step; tests set it themselves.
    bool bMobs = false;
    // Passive spawning (survival T6 20b2): with bMobs and GetBiomeId set, a column first seen loaded may spawn a
    // creature pack. The pawn copies foundation.Mobs.Spawn in before each step.
    bool bMobSpawn = false;
    // Fire (step 16a): mobs take lava contact and burning damage, as the pawn does. The pawn copies foundation.Fire in.
    bool bFire = false;
    // Furnaces (T1 steps 8a, 8b; smokers and blast furnaces too): a right-click opens the block's menu, and every one
    // in a loaded column ticks each step (fuel, cooking, cooling, its lit state), open or not. Off, as before 8a: no
    // menu (the click passes on and places) and no ticks. The pawn copies foundation.Furnace in before each step.
    bool bFurnace = false;
};

// Definitions and constants the authority applies; pure data, so tests build their own.
struct UNNAMED_API FFoundationRules
{
    TArray<FFoundationItemDefinition> Items;
    // The bare hand's melee and mining profile (an empty slot).
    FFoundationItemDefinition Hand;
    // Hotbar contents from slot 1; None leaves a slot empty.
    TArray<FName> Loadout;
    double BlockReachCm = 450, EntityReachCm = 300;
    float EligibleDivisor = 30, IneligibleDivisor = 100;
    float AirborneMiningMultiplier = .2f, SubmergedMiningMultiplier = .2f;
    // Extension points with neutral values: efficiency (added above speed 1), haste, fatigue, break speed.
    float MiningEfficiency = 0, HasteMultiplier = 1, FatigueMultiplier = 1, BlockBreakSpeed = 1;
    // A progressive break completes at 1 within this tolerance, so nominal step counts are exact.
    double CompletionTolerance = 1e-6;
    // Items carrying any of these break no blocks in creative, whatever their tool component says. BuildRules adds
    // Item.Spears: the wiki ("Breaking", "Creative") lists spears, which 26.3 gives no tool component.
    FGameplayTagContainer CreativeNoBreakTags;
    // CriticalStrength is the 84.8% gate (charge above 0.9) that critical hits, sweeps and sprint-knockback attacks
    // share.
    float AttackSampleTicks = .5f, CriticalStrength = .9f, CriticalMultiplier = 1.5f, HitExhaustion = .1f;
    // Sweeps (wiki "Sweep attack"): an item carrying any of SweepItemTags (BuildRules adds Item.Swords), at most
    // SweepMaxMoveCm moved over the last step (movement_speed, 0.1 blocks per tick), hurts every live target whose box
    // meets the primary's grown by SweepGrowCm and whose position lies within SweepReachCm of the player's. Each takes
    // SweepDamage plus SweepingDamageRatio (Sweeping Edge's extension point) times the hit's cooldown-scaled damage,
    // and is knocked back at SweepKnockback blocks per tick.
    FGameplayTagContainer SweepItemTags;
    double SweepMaxMoveCm = 10, SweepReachCm = 300;
    FVector SweepGrowCm = FVector(100, 100, 25);
    float SweepDamage = 1, SweepingDamageRatio = 0, SweepKnockback = .4f;
    float TargetMaxHealth = 20;
    int32 ResultCacheSize = 128;
    // Block loot (items S5b): tables by block id and the named tool predicates they use (BuildRules, from DA_Items). A
    // block without a table keeps the provisional rule, "drops itself". LootSeed is mixed with the cell and the break
    // count into every roll; 0 until a world's seed is wired in.
    TMap<FName, FFoundationLootTable> LootTables;
    TArray<FFoundationLootPredicate> LootPredicates;
    uint64 LootSeed = 0;
    // Entity loot (survival T6 20a): tables by entity id ("cow", "sheep/white"; BuildRules, from DA_Mobs).
    TMap<FName, FFoundationLootTable> EntityLootTables;
    // Creature spawning by biome id ("minecraft:plains"; survival T6 20b2, DA_Mobs).
    TMap<FName, FFoundationBiomeSpawns> BiomeSpawns;
    // Crafting recipes (survival step 6; BuildRules, from DA_Recipes), resolved against Items.
    FFoundationRecipeBook Recipes;

    int32 FindItem(FName Id) const;
    // The definition for an inventory index; an invalid index is the bare hand.
    const FFoundationItemDefinition& GetItem(int32 Index) const;
    // Whether a progressive break's accumulated progress completes it.
    bool CompletesBreak(double Progress) const { return Progress >= 1. - CompletionTolerance; }
    // Whether an item breaks blocks for a creative player: its tool component, then CreativeNoBreakTags.
    bool CanDestroyInCreative(const FFoundationItemDefinition& Item) const;
};

struct FFoundationMining
{
    bool bActive = false;
    FIntVector Cell = FIntVector::ZeroValue;
    int64 Revision = 0;
    EFoundationFace Face = EFoundationFace::None;
    FFoundationItemRef Item;
    double Progress = 0;
    int32 Steps = 0;
    int64 LastStep = -1;
};

enum class EFoundationEventKind : uint8
{
    Placed,
    Broken,
    Hit,
    Whiff,
    Rejected,
    Interacted,
    ToolBroke,
    TargetDown,
    Reset,
    // A plant (or another model block) lost its support after an edit and was removed (FoundationPlantRules).
    Popped,
    // A player picked up (part of) an item entity (items S8a): Item, Count, LocationCm, bWholeStack.
    PickedUp,
    // Item use (step 10): Eating on the steps that make the eating sound and particles (Item, LocationCm at the eye);
    // Ate when it finished (Item, Amount = the nutrition).
    Eating,
    Ate,
    // An axe stripped a block (step 14): Cell, State (the stripped state), Item (the axe).
    Stripped,
    // A player took an experience orb (step 12): EntityId the orb, Count its points, LocationCm where it stood (world
    // cm), bWholeStack whether that was its last (the orb is gone).
    ExperiencePickedUp,
    // A bucket (step 17) took a source (Filled: Item the filled bucket, LocationCm the player's feet) or poured one
    // (Emptied: Item the poured bucket); Cell the fluid's cell, State its new state.
    Filled,
    Emptied,
    // Lava met water and hardened (step 17): Cell, State the block it became (stone, obsidian, cobblestone, basalt).
    Extinguished,
    // A block's use opened a container (survival step 7): Cell the block, EntityId the container id, Interaction the
    // block id.
    ContainerOpened,
    // A mob's sound (survival T6 20a): Interaction the sound event, LocationCm where (world cm), Amount the volume,
    // EntityId the mob.
    MobSound,
    // A piece went into an armor slot (survival step 19a): Item the piece, LocationCm the player's feet (world cm).
    Equipped,
    // A block's hand use changed it (survival B3a: a door, trapdoor or gate opened or closed): Cell the used block,
    // State its new state, Interaction the sound event (FoundationBlockFamilies::GetUseSound).
    BlockUsed
};

// A committed (or rejected) outcome for presentation: effects fire once from these.
struct FFoundationEvent
{
    int64 Serial = 0;
    EFoundationEventKind Kind = EFoundationEventKind::Placed;
    int32 Player = 0;
    FIntVector Cell = FIntVector::ZeroValue;
    FFoundationBlockState State;
    int32 EntityId = 0;
    float Amount = 0;
    bool bCritical = false;
    // Hit events (items S7): the hit knocked the entity back; a sprint-knockback hit; a sweep's hit on a nearby entity.
    bool bKnockback = false, bSprintKnockback = false, bSweep = false;
    EFoundationReason Reason = EFoundationReason::None;
    FName Interaction;
    int64 Step = 0;
    // PickedUp: the item (a FFoundationRules::Items index) and how many; where the entity stood (world cm); whether
    // that took the whole stack (the entity is gone).
    int32 Item = INDEX_NONE, Count = 0;
    FVector LocationCm = FVector::ZeroVector;
    bool bWholeStack = false;
};

// A hold-to-use in progress: which hand and stack (by instance, so its count may change), how far.
struct FFoundationItemUse
{
    bool bActive = false;
    EFoundationHand Hand = EFoundationHand::Main;
    int32 InstanceId = 0;
    int32 Item = INDEX_NONE;
    int32 Ticks = 0, Duration = 0;
    int64 LastStep = -1;
};

namespace FoundationBucket
{
    // The buckets step 17 uses (empty, water, lava); milk and the mob buckets wait for mobs.
    inline bool IsBucket(FName Id)
    {
        return Id == TEXT("bucket") || Id == TEXT("water_bucket") || Id == TEXT("lava_bucket");
    }
}

namespace FoundationItemUse
{
    // foundation.Items.Use: 1 food is eaten by holding use (step 10); 0 it passes, as before. Steps 14 and 17 (axe
    // stripping, buckets) hang off the same switch.
    UNNAMED_API bool IsOn();
    // The periodic eating effect (sound and crumbs) fires on step Ticks of a Duration-step use when the elapsed steps
    // pass floor(Duration x 0.21875) and the remaining steps are a multiple of 4, before the last step (value report,
    // Eating / Crumb particles and duration, eating.crumbs): 8, 12, ..., 28 for 32. The last step has the completion
    // effect only (Ate).
    constexpr double EffectsDelayFraction = .21875;
    constexpr int32 EffectsEveryTicks = 4;
    UNNAMED_API bool IsEffectTick(int32 Ticks, int32 Duration);
}

// A container menu open besides the player's own (survival step 7): vanilla's containerId, the menu, the block it
// belongs to, and the crafting table's 3x3, which belongs to the screen (closing returns it).
struct FFoundationOpenMenu
{
    int32 ContainerId = 0;
    EFoundationMenuKind Kind = EFoundationMenuKind::Inventory;
    FIntVector Cell = FIntVector::ZeroValue;
    FName Block;
    TArray<FFoundationItemInstance> Grid;

    bool IsOpen() const { return ContainerId != 0; }
};

namespace FoundationBlockUse
{
    // foundation.Blocks.Use: 1 blocks with a use handler (the crafting table) open their screen on right-click; 0
    // right-click always places, as before.
    UNNAMED_API bool IsOn();
}

struct FFoundationPlayer
{
    int32 Id = 0;
    FFoundationPlayerFacts Facts;
    bool bHasFacts = false;
    FFoundationInventory Inventory;
    FFoundationMining Mining;
    FFoundationItemUse Use;
    int64 LastPlacementStep = MIN_int64;
    // Experience orbs (step 12): steps until the next orb can be taken, and points taken but not yet claimed by the
    // player's vitals (TakeExperience).
    int32 ExperienceDelay = 0, PendingExperience = 0;
    // The container open besides the inventory (survival step 7); its last id (vanilla counts 1-100, then wraps).
    FFoundationOpenMenu OpenMenu;
    int32 LastContainerId = 0;
    // The respawn point (step 11a): the block cell whose bottom centre the player's feet return to; unset, the world's
    // spawn. Not saved to world.json until player data is saved.
    TOptional<FIntVector> RespawnCell;
    // Results by sequence for retransmissions; bounded, oldest evicted first.
    TMap<uint32, FFoundationResult> Results;
    TArray<uint32> ResultOrder;
    uint32 HighestEvicted = 0;
};

// The one gameplay authority for the lab: it validates and commits block mutations, item counts,
// durability, mining progress and accepted combat exactly once. Standalone play uses this same
// boundary locally; nothing here is networked or saved.
class UNNAMED_API FFoundationAuthority
{
public:
    FFoundationBlockWorld World;
    // The plant survival rules (VoxelBlocks::GetPlantSurvival), set by the world's owner. Without them plants keep
    // only their two halves together.
    TSharedPtr<const FVoxelPlantSurvival> PlantSurvival;
    // The collision item entities move against (world cm), set by the world's owner to the query its player moves
    // against (the lab's fixtures and the blocks; a generated world's ungenerated columns are solid). Unset, the block
    // world's own and the statics.
    TFunction<void(const FBox& Region, TArray<FBox>& Out)> GatherCollision;
    // Passive spawning (survival T6 20b2), set by a generated world's owner: the biome id at a cell
    // ("minecraft:plains", none when unknown) and the world seed packs are drawn from. Unset (the Playground): no
    // passive spawning.
    TFunction<FName(const FIntVector& Cell)> GetBiomeId;
    uint64 MobSpawnSeed = 0;
    FFoundationRules Rules;
    FFoundationLabOptions Options;
    TArray<FFoundationStatic> Statics;
    TArray<FFoundationTrainingTarget> Targets;
    // Restored by every reset: the lab's initial blocks (the starter blocks and the block gallery).
    TArray<TPair<FIntVector, FFoundationBlockState>> Seed;

    int32 AddPlayer();
    void RemovePlayer(int32 Id);
    FFoundationPlayer* FindPlayer(int32 Id);
    const FFoundationPlayer* FindPlayer(int32 Id) const;
    // The player's container 0 as its menu shows it, by menu slot (46: the result the grid makes now, the grid, the
    // armor, the main grid, the hotbar, the offhand), for the screen's view.
    TArray<FFoundationItemInstance> GetPlayerMenu(const FFoundationPlayer& Player) const;
    // The player's open container (survival step 7) as its menu shows it, by menu slot; empty when none is open.
    TArray<FFoundationItemInstance> GetOpenMenu(const FFoundationPlayer& Player) const;

    // A block's use (survival step 7, vanilla's useWithoutItem/useItemOn for blocks): UseOn runs it for a block hit
    // with this id when Options.bBlockUse is on, before an axe strips or the hand places. It runs for the main hand
    // only, and not when a crouching player holds something (the sneak bypass: that player places instead). Return
    // true when the use happened (fill Result: Committed, and any effects); false passes on to stripping and placing.
    // The authority registers the crafting table and the furnace; doors, buttons and levers (T5) register theirs the
    // same way. To consume a click without acting (a bed by day), return true with Outcome Rejected; false only passes.
    // WouldUse, when given, answers the placement preview without side effects: false for a handler that would pass
    // now (Codex review 12, finding 4); unset, a registered handler counts as using the click.
    using FBlockUse = TFunction<bool(FFoundationAuthority& Authority, FFoundationPlayer& Player,
        const FFoundationHit& Hit, FFoundationResult& Result)>;
    using FBlockWouldUse = TFunction<bool(
        const FFoundationAuthority& Authority, const FFoundationPlayer& Player, const FFoundationHit& Hit)>;
    void RegisterBlockUse(FName BlockId, FBlockUse Use, FBlockWouldUse WouldUse = nullptr);
    // Writes a block use's states (survival B3a, FoundationBlockFamilies::Use: both door halves at once), each cell
    // settled as an edit (survival, the neighbour shapes), and adds a BlockUsed event with Sound; Result is Committed
    // and bInteracted, Cell the used block. A write to a cell nobody knows rejects it all (OutsideBounds), unwritten.
    void CommitBlockUse(FFoundationPlayer& Player, const FIntVector& Cell,
        TConstArrayView<TPair<FIntVector, FFoundationBlockState>> Writes, FName Sound, FFoundationResult& Result);
    // Whether the block in Cell has a use handler.
    bool HasBlockUse(const FIntVector& Cell) const;
    // Whether Player's use on Hit would be the block's (previews show no placement for it): a handler, and its
    // WouldUse.
    bool WouldUseBlock(const FFoundationPlayer& Player, const FFoundationHit& Hit) const;
    // Opens a container menu for Player at Cell (a block use calls it); Result carries the new id.
    void OpenContainer(FFoundationPlayer& Player, EFoundationMenuKind Kind, const FIntVector& Cell, FName Block,
        FFoundationResult& Result);
    // Block entities by world cell (T1 step 8a: furnaces): they outlive column unloads; Reset clears them.
    FFoundationBlockEntities BlockEntities;
    // The generated cooking recipes and fuels, resolved against Rules.Items on first use.
    const FFoundationCooking& GetCooking() const;
    // An open container closes at this distance past the interaction range, from the player's eye to its block's box
    // (Codex review 12, finding 1; survival2.r23.container_rule: the range plus 4 blocks, measured to the box).
    double ContainerReachExtraCm = 400;
    void SetFacts(int32 Player, const FFoundationPlayerFacts& Facts);

    FFoundationResult Execute(int32 Player, const FFoundationCommand& Command, FFoundationVitals& Vitals);
    // One reference step of authority-owned state: the targets' damage cooldowns, then the item entities (their
    // motion and pickup delays, then every live player's pickups), the orbs, the open containers' validity (a menu
    // whose block is gone or out of reach closes) and the furnaces.
    void Step();
    // A new session: old commands become stale, the seed blocks, loadouts and targets come back.
    void Reset();
    int32 GetSessionId() const { return SessionId; }
    // Resets once the current command finishes (or at once outside a command).
    void RequestReset() { bResetRequested = true; }
    // Called after a committed block interaction, before any requested reset: the owner reacts to
    // interactions the authority does not define itself (lab controls).
    TFunction<void(FName Interaction)> OnInteraction;
    // After a fluid replaced Before at Cell (the voxel world's FVoxelFluidSystem, water and sky W2b): a washed-away
    // block pops with no drop (plan Q7), then the plants around settle as after an edit (the other half of a double
    // plant, a lily pad over drained water).
    void SettleAfterFluid(const FIntVector& Cell, FFoundationBlockState Before, bool bWashedAway);

    // Shared queries: the same code serves previews, caller claims and validation.
    FFoundationHit Resolve(const FFoundationPlayerFacts& Facts, bool bFluidSources = false) const;
    // Where a placement would go without knowing the item: the clicked block when replaceable, else beside it.
    bool GetDestination(const FFoundationHit& Hit, FIntVector& OutDestination, EFoundationReason& OutReason) const;
    // The same for the item in a hand, as vanilla's BlockPlaceContext: a matching single slab is completed in
    // place. Command destinations come from this.
    bool GetDestination(const FFoundationPlayer& Player, EFoundationHand Hand, const FFoundationHit& Hit,
        FIntVector& OutDestination, EFoundationReason& OutReason) const;
    // Placement validation without side effects, for previews. Reason None means it would place OutState.
    EFoundationReason CheckPlacement(const FFoundationPlayer& Player, EFoundationHand Hand, const FFoundationHit& Hit,
        FIntVector& OutDestination, FFoundationBlockState* OutState = nullptr) const;
    double GetMiningDelta(const FFoundationPlayer& Player, const FFoundationBlockType& Block) const;
    // 26.3's harvest check: the block needs no correct tool, or the tool is correct for its drops.
    static bool CanHarvest(const FFoundationItemDefinition& Tool, const FFoundationBlockType& Block);
    float GetBlockHardness(const FIntVector& Cell) const;
    FFoundationTrainingTarget* FindTarget(int32 Id);
    const FFoundationItemDefinition& GetHandItem(const FFoundationPlayer& Player, EFoundationHand Hand) const;

    // Item entities (items S8a), oldest first; Reset clears them.
    const TArray<FFoundationItemEntity>& GetItemEntities() const { return ItemEntities; }
    // Adds an item entity at a position (cm from the block world's origin); its id, or 0 for an invalid item or count.
    int32 SpawnItem(int32 Item, int32 Count, const FVector& LocalCm, const FVector& VelocityCm, int32 PickupDelay,
        int32 Damage = 0);
    // Experience orbs (step 12), oldest first; Reset clears them.
    const TArray<FFoundationExperienceOrb>& GetExperienceOrbs() const { return ExperienceOrbs; }
    // Spawns orbs worth Total points around a point (cm from the block world's origin), split by the orb values, with
    // the block drop's spread seeded by RandomSeed; how many orbs. Ores call it on a break; a furnace (T1 step 8a)
    // calls it when its output is taken.
    int32 SpawnExperience(const FVector& LocalCm, int32 Total, int32 RandomSeed);
    // /kill @e (step 11a): removes every item entity / experience orb, as a despawn does (the views drop what is no
    // longer listed); how many.
    int32 RemoveItemEntities();
    int32 RemoveExperienceOrbs();
    // The points a player has taken since the last call (its owner adds them to its vitals after each step).
    int32 TakeExperience(int32 Player);
    // Armor (survival step 19a): the worn pieces' armor and toughness (each piece counts only in its own slot; none
    // with foundation.Inventory.Full 0, which has no armor slots). Its owner sets them on its vitals every step.
    void GetWornArmor(int32 Player, int32& OutArmor, float& OutToughness) const;
    // The wear a player's hurts owe (FFoundationVitals::PendingArmorWear) on every worn piece that wears, with lab
    // tool wear on and not in creative; a piece worn out breaks (a ToolBroke event). How many broke.
    int32 WearArmor(int32 Player, int32 Amount, EFoundationPlayerMode Mode);
    // Mobs (survival T6 20a), oldest first; Reset clears them. SpawnMob adds one standing at a position (cm from the
    // block world's origin) facing Yaw: its id, or 0 with Options.bMobs off.
    const TArray<FFoundationMob>& GetMobs() const { return Mobs; }
    int32 SpawnMob(EFoundationMobType Type, const FVector& LocalCm, float Yaw = 0);
    FFoundationMob* FindMob(int32 Id);
    // Removes every mob, without loot; how many.
    int32 ClearMobs();
    // A cell as the mobs' A* sees it (survival T6 20b): the top of the collision boxes that start in it, and whether it
    // holds a fluid.
    FFoundationPathCell GetPathCell(const FIntVector& Cell);

    // Death (step 11a). The respawn point: beds (step B7) set it when slept in and clear it when the bed is gone; mobs
    // (step 20c) read it. The pawn's Respawn puts the feet at the cell's bottom centre when set, else at the world's
    // spawn. Session only: not saved to world.json until player data is saved.
    void SetRespawnPoint(int32 PlayerId, const FIntVector& Cell);
    void ClearRespawnPoint(int32 PlayerId);
    TOptional<FIntVector> GetRespawnPoint(int32 PlayerId) const;
    // Vanilla drops the inventory at the moment of death [survival.r6.death_items]: every non-empty stack of the 41
    // slots, the carried stack and the 2x2 crafting grid becomes one item entity, then the inventory is empty.
    // FeetLocalCm: the feet, cm from the block world's origin. Nothing drops with infinite supply. RandomSeed makes the
    // throw deterministic.
    void DropInventoryOnDeath(int32 PlayerId, const FVector& FeetLocalCm, int32 RandomSeed);

    const TArray<FFoundationEvent>& GetEvents() const { return Events; }
    int64 GetEventSerial() const { return EventSerial; }

    // Diagnostics.
    int32 Placements = 0, Breaks = 0, Hits = 0, Rejections = 0, Duplicates = 0, Resets = 0, Pops = 0;
    int32 ItemsSpawned = 0, ItemsPickedUp = 0, ItemsThrown = 0, ItemsMerged = 0, ItemsDespawned = 0, ItemsEaten = 0,
          Strips = 0, BucketUses = 0;
    int32 OrbsSpawned = 0, OrbsPickedUp = 0, OrbsMerged = 0, OrbsDespawned = 0;
    int32 MobsSpawned = 0, MobsKilled = 0, MobPaths = 0, MobEggs = 0, MobGrazes = 0;
    // Passive spawning: columns seen loaded, packs spawned, the mobs in them.
    int32 MobColumnsSeen = 0, MobPacks = 0, MobsPackSpawned = 0;

private:
    FFoundationInventory MakeLoadout();
    // Block use handlers registered by block id (RegisterBlockUse); they override the built-in ones (the crafting
    // table, FindBlockUse).
    struct FBlockUseEntry
    {
        FBlockUse Use;
        FBlockWouldUse WouldUse;
    };
    TMap<FName, FBlockUseEntry> BlockUses;
    const FBlockUseEntry* FindBlockUse(FName BlockId) const;
    // The open menu's container slots: the crafting table's grid, or its block entity's (a copy).
    TArray<FFoundationItemInstance> GetMenuGrid(const FFoundationPlayer& Player) const;
    // Whether the player's open menu still has its block (and its block entity) at its cell, within reach.
    bool IsMenuValid(const FFoundationPlayer& Player) const;
    // Closes the open menu (ContainerClose): the cursor, the crafting grid, then the 2x2 back into the inventory, the
    // rest thrown; with foundation.Items.Entities 0 and leftovers, refused (Result says why) and the menu stays.
    void CloseMenu(FFoundationPlayer& Player, FFoundationResult& Result);
    // Each step: an open menu that is no longer valid closes (Codex review 12, finding 1).
    void StepOpenMenus();
    // Each step: a furnace entity in a loaded cell whose block is gone is dropped; with Options.bFurnace the rest tick.
    void StepFurnaces();
    FFoundationFurnaceRules MakeFurnaceRules() const;
    // Pays a furnace's stored XP as orbs at LocalCm (its output taken, or the furnace broken) and clears it.
    void PayFurnaceExperience(FFoundationBlockEntity& Furnace, const FVector& LocalCm, int32 RandomSeed);
    // A broken container's slots as item entities, each stack with its damage (provisional spread, the block drops').
    void SpawnContents(const FIntVector& Cell, TConstArrayView<FFoundationItemInstance> Items, int32 RandomSeed);
    mutable FFoundationCooking Cooking;
    mutable int32 CookingItems = INDEX_NONE;
    // The item facts container clicks read (max stack, equipment slot); Block, an open furnace's, picks what its
    // shift-click counts as cookable.
    FFoundationContainerRules MakeContainerRules(FName Block = NAME_None) const;
    // The placement context for a hit (statics must be anchors); false with a reason when nothing can be placed.
    bool MakePlacementContext(const FFoundationPlayer& Player, const FFoundationHit& Hit,
        const FFoundationBlockType& Placing, FFoundationPlacementContext& OutContext, bool& bOutAccepted,
        EFoundationReason& OutReason) const;
    void Remember(FFoundationPlayer& Player, const FFoundationResult& Result);
    FFoundationResult& Reject(FFoundationResult& Result, EFoundationReason Reason);
    void AddEvent(FFoundationEvent Event);
    void UseOn(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result,
        EFoundationPlayerMode Mode);
    // Axe stripping (step 14): true when the hand's axe stripped the hit block (Result filled). Adventure passes.
    bool StripOn(FFoundationPlayer& Player, const FFoundationCommand& Command, const FFoundationHit& Hit,
        FFoundationResult& Result, EFoundationPlayerMode Mode);
    void UseEntity(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result);
    void MineStart(FFoundationPlayer& Player, const FFoundationCommand& Command, const FFoundationVitals& Vitals,
        FFoundationResult& Result);
    void MineContinue(FFoundationPlayer& Player, const FFoundationCommand& Command, const FFoundationVitals& Vitals,
        FFoundationResult& Result);
    void Attack(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationVitals& Vitals,
        FFoundationResult& Result);
    void Whiff(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationVitals& Vitals,
        FFoundationResult& Result);
    // A bucket in the hand (step 17): fills from or pours a fluid source as an instant use. False when the hand holds
    // no bucket or the switch is off; otherwise Result says what happened (a pass included).
    bool UseBucket(FFoundationPlayer& Player, const FFoundationCommand& Command, EFoundationPlayerMode Mode,
        FFoundationResult& Result);
    void UseItemStart(FFoundationPlayer& Player, const FFoundationCommand& Command, const FFoundationVitals& Vitals,
        FFoundationResult& Result);
    // A swappable piece in the hand swaps with its armor slot (survival step 19a). False when the hand holds none.
    bool UseEquip(FFoundationPlayer& Player, const FFoundationCommand& Command, EFoundationPlayerMode Mode,
        FFoundationResult& Result);
    // An Equipped event for each armor slot that now holds a different piece than Before.
    void AddEquipEvents(const FFoundationPlayer& Player, TConstArrayView<FFoundationItemInstance> Before);
    void UseItemContinue(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationVitals& Vitals,
        FFoundationResult& Result);
    // Ends a use in progress (nothing consumed); true when one was active.
    bool CancelUse(FFoundationPlayer& Player);
    // Removes a block once: capacity first (unless the loot drops into the world), then removal, return and wear
    // together. A creative break returns nothing and wears nothing.
    bool BreakBlock(
        FFoundationPlayer& Player, const FIntVector& Cell, FFoundationResult& Result, bool bCreative = false);
    bool CheckHand(const FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result);
    // Plants: a placed state that can never hold (a faceless vine), or a double plant without room for its upper half.
    EFoundationReason CheckPlantPlacement(const FIntVector& Destination, FFoundationBlockState Placed) const;
    // Whether a state still holds in its cell after a neighbour changed (the plant survival rules, and the torch rule
    // for torches). A rule that reads an unknown cell (outside the bounds, a column not loaded) keeps the plant.
    bool CanPlantSurvive(const FIntVector& Cell, FFoundationBlockState State) const;
    // A vine, glow lichen or sculk vein without the faces that are no longer held (State when all are).
    FFoundationBlockState TrimPlantFaces(const FIntVector& Cell, FFoundationBlockState State) const;
    // After an edit at Cell replaced Before: a double plant's two halves stay one unit, then the changed cells'
    // neighbours that no longer survive pop, one level deep (Jesus's answer 6), and a stack to its end (Q1).
    void SettlePlants(const FIntVector& Cell, FFoundationBlockState Before, const FFoundationPlayer& Player);
    // A block's loot for Tool, rolled before it goes: its table, else "drops itself" (a double slab is two).
    void RollBlockDrops(const FIntVector& Cell, FFoundationBlockState State, const FFoundationItemDefinition& Tool,
        int32 RandomSeed, TArray<FFoundationLootDrop>& Out) const;
    // A broken block's loot as item entities, split into stacks that fit (provisional spread and speed).
    void SpawnDrops(const FIntVector& Cell, TConstArrayView<FFoundationLootDrop> Drops, int32 RandomSeed);
    void StepItemEntities();
    // Q and Ctrl+Q (items S8b): one item or the selected stack thrown from the eye along the look.
    void Drop(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result);
    // The throw Q and the container screens share (items S8b; items fixes): Count of Stack from 0.3 blocks below the
    // eye along the look, pickup delay 40. False when nothing spawned.
    bool Throw(const FFoundationPlayer& Player, const FFoundationItemInstance& Stack, int32 Count);
    // The world and static boxes in Region for an entity's step; for a Body inside a block (items fixes, S8a-b), out
    // to the neighbours of its centre's cell too.
    void GatherEntityCollision(const FBox& Region, const FBox& Body, TArray<FBox>& Boxes);
    // Every MergeTicks of its age, an item takes in same-item stacks inside its merge box (items S8b).
    void MergeItemEntities();
    // Step 12: the orbs' motion toward the nearest player, despawn and merges, then every live player's pickups.
    void StepExperienceOrbs();
    // Survival T6 20a: each mob's freeze test, AI, steering, motion and timers; a corpse at RemoveAtDeathTime drops
    // its loot and experience and goes.
    void StepMobs();
    // The killing hit (a player's or a hazard's): the loot and experience drop now [survival2.r20.mob_death].
    void DropMobLoot(const FFoundationMob& Mob);
    // Survival T6 20b2: a mob's falls, lava and burning through its FFoundationVitals (the chicken takes no fall
    // damage); the fluid its box touches.
    void StepMobHazards(FFoundationMob& Mob);
    bool IsMobInFluid(const FBox& WorldBody, bool bLava) const;
    // Survival T6 20b2: chunk-generation packs for the columns around the live players first seen loaded.
    void StepMobSpawns();
    // One column's chunk-generation roll and pack; how many mobs it spawned.
    int32 SpawnPack(const FIntPoint& Column);
    // A cell a passive mob of Type may stand in at the top of its column (X, Y): the feet cell above the top
    // motion-blocking block, which must be a grass block, with room for the box; false when there is none.
    bool FindPackCell(EFoundationMobType Type, int32 X, int32 Y, FIntVector& OutFeet);
    void AddMobSound(const FFoundationMob& Mob, const TCHAR* Sound, float Volume);
    // Survival T6 20b: a sheep standing in short grass or on a grass block; eating it (the grass goes, or the block
    // turns to dirt).
    bool CanGraze(const FFoundationMob& Mob) const;
    void EatGrass(const FFoundationMob& Mob);
    // GetPathCell's scratch boxes, kept between calls.
    TArray<FBox> PathBoxes;
    // The A* budget's first mob this step (rotates, so later mobs are not starved).
    int32 PathStart = 0;
    // Columns (SpawnColumnCells square) already seen loaded: each rolls its chunk-generation pack once per session.
    TSet<FIntPoint> MobSpawnColumns;

    TArray<FFoundationPlayer> Players;
    TArray<FFoundationEvent> Events;
    TArray<FFoundationItemEntity> ItemEntities;
    TArray<FFoundationExperienceOrb> ExperienceOrbs;
    TArray<FFoundationMob> Mobs;
    int32 NextItemEntityId = 1, NextOrbId = 1, NextMobId = FoundationMobs::FirstId;
    int64 EventSerial = 0;
    int32 SessionId = 1;
    int32 NextPlayerId = 1;
    bool bResetRequested = false;
};
