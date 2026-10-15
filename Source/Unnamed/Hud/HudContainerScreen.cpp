#include "Hud/HudContainerScreen.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/HudCreativeScreen.h"

namespace
{
    using FContainerScreenMenu = FFoundationContainerMenu;
    const FLinearColor ContainerTitleColor = FLinearColor(FColor(0x40, 0x40, 0x40));
    const TCHAR* ContainerBackground = TEXT("gui/container/inventory");
    const TCHAR* CraftingBackground = TEXT("gui/container/crafting_table");
    // The barrel's (T1 step 9): ContainerScreen's generic_54.png, its top 3 rows and its player part.
    const TCHAR* BarrelBackground = TEXT("gui/container/generic_54");
    // The furnace's, the smoker's and the blast furnace's own background, flame, arrow and title (T1 steps 8a2, 8b;
    // the same geometry, survival.r3.sprites).
    struct FFurnaceArt
    {
        const TCHAR *Background, *Flame, *Arrow, *Title;
    };
    const FFurnaceArt& GetFurnaceArt(EFoundationMenuKind Kind)
    {
        static const FFurnaceArt Furnace{TEXT("gui/container/furnace"),
            TEXT("gui/sprites/container/furnace/lit_progress"), TEXT("gui/sprites/container/furnace/burn_progress"),
            TEXT("Furnace")};
        static const FFurnaceArt Smoker{TEXT("gui/container/smoker"), TEXT("gui/sprites/container/smoker/lit_progress"),
            TEXT("gui/sprites/container/smoker/burn_progress"), TEXT("Smoker")};
        static const FFurnaceArt BlastFurnace{TEXT("gui/container/blast_furnace"),
            TEXT("gui/sprites/container/blast_furnace/lit_progress"),
            TEXT("gui/sprites/container/blast_furnace/burn_progress"), TEXT("Blast Furnace")};
        return Kind == EFoundationMenuKind::Smoker      ? Smoker
            : Kind == EFoundationMenuKind::BlastFurnace ? BlastFurnace
                                                        : Furnace;
    }
    const TCHAR* ContainerRecipeButton = TEXT("gui/sprites/recipe_book/button");
    const TCHAR* ContainerRecipeButtonHot = TEXT("gui/sprites/recipe_book/button_highlighted");
    const TCHAR* ContainerHighlightBack = TEXT("gui/sprites/container/slot_highlight_back");
    const TCHAR* ContainerHighlightFront = TEXT("gui/sprites/container/slot_highlight_front");

    // The silhouette an empty armor or offhand slot shows (InventoryMenu's slot backgrounds).
    const TCHAR* GetContainerSilhouette(int32 Slot)
    {
        switch (Slot)
        {
        case FContainerScreenMenu::FirstArmor:
            return TEXT("gui/sprites/container/slot/helmet");
        case FContainerScreenMenu::FirstArmor + 1:
            return TEXT("gui/sprites/container/slot/chestplate");
        case FContainerScreenMenu::FirstArmor + 2:
            return TEXT("gui/sprites/container/slot/leggings");
        case FContainerScreenMenu::FirstArmor + 3:
            return TEXT("gui/sprites/container/slot/boots");
        case FContainerScreenMenu::OffhandSlot:
            return TEXT("gui/sprites/container/slot/shield");
        default:
            return nullptr;
        }
    }
}

