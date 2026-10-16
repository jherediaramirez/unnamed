#include "Hud/HudCreative.h"
#include "Foundation/FoundationBlockAssets.h"
#include "Foundation/FoundationBlockRegistry.h"

namespace
{
    const FIntPoint ArmorSlots[4] = {{54, 6}, {54, 33}, {108, 6}, {108, 33}};

    bool Inside(FIntPoint Point, FIntPoint Origin, FIntPoint Size)
    {
        return Point.X >= Origin.X && Point.Y >= Origin.Y && Point.X < Origin.X + Size.X && Point.Y < Origin.Y + Size.Y;
    }

    // Beyond vanilla (survival B4a, Jesus via PM 4): firefly_bush stays a defined block and item, but no tab or search
    // lists it (worldgen places short grass in its place: voxel.Decoration.FireflyBushAsGrass).
    bool IsHiddenCreativeItem(FName Item)
    {
        return Item == TEXT("firefly_bush");
    }
}

void FHudCreativeModel::Build(
    TConstArrayView<FFoundationCreativeTabAsset> Source, const TMap<FName, FHudCreativeItem>& Items)
{
    Known = Items;
    Tabs.Reset();
    for (const FFoundationCreativeTabAsset& Asset : Source)
    {
        FTab Tab;
        Tab.Id = Asset.Id;
        Tab.Title = Asset.Title;
        Tab.bTop = Asset.Row == 0;
        Tab.Column = Asset.Column;
        Tab.Type = Asset.Type;
        Tab.bAlignedRight = Asset.bAlignedRight;
        Tab.bShowTitle = Asset.bShowTitle;
        Tab.bCanScroll = Asset.bCanScroll;
        Tab.Icon = Asset.Icon;
        for (const FName Item : Asset.Items)
            if (Known.Contains(Item) && !IsHiddenCreativeItem(Item)) Tab.Items.AddUnique(Item);
        if (Tab.Type == TEXT("CATEGORY") && Tab.Items.IsEmpty()) continue;
        Tabs.Add(MoveTemp(Tab));
    }
    // The search tab holds every item once, in tab order (vanilla builds it from all tabs' search lists).
    if (FTab* SearchTab = Tabs.FindByPredicate([](const FTab& Tab) { return Tab.Type == TEXT("SEARCH"); }))
        if (SearchTab->Items.IsEmpty())
            for (const FTab& Tab : Tabs)
                for (const FName Item : Tab.Items)
                    SearchTab->Items.AddUnique(Item);
    Selected = FMath::Clamp(Selected, 0, FMath::Max(0, Tabs.Num() - 1));
    Filter();
}

void FHudCreativeModel::SelectTab(int32 Index)
{
    if (!Tabs.IsValidIndex(Index)) return;
    Selected = Index;
    Scroll = 0;
    Filter();
}

void FHudCreativeModel::SetSearch(const FString& Text)
{
    Search = Text.Left(50);
    Scroll = 0;
    Filter();
}

void FHudCreativeModel::Filter()
{
    Visible.Reset();
    const FTab* Tab = GetSelectedTab();
    if (!Tab) return;
    const FString Query = Search.TrimStartAndEnd().ToLower();
    for (const FName Item : Tab->Items)
    {
        if (!IsSearch() || Query.IsEmpty())
        {
            Visible.Add(Item);
            continue;
        }
        const FHudCreativeItem* Info = Known.Find(Item);
        bool bMatch;
        if (Query.StartsWith(TEXT("#")))
        {
            const FString TagQuery = Query.RightChop(1);
            bMatch = Info &&
                Info->Tags.ContainsByPredicate([&TagQuery](const FString& Tag) { return Tag.Contains(TagQuery); });
        }
        else
            bMatch = Item.ToString().ToLower().Contains(Query) || (Info && Info->Name.ToLower().Contains(Query));
        if (bMatch) Visible.Add(Item);
    }
}

bool FHudCreativeModel::CanScroll() const
{
    const FTab* Tab = GetSelectedTab();
    return Tab && Tab->bCanScroll && !IsInventory() && (Visible.Num() + Columns - 1) / Columns > VisibleRows;
}

