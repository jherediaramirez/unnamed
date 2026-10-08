#include "Hud/HudView.h"
#include "Foundation/FoundationBlockAssets.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Hud/HudCreativeScreen.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SHudView::Construct(const FArguments& Arguments)
{
    SetVisibility(EVisibility::HitTestInvisible);
    Solid = *FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
}
void SHudView::SetBrushes(TMap<FName, FSlateBrush> InBrushes)
{
    Brushes = MoveTemp(InBrushes);
}
void SHudView::SetBlockIcons(TSharedPtr<const FFoundationBlockRegistry> InBlocks)
{
    Blocks = MoveTemp(InBlocks);
    const UFoundationBlockAssets* Assets = Blocks ? Blocks->GetAssets() : nullptr;
    Atlas = FSlateBrush();
    if (Assets && Assets->IconAtlas)
    {
        Atlas.SetResourceObject(Assets->IconAtlas);
        Atlas.ImageSize = FVector2D(Assets->IconAtlas->GetSizeX(), Assets->IconAtlas->GetSizeY());
    }
    Invalidate(EInvalidateWidgetReason::Paint);
}
void SHudView::SetFrame(TArray<FHudDraw> InDraws, int32 InScale)
{
    if (Scale == InScale && Draws == InDraws) return;
    Draws = MoveTemp(InDraws);
    Scale = InScale;
    Invalidate(EInvalidateWidgetReason::Paint);
}
int32 SHudView::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
    ++PaintCount;
    // Framebuffer coordinates / accumulated Slate layout scale: Windows DPI is applied exactly once.
    const float SlateScale = Geometry.GetAccumulatedLayoutTransform().GetScale();
    const float Units = float(Scale) / FMath::Max(SlateScale, .01f);
    for (const FHudDraw& Draw : Draws)
    {
        if (!Draw.Block.IsNone())
        {
            if (Blocks)
                HudBlockIcon::Paint(Elements, LayerId, Geometry, Atlas, *Blocks, Draw.Block, FVector2f(Draw.Rect.Min),
                    Units, Draw.Tick);
            continue;
        }
        const FSlateBrush* Source = Draw.Sprite.IsNone() ? &Solid : Brushes.Find(Draw.Sprite);
        if (!Source) continue;
        FSlateBrush Brush = *Source;
        Brush.SetUVRegion(FBox2D(FVector2D(Draw.UV.Min), FVector2D(Draw.UV.Max)));
        FSlateDrawElement::MakeBox(Elements, ++LayerId,
            Geometry.ToPaintGeometry(
                FVector2f(Draw.Rect.Size()) * Units, FSlateLayoutTransform(FVector2f(Draw.Rect.Min) * Units)),
            &Brush, ESlateDrawEffect::None, Draw.Color);
    }
    return LayerId;
}
