#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationLab.h"
#include "FoundationPlayground.generated.h"

class UFoundationInteractionView;

// The Playground (/Game/Maps/L_Playground), the one world every feature is tested in: the 2 km dev-grid
// ground (all of it buildable), the interaction stations around spawn (Dummy Block stations, damageable
// training targets, in-world lab controls) and the movement course 60 m east, played with the
// Foundation's pawn, controller and HUD. Every solid is an interaction static. It owns the session-only
// block world and the one gameplay authority; edits last until a lab reset or the end of the session.
// Nothing is saved or networked.
UCLASS()
class UNNAMED_API AFoundationPlayground : public AFoundationLab
{
    GENERATED_BODY()
public:
    AFoundationPlayground();
    virtual void BuildLayout() override;

    // The movement course's local origin: east of the stations, clear of their build area.
    static inline const FVector PlaygroundCourseOrigin{6000, 0, 0};
    // The block gallery (Docs/Blocks.md): the showcase rows south-east of spawn (clear of the stations, the probe
    // areas and the course), then, west of the stations, every block's default state in registry order and every
    // state in state-id order (as Minecraft's debug world).
    static inline const FIntVector ShowcaseOrigin{16, 14, 0};
    static inline const FIntVector RegistryGalleryOrigin{-70, -22, 0};
    static inline const FIntVector StateGalleryOrigin{-200, -58, 0};
    static constexpr int32 RegistryGalleryColumns = 24;
    static constexpr int32 StateGalleryColumns = 59;
    // The showcase row of model blocks (plants), the last row.
    static constexpr int32 PlantRow = 7;
    // Where a showcase row's first block sits; rows run east, three cells apart.
    static FIntVector GetShowcaseCell(int32 Row, int32 Column)
    {
        return ShowcaseOrigin + FIntVector(Column * 2, Row * 3, 0);
    }
    // A registry or state gallery cell. Both galleries keep the blocks before survival B1 where they always were;
    // with foundation.Blocks.Families 1 each adds a tail from a fresh row: one cell per B1 family block, its default
    // state.
    struct FGalleryCell
    {
        FIntVector Cell;
        FFoundationBlockState State;
        bool bTail = false;
    };
    static void GetGalleryCells(
        const FFoundationBlockRegistry& Registry, TArray<FGalleryCell>& OutRegistry, TArray<FGalleryCell>& OutStates);
    // Block world height in cells above the ground.
    static constexpr int32 BuildHeightCells = 64;
    virtual FFoundationTarget TraceTarget(
        FVector Eye, FVector Direction, const UFoundationProfile& Profile) const override;
    virtual void GatherCollision(const FBox& Region, TArray<FBox>& Out) const override;
    virtual void ApplyLogicalOffset(const FVector& Offset) override;
    virtual FVector GetSafeSpawn(const UFoundationProfile& Profile) const override;
    virtual FFoundationAuthority* GetAuthority() override { return &Authority; }

    const FFoundationAuthority& GetAuthorityState() const { return Authority; }
    UFoundationInteractionView* GetView() const { return View; }

    // Explicit lab reset: a new session (pending and old commands end), the seed blocks, fresh loadouts
    // and revived targets. The player returns to spawn.
    void ResetLab();
    // The finite verification fixture (64 blocks, tool wear) or the default infinite lab; resets the lab.
    void SetFiniteSupply(bool bFinite);
    bool IsFiniteSupply() const { return !Authority.Options.bInfiniteSupply; }
    void SetPlacementPreset(FName Preset);
    FName GetPlacementPreset() const { return Authority.Options.PlacementPreset; }

protected:
    virtual void AddSolid(
        const FString& Label, FVector Minimum, FVector Maximum, FLinearColor Color, bool bCaption) override;

private:
    void AddStatic(const FString& Label, FVector Min, FVector Max, FLinearColor Color, bool bAnchor,
        FName Interaction = NAME_None);
    void AddTarget(int32 Id, const FString& Label, FVector Min, FVector Max);
    void AddSign(const FString& Text, FVector Location, double Yaw);
    // Seeds the block gallery (restored by every reset like the starter blocks).
    void AddBlockGallery();
    void HandleInteraction(FName Interaction);

    FFoundationAuthority Authority;
    UPROPERTY(Transient)
    TObjectPtr<UFoundationInteractionView> View;
};
