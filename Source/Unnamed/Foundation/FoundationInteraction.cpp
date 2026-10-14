#include "Foundation/FoundationInteraction.h"

void FFoundationInteraction::SetPreset(const FFoundationPlacementPreset& NewPreset, double StepSeconds)
{
    Preset = NewPreset;
    const double Seconds = FMath::Max(StepSeconds, 1e-3);
    HeldRepeatSteps = FMath::Max(1, FMath::RoundToInt32(Preset.HeldRepeatSeconds / Seconds));
    RetrySteps = FMath::Max(0, FMath::RoundToInt32(Preset.SelfClearanceRetrySeconds / Seconds));
    if (Retry.bActive) CancelRetry(EFoundationReason::Canceled);
}

void FFoundationInteraction::Press(EFoundationInput Input, double NowSeconds)
{
    ++PressSerial;
    LastPressed = Input;
    if (Input == EFoundationInput::Attack)
    {
        bAttackHeld = true;
        bAttackNeedsRelease = false;
        ++AttackPresses;
        AttackPressTime = NowSeconds;
    }
    else
    {
        bUseHeld = true;
        ++UsePresses;
        UsePressTime = NowSeconds;
    }
}

void FFoundationInteraction::Release(EFoundationInput Input)
{
    if (Input == EFoundationInput::Attack)
    {
        // A held break ends at the next step (the reference stops it on its next tick).
        bAttackHeld = false;
        bAttackNeedsRelease = false;
        return;
    }
    bUseHeld = false;
    if (Retry.bActive) CancelRetry(EFoundationReason::Released);
}

void FFoundationInteraction::CancelAll(
    FFoundationAuthority& Authority, FFoundationVitals& Vitals, EFoundationReason Reason)
{
    bAttackHeld = bUseHeld = bAttackNeedsRelease = false;
    AttackPresses = UsePresses = 0;
    if (Retry.bActive) CancelRetry(Reason);
    if (bMining && SessionId == Authority.GetSessionId()) AbortMining(Authority, Vitals);
    ClearMining();
    if (bUsingItem && SessionId == Authority.GetSessionId()) ReleaseItemUse(Authority, Vitals);
    bUsingItem = false;
}

void FFoundationInteraction::Resync(FFoundationAuthority& Authority)
{
    SessionId = Authority.GetSessionId();
    NextSequence = 1;
    ClearMining();
    bUsingItem = false;
    MiningDelay = 0;
    Retry = {};
    AttackPresses = UsePresses = 0;
    bAttackNeedsRelease = false;
    LastRejectedMine = {};
    NextUseStep = 0;
    LastPlacementStep = MIN_int64 / 2;
    bStopSprint = false;
    LastText = TEXT("new lab session");
}

void FFoundationInteraction::ClearMining()
{
    bMining = false;
    MiningProgress = 0;
    MiningItem = {};
}

bool FFoundationInteraction::IsCadenceOpen() const
{
    return !Preset.bCommitCadence || Step >= LastPlacementStep + HeldRepeatSteps;
}

void FFoundationInteraction::SyncEquipment(const FFoundationAuthority& Authority, FFoundationVitals& Vitals) const
{
    const FFoundationPlayer* Player = Authority.FindPlayer(PlayerId);
    if (!Player) return;
    const FFoundationItemInstance& Main = Player->Inventory.GetHand(EFoundationHand::Main);
    const FFoundationItemDefinition& Item = Main.IsEmpty() ? Authority.Rules.Hand : Authority.Rules.GetItem(Main.Item);
    // Item type identity, like the reference's same-item check: a different item restarts recovery,
    // wear or another stack of the same item does not.
    Vitals.UpdateMainHand(Main.IsEmpty() ? FName() : Item.Id);
    Vitals.AttackSpeed = Item.AttacksPerSecond;
}

FFoundationCommand FFoundationInteraction::MakeCommand(
    const FFoundationAuthority& Authority, EFoundationCommandType Type, EFoundationHand Hand) const
{
    FFoundationCommand Command;
    Command.SessionId = SessionId;
    Command.Type = Type;
    Command.Hand = Hand;
    if (const FFoundationPlayer* Player = Authority.FindPlayer(PlayerId))
        Command.Item = Player->Inventory.GetHand(Hand).GetRef();
    return Command;
}

