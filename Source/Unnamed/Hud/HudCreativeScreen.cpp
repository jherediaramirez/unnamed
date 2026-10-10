#include "Hud/HudCreativeScreen.h"
#include "Engine/Texture2D.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/HudAssets.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "UnnamedProjectSettings.h"

namespace
{
    const TCHAR* CreativeGui = TEXT("gui/container/creative_inventory/");
    const TCHAR* CreativeSprites = TEXT("gui/sprites/container/creative_inventory/");
    const FLinearColor TitleColor = FLinearColor(FColor(0x40, 0x40, 0x40));

    FSlateBrush MakeBrush(UTexture2D* Texture)
    {
        FSlateBrush Brush;
        Brush.SetResourceObject(Texture);
        Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
        Brush.DrawAs = ESlateBrushDrawType::Image;
        return Brush;
    }

    // Item sprites for items that are not imported blocks: the tab icons and this lab's practice tools.
    FString ItemSprite(FName Item)
    {
        if (Item == TEXT("practice_pickaxe")) return TEXT("item/diamond_pickaxe");
        if (Item == TEXT("practice_sword")) return TEXT("item/netherite_sword");
        if (Item == TEXT("compass")) return TEXT("item/compass_16");
        return TEXT("item/") + Item.ToString();
    }
}

bool HudBlockIcon::Paint(FSlateWindowElementList& Elements, int32& LayerId, const FGeometry& Geometry,
    const FSlateBrush& Atlas, const FFoundationBlockRegistry& Registry, FName Block, FVector2f Origin, float Units,
    int64 Tick)
{
    const FFoundationBlockType* Type = Registry.FindBlock(Block);
    const UFoundationBlockAssets* Assets = Registry.GetAssets();
    const UTexture2D* AtlasTexture = Assets ? Assets->IconAtlas.Get() : nullptr;
    if (!Type || !AtlasTexture || !Atlas.GetResourceObject()) return false;
    const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Atlas);
    const FVector2f AtlasSize(AtlasTexture->GetSizeX(), AtlasTexture->GetSizeY());
    const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
    const TArray<SlateIndex> Indices = {0, 1, 2, 0, 2, 3};
    for (const FQuad& Quad : Build(Registry, Type->Id))
    {
        int32 Layer = Quad.Tile, Next;
        float Blend;
        if (Layer == INDEX_NONE)
        {
            if (!Assets->Textures.IsValidIndex(Quad.Texture)) continue;
            FoundationBlockFaces::GetFrame(Assets->Textures[Quad.Texture], Tick, Layer, Next, Blend);
        }
        const FVector2f Tile(float(Layer % Assets->IconAtlasColumns), float(Layer / Assets->IconAtlasColumns));
        TArray<FSlateVertex> Vertices;
        for (int32 Corner = 0; Corner < 4; ++Corner)
            Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,
                (Origin + Quad.Corners[Corner]) * Units, (Tile + Quad.UVs[Corner]) * 16.f / AtlasSize, Quad.Color));
        FSlateDrawElement::MakeCustomVerts(Elements, ++LayerId, Handle, Vertices, Indices, nullptr, 0, 0);
    }
    return true;
}

// Draws in GUI pixels relative to the viewport's top-left (the screen's own corner is Frame.Origin).
struct SHudCreativeScreen::FPainter
{
    const SHudCreativeScreen& Screen;
    const FGeometry& Geometry;
    FSlateWindowElementList& Elements;
    int32& LayerId;
    float Units;

    void Box(const FSlateBrush* Brush, FIntPoint Position, FIntPoint Size,
        FBox2f UV = FBox2f(FVector2f(0), FVector2f(1)), FLinearColor Color = FLinearColor::White) const
    {
        if (!Brush) return;
        FSlateBrush Copy = *Brush;
        Copy.SetUVRegion(FBox2D(FVector2D(UV.Min), FVector2D(UV.Max)));
        FSlateDrawElement::MakeBox(Elements, ++LayerId,
            Geometry.ToPaintGeometry(FVector2f(Size) * Units, FSlateLayoutTransform(FVector2f(Position) * Units)),
            &Copy, ESlateDrawEffect::None, Color);
    }

    // A nine-slice GUI sprite (tooltip background and frame): Border texels of a 100 x 100 sprite.
    void NineSlice(const FSlateBrush* Brush, FIntPoint Position, FIntPoint Size, float Border) const
    {
        if (!Brush) return;
        FSlateBrush Copy = *Brush;
        Copy.DrawAs = ESlateBrushDrawType::Box;
        Copy.Margin = FMargin(Border / 100.f);
        Copy.ImageSize = FVector2D(100 * Units);
        FSlateDrawElement::MakeBox(Elements, ++LayerId,
            Geometry.ToPaintGeometry(FVector2f(Size) * Units, FSlateLayoutTransform(FVector2f(Position) * Units)),
            &Copy);
    }