void SHudContainerScreen::Construct(const FArguments& Arguments)
{
    RequestedGuiScale = Arguments._RequestedGuiScale;
    GetView = Arguments._GetView;
    OnClick = Arguments._OnClick;
    OnClose = Arguments._OnClose;
    GetKey = Arguments._GetKey;
    GetTime = Arguments._GetTime;
    Model.Items = Arguments._Items;
    Model.FuelItems = Arguments._FuelItems;
    Model.Kind = Arguments._Kind;
    Model.bFullInventory = FoundationInventory::IsFull();
    Art.Init(Arguments._Registry, Arguments._HudAssets, Arguments._Items);
    // The screen's own art (ImportContainerGui.py), the shared slot highlight (ImportBlocks.py), item sprites and
    // the HUD's font and tooltip sprites.
    for (const TCHAR* Id : {GetBackground(), ContainerRecipeButton, ContainerRecipeButtonHot})
        Art.LoadContainerGui(Id);
    if (IsFurnaceMenu(Model.Kind))
        for (const TCHAR* Id : {GetFurnaceArt(Model.Kind).Flame, GetFurnaceArt(Model.Kind).Arrow})
            Art.LoadContainerGui(Id);
    for (int32 Slot = FContainerScreenMenu::FirstArmor; Slot < FContainerScreenMenu::PlayerMenuSlots; ++Slot)
        if (const TCHAR* Id = GetContainerSilhouette(Slot)) Art.LoadContainerGui(Id);
    for (const TCHAR* Id : {ContainerHighlightBack, ContainerHighlightFront})
        Art.LoadBlockGui(Id);
    Art.LoadItemSprites();
    Art.LoadHudSprites();
}

SHudContainerScreen::FFrame SHudContainerScreen::GetFrame(const FGeometry& Geometry) const
{
    FFrame Frame;
    const float SlateScale = FMath::Max(Geometry.GetAccumulatedLayoutTransform().GetScale(), .01f);
    const FVector2f Pixels = FVector2f(Geometry.GetLocalSize()) * SlateScale;
    Frame.Scale = SHudCreativeScreen::GetGuiScale(Pixels, RequestedGuiScale.Get());
    LastFrameScale = Frame.Scale;
    Frame.Units = float(Frame.Scale) / SlateScale;
    // leftPos/topPos: centred in the scaled screen with integer division.
    Frame.Origin = FIntPoint((int32(Pixels.X) / Frame.Scale - FHudContainerModel::Width) / 2,
        (int32(Pixels.Y) / Frame.Scale - FHudContainerModel::GetHeight(Model.Kind)) / 2);
    return Frame;
}

FIntPoint SHudContainerScreen::ToGui(const FGeometry& Geometry, FVector2D ScreenPosition) const
{
    const FFrame Frame = GetFrame(Geometry);
    const FVector2f Local = FVector2f(Geometry.AbsoluteToLocal(ScreenPosition)) / Frame.Units;
    return FIntPoint(FMath::FloorToInt32(Local.X), FMath::FloorToInt32(Local.Y)) - Frame.Origin;
}

FHudContainerView SHudContainerScreen::GetCurrentView() const
{
    FHudContainerView View = GetView ? GetView() : FHudContainerView();
    View.Slots.SetNum(Model.GetSlots());
    return View;
}

void SHudContainerScreen::Send(const TArray<FHudContainerClick>& Clicks)
{
    if (OnClick)
        for (const FHudContainerClick& Click : Clicks)
            OnClick(Click);
}

int32 SHudContainerScreen::GetButton(const FPointerEvent& Event)
{
    const FKey Button = Event.GetEffectingButton();
    return Button == EKeys::LeftMouseButton  ? 0
        : Button == EKeys::RightMouseButton  ? 1
        : Button == EKeys::MiddleMouseButton ? 2
                                             : -1;
}

void SHudContainerScreen::Press(FIntPoint Point, int32 Button, bool bShift, double Time)
{
    Mouse = Point;
    Model.bFullInventory = FoundationInventory::IsFull();
    Send(Model.MouseDown(Point, Button, bShift, GetCurrentView(), Time));
}

void SHudContainerScreen::Move(FIntPoint Point)
{
    Mouse = Point;
    Send(Model.MouseMove(Point, GetCurrentView()));
}

void SHudContainerScreen::Release(FIntPoint Point, int32 Button)
{
    Mouse = Point;
    Send(Model.MouseUp(Point, Button, GetCurrentView()));
}