void FHudCreativeModel::SetScroll(float Value)
{
    Scroll = CanScroll() ? FMath::Clamp(Value, 0.f, 1.f) : 0.f;
}

void FHudCreativeModel::ScrollRows(int32 Rows)
{
    // CreativeModeInventoryScreen.mouseScrolled: one row per notch over the rows beyond the first five.
    const int32 Extra = (Visible.Num() + Columns - 1) / Columns - VisibleRows;
    if (Extra > 0) SetScroll(Scroll + float(Rows) / float(Extra));
}

int32 FHudCreativeModel::GetFirstRow() const
{
    const int32 Extra = (Visible.Num() + Columns - 1) / Columns - VisibleRows;
    return Extra > 0 ? FMath::Max(0, int32(Scroll * Extra + .5f)) : 0;
}

FName FHudCreativeModel::GetGridItem(int32 Slot) const
{
    const int32 Index = GetFirstRow() * Columns + Slot;
    return Slot >= 0 && Slot < Columns * VisibleRows && Visible.IsValidIndex(Index) ? Visible[Index] : NAME_None;
}

FIntPoint FHudCreativeModel::GetTabPosition(const FTab& Tab)
{
    const int32 X = Tab.bAlignedRight ? Width - 27 * (7 - Tab.Column) + 1 : 27 * Tab.Column;
    return {X, Tab.bTop ? -28 : Height - 4};
}

FIntPoint FHudCreativeModel::GetSlotPosition(EHudCreativeHit Kind, int32 Index)
{
    switch (Kind)
    {
    case EHudCreativeHit::Grid:
        return {9 + Index % Columns * SlotSize, 18 + Index / Columns * SlotSize};
    case EHudCreativeHit::Hotbar:
        return {9 + Index * SlotSize, 112};
    case EHudCreativeHit::Inventory:
        return {9 + Index % Columns * SlotSize, 54 + Index / Columns * SlotSize};
    case EHudCreativeHit::Armor:
        return ArmorSlots[FMath::Clamp(Index, 0, 3)];
    case EHudCreativeHit::Offhand:
        return {35, 20};
    case EHudCreativeHit::Destroy:
        return {173, 112};
    default:
        return {0, 0};
    }
}

FHudCreativeHit FHudCreativeModel::HitTest(FIntPoint Point) const
{
    for (int32 Index = 0; Index < Tabs.Num(); ++Index)
        if (Inside(Point, GetTabPosition(Tabs[Index]), {TabWidth, TabHeight})) return {EHudCreativeHit::Tab, Index};
    if (!Inside(Point, {0, 0}, {Width, Height})) return {EHudCreativeHit::Outside};
    // Slots react over 18 x 18: one pixel around the 16 x 16 item (AbstractContainerScreen.isHovering).
    auto Slot = [&Point](EHudCreativeHit Kind, int32 Index) -> bool
    { return Inside(Point, GetSlotPosition(Kind, Index) - FIntPoint(1, 1), {SlotSize, SlotSize}); };
    for (int32 Index = 0; Index < Columns; ++Index)
        if (Slot(EHudCreativeHit::Hotbar, Index)) return {EHudCreativeHit::Hotbar, Index};
    if (IsInventory())
    {
        for (int32 Index = 0; Index < 27; ++Index)
            if (Slot(EHudCreativeHit::Inventory, Index)) return {EHudCreativeHit::Inventory, Index};
        for (int32 Index = 0; Index < 4; ++Index)
            if (Slot(EHudCreativeHit::Armor, Index)) return {EHudCreativeHit::Armor, Index};
        if (Slot(EHudCreativeHit::Offhand, 0)) return {EHudCreativeHit::Offhand, 0};
        if (Slot(EHudCreativeHit::Destroy, 0)) return {EHudCreativeHit::Destroy, 0};
        return {EHudCreativeHit::None};
    }
    for (int32 Index = 0; Index < Columns * VisibleRows; ++Index)
        if (Slot(EHudCreativeHit::Grid, Index)) return {EHudCreativeHit::Grid, Index};
    if (Inside(Point, {ScrollerX, ScrollerY}, {14, ScrollerHeight})) return {EHudCreativeHit::Scroller};
    if (IsSearch() && Inside(Point, {82, 6}, {80, 9})) return {EHudCreativeHit::Search};
    return {EHudCreativeHit::None};
}

