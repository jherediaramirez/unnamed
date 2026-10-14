#pragma once

#include "CoreMinimal.h"
#include "Hud/HudCreative.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"

class FFoundationBlockRegistry;
class FSlateWindowElementList;
class UHudAssets;
class UTexture2D;
struct FGeometry;

// The brushes and item facts a native container screen draws with (the creative and survival inventories): GUI
// textures by their reference path ("gui/container/inventory"), item sprites, the block icon atlas, the HUD's font
// and tooltip sprites.
struct UNNAMED_API FHudGuiArt
{
    TSharedPtr<const FFoundationBlockRegistry> Registry;
    TWeakObjectPtr<const UHudAssets> HudAssets;
    FHudCreativeItems Items;
    TMap<FString, FSlateBrush> Brushes;
    // A brush does not keep its texture alive; these do, so a garbage collection while the screen is open cannot
    // free them (it did once the held-item shots made the tour longer).
    TArray<TStrongObjectPtr<UTexture2D>> BrushTextures;
    FSlateBrush Atlas;
    FSlateBrush Solid;
    // Block animation tick for icons, from the clock at each paint (20 per second).
    mutable int64 Tick = 0;

    // The registry's icon atlas and the solid brush.
    void Init(TSharedPtr<const FFoundationBlockRegistry> InRegistry, TWeakObjectPtr<const UHudAssets> InHudAssets,
        FHudCreativeItems InItems);
    void Add(const FString& Id, UTexture2D* Texture);
    // An item sprite from ImportItems.py (ItemRoot/Gui) where it has one, else a texture from ImportBlocks.py
    // (BlockRoot/Gui).
    void LoadBlockGui(const FString& Id);
    // A texture from ImportContainerGui.py (GuiRoot/Container).
    void LoadContainerGui(const FString& Id);
    // The flat sprite of every item that is not a block.
    void LoadItemSprites();
    // The HUD's font and tooltip sprites.
    void LoadHudSprites();
    const FSlateBrush* Find(const FString& Id) const { return Brushes.Find(Id); }
    FString ItemName(FName Item) const;
    // The sprite of an item that is not an imported block: the tab icons and the 26.3 items (DA_Items), by the
    // item's first texture where it is known (the saplings' "block/oak_sapling").
    static FString ItemSprite(FName Item, const FHudCreativeItems& Items);
    static FSlateBrush MakeBrush(UTexture2D* Texture);
};

// Draws in GUI pixels of Units Slate units, relative to the viewport's top-left.
struct UNNAMED_API FHudGuiPainter
{
    const FHudGuiArt& Art;
    const FGeometry& Geometry;
    FSlateWindowElementList& Elements;
    int32& LayerId;
    float Units;

    void Box(const FSlateBrush* Brush, FIntPoint Position, FIntPoint Size,
        FBox2f UV = FBox2f(FVector2f(0), FVector2f(1)), FLinearColor Color = FLinearColor::White) const;
    // A nine-slice GUI sprite (tooltip background and frame): Border texels of a 100 x 100 sprite.
    void NineSlice(const FSlateBrush* Brush, FIntPoint Position, FIntPoint Size, float Border) const;
    int32 Measure(const FString& Text) const;
    // The default font's ASCII page, as the HUD draws it; the shadow is a quarter-bright copy one pixel down-right.
    void Text(const FString& Text, FIntPoint Point, FLinearColor Color, bool bShadow) const;
    // An item in a 16 x 16 slot area: a block as its 3D model, anything else as its flat sprite.
    void Item(FName Item, FIntPoint Position) const;
    // A stack with its durability bar and count.
    void Stack(const FHudCreativeStack& Stack, FIntPoint Position) const;
    // TooltipRenderingUtil: 12 right and 12 up of the cursor at Mouse, 3 pixels of padding, nine-sliced sprites.
    void Tooltip(const TArray<FHudTooltipLine>& Lines, FIntPoint Mouse) const;
};