FFoundationResult FFoundationInteraction::Send(
    FFoundationAuthority& Authority, FFoundationVitals& Vitals, FFoundationCommand Command)
{
    Command.Sequence = NextSequence++;
    const FFoundationResult Result = Authority.Execute(PlayerId, Command, Vitals);
    ++Commands;
    if (Result.bSprintKnockback) bStopSprint = true;
    LastResult = Result;
    History.Add(Result);
    if (History.Num() > 32) History.RemoveAt(0, History.Num() - 32);
    LastText = FString::Printf(TEXT("#%u %s %s%s%s"), Result.Sequence, LexToString(Result.Type),
        LexToString(Result.Outcome), Result.Reason == EFoundationReason::None ? TEXT("") : TEXT(": "),
        Result.Reason == EFoundationReason::None ? TEXT("") : LexToString(Result.Reason));
    return Result;
}

void FFoundationInteraction::StepOnce(
    FFoundationAuthority& Authority, FFoundationVitals& Vitals, bool bLatestStep, double NowSeconds)
{
    const FFoundationPlayer* Player = Authority.FindPlayer(PlayerId);
    if (!Player) return;
    if (SessionId != Authority.GetSessionId()) Resync(Authority);
    Step = Player->Facts.Step;
    SyncEquipment(Authority, Vitals);
    Target = Authority.Resolve(Player->Facts);
    if (!Player->Facts.bAlive)
    {
        // Death disables actions; nothing waits to act after the respawn.
        if (bMining) AbortMining(Authority, Vitals);
        if (Retry.bActive) CancelRetry(EFoundationReason::Dead);
        bUsingItem = false;
        AttackPresses = UsePresses = 0;
        return;
    }
    const bool bFreshAttack = bLatestStep && AttackPresses > 0;
    if (bFreshAttack) AttackPresses = 0;
    // Attack does nothing while an item is in use (eating).
    if (!bUsingItem) StepAttack(Authority, Vitals, bFreshAttack);
    // A target interaction may have reset the lab; the new session starts clean next step.
    if (SessionId != Authority.GetSessionId()) return;
    StepUse(Authority, Vitals, bLatestStep, NowSeconds);
}

void FFoundationInteraction::StepAttack(FFoundationAuthority& Authority, FFoundationVitals& Vitals, bool bFresh)
{
    if (bFresh)
    {
        // One issued attack per press: an entity in reach, else a block to mine, else a whiff.
        if (Target.bInRange && Target.Kind == EFoundationHitKind::Entity)
        {
            FFoundationCommand Command = MakeCommand(Authority, EFoundationCommandType::Attack, EFoundationHand::Main);
            Command.Target = Target;
            Send(Authority, Vitals, Command);
            if (bMining) AbortMining(Authority, Vitals);
            return;
        }
        if (Target.bInRange && Target.IsBlockLike())
        {
            StartMining(Authority, Vitals, true);
            return;
        }
        Send(Authority, Vitals, MakeCommand(Authority, EFoundationCommandType::Whiff, EFoundationHand::Main));
        if (bMining) AbortMining(Authority, Vitals);
        return;
    }
    // A hold that began with a press still waiting for the latest step has not begun at this earlier
    // catch-up step: nothing acts for time before the press.
    if (AttackPresses > 0) return;
    if (bAttackHeld)
    {
        // Holding continues or starts a break on a block in reach; it never repeats melee attacks.
        // The post-break delay counts only while aiming at a block, as in the reference.
        if (bAttackNeedsRelease) return;
        if (Target.bInRange && Target.IsBlockLike())
        {
            if (MiningDelay > 0)
                --MiningDelay;
            else
                StartMining(Authority, Vitals, false);
            return;
        }
    }
    if (bMining) AbortMining(Authority, Vitals);
}

