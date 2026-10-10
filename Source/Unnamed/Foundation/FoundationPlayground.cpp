#include "Foundation/FoundationPlayground.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationSimulation.h"
#include "Components/TextRenderComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AFoundationPlayground::AFoundationPlayground() = default;

namespace
{
    bool IsGridAligned(const FVector& Point)
    {
        const auto OnLine = [](double Cm) { return FMath::IsNearlyZero(FMath::Fmod(Cm, UFoundationProfile::BlockCm)); };
        return OnLine(Point.X) && OnLine(Point.Y) && OnLine(Point.Z);
    }
}

void AFoundationPlayground::AddSolid(
    const FString& Label, FVector Minimum, FVector Maximum, FLinearColor Color, bool bCaption)
{
    // Targetable and occluding like every static; only grid-aligned solids anchor placement.
    AddStatic(Label, Minimum, Maximum, Color, IsGridAligned(Minimum) && IsGridAligned(Maximum));
    if (bCaption) AddLabel(Label, FBox(Minimum, Maximum));
}

void AFoundationPlayground::AddStatic(
    const FString& Label, FVector Min, FVector Max, FLinearColor Color, bool bAnchor, FName Interaction)
{
    const FBox Box(Min, Max);
    FFoundationStatic Static;
    Static.Box = Box;
    Static.Label = Label;
    Static.bAnchor = bAnchor;
    Static.Interaction = Interaction;
    Authority.Statics.Add(Static);
    CollisionBoxes.Add(Box);
    AddMesh(Box, Color, true);
}

void AFoundationPlayground::AddTarget(int32 Id, const FString& Label, FVector Min, FVector Max)
{
    FFoundationTrainingTarget Target;
    Target.Id = Id;
    Target.Label = Label;
    Target.Box = FBox(Min, Max);
    Authority.Targets.Add(Target);
    // Anchored: movement collides with it; the view draws and tints it.
    CollisionBoxes.Add(Target.Box);
}

void AFoundationPlayground::AddSign(const FString& Text, FVector Location, double Yaw)
{
    auto* Sign = NewObject<UTextRenderComponent>(this);
    Sign->SetupAttachment(GetRootComponent());
    Sign->RegisterComponent();
    Sign->SetText(FText::FromString(Text));
    Sign->SetWorldSize(16);
    Sign->SetHorizontalAlignment(EHTA_Center);
    Sign->SetVerticalAlignment(EVRTA_TextBottom);
    Sign->SetTextRenderColor(FColor(235, 240, 245));
    Sign->SetWorldLocationAndRotation(Location, FRotator(0, Yaw, 0));
}

