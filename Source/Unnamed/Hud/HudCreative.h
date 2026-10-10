#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockAssets.h"

class FFoundationBlockRegistry;

// One stack as the creative screen holds it: an item id and a count.
struct FHudCreativeStack
{
    FName Item;
    int32 Count = 0;

    bool IsEmpty() const { return Item.IsNone() || Count <= 0; }
    bool operator==(const FHudCreativeStack& Other) const = default;
};

// What the screen knows about an item: name for the tooltip and search, stack size, search tags.
struct FHudCreativeItem
{
    FString Name;
    int32 MaxStack = 64;
    // Lowercase vanilla-style tag names for "#" search ("logs", "mineable/axe").
    TArray<FString> Tags;
};

// Where a point on the screen lands. Slot indices: Grid 0-44 (visible rows), Hotbar 0-8, Inventory 0-26 (the
// survival tab's main slots), Armor 0-3, Tab = index into GetTabs().
enum class EHudCreativeHit : uint8
{
    None,
    Outside,
    Grid,
    Hotbar,
    Offhand,
    Inventory,
    Armor,
    Destroy,
    Tab,
    Scroller,
    Search
};

struct FHudCreativeHit
{
    EHudCreativeHit Kind = EHudCreativeHit::None;
    int32 Index = INDEX_NONE;

    bool IsSlot() const
    {
        return Kind == EHudCreativeHit::Grid || Kind == EHudCreativeHit::Hotbar || Kind == EHudCreativeHit::Offhand ||
            Kind == EHudCreativeHit::Inventory || Kind == EHudCreativeHit::Armor || Kind == EHudCreativeHit::Destroy;
    }
};

// A write into the player's inventory the screen asks the authority for (vanilla's creative slot packet).
// Slot 0-8 is the hotbar, 9 the offhand.
struct FHudCreativeWrite
{
    int32 Slot = 0;
    FHudCreativeStack Stack;
};

// Java 26.3's creative inventory (CreativeModeInventoryScreen) as pure state, layout and click rules, in GUI
// pixels relative to the screen's top-left corner (195 x 136). The widget only draws it and forwards input.
class UNNAMED_API FHudCreativeModel
{
public:
    static constexpr int32 Width = 195;
    static constexpr int32 Height = 136;
    static constexpr int32 Columns = 9;
    static constexpr int32 VisibleRows = 5;
    static constexpr int32 SlotSize = 18;
    static constexpr int32 TabWidth = 26;
    static constexpr int32 TabHeight = 32;
    static constexpr int32 ScrollerX = 175;
    static constexpr int32 ScrollerY = 18;
    static constexpr int32 ScrollerHeight = 112;

    struct FTab
    {
        FName Id;
        FString Title;
        bool bTop = true;
        int32 Column = 0;
        FName Type;
        bool bAlignedRight = false;
        bool bShowTitle = true;
        bool bCanScroll = true;
        FName Icon;
        // The tab's display order with only items that exist here.
        TArray<FName> Items;
    };

    // Tabs from the 26.3 data, keeping only known items; empty category tabs are hidden as vanilla's
    // shouldDisplay does, the hotbar, search and inventory tabs always show.
    void Build(TConstArrayView<FFoundationCreativeTabAsset> Source, const TMap<FName, FHudCreativeItem>& Items);

    const TArray<FTab>& GetTabs() const { return Tabs; }
    const FTab* GetSelectedTab() const { return Tabs.IsValidIndex(Selected) ? &Tabs[Selected] : nullptr; }
    int32 GetSelectedIndex() const { return Selected; }
    void SelectTab(int32 Index);
    bool IsSearch() const { return GetSelectedTab() && GetSelectedTab()->Type == TEXT("SEARCH"); }
    bool IsInventory() const { return GetSelectedTab() && GetSelectedTab()->Type == TEXT("INVENTORY"); }

    // The selected tab's items after the search filter.
    const TArray<FName>& GetItems() const { return Visible; }
    const FString& GetSearch() const { return Search; }
    void SetSearch(const FString& Text);

    // Scrolling, as vanilla: Scroll 0..1 over the rows beyond the first five; the wheel moves one row.
    bool CanScroll() const;
    float GetScroll() const { return Scroll; }
    void SetScroll(float Value);
    void ScrollRows(int32 Rows);
    int32 GetFirstRow() const;
    FName GetGridItem(int32 Slot) const;

    // Layout.
    static FIntPoint GetTabPosition(const FTab& Tab);
    static FIntPoint GetSlotPosition(EHudCreativeHit Kind, int32 Index);
    FHudCreativeHit HitTest(FIntPoint Point) const;

    // Clicks (slotClicked): Button 0 left, 1 right, 2 middle. Inventory is the hotbar (0-8) then the offhand.
    TArray<FHudCreativeWrite> Click(
        const FHudCreativeHit& Hit, int32 Button, bool bShift, TConstArrayView<FHudCreativeStack> Inventory);
    // A number key over a slot (SWAP): a grid item fills hotbar slot Number as a full stack; a hotbar slot swaps.
    TArray<FHudCreativeWrite> PressNumber(
        const FHudCreativeHit& Hit, int32 Number, TConstArrayView<FHudCreativeStack> Inventory) const;

    FHudCreativeStack Carried;

private:
    int32 GetMaxStack(FName Item) const;
    void Filter();
    TArray<FTab> Tabs;
    TMap<FName, FHudCreativeItem> Known;
    TArray<FName> Visible;
    FString Search;
    int32 Selected = 0;
    float Scroll = 0;
};

// A block item's GUI icon: the item's model under the 26.3 "gui" display transform (rotation 30, 225, 0;
// scale 0.625) in a 16 x 16 slot, as three textured quads with fixed face shading. Model blocks' items draw their
// item model instead: a flat sprite, or their baked block model under its own gui transform (back faces culled,
// far quads first).
namespace HudBlockIcon
{
    struct FQuad
    {
        // Corners in slot pixels (0..16, y down) and their texture UVs within the face's texture.
        FVector2f Corners[4];
        FVector2f UVs[4];
        int32 Texture = 0;
        // An IconAtlas tile drawn as is (item sprites); INDEX_NONE samples Texture's current frame.
        int32 Tile = INDEX_NONE;
        // Brightness times the item tint where the face is tinted (alpha 255).
        FColor Color = FColor::White;
    };
    // Measured from a 26.3 creative screenshot: top 1.0, left (east) 0.68, right (north) 0.44.
    constexpr float TopShade = 1.f, LeftShade = .68f, RightShade = .44f;
    UNNAMED_API TArray<FQuad> Build(const FFoundationBlockRegistry& Registry, FName Block);
    // True when a drawn face uses an animated texture, so the icon changes with the tick.
    UNNAMED_API bool IsAnimated(const FFoundationBlockRegistry& Registry, FName Block);
}
