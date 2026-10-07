#pragma once

#include "CoreMinimal.h"
#include "FoundationActions.generated.h"

UENUM()
enum class EFoundationTargetKind : uint8
{
    None,
    Block,
    Entity
};
UENUM()
enum class EFoundationAction : uint8
{
    Attack,
    Use,
    Pick,
    Drop,
    Swap,
    Inventory
};
UENUM()
enum class EFoundationPhase : uint8
{
    Press,
    Hold,
    Release,
    Cancel
};
enum class EFoundationHand : uint8
{
    Main,
    Off
};

struct FFoundationTarget
{
    EFoundationTargetKind Kind = EFoundationTargetKind::None;
    FString Name = TEXT("none");
    FVector Hit = {}, Normal = {};
    double DistanceCm = 0, ReachCm = 0;
    bool bInRange = false, bInteractive = false, bRejectInteraction = false;
};

struct FFoundationActionRequest
{
    EFoundationAction Action = EFoundationAction::Attack;
    EFoundationPhase Phase = EFoundationPhase::Press;
    FFoundationTarget Target;
    bool bSneak = false, bControl = false;
};

struct FFoundationActionResult
{
    bool bAccepted = false;
    FString Reason;
    EFoundationHand Hand = EFoundationHand::Main;
};

// Typed test double. Tokens only; no inventory, damage, loot or block mutation.
struct UNNAMED_API FFoundationActions
{
    int32 Slot = 0, Offhand = 2;
    TArray<int32> Slots = {64, 16, 0, 0, 0, 0, 0, 0, 0};
    bool bBreaking = false, bUsing = false;
    FString ActiveTarget, LastResult = TEXT("no action"), LastHand = TEXT("main");
    FFoundationActionResult Dispatch(const FFoundationActionRequest& Request);
    void Select(int32 NewSlot);
    void Scroll(int32 Notches);
    void Cancel();
};