void AFoundationPlayground::AddBlockGallery()
{
    const FFoundationBlockRegistry& Registry = Authority.World.GetRegistry();
    auto Seed = [this](const FIntVector& Cell, FFoundationBlockState State) { Authority.Seed.Add({Cell, State}); };
    auto Parse = [&Registry](const TCHAR* Text)
    {
        FFoundationBlockState State;
        return Registry.Parse(Text, State) ? State : FFoundationBlockState{};
    };
    auto CellCenter = [](const FIntVector& Cell, double Z)
    { return FVector(Cell) * UFoundationProfile::BlockCm + FVector(50, 50, Z); };

    // Showcase rows, one per acceptance shot (Docs/Blocks.md lists them); each row reads west to east.
    struct FRow
    {
        const TCHAR* Label;
        TArray<const TCHAR*> States;
    };
    const TArray<FRow> Rows = {
        {TEXT("GRASS: tinted top and side overlay; under snow it turns snowy"),
            {TEXT("grass_block[snowy=false]"), TEXT("grass_block[snowy=false]"), TEXT("grass_block[snowy=true]"),
                TEXT("podzol[snowy=false]"), TEXT("mycelium[snowy=false]"), TEXT("dirt")}},
        {TEXT("LEAVES: cutout, foliage tint (birch and spruce fixed, cherry and azalea untinted)"),
            {TEXT("oak_leaves"), TEXT("spruce_leaves"), TEXT("birch_leaves"), TEXT("jungle_leaves"),
                TEXT("acacia_leaves"), TEXT("dark_oak_leaves"), TEXT("mangrove_leaves"), TEXT("cherry_leaves"),
                TEXT("azalea_leaves"), TEXT("copper_grate")}},
        {TEXT("TRANSLUCENT: glass, stained glass, tinted glass, ice"),
            {TEXT("glass"), TEXT("white_stained_glass"), TEXT("red_stained_glass"), TEXT("blue_stained_glass"),
                TEXT("lime_stained_glass"), TEXT("tinted_glass"), TEXT("ice"), TEXT("frosted_ice")}},
        {TEXT("AXIS AND FACING: oak log x y z, furnace north east south west, observer up"),
            {TEXT("oak_log[axis=x]"), TEXT("oak_log[axis=y]"), TEXT("oak_log[axis=z]"),
                TEXT("furnace[facing=north,lit=false]"), TEXT("furnace[facing=east,lit=false]"),
                TEXT("furnace[facing=south,lit=false]"), TEXT("furnace[facing=west,lit=true]"),
                TEXT("observer[facing=up,powered=false]")}},
        {TEXT("SLABS: bottom, top and double (smooth stone, oak, stone bricks)"),
            {TEXT("smooth_stone_slab[type=bottom,waterlogged=false]"),
                TEXT("smooth_stone_slab[type=top,waterlogged=false]"),
                TEXT("smooth_stone_slab[type=double,waterlogged=false]"),
                TEXT("oak_slab[type=bottom,waterlogged=false]"), TEXT("oak_slab[type=top,waterlogged=false]"),
                TEXT("oak_slab[type=double,waterlogged=false]"),
                TEXT("stone_brick_slab[type=bottom,waterlogged=false]"),
                TEXT("stone_brick_slab[type=top,waterlogged=false]")}},
        {TEXT("WATER: source (8/9), levels 1-7 (7/9 .. 1/9), falling"),
            {TEXT("water[level=0]"), TEXT("water[level=1]"), TEXT("water[level=2]"), TEXT("water[level=3]"),
                TEXT("water[level=4]"), TEXT("water[level=5]"), TEXT("water[level=6]"), TEXT("water[level=7]"),
                TEXT("water[level=8]")}},
        {TEXT("ANIMATED: sea lantern, magma, prismarine, crimson stem, command block, lit smoker"),
            {TEXT("sea_lantern"), TEXT("magma_block"), TEXT("prismarine"), TEXT("crimson_stem[axis=y]"),
                TEXT("command_block[conditional=false,facing=up]"), TEXT("smoker[facing=north,lit=true]")}},
        {TEXT("PLANTS: grass, poppy, tall grass, pink petals, leaf litter, moss carpet, firefly bush, sea pickles and "
              "seagrass (holding water), lily pad, bamboo, amethyst cluster"),
            {TEXT("short_grass"), TEXT("poppy"), TEXT("tall_grass[half=lower]"),
                TEXT("pink_petals[facing=north,flower_amount=4]"), TEXT("leaf_litter[facing=east,segment_amount=4]"),
                TEXT("moss_carpet"), TEXT("firefly_bush"), TEXT("sea_pickle[pickles=4,waterlogged=true]"),
                TEXT("seagrass"), TEXT("lily_pad"), TEXT("bamboo[age=0,leaves=large,stage=0]"),
                TEXT("amethyst_cluster[facing=up,waterlogged=false]")}},
    };
    for (int32 Row = 0; Row < Rows.Num(); ++Row)
    {
        for (int32 Column = 0; Column < Rows[Row].States.Num(); ++Column)
        {
            const FFoundationBlockState State = Parse(Rows[Row].States[Column]);
            if (State.IsAir()) continue;
            Seed(GetShowcaseCell(Row, Column), State);
        }
        // Readable from the south, above the row's west end.
        AddSign(Rows[Row].Label, CellCenter(GetShowcaseCell(Row, 0), 0) + FVector(-150, 0, 120), 90);
    }
    // The snowy grass block carries its snow, as it would in the world.
    if (const FFoundationBlockType* Snow = Authority.World.FindBlock(TEXT("snow_block")))
        Seed(GetShowcaseCell(0, 2) + FIntVector(0, 0, 1), Snow->GetDefaultState());
    // The tall grass's upper half stands on its lower half.
    check(Rows.Num() == PlantRow + 1);
    if (const FFoundationBlockState Upper = Parse(TEXT("tall_grass[half=upper]")); !Upper.IsAir())
        Seed(GetShowcaseCell(PlantRow, 2) + FIntVector(0, 0, 1), Upper);

    // Every block in registry order (its default state), then every state in state-id order.
    for (int32 Block = 1; Block < Registry.GetBlockCount(); ++Block)
    {
        const int32 Slot = Block - 1;
        Seed(
            RegistryGalleryOrigin + FIntVector(Slot % RegistryGalleryColumns * 2, Slot / RegistryGalleryColumns * 2, 0),
            Registry.GetBlockAt(Block).GetDefaultState());
    }
    AddSign(TEXT("BLOCK GALLERY: every block in registry order"),
        CellCenter(RegistryGalleryOrigin, 0) + FVector(-150, 0, 160), 90);
    int32 Index = 0;
    for (int32 Block = 1; Block < Registry.GetBlockCount(); ++Block)
    {
        const FFoundationBlockType& Type = Registry.GetBlockAt(Block);
        for (int32 Local = 0; Local < Type.StateCount; ++Local, ++Index)
            Seed(StateGalleryOrigin + FIntVector(Index % StateGalleryColumns * 2, Index / StateGalleryColumns * 2, 0),
                {uint16(Type.MinStateId + Local)});
    }
    AddSign(TEXT("EVERY STATE (as the debug world): state-id order"),
        CellCenter(StateGalleryOrigin, 0) + FVector(-150, 0, 160), 90);
}

