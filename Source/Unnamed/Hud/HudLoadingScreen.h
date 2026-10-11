#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationLab.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

class UHudAssets;
class UTexture2D;

// Minecraft 26.3's "Loading terrain..." screen over the game viewport, at the HUD's GUI scale: the title panorama
// (panorama_0, from the menu's own files) cover-fitted, blurred as the menu's background and darkened, a 200 x 2
// progress bar and the text above it, and under the bar the chunk map (2 x 2 GUI pixels per column, rows from the
// minimum corner: +X right, +Y down) when the state carries one.
class UNNAMED_API SHudLoadingScreen : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SHudLoadingScreen) {}
    SLATE_ARGUMENT(TWeakObjectPtr<const UHudAssets>, HudAssets)
    // Pixels of the Gaussian blur on the panorama (the menu's menuBackgroundBlurriness).
    SLATE_ARGUMENT(float, BlurPixels)
    SLATE_END_ARGS()

    void Construct(const FArguments& Arguments);
    void SetState(const FFoundationLoadState& InState) { State = InState; }

protected:
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style,
        bool bParentEnabled) const override;

private:
    TWeakObjectPtr<const UHudAssets> HudAssets;
    float BlurPixels = 5;
    FFoundationLoadState State;
    FSlateBrush Solid;
    FSlateBrush Panorama;
    FSlateBrush Font;
    TStrongObjectPtr<UTexture2D> PanoramaTexture;
    TStrongObjectPtr<UTexture2D> FontTexture;
};