    int32 Measure(const FString& Text) const
    {
        int32 Width = 0;
        if (const UHudAssets* Assets = Screen.HudAssets.Get())
            for (const TCHAR Character : Text)
                if (const FHudGlyph* Glyph = Assets->Glyphs.FindByPredicate(
                        [Character](const FHudGlyph& G) { return G.Code == int32(Character); }))
                    Width += Glyph->Advance;
        return Width;
    }

    // The default font's ASCII page, as the HUD draws it; the shadow is a quarter-bright copy one pixel down-right.
    void Text(const FString& Text, FIntPoint Point, FLinearColor Color, bool bShadow) const
    {
        const UHudAssets* Assets = Screen.HudAssets.Get();
        const FSlateBrush* Font = Screen.FindBrush(TEXT("font/ascii"));
        if (!Assets || !Font) return;
        if (bShadow)
            this->Text(Text, Point + FIntPoint(1, 1),
                FLinearColor(Color.R * .25f, Color.G * .25f, Color.B * .25f, Color.A), false);
        for (const TCHAR Character : Text)
            if (const FHudGlyph* Glyph = Assets->Glyphs.FindByPredicate(
                    [Character](const FHudGlyph& G) { return G.Code == int32(Character); }))
            {
                if (Glyph->InkWidth > 0)
                    Box(Font, Point, {8, 8},
                        FBox2f(FVector2f(Glyph->Origin) / 128.f, FVector2f(Glyph->Origin + FIntPoint(8, 8)) / 128.f),
                        Color);
                Point.X += Glyph->Advance;
            }
    }

    // An item in a 16 x 16 slot area: a block as its 3D model, anything else as its flat sprite.
    void Item(FName Item, FIntPoint Position) const
    {
        if (!Screen.Registry ||
            !HudBlockIcon::Paint(Elements, LayerId, Geometry, Screen.Atlas, *Screen.Registry, Item, FVector2f(Position),
                Units, Screen.Tick))
            Box(Screen.FindBrush(ItemSprite(Item)), Position, {16, 16});
    }

    void Stack(const FHudCreativeStack& Stack, FIntPoint Position) const
    {
        if (Stack.IsEmpty()) return;
        Item(Stack.Item, Position);
        if (Stack.Count > 1)
        {
            // ItemDecorations: the count at the slot's bottom right, white with a shadow.
            const FString Count = FString::FromInt(Stack.Count);
            Text(Count, Position + FIntPoint(17 - Measure(Count), 9), FLinearColor::White, true);
        }
    }
};

void SHudCreativeScreen::Construct(const FArguments& Arguments)
{
    Registry = Arguments._Registry;
    HudAssets = Arguments._HudAssets;
    Items = Arguments._Items;
    GetInventory = Arguments._GetInventory;
    OnWrite = Arguments._OnWrite;
    OnClose = Arguments._OnClose;
    Solid = *FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    FocusTime = FPlatformTime::Seconds();
    const UFoundationBlockAssets* Assets = Registry ? Registry->GetAssets() : nullptr;
    if (Assets) Model.Build(Assets->CreativeTabs, Arguments._Items);
    if (Assets && Assets->IconAtlas) Atlas = MakeBrush(Assets->IconAtlas);
    // The screen's art (ImportBlocks.py, BlockRoot/Gui) and the HUD's font and tooltip sprites.
    TArray<FString> Ids = {FString(CreativeGui) + TEXT("tab_items"), FString(CreativeGui) + TEXT("tab_item_search"),
        FString(CreativeGui) + TEXT("tab_inventory"), FString(CreativeSprites) + TEXT("scroller"),
        FString(CreativeSprites) + TEXT("scroller_disabled"), TEXT("gui/sprites/container/slot_highlight_back"),
        TEXT("gui/sprites/container/slot_highlight_front")};
    for (const TCHAR* Row : {TEXT("top"), TEXT("bottom")})
        for (const TCHAR* State : {TEXT("selected"), TEXT("unselected")})
            for (int32 Column = 1; Column <= 7; ++Column)
                Ids.Add(FString::Printf(TEXT("%stab_%s_%s_%d"), CreativeSprites, Row, State, Column));
    for (const FHudCreativeModel::FTab& Tab : Model.GetTabs())
        Ids.Add(ItemSprite(Tab.Icon));
    Ids.Add(ItemSprite(TEXT("practice_pickaxe")));
    Ids.Add(ItemSprite(TEXT("practice_sword")));
    for (const FString& Id : Ids)
        if (UTexture2D* Texture =
                Cast<UTexture2D>(UUnnamedProjectSettings::GetBlockAssetPath(TEXT("Gui/") + Id).TryLoad()))
            Brushes.Add(Id, MakeBrush(Texture));
    if (const UHudAssets* Hud = HudAssets.Get())
        for (const FHudSpriteAsset& Sprite : Hud->Sprites)
        {
            const FString Id = Sprite.Id.ToString();
            if (Id == TEXT("font/ascii") || Id.StartsWith(TEXT("gui/sprites/tooltip/")))
                if (UTexture2D* Texture = Sprite.Texture.LoadSynchronous()) Brushes.Add(Id, MakeBrush(Texture));
        }
}

