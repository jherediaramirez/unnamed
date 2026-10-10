#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationAuthority.h"
#include "FoundationInteraction.generated.h"

// A placement timing preset. Seconds become whole 50 ms reference steps.
USTRUCT()
struct FFoundationPlacementPreset
{
    GENERATED_BODY()

    UPROPERTY(Config, EditAnywhere, Category = "Placement")
    FName Name;

    // Held use repeats this long after an attempt (reference: 4 ticks).
    UPROPERTY(Config, EditAnywhere, Category = "Placement")
    float HeldRepeatSeconds = .2f;

    // A destination rejected only by the player's own body is rechecked every step for this long, while
    // use stays held (0 = no retry). The expiry is fixed when the candidate is captured.
    UPROPERTY(Config, EditAnywhere, Category = "Placement")
    float SelfClearanceRetrySeconds = 0;

    // Commits, fresh presses included, keep the held cadence from the last committed placement, and a
    // rejection does not buy a full new wait. Off: every attempt waits the held repeat (the reference).
    UPROPERTY(Config, EditAnywhere, Category = "Placement")
    bool bCommitCadence = false;

    UPROPERTY(Config, EditAnywhere, Category = "Placement")
    FString Status;
};

enum class EFoundationInput : uint8
{
    Attack,
    Use
};

// What a use would do at the current target, for the outline and placement preview.
struct FFoundationPreview
{
    FFoundationHit Target;
    bool bPlacement = false, bValid = false, bInteraction = false;
    FIntVector Cell = FIntVector::ZeroValue;
    // The state a valid placement would commit (its shape is what the preview outlines).
    FFoundationBlockState State;
    EFoundationReason Reason = EFoundationReason::None;
    EFoundationHand Hand = EFoundationHand::Main;
};

// The self-clearance retry: one candidate, fixed expiry.
struct FFoundationRetry
{
    bool bActive = false;
    EFoundationHand Hand = EFoundationHand::Main;
    FFoundationHit Target;
    FIntVector Destination = FIntVector::ZeroValue;
    int64 DestinationRevision = 0;
    FFoundationItemRef Item;
    int64 ExpiresStep = 0;
    double PressTime = 0;
};

// The player's interaction state machine: logical held state, fresh presses, mining continuation,
// use repeat timing, the optional retry and command identities. It is the one owner of held-action
// timing. It never mutates gameplay state itself: every change is a command to the authority.
//
// Order inside one reference step (the pawn runs movement, then vitals, then this): resync after a
// reset, equipment seam, target from the step's pose, attack/mining, then use. Fresh presses act only
// on the latest step of an engine frame, so catch-up steps never replay them; held states continue
// through every step; at most one placement commits per engine update.
struct UNNAMED_API FFoundationInteraction
{
    static constexpr int32 MiningDelaySteps = 5;

    FFoundationPlacementPreset Preset;
    int32 HeldRepeatSteps = 4, RetrySteps = 0;
    void SetPreset(const FFoundationPlacementPreset& NewPreset, double StepSeconds);

    int32 PlayerId = INDEX_NONE;
    int32 SessionId = 0;
    uint32 NextSequence = 1;

    // Logical input; Hold/Toggle is already resolved by the controller.
    bool bAttackHeld = false, bUseHeld = false;
    int32 AttackPresses = 0, UsePresses = 0;
    double AttackPressTime = 0, UsePressTime = 0;
    // Bumps on every press: the presentation starts a predicted (cosmetic) swing from it.
    uint32 PressSerial = 0;
    EFoundationInput LastPressed = EFoundationInput::Attack;

    // The latest step's target and the per-frame preview.
    FFoundationHit Target;
    FFoundationPreview Preview;
    // Mirror of the authority's mining session for continuation decisions and presentation.
    bool bMining = false;
    FIntVector MiningCell = FIntVector::ZeroValue;
    int64 MiningRevision = 0;
    FFoundationItemRef MiningItem;
    double MiningProgress = 0;
    int32 MiningDelay = 0;
    bool bAttackNeedsRelease = false;
    FFoundationHit LastRejectedMine;
    // Use timing.
    int64 Step = 0;
    int64 NextUseStep = 0;
    int64 LastPlacementStep = MIN_int64 / 2;
    bool bPlacedThisUpdate = false;
    FFoundationRetry Retry;

