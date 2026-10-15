#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItemMesh.h"
#include "Foundation/FoundationItems.h"
#include "Hud/HudCreative.h"
#include "Hud/HudDrawList.h"
#include "Hud/HudGuiPainter.h"
#include "Hud/HudModel.h"
#include "Hud/HudPresenter.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
{
    constexpr EAutomationTestFlags ArmorViewTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // The leather dye's default colour (#A06540).
    const FColor LeatherDye(0xA0, 0x65, 0x40);

    // Rules with the project's items and the hand-built armor items of 19a2: a dyed leather-like helmet, a pumpkin-like
    // head item with a camera overlay, and a chestplate that carries one too (it must not show it).
    struct FArmorViewRules
    {
        FFoundationRules Rules;
        int32 Leather = INDEX_NONE, Pumpkin = INDEX_NONE, Chest = INDEX_NONE;

        FArmorViewRules()
        {
            UFoundationInteractionSettings::Get().BuildRules(Rules);
            FFoundationItemDefinition Item;
            Item.Id = TEXT("armor_view_leather_helmet");
            Item.DisplayName = TEXT("Leather Cap");
            Item.Sprite = TEXT("item/leather_helmet");
            Item.SpriteOverlay = TEXT("item/leather_helmet_overlay");
            Item.SpriteTint = LeatherDye;
            Item.Equipment = EFoundationEquipmentSlot::Head;
            Leather = Rules.Items.Add(Item);
            Item = FFoundationItemDefinition();
            Item.Id = TEXT("armor_view_pumpkin");
            Item.DisplayName = TEXT("Carved Pumpkin");
            Item.Sprite = TEXT("block/carved_pumpkin");
            Item.Equipment = EFoundationEquipmentSlot::Head;
            Item.CameraOverlay = TEXT("misc/pumpkinblur");
            Pumpkin = Rules.Items.Add(Item);
            Item = FFoundationItemDefinition();
            Item.Id = TEXT("armor_view_chest");
            Item.Sprite = TEXT("item/iron_chestplate");
            Item.Equipment = EFoundationEquipmentSlot::Chest;
            Item.CameraOverlay = TEXT("misc/pumpkinblur");
            Chest = Rules.Items.Add(Item);
        }
    };

    FFoundationInventory MakeArmorInventory()
    {
        FFoundationInventory Inventory;
        Inventory.Hotbar.SetNum(FFoundationInventory::HotbarSlots);
        Inventory.Armor.SetNum(FFoundationInventory::ArmorSlots);
        return Inventory;
    }

    FFoundationItemInstance MakeStack(int32 Id, int32 Item)
    {
        FFoundationItemInstance Stack;
        Stack.InstanceId = Id;
        Stack.Item = Item;
        Stack.Count = 1;
        return Stack;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudArmorSpriteLayersTest, "Unnamed.Hud.Armor.SpriteLayers", ArmorViewTestFlags)

bool FHudArmorSpriteLayersTest::RunTest(const FString&)
{
    // The screens' sprite: the tinted first layer, then the untinted overlay over it, same rect.
    FHudGuiArt Art;
    FHudCreativeItem& Leather = Art.Items.Add(TEXT("leather_helmet"));
    Leather.Sprite = TEXT("item/leather_helmet");
    Leather.SpriteOverlay = TEXT("item/leather_helmet_overlay");
    Leather.SpriteTint = FLinearColor(LeatherDye);
    Art.Items.Add(TEXT("iron_helmet")).Sprite = TEXT("item/iron_helmet");
    const TArray<FHudSpriteLayer> Layers = Art.GetSpriteLayers(TEXT("leather_helmet"));
    if (!TestEqual(TEXT("leather: two layers"), Layers.Num(), 2)) return false;
    TestEqual(TEXT("first layer is the sprite"), Layers[0].Sprite, FString(TEXT("item/leather_helmet")));
    TestTrue(TEXT("first layer times the dye"), Layers[0].Color.Equals(FLinearColor(LeatherDye)));
    TestEqual(TEXT("second layer is the overlay"), Layers[1].Sprite, FString(TEXT("item/leather_helmet_overlay")));
    TestTrue(TEXT("overlay untinted"), Layers[1].Color.Equals(FLinearColor::White));
    const TArray<FHudSpriteLayer> Plain = Art.GetSpriteLayers(TEXT("iron_helmet"));
    TestTrue(TEXT("iron: one white layer"),
        Plain.Num() == 1 && Plain[0].Sprite == TEXT("item/iron_helmet") && Plain[0].Color.Equals(FLinearColor::White));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudArmorHotbarTintTest, "Unnamed.Hud.Armor.HotbarTint", ArmorViewTestFlags)

bool FHudArmorHotbarTintTest::RunTest(const FString&)
{
    // The hotbar: the sprite draw carries the dye, the overlay draws right after it, untinted, on the same rect.
    FArmorViewRules Lab;
    FFoundationInventory Inventory = MakeArmorInventory();
    Inventory.Hotbar[0] = MakeStack(1, Lab.Leather);
    Inventory.Hotbar[1] = MakeStack(2, Lab.Rules.FindItem(TEXT("iron_pickaxe")));
    FHudSnapshot S;
    UHudPresenter::ApplyInventory(S, Inventory, Lab.Rules);
    TestTrue(TEXT("the view carries the dye and the overlay"),
        S.ItemViews[0].SpriteTint.Equals(FLinearColor(LeatherDye)) &&
            S.ItemViews[0].SpriteOverlay == TEXT("item/leather_helmet_overlay"));
    TestTrue(TEXT("an undyed item stays white without an overlay"),
        S.ItemViews[1].SpriteTint.Equals(FLinearColor::White) && S.ItemViews[1].SpriteOverlay.IsNone());

    FHudPresentation P;
    P.Update(S, 0, 0);
    const FHudLayout L = HudModel::GetLayout({640, 360}, 0, false);
    const TArray<FHudDraw> Draws = HudDrawList::Build(S, P, L, 0, {});
    const FIntRect Rect(L.GetItem(0), L.GetItem(0) + FIntPoint(16, 16));
    const int32 Base = Draws.IndexOfByPredicate(
        [&Rect](const FHudDraw& D) { return D.Sprite == TEXT("item/leather_helmet") && D.Rect == Rect; });
    if (!TestTrue(TEXT("the sprite draws"), Base != INDEX_NONE)) return false;
    TestTrue(TEXT("the sprite is multiplied by the dye"), Draws[Base].Color.Equals(FLinearColor(LeatherDye)));
    if (TestTrue(TEXT("the overlay follows"), Draws.IsValidIndex(Base + 1)))
        TestTrue(TEXT("the overlay: same rect, untinted"),
            Draws[Base + 1].Sprite == TEXT("item/leather_helmet_overlay") && Draws[Base + 1].Rect == Rect &&
                Draws[Base + 1].Color.Equals(FLinearColor::White));
    const FIntRect PickRect(L.GetItem(1), L.GetItem(1) + FIntPoint(16, 16));
    TestTrue(TEXT("the undyed sprite draws white"),
        Draws.ContainsByPredicate(
            [&PickRect](const FHudDraw& D)
            {
                return D.Sprite == TEXT("item/iron_pickaxe") && D.Rect == PickRect &&
                    D.Color.Equals(FLinearColor::White);
            }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudArmorNameColorTest, "Unnamed.Hud.Armor.NameColor", ArmorViewTestFlags)

bool FHudArmorNameColorTest::RunTest(const FString&)
{
    // The tooltip's name line takes its rarity's colour: yellow for uncommon (the chainmail pieces), white otherwise.
    FHudGuiArt Art;
    FHudCreativeItem& Chain = Art.Items.Add(TEXT("chainmail_helmet"));
    Chain.Name = TEXT("Chainmail Helmet");
    Chain.Rarity = TEXT("uncommon");
    Art.Items.Add(TEXT("iron_helmet")).Name = TEXT("Iron Helmet");
    const FHudTooltipLine Uncommon = Art.GetNameLine(TEXT("chainmail_helmet"));
    TestEqual(TEXT("name"), Uncommon.Text, FString(TEXT("Chainmail Helmet")));
    TestTrue(TEXT("uncommon: yellow"), Uncommon.Color == HudTooltip::Yellow);
    TestTrue(TEXT("common: white"), Art.GetNameLine(TEXT("iron_helmet")).Color == HudTooltip::White);
    TestTrue(TEXT("unknown item: white"), Art.GetNameLine(TEXT("nothing")).Color == HudTooltip::White);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudArmorCameraOverlayTest, "Unnamed.Hud.Armor.CameraOverlay", ArmorViewTestFlags)

bool FHudArmorCameraOverlayTest::RunTest(const FString&)
{
    // The camera overlay shows only while an item that has one is worn on the head.
    FArmorViewRules Lab;
    auto Overlay = [&Lab](const FFoundationInventory& Inventory)
    {
        FHudSnapshot S;
        UHudPresenter::ApplyInventory(S, Inventory, Lab.Rules);
        return S.CameraOverlay;
    };
    FFoundationInventory Inventory = MakeArmorInventory();
    TestTrue(TEXT("nothing worn: none"), Overlay(Inventory).IsNone());
    Inventory.Armor[3] = MakeStack(1, Lab.Pumpkin);
    TestEqual(TEXT("worn on the head: the pumpkin blur"), Overlay(Inventory), FName(TEXT("misc/pumpkinblur")));
    FFoundationInventory Elsewhere = MakeArmorInventory();
    Elsewhere.Armor[2] = MakeStack(2, Lab.Pumpkin);
    TestTrue(TEXT("in the chest slot: none"), Overlay(Elsewhere).IsNone());
    FFoundationInventory Held = MakeArmorInventory();
    Held.Hotbar[0] = MakeStack(3, Lab.Pumpkin);
    TestTrue(TEXT("only held: none"), Overlay(Held).IsNone());
    FFoundationInventory Wrong = MakeArmorInventory();
    Wrong.Armor[3] = MakeStack(4, Lab.Chest);
    TestTrue(TEXT("a chest piece in the head slot: none"), Overlay(Wrong).IsNone());

    // Drawn first (behind every widget) over the whole view, in first person only.
    FHudSnapshot S;
    UHudPresenter::ApplyInventory(S, Inventory, Lab.Rules);
    const FHudLayout L = HudModel::GetLayout({640, 360}, 0, false);
    auto Build = [&] { return HudDrawList::Build(S, FHudPresentation{}, L, 0, {}); };
    const TArray<FHudDraw> Draws = Build();
    if (TestFalse(TEXT("draws"), Draws.IsEmpty()))
        TestTrue(TEXT("first draw: the whole view"),
            Draws[0].Sprite == TEXT("misc/pumpkinblur") && Draws[0].Rect == FIntRect(FIntPoint(0, 0), L.Size));
    S.bFirstPerson = false;
    TestFalse(TEXT("third person: not drawn"),
        Build().ContainsByPredicate([](const FHudDraw& D) { return D.Sprite == TEXT("misc/pumpkinblur"); }));
    S.bFirstPerson = true;
    S.CameraOverlay = NAME_None;
    TestFalse(TEXT("unworn: not drawn"),
        Build().ContainsByPredicate([](const FHudDraw& D) { return D.Sprite == TEXT("misc/pumpkinblur"); }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudArmorHeldTintTest, "Unnamed.Foundation.Items.MeshTint", ArmorViewTestFlags)

bool FHudArmorHeldTintTest::RunTest(const FString&)
{
    // A two-layer held model: the first layer's vertices carry the dye, the overlay's stay white and sit a hair
    // beyond both faces; a single untinted layer keeps the default white colour.
    TArray<bool> Opaque;
    Opaque.Init(true, 16 * 16);
    TArray<FFoundationItemMeshLayer> Layers;
    FFoundationItemMeshLayer& Base = Layers.AddDefaulted_GetRef();
    Base.Quads = FoundationItemMesh::BuildQuads(16, 16, Opaque);
    Base.Layer = 3;
    Base.Tint = LeatherDye;
    FFoundationItemMeshLayer& Top = Layers.AddDefaulted_GetRef();
    Top.Quads = FoundationItemMesh::BuildQuads(16, 16, Opaque, .001f);
    Top.Layer = 4;
    FMeshDescription Mesh;
    FoundationItemMesh::FillMeshDescription(Mesh, Layers, 100);
    FStaticMeshConstAttributes Attributes(Mesh);
    const TVertexInstanceAttributesConstRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
    const TVertexInstanceAttributesConstRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
    const FLinearColor DyeColor(LeatherDye);
    // Review 19a2: the description holds the linear colour (#A0 -> 0.3515), not the sRGB bytes.
    TestTrue(TEXT("the dye in linear"), FMath::IsNearlyEqual(DyeColor.R, .3515f, .001f));
    const FVector4f Dye(DyeColor.R, DyeColor.G, DyeColor.B, DyeColor.A), White(1, 1, 1, 1);
    int32 Dyed = 0, Plain = 0, Wrong = 0;
    for (const FVertexInstanceID Instance : Mesh.VertexInstances().GetElementIDs())
    {
        const int32 Layer = FMath::RoundToInt(UVs.Get(Instance, 1).X);
        const FVector4f Color = Colors[Instance];
        if (Layer == 3)
            Color.Equals(Dye, KINDA_SMALL_NUMBER) ? ++Dyed : ++Wrong;
        else if (Layer == 4)
            Color.Equals(White, KINDA_SMALL_NUMBER) ? ++Plain : ++Wrong;
        else
            ++Wrong;
    }
    TestEqual(TEXT("the first layer's vertices: all the dye"), Dyed, Base.Quads.Num() * 4);
    TestEqual(TEXT("the overlay's vertices: all white"), Plain, Top.Quads.Num() * 4);
    TestEqual(TEXT("no other vertex"), Wrong, 0);
    // The overlay's whole front quad is in front of the base's, its back behind the base's.
    float BaseFront = -1e9f, TopFront = -1e9f, BaseBack = 1e9f, TopBack = 1e9f;
    for (const FFoundationItemQuad& Quad : Base.Quads)
        for (const FVector3f& P : Quad.Positions)
            BaseFront = FMath::Max(BaseFront, P.Z), BaseBack = FMath::Min(BaseBack, P.Z);
    for (const FFoundationItemQuad& Quad : Top.Quads)
        for (const FVector3f& P : Quad.Positions)
            TopFront = FMath::Max(TopFront, P.Z), TopBack = FMath::Min(TopBack, P.Z);
    TestTrue(TEXT("the overlay covers both faces"), TopFront > BaseFront && TopBack < BaseBack);

    // A plain layer builds the mesh it always did: every vertex colour stays the default white.
    FFoundationItemMeshLayer Single;
    Single.Quads = FoundationItemMesh::BuildQuads(16, 16, Opaque);
    FMeshDescription Plain1;
    FoundationItemMesh::FillMeshDescription(Plain1, MakeArrayView(&Single, 1), 100);
    FStaticMeshConstAttributes PlainAttributes(Plain1);
    int32 NotWhite = 0;
    for (const FVertexInstanceID Instance : Plain1.VertexInstances().GetElementIDs())
        if (!PlainAttributes.GetVertexInstanceColors()[Instance].Equals(White, KINDA_SMALL_NUMBER)) ++NotWhite;
    TestEqual(TEXT("untinted: all white"), NotWhite, 0);
    return true;
}
#endif
