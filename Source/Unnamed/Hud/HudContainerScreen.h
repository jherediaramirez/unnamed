#pragma once

#include "CoreMinimal.h"
#include "Hud/HudContainer.h"
#include "Hud/HudGuiPainter.h"
#include "Widgets/SLeafWidget.h"

class FFoundationBlockRegistry;
class UHudAssets;

// The survival inventory screen over the game viewport (survival step 5b): Java 26.3's InventoryScreen art
// (gui/container/inventory.png, the empty armor and offhand silhouettes, the recipe-book button, inert) and gestures
// (FHudContainerModel) at the HUD's GUI scale, with the dark backdrop container screens draw. The owner supplies the
// player's menu slots and cursor each frame and sends each click to the authority (ContainerClick); closing sends
// ContainerClose. The player-model frame stays empty by decision (first person only, no player body).
class UNNAMED_API SHudContainerScreen : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SHudContainerScreen) : _RequestedGuiScale(0) {}
    SLATE_ARGUMENT(TSharedPtr<const FFoundationBlockRegistry>, Registry)
    SLATE_ARGUMENT(TWeakObjectPtr<const UHudAssets>, HudAssets)
    // Every item the player could hold: names, stack sizes, equipment, tooltip lines.
    SLATE_ARGUMENT(FHudCreativeItems, Items)
    SLATE_ARGUMENT(TFunction<FHudContainerView()>, GetView)
    SLATE_ARGUMENT(TFunction<void(const FHudContainerClick&)>, OnClick)
    SLATE_ARGUMENT(TFunction<void()>, OnClose)
    // The player's current key for a binding id (key.inventory, key.hotbar.1-9, key.swapOffhand, key.drop). Unset,
    // the default keys (E, 1-9, F, Q).
    SLATE_ARGUMENT(TFunction<FKey(FName)>, GetKey)
    // Options > GUI Scale (0 Auto), read every frame.
    SLATE_ATTRIBUTE(int32, RequestedGuiScale)
    SLATE_END_ARGS()

    void Construct(const FArguments& Arguments);

    FHudContainerModel& GetModel() { return Model; }
    // The pointer in GUI pixels from the screen's corner (probes place it for hover, tooltip and drag shots).
    void SetMouse(FIntPoint Gui) { Mouse = Gui; }
    // The scale of the last frame painted or hit-tested (0 before the first), for probes.
    int32 GetLastFrameScale() const { return LastFrameScale; }
    // A click from a probe or test, through the same path as the mouse (Point in screen GUI pixels).
    void Press(FIntPoint Point, int32 Button, bool bShift, double Time);
    void Move(FIntPoint Point);
    void Release(FIntPoint Point, int32 Button);
    // The textures the screen found (probes check the import ran).
    bool HasArt() const { return Art.Find(TEXT("gui/container/inventory")) != nullptr; }

    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent& Event) override;
    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

protected:
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style,
        bool bParentEnabled) const override;

private:
    struct FFrame
    {
        int32 Scale = 1;
        FIntPoint Origin = FIntPoint::ZeroValue;
        float Units = 1;
    };
    FFrame GetFrame(const FGeometry& Geometry) const;
    FIntPoint ToGui(const FGeometry& Geometry, FVector2D ScreenPosition) const;
    FHudContainerView GetCurrentView() const;
    void Send(const TArray<FHudContainerClick>& Clicks);
    static int32 GetButton(const FPointerEvent& Event);

    FHudContainerModel Model;
    TAttribute<int32> RequestedGuiScale;
    mutable int32 LastFrameScale = 0;
    FHudGuiArt Art;
    TFunction<FHudContainerView()> GetView;
    TFunction<void(const FHudContainerClick&)> OnClick;
    TFunction<void()> OnClose;
    TFunction<FKey(FName)> GetKey;
    FIntPoint Mouse = FIntPoint(-1000, -1000);
    // Set when the screen's own button release lets the capture go: that capture loss keeps the double-click state.
    bool bReleasingCapture = false;

    FKey GetBoundKey(const TCHAR* Id, const FKey& Default) const;
};