void AFoundationPlayground::BuildLayout()
{
    const UFoundationInteractionSettings& Settings = UFoundationInteractionSettings::Get();
    Settings.BuildRules(Authority.Rules);
    Authority.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
    Authority.World.OriginCm = FVector::ZeroVector;
    // The whole ground is buildable: 2000 x 2000 cells, BuildHeightCells high.
    const int32 HalfCells = int32(GroundHalfExtentCm / UFoundationProfile::BlockCm);
    Authority.World.BoundsMin = FIntVector(-HalfCells, -HalfCells, 0);
    Authority.World.BoundsMax = FIntVector(HalfCells - 1, HalfCells - 1, BuildHeightCells - 1);
    // Lab rules; the verification probe may start finite or with the other preset.
    Authority.Options.bInfiniteSupply =
        Settings.bInfiniteSupplyDefault && !FParse::Param(FCommandLine::Get(), TEXT("InteractionFinite"));
    Authority.Options.bToolWear = !Authority.Options.bInfiniteSupply;
    Authority.Options.FiniteBlockCount = Settings.FiniteBlockCount;
    FString Preset;
    Authority.Options.PlacementPreset =
        FParse::Value(FCommandLine::Get(), TEXT("InteractionPreset="), Preset) && Settings.FindPreset(FName(*Preset))
        ? FName(*Preset)
        : Settings.DefaultPreset;
    Authority.OnInteraction = [this](FName Interaction) { HandleInteraction(Interaction); };

    // The grid-aligned anchor ground: its top is the z = 0 cell boundary.
    AddGround();
    // Lab controls west of spawn, in reach from it. Use activates; crouching with an item places instead.
    AddStatic(TEXT("placement preset"), {-400, -200, 0}, {-300, -100, 100}, FLinearColor(.2f, .45f, .9f), true,
        TEXT("lab.preset"));
    AddStatic(
        TEXT("reset lab"), {-400, 0, 0}, {-300, 100, 100}, FLinearColor(.85f, .2f, .15f), true, TEXT("lab.reset"));
    AddStatic(
        TEXT("supply"), {-400, 200, 0}, {-300, 300, 100}, FLinearColor(.95f, .7f, .15f), true, TEXT("lab.supply"));
    // A three-block ledge with steps: crouch-build at its edge, jump off and place while falling.
    AddStatic(TEXT("ledge"), {-1300, -300, 0}, {-1000, 300, 300}, FLinearColor(.45f, .5f, .55f), true);
    AddStatic(TEXT("ledge step 2"), {-1000, 200, 0}, {-900, 300, 200}, FLinearColor(.45f, .5f, .55f), true);
    AddStatic(TEXT("ledge step 1"), {-900, 200, 0}, {-800, 300, 100}, FLinearColor(.45f, .5f, .55f), true);
    // A low ceiling whose underside is 2.8 m: the head stops with the feet exactly one block up.
    AddStatic(TEXT("low ceiling"), {500, -1000, 280}, {900, -600, 300}, FLinearColor(.55f, .45f, .35f), false);
    // Occlusion: thin walls (not grid aligned, not anchors) in front of a block and a target.
    AddStatic(TEXT("occluding wall"), {-700, -900, 0}, {-690, -500, 250}, FLinearColor(.6f, .62f, .66f), false);
    AddStatic(TEXT("target wall"), {300, 900, 0}, {700, 910, 250}, FLinearColor(.6f, .62f, .66f), false);
    AddTarget(1, TEXT("Training target"), {-30, 970, 0}, {30, 1030, 180});
    AddTarget(2, TEXT("Target behind wall"), {470, 970, 0}, {530, 1030, 180});
    AddWater(TEXT("water: eyes underwater mine x0.2"), {1000, 400, 0}, {1300, 700, 200});
    // The movement course, east of the stations and clear of their build area.
    CourseOrigin = PlaygroundCourseOrigin;
    BuildMovementCourse();

    // Seed blocks restored by every reset (session-only: edits are never saved).
    if (const FFoundationBlockType* Stone = Authority.World.FindBlock(TEXT("stone")))
        for (const FIntVector Cell :
            {FIntVector(3, -1, 0), FIntVector(3, 0, 0), FIntVector(3, 1, 0), FIntVector(4, -1, 0), FIntVector(4, 0, 0),
                FIntVector(4, 1, 0), FIntVector(4, 0, 1), FIntVector(6, 3, 0), FIntVector(7, 3, 0), FIntVector(6, 4, 0),
                FIntVector(7, 4, 0), FIntVector(12, 6, 0), FIntVector(-8, -7, 0)})
            Authority.Seed.Add({Cell, Stone->GetDefaultState()});

    AddSign(TEXT("STARTER BLOCKS\nleft: mine (slot 2 pickaxe is fast)\nright: place"), {290, 50, 260}, 180);
    AddSign(TEXT("PLACEMENT PRESET"), {-290, -150, 140}, 0);
    AddSign(TEXT("RESET LAB"), {-290, 50, 140}, 0);
    AddSign(TEXT("SUPPLY"), {-290, 250, 140}, 0);
    AddSign(TEXT("JUMP-STACK HERE\nlook down, hold jump + right click"), {100, -500, 200}, 90);
    AddSign(TEXT("LOW CEILING 2.8 m\nexact-contact stacking"), {700, -580, 330}, 90);
    AddSign(TEXT("LEDGE: crouch at the edge, look back down to build out\njump off and place below while falling"),
        {-990, 0, 420}, 0);
    AddSign(TEXT("BREAK UNDERFOOT\nstand on these, look down, mine"), {600, 380, 220}, 180);
    AddSign(TEXT("OCCLUSION: walls block reach"), {-650, -700, 300}, 0);
    AddSign(TEXT("TRAINING TARGET\nleft click: hand / pickaxe / sword\ncriticals while falling"), {0, 880, 300}, -90);
    AddSign(TEXT("TARGET BEHIND WALL\nthe wall blocks attacks"), {500, 860, 330}, -90);
    AddSign(TEXT("MOVEMENT COURSE 42 m EAST OF SPAWN\nsteps, ceilings, gaps, ledges, stairs, drop, water\nbuild "
                 "anywhere on the grid"),
        {2500, 0, 260}, 180);

    AddBlockGallery();

    Authority.Reset();
    View = NewObject<UFoundationInteractionView>(this);
    View->RegisterComponent();
}