FReply SHudContainerScreen::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    const int32 Button = GetButton(Event);
    // Another mouse button (a thumb button) acts as the binding it is set to, as a key does (Codex review 10,
    // finding 2).
    if (Button < 0)
        return PressBound(Geometry, Event.GetEffectingButton(), Event.IsControlDown()) ? FReply::Handled()
                                                                                       : FReply::Unhandled();
    bReleasingCapture = false;
    Press(ToGui(Geometry, Event.GetScreenSpacePosition()), Button, Event.IsShiftDown(),
        GetTime ? GetTime() : FPlatformTime::Seconds());
    return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SHudContainerScreen::OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
    // Slate turns a quick second press into this event; the model times double-clicks itself.
    return OnMouseButtonDown(Geometry, Event);
}

FReply SHudContainerScreen::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
    const int32 Button = GetButton(Event);
    if (Button >= 0) Release(ToGui(Geometry, Event.GetScreenSpacePosition()), Button);
    // The capture lasts while any button is down (a right tap during a left drag keeps it). Letting it go makes Slate
    // report a capture loss, which must not forget the click a double-click needs (Codex 5b finding 1).
    if (!Event.GetPressedButtons().IsEmpty()) return FReply::Handled();
    bReleasingCapture = true;
    return FReply::Handled().ReleaseMouseCapture();
}

FReply SHudContainerScreen::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
    Move(ToGui(Geometry, Event.GetScreenSpacePosition()));
    return FReply::Handled();
}

void SHudContainerScreen::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{
    // Our own release: only a drag (already ended by the release) is forgotten. Otherwise a drag or a double-click
    // whose release will never come (focus lost, alt-tab) ends without a click.
    if (bReleasingCapture)
        Model.ResetDrag();
    else
        Model.Reset();
    bReleasingCapture = false;
}

FReply SHudContainerScreen::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    PressBound(Geometry, Event.GetKey(), Event.IsControlDown());
    return FReply::Handled();
}

bool SHudContainerScreen::PressBound(const FGeometry& Geometry, const FKey& Key, bool bControl)
{
    // Escape, or the player's inventory key, closes; the hotbar, offhand and drop keys are the player's bindings
    // (Codex 5b finding 3).
    if (Key == EKeys::Escape || Key == GetBoundKey(TEXT("key.inventory"), EKeys::E))
    {
        Model.Reset();
        if (OnClose) OnClose();
        return true;
    }
    Model.bFullInventory = FoundationInventory::IsFull();
    // Keys act on the slot under the cursor now, also before the mouse has moved over the open screen.
    Mouse = ToGui(Geometry, FSlateApplication::Get().GetCursorPos());
    static const FKey Numbers[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six,
        EKeys::Seven, EKeys::Eight, EKeys::Nine};
    bool bBound = false;
    for (int32 Number = 0; Number < 9; ++Number)
        if (Key == GetBoundKey(*FString::Printf(TEXT("key.hotbar.%d"), Number + 1), Numbers[Number]))
        {
            bBound = true;
            Send(Model.PressSwap(Mouse, Number, GetCurrentView()));
        }
    // The offhand key swaps with the offhand (Inventory number 40), the drop key throws.
    if (Key == GetBoundKey(TEXT("key.swapOffhand"), EKeys::F))
    {
        bBound = true;
        Send(Model.PressSwap(Mouse, FFoundationInventory::OffhandSlot, GetCurrentView()));
    }
    if (Key == GetBoundKey(TEXT("key.drop"), EKeys::Q))
    {
        bBound = true;
        Send(Model.PressDrop(Mouse, bControl, GetCurrentView()));
    }
    return bBound;
}

const TCHAR* SHudContainerScreen::GetBackground() const
{
    return Model.Kind == EFoundationMenuKind::Crafting ? CraftingBackground
        : IsFurnaceMenu(Model.Kind)                    ? GetFurnaceArt(Model.Kind).Background
        : HasViewers(Model.Kind)                       ? BarrelBackground
                                                       : ContainerBackground;
}

FKey SHudContainerScreen::GetBoundKey(const TCHAR* Id, const FKey& Default) const
{
    return GetKey ? GetKey(FName(Id)) : Default;
}

