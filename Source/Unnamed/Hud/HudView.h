#pragma once
#include "Hud/HudDrawList.h"
#include "Widgets/SLeafWidget.h"
#include "Styling/SlateBrush.h"

class FFoundationBlockRegistry;

// One passive leaf. Input/focus and the independent F3 overlay stay with Foundation.
class SHudView : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SHudView) {}
    SLATE_END_ARGS()
    void Construct(const FArguments& Arguments);
    void SetBrushes(TMap<FName, FSlateBrush> InBrushes);
    void SetFrame(TArray<FHudDraw> InDraws, int32 InScale);
    // The registry whose icon atlas paints block item models (FHudDraw::Block).
    void SetBlockIcons(TSharedPtr<const FFoundationBlockRegistry> InBlocks);
    const FFoundationBlockRegistry* GetBlockIcons() const { return Blocks.Get(); }
    int32 GetDrawCount() const { return Draws.Num(); }
    int32 GetPaintCount() const { return PaintCount; }

protected:
    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style,
        bool bParentEnabled) const override;

private:
    TArray<FHudDraw> Draws;
    TMap<FName, FSlateBrush> Brushes;
    FSlateBrush Solid;
    TSharedPtr<const FFoundationBlockRegistry> Blocks;
    FSlateBrush Atlas;
    int32 Scale = 1;
    mutable int32 PaintCount = 0;
};
