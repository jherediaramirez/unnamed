#include "Foundation/FoundationActions.h"

FFoundationActionResult FFoundationActions::Dispatch(const FFoundationActionRequest& R)
{
    FFoundationActionResult Result;
    if (R.Phase == EFoundationPhase::Cancel || R.Phase == EFoundationPhase::Release)
    {
        if (R.Action == EFoundationAction::Attack) bBreaking = false;
        if (R.Action == EFoundationAction::Use) bUsing = false;
        Result = {true, R.Phase == EFoundationPhase::Cancel ? TEXT("cancelled") : TEXT("released")};
    }
    else
        switch (R.Action)
        {
        case EFoundationAction::Attack:
            if (bUsing)
                Result.Reason = TEXT("attack rejected: active item use");
            else if (!R.Target.bInRange)
            {
                bBreaking = false;
                Result.Reason = TEXT("attack rejected: no visible target in reach");
            }
            else if (R.Target.Kind == EFoundationTargetKind::Entity && R.Phase == EFoundationPhase::Hold)
                Result.Reason = TEXT("entity attack requires press");
            else
            {
                bBreaking = R.Target.Kind == EFoundationTargetKind::Block;
                ActiveTarget = R.Target.Name;
                Result = {true,
                    bBreaking ? TEXT("break fixture active (no mining)") : TEXT("entity attack fixture (no damage)")};
            }
            break;
        case EFoundationAction::Use:
            if (bBreaking)
                Result.Reason = TEXT("use rejected: block break active");
            else if (R.Target.bInRange && R.Target.Kind == EFoundationTargetKind::Block &&
                R.Target.bRejectInteraction && !(R.bSneak && (Slots[Slot] > 0 || Offhand > 0)))
                Result.Reason = TEXT("block interaction failed; item/offhand fallback rejected");
            else if (R.Target.bInRange && R.Target.bInteractive &&
                !(R.Target.Kind == EFoundationTargetKind::Block && R.bSneak && (Slots[Slot] > 0 || Offhand > 0)))
                Result = {true, TEXT("target interaction fixture accepted")};
            else if (Slots[Slot] > 0)
            {
                bUsing = true;
                Result = {true, TEXT("main-hand use/place fixture accepted (no placement)")};
            }
            else if (Offhand > 0)
            {
                bUsing = true;
                Result = {true, TEXT("offhand use fixture accepted"), EFoundationHand::Off};
            }
            else
                Result.Reason = TEXT("use passed both empty hands");
            break;
        case EFoundationAction::Pick:
            if (bUsing)
                Result.Reason = TEXT("pick rejected: active use");
            else if (!R.Target.bInRange)
                Result.Reason = TEXT("pick rejected: no target in reach");
            else
            {
                Select(R.Target.Kind == EFoundationTargetKind::Block ? 0 : 1);
                Result = {true, TEXT("picked matching diagnostic token slot")};
            }
            break;
        case EFoundationAction::Drop:
            if (Slots[Slot] <= 0)
                Result.Reason = TEXT("drop rejected: empty fixture slot");
            else
            {
                Slots[Slot] = R.bControl ? 0 : Slots[Slot] - 1;
                Result = {true, R.bControl ? TEXT("dropped fixture stack") : TEXT("dropped one fixture token")};
            }
            break;
        case EFoundationAction::Swap:
            Swap(Slots[Slot], Offhand);
            Cancel();
            Result = {true, TEXT("swapped fixture hands")};
            break;
        case EFoundationAction::Inventory:
            Cancel();
            Result = {true, TEXT("opened inventory routing fixture")};
            break;
        }
    LastResult = Result.Reason;
    LastHand = Result.Hand == EFoundationHand::Main ? TEXT("main") : TEXT("off");
    return Result;
}

void FFoundationActions::Select(int32 NewSlot)
{
    Slot = FMath::Clamp(NewSlot, 0, 8);
    Cancel();
}

void FFoundationActions::Scroll(int32 Notches)
{
    Select(((Slot - Notches) % 9 + 9) % 9);
}

void FFoundationActions::Cancel()
{
    bBreaking = bUsing = false;
    ActiveTarget.Empty();
}