int32 FHudCreativeModel::GetMaxStack(FName Item) const
{
    const FHudCreativeItem* Info = Known.Find(Item);
    return Info ? FMath::Max(1, Info->MaxStack) : 64;
}

TArray<FHudCreativeWrite> FHudCreativeModel::Click(
    const FHudCreativeHit& Hit, int32 Button, bool bShift, TConstArrayView<FHudCreativeStack> Inventory)
{
    TArray<FHudCreativeWrite> Writes;
    switch (Hit.Kind)
    {
    case EHudCreativeHit::Grid:
    {
        // The picker: its slots hold one of each item and never change.
        const FName Item = GetGridItem(Hit.Index);
        const int32 Max = Item.IsNone() ? 64 : GetMaxStack(Item);
        if (Button == 2)
        {
            if (Carried.IsEmpty() && !Item.IsNone()) Carried = {Item, Max};
        }
        else if (!Carried.IsEmpty() && !Item.IsNone() && Carried.Item == Item)
        {
            if (Button == 0)
                Carried.Count = bShift ? Max : FMath::Min(Max, Carried.Count + 1);
            else
                --Carried.Count;
        }
        else if (!Item.IsNone() && Carried.IsEmpty())
            Carried = {Item, bShift ? Max : 1};
        else if (Button == 0)
            Carried = {};
        else if (!Carried.IsEmpty())
            --Carried.Count;
        if (Carried.Count <= 0) Carried = {};
        break;
    }
    case EHudCreativeHit::Hotbar:
    case EHudCreativeHit::Offhand:
    case EHudCreativeHit::Inventory:
    case EHudCreativeHit::Armor:
    {
        // Ordinary container slots (AbstractContainerMenu.doClick). Without the full inventory, the survival tab's
        // main and armor slots stay empty and do nothing.
        const int32 Slot = GetInventorySlot(Hit);
        const bool bLab = Hit.Kind == EHudCreativeHit::Inventory || Hit.Kind == EHudCreativeHit::Armor;
        if (!Inventory.IsValidIndex(Slot) || (bLab && !bFullInventory)) break;
        if (bShift)
        {
            // The picker tabs delete the stack (ItemPickerMenu.quickMoveStack); the survival tab moves it.
            if (!IsInventory())
            {
                if (!Inventory[Slot].IsEmpty()) Writes.Add({Slot, {}});
            }
            else if (bFullInventory)
                Writes = ClickMenu(Slot, 0, EFoundationClickType::QuickMove, Inventory);
        }
        else if (Button == 2)
        {
            if (bFullInventory) Writes = ClickMenu(Slot, 2, EFoundationClickType::Clone, Inventory);
        }
        else
            Writes = ClickMenu(Slot, Button, EFoundationClickType::Pickup, Inventory);
        break;
    }
    case EHudCreativeHit::Destroy:
        // The bin: the carried stack goes; shift-clicking it empties the whole inventory.
        Carried = {};
        if (bShift)
            for (int32 Slot = 0; Slot < Inventory.Num(); ++Slot)
                if (!Inventory[Slot].IsEmpty()) Writes.Add({Slot, {}});
        break;
    case EHudCreativeHit::Outside:
        // Thrown, left the stack and right one item, as the survival screens' outside click (minecraft.wiki
        // "Inventory"; QC RA1-1): a write to the outside slot, which the authority throws as an item entity.
        if (!Carried.IsEmpty() && Button <= 1)
        {
            FHudCreativeStack Thrown = Carried;
            Thrown.Count = Button == 1 ? 1 : Carried.Count;
            Writes.Add({FFoundationContainerMenu::Outside, Thrown});
            Carried.Count -= Thrown.Count;
            if (Carried.Count <= 0) Carried = {};
        }
        break;
    default:
        break;
    }
    return Writes;
}

