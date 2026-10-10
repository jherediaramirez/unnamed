#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockPlacement.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationItems.h"
#include "Foundation/FoundationTargeting.h"
#include "Foundation/FoundationVitals.h"

enum class EFoundationCommandType : uint8
{
    SelectSlot,
    SwapHands,
    Drop,
    // Use against a block-like target: its interaction, then the hand item's place capability.
    UseOn,
    // Use against an entity: its interaction (the training targets have none).
    UseEntity,
    MineStart,
    MineContinue,
    MineAbort,
    Attack,
    // An attack at nothing in reach: it still restarts attack recovery.
    Whiff,
    // The creative inventory writes a stack into a hotbar slot or the offhand (Slot 9), as vanilla's
    // ServerboundSetCreativeModeSlotPacket does. Allowed only while the lab's creative inventory rule is on.
    SetCreativeSlot
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
    // SetCreativeSlot: the item (None clears the slot) and count.
    FName CreativeItem;
    int32 CreativeCount = 0;
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
    bool bItemConsumed = false, bItemReturned = false, bToolWorn = false, bToolBroke = false;
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
    // The creative inventory may write any item into the hotbar and offhand (the Playground's block picker).
    bool bCreativeInventory = true;
    // Plant rules (phase 6, S7): double plants as one unit, and pops after an edit. Off only for A/B runs.
    bool bPlantRules = true;
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
    float AttackSampleTicks = .5f, CriticalStrength = .9f, CriticalMultiplier = 1.5f, HitExhaustion = .1f;
    float TargetMaxHealth = 20;
    int32 ResultCacheSize = 128;

    int32 FindItem(FName Id) const;
    // The definition for an inventory index; an invalid index is the bare hand.
    const FFoundationItemDefinition& GetItem(int32 Index) const;
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
    Popped
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
    EFoundationReason Reason = EFoundationReason::None;
    FName Interaction;
    int64 Step = 0;
};

struct FFoundationPlayer
{
    int32 Id = 0;
    FFoundationPlayerFacts Facts;
    bool bHasFacts = false;
    FFoundationInventory Inventory;
    FFoundationMining Mining;
    int64 LastPlacementStep = MIN_int64;
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
    void SetFacts(int32 Player, const FFoundationPlayerFacts& Facts);

    FFoundationResult Execute(int32 Player, const FFoundationCommand& Command, FFoundationVitals& Vitals);
    // One reference step of authority-owned state (the targets' damage cooldowns).
    void Step();
    // A new session: old commands become stale, the seed blocks, loadouts and targets come back.
    void Reset();
    int32 GetSessionId() const { return SessionId; }
    // Resets once the current command finishes (or at once outside a command).
    void RequestReset() { bResetRequested = true; }
    // Called after a committed block interaction, before any requested reset: the owner reacts to
    // interactions the authority does not define itself (lab controls).
    TFunction<void(FName Interaction)> OnInteraction;

    // Shared queries: the same code serves previews, caller claims and validation.
    FFoundationHit Resolve(const FFoundationPlayerFacts& Facts) const;
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
    float GetBlockHardness(const FIntVector& Cell) const;
    FFoundationTrainingTarget* FindTarget(int32 Id);
    const FFoundationItemDefinition& GetHandItem(const FFoundationPlayer& Player, EFoundationHand Hand) const;

    const TArray<FFoundationEvent>& GetEvents() const { return Events; }
    int64 GetEventSerial() const { return EventSerial; }

    // Diagnostics.
    int32 Placements = 0, Breaks = 0, Hits = 0, Rejections = 0, Duplicates = 0, Resets = 0, Pops = 0;

private:
    FFoundationInventory MakeLoadout();
    // The placement context for a hit (statics must be anchors); false with a reason when nothing can be placed.
    bool MakePlacementContext(const FFoundationPlayer& Player, const FFoundationHit& Hit,
        const FFoundationBlockType& Placing, FFoundationPlacementContext& OutContext, bool& bOutAccepted,
        EFoundationReason& OutReason) const;
    void Remember(FFoundationPlayer& Player, const FFoundationResult& Result);
    FFoundationResult& Reject(FFoundationResult& Result, EFoundationReason Reason);
    void AddEvent(FFoundationEvent Event);
    void UseOn(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result);
    void UseEntity(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result);
    void MineStart(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result);
    void MineContinue(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result);
    void Attack(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationVitals& Vitals,
        FFoundationResult& Result);
    void Whiff(FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationVitals& Vitals,
        FFoundationResult& Result);
    // Removes a block once: capacity first, then removal, return and wear together.
    bool BreakBlock(FFoundationPlayer& Player, const FIntVector& Cell, FFoundationResult& Result);
    bool CheckHand(const FFoundationPlayer& Player, const FFoundationCommand& Command, FFoundationResult& Result);
    // Plants: a placed state that can never hold (a faceless vine), or a double plant without room for its upper half.
    EFoundationReason CheckPlantPlacement(const FIntVector& Destination, FFoundationBlockState Placed) const;
    // Whether a state still holds in its cell after a neighbour changed (the plant survival rules). A rule that reads
    // an unknown cell (outside the bounds, a column not loaded) keeps the plant.
    bool CanPlantSurvive(const FIntVector& Cell, FFoundationBlockState State) const;
    // A vine, glow lichen or sculk vein without the faces that are no longer held (State when all are).
    FFoundationBlockState TrimPlantFaces(const FIntVector& Cell, FFoundationBlockState State) const;
    // After an edit at Cell replaced Before: a double plant's two halves stay one unit, then the changed cells'
    // neighbours that no longer survive pop, one level deep (Jesus's answer 6).
    void SettlePlants(const FIntVector& Cell, FFoundationBlockState Before, const FFoundationPlayer& Player);

    TArray<FFoundationPlayer> Players;
    TArray<FFoundationEvent> Events;
    int64 EventSerial = 0;
    int32 SessionId = 1;
    int32 NextPlayerId = 1;
    bool bResetRequested = false;
};
