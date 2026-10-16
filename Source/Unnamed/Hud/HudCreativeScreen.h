#pragma once

#include "CoreMinimal.h"
#include "Hud/HudCreative.h"
#include "Hud/HudGuiPainter.h"
#include "Widgets/SLeafWidget.h"

class FFoundationBlockRegistry;
class UHudAssets;

using FHudCreativeInventory = TFunction<TArray<FHudCreativeStack>()>;
// Sends one write to the authority; whether it was accepted.
using FHudCreativeWriter = TFunction<bool(const FHudCreativeWrite&)>;

// The creative inventory screen over the game viewport: Java 26.3's CreativeModeInventoryScreen art, layout and
// clicks (FHudCreativeModel) at the HUD's GUI scale, with the dark backdrop container screens draw. The owner
// supplies the player's inventory (hotbar 0-8, offhand 9) each frame and turns writes into authority commands.
namespace HudBlockIcon
{
    // Paints a block's 3D item model (from the registry's icon atlas, brush Atlas) in the 16 x 16 slot whose
    // top-left corner is Origin, in GUI pixels of Units Slate units. False when the block or atlas is missing.
    UNNAMED_API bool Paint(FSlateWindowElementList& Elements, int32& LayerId, const FGeometry& Geometry,
        const FSlateBrush& Atlas, const FFoundationBlockRegistry& Registry, FName Block, FVector2f Origin, float Units,
        int64 Tick);
}

class UNNAMED_API SHudCreativeScreen : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SHudCreativeScreen) : _RequestedGuiScale(0) {}
    SLATE_ARGUMENT(TSharedPtr<const FFoundationBlockRegistry>, Registry)
    SLATE_ARGUMENT(TWeakObjectPtr<const UHudAssets>, HudAssets)
    // Every item the player could hold (blocks and tools): names, stack sizes, search tags.
    SLATE_ARGUMENT(FHudCreativeItems, Items)
    SLATE_ARGUMENT(FHudCreativeInventory, GetInventory)
    SLATE_ARGUMENT(FHudCreativeWriter, OnWrite)
    SLATE_ARGUMENT(TFunction<void()>, OnClose)
    // The player's current key for a binding id (key.inventory, key.hotbar.1-9), as SHudContainerScreen's. Unset, the
    // default keys (E, 1-9).
    SLATE_ARGUMENT(TFunction<FKey(FName)>, GetKey)
    // Options > GUI Scale (0 Auto), read every frame so a change in the open menu applies at once.
    SLATE_ATTRIBUTE(int32, RequestedGuiScale)
    SLATE_END_ARGS()

    void Construct(const FArguments& Arguments);

    FHudCreativeModel& GetModel() { return Model; }
    // The pointer in GUI pixels from the screen's corner (probes place it for hover and tooltip shots).
    void SetMouse(FIntPoint Gui) { Mouse = Gui; }
    // The GUI scale for a viewport size in pixels and Options > GUI Scale (0 Auto): the HUD's, HudModel::GetScale.
    static int32 GetGuiScale(FVector2f ViewportPixels, int32 Requested);
    // The scale for a viewport in pixels at the screen's requested GUI scale (what its frames use).
    int32 GetScaleFor(FVector2f ViewportPixels) const { return GetGuiScale(ViewportPixels, RequestedGuiScale.Get()); }
    // The scale of the last frame painted or hit-tested (0 before the first), for probes.
    int32 GetLastFrameScale() const { return LastFrameScale; }

    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply OnKeyChar(const FGeometry& Geometry, const FCharacterEvent& Event) override;

protected:
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style,
        bool bParentEnabled) const override;

private:
    // GUI-pixel frame: scale and the screen's top-left corner (vanilla leftPos/topPos).
    struct FFrame
    {
        int32 Scale = 1;
        FIntPoint Origin = FIntPoint::ZeroValue;
        float Units = 1;
    };
    FFrame GetFrame(const FGeometry& Geometry) const;
    FIntPoint ToGui(const FGeometry& Geometry, const FPointerEvent& Event) const;
    // Sends the writes; false when the authority refused a throw (a write to the outside slot).
    bool Write(const TArray<FHudCreativeWrite>& Writes);
    FKey GetBoundKey(const TCHAR* Id, const FKey& Default) const;
    // The inventory key closes; a hotbar key swaps the hovered slot with that hotbar slot. Whether Key was one.
    bool PressInventory(const FKey& Key);
    bool PressHotbar(const FKey& Key);
    const FSlateBrush* FindBrush(const FString& Id) const { return Art.Find(Id); }

    FHudCreativeModel Model;
    TAttribute<int32> RequestedGuiScale;
    mutable int32 LastFrameScale = 0;
    // The registry, the HUD assets, the items and the screen's brushes (HudGuiPainter).
    FHudGuiArt Art;
    FHudCreativeInventory GetInventory;
    FHudCreativeWriter OnWrite;
    TFunction<void()> OnClose;
    TFunction<FKey(FName)> GetKey;
    FIntPoint Mouse = FIntPoint(-1000, -1000);
    bool bDraggingScroller = false;
    double FocusTime = 0;
};