const FSlateBrush* SHudCreativeScreen::FindBrush(const FString& Id) const
{
    return Brushes.Find(Id);
}

FString SHudCreativeScreen::ItemName(FName Item) const
{
    const FHudCreativeItem* Info = Items.Find(Item);
    return Info && !Info->Name.IsEmpty() ? Info->Name : Item.ToString();
}

int32 SHudCreativeScreen::GetGuiScale(FVector2f ViewportPixels)
{
    // Minecraft's automatic GUI scale: the largest that keeps at least 320 x 240 GUI pixels.
    int32 Scale = 1;
    while (ViewportPixels.X / (Scale + 1) >= 320 && ViewportPixels.Y / (Scale + 1) >= 240)
        ++Scale;
    return Scale;
}

SHudCreativeScreen::FFrame SHudCreativeScreen::GetFrame(const FGeometry& Geometry) const
{
    FFrame Frame;
    const float SlateScale = FMath::Max(Geometry.GetAccumulatedLayoutTransform().GetScale(), .01f);
    const FVector2f Pixels = FVector2f(Geometry.GetLocalSize()) * SlateScale;
    Frame.Scale = GetGuiScale(Pixels);
    Frame.Units = float(Frame.Scale) / SlateScale;
    // leftPos/topPos: centred in the scaled screen with integer division.
    Frame.Origin = FIntPoint((int32(Pixels.X) / Frame.Scale - FHudCreativeModel::Width) / 2,
        (int32(Pixels.Y) / Frame.Scale - FHudCreativeModel::Height) / 2);
    return Frame;
}

FIntPoint SHudCreativeScreen::ToGui(const FGeometry& Geometry, const FPointerEvent& Event) const
{
    const FFrame Frame = GetFrame(Geometry);
    const FVector2f Local = FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())) / Frame.Units;
    return FIntPoint(FMath::FloorToInt32(Local.X), FMath::FloorToInt32(Local.Y)) - Frame.Origin;
}

void SHudCreativeScreen::Write(const TArray<FHudCreativeWrite>& Writes)
{
    if (!OnWrite) return;
    for (const FHudCreativeWrite& Write : Writes)
        OnWrite(Write);
}

FReply SHudCreativeScreen::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    Mouse = ToGui(Geometry, Event);
    const FHudCreativeHit Hit = Model.HitTest(Mouse);
    const FKey Button = Event.GetEffectingButton();
    const int32 Index = Button == EKeys::LeftMouseButton ? 0
        : Button == EKeys::RightMouseButton              ? 1
        : Button == EKeys::MiddleMouseButton             ? 2
                                                         : -1;
    if (Index < 0) return FReply::Unhandled();
    if (Hit.Kind == EHudCreativeHit::Tab)
        Model.SelectTab(Hit.Index);
    else if (Hit.Kind == EHudCreativeHit::Scroller && Model.CanScroll())
    {
        bDraggingScroller = true;
        Model.SetScroll((Mouse.Y - FHudCreativeModel::ScrollerY - 7.5f) / (FHudCreativeModel::ScrollerHeight - 15.f));
    }
    else
        Write(
            Model.Click(Hit, Index, Event.IsShiftDown(), GetInventory ? GetInventory() : TArray<FHudCreativeStack>()));
    return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SHudCreativeScreen::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
    bDraggingScroller = false;
    return FReply::Handled().ReleaseMouseCapture();
}

FReply SHudCreativeScreen::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
    Mouse = ToGui(Geometry, Event);
    if (bDraggingScroller)
        Model.SetScroll((Mouse.Y - FHudCreativeModel::ScrollerY - 7.5f) / (FHudCreativeModel::ScrollerHeight - 15.f));
    return FReply::Handled();
}

