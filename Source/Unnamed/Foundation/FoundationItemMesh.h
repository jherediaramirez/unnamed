#pragma once

#include "CoreMinimal.h"

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

namespace FoundationItemMesh
{
    // The opacity mask as one flag per pixel, rows from the top; false when a row is missing, too short or not hex.
    bool ReadMask(const FFoundationItemTextureAsset& Texture, TArray<bool>& Opaque);
    // A front and a back quad of the whole sprite one model pixel apart (the cutout material discards transparent
    // texels), and one side face per opaque pixel edge that borders a transparent pixel or the sprite's border. A
    // side samples its pixel's centre, so nearest filtering never bleeds a neighbour into it.
    TArray<FFoundationItemQuad> BuildQuads(int32 Width, int32 Height, TConstArrayView<bool> Opaque);
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
}
