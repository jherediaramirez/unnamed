#include "Hud/HudDrawList.h"

namespace
{
    const FHudGlyph* FindGlyph(const TArray<FHudGlyph>& Glyphs, TCHAR Character)
    {
        const int32 Code = Character >= 32 && Character <= 126 ? int32(Character) : int32('?');
        return Glyphs.FindByPredicate([Code](const FHudGlyph& Glyph) { return Glyph.Code == Code; });
    }

    struct FBuilder
    {
        TArray<FHudDraw> Draws;
        const TArray<FHudGlyph>& Glyphs;
        int64 Tick = 0;

        void Sprite(
            FName Name, FIntPoint Point, FIntPoint Size = {9, 9}, FBox2f UV = FBox2f(FVector2f(0), FVector2f(1)))
        {
            if (Size.X > 0 && Size.Y > 0) Draws.Add({Name, FIntRect(Point, Point + Size), UV});
        }
        void Hud(const TCHAR* Name, FIntPoint Point, FIntPoint Size = {9, 9})
        {
            Sprite(FName(FString(TEXT("gui/sprites/hud/")) + Name), Point, Size);
        }
        void Rect(FIntPoint Point, FIntPoint Size, FLinearColor Color)
        {
            Draws.Add({NAME_None, FIntRect(Point, Point + Size), FBox2f(FVector2f(0), FVector2f(1)), Color});
        }
        void Text(const FString& Text, FIntPoint Point, FLinearColor Color, bool bShadow = true)
        {
            if (bShadow)
            {
                // Java quarters the encoded RGB bytes, then truncates. Quartering linear
                // Slate tint instead makes a white glyph's shadow 137 rather than 63.
                const FColor Encoded = Color.ToFColorSRGB();
                FLinearColor Shadow(FColor(Encoded.R / 4, Encoded.G / 4, Encoded.B / 4));
                Shadow.A = Color.A;
                this->Text(Text, Point + FIntPoint(1, 1), Shadow, false);
            }
            for (TCHAR Character : Text)
                if (const FHudGlyph* Glyph = FindGlyph(Glyphs, Character))
                {
                    if (Glyph->InkWidth > 0)
                        Draws.Add({TEXT("font/ascii"), FIntRect(Point, Point + FIntPoint(8, 8)),
                            FBox2f(
                                FVector2f(Glyph->Origin) / 128.f, FVector2f(Glyph->Origin + FIntPoint(8, 8)) / 128.f),
                            Color});
                    Point.X += Glyph->Advance;
                }
        }
        void Pixels(FIntPoint Point, std::initializer_list<FIntPoint> Cells, FIntPoint Size, FLinearColor Color)
        {
            for (const FIntPoint Cell : Cells)
                Rect(Point + Cell, Size, Color);
        }
        void Item(FIntPoint Point, int32 Count, const FHudItemView* View = nullptr)
        {
            if (Count <= 0) return;
            const FLinearColor Tint = View ? View->Tint : FLinearColor::White;
            const FLinearColor Ink(FColor(30, 30, 34));
            switch (View ? View->Icon : EHudItemIcon::Token)
            {
            case EHudItemIcon::Block:
                if (!View->Block.IsNone())
                {
                    FHudDraw Model{NAME_None, FIntRect(Point, Point + FIntPoint(16, 16))};
                    Model.Block = View->Block;
                    Model.Tick = View->bAnimatedBlock ? Tick : 0;
                    Draws.Add(Model);
                    break;
                }
                // Original matte cube with a crash-test target on its face.
                Rect(Point + FIntPoint(2, 2), {12, 13}, Ink);
                Rect(Point + FIntPoint(3, 3), {10, 3}, Tint);
                Rect(Point + FIntPoint(3, 6), {10, 8}, Tint * FLinearColor(.78f, .78f, .8f));
                Rect(Point + FIntPoint(12, 4), {1, 10}, Tint * FLinearColor(.58f, .58f, .62f));
                Pixels(Point, {{6, 8}, {8, 10}}, {2, 2}, Ink);
                Pixels(Point, {{8, 8}, {6, 10}}, {2, 2}, FLinearColor(FColor(240, 200, 40)));
                break;
            case EHudItemIcon::Pickaxe:
                Pixels(Point, {{9, 4}, {8, 5}, {7, 6}, {6, 7}, {5, 8}, {4, 9}, {3, 10}, {2, 11}}, {2, 2},
                    FLinearColor(FColor(120, 82, 44)));
                Rect(Point + FIntPoint(5, 2), {7, 2}, Tint);
                Pixels(Point, {{4, 3}, {11, 3}}, {2, 2}, Tint);
                Rect(Point + FIntPoint(12, 5), {2, 3}, Tint * FLinearColor(.75f, .75f, .78f));
                Rect(Point + FIntPoint(3, 5), {2, 2}, Tint * FLinearColor(.75f, .75f, .78f));
                break;
            case EHudItemIcon::Sword:
                Pixels(Point, {{12, 2}, {11, 3}, {10, 4}, {9, 5}, {8, 6}, {7, 7}}, {2, 2}, Tint);
                Pixels(Point, {{13, 2}, {12, 3}, {11, 4}, {10, 5}}, {1, 1}, FLinearColor::White);
                Pixels(Point, {{4, 7}, {5, 8}, {8, 11}, {9, 12}}, {2, 2}, FLinearColor(FColor(70, 70, 76)));
                Pixels(Point, {{6, 9}, {5, 10}}, {2, 2}, FLinearColor(FColor(120, 82, 44)));
                Rect(Point + FIntPoint(3, 12), {2, 2}, FLinearColor(FColor(70, 70, 76)));
                break;
            default:
                // Original geometric token: a cyan outlined tile with a white notch. No Minecraft item identity.
                Rect(Point + FIntPoint(3, 2), {10, 12}, FLinearColor(FColor(15, 44, 57)));
                Rect(Point + FIntPoint(4, 3), {8, 10}, FLinearColor(FColor(74, 190, 218)));
                Rect(Point + FIntPoint(5, 4), {6, 2}, FLinearColor(FColor(192, 244, 251)));
                Rect(Point + FIntPoint(8, 8), {2, 3}, FLinearColor(FColor(34, 107, 132)));
                break;
            }
            if (View && View->Durability >= 0 && View->Durability < 1)
            {
                // The reference bar: 13 px, green to red by what is left, black behind.
                const double Left = FMath::Clamp(View->Durability, 0., 1.);
                Rect(Point + FIntPoint(2, 13), {13, 2}, FLinearColor::Black);
                Rect(Point + FIntPoint(2, 13), {FMath::RoundToInt32(13 * Left), 1},
                    FLinearColor::MakeFromHSV8(uint8(Left / 3. * 255.), 255, 255));
            }
            if (View && View->bInfinite)
            {
                // Unlimited lab supply: a small infinity sign where a count would be.
                for (const FIntPoint Offset : {FIntPoint(1, 1), FIntPoint(0, 0)})
                    Pixels(Point + Offset,
                        {{9, 11}, {10, 11}, {12, 11}, {13, 11}, {8, 12}, {11, 12}, {14, 12}, {9, 13}, {10, 13},
                            {12, 13}, {13, 13}},
                        {1, 1}, Offset.X ? FLinearColor(FColor(63, 63, 63)) : FLinearColor::White);
            }
            else if (Count != 1)
            {
                const FString Number = FString::FromInt(Count);
                Text(Number, {Point.X + 17 - HudDrawList::MeasureText(Number, Glyphs), Point.Y + 9},
                    FLinearColor::White);
            }
        }
        void Crop(const TCHAR* Name, FIntPoint Point, FIntPoint FullSize, int32 Fill, bool bVertical = false)
        {
            if (Fill <= 0) return;
            const FName Id(FString(TEXT("gui/sprites/hud/")) + Name);
            if (bVertical)
                Sprite(Id, Point + FIntPoint(0, FullSize.Y - Fill), {FullSize.X, Fill},
                    FBox2f(FVector2f(0, 1.f - float(Fill) / FullSize.Y), FVector2f(1)));
            else
                Sprite(Id, Point, {Fill, FullSize.Y}, FBox2f(FVector2f(0), FVector2f(float(Fill) / FullSize.X, 1)));
        }
    };
}

