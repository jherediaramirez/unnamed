#pragma once

#include "CoreMinimal.h"
#include "Hud/HudCreative.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

class FFoundationBlockRegistry;
class UHudAssets;
class UTexture2D;

using FHudCreativeItems = TMap<FName, FHudCreativeItem>;
using FHudCreativeInventory = TFunction<TArray<FHudCreativeStack>()>;
using FHudCreativeWriter = TFunction<void(const FHudCreativeWrite&)>;

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
    SLATE_BEGIN_ARGS(SHudCreativeScreen) {}
    SLATE_ARGUMENT(TSharedPtr<const FFoundationBlockRegistry>, Registry)
    SLATE_ARGUMENT(TWeakObjectPtr<const UHudAssets>, HudAssets)
    // Every item the player could hold (blocks and tools): names, stack sizes, search tags.
    SLATE_ARGUMENT(FHudCreativeItems, Items)
    SLATE_ARGUMENT(FHudCreativeInventory, GetInventory)
    SLATE_ARGUMENT(FHudCreativeWriter, OnWrite)
    SLATE_ARGUMENT(TFunction<void()>, OnClose)
    SLATE_END_ARGS()

    void Construct(const FArguments& Arguments);

    FHudCreativeModel& GetModel() { return Model; }
    // The pointer in GUI pixels from the screen's corner (probes place it for hover and tooltip shots).
    void SetMouse(FIntPoint Gui) { Mouse = Gui; }
    // The GUI scale the screen uses for a viewport size in pixels (vanilla's auto scale, as the HUD's).
    static int32 GetGuiScale(FVector2f ViewportPixels);

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
    void Write(const TArray<FHudCreativeWrite>& Writes);
    const FSlateBrush* FindBrush(const FString& Id) const;
    FString ItemName(FName Item) const;

    struct FPainter;

    FHudCreativeModel Model;
    TSharedPtr<const FFoundationBlockRegistry> Registry;
    TWeakObjectPtr<const UHudAssets> HudAssets;
    FHudCreativeItems Items;
    FHudCreativeInventory GetInventory;
    FHudCreativeWriter OnWrite;
    TFunction<void()> OnClose;
    TMap<FString, FSlateBrush> Brushes;
    // A brush does not keep its texture alive; these do, so a garbage collection while the screen is open cannot
    // free them (it did once the held-item shots made the tour longer).
    TArray<TStrongObjectPtr<UTexture2D>> BrushTextures;
    FSlateBrush Atlas;
    FSlateBrush Solid;
    FIntPoint Mouse = FIntPoint(-1000, -1000);
    bool bDraggingScroller = false;
    // Block animation tick for icons, from the clock at each paint (20 per second).
    mutable int64 Tick = 0;
    double FocusTime = 0;
};
