#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

class UTexture2D;

// The loading overlay's geometry in GUI pixels: the logo min(width x 0.75, height) / 4 tall and four times as wide,
// drawn as mojangstudios.png's two stacked halves side by side (the texture's layout); a 10-pixel bar at 0.8325 of the
// height. Our own layout, close to vanilla's as remembered: the sizes are unverified until a capture of the vanilla
// client measures them (Docs/Records/2026-10-12-Menu-Transitions.md).
struct UNNAMED_API FMenuLoadingOverlayLayout
{
    int32 GuiScale = 1;
    FIntPoint GuiSize = FIntPoint::ZeroValue;
    // GUI pixels, Min inclusive and Max exclusive.
    FIntRect LogoLeft, LogoRight, Bar;

    static FMenuLoadingOverlayLayout Compute(FIntPoint Framebuffer, int32 RequestedGuiScale);
};

// The Mojang Studios loading overlay that covers the kept title browser (menu.Title.Keep) until its page has painted
// the title: red #EF323D (black with Accessibility > Monochrome Logo, as the wiki describes the option), the logo and
// a progress bar. After StartFadeOut: 1 s solid while the bar fades, then 1 s fading over the screen beneath (vanilla
// fades into the title; the red value and these timings are unverified until a capture measures them).
class UNNAMED_API SMenuLoadingOverlay : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMenuLoadingOverlay) : _Monochrome(false), _RequestedGuiScale(0) {}
    SLATE_ARGUMENT(bool, Monochrome)
    // Options > GUI Scale (0 = Auto), as the HUD reads it.
    SLATE_ATTRIBUTE(int32, RequestedGuiScale)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    // The page's boot stands in for vanilla's resource reload: 0 made, 0.5 document loaded, 1 title painted. The bar
    // eases towards it by 5% of the gap every frame.
    void SetTargetProgress(float Target) { TargetProgress = FMath::Clamp(Target, 0.f, 1.f); }
    void StartFadeOut(double Now);
    // Seconds since StartFadeOut, or -1 before it: from 1 the screen beneath shows through, at 2 the overlay is done.
    double GetFade(double Now) const { return FadeStart < 0 ? -1 : Now - FadeStart; }
    bool IsFadedOut(double Now) const { return GetFade(Now) >= 2; }

    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
        bool bParentEnabled) const override;
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
    // mojangstudios.png as a texture (a brush naming the file draws a plain white box) and its two halves.
    TStrongObjectPtr<UTexture2D> LogoTexture;
    FSlateBrush LogoLeft, LogoRight;
    TAttribute<int32> RequestedGuiScale;
    bool bMonochrome = false;
    float TargetProgress = 0, Progress = 0;
    double FadeStart = -1;
};