TArray<FHudCreativeWrite> FHudCreativeModel::PressNumber(
    const FHudCreativeHit& Hit, int32 Number, TConstArrayView<FHudCreativeStack> Inventory)
{
    TArray<FHudCreativeWrite> Writes;
    if (Number < 0 || Number >= Columns) return Writes;
    const int32 Slot = GetInventorySlot(Hit);
    if (Hit.Kind == EHudCreativeHit::Grid)
    {
        const FName Item = GetGridItem(Hit.Index);
        if (!Item.IsNone()) Writes.Add({Number, {Item, GetMaxStack(Item)}});
    }
    else if (Inventory.IsValidIndex(Slot) &&
        (bFullInventory || Hit.Kind == EHudCreativeHit::Hotbar || Hit.Kind == EHudCreativeHit::Offhand))
        Writes = ClickMenu(Slot, Number, EFoundationClickType::Swap, Inventory);
    return Writes;
}

int32 FHudCreativeModel::GetInventorySlot(const FHudCreativeHit& Hit)
{
    switch (Hit.Kind)
    {
    case EHudCreativeHit::Hotbar:
        return Hit.Index;
    case EHudCreativeHit::Offhand:
        return FFoundationInventory::OffhandSlot;
    case EHudCreativeHit::Inventory:
        return FFoundationInventory::FirstMain + Hit.Index;
    case EHudCreativeHit::Armor:
        // The survival tab shows the armor head to feet, the menu's slots 5-8.
        return FFoundationContainerMenu::ToInventorySlot(FFoundationContainerMenu::FirstArmor + Hit.Index);
    default:
        return INDEX_NONE;
    }
}

TArray<FHudCreativeWrite> FHudCreativeModel::ClickMenu(
    int32 Slot, int32 Button, EFoundationClickType Type, TConstArrayView<FHudCreativeStack> Inventory)
{
    // Items get indices local to this click.
    TArray<FName> Names;
    FFoundationInventory Copy;
    Copy.bFull = bFullInventory;
    auto ToInstance = [&Names, &Copy](const FHudCreativeStack& Stack)
    {
        if (Stack.IsEmpty()) return FFoundationItemInstance();
        FFoundationItemInstance Instance = Copy.MakeStack(Names.AddUnique(Stack.Item), Stack.Count);
        Instance.Damage = Stack.Damage;
        return Instance;
    };
    auto ToStack = [&Names](const FFoundationItemInstance& Instance)
    {
        return Instance.IsEmpty() ? FHudCreativeStack()
                                  : FHudCreativeStack{Names[Instance.Item], Instance.Count, Instance.Damage};
    };
    for (int32 Index = 0; Index < Inventory.Num(); ++Index)
        if (FFoundationItemInstance* Stack = Copy.GetSlot(Index)) *Stack = ToInstance(Inventory[Index]);
    Copy.Carried = ToInstance(Carried);
    FFoundationContainerRules Rules;
    Rules.GetMaxStack = [this, &Names](int32 Item) { return GetMaxStack(Names[Item]); };
    Rules.GetEquipment = [this, &Names](int32 Item)
    {
        const FHudCreativeItem* Info = Known.Find(Names[Item]);
        return Info ? Info->Equipment : EFoundationEquipmentSlot::None;
    };
    FFoundationContainerMenu(Copy, Rules, true).Click(FFoundationContainerMenu::ToMenuSlot(Slot), Button, Type);
    TArray<FHudCreativeWrite> Writes;
    for (int32 Index = 0; Index < Inventory.Num(); ++Index)
        if (const FFoundationItemInstance* Stack = Copy.GetSlot(Index))
            if (!(ToStack(*Stack) == Inventory[Index])) Writes.Add({Index, ToStack(*Stack)});
    Carried = ToStack(Copy.Carried);
    return Writes;
}