FReply SHudCreativeScreen::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
    Model.ScrollRows(Event.GetWheelDelta() > 0 ? -1 : 1);
    return FReply::Handled();
}

FReply SHudCreativeScreen::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    const FKey Key = Event.GetKey();
    if (Key == EKeys::Escape || (Key == EKeys::E && !Model.IsSearch()))
    {
        if (OnClose) OnClose();
        return FReply::Handled();
    }
    if (Model.IsSearch())
    {
        if (Key == EKeys::BackSpace) Model.SetSearch(Model.GetSearch().LeftChop(1));
        return FReply::Handled();
    }
    // The chat key jumps to the search tab, as vanilla's keyPressed does.
    if (Key == EKeys::T)
    {
        const int32 Search = Model.GetTabs().IndexOfByPredicate(
            [](const FHudCreativeModel::FTab& Tab) { return Tab.Type == TEXT("SEARCH"); });
        Model.SelectTab(Search);
        FocusTime = FPlatformTime::Seconds();
        return FReply::Handled();
    }
    static const FKey Numbers[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six,
        EKeys::Seven, EKeys::Eight, EKeys::Nine};
    for (int32 Number = 0; Number < 9; ++Number)
        if (Key == Numbers[Number])
        {
            Write(Model.PressNumber(
                Model.HitTest(Mouse), Number, GetInventory ? GetInventory() : TArray<FHudCreativeStack>()));
            return FReply::Handled();
        }
    return FReply::Handled();
}

FReply SHudCreativeScreen::OnKeyChar(const FGeometry& Geometry, const FCharacterEvent& Event)
{
    const TCHAR Character = Event.GetCharacter();
    if (Model.IsSearch() && Character >= 32 && Character != 127) Model.SetSearch(Model.GetSearch() + Character);
    return FReply::Handled();
}