int32 HudDrawList::MeasureText(const FString& Text, const TArray<FHudGlyph>& Glyphs)
{
    int32 Width = 0;
    for (TCHAR Character : Text)
        if (const FHudGlyph* Glyph = FindGlyph(Glyphs, Character)) Width += Glyph->Advance;
    return Width;
}

TArray<FHudDraw> HudDrawList::Build(
    const FHudSnapshot& S, const FHudPresentation& P, const FHudLayout& L, int64 Tick, const TArray<FHudGlyph>& Glyphs)
{
    FBuilder B{{}, Glyphs, Tick};
    if (!S.bVisible) return B.Draws;
    if (HudModel::IsCrosshairVisible(S))
    {
        B.Hud(TEXT("crosshair"), L.Crosshair.Min, L.Crosshair.Size());
        if (S.AttackIndicator == EHudAttack::Crosshair)
        {
            if (HudModel::IsReadyMarkerVisible(S))
                B.Hud(TEXT("crosshair_attack_indicator_full"), L.AttackCrosshair, {16, 16});
            else if (S.AttackStrength < 1)
            {
                B.Hud(TEXT("crosshair_attack_indicator_background"), L.AttackCrosshair, {16, 4});
                B.Crop(TEXT("crosshair_attack_indicator_progress"), L.AttackCrosshair, {16, 4},
                    HudModel::GetAttackWidth(S.AttackStrength));
            }
        }
    }
    if (S.Mode != EHudMode::Spectator && S.Items != EHudDataSource::Unsupported)
    {
        B.Hud(TEXT("hotbar"), L.Hotbar.Min, L.Hotbar.Size());
        B.Hud(TEXT("hotbar_selection"), L.Selection.Min, L.Selection.Size());
        if (S.OffhandCount > 0)
            B.Hud(S.bMainHandLeft ? TEXT("hotbar_offhand_right") : TEXT("hotbar_offhand_left"), L.Offhand.Min,
                L.Offhand.Size());
        for (int32 Slot = 0; Slot < 9; ++Slot)
            B.Item(L.GetItem(Slot), S.Counts.IsValidIndex(Slot) ? S.Counts[Slot] : 0,
                S.ItemViews.IsValidIndex(Slot) ? &S.ItemViews[Slot] : nullptr);
        B.Item(L.OffhandItem, S.OffhandCount, S.ItemViews.IsEmpty() ? nullptr : &S.OffhandView);
        if (S.AttackIndicator == EHudAttack::Hotbar && S.AttackStrength < 1)
        {
            B.Hud(TEXT("hotbar_attack_indicator_background"), L.AttackHotbar, {18, 18});
            B.Crop(TEXT("hotbar_attack_indicator_progress"), L.AttackHotbar, {18, 18},
                HudModel::GetAttackHeight(S.AttackStrength), true);
        }
    }
    if (S.CanHurt() && S.Resources != EHudDataSource::Unsupported)
    {
        const int32 Current = FMath::CeilToInt(S.Health), Absorption = FMath::CeilToInt(S.Absorption);
        const FHudHealthGeometry Health =
            HudModel::GetHealthGeometry(S.MaxHealth, P.DisplayedHealth, S.Health, S.Absorption);
        const EHudHeart Type = HudModel::GetHeartType(S);
        const bool bBlink = P.IsBlinking(Tick);
        // Armor is behind hearts, including an odd maximum whose separate container
        // rounding can spill into a row not counted by the armor-row formula.
        if (S.Armor > 0)
            for (int32 Cell = 0; Cell < 10; ++Cell)
            {
                const int32 Armor = HudModel::GetPairFill(S.Armor, Cell);
                B.Hud(Armor == 2     ? TEXT("armor_full")
                        : Armor == 1 ? TEXT("armor_half")
                                     : TEXT("armor_empty"),
                    {L.Center - 91 + Cell * 8, L.GetArmorY(Health)});
            }
        for (int32 Cell = Health.NormalContainers + Health.AbsorptionContainers - 1; Cell >= 0; --Cell)
        {
            FIntPoint Point = L.GetHeart(Cell, Health.RowHeight);
            if (Current + Absorption <= 4) Point.Y += HudModel::GetJitter(Tick, Cell, 2);
            if (S.bRegeneration && Cell < Health.NormalContainers &&
                Cell == Tick % FMath::Max(1, FMath::CeilToInt(Health.DisplayMax + 5)))
                Point.Y -= 2;
            B.Sprite(HudModel::GetHeartSprite(EHudHeart::Container, S.bHardcore, false, bBlink), Point);
            const int32 AbsFill = HudModel::GetPairFill(Absorption, Cell - Health.NormalContainers);
            if (Cell >= Health.NormalContainers && AbsFill)
                B.Sprite(HudModel::GetHeartSprite(Type == EHudHeart::Withered ? Type : EHudHeart::Absorbing,
                             S.bHardcore, AbsFill == 1, false),
                    Point);
            const int32 OldFill = HudModel::GetPairFill(P.DisplayedHealth, Cell);
            if (bBlink && OldFill) B.Sprite(HudModel::GetHeartSprite(Type, S.bHardcore, OldFill == 1, true), Point);
            const int32 Fill = HudModel::GetPairFill(Current, Cell);
            if (Fill) B.Sprite(HudModel::GetHeartSprite(Type, S.bHardcore, Fill == 1, false), Point);
        }
        for (int32 Cell = 0; Cell < 10; ++Cell)
        {
            FIntPoint FoodPoint(L.Center + 82 - Cell * 8, L.Size.Y - 39);
            if (S.Saturation <= 0 && Tick % (FMath::Clamp(S.Food, 0, 20) * 3 + 1) == 0)
                FoodPoint.Y += HudModel::GetJitter(Tick, 100 + Cell, 3) - 1;
            B.Hud(S.bHunger ? TEXT("food_empty_hunger") : TEXT("food_empty"), FoodPoint);
            const int32 Food = HudModel::GetPairFill(S.Food, Cell);
            if (Food)
                B.Hud(S.bHunger ? (Food == 2 ? TEXT("food_full_hunger") : TEXT("food_half_hunger"))
                                : (Food == 2 ? TEXT("food_full") : TEXT("food_half")),
                    FoodPoint);
            const EHudAir Air = HudModel::GetAirCell(S.Air, S.MaxAir, S.bUnderwater, Cell + 1);
            if (Air != EHudAir::None)
            {
                const int32 Wobble = S.Air <= 0 && Tick % 2 == 0 ? HudModel::GetJitter(Tick, 200 + Cell, 2) : 0;
                B.Hud(Air == EHudAir::Full         ? TEXT("air")
                        : Air == EHudAir::Bursting ? TEXT("air_bursting")
                                                   : TEXT("air_empty"),
                    {L.Center + 82 - Cell * 8, L.GetAirY() + Wobble});
            }
        }
        if (S.HasExperience())
        {
            B.Hud(TEXT("experience_bar_background"), L.Context.Min, L.Context.Size());
            B.Crop(TEXT("experience_bar_progress"), L.Context.Min, L.Context.Size(),
                HudModel::GetExperienceWidth(S.Experience));
            if (S.Level > 0)
            {
                const FString Level = FString::FromInt(S.Level);
                const FIntPoint Point((L.Size.X - MeasureText(Level, Glyphs)) / 2, L.Size.Y - 35);
                for (const FIntPoint Offset : {FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(0, 1)})
                    B.Text(Level, Point + Offset, FLinearColor::Black, false);
                B.Text(Level, Point, FLinearColor(FColor(128, 255, 32)), false);
            }
        }
    }
    if (S.Mode != EHudMode::Spectator && !S.ItemName.IsEmpty() && P.GetNameAlpha(Tick) > 0)
        B.Text(S.ItemName, {(L.Size.X - MeasureText(S.ItemName, Glyphs)) / 2, L.Size.Y - (S.CanHurt() ? 59 : 45)},
            FLinearColor(1, 1, 1, P.GetNameAlpha(Tick)));
    // Fixture data is always labelled: the whole cluster, or only the token hotbar beside real vitals.
    if (S.Resources == EHudDataSource::Preview)
        B.Text(TEXT("PREVIEW"), {4, L.Size.Y - 12}, FLinearColor(FColor(255, 223, 138)));
    else if (S.Items == EHudDataSource::Preview)
        B.Text(TEXT("PREVIEW ITEMS"), {4, L.Size.Y - 12}, FLinearColor(FColor(255, 223, 138)));
    return MoveTemp(B.Draws);
}
