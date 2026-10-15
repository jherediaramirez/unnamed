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
        // Scale: vanilla's pose scale (the death screen's title draws at 2), glyphs, advances and shadow offset alike.
        void Text(const FString& Text, FIntPoint Point, FLinearColor Color, bool bShadow = true, int32 Scale = 1)
        {
            if (bShadow)
            {
                // Java quarters the encoded RGB bytes, then truncates. Quartering linear
                // Slate tint instead makes a white glyph's shadow 137 rather than 63.
                const FColor Encoded = Color.ToFColorSRGB();
                FLinearColor Shadow(FColor(Encoded.R / 4, Encoded.G / 4, Encoded.B / 4));
                Shadow.A = Color.A;
                this->Text(Text, Point + FIntPoint(Scale, Scale), Shadow, false, Scale);
            }
            for (TCHAR Character : Text)
                if (const FHudGlyph* Glyph = FindGlyph(Glyphs, Character))
                {
                    if (Glyph->InkWidth > 0)
                        Draws.Add({TEXT("font/ascii"), FIntRect(Point, Point + FIntPoint(8, 8) * Scale),
                            FBox2f(
                                FVector2f(Glyph->Origin) / 128.f, FVector2f(Glyph->Origin + FIntPoint(8, 8)) / 128.f),
                            Color});
                    Point.X += Glyph->Advance * Scale;
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
            case EHudItemIcon::Sprite:
                // The item's own 16 x 16 GUI sprite (ItemRoot/Gui), untinted.
                Sprite(View->Sprite, Point, {16, 16});
                break;
            default:
                // Original geometric token: a cyan outlined tile with a white notch. No Minecraft item identity.
                Rect(Point + FIntPoint(3, 2), {10, 12}, FLinearColor(FColor(15, 44, 57)));
                Rect(Point + FIntPoint(4, 3), {8, 10}, FLinearColor(FColor(74, 190, 218)));
                Rect(Point + FIntPoint(5, 4), {6, 2}, FLinearColor(FColor(192, 244, 251)));
                Rect(Point + FIntPoint(8, 8), {2, 3}, FLinearColor(FColor(34, 107, 132)));
                break;
            }
            if (const FHudDurabilityBar Bar =
                    HudModel::GetDurabilityBar(View ? View->Damage : 0, View ? View->MaxDamage : 0);
                Bar.bVisible)
            {
                Rect(Point + FIntPoint(2, 13), {13, 2}, FLinearColor::Black);
                if (Bar.Width > 0) Rect(Point + FIntPoint(2, 13), {Bar.Width, 1}, FLinearColor(Bar.Color));
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

namespace
{
    // One message's rows: characters with their colours; a row breaks after the last space that fits (the space
    // that breaks is dropped), or mid-word when none does. An empty message keeps one empty row.
    void WrapLine(const FHudChatLine& Line, const TArray<FHudGlyph>& Glyphs, TArray<TArray<FHudChatRun>>& Rows)
    {
        TArray<TPair<TCHAR, FLinearColor>> Characters;
        for (const FHudChatRun& Run : Line.Runs)
            for (TCHAR Character : Run.Text)
                Characters.Add({Character, Run.Color});
        int32 Start = 0;
        do
        {
            int32 End = Start, Width = 0, LastSpace = INDEX_NONE;
            while (End < Characters.Num())
            {
                const FHudGlyph* Glyph = FindGlyph(Glyphs, Characters[End].Key);
                const int32 Advance = Glyph ? Glyph->Advance : 0;
                if (Width + Advance > HudDrawList::ChatWidth && End > Start) break;
                if (Characters[End].Key == TEXT(' ')) LastSpace = End;
                Width += Advance;
                ++End;
            }
            if (End < Characters.Num() && LastSpace > Start) End = LastSpace + 1;
            TArray<FHudChatRun>& Row = Rows.AddDefaulted_GetRef();
            for (int32 Index = Start; Index < End; ++Index)
            {
                if (Row.IsEmpty() || Row.Last().Color != Characters[Index].Value)
                    Row.Add({FString(), Characters[Index].Value});
                Row.Last().Text.AppendChar(Characters[Index].Key);
            }
            Start = End;
            if (Start > 0 && Start < Characters.Num() && Characters[Start].Key == TEXT(' ')) ++Start;
        } while (Start < Characters.Num());
    }
}

TArray<TArray<FHudChatRun>> HudDrawList::WrapChat(const TArray<FHudChatLine>& Lines, const TArray<FHudGlyph>& Glyphs)
{
    TArray<TArray<FHudChatRun>> Rows;
    for (const FHudChatLine& Line : Lines)
        WrapLine(Line, Glyphs, Rows);
    return Rows;
}

FIntRect HudDrawList::GetDeathButton(FIntPoint LogicalSize, EHudDeathButton Button)
{
    if (Button == EHudDeathButton::None) return FIntRect();
    const FIntPoint Min(LogicalSize.X / 2 - 100, LogicalSize.Y / 4 + (Button == EHudDeathButton::Respawn ? 72 : 96));
    return FIntRect(Min, Min + FIntPoint(200, 20));
}

EHudDeathButton HudDrawList::HitDeathButton(FIntPoint LogicalSize, FIntPoint Point)
{
    for (const EHudDeathButton Button : {EHudDeathButton::Respawn, EHudDeathButton::TitleScreen})
    {
        const FIntRect Rect = GetDeathButton(LogicalSize, Button);
        if (Point.X >= Rect.Min.X && Point.Y >= Rect.Min.Y && Point.X < Rect.Max.X && Point.Y < Rect.Max.Y)
            return Button;
    }
    return EHudDeathButton::None;
}

namespace
{
    // The death screen (step 11a) over the HUD, in GUI pixels of Size [survival.r6.death_layout,
    // survival.r6.death_screen]; strings are en_us.json's.
    void DeathScreen(FBuilder& B, const FHudSnapshot& S, FIntPoint Size)
    {
        // fillGradient: a constant vertical gradient, top 0x60500000 to bottom 0xA0803030 (ARGB), no fade-in. The draw
        // list has one colour per draw, so it is 1 GUI px bands, each the vertex colours interpolated in their
        // encoded bytes at the band's centre.
        const FColor Top(0x50, 0x00, 0x00, 0x60), Bottom(0x80, 0x30, 0x30, 0xA0);
        for (int32 Y = 0; Y < Size.Y; ++Y)
        {
            const float Alpha = (Y + .5f) / Size.Y;
            auto Mix = [Alpha](uint8 From, uint8 To)
            { return uint8(FMath::RoundToInt(FMath::Lerp(float(From), float(To), Alpha))); };
            B.Rect({0, Y}, {Size.X, 1},
                FLinearColor(
                    FColor(Mix(Top.R, Bottom.R), Mix(Top.G, Bottom.G), Mix(Top.B, Bottom.B), Mix(Top.A, Bottom.A))));
        }
        // Vanilla buttons: the widget sprite (200 x 20, so its nine-slice draws unstretched), highlighted under the
        // cursor while enabled, disabled before tick 20; the label centred, white or 0xA0A0A0 when disabled, 6 px down
        // (provisional: AbstractButton's centred string, (height - 9) / 2 + 1).
        for (const EHudDeathButton Button : {EHudDeathButton::Respawn, EHudDeathButton::TitleScreen})
        {
            const FIntRect Rect = HudDrawList::GetDeathButton(Size, Button);
            B.Sprite(FName(!S.bDeathButtonsEnabled    ? TEXT("gui/sprites/widget/button_disabled")
                             : S.DeathHover == Button ? TEXT("gui/sprites/widget/button_highlighted")
                                                      : TEXT("gui/sprites/widget/button")),
                Rect.Min, Rect.Size());
            // deathScreen.respawn, deathScreen.titleScreen.
            const FString Label = Button == EHudDeathButton::Respawn ? TEXT("Respawn") : TEXT("Title Screen");
            B.Text(Label,
                {Rect.Min.X + Rect.Width() / 2 - HudDrawList::MeasureText(Label, B.Glyphs) / 2, Rect.Min.Y + 6},
                S.bDeathButtonsEnabled ? FLinearColor::White : FLinearColor(FColor(0xA0, 0xA0, 0xA0)));
        }
        // deathScreen.title at scale 2, centred on width / 2 with its top at y 60 (vanilla: width / 2 / 2, 30 in the
        // scaled pose). Hardcore's "Game Over!" (deathScreen.title.hardcore) and its Spectate World button are a known
        // issue: there is no hardcore world yet.
        const FString Title = TEXT("You Died!");
        B.Text(Title, {2 * (Size.X / 2 / 2 - HudDrawList::MeasureText(Title, B.Glyphs) / 2), 60}, FLinearColor::White,
            true, 2);
        // The cause (the death message) centred at y 85; the score at y 100: deathScreen.score.value "Score: %s" with
        // the number in yellow (ChatFormatting.YELLOW 0xFFFF55).
        if (!S.DeathMessage.IsEmpty())
            B.Text(S.DeathMessage, {Size.X / 2 - HudDrawList::MeasureText(S.DeathMessage, B.Glyphs) / 2, 85},
                FLinearColor::White);
        const FString Label = TEXT("Score: "), Score = FString::FromInt(S.DeathScore);
        const int32 Left = Size.X / 2 - HudDrawList::MeasureText(Label + Score, B.Glyphs) / 2;
        B.Text(Label, {Left, 100}, FLinearColor::White);
        B.Text(Score, {Left + HudDrawList::MeasureText(Label, B.Glyphs), 100}, FLinearColor(FColor(0xFF, 0xFF, 0x55)));
    }
}

TArray<FHudDraw> HudDrawList::Build(
    const FHudSnapshot& S, const FHudPresentation& P, const FHudLayout& L, int64 Tick, const TArray<FHudGlyph>& Glyphs)
{
    FBuilder B{{}, Glyphs, Tick};
    if (!S.bVisible) return B.Draws;
    // F1 hides the HUD; the open chat and the death screen (screens) still draw.
    const bool bHud = !S.bHideHud;
    // The red damage tint, first so everything draws over it: overlay texel 0xB2FF0000 while hurtTime > 0 or dead
    // [survival.r6.hud_rules]. PROVISIONAL: the final framebuffer alpha was not measured (SF 10088). It is vanilla's
    // world/entity render effect, not HUD, so F1 does not hide it.
    if (S.bDamageTint) B.Rect({0, 0}, L.Size, FLinearColor(FColor(0xFF, 0x00, 0x00, 0xB2)));
    // The first-person fire overlay (PROVISIONAL placement, no measurement): two copies of the current fire_1 frame,
    // each half the screen width square, bottom aligned, shifted outward by 0.24 of their size, alpha 0.9, the right
    // one mirrored. Like the tint it is a render effect, so F1 keeps it.
    if (S.bOnFire && S.bFirstPerson)
    {
        const int32 Side = L.Size.X / 2, Shift = FMath::RoundToInt(.24f * Side);
        const float V0 = float(S.FireFrame % HudDrawList::FireFrames) / HudDrawList::FireFrames,
                    V1 = float(S.FireFrame % HudDrawList::FireFrames + 1) / HudDrawList::FireFrames;
        const FLinearColor Tint(1, 1, 1, .9f);
        B.Draws.Add({TEXT("block/fire_1"), FIntRect({-Shift, L.Size.Y - Side}, {Side - Shift, L.Size.Y}),
            FBox2f(FVector2f(0, V0), FVector2f(1, V1)), Tint});
        B.Draws.Add(
            {TEXT("block/fire_1"), FIntRect({L.Size.X - Side + Shift, L.Size.Y - Side}, {L.Size.X + Shift, L.Size.Y}),
                FBox2f(FVector2f(1, V0), FVector2f(0, V1)), Tint});
    }
    // The death screen hides the crosshair; the rest of the HUD stays under it.
    if (bHud && !S.bDeathScreen && HudModel::IsCrosshairVisible(S))
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
    if (bHud && S.Mode != EHudMode::Spectator && S.Items != EHudDataSource::Unsupported)
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
    if (bHud && S.CanHurt() && S.Resources != EHudDataSource::Unsupported)
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
    if (bHud && S.Mode != EHudMode::Spectator && !S.ItemName.IsEmpty() && P.GetNameAlpha(Tick) > 0)
        B.Text(S.ItemName, {(L.Size.X - MeasureText(S.ItemName, Glyphs)) / 2, L.Size.Y - (S.CanHurt() ? 59 : 45)},
            FLinearColor(1, 1, 1, P.GetNameAlpha(Tick)));
    // The chat (step 15), bottom-left: the newest rows that fit, each on a black band at half its opacity.
    TArray<TArray<FHudChatRun>> Rows;
    TArray<float> RowAlpha;
    for (const FHudChatLine& Line : S.Chat)
    {
        WrapLine(Line, Glyphs, Rows);
        while (RowAlpha.Num() < Rows.Num())
            RowAlpha.Add(Line.Alpha);
    }
    const int32 Shown = bHud || S.bChatOpen
        ? FMath::Min(Rows.Num(), S.bChatOpen ? HudDrawList::ChatOpenRows : HudDrawList::ChatClosedRows)
        : 0;
    for (int32 Index = 0; Index < Shown; ++Index)
    {
        const int32 Row = Rows.Num() - 1 - Index;
        const int32 Bottom = L.Size.Y - HudDrawList::ChatBottomOffset - Index * HudDrawList::ChatLineHeight;
        B.Rect({0, Bottom - HudDrawList::ChatLineHeight}, {HudDrawList::ChatWidth + 8, HudDrawList::ChatLineHeight},
            FLinearColor(0, 0, 0, .5f * RowAlpha[Row]));
        FIntPoint Point(4, Bottom - 8);
        for (const FHudChatRun& Run : Rows[Row])
        {
            B.Text(Run.Text, Point, FLinearColor(Run.Color.R, Run.Color.G, Run.Color.B, RowAlpha[Row]));
            Point.X += MeasureText(Run.Text, Glyphs);
        }
    }
    // The open chat's input bar (minecraft.wiki, provisional): 2 px from the sides, 14 px from the bottom, 12 px tall,
    // half-opaque black; the typed line at (4, height - 12) with vanilla's "_" caret at its end.
    if (S.bChatOpen)
    {
        B.Rect({2, L.Size.Y - 14}, {L.Size.X - 4, 12}, FLinearColor(0, 0, 0, .5f));
        // A line longer than the bar shows its end (vanilla scrolls with the cursor): characters drop from the left
        // until the line and, at the end, its "_" fit; a caret further left keeps itself in view.
        const int32 Caret = S.CaretIndex < 0 ? S.ChatInput.Len() : FMath::Min(S.CaretIndex, S.ChatInput.Len());
        const bool bAtEnd = Caret == S.ChatInput.Len();
        const FString Tail = bAtEnd ? TEXT("_") : TEXT("");
        int32 Cut = 0;
        while (Cut < Caret && MeasureText(S.ChatInput.Mid(Cut) + Tail, Glyphs) > L.Size.X - 8)
            ++Cut;
        FString Typed = S.ChatInput.Mid(Cut);
        while (Typed.Len() > 1 && !bAtEnd && MeasureText(Typed, Glyphs) > L.Size.X - 8)
            Typed.LeftChopInline(1);
        // The blink [survival.r4.caret]: a "_" after the text at the end, otherwise a 1 px bar, line height, at the
        // caret.
        if (bAtEnd && S.bCaretVisible) Typed += TEXT("_");
        if (!bAtEnd && S.bCaretVisible)
            B.Rect({4 + MeasureText(S.ChatInput.Mid(Cut, Caret - Cut), Glyphs), L.Size.Y - 13}, {1, 10},
                FLinearColor(FColor(0xE0, 0xE0, 0xE0)));
        B.Text(Typed, {4, L.Size.Y - 12}, FLinearColor(FColor(0xE0, 0xE0, 0xE0)));
    }
    // Fixture data is always labelled: the whole cluster, or only the token hotbar beside real vitals.
    // The open chat's input bar takes their place.
    if (bHud && !S.bChatOpen)
    {
        if (S.Resources == EHudDataSource::Preview)
            B.Text(TEXT("PREVIEW"), {4, L.Size.Y - 12}, FLinearColor(FColor(255, 223, 138)));
        else if (S.Items == EHudDataSource::Preview)
            B.Text(TEXT("PREVIEW ITEMS"), {4, L.Size.Y - 12}, FLinearColor(FColor(255, 223, 138)));
    }
    if (S.bDeathScreen) DeathScreen(B, S, L.Size);
    return MoveTemp(B.Draws);
}
