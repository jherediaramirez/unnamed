#include "Misc/AutomationTest.h"
#include "Algo/Count.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationItemMesh.h"
#include "Misc/PackageName.h"
#include "UnnamedProjectSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags ItemMeshTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // A 16 x 16 mask with the given pixels opaque (column, row from the top).
    TArray<bool> MakeMask(std::initializer_list<FIntPoint> Pixels, int32 Size = 16)
    {
        TArray<bool> Opaque;
        Opaque.Init(false, Size * Size);
        for (const FIntPoint& Pixel : Pixels)
            Opaque[Pixel.Y * Size + Pixel.X] = true;
        return Opaque;
    }

    TArray<bool> FullMask(int32 Size = 16)
    {
        TArray<bool> Opaque;
        Opaque.Init(true, Size * Size);
        return Opaque;
    }

    // Model pixels in quarters: every corner of a 16 or 32 pixel sprite and both faces (7.5, 8.5) land on integers.
    FIntVector Key(const FVector3f& Position)
    {
        return FIntVector(
            FMath::RoundToInt(Position.X * 4), FMath::RoundToInt(Position.Y * 4), FMath::RoundToInt(Position.Z * 4));
    }

    // The surface's directed edges: the opaque pixels' front and back squares (what the cutout keeps of the two whole
    // quads) and every side quad, each wound counter-clockwise seen from outside.
    TMap<TPair<FIntVector, FIntVector>, int32> CountEdges(
        int32 Width, int32 Height, TConstArrayView<bool> Opaque, TConstArrayView<FFoundationItemQuad> Quads)
    {
        TMap<TPair<FIntVector, FIntVector>, int32> Edges;
        auto Loop = [&Edges](const FVector3f(&Corners)[4])
        {
            for (int32 Corner = 0; Corner < 4; ++Corner)
                ++Edges.FindOrAdd({Key(Corners[Corner]), Key(Corners[(Corner + 1) % 4])});
        };
        const float PixelX = 16.f / Width, PixelY = 16.f / Height;
        for (int32 Y = 0; Y < Height; ++Y)
            for (int32 X = 0; X < Width; ++X)
                if (Opaque[Y * Width + X])
                {
                    const float X0 = X * PixelX, X1 = X0 + PixelX, Y1 = 16 - Y * PixelY, Y0 = Y1 - PixelY;
                    Loop({{X0, Y0, 8.5f}, {X1, Y0, 8.5f}, {X1, Y1, 8.5f}, {X0, Y1, 8.5f}});
                    Loop({{X0, Y0, 7.5f}, {X0, Y1, 7.5f}, {X1, Y1, 7.5f}, {X1, Y0, 7.5f}});
                }
        for (int32 Index = 2; Index < Quads.Num(); ++Index)
            Loop(Quads[Index].Positions);
        return Edges;
    }

    // Closed and consistently wound: each directed edge is matched by as many running the other way. Most edges are
    // used once each way; a corner where two opaque pixels touch only diagonally uses its depth edge twice each way.
    bool IsClosed(int32 Width, int32 Height, TConstArrayView<bool> Opaque, TConstArrayView<FFoundationItemQuad> Quads,
        int32* Shared = nullptr)
    {
        const TMap<TPair<FIntVector, FIntVector>, int32> Edges = CountEdges(Width, Height, Opaque, Quads);
        if (Shared) *Shared = 0;
        for (const auto& [Edge, Count] : Edges)
        {
            const int32* Back = Edges.Find({Edge.Value, Edge.Key});
            if (!Back || *Back != Count || Count > 2) return false;
            if (Shared && Count == 2) ++*Shared;
        }
        return true;
    }

    // Every quad winds counter-clockwise seen from outside in model space, so clockwise in Unreal's (the mirror).
    bool IsWound(TConstArrayView<FFoundationItemQuad> Quads)
    {
        for (const FFoundationItemQuad& Quad : Quads)
        {
            const FVector3f* P = Quad.Positions;
            if (FVector3f::CrossProduct(P[1] - P[0], P[2] - P[0]).Dot(FVector3f(Quad.Normal)) <= 0) return false;
            FVector3f U[4];
            for (int32 Corner = 0; Corner < 4; ++Corner)
                U[Corner] = FoundationItemMesh::ToUnreal(P[Corner], 100);
            if (FVector3f::CrossProduct(U[1] - U[0], U[2] - U[0])
                    .Dot(FoundationItemMesh::ToUnrealDirection(Quad.Normal)) >= 0)
                return false;
        }
        return true;
    }

    // Front and back span the sprite, one model pixel apart about Z 8, and give a position the same UV on both.
    bool AreFacesMirrored(TConstArrayView<FFoundationItemQuad> Quads)
    {
        if (Quads.Num() < 2 || Quads[0].Normal != FIntVector(0, 0, 1) || Quads[1].Normal != FIntVector(0, 0, -1))
            return false;
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            const FVector3f& Front = Quads[0].Positions[Corner];
            const FVector2f Expected(Front.X / 16, 1 - Front.Y / 16);
            if (Front.Z != 8.5f || (Front.X != 0 && Front.X != 16) || (Front.Y != 0 && Front.Y != 16) ||
                !Quads[0].UVs[Corner].Equals(Expected, 0))
                return false;
            bool bMirrored = false;
            for (int32 Other = 0; Other < 4; ++Other)
                bMirrored |= Quads[1].Positions[Other] == FVector3f(Front.X, Front.Y, 16 - Front.Z) &&
                    Quads[1].UVs[Other] == Quads[0].UVs[Corner];
            if (!bMirrored) return false;
        }
        return true;
    }

    // A side samples one texel's centre: the opaque pixel behind the face (half a pixel against its normal).
    bool AreSidesSampledInside(
        int32 Width, int32 Height, TConstArrayView<bool> Opaque, TConstArrayView<FFoundationItemQuad> Quads)
    {
        for (int32 Index = 2; Index < Quads.Num(); ++Index)
        {
            const FFoundationItemQuad& Quad = Quads[Index];
            const FVector2f UV = Quad.UVs[0];
            if (Quad.UVs[1] != UV || Quad.UVs[2] != UV || Quad.UVs[3] != UV) return false;
            const int32 X = FMath::FloorToInt(UV.X * Width), Y = FMath::FloorToInt(UV.Y * Height);
            if (X < 0 || Y < 0 || X >= Width || Y >= Height || !Opaque[Y * Width + X] ||
                !FMath::IsNearlyEqual(UV.X * Width - X, .5f) || !FMath::IsNearlyEqual(UV.Y * Height - Y, .5f))
                return false;
            const FVector3f Centre =
                (Quad.Positions[0] + Quad.Positions[1] + Quad.Positions[2] + Quad.Positions[3]) / 4.f;
            const float PixelX = 16.f / Width, PixelY = 16.f / Height;
            const FVector3f Inside =
                Centre - FVector3f(Quad.Normal.X * PixelX / 2, Quad.Normal.Y * PixelY / 2, Quad.Normal.Z);
            if (FMath::FloorToInt(Inside.X / PixelX) != X || FMath::FloorToInt((16 - Inside.Y) / PixelY) != Y)
                return false;
        }
        return true;
    }

    // Mojang's display transform written out: v = t / 16 + Rx(a) Ry(b) Rz(c) (s * (p - 8) / 16), right-handed, then
    // the axis map (x, y, z) to Unreal (-z, x, y).
    FVector DirectDisplay(const FFoundationItemDisplayAsset& Display, const FVector& Pixels, double BlockCm)
    {
        // The rows of the right-handed rotation matrices about X, Y and Z.
        auto Turn = [](int32 Axis, double Degrees, const FVector& V)
        {
            const double C = FMath::Cos(FMath::DegreesToRadians(Degrees));
            const double S = FMath::Sin(FMath::DegreesToRadians(Degrees));
            return Axis == 0 ? FVector(V.X, C * V.Y - S * V.Z, S * V.Y + C * V.Z)
                : Axis == 1  ? FVector(C * V.X + S * V.Z, V.Y, -S * V.X + C * V.Z)
                             : FVector(C * V.X - S * V.Y, S * V.X + C * V.Y, V.Z);
        };
        FVector V = Display.Scale * (Pixels - FVector(8)) / 16;
        V = Turn(2, Display.Rotation.Z, V);
        V = Turn(1, Display.Rotation.Y, V);
        V = Turn(0, Display.Rotation.X, V);
        V += Display.Translation / 16;
        return FVector(-V.Z, V.X, V.Y) * BlockCm;
    }

    FFoundationItemDisplayAsset MakeDisplay(FVector Rotation, FVector Translation, FVector Scale)
    {
        FFoundationItemDisplayAsset Display;
        Display.Context = TEXT("firstperson_righthand");
        Display.Rotation = Rotation;
        Display.Translation = Translation;
        Display.Scale = Scale;
        return Display;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsMeshTest, "Unnamed.Foundation.Items.Mesh", ItemMeshTestFlags)

