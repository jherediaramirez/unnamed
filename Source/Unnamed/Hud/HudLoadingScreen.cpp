#include "Hud/HudLoadingScreen.h"
#include "Engine/Texture2D.h"
#include "Hud/HudAssets.h"
#include "Hud/HudCreativeScreen.h"
#include "Hud/HudDrawList.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
    // The screen's text, 26.3 en_us "multiplayer.downloadingTerrain".
    const TCHAR* LoadingScreenTitle = TEXT("Loading terrain...");
    // The menu's own copy of the title panorama's first face.
    const TCHAR* LoadingScreenPanorama = TEXT("Menu/Web/assets/minecraft/textures/gui/title/background/panorama_0.png");

    constexpr int32 LoadingCellGui = 2;
    constexpr int32 LoadingBarWidth = 200;
    constexpr int32 LoadingBarHeight = 2;
    constexpr int32 LoadingBarGap = 12;
    constexpr int32 LoadingTextGap = 12;

    FSlateBrush LoadingBrush(UTexture2D* Texture)
    {
        FSlateBrush Brush;
        Brush.SetResourceObject(Texture);
        Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
        Brush.DrawAs = ESlateBrushDrawType::Image;
        return Brush;
    }
}

void SHudLoadingScreen::Construct(const FArguments& Arguments)
{
    HudAssets = Arguments._HudAssets;
    BlurPixels = Arguments._BlurPixels;
    RequestedGuiScale = Arguments._RequestedGuiScale;
    Solid = *FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const FString Path = FPaths::ProjectContentDir() / LoadingScreenPanorama;
    if (UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Path))
    {
        PanoramaTexture.Reset(Texture);
        Panorama = LoadingBrush(Texture);
    }
    if (const UHudAssets* Hud = HudAssets.Get())
        for (const FHudSpriteAsset& Sprite : Hud->Sprites)
            if (Sprite.Id == TEXT("font/ascii"))
                if (UTexture2D* Texture = Sprite.Texture.LoadSynchronous())
                {
                    FontTexture.Reset(Texture);
                    Font = LoadingBrush(Texture);
                }
}

int32 SHudLoadingScreen::GetScaleFor(FVector2f ViewportPixels) const
{
    return SHudCreativeScreen::GetGuiScale(ViewportPixels, RequestedGuiScale.Get());
}

