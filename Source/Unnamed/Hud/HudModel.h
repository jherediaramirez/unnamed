#pragma once

#include "CoreMinimal.h"

enum class EHudMode : uint8
{
    Survival,
    Adventure,
    Creative,
    Spectator
};
enum class EHudHeart : uint8
{
    Normal,
    Poisoned,
    Withered,
    Frozen,
    Absorbing,
    Container
};
enum class EHudAir : uint8
{
    None,
    Full,
    Bursting,
    Empty
};
enum class EHudAttack : uint8
{
    Off,
    Crosshair,
    Hotbar
};
enum class EHudContext : uint8
{
    Empty,
    Experience,
    Locator,
    MountJump
};
enum class EHudDataSource : uint8
{
    Unsupported,
    Preview,
    Authoritative
};
// Original geometric item art; Token is the test double's placeholder.
enum class EHudItemIcon : uint8
{
    Token,
    Block,
    Pickaxe,
    Sword
};

// How one slot's item draws: icon and tint, durability left (negative: no bar), unlimited supply marker.
struct FHudItemView
{
    EHudItemIcon Icon = EHudItemIcon::Token;
    FLinearColor Tint = FLinearColor::White;
    double Durability = -1;
    bool bInfinite = false;
    // The block a block item places, drawn as its 3D item model; animated textures repaint every tick.
    FName Block;
    bool bAnimatedBlock = false;
    bool operator==(const FHudItemView& Other) const = default;
};

// One coherent adapter snapshot. No field implies a gameplay backend exists.
struct FHudSnapshot
{
    EHudDataSource Resources = EHudDataSource::Unsupported;
    EHudDataSource Items = EHudDataSource::Unsupported;
    EHudMode Mode = EHudMode::Survival;
    double Health = 20, MaxHealth = 20, Absorption = 0;
    int32 Armor = 0, Food = 20, Air = 300, MaxAir = 300, Level = 0;
    double Saturation = 5, Experience = 0, AttackStrength = 1, AttackDelay = 6;
    bool bUnderwater = false, bHardcore = false, bPoison = false, bWither = false, bFrozen = false;
    bool bHunger = false, bRegeneration = false, bDamageCooldown = false;
    bool bLivingTarget = false, bTargetAlive = false, bTargetInRange = false;
    bool bSpectatorTarget = false, bDebugCrosshair3D = false;
    bool bFirstPerson = true, bVisible = true, bMainHandLeft = false;
    int32 SelectedSlot = 0, OffhandCount = 0;
    TArray<int32> Counts;
    // Parallel to Counts when items are real; empty keeps the token placeholder.
    TArray<FHudItemView> ItemViews;
    FHudItemView OffhandView;
    FString ItemName;
    double NotificationTime = 1;
    EHudAttack AttackIndicator = EHudAttack::Crosshair;

    bool CanHurt() const { return Mode == EHudMode::Survival || Mode == EHudMode::Adventure; }
    bool HasExperience() const { return CanHurt(); }
    bool operator==(const FHudSnapshot& Other) const = default;
};

struct FHudHealthGeometry
{
    double DisplayMax = 20;
    int32 Rows = 1, RowHeight = 11, NormalContainers = 10, AbsorptionContainers = 0;
};

struct FHudLayout
{
    FIntPoint Size;
    int32 Center = 0;
    FIntRect Hotbar, Selection, Offhand, Crosshair, Context;
    FIntPoint OffhandItem, AttackCrosshair, AttackHotbar;
    FIntPoint GetItem(int32 Slot) const;
    FIntPoint GetHeart(int32 Cell, int32 RowHeight) const;
    int32 GetArmorY(const FHudHealthGeometry& Health) const;
    int32 GetAirY(int32 MountRows = 0) const;
};

struct FHudPresentation
{
    bool bInitialized = false;
    int32 LastHealth = 20, DisplayedHealth = 20;
    int64 BlinkUntil = 0, ItemNameUntil = 0;
    double LastHealthTime = 0;
    FString LastItemName;

    void Update(const FHudSnapshot& Snapshot, int64 Tick, double Seconds);
    bool IsBlinking(int64 Tick) const;
    float GetNameAlpha(int64 Tick) const;
};

namespace HudModel
{
    UNNAMED_API int32 GetScale(FIntPoint Framebuffer, int32 Requested);
    UNNAMED_API FIntPoint GetLogicalSize(FIntPoint Framebuffer, int32 Scale);
    UNNAMED_API FHudLayout GetLayout(FIntPoint LogicalSize, int32 Slot, bool bMainHandLeft);
    UNNAMED_API FHudHealthGeometry GetHealthGeometry(
        double MaxHealth, int32 OldHealth, double Health, double Absorption);
    UNNAMED_API EHudHeart GetHeartType(const FHudSnapshot& Snapshot);
    UNNAMED_API FName GetHeartSprite(EHudHeart Type, bool bHardcore, bool bHalf, bool bBlink);
    UNNAMED_API EHudAir GetAirCell(int32 Air, int32 MaxAir, bool bUnderwater, int32 Cell);
    UNNAMED_API int32 GetPairFill(int32 Points, int32 Cell);
    UNNAMED_API int32 GetExperienceWidth(double Progress);
    UNNAMED_API int32 GetAttackWidth(double Strength);
    UNNAMED_API int32 GetAttackHeight(double Strength);
    UNNAMED_API EHudContext GetContext(
        bool bWaypoints, bool bExperience, bool bRecentXp, bool bJumpable, bool bCharging, bool bCooldown);
    UNNAMED_API bool IsCrosshairVisible(const FHudSnapshot& Snapshot);
    UNNAMED_API bool IsReadyMarkerVisible(const FHudSnapshot& Snapshot);
    // Stable per HUD tick, independent of render cadence. Not Java's exact RNG stream.
    UNNAMED_API int32 GetJitter(int64 Tick, int32 Cell, int32 Range);
}
