#include "Hud/HudCreativeScreen.h"
#include "Engine/Texture2D.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/HudAssets.h"
#include "Hud/HudModel.h"
#include "Misc/PackageName.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "UnnamedProjectSettings.h"

namespace
{
    const TCHAR* CreativeGui = TEXT("gui/container/creative_inventory/");
    const TCHAR* CreativeSprites = TEXT("gui/sprites/container/creative_inventory/");
    const FLinearColor TitleColor = FLinearColor(FColor(0x40, 0x40, 0x40));
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

void SHudCreativeScreen::Construct(const FArguments& Arguments)
{
    RequestedGuiScale = Arguments._RequestedGuiScale;
    GetInventory = Arguments._GetInventory;
    OnWrite = Arguments._OnWrite;
    OnClose = Arguments._OnClose;
    FocusTime = FPlatformTime::Seconds();
    Art.Init(Arguments._Registry, Arguments._HudAssets, Arguments._Items);
    const UFoundationBlockAssets* Assets = Art.Registry ? Art.Registry->GetAssets() : nullptr;
    if (Assets) Model.Build(Assets->CreativeTabs, Arguments._Items);
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
        Ids.Add(FHudGuiArt::ItemSprite(Tab.Icon, Art.Items));
    for (const FString& Id : Ids)
        Art.LoadBlockGui(Id);
    Art.LoadItemSprites();
    Art.LoadHudSprites();
}

int32 SHudCreativeScreen::GetGuiScale(FVector2f ViewportPixels, int32 Requested)
{
    // One rule for every native screen (vanilla's: Options > GUI Scale changes the HUD and every GUI). Flooring the
    // pixels keeps Auto exactly the old float test against 320 x 240.
    return HudModel::GetScale(
        FIntPoint(FMath::FloorToInt32(ViewportPixels.X), FMath::FloorToInt32(ViewportPixels.Y)), Requested);
}

SHudCreativeScreen::FFrame SHudCreativeScreen::GetFrame(const FGeometry& Geometry) const
{
    FFrame Frame;
    const float SlateScale = FMath::Max(Geometry.GetAccumulatedLayoutTransform().GetScale(), .01f);
    const FVector2f Pixels = FVector2f(Geometry.GetLocalSize()) * SlateScale;
    Frame.Scale = GetScaleFor(Pixels);
    LastFrameScale = Frame.Scale;
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

bool SHudCreativeScreen::Write(const TArray<FHudCreativeWrite>& Writes)
{
    bool bThrown = true;
    if (!OnWrite) return bThrown;
    for (const FHudCreativeWrite& Write : Writes)
        if (!OnWrite(Write) && Write.Slot == FFoundationContainerMenu::Outside) bThrown = false;
    return bThrown;
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
    {
        Model.bFullInventory = FoundationInventory::IsFull();
        // A refused throw (foundation.Items.Entities 0, a dead player) keeps the stack on the cursor (QC RA1-1).
        const FHudCreativeStack Before = Model.Carried;
        if (!Write(Model.Click(
                Hit, Index, Event.IsShiftDown(), GetInventory ? GetInventory() : TArray<FHudCreativeStack>())))
            Model.Carried = Before;
    }
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
            Model.bFullInventory = FoundationInventory::IsFull();
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
    Art.Tick = FMath::FloorToInt64(FPlatformTime::Seconds() * FoundationBlockFaces::TicksPerSecond);
    const FFrame Frame = GetFrame(Geometry);
    const FHudGuiPainter Paint{Art, Geometry, Elements, LayerId, Frame.Units};
    const FIntPoint O = Frame.Origin;
    // renderTransparentBackground: the world darkened behind the container.
    const FVector2f Pixels = FVector2f(Geometry.GetLocalSize()) / Frame.Units;
    Paint.Box(&Art.Solid, {0, 0}, FIntPoint(int32(Pixels.X) + 1, int32(Pixels.Y) + 1),
        FBox2f(FVector2f(0), FVector2f(1)), FLinearColor(.0627f, .0627f, .0627f, .784f));
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
        // Without the full inventory (foundation.Inventory.Full 0), the main and armor slots draw empty.
        const bool bFull = FoundationInventory::IsFull();
        auto Get = [&Inventory, bFull](const FHudCreativeHit& Hit)
        {
            const int32 Index = FHudCreativeModel::GetInventorySlot(Hit);
            const bool bLab = Hit.Kind == EHudCreativeHit::Hotbar || Hit.Kind == EHudCreativeHit::Offhand;
            return (bFull || bLab) && Inventory.IsValidIndex(Index) ? Inventory[Index] : FHudCreativeStack();
        };
        Slot(EHudCreativeHit::Offhand, 0, Get({EHudCreativeHit::Offhand, 0}));
        for (int32 Index = 0; Index < 27; ++Index)
            Slot(EHudCreativeHit::Inventory, Index, Get({EHudCreativeHit::Inventory, Index}));
        for (int32 Index = 0; Index < 4; ++Index)
            Slot(EHudCreativeHit::Armor, Index, Get({EHudCreativeHit::Armor, Index}));
        Slot(EHudCreativeHit::Destroy, 0, {});
    }
    // The carried stack follows the mouse; without one, the hovered item or tab shows its name.
    if (!Model.Carried.IsEmpty())
        Paint.Stack(Model.Carried, O + Mouse - FIntPoint(8, 8));
    else
    {
        // An item's tooltip: its name, then its attribute lines (FHudCreativeItem::Details).
        TArray<FHudTooltipLine> Lines;
        auto ForItem = [this, &Lines](FName Item)
        {
            Lines.Add(Art.GetNameLine(Item));
            if (const FHudCreativeItem* Info = Art.Items.Find(Item)) Lines.Append(Info->Details);
        };
        if (Hover.Kind == EHudCreativeHit::Tab)
            Lines.Add({Model.GetTabs()[Hover.Index].Title, HudTooltip::White});
        else if (Hover.Kind == EHudCreativeHit::Grid && !Model.GetGridItem(Hover.Index).IsNone())
            ForItem(Model.GetGridItem(Hover.Index));
        else if (const int32 HoverSlot = FHudCreativeModel::GetInventorySlot(Hover);
            Inventory.IsValidIndex(HoverSlot) && !Inventory[HoverSlot].IsEmpty() &&
            (Hover.Kind == EHudCreativeHit::Hotbar || FoundationInventory::IsFull()))
            ForItem(Inventory[HoverSlot].Item);
        Paint.Tooltip(Lines, O + Mouse);
    }
    return LayerId;
}