void FFoundationInteraction::StartMining(FFoundationAuthority& Authority, FFoundationVitals& Vitals, bool bFresh)
{
    const FFoundationPlayer* Player = Authority.FindPlayer(PlayerId);
    const FFoundationItemRef Item =
        Player ? Player->Inventory.GetHand(EFoundationHand::Main).GetRef() : FFoundationItemRef{};
    // The same cell, block revision and stack continues; a face change keeps the progress.
    if (bMining && Target.Kind == EFoundationHitKind::Block && Target.Cell == MiningCell &&
        Target.Revision == MiningRevision && Item == MiningItem)
    {
        if (MiningDelay > 0)
            --MiningDelay;
        else
            ContinueMining(Authority, Vitals);
        return;
    }
    // A rejected target is asked about once while held, not every step.
    if (!bFresh && LastRejectedMine.SameTarget(Target) && LastRejectedMine.Revision == Target.Revision)
    {
        if (bMining) AbortMining(Authority, Vitals);
        return;
    }
    FFoundationCommand Command = MakeCommand(Authority, EFoundationCommandType::MineStart, EFoundationHand::Main);
    Command.Target = Target;
    const FFoundationResult Result = Send(Authority, Vitals, Command);
    // Whatever session existed has ended at the authority: replaced, refused or completed.
    ClearMining();
    if (Result.IsCommitted())
    {
        LastRejectedMine = {};
        if (Result.bBroken)
        {
            // The instant path: no accumulation and no post-break delay, except in creative (wiki "Creative"), where
            // holding attack breaks a block every 6 ticks (the delay, then the next start); a fresh click bypasses it.
            if (Vitals.Mode == EFoundationPlayerMode::Creative) MiningDelay = MiningDelaySteps;
            return;
        }
        bMining = true;
        MiningCell = Target.Cell;
        MiningRevision = Target.Revision;
        MiningItem = Item;
        // A fresh click accumulates in its own step; a held continuation starts now and accumulates next step.
        if (bFresh)
        {
            if (MiningDelay > 0)
                --MiningDelay;
            else
                ContinueMining(Authority, Vitals);
        }
        return;
    }
    LastRejectedMine = Target;
}

void FFoundationInteraction::ContinueMining(FFoundationAuthority& Authority, FFoundationVitals& Vitals)
{
    FFoundationCommand Command = MakeCommand(Authority, EFoundationCommandType::MineContinue, EFoundationHand::Main);
    Command.Target = Target;
    Command.Target.Cell = MiningCell;
    const FFoundationResult Result = Send(Authority, Vitals, Command);
    if (Result.IsCommitted())
    {
        if (!Result.bBroken)
        {
            MiningProgress = Result.Progress;
            return;
        }
        // Ordinary progressive completion: a separate 5-step delay before the next break accumulates.
        LastBreakSteps = Result.MiningSteps;
        ClearMining();
        MiningDelay = MiningDelaySteps;
        return;
    }
    ClearMining();
    // Without room for the return the block stays; holding does not retry until a fresh press.
    if (Result.Reason == EFoundationReason::InventoryFull) bAttackNeedsRelease = true;
}

void FFoundationInteraction::AbortMining(FFoundationAuthority& Authority, FFoundationVitals& Vitals)
{
    FFoundationCommand Command = MakeCommand(Authority, EFoundationCommandType::MineAbort, EFoundationHand::Main);
    Command.Target.Cell = MiningCell;
    Send(Authority, Vitals, Command);
    ClearMining();
}

void FFoundationInteraction::StepUse(
    FFoundationAuthority& Authority, FFoundationVitals& Vitals, bool bLatestStep, double NowSeconds)
{
    if (bUsingItem)
    {
        // A use in progress takes the use input: its presses are spent, letting go releases it.
        UsePresses = 0;
        StepItemUse(Authority, Vitals);
        return;
    }
    if (bMining)
    {
        // Active mining wins: use never places during a live break, and its clicks are spent.
        UsePresses = 0;
        if (Retry.bActive) CancelRetry(EFoundationReason::Canceled);
        return;
    }
    if (Retry.bActive && StepRetry(Authority, Vitals, NowSeconds)) return;
    if (bLatestStep && UsePresses > 0)
    {
        // Never two placements in one update, nor ahead of a commit cadence: the press waits instead.
        if (bPlacedThisUpdate || !IsCadenceOpen()) return;
        UsePresses = 0;
        Attempt(Authority, Vitals, UsePressTime, NowSeconds);
        return;
    }
    // A hold that began with a still-pending press acts from its press onward, never on earlier catch-up steps.
    if (bUseHeld && UsePresses == 0 && Step >= NextUseStep && !bPlacedThisUpdate && IsCadenceOpen())
        Attempt(Authority, Vitals, 0, NowSeconds);
}

