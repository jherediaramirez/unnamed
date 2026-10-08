#include "Hud/HudModel.h"

int32 HudModel::GetScale(FIntPoint Framebuffer, int32 Requested)
{
    int32 Scale = 1;
    while ((Requested <= 0 || Scale < Requested) && Framebuffer.X / (Scale + 1) >= 320 &&
        Framebuffer.Y / (Scale + 1) >= 240)
        ++Scale;
    return Scale;
}

FIntPoint HudModel::GetLogicalSize(FIntPoint Framebuffer, int32 Scale)
{
    Scale = FMath::Max(1, Scale);
    return {FMath::DivideAndRoundUp(Framebuffer.X, Scale), FMath::DivideAndRoundUp(Framebuffer.Y, Scale)};
}

FHudLayout HudModel::GetLayout(FIntPoint Size, int32 Slot, bool bMainHandLeft)
{
    FHudLayout Out;
    Out.Size = Size;
    Out.Center = Size.X / 2;
    auto Rect = [](int32 X, int32 Y, int32 W, int32 H) { return FIntRect(X, Y, X + W, Y + H); };
    const int32 C = Out.Center, H = Size.Y;
    Out.Hotbar = Rect(C - 91, H - 22, 182, 22);
    Out.Selection = Rect(C - 92 + 20 * FMath::Clamp(Slot, 0, 8), H - 23, 24, 23);
    Out.Offhand = Rect(bMainHandLeft ? C + 91 : C - 120, H - 23, 29, 24);
    Out.OffhandItem = {bMainHandLeft ? C + 101 : C - 117, H - 19};
    Out.Crosshair = Rect((Size.X - 15) / 2, (H - 15) / 2, 15, 15);
    Out.Context = Rect((Size.X - 182) / 2, H - 29, 182, 5);
    Out.AttackCrosshair = {C - 8, H / 2 + 9};
    Out.AttackHotbar = {bMainHandLeft ? C - 113 : C + 97, H - 20};
    return Out;
}

FIntPoint FHudLayout::GetItem(int32 Slot) const
{
    return {Center - 88 + 20 * Slot, Size.Y - 19};
}
FIntPoint FHudLayout::GetHeart(int32 Cell, int32 RowHeight) const
{
    return {Center - 91 + 8 * (Cell % 10), Size.Y - 39 - Cell / 10 * RowHeight};
}
int32 FHudLayout::GetArmorY(const FHudHealthGeometry& Health) const
{
    return Size.Y - 49 - (Health.Rows - 1) * Health.RowHeight;
}
int32 FHudLayout::GetAirY(int32 MountRows) const
{
    return Size.Y - 49 - 10 * FMath::Max(0, MountRows - 1);
}

FHudHealthGeometry HudModel::GetHealthGeometry(double MaxHealth, int32 OldHealth, double Health, double Absorption)
{
    FHudHealthGeometry Out;
    Out.DisplayMax = FMath::Max3(MaxHealth, double(OldHealth), double(FMath::CeilToInt(Health)));
    const int32 Absorbed = FMath::CeilToInt(FMath::Max(0., Absorption));
    Out.Rows = FMath::Max(1, FMath::CeilToInt((Out.DisplayMax + Absorbed) / 20.));
    Out.RowHeight = FMath::Max(12 - Out.Rows, 3);
    Out.NormalContainers = FMath::CeilToInt(Out.DisplayMax / 2.);
    Out.AbsorptionContainers = FMath::DivideAndRoundUp(Absorbed, 2);
    return Out;
}

EHudHeart HudModel::GetHeartType(const FHudSnapshot& Snapshot)
{
    if (Snapshot.bPoison) return EHudHeart::Poisoned;
    if (Snapshot.bWither) return EHudHeart::Withered;
    if (Snapshot.bFrozen) return EHudHeart::Frozen;
    return EHudHeart::Normal;
}

FName HudModel::GetHeartSprite(EHudHeart Type, bool bHardcore, bool bHalf, bool bBlink)
{
    FString Name = TEXT("gui/sprites/hud/heart/");
    switch (Type)
    {
    case EHudHeart::Poisoned:
        Name += TEXT("poisoned_");
        break;
    case EHudHeart::Withered:
        Name += TEXT("withered_");
        break;
    case EHudHeart::Frozen:
        Name += TEXT("frozen_");
        break;
    case EHudHeart::Absorbing:
        Name += TEXT("absorbing_");
        break;
    case EHudHeart::Container:
        Name += TEXT("container");
        break;
    default:
        break;
    }
    if (Type == EHudHeart::Container)
    {
        if (bHardcore) Name += TEXT("_hardcore");
    }
    else
    {
        if (bHardcore) Name += TEXT("hardcore_");
        Name += bHalf ? TEXT("half") : TEXT("full");
    }
    if (bBlink) Name += TEXT("_blinking");
    return FName(Name);
}

