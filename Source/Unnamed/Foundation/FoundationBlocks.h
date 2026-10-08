#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "FoundationBlocks.generated.h"

// A block this project adds itself, authored as data (Project Settings > Game > Unnamed Interaction). The
// registry appends it after the imported 26.3 blocks with one full-cube state that draws in its flat Color.
// Imported blocks come from the generated registry asset, never from here.
USTRUCT()
struct FFoundationBlockDefinition
{
    GENERATED_BODY()

    // Stable saved ID. Never rename once shipped, and never reuse a retired one (Docs/Blocks.md).
    UPROPERTY(Config, EditAnywhere, Category = "Block")
    FName Id;

    UPROPERTY(Config, EditAnywhere, Category = "Block")
    FString DisplayName;

    // Java-style hardness: negative never breaks, zero breaks at once, positive accumulates progress.
    UPROPERTY(Config, EditAnywhere, Category = "Block")
    float Hardness = 1.5f;

    UPROPERTY(Config, EditAnywhere, Category = "Block")
    bool bReplaceable = false;

    // Classification and efficient-tool tags, e.g. Block.Mineable.Pickaxe.
    UPROPERTY(Config, EditAnywhere, Category = "Block")
    FGameplayTagContainer Tags;

    UPROPERTY(Config, EditAnywhere, Category = "Block")
    bool bRequiresCorrectToolForDrops = false;

    // Item returned straight to the inventory in finite lab mode (a stand-in for dropped item entities).
    UPROPERTY(Config, EditAnywhere, Category = "Block")
    FName DropItem;

    UPROPERTY(Config, EditAnywhere, Category = "Block")
    int32 DropCount = 1;

    // Survival rule hook. A full cube needs nothing and keeps floating when its neighbors go.
    UPROPERTY(Config, EditAnywhere, Category = "Block")
    bool bNeedsSupportBelow = false;

    UPROPERTY(Config, EditAnywhere, Category = "Block")
    FLinearColor Color = FLinearColor::White;
};

// A cell's state and the world revision at which it last changed (0 = never changed).
struct FFoundationCell
{
    FFoundationBlockState State;
    int64 Revision = 0;
};

// One committed change, kept for presentation and for a later save or replication backend.
struct FFoundationBlockChange
{
    FIntVector Cell = FIntVector::ZeroValue;
    FFoundationBlockState Before, After;
    int64 Revision = 0;
};

// Cell storage behind the block world. The Playground uses bounded sparse 16^3 sections; the chunk loader will
// implement this interface (paletted sections of state ids) without changing queries, commits or the
// interaction loop.
class IFoundationBlockStorage
{
public:
    virtual ~IFoundationBlockStorage() = default;
    virtual FFoundationCell Get(const FIntVector& Cell) const = 0;
    virtual void Set(const FIntVector& Cell, const FFoundationCell& Value) = 0;
    virtual void Empty() = 0;
    virtual int32 GetSolidCount() const = 0;
    // Visits every non-air cell inside the inclusive range.
    virtual void ForEachSolid(const FIntVector& Min, const FIntVector& Max,
        TFunctionRef<void(const FIntVector&, const FFoundationCell&)> Visit) const = 0;
    virtual SIZE_T GetAllocatedSize() const = 0;
};

// 16x16x16 sections allocated on first write and kept (their revisions matter) until Empty.
class UNNAMED_API FFoundationSectionStorage final : public IFoundationBlockStorage
{
public:
    virtual FFoundationCell Get(const FIntVector& Cell) const override;
    virtual void Set(const FIntVector& Cell, const FFoundationCell& Value) override;
    virtual void Empty() override;
    virtual int32 GetSolidCount() const override { return Solid; }
    virtual void ForEachSolid(const FIntVector& Min, const FIntVector& Max,
        TFunctionRef<void(const FIntVector&, const FFoundationCell&)> Visit) const override;
    virtual SIZE_T GetAllocatedSize() const override;
    int32 GetSectionCount() const { return Sections.Num(); }

private:
    struct FSection
    {
        TArray<FFoundationCell> Cells;
        int32 Solid = 0;
    };
    static int32 IndexInSection(const FIntVector& Cell);
    TMap<FIntVector, FSection> Sections;
    int32 Solid = 0;
};

// The mutable block grid: the shared registry, build bounds, revisions and the change log. Pure data; the
// authority is its only writer. Cells outside the bounds are unknown (never placeable).
struct UNNAMED_API FFoundationBlockWorld
{
    FFoundationBlockWorld();
    FFoundationBlockWorld(const FFoundationBlockWorld&) = delete;
    FFoundationBlockWorld& operator=(const FFoundationBlockWorld&) = delete;

    // World position of cell (0, 0, 0)'s minimum corner. World rebasing moves it with everything else.
    FVector OriginCm = FVector::ZeroVector;
    // Inclusive build bounds.
    FIntVector BoundsMin = FIntVector(-16, -16, 0), BoundsMax = FIntVector(15, 15, 15);
    // Increments on every committed change; cells remember the revision that last changed them.
    int64 Revision = 0;

    // Replaces the block vocabulary. Existing cells keep their state ids.
    void SetRegistry(TSharedRef<const FFoundationBlockRegistry> NewRegistry) { Registry = NewRegistry; }
    const FFoundationBlockRegistry& GetRegistry() const { return *Registry; }
    TSharedRef<const FFoundationBlockRegistry> GetRegistryRef() const { return Registry; }
    const FFoundationBlockType* FindBlock(FName Id) const { return Registry->FindBlock(Id); }
    const FFoundationBlockType& GetBlock(FFoundationBlockState State) const { return Registry->GetBlock(State); }

    bool IsKnown(const FIntVector& Cell) const;
    FFoundationCell Get(const FIntVector& Cell) const;
    // The only mutation: sets the state, stamps the next world revision and records the change.
    int64 Set(const FIntVector& Cell, FFoundationBlockState State);
    // Empties every cell (a lab reset). The world revision keeps counting.
    void Clear();

    // World boxes of a state's shapes in a cell; false when it has none (air, water).
    bool GetSelectionBox(const FIntVector& Cell, FFoundationBlockState State, FBox& OutBox) const;
    bool GetCollisionBox(const FIntVector& Cell, FFoundationBlockState State, FBox& OutBox) const;
    // Appends the collision boxes of solid cells overlapping Region (world cm).
    void GatherCollision(const FBox& Region, TArray<FBox>& Out) const;

    int32 GetSolidCount() const;
    SIZE_T GetAllocatedSize() const;
    const IFoundationBlockStorage& GetStorage() const { return *Storage; }
    // Changes since the last call, oldest first. The log is bounded: when nobody drains it past
    // MaxPendingChanges it is dropped and bChangesOverflowed asks the reader to rebuild from storage.
    TArray<FFoundationBlockChange> TakeChanges();
    int32 GetPendingChangeCount() const { return Changes.Num(); }
    static constexpr int32 MaxPendingChanges = 65536;
    bool bChangesOverflowed = false;

private:
    void Record(const FFoundationBlockChange& Change);
    TSharedRef<const FFoundationBlockRegistry> Registry;
    TUniquePtr<IFoundationBlockStorage> Storage;
    TArray<FFoundationBlockChange> Changes;
};