FFoundationTarget AFoundationPlayground::TraceTarget(
    FVector Eye, FVector Direction, const UFoundationProfile& Profile) const
{
    return TraceAuthority(Authority, Eye, Direction);
}

void AFoundationPlayground::GatherCollision(const FBox& Region, TArray<FBox>& Out) const
{
    // The lab's immutable cuboids and targets, then the committed blocks near the region.
    Super::GatherCollision(Region, Out);
    Authority.World.GatherCollision(Region, Out);
}

void AFoundationPlayground::ApplyLogicalOffset(const FVector& Offset)
{
    Super::ApplyLogicalOffset(Offset);
    Authority.World.OriginCm += Offset;
    for (FFoundationStatic& Static : Authority.Statics)
        Static.Box = Static.Box.ShiftBy(Offset);
    for (FFoundationTrainingTarget& Target : Authority.Targets)
        Target.Box = Target.Box.ShiftBy(Offset);
}

FVector AFoundationPlayground::GetSafeSpawn(const UFoundationProfile& Profile) const
{
    // Spawn is the grid origin; blocks built there lift the feet onto the stack rather than inside it.
    FVector Feet = Authority.World.OriginCm;
    TArray<FBox> Boxes;
    for (int32 Rise = 0; Rise <= Authority.World.BoundsMax.Z + 1; ++Rise)
    {
        const FBox Body = FFoundationSimulation::Bounds(Feet, Profile.StandingHeightCm, Profile.WidthCm);
        Boxes.Reset();
        GatherCollision(Body.ExpandBy(1), Boxes);
        if (FFoundationSimulation::Fits(Body, Boxes)) break;
        Feet.Z += UFoundationProfile::BlockCm;
    }
    return Feet;
}

