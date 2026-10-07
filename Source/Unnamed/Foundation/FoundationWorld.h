#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameModeBase.h"
#include "FoundationSimulation.h"
#include "FoundationWorld.generated.h"

class UBoxComponent;
class UCameraComponent;
class UStaticMesh;
class UMaterialInterface;

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

struct FFoundationPrimitive
{
    FBox Box;
    FString Label;
    EFoundationTargetKind Kind;
    bool bInteractive = false;
};
UCLASS()

class UNNAMED_API AFoundationLab : public AActor
{
    GENERATED_BODY()
public:
    AFoundationLab();
    virtual void BeginPlay() override;
    TArray<FBox> CollisionBoxes;
    TArray<FFoundationPrimitive> Primitives;
    FFoundationTarget Target(FVector Eye, FVector Direction, const UFoundationProfile& Profile) const;

private:
    void Add(FString Label, FVector Minimum, FVector Maximum, EFoundationTargetKind Kind = EFoundationTargetKind::Block,
        bool Interactive = false);
    UPROPERTY()
    TObjectPtr<UStaticMesh> Cube;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> Material;
};
UCLASS()

class UNNAMED_API AFoundationPawn : public APawn
{
    GENERATED_BODY()
public:
    AFoundationPawn();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;
    FFoundationSimulation Simulation;
    FFoundationIntent LastIntent;
    FFoundationEligibility Eligibility;
    FVector EyePosition() const;
    FFoundationTarget Target() const;
    void SetSimulationEnabled(bool Enabled);

    void SetPerspective(int32 Value) { Perspective = Value; }

    int32 GetPerspective() const { return Perspective; }

    UCameraComponent* GetCamera() const { return Camera; }

    AFoundationLab* GetLab() const { return Lab.Get(); }

private:
    UPROPERTY()
    TObjectPtr<UBoxComponent> Collision;
    UPROPERTY()
    TObjectPtr<UCameraComponent> Camera;
    TWeakObjectPtr<AFoundationLab> Lab;
    bool bSimulate = true;
    bool bProbeCreated = false;
    int32 Perspective = 0;
};
UCLASS()

class UNNAMED_API AFoundationGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AFoundationGameMode();
};