int32 SHudLoadingScreen::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
    const float SlateScale = FMath::Max(Geometry.GetAccumulatedLayoutTransform().GetScale(), .01f);
    const FVector2f Pixels = FVector2f(Geometry.GetLocalSize()) * SlateScale;
    const int32 Scale = GetScaleFor(Pixels);
    LastFrameScale = Scale;
    const float Units = float(Scale) / SlateScale;
    const FIntPoint Gui(int32(Pixels.X) / Scale, int32(Pixels.Y) / Scale);

    auto Box = [&](const FSlateBrush* Brush, FIntPoint Position, FIntPoint Size, FLinearColor Color,
                   FBox2f UV = FBox2f(FVector2f(0), FVector2f(1)))
    {
        FSlateBrush Copy = *Brush;
        Copy.SetUVRegion(FBox2D(FVector2D(UV.Min), FVector2D(UV.Max)));
        FSlateDrawElement::MakeBox(Elements, ++LayerId,
            Geometry.ToPaintGeometry(FVector2f(Size) * Units, FSlateLayoutTransform(FVector2f(Position) * Units)),
            &Copy, ESlateDrawEffect::None, Color);
    };

    // Background: the panorama face cover-fitted into a box 3% larger than the screen on every side (a square image:
    // its side is the box's longer side), centred.
    const FVector2f Local = FVector2f(Geometry.GetLocalSize());
    if (Panorama.GetResourceObject())
    {
        const float Side = FMath::Max(Local.X, Local.Y) * 1.06f;
        FSlateBrush Copy = Panorama;
        FSlateDrawElement::MakeBox(Elements, ++LayerId,
            Geometry.ToPaintGeometry(FVector2f(Side), FSlateLayoutTransform((Local - FVector2f(Side)) * .5f)), &Copy);
    }
    else
        FSlateDrawElement::MakeBox(Elements, ++LayerId, Geometry.ToPaintGeometry(), &Solid, ESlateDrawEffect::None,
            FLinearColor(.05f, .05f, .05f, 1.f));
    // CSS blur(N px): a Gaussian of standard deviation N pixels over what is drawn so far (the panorama), with
    // SBackgroundBlur's kernel and downsample rules.
    if (BlurPixels > 0 && Local.GetMin() > 0)
    {
        const FSlateRect Bounds = Geometry.GetRenderBoundingRect();
        FPaintGeometry PaintGeometry(
            Bounds.GetTopLeft(), Bounds.GetSize(), Geometry.GetAccumulatedLayoutTransform().GetScale());
        int32 Width = FMath::RoundToInt(Bounds.GetSize().X);
        int32 Height = FMath::RoundToInt(Bounds.GetSize().Y);
        int32 Kernel = FMath::RoundToInt(BlurPixels * 3.f);
        int32 Downsample = 0;
        if (Kernel > 9)
        {
            Downsample = Kernel >= 64 ? (Kernel >= 96 ? 4 : 3) : 2;
            Kernel /= Downsample;
        }
        float Strength = FMath::Max(.5f, BlurPixels);
        if (Downsample > 0)
        {
            Width = FMath::DivideAndRoundUp(Width, Downsample);
            Height = FMath::DivideAndRoundUp(Height, Downsample);
            Strength /= Downsample;
        }
        if (Width > 0 && Height > 0)
        {
            Elements.PushClip(FSlateClippingZone(Geometry));
            FSlateDrawElement::MakePostProcessBlur(Elements, ++LayerId, PaintGeometry,
                FVector4f(float(Kernel), Strength, float(Width), float(Height)), Downsample, FVector4f(0));
            Elements.PopClip();
        }
    }
    // menu_background.png: a black texel at alpha 64/255, tiled.
    Box(&Solid, {0, 0}, FIntPoint(Gui.X + 1, Gui.Y + 1), FLinearColor(0.f, 0.f, 0.f, 64.f / 255.f));

    // Layout in GUI pixels: the map centred, the bar 12 above it, the text 12 above the bar.
    const int32 MapGui = State.MapSize * LoadingCellGui;
    const FIntPoint MapMin((Gui.X - MapGui) / 2, (Gui.Y - MapGui) / 2);
    const FIntPoint BarMin((Gui.X - LoadingBarWidth) / 2, MapMin.Y - LoadingBarGap - LoadingBarHeight);
    // Rows from the minimum corner: +X to the right, +Y down.
    if (State.MapSize > 0 && State.Map.Num() == State.MapSize * State.MapSize)
        for (int32 Y = 0; Y < State.MapSize; ++Y)
            for (int32 X = 0; X < State.MapSize; ++X)
                Box(&Solid, MapMin + FIntPoint(X, Y) * LoadingCellGui, {LoadingCellGui, LoadingCellGui},
                    FLinearColor::FromSRGBColor(State.Map[Y * State.MapSize + X]));
    Box(&Solid, BarMin, {LoadingBarWidth, LoadingBarHeight}, FLinearColor::Black);
    const int32 Fill = FMath::RoundToInt(LoadingBarWidth * FMath::Clamp(State.Progress, 0.f, 1.f));
    if (Fill > 0) Box(&Solid, BarMin, {Fill, LoadingBarHeight}, FLinearColor::FromSRGBColor(FColor(0x00, 0xFF, 0x00)));

    // The title in the HUD's bitmap font, white with vanilla's shadow (a quarter of the encoded RGB, 1 pixel
    // down-right).
    const UHudAssets* Assets = HudAssets.Get();
    if (Assets && Font.GetResourceObject())
    {
        const FString Title = LoadingScreenTitle;
        const int32 Width = HudDrawList::MeasureText(Title, Assets->Glyphs);
        const FIntPoint Origin((Gui.X - Width) / 2, BarMin.Y - LoadingTextGap);
        for (int32 Pass = 0; Pass < 2; ++Pass)
        {
            FIntPoint Point = Origin + (Pass == 0 ? FIntPoint(1, 1) : FIntPoint::ZeroValue);
            const FLinearColor Color =
                Pass == 0 ? FLinearColor::FromSRGBColor(FColor(63, 63, 63)) : FLinearColor::White;
            for (const TCHAR Character : Title)
                if (const FHudGlyph* Glyph = Assets->Glyphs.FindByPredicate(
                        [Character](const FHudGlyph& G) { return G.Code == int32(Character); }))
                {
                    if (Glyph->InkWidth > 0)
                        Box(&Font, Point, {8, 8}, Color,
                            FBox2f(
                                FVector2f(Glyph->Origin) / 128.f, FVector2f(Glyph->Origin + FIntPoint(8, 8)) / 128.f));
                    Point.X += Glyph->Advance;
                }
        }
    }
    return LayerId;
}