void AFoundationPlayground::ResetLab()
{
    Authority.Reset();
}

void AFoundationPlayground::SetFiniteSupply(bool bFinite)
{
    Authority.Options.bInfiniteSupply = !bFinite;
    Authority.Options.bToolWear = bFinite;
    Authority.Reset();
}

void AFoundationPlayground::SetPlacementPreset(FName Preset)
{
    if (UFoundationInteractionSettings::Get().FindPreset(Preset)) Authority.Options.PlacementPreset = Preset;
}

void AFoundationPlayground::HandleInteraction(FName Interaction)
{
    if (Interaction == TEXT("lab.supply"))
    {
        // Runs inside the command: change the rule, then let the authority reset after it.
        Authority.Options.bInfiniteSupply = !Authority.Options.bInfiniteSupply;
        Authority.Options.bToolWear = !Authority.Options.bInfiniteSupply;
        Authority.RequestReset();
    }
    else if (Interaction == TEXT("lab.preset"))
    {
        const TArray<FFoundationPlacementPreset>& Presets = UFoundationInteractionSettings::Get().Presets;
        const int32 Current = Presets.IndexOfByPredicate([this](const FFoundationPlacementPreset& Preset)
            { return Preset.Name == Authority.Options.PlacementPreset; });
        if (Presets.Num()) Authority.Options.PlacementPreset = Presets[(Current + 1) % Presets.Num()].Name;
    }
}