bool FFoundationItemsMeshTest::RunTest(const FString&)
{
    using namespace FoundationItemMesh;
    // Known masks: front and back always, plus one side per exposed pixel edge.
    struct FKnown
    {
        const TCHAR* Name;
        TArray<bool> Opaque;
        int32 Quads;
        int32 Size = 16;
    };
    const FKnown Known[] = {
        {TEXT("one pixel"), MakeMask({{5, 7}}), 6},
        {TEXT("full 16 x 16"), FullMask(), 66},
        {TEXT("a 2 x 1 bar"), MakeMask({{3, 3}, {4, 3}}), 8},
        {TEXT("a 3 x 3 ring"), MakeMask({{7, 7}, {8, 7}, {9, 7}, {7, 8}, {9, 8}, {7, 9}, {8, 9}, {9, 9}}), 18},
        {TEXT("empty"), MakeMask({}), 2},
        {TEXT("two pixels touching at a corner"), MakeMask({{2, 2}, {3, 3}}), 10},
        {TEXT("one pixel of 32 x 32"), MakeMask({{0, 31}}, 32), 6, 32},
        {TEXT("full 32 x 32"), FullMask(32), 130, 32},
    };
    for (const FKnown& Case : Known)
    {
        const TArray<FFoundationItemQuad> Quads = BuildQuads(Case.Size, Case.Size, Case.Opaque);
        int32 Shared = 0;
        TestEqual(FString::Printf(TEXT("%s: quads"), Case.Name), Quads.Num(), Case.Quads);
        TestTrue(FString::Printf(TEXT("%s: closed and consistently wound"), Case.Name),
            IsClosed(Case.Size, Case.Size, Case.Opaque, Quads, &Shared));
        TestEqual(FString::Printf(TEXT("%s: edges shared by four faces"), Case.Name), Shared,
            FCString::Strstr(Case.Name, TEXT("corner")) ? 2 : 0);
        TestTrue(FString::Printf(TEXT("%s: front and back mirror"), Case.Name), AreFacesMirrored(Quads));
        TestTrue(FString::Printf(TEXT("%s: sides sample their own opaque texel"), Case.Name),
            AreSidesSampledInside(Case.Size, Case.Size, Case.Opaque, Quads));
        TestTrue(FString::Printf(TEXT("%s: wound for Unreal"), Case.Name), IsWound(Quads));
    }
    {
        // A 32 x 32 pixel is half a model pixel wide but the layer stays one model pixel deep.
        const TArray<FFoundationItemQuad> Quads = BuildQuads(32, 32, MakeMask({{0, 31}}, 32));
        FBox3f Side(ForceInit);
        for (int32 Index = 2; Index < Quads.Num(); ++Index)
            for (const FVector3f& Corner : Quads[Index].Positions)
                Side += Corner;
        TestTrue(TEXT("a 32 x 32 pixel's sides: 0.5 wide, 7.5 to 8.5 deep, at the bottom left"),
            Side.Min == FVector3f(0, 0, 7.5f) && Side.Max == FVector3f(.5f, .5f, 8.5f));
    }
    TestTrue(TEXT("a mask that does not match its size builds nothing"), BuildQuads(16, 16, MakeMask({}, 4)).IsEmpty());

    // The mask format: one hex string per row, top first, the leftmost pixel in the highest bit.
    FFoundationItemTextureAsset Texture;
    Texture.Width = Texture.Height = 4;
    Texture.Mask = {TEXT("8"), TEXT("1"), TEXT("0"), TEXT("f")};
    TArray<bool> Opaque;
    TestTrue(TEXT("read a 4 x 4 mask"),
        ReadMask(Texture, Opaque) && Opaque.Num() == 16 && Opaque[0] && !Opaque[1] && Opaque[7] && !Opaque[4] &&
            !Opaque[8] && Opaque[12] && Opaque[15]);
    Texture.Mask[2] = TEXT("g");
    TestFalse(TEXT("a mask row that is not hex is refused"), ReadMask(Texture, Opaque));
    Texture.Mask[2] = TEXT("00");
    TestFalse(TEXT("a mask row of the wrong length is refused"), ReadMask(Texture, Opaque));

    // Model space to Unreal: east is right, up is up, south is back (toward the viewer), about the model's centre.
    TestTrue(TEXT("axis map about (8, 8, 8)"),
        ToUnreal(FVector3f(8), 100).IsNearlyZero() && ToUnreal(FVector3f(16, 8, 8), 100).Equals(FVector3f(0, 50, 0)) &&
            ToUnreal(FVector3f(8, 16, 8), 100).Equals(FVector3f(0, 0, 50)) &&
            ToUnreal(FVector3f(8, 8, 16), 100).Equals(FVector3f(-50, 0, 0)) &&
            ToUnrealDirection(FIntVector(0, 0, 1)) == FVector3f(-1, 0, 0));

    // Display transforms against the written-out Mojang math: item/generated's first person, the spear's, and one
    // with distinct values on every axis.
    const FFoundationItemDisplayAsset Displays[] = {
        MakeDisplay({0, -90, 25}, {1.13, 3.2, 1.13}, FVector(.68)),
        MakeDisplay({-20, 90, -35}, {3.13, 2, .13}, {1.36, 1.36, .68}),
        MakeDisplay({30, 45, 60}, {1, -2, 3}, {1, 2, 3}),
    };
    for (const FFoundationItemDisplayAsset& Display : Displays)
    {
        const FTransform Transform = MakeDisplayTransform(Display, 50);
        double Worst = 0;
        for (const FVector& Point : {FVector(0, 0, 0), FVector(16, 0, 0), FVector(0, 16, 0), FVector(0, 0, 16),
                 FVector(16, 16, 16), FVector(3, 11, 5), FVector(8, 8, 8)})
            Worst = FMath::Max(Worst,
                FVector::Dist(Transform.TransformPosition(FVector(ToUnreal(FVector3f(Point), 50))),
                    DirectDisplay(Display, Point, 50)));
        TestTrue(FString::Printf(TEXT("display %s: matches Mojang's matrices (worst %.6f cm)"),
                     *Display.Rotation.ToString(), Worst),
            Worst < 1e-3);
    }

    // DA_Items: every sprite's mask builds a closed, mirrored, consistently wound mesh, and every item resolves its
    // first-person model.
    const UFoundationItemAssets* Assets = UFoundationItemAssets::LoadDefault();
    if (!TestNotNull(TEXT("DA_Items"), Assets)) return false;
    int32 Good = 0, Shared = 0;
    for (const FFoundationItemTextureAsset& Sprite : Assets->Textures)
    {
        int32 Corners = 0;
        const bool bRead = ReadMask(Sprite, Opaque);
        const TArray<FFoundationItemQuad> Quads = BuildQuads(Sprite.Width, Sprite.Height, Opaque);
        int32 Exposed = 0;
        for (int32 Y = 0; bRead && Y < Sprite.Height; ++Y)
            for (int32 X = 0; X < Sprite.Width; ++X)
                if (Opaque[Y * Sprite.Width + X])
                    for (const FIntPoint Step : {FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(0, 1)})
                    {
                        const FIntPoint N(X + Step.X, Y + Step.Y);
                        Exposed += N.X < 0 || N.Y < 0 || N.X >= Sprite.Width || N.Y >= Sprite.Height ||
                            !Opaque[N.Y * Sprite.Width + N.X];
                    }
        const bool bGood = bRead && Algo::Count(Opaque, true) == Sprite.OpaquePixels && Quads.Num() == 2 + Exposed &&
            IsClosed(Sprite.Width, Sprite.Height, Opaque, Quads, &Corners) && AreFacesMirrored(Quads) &&
            AreSidesSampledInside(Sprite.Width, Sprite.Height, Opaque, Quads) && IsWound(Quads);
        if (!bGood) AddError(TEXT("extruded mesh of ") + Sprite.Name.ToString());
        Good += bGood;
        Shared += Corners;
    }
    TestEqual(TEXT("111 sprites extrude to closed meshes"), Good, 111);
    AddInfo(FString::Printf(TEXT("depth edges shared at diagonal corners across the 111 sprites: %d"), Shared));

    int32 Resolved = 0;
    for (const FFoundationItemAsset& Item : Assets->Items)
    {
        FFoundationItemModelView View;
        const bool bResolved = Assets->FindModelView(Item.Id, TEXT("firstperson_righthand"), View) && View.Display;
        if (!bResolved) AddError(TEXT("no first-person model for ") + Item.Id.ToString());
        Resolved += bResolved;
    }
    TestEqual(TEXT("104 items have a first-person model and transform"), Resolved, 104);
    FFoundationItemModelView Pickaxe, Spear, SpearGui;
    TestTrue(TEXT("iron pickaxe: its own model and sprite, item/generated's first person (handheld inherits it)"),
        Assets->FindModelView(TEXT("iron_pickaxe"), TEXT("firstperson_righthand"), Pickaxe) &&
            Pickaxe.Model->Name == TEXT("item/iron_pickaxe") && Pickaxe.Texture->Name == TEXT("item/iron_pickaxe") &&
            Pickaxe.Texture->Width == 16 && Pickaxe.Display->Rotation.Equals(FVector(0, -90, 25)) &&
            Pickaxe.Display->Translation.Equals(FVector(1.13, 3.2, 1.13)) &&
            Pickaxe.Display->Scale.Equals(FVector(.68)));
    TestTrue(TEXT("iron spear: the 32 x 32 in-hand model in first person, its 16 x 16 model in the GUI"),
        Assets->FindModelView(TEXT("iron_spear"), TEXT("firstperson_righthand"), Spear) &&
            Spear.Model->Name == TEXT("item/iron_spear_in_hand") && Spear.Texture->Width == 32 &&
            Spear.Display->Rotation.Equals(FVector(-20, 90, -35)) &&
            Spear.Display->Translation.Equals(FVector(3.13, 2, .13)) &&
            Spear.Display->Scale.Equals(FVector(1.36, 1.36, .68)) &&
            Assets->FindModelView(TEXT("iron_spear"), TEXT("gui"), SpearGui) &&
            SpearGui.Model->Name == TEXT("item/iron_spear") && SpearGui.Texture->Width == 16);
    FFoundationItemModelView None;
    TestFalse(TEXT("a block's item (not in DA_Items) has no model here"),
        Assets->FindModelView(TEXT("stone"), TEXT("gui"), None));
    // The held view's material: checked on disk, never loaded here (it holds the arrays, which have no platform data
    // under -NullRHI).
    TestTrue(TEXT("M_HeldItem exists (CreateItemMaterials.py)"),
        FPackageName::DoesPackageExist(
            UUnnamedProjectSettings::GetItemAssetPath(TEXT("M_HeldItem")).GetLongPackageName()));
    return true;
}
#endif
