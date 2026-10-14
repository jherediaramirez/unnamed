#include "Hud/HudGuiPainter.h"
#include "Engine/Texture2D.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Hud/HudAssets.h"
#include "Hud/HudCreativeScreen.h"
#include "Hud/HudModel.h"
#include "Misc/PackageName.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UnnamedProjectSettings.h"

void FHudGuiArt::Init(TSharedPtr<const FFoundationBlockRegistry> InRegistry,
    TWeakObjectPtr<const UHudAssets> InHudAssets, FHudCreativeItems InItems)
{
    Registry = MoveTemp(InRegistry);
    HudAssets = InHudAssets;
    Items = MoveTemp(InItems);
    Solid = *FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const UFoundationBlockAssets* Assets = Registry ? Registry->GetAssets() : nullptr;
    if (Assets && Assets->IconAtlas) Atlas = MakeBrush(Assets->IconAtlas);
}

void FHudGuiArt::Add(const FString& Id, UTexture2D* Texture)
{
    if (!Texture) return;
    Brushes.Add(Id, MakeBrush(Texture));
    BrushTextures.Emplace(Texture);
}

void FHudGuiArt::LoadBlockGui(const FString& Id)
{
    const FSoftObjectPath ItemPath = UUnnamedProjectSettings::GetItemAssetPath(TEXT("Gui/") + Id);
    UTexture2D* Texture = (Id.StartsWith(TEXT("item/")) || Id.StartsWith(TEXT("block/"))) &&
            FPackageName::DoesPackageExist(ItemPath.GetLongPackageName())
        ? Cast<UTexture2D>(ItemPath.TryLoad())
        : nullptr;
    if (!Texture) Texture = Cast<UTexture2D>(UUnnamedProjectSettings::GetBlockAssetPath(TEXT("Gui/") + Id).TryLoad());
    Add(Id, Texture);
}

void FHudGuiArt::LoadContainerGui(const FString& Id)
{
    const FSoftObjectPath Path = UUnnamedProjectSettings::GetGuiAssetPath(TEXT("Container/") + Id);
    if (FPackageName::DoesPackageExist(Path.GetLongPackageName())) Add(Id, Cast<UTexture2D>(Path.TryLoad()));
}

void FHudGuiArt::LoadItemSprites()
{
    // Each sprite is tried once, also one that several items share and that is missing.
    TSet<FString> Tried;
    for (const TPair<FName, FHudCreativeItem>& Item : Items)
        if (!Registry || !Registry->FindBlock(Item.Key))
            if (const FString Id = ItemSprite(Item.Key, Items); !Brushes.Contains(Id) && !Tried.Contains(Id))
            {
                Tried.Add(Id);
                LoadBlockGui(Id);
            }
}

void FHudGuiArt::LoadHudSprites()
{
    if (const UHudAssets* Hud = HudAssets.Get())
        for (const FHudSpriteAsset& Sprite : Hud->Sprites)
        {
            const FString Id = Sprite.Id.ToString();
            if (Id == TEXT("font/ascii") || Id.StartsWith(TEXT("gui/sprites/tooltip/")))
                Add(Id, Sprite.Texture.LoadSynchronous());
        }
}

FString FHudGuiArt::ItemName(FName Item) const
{
    const FHudCreativeItem* Info = Items.Find(Item);
    return Info && !Info->Name.IsEmpty() ? Info->Name : Item.ToString();
}

FString FHudGuiArt::ItemSprite(FName Item, const FHudCreativeItems& Items)
{
    if (Item == TEXT("compass")) return TEXT("item/compass_16");
    if (const FHudCreativeItem* Info = Items.Find(Item); Info && !Info->Sprite.IsEmpty()) return Info->Sprite;
    return TEXT("item/") + Item.ToString();
}

FSlateBrush FHudGuiArt::MakeBrush(UTexture2D* Texture)
{
    FSlateBrush Brush;
    Brush.SetResourceObject(Texture);
    Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
    Brush.DrawAs = ESlateBrushDrawType::Image;
    return Brush;
}

void FHudGuiPainter::Box(
    const FSlateBrush* Brush, FIntPoint Position, FIntPoint Size, FBox2f UV, FLinearColor Color) const
{
    if (!Brush) return;
    FSlateBrush Copy = *Brush;
    Copy.SetUVRegion(FBox2D(FVector2D(UV.Min), FVector2D(UV.Max)));
    FSlateDrawElement::MakeBox(Elements, ++LayerId,
        Geometry.ToPaintGeometry(FVector2f(Size) * Units, FSlateLayoutTransform(FVector2f(Position) * Units)), &Copy,
        ESlateDrawEffect::None, Color);
}