int32 SHudCreativeScreen::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
    Tick = FMath::FloorToInt64(FPlatformTime::Seconds() * FoundationBlockFaces::TicksPerSecond);
    const FFrame Frame = GetFrame(Geometry);
    const FPainter Paint{*this, Geometry, Elements, LayerId, Frame.Units};
    const FIntPoint O = Frame.Origin;
    // renderTransparentBackground: the world darkened behind the container.
    const FVector2f Pixels = FVector2f(Geometry.GetLocalSize()) / Frame.Units;
    Paint.Box(&Solid, {0, 0}, FIntPoint(int32(Pixels.X) + 1, int32(Pixels.Y) + 1), FBox2f(FVector2f(0), FVector2f(1)),
        FLinearColor(.0627f, .0627f, .0627f, .784f));
    const TArray<FHudCreativeStack> Inventory = GetInventory ? GetInventory() : TArray<FHudCreativeStack>();
    auto Tab = [&](int32 Index, bool bSelected)
    {
        const FHudCreativeModel::FTab& Entry = Model.GetTabs()[Index];
        const FIntPoint At = O + FHudCreativeModel::GetTabPosition(Entry);
        Paint.Box(FindBrush(FString::Printf(TEXT("%stab_%s_%s_%d"), CreativeSprites,
                      Entry.bTop ? TEXT("top") : TEXT("bottom"), bSelected ? TEXT("selected") : TEXT("unselected"),
                      Entry.Column + 1)),
            At, {FHudCreativeModel::TabWidth, FHudCreativeModel::TabHeight});
        Paint.Item(Entry.Icon, At + FIntPoint(5, 8 + (Entry.bTop ? 1 : -1)));
    };
    for (int32 Index = 0; Index < Model.GetTabs().Num(); ++Index)
        if (Index != Model.GetSelectedIndex()) Tab(Index, false);
    const FString Background = Model.IsSearch() ? TEXT("tab_item_search")
        : Model.IsInventory()                   ? TEXT("tab_inventory")
                                                : TEXT("tab_items");
    Paint.Box(FindBrush(FString(CreativeGui) + Background), O, {FHudCreativeModel::Width, FHudCreativeModel::Height},
        FBox2f(FVector2f(0), FVector2f(FHudCreativeModel::Width / 256.f, FHudCreativeModel::Height / 256.f)));
    if (Model.GetTabs().IsValidIndex(Model.GetSelectedIndex())) Tab(Model.GetSelectedIndex(), true);
    const FHudCreativeModel::FTab* Selected = Model.GetSelectedTab();
    if (Selected && Selected->bShowTitle && !Model.IsSearch())
        Paint.Text(Selected->Title, O + FIntPoint(8, 6), TitleColor, false);
    if (Model.IsSearch())
    {
        // The search box: white text with a shadow and a blinking underscore cursor.
        const bool bCursor = int64((FPlatformTime::Seconds() - FocusTime) / .3) % 2 == 0;
        Paint.Text(
            Model.GetSearch() + (bCursor ? TEXT("_") : TEXT("")), O + FIntPoint(82, 6), FLinearColor::White, true);
    }
    if (!Model.IsInventory())
    {
        const int32 ScrollerTop =
            FHudCreativeModel::ScrollerY + int32((FHudCreativeModel::ScrollerHeight - 17) * Model.GetScroll());
        Paint.Box(
            FindBrush(FString(CreativeSprites) + (Model.CanScroll() ? TEXT("scroller") : TEXT("scroller_disabled"))),
            O + FIntPoint(FHudCreativeModel::ScrollerX, ScrollerTop), {12, 15});
    }
    // Slots: the hovered one gets the 1.21.6+ highlight behind and in front of its item.
    const FHudCreativeHit Hover = Model.HitTest(Mouse);
    auto Slot = [&](EHudCreativeHit Kind, int32 Index, const FHudCreativeStack& Stack)
    {
        const FIntPoint At = O + FHudCreativeModel::GetSlotPosition(Kind, Index);
        const bool bHover = Hover.Kind == Kind && Hover.Index == Index;
        if (bHover)
            Paint.Box(FindBrush(TEXT("gui/sprites/container/slot_highlight_back")), At - FIntPoint(4, 4), {24, 24});
        Paint.Stack(Stack, At);
        if (bHover)
            Paint.Box(FindBrush(TEXT("gui/sprites/container/slot_highlight_front")), At - FIntPoint(4, 4), {24, 24});
    };
    if (!Model.IsInventory())
        for (int32 Index = 0; Index < FHudCreativeModel::Columns * FHudCreativeModel::VisibleRows; ++Index)
        {
            const FName Item = Model.GetGridItem(Index);
            Slot(EHudCreativeHit::Grid, Index, Item.IsNone() ? FHudCreativeStack() : FHudCreativeStack{Item, 1});
        }
    for (int32 Index = 0; Index < FHudCreativeModel::Columns; ++Index)
        Slot(EHudCreativeHit::Hotbar, Index, Inventory.IsValidIndex(Index) ? Inventory[Index] : FHudCreativeStack());
    if (Model.IsInventory())
    {
        Slot(EHudCreativeHit::Offhand, 0, Inventory.IsValidIndex(9) ? Inventory[9] : FHudCreativeStack());
        for (int32 Index = 0; Index < 27; ++Index)
            Slot(EHudCreativeHit::Inventory, Index, {});
        for (int32 Index = 0; Index < 4; ++Index)
            Slot(EHudCreativeHit::Armor, Index, {});
        Slot(EHudCreativeHit::Destroy, 0, {});
    }
    // The carried stack follows the mouse; without one, the hovered item or tab shows its name.
    if (!Model.Carried.IsEmpty())
        Paint.Stack(Model.Carried, O + Mouse - FIntPoint(8, 8));
    else
    {
        FString Tooltip;
        if (Hover.Kind == EHudCreativeHit::Tab)
            Tooltip = Model.GetTabs()[Hover.Index].Title;
        else if (Hover.Kind == EHudCreativeHit::Grid)
            Tooltip = Model.GetGridItem(Hover.Index).IsNone() ? FString() : ItemName(Model.GetGridItem(Hover.Index));
        else if (Hover.Kind == EHudCreativeHit::Hotbar && Inventory.IsValidIndex(Hover.Index) &&
            !Inventory[Hover.Index].IsEmpty())
            Tooltip = ItemName(Inventory[Hover.Index].Item);
        if (!Tooltip.IsEmpty())
        {
            // TooltipRenderingUtil: 12 right and 12 up of the cursor, 3 pixels of padding, nine-sliced sprites.
            const FIntPoint At = O + Mouse + FIntPoint(12, -12);
            const FIntPoint Size(Paint.Measure(Tooltip), 8);
            Paint.NineSlice(
                FindBrush(TEXT("gui/sprites/tooltip/background")), At - FIntPoint(12, 12), Size + FIntPoint(24, 24), 9);
            Paint.NineSlice(
                FindBrush(TEXT("gui/sprites/tooltip/frame")), At - FIntPoint(12, 12), Size + FIntPoint(24, 24), 10);
            Paint.Text(Tooltip, At, FLinearColor::White, true);
        }
    }
    return LayerId;
}
