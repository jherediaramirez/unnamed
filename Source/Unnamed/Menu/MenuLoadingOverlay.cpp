#include "Menu/MenuLoadingOverlay.h"
#include "Hud/HudModel.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
    // The logo texture is addressed as 120 x 120 units whatever its size; each half is 120 x 60, nudged by 1/16 unit
    // so the halves meet without a seam.
    constexpr float MenuOverlayLogoTexels = 120.f;
    constexpr float MenuOverlayLogoNudge = .0625f;

    FSlateBrush MakeMenuOverlayLogoHalf(UTexture2D* Texture, bool bRight)
    {
        FSlateBrush Brush;
        Brush.SetResourceObject(Texture);
        Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
        Brush.DrawAs = ESlateBrushDrawType::Image;
        const float Nudge = (bRight ? MenuOverlayLogoNudge : -MenuOverlayLogoNudge) / MenuOverlayLogoTexels;
        Brush.SetUVRegion(FBox2f(FVector2f(Nudge, bRight ? .5f : 0.f), FVector2f(1.f + Nudge, bRight ? 1.f : .5f)));
        return Brush;
    }

    // One GUI-pixel rectangle, corners in either order.
    void PaintMenuOverlayRect(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry,
        float Units, FIntRect Rect, const FSlateBrush* Brush, const FLinearColor& Color)
    {
        const FIntPoint Min(FMath::Min(Rect.Min.X, Rect.Max.X), FMath::Min(Rect.Min.Y, Rect.Max.Y));
        const FIntPoint Max(FMath::Max(Rect.Min.X, Rect.Max.X), FMath::Max(Rect.Min.Y, Rect.Max.Y));
        if (Min.X == Max.X || Min.Y == Max.Y) return;
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
            Geometry.ToPaintGeometry(FVector2f(Max - Min) * Units, FSlateLayoutTransform(FVector2f(Min) * Units)),
            Brush, ESlateDrawEffect::None, Color);
    }
}

FMenuLoadingOverlayLayout FMenuLoadingOverlayLayout::Compute(FIntPoint Framebuffer, int32 RequestedGuiScale)
{
    FMenuLoadingOverlayLayout Layout;
    Layout.GuiScale = HudModel::GetScale(Framebuffer, RequestedGuiScale);
    Layout.GuiSize = HudModel::GetLogicalSize(Framebuffer, Layout.GuiScale);
    const int32 Width = Layout.GuiSize.X, Height = Layout.GuiSize.Y;
    const int32 CenterX = int32(Width * .5), CenterY = int32(Height * .5);
    const double LogoHeight = FMath::Min(Width * .75, double(Height)) * .25;
    const int32 HalfHeight = int32(LogoHeight * .5), HalfWidth = int32(LogoHeight * 4. * .5);
    const int32 Top = CenterY - HalfHeight, Bottom = Top + int32(LogoHeight);
    Layout.LogoLeft = FIntRect(CenterX - HalfWidth, Top, CenterX, Bottom);
    Layout.LogoRight = FIntRect(CenterX, Top, CenterX + HalfWidth, Bottom);
    const int32 BarY = int32(Height * .8325);
    Layout.Bar = FIntRect(Width / 2 - HalfWidth, BarY - 5, Width / 2 + HalfWidth, BarY + 5);
    return Layout;
}

void SMenuLoadingOverlay::Construct(const FArguments& InArgs)
{
    bMonochrome = InArgs._Monochrome;
    RequestedGuiScale = InArgs._RequestedGuiScale;
    // The same file the menu page and vanilla use (assets/minecraft/textures/gui/title/mojangstudios.png).
    const FString Path = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectContentDir() / TEXT("Menu/Web/assets/minecraft/textures/gui/title/mojangstudios.png"));
    if (UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Path))
    {
        LogoTexture.Reset(Texture);
        LogoLeft = MakeMenuOverlayLogoHalf(Texture, false);
        LogoRight = MakeMenuOverlayLogoHalf(Texture, true);
    }
}

void SMenuLoadingOverlay::StartFadeOut(double Now)
{
    if (FadeStart < 0) FadeStart = Now;
}

void SMenuLoadingOverlay::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    Progress = FMath::Clamp(Progress * .95f + TargetProgress * .05f, 0.f, 1.f);
}

int32 SMenuLoadingOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
    const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
    const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const float SlateScale = FMath::Max(AllottedGeometry.GetAccumulatedLayoutTransform().GetScale(), .01f);
    const FVector2f Pixels = FVector2f(AllottedGeometry.GetLocalSize()) * SlateScale;
    const FMenuLoadingOverlayLayout Layout = FMenuLoadingOverlayLayout::Compute(
        FIntPoint(FMath::FloorToInt32(Pixels.X), FMath::FloorToInt32(Pixels.Y)), RequestedGuiScale.Get());
    const float Units = Layout.GuiScale / SlateScale;
    const double Fade = GetFade(FPlatformTime::Seconds());
    // Solid until 1 s after the fade starts, then the background and the logo fade out together over the next second.
    const float Fading = Fade >= 1 ? 1.f - FMath::Clamp(float(Fade) - 1.f, 0.f, 1.f) : 1.f;
    const FSlateBrush* Solid = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    FLinearColor Background = bMonochrome ? FLinearColor::Black : FLinearColor(FColor(239, 50, 61));
    Background.A = FMath::CeilToFloat(Fading * 255.f) / 255.f;
    FSlateDrawElement::MakeBox(
        OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Solid, ESlateDrawEffect::None, Background);
    const FLinearColor Logo(1.f, 1.f, 1.f, Fading);
    if (LogoTexture.IsValid())
    {
        PaintMenuOverlayRect(OutDrawElements, LayerId + 1, AllottedGeometry, Units, Layout.LogoLeft, &LogoLeft, Logo);
        PaintMenuOverlayRect(OutDrawElements, LayerId + 1, AllottedGeometry, Units, Layout.LogoRight, &LogoRight, Logo);
    }
    if (Fade < 1)
    {
        // The bar fades out over the fade's first second: a 1-pixel outline and a fill 2 pixels inside it.
        const FLinearColor Bar(
            1.f, 1.f, 1.f, FMath::RoundToFloat((1.f - FMath::Clamp(float(Fade), 0.f, 1.f)) * 255.f) / 255.f);
        const int32 X0 = Layout.Bar.Min.X, Y0 = Layout.Bar.Min.Y, X1 = Layout.Bar.Max.X, Y1 = Layout.Bar.Max.Y;
        const int32 Filled = FMath::CeilToInt32(float(X1 - X0 - 2) * Progress);
        for (const FIntRect& Rect :
            {FIntRect(X0 + 2, Y0 + 2, X0 + Filled, Y1 - 2), FIntRect(X0 + 1, Y0, X1 - 1, Y0 + 1),
                FIntRect(X0 + 1, Y1, X1 - 1, Y1 - 1), FIntRect(X0, Y0, X0 + 1, Y1), FIntRect(X1, Y0, X1 - 1, Y1)})
            PaintMenuOverlayRect(OutDrawElements, LayerId + 1, AllottedGeometry, Units, Rect, Solid, Bar);
    }
    return LayerId + 1;
}