namespace
{
    // A model item: its baked quads under the model's gui transform (ItemTransform: translate, rotate X then Y then
    // Z, scale), a block spanning 16 slot pixels with y up. Quads facing away are culled and the rest drawn far to
    // near; each is shaded by the direction it is lit as, like the cube icon's three faces.
    TArray<HudBlockIcon::FQuad> BuildModel(const FFoundationBlockRegistry& Registry, const FFoundationBlockType& Block)
    {
        using namespace HudBlockIcon;
        const FVector3d Degrees(Block.ItemIconRotation);
        const FQuat4d Rotation = FQuat4d(FVector3d::XAxisVector, FMath::DegreesToRadians(Degrees.X)) *
            FQuat4d(FVector3d::YAxisVector, FMath::DegreesToRadians(Degrees.Y)) *
            FQuat4d(FVector3d::ZAxisVector, FMath::DegreesToRadians(Degrees.Z));
        const FVector3d Scale(Block.ItemIconScale), Translation(FVector3d(Block.ItemIconTranslation) / 16.);
        // Unreal block space (X east, Y south, Z up) to Minecraft (x east, y up, z south), centred, transformed.
        auto Transform = [&](const FVector3f& P)
        { return Rotation.RotateVector(FVector3d(P.X - .5, P.Z - .5, P.Y - .5) * Scale) + Translation; };
        TArray<TPair<double, FQuad>> Sorted;
        for (const FFoundationModelQuad& Model : Registry.GetModelQuads(Block.ItemIconModel))
        {
            FVector3d Points[4];
            for (int32 Corner = 0; Corner < 4; ++Corner)
                Points[Corner] = Transform(Model.Positions[Corner]);
            // FaceInfo order winds counter-clockwise seen from the front; the viewer looks down -z.
            if (((Points[1] - Points[0]) ^ (Points[2] - Points[0])).Z <= 0) continue;
            const float Shade = Model.Shade == EFoundationFace::Up                             ? TopShade
                : Model.Shade == EFoundationFace::East || Model.Shade == EFoundationFace::West ? LeftShade
                                                                                               : RightShade;
            FLinearColor Color(Shade, Shade, Shade, 1.f);
            if (Model.TintIndex >= 0 && Block.ItemTint.A > 0)
                Color *=
                    FLinearColor(Block.ItemTint.R / 255.f, Block.ItemTint.G / 255.f, Block.ItemTint.B / 255.f, 1.f);
            FQuad Quad;
            Quad.Texture = Model.Texture;
            Quad.Color = Color.ToFColor(false);
            double Depth = 0;
            for (int32 Corner = 0; Corner < 4; ++Corner)
            {
                Quad.Corners[Corner] =
                    FVector2f(float(8. + Points[Corner].X * 16.), float(8. - Points[Corner].Y * 16.));
                Quad.UVs[Corner] = Model.UVs[Corner];
                Depth += Points[Corner].Z;
            }
            Sorted.Add({Depth, Quad});
        }
        Sorted.StableSort([](const auto& A, const auto& B) { return A.Key < B.Key; });
        TArray<FQuad> Quads;
        for (const auto& [Depth, Quad] : Sorted)
            Quads.Add(Quad);
        return Quads;
    }
}