int32 SHudContainerScreen::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
    Art.Tick = FMath::FloorToInt64(FPlatformTime::Seconds() * FoundationBlockFaces::TicksPerSecond);
    const FFrame Frame = GetFrame(Geometry);
    const FHudGuiPainter Paint{Art, Geometry, Elements, LayerId, Frame.Units};
    const FIntPoint O = Frame.Origin;
    const FBox2f Whole(FVector2f(0), FVector2f(1));
    // renderTransparentBackground: the world darkened behind the container.
    const FVector2f Pixels = FVector2f(Geometry.GetLocalSize()) / Frame.Units;
    Paint.Box(&Art.Solid, {0, 0}, FIntPoint(int32(Pixels.X) + 1, int32(Pixels.Y) + 1), Whole,
        FLinearColor(.0627f, .0627f, .0627f, .784f));
    // The barrel, the chest and the double chest (ContainerScreen over generic_54).
    const bool bBarrel = HasViewers(Model.Kind);
    const bool bLarge = Model.Kind == EFoundationMenuKind::LargeChest;
    if (bBarrel)
    {
        // ContainerScreen.renderBg: the title and the rows (71 high for 3, 125 for 6), then the player part from y 126
        // (96 high).
        using M = FHudContainerModel;
        const int32 Top = bLarge ? M::LargeChestTopHeight : M::BarrelTopHeight;
        Paint.Box(Art.Find(GetBackground()), O, {M::Width, Top},
            FBox2f(FVector2f(0), FVector2f(M::Width / 256.f, Top / 256.f)));
        Paint.Box(Art.Find(GetBackground()), O + FIntPoint(0, Top), {M::Width, M::BarrelPlayerHeight},
            FBox2f(FVector2f(0, M::BarrelPlayerTextureY / 256.f),
                FVector2f(M::Width / 256.f, (M::BarrelPlayerTextureY + M::BarrelPlayerHeight) / 256.f)));
    }
    else
        Paint.Box(Art.Find(GetBackground()), O, {FHudContainerModel::Width, FHudContainerModel::Height},
            FBox2f(FVector2f(0), FVector2f(FHudContainerModel::Width / 256.f, FHudContainerModel::Height / 256.f)));
    // The recipe-book button (inert until the recipe book exists) and the labels: the inventory screen's "Crafting"
    // over its 2x2; the crafting table's title and "Inventory" over the player's rows; the furnace's title centred.
    const bool bCrafting = Model.Kind == EFoundationMenuKind::Crafting;
    const bool bFurnace = IsFurnaceMenu(Model.Kind);
    const FFurnaceArt& Furnace = GetFurnaceArt(Model.Kind);
    if (FHudContainerModel::HasRecipeButton(Model.Kind))
        Paint.Box(Art.Find(FHudContainerModel::IsInRecipeButton(Model.Kind, Mouse) ? ContainerRecipeButtonHot
                                                                                   : ContainerRecipeButton),
            O + FHudContainerModel::GetRecipeButton(Model.Kind),
            {FHudContainerModel::RecipeButtonWidth, FHudContainerModel::RecipeButtonHeight});
    if (bBarrel)
    {
        // container.barrel, container.chest, container.chestDouble.
        Paint.Text(bLarge                                  ? TEXT("Large Chest")
                : Model.Kind == EFoundationMenuKind::Chest ? TEXT("Chest")
                                                           : TEXT("Barrel"),
            O + FIntPoint(FHudContainerModel::InventoryLabelX, FHudContainerModel::FurnaceTitleY), ContainerTitleColor,
            false);
        Paint.Text(TEXT("Inventory"),
            O +
                FIntPoint(FHudContainerModel::InventoryLabelX,
                    bLarge ? FHudContainerModel::LargeChestInventoryLabelY : FHudContainerModel::BarrelInventoryLabelY),
            ContainerTitleColor, false);
    }
    else if (bCrafting)
    {
        Paint.Text(TEXT("Crafting"),
            O + FIntPoint(FHudContainerModel::CraftingTitleX, FHudContainerModel::CraftingTitleY), ContainerTitleColor,
            false);
        Paint.Text(TEXT("Inventory"),
            O + FIntPoint(FHudContainerModel::InventoryLabelX, FHudContainerModel::InventoryLabelY),
            ContainerTitleColor, false);
    }
    else if (bFurnace)
    {
        const FString Title = Furnace.Title;
        Paint.Text(Title,
            O + FIntPoint((FHudContainerModel::Width - Paint.Measure(Title)) / 2, FHudContainerModel::FurnaceTitleY),
            ContainerTitleColor, false);
        Paint.Text(TEXT("Inventory"),
            O + FIntPoint(FHudContainerModel::InventoryLabelX, FHudContainerModel::InventoryLabelY),
            ContainerTitleColor, false);
    }
    else
        Paint.Text(TEXT("Crafting"), O + FIntPoint(FHudContainerModel::TitleX, FHudContainerModel::TitleY),
            ContainerTitleColor, false);

    const FHudContainerView View = GetCurrentView();
    if (bFurnace)
    {
        // AbstractFurnaceScreen: the flame's lower part grows up from its lower edge, the arrow's left part rightward.
        const int32 Flame = FHudContainerModel::GetFlameHeight(View.bLit, View.LitFraction);
        if (Flame > 0)
            Paint.Box(Art.Find(Furnace.Flame),
                O + FIntPoint(FHudContainerModel::FlameX, FHudContainerModel::FlameBottom - Flame),
                {FHudContainerModel::FlameSize, Flame},
                FBox2f(FVector2f(0, 1 - float(Flame) / FHudContainerModel::FlameSize), FVector2f(1)));
        const int32 Arrow = FHudContainerModel::GetArrowWidth(View.CookFraction);
        if (Arrow > 0)
            Paint.Box(Art.Find(Furnace.Arrow), O + FIntPoint(FHudContainerModel::ArrowX, FHudContainerModel::ArrowY),
                {Arrow, FHudContainerModel::ArrowHeight},
                FBox2f(FVector2f(0), FVector2f(float(Arrow) / FHudContainerModel::ArrowWidth, 1)));
    }
    TMap<int32, FHudCreativeStack> Preview;
    FHudCreativeStack Carried;
    Model.GetDragPreview(View, Preview, Carried);
    const int32 Hover = Model.HitTest(Mouse);
    for (int32 Slot = 0; Slot < Model.GetSlots(); ++Slot)
    {
        const FIntPoint At = O + Model.GetPosition(Slot);
        // Without the full inventory the main and armor slots stay empty, as the creative screen's survival tab.
        const bool bShown = Model.IsShown(Slot);
        const FHudCreativeStack* Shown = Preview.Find(Slot);
        const FHudCreativeStack Stack = Shown ? *Shown : bShown ? View.Get(Slot) : FHudCreativeStack();
        // The hovered slot and the slots a drag covers get the 1.21.6+ highlight behind and in front of the item.
        const bool bHighlight = Slot == Hover || Model.GetDragSlots().Contains(Slot);
        if (bHighlight) Paint.Box(Art.Find(ContainerHighlightBack), At - FIntPoint(4, 4), {24, 24});
        if (Stack.IsEmpty())
        {
            if (const TCHAR* Silhouette = bCrafting || bFurnace || bBarrel ? nullptr : GetContainerSilhouette(Slot))
                Paint.Box(Art.Find(Silhouette), At, {16, 16});
        }
        else
            Paint.Stack(Stack, At);
        if (bHighlight) Paint.Box(Art.Find(ContainerHighlightFront), At - FIntPoint(4, 4), {24, 24});
    }
    // The cursor stack follows the mouse (during a drag, what the spread leaves); without one, the hovered item's
    // tooltip.
    if (!View.Carried.IsEmpty())
        Paint.Stack(Carried, O + Mouse - FIntPoint(8, 8));
    else if (Hover != INDEX_NONE && Hover != FContainerScreenMenu::Outside && !View.Get(Hover).IsEmpty())
    {
        TArray<FHudTooltipLine> Lines = {Art.GetNameLine(View.Get(Hover).Item)};
        if (const FHudCreativeItem* Info = Art.Items.Find(View.Get(Hover).Item)) Lines.Append(Info->Details);
        Paint.Tooltip(Lines, O + Mouse);
    }
    return LayerId;
}