void FFoundationInteraction::StepItemUse(FFoundationAuthority& Authority, FFoundationVitals& Vitals)
{
    if (!bUseHeld)
    {
        ReleaseItemUse(Authority, Vitals);
        return;
    }
    // A slot change or swap already ended the use at the authority: end it here quietly, with no command.
    const FFoundationPlayer* Player = Authority.FindPlayer(PlayerId);
    if (!Player || !Player->Use.bActive || Player->Use.Hand != UseHand)
    {
        bUsingItem = false;
        NextUseStep = Step + HeldRepeatSteps;
        return;
    }
    const FFoundationResult Result =
        Send(Authority, Vitals, MakeCommand(Authority, EFoundationCommandType::UseItemContinue, UseHand));
    UseTicks = Result.UseTicks;
    // A second continue in one step leaves the use running.
    if ((Result.IsCommitted() && !Result.bAte) || Result.Reason == EFoundationReason::StepLimit) return;
    // Finished or canceled. Still holding, the next use waits the held repeat, then eats again.
    bUsingItem = false;
    NextUseStep = Step + HeldRepeatSteps;
}

void FFoundationInteraction::ReleaseItemUse(FFoundationAuthority& Authority, FFoundationVitals& Vitals)
{
    Send(Authority, Vitals, MakeCommand(Authority, EFoundationCommandType::UseItemRelease, UseHand));
    bUsingItem = false;
}

bool FFoundationInteraction::StepRetry(FFoundationAuthority& Authority, FFoundationVitals& Vitals, double NowSeconds)
{
    const FFoundationPlayer* Player = Authority.FindPlayer(PlayerId);
    EFoundationReason Cancel = EFoundationReason::None;
    if (!bUseHeld)
        Cancel = EFoundationReason::Released;
    else if (Step > Retry.ExpiresStep)
        Cancel = EFoundationReason::Expired;
    else if (!Target.bInRange || !Target.SameTarget(Retry.Target) || Target.Revision != Retry.Target.Revision)
        Cancel = EFoundationReason::TargetChanged;
    else if (!Player || Player->Inventory.GetHand(Retry.Hand).GetRef() != Retry.Item)
        Cancel = EFoundationReason::ItemChanged;
    if (Cancel != EFoundationReason::None)
    {
        CancelRetry(Cancel);
        return false;
    }
    // Waiting for the update or cadence still runs the fixed expiry.
    if (bPlacedThisUpdate || !IsCadenceOpen()) return true;
    FFoundationCommand Command = MakeCommand(Authority, EFoundationCommandType::UseOn, Retry.Hand);
    Command.Target = Retry.Target;
    Command.Destination = Retry.Destination;
    Command.DestinationRevision = Retry.DestinationRevision;
    const FFoundationResult Result = Send(Authority, Vitals, Command);
    if (Result.bPlaced)
    {
        ++RetryCommits;
        const double PressTime = Retry.PressTime;
        Retry = {};
        OnPlaced(PressTime, NowSeconds);
        return true;
    }
    // Still inside the player's own body: recheck next step until the expiry.
    if (Result.Outcome == EFoundationOutcome::Rejected && Result.Reason == EFoundationReason::ObstructedBySelf)
        return true;
    CancelRetry(Result.Reason);
    return true;
}