void FHudGuiPainter::NineSlice(const FSlateBrush* Brush, FIntPoint Position, FIntPoint Size, float Border) const
{
    if (!Brush) return;
    FSlateBrush Copy = *Brush;
    Copy.DrawAs = ESlateBrushDrawType::Box;
    Copy.Margin = FMargin(Border / 100.f);
    Copy.ImageSize = FVector2D(100 * Units);
    FSlateDrawElement::MakeBox(Elements, ++LayerId,
        Geometry.ToPaintGeometry(FVector2f(Size) * Units, FSlateLayoutTransform(FVector2f(Position) * Units)), &Copy);
}

int32 FHudGuiPainter::Measure(const FString& Text) const
{
    int32 Width = 0;
    if (const UHudAssets* Assets = Art.HudAssets.Get())
        for (const TCHAR Character : Text)
            if (const FHudGlyph* Glyph = Assets->Glyphs.FindByPredicate(
                    [Character](const FHudGlyph& G) { return G.Code == int32(Character); }))
                Width += Glyph->Advance;
    return Width;
}

void FHudGuiPainter::Text(const FString& Text, FIntPoint Point, FLinearColor Color, bool bShadow) const
{
    const UHudAssets* Assets = Art.HudAssets.Get();
    const FSlateBrush* Font = Art.Find(TEXT("font/ascii"));
    if (!Assets || !Font) return;
    if (bShadow)
    {
        // As HudDrawList: Java quarters the encoded RGB bytes (white's shadow is 63, not linear quarter's 137).
        const FColor Encoded = Color.ToFColorSRGB();
        FLinearColor Shadow(FColor(Encoded.R / 4, Encoded.G / 4, Encoded.B / 4));
        Shadow.A = Color.A;
        this->Text(Text, Point + FIntPoint(1, 1), Shadow, false);
    }
    for (const TCHAR Character : Text)
        if (const FHudGlyph* Glyph =
                Assets->Glyphs.FindByPredicate([Character](const FHudGlyph& G) { return G.Code == int32(Character); }))
        {
            if (Glyph->InkWidth > 0)
                Box(Font, Point, {8, 8},
                    FBox2f(FVector2f(Glyph->Origin) / 128.f, FVector2f(Glyph->Origin + FIntPoint(8, 8)) / 128.f),
                    Color);
            Point.X += Glyph->Advance;
        }
}

void FHudGuiPainter::Item(FName Item, FIntPoint Position) const
{
    if (!Art.Registry ||
        !HudBlockIcon::Paint(
            Elements, LayerId, Geometry, Art.Atlas, *Art.Registry, Item, FVector2f(Position), Units, Art.Tick))
        Box(Art.Find(FHudGuiArt::ItemSprite(Item, Art.Items)), Position, {16, 16});
}

void FHudGuiPainter::Stack(const FHudCreativeStack& Stack, FIntPoint Position) const
{
    if (Stack.IsEmpty()) return;
    Item(Stack.Item, Position);
    // The durability bar, as the HUD's hotbar draws it.
    const FHudCreativeItem* Info = Art.Items.Find(Stack.Item);
    if (const FHudDurabilityBar Bar = HudModel::GetDurabilityBar(Stack.Damage, Info ? Info->MaxDamage : 0);
        Bar.bVisible)
    {
        const FBox2f Whole(FVector2f(0), FVector2f(1));
        Box(&Art.Solid, Position + FIntPoint(2, 13), {13, 2}, Whole, FLinearColor::Black);
        if (Bar.Width > 0) Box(&Art.Solid, Position + FIntPoint(2, 13), {Bar.Width, 1}, Whole, FLinearColor(Bar.Color));
    }
    if (Stack.Count > 1)
    {
        // ItemDecorations: the count at the slot's bottom right, white with a shadow.
        const FString Count = FString::FromInt(Stack.Count);
        Text(Count, Position + FIntPoint(17 - Measure(Count), 9), FLinearColor::White, true);
    }
}

void FHudGuiPainter::Tooltip(const TArray<FHudTooltipLine>& Lines, FIntPoint Mouse) const
{
    if (Lines.IsEmpty()) return;
    const FIntPoint At = Mouse + FIntPoint(12, -12);
    int32 Width = 0;
    for (const FHudTooltipLine& Line : Lines)
        Width = FMath::Max(Width, Measure(Line.Text));
    const FIntPoint Size(Width, HudTooltip::GetHeight(Lines.Num()));
    NineSlice(Art.Find(TEXT("gui/sprites/tooltip/background")), At - FIntPoint(12, 12), Size + FIntPoint(24, 24), 9);
    NineSlice(Art.Find(TEXT("gui/sprites/tooltip/frame")), At - FIntPoint(12, 12), Size + FIntPoint(24, 24), 10);
    for (int32 Line = 0; Line < Lines.Num(); ++Line)
        Text(Lines[Line].Text, At + FIntPoint(0, HudTooltip::GetLineY(Line)), FLinearColor(Lines[Line].Color), true);
}