TArray<HudBlockIcon::FQuad> HudBlockIcon::Build(const FFoundationBlockRegistry& Registry, FName BlockId)
{
    TArray<FQuad> Quads;
    const FFoundationBlockType* Block = Registry.FindBlock(BlockId);
    if (!Block || Block->IsAir()) return Quads;
    if (Block->ItemIconTile != INDEX_NONE)
    {
        // item/generated: the sprite fills the slot, tinted like its layer.
        FQuad Quad;
        Quad.Tile = Block->ItemIconTile;
        Quad.Color =
            Block->ItemTint.A > 0 ? FColor(Block->ItemTint.R, Block->ItemTint.G, Block->ItemTint.B) : FColor::White;
        const FVector2f Corners[4] = {{0, 0}, {16, 0}, {16, 16}, {0, 16}};
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            Quad.Corners[Corner] = Corners[Corner];
            Quad.UVs[Corner] = Corners[Corner] / 16.f;
        }
        Quads.Add(Quad);
        return Quads;
    }
    if (Block->ItemIconModel != INDEX_NONE) return BuildModel(Registry, *Block);
    const FFoundationBlockState State = Block->GetDefaultState();
    const uint32* Codes = Registry.GetFaces(State);
    FBox Shape;
    if (!Codes || !Registry.GetSelectionShape(State, Shape)) return Quads;
    // The gui display transform: Rx(30) * Ry(225), scale 0.625, a block spanning 16 slot pixels, y up.
    const FQuat4d Rotation = FQuat4d(FVector3d::XAxisVector, FMath::DegreesToRadians(30.)) *
        FQuat4d(FVector3d::YAxisVector, FMath::DegreesToRadians(225.));
    auto Project = [&Rotation](const FVector& UnrealPoint)
    {
        // Unreal (X east, Y south, Z up) to Minecraft (x east, y up, z south), centred on the block.
        const FVector3d Mc(UnrealPoint.X - .5, UnrealPoint.Z - .5, UnrealPoint.Y - .5);
        const FVector3d R = Rotation.RotateVector(Mc) * .625 * 16.;
        return FVector2f(float(8. + R.X), float(8. - R.Y));
    };
    auto Tinted = [Block](uint32 Code, float Shade)
    {
        FLinearColor Color(Shade, Shade, Shade, 1.f);
        if (FoundationBlockFaces::IsTinted(Code) && Block->ItemTint.A > 0)
            Color *= FLinearColor(Block->ItemTint.R / 255.f, Block->ItemTint.G / 255.f, Block->ItemTint.B / 255.f, 1.f);
        return Color.ToFColor(false);
    };
    // Visible under this transform: the top, east (left) and north (right) faces.
    const TPair<EFoundationFace, float> Faces[3] = {
        {EFoundationFace::Up, TopShade}, {EFoundationFace::East, LeftShade}, {EFoundationFace::North, RightShade}};
    for (const auto& [Face, Shade] : Faces)
    {
        const int32 Axis = int32(Face) / 2, U = (Axis + 1) % 3, V = (Axis + 2) % 3;
        FVector Corners[4];
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            FVector P;
            P[Axis] = int32(Face) % 2 ? Shape.Max[Axis] : Shape.Min[Axis];
            P[U] = Corner == 1 || Corner == 2 ? Shape.Max[U] : Shape.Min[U];
            P[V] = Corner >= 2 ? Shape.Max[V] : Shape.Min[V];
            Corners[Corner] = P;
        }
        const uint32 Code = Codes[int32(Face)];
        FQuad Quad;
        Quad.Texture = FoundationBlockFaces::GetTexture(Code);
        Quad.Color = Tinted(Code, Shade);
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            Quad.Corners[Corner] = Project(Corners[Corner]);
            Quad.UVs[Corner] = FoundationBlockFaces::TransformUV(FoundationBlockFaces::GetTransform(Code),
                FoundationBlockFaces::GetCanonicalUV(Face, FVector3f(Corners[Corner])));
        }
        Quads.Add(Quad);
        const uint32 Overlay = Codes[FoundationBlockFaces::OverlayIndex];
        if (FoundationBlockFaces::HasOverlay(Overlay, Face))
        {
            FQuad Over = Quad;
            Over.Texture = FoundationBlockFaces::GetTexture(Overlay);
            Over.Color = Tinted(Overlay, Shade);
            Quads.Add(Over);
        }
    }
    return Quads;
}

bool HudBlockIcon::IsAnimated(const FFoundationBlockRegistry& Registry, FName Block)
{
    const UFoundationBlockAssets* Assets = Registry.GetAssets();
    if (!Assets) return false;
    for (const FQuad& Quad : Build(Registry, Block))
        if (Assets->Textures.IsValidIndex(Quad.Texture) && Assets->Textures[Quad.Texture].IsAnimated()) return true;
    return false;
}