void FFoundationInteraction::Attempt(
    FFoundationAuthority& Authority, FFoundationVitals& Vitals, double PressTime, double NowSeconds)
{
    ++Attempts;
    // The reference waits its held repeat after every attempt. A commit cadence only charges commits,
    // so after a non-placing attempt holding may try again next step.
    NextUseStep = Step + (Preset.bCommitCadence ? 1 : HeldRepeatSteps);
    const FFoundationResult Result = UsePipeline(Authority, Vitals);
    // Full hunger asks again only after the held repeat, whatever the cadence.
    if (Result.Reason == EFoundationReason::NotHungry) NextUseStep = Step + HeldRepeatSteps;
    if (Result.bPlaced)
    {
        OnPlaced(PressTime, NowSeconds);
        return;
    }
    if (Result.Outcome == EFoundationOutcome::Rejected && Result.Reason == EFoundationReason::ObstructedBySelf &&
        RetrySteps > 0 && bUseHeld)
    {
        const FFoundationPlayer* Player = Authority.FindPlayer(PlayerId);
        Retry.bActive = true;
        Retry.Hand = Result.Hand;
        Retry.Target = Target;
        Retry.Destination = Result.Cell;
        Retry.DestinationRevision = Authority.World.Get(Result.Cell).Revision;
        Retry.Item = Player ? Player->Inventory.GetHand(Result.Hand).GetRef() : FFoundationItemRef{};
        Retry.ExpiresStep = Step + RetrySteps;
        Retry.PressTime = PressTime;
        ++RetriesStarted;
    }
}

FFoundationResult FFoundationInteraction::UsePipeline(FFoundationAuthority& Authority, FFoundationVitals& Vitals)
{
    FFoundationResult Result;
    Result.Outcome = EFoundationOutcome::Pass;
    // A ray miss never places: there is no anchor at the ray's end. Hold-to-use items (foundation.Items.Use) still
    // start in the air.
    const bool bTargeted = Target.bInRange && Target.Kind != EFoundationHitKind::None;
    const bool bItemUse = Authority.Options.bItemUse;
    if (!bTargeted && !bItemUse) return Result;
    // Per hand: the target's interaction (inside UseOn), then the item's place capability, then its use; the offhand
    // only after a pass. An explicit failure stops.
    for (const EFoundationHand Hand : {EFoundationHand::Main, EFoundationHand::Off})
    {
        if (bTargeted)
        {
            Result = UseTarget(Authority, Vitals, Hand);
            if (Result.Outcome != EFoundationOutcome::Pass) return Result;
        }
        // Only a hand holding a consumable asks the authority, so other items send nothing new.
        const FFoundationPlayer* Player = Authority.FindPlayer(PlayerId);
        const FFoundationItemInstance* Stack = Player ? &Player->Inventory.GetHand(Hand) : nullptr;
        if (!bItemUse || !Stack || Stack->IsEmpty() || !Authority.Rules.GetItem(Stack->Item).GetConsumable().IsValid())
            continue;
        Result = Send(Authority, Vitals, MakeCommand(Authority, EFoundationCommandType::UseItemStart, Hand));
        if (Result.IsCommitted())
        {
            bUsingItem = true;
            UseHand = Hand;
            UseTicks = 0;
            UseDuration = Result.UseDuration;
        }
        if (Result.Outcome != EFoundationOutcome::Pass) return Result;
    }
    return Result;
}

FFoundationResult FFoundationInteraction::UseTarget(
    FFoundationAuthority& Authority, FFoundationVitals& Vitals, EFoundationHand Hand)
{
    FFoundationResult Result;
    {
        const bool bEntity = Target.Kind == EFoundationHitKind::Entity;
        FFoundationCommand Command =
            MakeCommand(Authority, bEntity ? EFoundationCommandType::UseEntity : EFoundationCommandType::UseOn, Hand);
        Command.Target = Target;
        FIntVector Destination;
        EFoundationReason Ignored;
        const FFoundationPlayer* Player = Authority.FindPlayer(PlayerId);
        if (!bEntity && Player && Authority.GetDestination(*Player, Hand, Target, Destination, Ignored) &&
            Authority.World.IsKnown(Destination))
        {
            Command.Destination = Destination;
            Command.DestinationRevision = Authority.World.Get(Destination).Revision;
        }
        Result = Send(Authority, Vitals, Command);
    }
    return Result;
}