EHudAir HudModel::GetAirCell(int32 Air, int32 MaxAir, bool bUnderwater, int32 Cell)
{
    if (MaxAir <= 0 || Cell < 1 || Cell > 10) return EHudAir::None;
    const int32 A = FMath::Clamp(Air, 0, MaxAir);
    if (!bUnderwater && A == MaxAir) return EHudAir::None;
    const int32 Full = FMath::CeilToInt((A - 2) * 10. / MaxAir);
    const int32 Pop = FMath::CeilToInt(A * 10. / MaxAir);
    const int32 EmptyStart = FMath::CeilToInt((A + (A != 0 && bUnderwater ? 1 : 0)) * 10. / MaxAir);
    if (Cell <= Full) return EHudAir::Full;
    if (bUnderwater && Full != Pop && Cell == Pop) return EHudAir::Bursting;
    return Cell > EmptyStart ? EHudAir::Empty : EHudAir::None;
}

int32 HudModel::GetPairFill(int32 Points, int32 Cell)
{
    return FMath::Clamp(Points - Cell * 2, 0, 2);
}
int32 HudModel::GetExperienceWidth(double Progress)
{
    return FMath::Min(182, FMath::FloorToInt(183 * FMath::Clamp(Progress, 0., 1.)));
}
int32 HudModel::GetAttackWidth(double Strength)
{
    return FMath::Min(16, FMath::FloorToInt(17 * FMath::Clamp(Strength, 0., 1.)));
}
int32 HudModel::GetAttackHeight(double Strength)
{
    return FMath::Min(18, FMath::FloorToInt(19 * FMath::Clamp(Strength, 0., 1.)));
}

EHudContext HudModel::GetContext(
    bool bWaypoints, bool bExperience, bool bRecentXp, bool bJumpable, bool bCharging, bool bCooldown)
{
    if (bJumpable && (!bWaypoints || bCharging || bCooldown)) return EHudContext::MountJump;
    if (bExperience && (!bWaypoints || bRecentXp)) return EHudContext::Experience;
    return bWaypoints ? EHudContext::Locator : EHudContext::Empty;
}

bool HudModel::IsCrosshairVisible(const FHudSnapshot& Snapshot)
{
    return Snapshot.bVisible && Snapshot.bFirstPerson && !Snapshot.bDebugCrosshair3D &&
        (Snapshot.Mode != EHudMode::Spectator || Snapshot.bSpectatorTarget);
}
bool HudModel::IsReadyMarkerVisible(const FHudSnapshot& Snapshot)
{
    return Snapshot.AttackStrength >= 1 && Snapshot.bLivingTarget && Snapshot.bTargetAlive && Snapshot.bTargetInRange &&
        Snapshot.AttackDelay > 5;
}
int32 HudModel::GetJitter(int64 Tick, int32 Cell, int32 Range)
{
    uint32 Value = uint32(Tick) * 312871u + uint32(Cell) * 374761393u;
    Value = (Value ^ (Value >> 13)) * 1274126177u;
    return int32((Value ^ (Value >> 16)) % uint32(FMath::Max(1, Range)));
}

void FHudPresentation::Update(const FHudSnapshot& Snapshot, int64 Tick, double Seconds)
{
    const int32 Health = FMath::CeilToInt(Snapshot.Health);
    if (!bInitialized)
    {
        LastHealth = DisplayedHealth = Health;
        LastHealthTime = Seconds;
        bInitialized = true;
    }
    if (Health != LastHealth && Snapshot.bDamageCooldown)
    {
        BlinkUntil = Tick + (Health < LastHealth ? 20 : 10);
        LastHealthTime = Seconds;
    }
    if (Seconds - LastHealthTime > 1.)
    {
        DisplayedHealth = Health;
        LastHealthTime = Seconds;
    }
    LastHealth = Health;
    if (LastItemName != Snapshot.ItemName)
    {
        LastItemName = Snapshot.ItemName;
        ItemNameUntil = Snapshot.ItemName.IsEmpty() ? Tick : Tick + FMath::FloorToInt64(40 * Snapshot.NotificationTime);
    }
}
bool FHudPresentation::IsBlinking(int64 Tick) const
{
    return BlinkUntil > Tick && ((BlinkUntil - Tick) / 3) % 2 == 1;
}
float FHudPresentation::GetNameAlpha(int64 Tick) const
{
    return FMath::Clamp(float(ItemNameUntil - Tick) / 10.f, 0.f, 1.f);
}