FString HudTooltip::FormatAmount(double Value)
{
    const double Scaled = Value * 100., Floor = FMath::FloorToDouble(Scaled), Fraction = Scaled - Floor;
    const double Rounded = Fraction > .5 ? Floor + 1
        : Fraction < .5                  ? Floor
                                         : Floor + FMath::Fmod(FMath::Abs(Floor), 2.);
    FString Text = FString::Printf(TEXT("%.2f"), Rounded / 100.);
    Text.RemoveFromEnd(TEXT("0"));
    Text.RemoveFromEnd(TEXT("0"));
    Text.RemoveFromEnd(TEXT("."));
    return Text;
}

FColor HudTooltip::GetNameColor(FName Rarity)
{
    if (Rarity == TEXT("uncommon")) return Yellow;
    if (Rarity == TEXT("rare")) return Aqua;
    if (Rarity == TEXT("epic")) return LightPurple;
    return White;
}

TArray<FHudTooltipLine> HudTooltip::GetModifierLines(
    TConstArrayView<FFoundationItemModifierAsset> Modifiers, const TMap<FString, FString>& Lang, bool bPlayer)
{
    auto Text = [&Lang](const FString& Key) -> FString
    {
        const FString* Found = Lang.Find(Key);
        return Found ? *Found : Key;
    };
    // "%s %s": the amount, then the attribute's name.
    auto Fill = [](FString Format, const FString& Amount, const FString& Name)
    {
        int32 At = Format.Find(TEXT("%s"));
        if (At != INDEX_NONE) Format = Format.Left(At) + Amount + Format.Mid(At + 2);
        At = Format.Find(TEXT("%s"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + Amount.Len());
        if (At != INDEX_NONE) Format = Format.Left(At) + Name + Format.Mid(At + 2);
        return Format;
    };
    TArray<FName> Slots;
    for (const FFoundationItemModifierAsset& Modifier : Modifiers)
        Slots.AddUnique(Modifier.Slot);
    TArray<FHudTooltipLine> Lines;
    for (const FName Slot : Slots)
    {
        Lines.Add({FString(), White});
        Lines.Add({Text(TEXT("item.modifiers.") + Slot.ToString()), Gray});
        for (const FFoundationItemModifierAsset& Modifier : Modifiers)
        {
            // Tier A's modifiers all add a value (ItemImportData.py refuses others); the lang keys end in ".0".
            if (Modifier.Slot != Slot || Modifier.Operation != TEXT("add_value")) continue;
            const FString Attribute = Modifier.Attribute.ToString(), Name = Text(TEXT("attribute.name.") + Attribute);
            const double* Base = Attribute == TEXT("attack_damage") ? &BaseAttackDamage
                : Attribute == TEXT("attack_speed")                 ? &BaseAttackSpeed
                                                                    : nullptr;
            if (bPlayer && Base && Modifier.Id.ToString() == TEXT("base_") + Attribute)
                Lines.Add({TEXT(" ") +
                        Fill(Text(TEXT("attribute.modifier.equals.0")), FormatAmount(*Base + Modifier.Amount), Name),
                    DarkGreen});
            else if (Modifier.Amount != 0)
                // Knockback resistance shows ten times its amount (minecraft.wiki "Attribute"; the reference's
                // tooltips: the netherite pieces' 0.1 is "+1 Knockback Resistance").
                Lines.Add({Fill(Text(Modifier.Amount > 0 ? TEXT("attribute.modifier.plus.0")
                                                         : TEXT("attribute.modifier.take.0")),
                               FormatAmount(
                                   FMath::Abs(Modifier.Amount) * (Attribute == TEXT("knockback_resistance") ? 10 : 1)),
                               Name),
                    Modifier.Amount > 0 ? Blue : Red});
        }
    }
    return Lines;
}

int32 HudTooltip::GetLineY(int32 Line)
{
    return Line <= 0 ? 0 : Line * 10 + 2;
}

int32 HudTooltip::GetHeight(int32 Lines)
{
    return Lines <= 1 ? 8 : Lines * 10;
}