void FFoundationInteraction::OnPlaced(double PressTime, double NowSeconds)
{
    ++Placed;
    LastPlacementStep = Step;
    bPlacedThisUpdate = true;
    if (PressTime > 0)
    {
        LastPressToCommitMs = (NowSeconds - PressTime) * 1000.;
        PressToCommitMs.Add(LastPressToCommitMs);
        if (PressToCommitMs.Num() > 256) PressToCommitMs.RemoveAt(0, PressToCommitMs.Num() - 256);
    }
}

void FFoundationInteraction::CancelRetry(EFoundationReason Reason)
{
    if (!Retry.bActive) return;
    ++RetriesCanceled;
    Retry = {};
    LastText = FString(TEXT("retry canceled: ")) + LexToString(Reason);
}

FFoundationResult FFoundationInteraction::SelectSlot(
    FFoundationAuthority& Authority, FFoundationVitals& Vitals, int32 Slot)
{
    FFoundationCommand Command = MakeCommand(Authority, EFoundationCommandType::SelectSlot, EFoundationHand::Main);
    Command.Slot = Slot;
    if (Retry.bActive) CancelRetry(EFoundationReason::ItemChanged);
    return Send(Authority, Vitals, Command);
}

FFoundationResult FFoundationInteraction::Scroll(
    FFoundationAuthority& Authority, FFoundationVitals& Vitals, int32 Notches)
{
    const FFoundationPlayer* Player = Authority.FindPlayer(PlayerId);
    const int32 Current = Player ? Player->Inventory.Selected : 0;
    const int32 Slots = FFoundationInventory::HotbarSlots;
    return SelectSlot(Authority, Vitals, ((Current - Notches) % Slots + Slots) % Slots);
}

FFoundationResult FFoundationInteraction::SwapHands(FFoundationAuthority& Authority, FFoundationVitals& Vitals)
{
    if (Retry.bActive) CancelRetry(EFoundationReason::ItemChanged);
    return Send(Authority, Vitals, MakeCommand(Authority, EFoundationCommandType::SwapHands, EFoundationHand::Main));
}

FFoundationResult FFoundationInteraction::Drop(
    FFoundationAuthority& Authority, FFoundationVitals& Vitals, bool bWholeStack)
{
    FFoundationCommand Command = MakeCommand(Authority, EFoundationCommandType::Drop, EFoundationHand::Main);
    Command.bWholeStack = bWholeStack;
    return Send(Authority, Vitals, Command);
}

FFoundationResult FFoundationInteraction::SetCreativeSlot(
    FFoundationAuthority& Authority, FFoundationVitals& Vitals, int32 Slot, FName Item, int32 Count, int32 Damage)
{
    FFoundationCommand Command = MakeCommand(Authority, EFoundationCommandType::SetCreativeSlot, EFoundationHand::Main);
    Command.Slot = Slot;
    Command.CreativeItem = Item;
    Command.CreativeCount = Count;
    Command.CreativeDamage = Damage;
    if (Retry.bActive) CancelRetry(EFoundationReason::ItemChanged);
    return Send(Authority, Vitals, Command);
}

FFoundationResult FFoundationInteraction::ContainerClick(FFoundationAuthority& Authority, FFoundationVitals& Vitals,
    int32 ContainerId, int32 Slot, int32 Button, EFoundationClickType Type)
{
    FFoundationCommand Command = MakeCommand(Authority, EFoundationCommandType::ContainerClick, EFoundationHand::Main);
    Command.ContainerId = ContainerId;
    Command.Slot = Slot;
    Command.Button = Button;
    Command.ClickType = Type;
    if (Retry.bActive) CancelRetry(EFoundationReason::ItemChanged);
    return Send(Authority, Vitals, Command);
}

