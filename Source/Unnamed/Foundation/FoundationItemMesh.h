#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationActions.h"

struct FFoundationItemDisplayAsset;
struct FFoundationItemTextureAsset;
class UStaticMesh;

// One quad of an extruded item sprite (26.3's builtin/generated), in Mojang model pixels: X east, Y up, Z south
// (right-handed). The sprite spans 0-16 in X and Y whatever its size; the front at Z 8.5 faces +Z, the back at 7.5.
// Corners wind counter-clockwise seen from outside; FoundationItemMesh::ToUnreal's mirror turns that into the
// clockwise winding Unreal draws.
struct FFoundationItemQuad
{
    FVector3f Positions[4];
    // Sprite UVs, (0, 0) at the top left.
    FVector2f UVs[4];
    FIntVector Normal;
};

// One extruded sprite of a held item's mesh: the quads, the sprite's array layer and the sRGB colour that multiplies
// it (white: none; the leather dye on a model's first layer).
struct FFoundationItemMeshLayer
{
    TArray<FFoundationItemQuad> Quads;
    int32 Layer = 0;
    FColor Tint = FColor::White;
};

// One box of an entity-style model (the shield), in the entity's own pixels about its origin: Min corner and Size, and
// the offset of its Minecraft "box UV" unwrapping in the entity texture.
struct FFoundationItemBox
{
    FVector3f Min;
    FVector3f Size;
    FIntPoint TextureOffset;
};

struct FMeshDescription;

namespace FoundationItemMesh
{
    // The shield's stand-in geometry: the plate (12 x 22 x 1, texture offset 0, 0) and the handle (2 x 6 x 6, offset
    // 26, 0). PROVISIONAL: sizes and offsets were read from the texture's opaque layout and the placement (plate z -2
    // to -1, handle z -1 to 5, both centred on x and y) is a guess, until the real model is read.
    TArray<FFoundationItemBox> GetShieldBoxes();
    // Six quads per box. Entity space is Y down and the front faces -Z; Scale (the item definition's transformation
    // scale, (1, -1, -1) for the shield) is applied to the geometry about the entity origin, then the quads are in the
    // item-model space ToUnreal takes (origin at model pixel 0, as vanilla draws a special model). UVs are the box
    // UV's pixels over TextureSize, (0, 0) at the top left.
    TArray<FFoundationItemQuad> BuildBoxQuads(
        TConstArrayView<FFoundationItemBox> Boxes, const FIntPoint& TextureSize, const FVector3f& Scale);
    // The opacity mask as one flag per pixel, rows from the top; false when a row is missing, too short or not hex.
    bool ReadMask(const FFoundationItemTextureAsset& Texture, TArray<bool>& Opaque);
    // A front and a back quad of the whole sprite one model pixel apart (the cutout material discards transparent
    // texels), and one side face per opaque pixel edge that borders a transparent pixel or the sprite's border. A
    // side samples its pixel's centre, so nearest filtering never bleeds a neighbour into it.
    // Grow (model pixels) thickens the slab on both faces: a layer over another (the model's second layer) grows by
    // 0.001 so it draws over the first from either side.
    TArray<FFoundationItemQuad> BuildQuads(int32 Width, int32 Height, TConstArrayView<bool> Opaque, float Grow = 0);
    // Model pixels to Unreal cm about the model centre (8, 8, 8): Mojang (x, y, z) is Unreal (-z, x, y), BlockCm
    // per 16 pixels.
    FVector3f ToUnreal(const FVector3f& Pixels, double BlockCm);
    FVector3f ToUnrealDirection(const FIntVector& Direction);
    // A display context's transform (Mojang units, verbatim) on the centred mesh: scale, then rotation, then
    // translation. The rotation is Mojang's Rx * Ry * Rz (Z applied first), provisional until probe P1 confirms it.
    FTransform MakeDisplayTransform(const FFoundationItemDisplayAsset& Display, double BlockCm);
    // A transient mesh of the quads, one material slot; UV 1's X carries the sprite's array layer. Null without
    // quads.
    UStaticMesh* BuildMesh(UObject* Outer, TConstArrayView<FFoundationItemQuad> Quads, int32 Layer, double BlockCm);
    // The same for several layers drawn in one mesh; a tinted layer's vertices carry its tint as their colour (the
    // held item material multiplies it in), an untinted one's keep the default white.
    UStaticMesh* BuildMesh(UObject* Outer, TConstArrayView<FFoundationItemMeshLayer> Layers, double BlockCm);
    // The mesh description BuildMesh builds from.
    void FillMeshDescription(FMeshDescription& Mesh, TConstArrayView<FFoundationItemMeshLayer> Layers, double BlockCm);
}

// Where each hand holds its item in first person: the ONE place a later first-person body rig replaces. The item's
// display context, and the pivot (cm, view space) the item hangs from; the offhand mirrors the main hand across the
// view's centre (Y negated). The main hand's pivot is the fitted one (foundation.Hand.Fit; HandFit) or the old rest.
namespace FoundationHeldPose
{
    struct FPose
    {
        FName Context;
        FVector Pivot;
        // +1 main, -1 offhand: the sign of the pivot's Y (the right-hand side is +Y).
        double Mirror;
    };

    // A drawn bow's pose (step 19c), Draw 0 to 1 over the 20-tick draw: R18's right-arm trace moves the item from
    // (0.2807, -0.3368, -0.5637) at tick 0 to (0.3028, -0.3309, -0.5325) at tick 20
    // (survival2.r18.held_item_transforms, partial tick 0), 1 unit = 100 cm: back 3.12, right 2.21, up 0.59, lerped
    // (provisional; no scale, no shake).
    constexpr double DrawBackCm = 3.12, DrawRightCm = 2.21, DrawUpCm = .59;

    inline FPose Get(EFoundationHand Hand, bool bFit, double Draw = 0)
    {
        const FVector Main = (bFit ? FVector(71.9, 56.2, -52.2) : FVector(42, 24, -24)) +
            FVector(-DrawBackCm, DrawRightCm, DrawUpCm) * FMath::Clamp(Draw, 0., 1.);
        if (Hand == EFoundationHand::Off) return {TEXT("firstperson_lefthand"), FVector(Main.X, -Main.Y, Main.Z), -1.};
        return {TEXT("firstperson_righthand"), Main, 1.};
    }
}
