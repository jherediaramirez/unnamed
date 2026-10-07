#include "Foundation/FoundationActions.h"

FFoundationActionResult FFoundationActions::Dispatch(const FFoundationActionRequest& Request)
{
    FFoundationActionResult Result;
    if (Request.Phase == EFoundationPhase::Cancel || Request.Phase == EFoundationPhase::Release)
    {
        if (Request.Action == EFoundationAction::Attack) bBreaking = false;
        if (Request.Action == EFoundationAction::Use) bUsing = false;
        Result = {true, Request.Phase == EFoundationPhase::Cancel ? TEXT("cancelled") : TEXT("released")};
    }
    else
        switch (Request.Action)
        {
        case EFoundationAction::Attack:
            if (bUsing)
                Result.Reason = TEXT("attack rejected: active item use");
            else if (!Request.Target.bInRange)
            {
                bBreaking = false;
                Result.Reason = TEXT("attack rejected: no visible target in reach");
            }
            else if (Request.Target.Kind == EFoundationTargetKind::Entity && Request.Phase == EFoundationPhase::Hold)
                Result.Reason = TEXT("entity attack requires press");
            else
            {
                bBreaking = Request.Target.Kind == EFoundationTargetKind::Block;
                ActiveTarget = Request.Target.Name;
                Result = {true,
                    bBreaking ? TEXT("break fixture active (no mining)") : TEXT("entity attack fixture (no damage)")};
            }
            break;
        case EFoundationAction::Use:
            if (bBreaking)
                Result.Reason = TEXT("use rejected: block break active");
            else if (Request.Target.bInRange && Request.Target.Kind == EFoundationTargetKind::Block &&
                Request.Target.bRejectInteraction && !(Request.bCrouch && (Slots[Slot] > 0 || Offhand > 0)))
                Result.Reason = TEXT("block interaction failed; item/offhand fallback rejected");
            else if (Request.Target.bInRange && Request.Target.bInteractive &&
                !(Request.Target.Kind == EFoundationTargetKind::Block && Request.bCrouch &&
                    (Slots[Slot] > 0 || Offhand > 0)))
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
            else if (!Request.Target.bInRange)
                Result.Reason = TEXT("pick rejected: no target in reach");
            else
            {
                Select(Request.Target.Kind == EFoundationTargetKind::Block ? 0 : 1);
                Result = {true, TEXT("picked matching diagnostic token slot")};
            }
            break;
        case EFoundationAction::Drop:
            if (Slots[Slot] <= 0)
                Result.Reason = TEXT("drop rejected: empty fixture slot");
            else
            {
                Slots[Slot] = Request.bControl ? 0 : Slots[Slot] - 1;
                Result = {true, Request.bControl ? TEXT("dropped fixture stack") : TEXT("dropped one fixture token")};
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