FFoundationResult FFoundationInteraction::ContainerClose(FFoundationAuthority& Authority, FFoundationVitals& Vitals)
{
    return Send(
        Authority, Vitals, MakeCommand(Authority, EFoundationCommandType::ContainerClose, EFoundationHand::Main));
}
bool FFoundationInteraction::Pick(FFoundationAuthority& Authority, FFoundationVitals& Vitals)
{
    const FFoundationHit& Hit = Preview.Target.Kind != EFoundationHitKind::None ? Preview.Target : Target;
    const FFoundationPlayer* Player = Authority.FindPlayer(PlayerId);
    if (!Player || !Hit.bInRange || Hit.Kind != EFoundationHitKind::Block) return false;
    const FName Block = Authority.World.GetBlock(Authority.World.Get(Hit.Cell).State).Id;
    for (int32 Slot = 0; Slot < FFoundationInventory::HotbarSlots; ++Slot)
    {
        const FFoundationItemInstance& Stack = Player->Inventory.Hotbar[Slot];
        if (Stack.IsEmpty()) continue;
        // A standing-and-wall item is picked from either block (a wall torch picks the torch).
        const FFoundationItemDefinition& Item = Authority.Rules.GetItem(Stack.Item);
        if (Item.PlacesBlock == Block || Item.PlacesWallBlock == Block)
        {
            if (Slot != Player->Inventory.Selected) SelectSlot(Authority, Vitals, Slot);
            return true;
        }
    }
    return false;
}

void FFoundationInteraction::UpdatePreview(const FFoundationAuthority& Authority, const FFoundationPlayerFacts& Facts)
{
    Preview = {};
    Preview.Target = Authority.Resolve(Facts);
    const FFoundationHit& Hit = Preview.Target;
    const FFoundationPlayer* Player = Authority.FindPlayer(PlayerId);
    if (!Player || !Facts.bAlive || !Hit.bInRange || !Hit.IsBlockLike() || bMining) return;
    const bool bHolding = !Player->Inventory.GetHand(EFoundationHand::Main).IsEmpty() ||
        !Player->Inventory.GetHand(EFoundationHand::Off).IsEmpty();
    if (Hit.Kind == EFoundationHitKind::Static && Authority.Statics.IsValidIndex(Hit.StaticIndex) &&
        !Authority.Statics[Hit.StaticIndex].Interaction.IsNone() && !(Facts.bSecondaryUse && bHolding))
    {
        Preview.bInteraction = true;
        return;
    }
    for (const EFoundationHand Hand : {EFoundationHand::Main, EFoundationHand::Off})
    {
        const FFoundationItemInstance& Stack = Player->Inventory.GetHand(Hand);
        if (Stack.IsEmpty() || !Authority.Rules.GetItem(Stack.Item).CanPlace()) continue;
        Preview.bPlacement = true;
        Preview.Hand = Hand;
        Preview.Reason = Authority.CheckPlacement(*Player, Hand, Hit, Preview.Cell, &Preview.State);
        Preview.bValid = Preview.Reason == EFoundationReason::None;
        return;
    }
}

FString FFoundationInteraction::Describe() const
{
    FString Text = FString::Printf(TEXT("Preset %s: held repeat %d steps, retry %d, %s | session %d seq %u\n"),
        *Preset.Name.ToString(), HeldRepeatSteps, RetrySteps,
        Preset.bCommitCadence ? TEXT("commit cadence") : TEXT("attempt cadence"), SessionId, NextSequence - 1);
    Text +=
        FString::Printf(TEXT("Held attack=%d use=%d | mining %s %.0f%% delay %d | retry %s\n"), bAttackHeld, bUseHeld,
            bMining ? *FoundationGrid::DescribeCell(MiningCell) : TEXT("none"), MiningProgress * 100., MiningDelay,
            Retry.bActive ? *FString::Printf(TEXT("%s until step %lld"),
                                *FoundationGrid::DescribeCell(Retry.Destination), Retry.ExpiresStep)
                          : TEXT("none"));
    Text += FString::Printf(TEXT("Use next step %lld (now %lld) | attempts %d placed %d retries %d->%d canceled %d | "
                                 "press->commit %.1f ms\n"),
        NextUseStep, Step, Attempts, Placed, RetriesStarted, RetryCommits, RetriesCanceled, LastPressToCommitMs);
    return Text + TEXT("Last: ") + LastText;
}