    // Diagnostics.
    FFoundationResult LastResult;
    FString LastText = TEXT("no interaction yet");
    TArray<FFoundationResult> History;
    int32 Commands = 0, Attempts = 0, Placed = 0, RetriesStarted = 0, RetryCommits = 0, RetriesCanceled = 0;
    double LastPressToCommitMs = -1;
    // Progress steps of the last progressive break (excluding the post-break delay).
    int32 LastBreakSteps = 0;
    TArray<double> PressToCommitMs;

    void Press(EFoundationInput Input, double NowSeconds);
    void Release(EFoundationInput Input);
    // Focus loss, UI, rebinding, possession, death or reset: drop held state, presses, the retry and mining.
    void CancelAll(FFoundationAuthority& Authority, FFoundationVitals& Vitals, EFoundationReason Reason);
    // Adopts the authority's current session (after a reset) with nothing in progress.
    void Resync(FFoundationAuthority& Authority);
    void BeginUpdate() { bPlacedThisUpdate = false; }
    // One reference step; the caller has published this step's facts to the authority.
    void StepOnce(FFoundationAuthority& Authority, FFoundationVitals& Vitals, bool bLatestStep, double NowSeconds);

    FFoundationResult SelectSlot(FFoundationAuthority& Authority, FFoundationVitals& Vitals, int32 Slot);
    FFoundationResult Scroll(FFoundationAuthority& Authority, FFoundationVitals& Vitals, int32 Notches);
    FFoundationResult SwapHands(FFoundationAuthority& Authority, FFoundationVitals& Vitals);
    FFoundationResult Drop(FFoundationAuthority& Authority, FFoundationVitals& Vitals, bool bWholeStack);
    // The creative inventory's write: Slot 0-8 is the hotbar, 9 the offhand; a None item or zero count clears it.
    FFoundationResult SetCreativeSlot(
        FFoundationAuthority& Authority, FFoundationVitals& Vitals, int32 Slot, FName Item, int32 Count);
    // Selects the hotbar slot whose item places the targeted block (or has it as its wall block). False when there is
    // none.
    bool Pick(FFoundationAuthority& Authority, FFoundationVitals& Vitals);
    // Per frame, after the steps: what a use would do at the current aim.
    void UpdatePreview(const FFoundationAuthority& Authority, const FFoundationPlayerFacts& Facts);
    // The equipment seam: the main-hand item type and its total attack speed drive the vitals' clock.
    void SyncEquipment(const FFoundationAuthority& Authority, FFoundationVitals& Vitals) const;
    FString Describe() const;

private:
    FFoundationResult Send(FFoundationAuthority& Authority, FFoundationVitals& Vitals, FFoundationCommand Command);
    FFoundationCommand MakeCommand(
        const FFoundationAuthority& Authority, EFoundationCommandType Type, EFoundationHand Hand) const;
    void StepAttack(FFoundationAuthority& Authority, FFoundationVitals& Vitals, bool bFresh);
    void StartMining(FFoundationAuthority& Authority, FFoundationVitals& Vitals, bool bFresh);
    void ContinueMining(FFoundationAuthority& Authority, FFoundationVitals& Vitals);
    void AbortMining(FFoundationAuthority& Authority, FFoundationVitals& Vitals);
    void StepUse(FFoundationAuthority& Authority, FFoundationVitals& Vitals, bool bLatestStep, double NowSeconds);
    bool StepRetry(FFoundationAuthority& Authority, FFoundationVitals& Vitals, double NowSeconds);
    void Attempt(FFoundationAuthority& Authority, FFoundationVitals& Vitals, double PressTime, double NowSeconds);
    FFoundationResult UsePipeline(FFoundationAuthority& Authority, FFoundationVitals& Vitals);
    void OnPlaced(double PressTime, double NowSeconds);
    void CancelRetry(EFoundationReason Reason);
    bool IsCadenceOpen() const;
    void ClearMining();
};
