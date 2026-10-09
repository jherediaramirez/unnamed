#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2DArray.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockImport.h"
#include "Foundation/FoundationBlockPlacement.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationTargeting.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags BlockTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    TSharedPtr<FJsonObject> ReadReference(const TCHAR* Name)
    {
        FString Text;
        TSharedPtr<FJsonObject> Json;
        const FString Path = FPaths::ProjectDir() / TEXT("Reference/minecraft-blocks-26.3") / Name;
        if (FFileHelper::LoadFileToString(Text, *Path))
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
        return Json;
    }

    FName Value(const FFoundationBlockRegistry& Registry, FFoundationBlockState State, const TCHAR* Property)
    {
        return Registry.GetValue(State, Property);
    }

    // A world on the project registry with a few cells set by id and properties.
    struct FBlocks
    {
        FFoundationBlockWorld World;

        FBlocks()
        {
            World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            World.BoundsMin = FIntVector(-20);
            World.BoundsMax = FIntVector(19);
        }

        const FFoundationBlockRegistry& R() const { return World.GetRegistry(); }

        FFoundationBlockState State(const TCHAR* Text) const
        {
            FFoundationBlockState State;
            ensureMsgf(R().Parse(Text, State), TEXT("parse %s"), Text);
            return State;
        }

        void Put(FIntVector Cell, const TCHAR* Text) { World.Set(Cell, State(Text)); }

        // getStateForPlacement for a click on Cell's Face at a height inside the destination cell.
        FFoundationBlockState Place(const TCHAR* Block, FIntVector Clicked, EFoundationFace Face, double Yaw = 0,
            double Pitch = 0, double HitHeight = .5, bool bClickedIsBlock = true)
        {
            FFoundationPlacementContext Context;
            Context.ClickedCell = Clicked;
            Context.ClickedFace = Face;
            Context.Yaw = Yaw;
            Context.Pitch = Pitch;
            const FFoundationBlockType& Type = *World.FindBlock(Block);
            Context.ClickPointCm = FVector::ZeroVector;
            FoundationBlockPlacement::ResolveDestination(World, Type, bClickedIsBlock, Context);
            // The hit lies on the clicked face; its height is given relative to the destination cell.
            Context.ClickPointCm = FoundationGrid::CellMin(Context.Destination, World.OriginCm) +
                FVector(50, 50, HitHeight * UFoundationProfile::BlockCm);
            FoundationBlockPlacement::ResolveDestination(World, Type, bClickedIsBlock, Context);
            LastDestination = Context.Destination;
            return FoundationBlockPlacement::GetStateForPlacement(World, Type, Context);
        }

        FIntVector LastDestination = FIntVector::ZeroValue;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBlocksRegistryTest, "Unnamed.Foundation.Blocks.Registry", BlockTestFlags)

bool FFoundationBlocksRegistryTest::RunTest(const FString&)
{
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    const TSharedPtr<FJsonObject> Manifest = ReadReference(TEXT("block_manifest.json"));
    const TSharedPtr<FJsonObject> Report = ReadReference(TEXT("reports/blocks-imported.json"));
    if (!TestNotNull(TEXT("registry asset"), Registry->GetAssets()) || !TestValid(TEXT("manifest"), Manifest) ||
        !TestValid(TEXT("report"), Report))
        return false;
    // 529 blocks (428 cubes + 101 slabs) plus air and water, and no project blocks by default.
    TestEqual(TEXT("air + 529 + water"), Registry->GetBlockCount(), 531);
    TArray<TSharedPtr<FJsonValue>> Blocks = Manifest->GetArrayField(TEXT("blocks"));
    Blocks.Append(Manifest->GetArrayField(TEXT("fluids")));
    int32 States = 0, Mismatches = 0;
    for (const TSharedPtr<FJsonValue>& Value : Blocks)
    {
        const TSharedPtr<FJsonObject> Entry = Value->AsObject();
        const FString Id = Entry->GetStringField(TEXT("id"));
        const FFoundationBlockType* Block = Registry->FindBlock(*Id);
        const TArray<TSharedPtr<FJsonValue>>& Range = Entry->GetArrayField(TEXT("state_id_range"));
        if (!TestNotNull(*Id, Block)) continue;
        TestTrue(Id + TEXT(" protocol id and state range"),
            Block->ProtocolId == int32(Entry->GetNumberField(TEXT("protocol_id"))) &&
                Block->MinStateId == int32(Range[0]->AsNumber()) &&
                Block->MinStateId + Block->StateCount - 1 == int32(Range[1]->AsNumber()) &&
                Block->DefaultStateId == int32(Entry->GetNumberField(TEXT("default_state_id"))));
        TestEqual(Id + TEXT(" by protocol id"), Registry->FindByProtocolId(Block->ProtocolId), Block);
        // Every state: id <-> properties exactly as Mojang's report.
        for (const TSharedPtr<FJsonValue>& StateValue :
            Report->GetObjectField(TEXT("minecraft:") + Id)->GetArrayField(TEXT("states")))
        {
            const TSharedPtr<FJsonObject> State = StateValue->AsObject();
            const FFoundationBlockState Expected{uint16(State->GetNumberField(TEXT("id")))};
            TArray<TPair<FName, FName>> Properties;
            const TSharedPtr<FJsonObject>* Given;
            if (State->TryGetObjectField(TEXT("properties"), Given))
                for (const auto& Pair : (*Given)->Values)
                    Properties.Add({FName(*Pair.Key), FName(*Pair.Value->AsString())});
            FFoundationBlockState Made;
            const bool bExact = Registry->TryMakeState(*Block, Properties, Made);
            bool bSame = bExact && Made == Expected && &Registry->GetBlock(Expected) == Block;
            for (const TPair<FName, FName>& Pair : Properties)
                bSame &= Registry->GetValue(Expected, Pair.Key) == Pair.Value;
            Mismatches += !bSame;
            ++States;
        }
    }
    TestEqual(TEXT("states checked"), States, 3445);
    TestEqual(TEXT("states whose id or properties differ from the report"), Mismatches, 0);
    TestEqual(TEXT("air is state 0"), Registry->GetBlock({0}).Id, FName(TEXT("air")));
    TestTrue(TEXT("unknown ids resolve to air"),
        Registry->GetBlock({2}).Id == TEXT("granite") && !Registry->IsKnown({30}) && Registry->GetBlock({30}).IsAir());
    // Spot checks named in the reference doc.
    TestEqual(TEXT("stone = 1"), Registry->FindBlock(TEXT("stone"))->DefaultStateId, 1);
    TestTrue(TEXT("water 37, states 89-104"),
        Registry->FindBlock(TEXT("water"))->ProtocolId == 37 && Registry->FindBlock(TEXT("water"))->MinStateId == 89);
    TestTrue(TEXT("oak_slab 15177-15182, default bottom dry 15180"),
        Registry->FindBlock(TEXT("oak_slab"))->MinStateId == 15177 &&
            Registry->FindBlock(TEXT("oak_slab"))->DefaultStateId == 15180);
    TestTrue(TEXT("poplar_planks inserted at 23"), Registry->FindByProtocolId(23)->Id == TEXT("poplar_planks"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBlocksCodecTest, "Unnamed.Foundation.Blocks.Codec", BlockTestFlags)

bool FFoundationBlocksCodecTest::RunTest(const FString&)
{
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    int32 States = 0, Failures = 0;
    for (int32 Index = 1; Index < Registry->GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry->GetBlockAt(Index);
        for (int32 Local = 0; Local < Block.StateCount; ++Local)
        {
            const FFoundationBlockState State{uint16(Block.MinStateId + Local)};
            const FFoundationSavedState Saved = Registry->Save(State);
            FFoundationBlockState Loaded, Parsed;
            TArray<FString> Warnings;
            const bool bLoad = Registry->Load(Saved, Loaded, &Warnings) && Loaded == State && Warnings.IsEmpty();
            const bool bText = Registry->Parse(Registry->ToString(State), Parsed) && Parsed == State;
            const bool bShape = Saved.Id == Block.Id && Saved.Properties.Num() == Block.Properties.Num();
            Failures += !(bLoad && bText && bShape);
            ++States;
        }
    }
    TestEqual(TEXT("every imported state"), States, 3445);
    TestEqual(TEXT("save/load and text round-trip failures"), Failures, 0);
    // The saved form is ids and properties, never numbers.
    const FFoundationBlockState Slab = Registry->FindBlock(TEXT("oak_slab"))->GetDefaultState();
    const FFoundationSavedState Saved = Registry->Save(Slab);
    TestTrue(TEXT("oak_slab saves as {oak_slab, type=bottom, waterlogged=false}"),
        Saved.Id == TEXT("oak_slab") && Saved.Properties.Num() == 2 && Saved.Properties[0].Key == TEXT("type") &&
            Saved.Properties[0].Value == TEXT("bottom") && Saved.Properties[1].Value == TEXT("false"));
    TestEqual(
        TEXT("block-state text"), Registry->ToString(Slab), FString(TEXT("oak_slab[type=bottom,waterlogged=false]")));
    FFoundationBlockState State;
    TestTrue(TEXT("namespace accepted"),
        Registry->Parse(TEXT("minecraft:oak_log[axis=x]"), State) &&
            Value(*Registry, State, TEXT("axis")) == TEXT("x"));
    // Missing properties take the default state's values; unknown ones keep the default and warn.
    TArray<FString> Warnings;
    TestTrue(TEXT("missing property"),
        Registry->Load({TEXT("oak_slab"), {{TEXT("type"), TEXT("top")}}}, State, &Warnings) &&
            Value(*Registry, State, TEXT("waterlogged")) == TEXT("false") && Warnings.IsEmpty());
    TestTrue(TEXT("unknown value keeps the default"),
        Registry->Load({TEXT("oak_slab"), {{TEXT("type"), TEXT("sideways")}}}, State, &Warnings) &&
            Value(*Registry, State, TEXT("type")) == TEXT("bottom") && Warnings.Num() == 1);
    TestFalse(TEXT("unknown block fails"), Registry->Load({TEXT("dummy_block"), {}}, State));
    TestFalse(
        TEXT("strict text parse rejects unknown values"), Registry->Parse(TEXT("oak_slab[type=sideways]"), State));
    TestTrue(TEXT("WithValue"),
        Value(*Registry, Registry->WithValue(Slab, TEXT("type"), TEXT("double")), TEXT("type")) == TEXT("double"));
    return true;
}

// Faces and Textures read the texture arrays' source data, which only editor builds keep.
#if WITH_EDITORONLY_DATA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBlocksFacesTest, "Unnamed.Foundation.Blocks.Faces", BlockTestFlags)

bool FFoundationBlocksFacesTest::RunTest(const FString&)
{
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    const UFoundationBlockAssets* Assets = Registry->GetAssets();
    if (!TestNotNull(TEXT("assets"), Assets) || !TestNotNull(TEXT("block array"), Assets->BlockTextures.Get()))
        return false;
    // Source slices: platform data is not built under -NullRHI.
    const int32 Layers = Assets->BlockTextures->Source.GetNumSlices();
    const int32 FlowLayers = Assets->WaterFlowTextures ? Assets->WaterFlowTextures->Source.GetNumSlices() : 0;
    auto ValidTexture = [&](int32 Texture)
    {
        if (!Assets->Textures.IsValidIndex(Texture)) return false;
        const FFoundationBlockTextureAsset& Entry = Assets->Textures[Texture];
        return Entry.FirstLayer >= 0 && Entry.FirstLayer + Entry.FrameCount <= (Entry.bWaterFlow ? FlowLayers : Layers);
    };
    int32 Faces = 0, Invalid = 0;
    for (int32 Index = 1; Index < Registry->GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry->GetBlockAt(Index);
        if (Block.IsWater()) continue;
        for (int32 Local = 0; Local < Block.StateCount; ++Local)
        {
            const uint32* Codes = Registry->GetFaces({uint16(Block.MinStateId + Local)});
            for (int32 Face = 0; Face < 6; ++Face, ++Faces)
                Invalid += !ValidTexture(FoundationBlockFaces::GetTexture(Codes[Face]));
            const uint32 Overlay = Codes[FoundationBlockFaces::OverlayIndex];
            if (Overlay & FoundationBlockFaces::OverlayBit)
                Invalid += !ValidTexture(FoundationBlockFaces::GetTexture(Overlay));
        }
    }
    TestEqual(TEXT("faces checked (3429 states x 6)"), Faces, 3429 * 6);
    TestEqual(TEXT("faces without a valid array layer"), Invalid, 0);
    TestTrue(TEXT("water textures"),
        ValidTexture(Assets->Textures.IndexOfByPredicate(
            [](const FFoundationBlockTextureAsset& T) { return T.Name == TEXT("water_flow"); })));
    FBlocks B;
    auto TextureOf = [&](const TCHAR* State, EFoundationFace Face)
    { return Assets->Textures[FoundationBlockFaces::GetTexture(B.R().GetFaces(B.State(State))[int32(Face)])].Name; };
    TestEqual(TEXT("oak_log axis=x shows its rings east"), TextureOf(TEXT("oak_log[axis=x]"), EFoundationFace::East),
        FName(TEXT("oak_log_top")));
    TestEqual(TEXT("oak_log axis=z shows its rings north"), TextureOf(TEXT("oak_log[axis=z]"), EFoundationFace::North),
        FName(TEXT("oak_log_top")));
    TestEqual(TEXT("furnace facing south"), TextureOf(TEXT("furnace[facing=south,lit=false]"), EFoundationFace::South),
        FName(TEXT("furnace_front")));
    TestEqual(TEXT("lit furnace"), TextureOf(TEXT("furnace[facing=west,lit=true]"), EFoundationFace::West),
        FName(TEXT("furnace_front_on")));
    TestEqual(TEXT("observer facing up"), TextureOf(TEXT("observer[facing=up,powered=false]"), EFoundationFace::Up),
        FName(TEXT("observer_front")));
    const uint32* Grass = B.R().GetFaces(B.State(TEXT("grass_block[snowy=false]")));
    TestTrue(TEXT("grass: tinted top, tinted side overlay, untinted dirt"),
        FoundationBlockFaces::IsTinted(Grass[int32(EFoundationFace::Up)]) &&
            !FoundationBlockFaces::IsTinted(Grass[int32(EFoundationFace::Down)]) &&
            FoundationBlockFaces::HasOverlay(Grass[6], EFoundationFace::North) &&
            !FoundationBlockFaces::HasOverlay(Grass[6], EFoundationFace::Up) &&
            (Grass[6] & FoundationBlockFaces::TintBit));
    TestEqual(
        TEXT("grass plains tint #91BD59"), B.World.FindBlock(TEXT("grass_block"))->Tint, FColor(0x91, 0xBD, 0x59));
    TestEqual(TEXT("cherry leaves are not tinted in 26.3"), B.World.FindBlock(TEXT("cherry_leaves"))->Tint.A, uint8(0));
    TestEqual(TEXT("render layers"),
        int32(B.World.FindBlock(TEXT("oak_leaves"))->Layer) * 10 + int32(B.World.FindBlock(TEXT("glass"))->Layer),
        int32(EFoundationRenderLayer::Cutout) * 10 + int32(EFoundationRenderLayer::Translucent));
    // UV transforms: bit 2 mirrors U, bits 0-1 turn (u, v) -> (v, 1 - u).
    TestEqual(TEXT("identity"), FoundationBlockFaces::TransformUV(0, {.25f, .1f}), FVector2f(.25f, .1f));
    TestEqual(TEXT("one turn"), FoundationBlockFaces::TransformUV(1, {.25f, .1f}), FVector2f(.1f, .75f));
    TestEqual(TEXT("mirror"), FoundationBlockFaces::TransformUV(4, {.25f, .1f}), FVector2f(.75f, .1f));
    TestEqual(TEXT("north face top-left"), FoundationBlockFaces::GetCanonicalUV(EFoundationFace::North, {1, 0, 1}),
        FVector2f(0, 0));
    TestEqual(TEXT("bottom slab side samples the lower half"),
        FoundationBlockFaces::GetCanonicalUV(EFoundationFace::South, {0, 1, .5f}).Y, .5f);
    // Animation: frame tables tick at 20 per second, interpolating where .mcmeta says so.
    auto Texture = [&](const TCHAR* Name) -> const FFoundationBlockTextureAsset&
    {
        return *Assets->Textures.FindByPredicate(
            [Name](const FFoundationBlockTextureAsset& T) { return T.Name == Name; });
    };
    int32 Layer, Next;
    float Blend;
    const FFoundationBlockTextureAsset& Lantern = Texture(TEXT("sea_lantern"));
    FoundationBlockFaces::GetFrame(Lantern, 4, Layer, Next, Blend);
    TestTrue(TEXT("sea lantern holds frame 0 for 5 ticks"), Layer == Lantern.FirstLayer && Blend == 0);
    FoundationBlockFaces::GetFrame(Lantern, 5, Layer, Next, Blend);
    TestEqual(TEXT("then frame 1"), Layer, Lantern.FirstLayer + 1);
    FoundationBlockFaces::GetFrame(Lantern, 25, Layer, Next, Blend);
    TestEqual(TEXT("and loops after 25 ticks"), Layer, Lantern.FirstLayer);
    const FFoundationBlockTextureAsset& Magma = Texture(TEXT("magma"));
    FoundationBlockFaces::GetFrame(Magma, 4, Layer, Next, Blend);
    TestTrue(TEXT("magma interpolates toward its next frame"),
        Magma.bInterpolate && FMath::IsNearlyEqual(Blend, .5f) && Next != Layer);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBlocksTexturesTest, "Unnamed.Foundation.Blocks.Textures", BlockTestFlags)

bool FFoundationBlocksTexturesTest::RunTest(const FString&)
{
    const UFoundationBlockAssets* Assets = FFoundationBlockRegistry::GetDefault()->GetAssets();
    const TSharedPtr<FJsonObject> Reference = ReadReference(TEXT("textures.json"));
    if (!TestNotNull(TEXT("assets"), Assets) || !TestValid(TEXT("textures.json"), Reference)) return false;
    UTexture2DArray* Blocks = Assets->BlockTextures;
    UTexture2DArray* Flow = Assets->WaterFlowTextures;
    if (!TestNotNull(TEXT("arrays"), Blocks) || !TestNotNull(TEXT("flow array"), Flow)) return false;
    for (UTexture2DArray* Array : {Blocks, Flow})
        TestTrue(Array->GetName() + TEXT(" settings: uncompressed BGRA8, sRGB, nearest, own mips"),
            Array->CompressionSettings == TC_EditorIcon && Array->SRGB && Array->Filter == TF_Nearest &&
                Array->MipGenSettings == TMGS_LeaveExistingMips && Array->Source.GetFormat() == TSF_BGRA8 &&
                Array->Source.GetNumMips() >= 5);
    // Mip 0 of every layer is the jar PNG's pixels: hashes recorded by an independent decoder (Pillow).
    int32 Checked = 0, Different = 0;
    for (const TSharedPtr<FJsonValue>& Value : Reference->GetArrayField(TEXT("textures")))
    {
        const TSharedPtr<FJsonObject> Texture = Value->AsObject();
        UTexture2DArray* Array = Texture->GetStringField(TEXT("array")) == TEXT("water_flow") ? Flow : Blocks;
        const int32 First = int32(Texture->GetNumberField(TEXT("first_layer")));
        const TArray<TSharedPtr<FJsonValue>>& Hashes = Texture->GetArrayField(TEXT("layer_rgba_sha256"));
        for (int32 Frame = 0; Frame < Hashes.Num(); ++Frame, ++Checked)
            if (UFoundationBlockImport::HashLayer(Array, First + Frame) != Hashes[Frame]->AsString())
            {
                ++Different;
                AddError(FString::Printf(TEXT("%s frame %d differs"), *Texture->GetStringField(TEXT("name")), Frame));
            }
    }
    TestEqual(TEXT("layers checked"), Checked, 719);
    TestEqual(TEXT("layers whose pixels differ from the jar"), Different, 0);
    return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBlocksPlacementTest, "Unnamed.Foundation.Blocks.Placement", BlockTestFlags)

bool FFoundationBlocksPlacementTest::RunTest(const FString&)
{
    using namespace FoundationBlockPlacement;
    FBlocks B;
    // Direction.fromYRot: floor(yaw / 90 + 0.5) & 3 -> south, west, north, east.
    for (const auto& [Yaw, Expected] : TArray<TTuple<double, EFoundationFace>>{{0, EFoundationFace::South},
             {44.99, EFoundationFace::South}, {45, EFoundationFace::West}, {90, EFoundationFace::West},
             {180, EFoundationFace::North}, {-90, EFoundationFace::East}, {270, EFoundationFace::East},
             {-45.01, EFoundationFace::East}, {-45, EFoundationFace::South}})
        TestEqual(FString::Printf(TEXT("horizontal look at yaw %.2f"), Yaw), GetHorizontalLook(Yaw), Expected);
    // Direction.orderedByNearest: the dominant axis of the look, then the others.
    for (const auto& [Yaw, Pitch, Expected] : TArray<TTuple<double, double, EFoundationFace>>{
             {0, 0, EFoundationFace::South}, {90, 10, EFoundationFace::West}, {180, -20, EFoundationFace::North},
             {-90, 0, EFoundationFace::East}, {0, 60, EFoundationFace::Down}, {33, -80, EFoundationFace::Up}})
        TestEqual(FString::Printf(TEXT("nearest look yaw %.0f pitch %.0f"), Yaw, Pitch), GetNearestLooks(Yaw, Pitch)[0],
            Expected);
    const TStaticArray<EFoundationFace, 6> Order = GetNearestLooks(30, 20);
    // Yaw 30, pitch 20: south 0.87, west 0.5 x cos 20, down sin 20 -> south, west, down, then the opposites.
    TestTrue(TEXT("full order: south, west, down, then opposites"),
        Order[0] == EFoundationFace::South && Order[1] == EFoundationFace::West && Order[2] == EFoundationFace::Down &&
            Order[3] == EFoundationFace::Up && Order[4] == EFoundationFace::East && Order[5] == EFoundationFace::North);
    double Yaw, Pitch;
    GetYawPitch(FVector(0, 1, 0), Yaw, Pitch);
    TestTrue(
        TEXT("Unreal +Y (south) is Minecraft yaw 0"), FMath::IsNearlyZero(Yaw, 1e-9) && FMath::IsNearlyZero(Pitch));
    GetYawPitch(FVector(1, 0, -1), Yaw, Pitch);
    TestTrue(
        TEXT("east and down: yaw -90, pitch 45"), FMath::IsNearlyEqual(Yaw, -90.) && FMath::IsNearlyEqual(Pitch, 45.));

    // axis from the clicked face (RotatedPillarBlock).
    for (const auto& [Face, Axis] : TArray<TTuple<EFoundationFace, const TCHAR*>>{{EFoundationFace::Up, TEXT("y")},
             {EFoundationFace::Down, TEXT("y")}, {EFoundationFace::East, TEXT("x")}, {EFoundationFace::West, TEXT("x")},
             {EFoundationFace::North, TEXT("z")}, {EFoundationFace::South, TEXT("z")}})
        TestEqual(FString::Printf(TEXT("oak_log on %s"), FoundationGrid::FaceName(Face)),
            Value(B.R(), B.Place(TEXT("oak_log"), {0, 0, 0}, Face), TEXT("axis")), FName(Axis));
    // facing the player (furnace-likes) from the horizontal look; six-way blocks from the nearest look.
    for (const auto& [LookYaw, Facing] : TArray<TTuple<double, const TCHAR*>>{
             {0, TEXT("north")}, {90, TEXT("east")}, {180, TEXT("south")}, {-90, TEXT("west")}})
    {
        TestEqual(FString::Printf(TEXT("furnace placed looking yaw %.0f"), LookYaw),
            Value(B.R(), B.Place(TEXT("furnace"), {0, 0, 0}, EFoundationFace::Up, LookYaw, 40), TEXT("facing")),
            FName(Facing));
        TestEqual(FString::Printf(TEXT("carved pumpkin yaw %.0f"), LookYaw),
            Value(B.R(), B.Place(TEXT("carved_pumpkin"), {0, 0, 0}, EFoundationFace::Up, LookYaw, 40), TEXT("facing")),
            FName(Facing));
    }
    TestEqual(TEXT("dispenser placed looking down faces up"),
        Value(B.R(), B.Place(TEXT("dispenser"), {0, 0, 0}, EFoundationFace::Up, 0, 70), TEXT("facing")),
        FName(TEXT("up")));
    TestEqual(TEXT("barrel placed looking south faces north"),
        Value(B.R(), B.Place(TEXT("barrel"), {0, 0, 0}, EFoundationFace::North, 0, 10), TEXT("facing")),
        FName(TEXT("north")));
    TestEqual(TEXT("observer placed looking down faces down"),
        Value(B.R(), B.Place(TEXT("observer"), {0, 0, 0}, EFoundationFace::Up, 0, 70), TEXT("facing")),
        FName(TEXT("down")));
    TestEqual(TEXT("crafter looking down: front up, top toward the player's back"),
        Value(B.R(), B.Place(TEXT("crafter"), {0, 0, 0}, EFoundationFace::Up, 0, 70), TEXT("orientation")),
        FName(TEXT("up_south")));
    TestEqual(TEXT("crafter looking west: front east, top up"),
        Value(B.R(), B.Place(TEXT("crafter"), {0, 0, 0}, EFoundationFace::East, 90, 0), TEXT("orientation")),
        FName(TEXT("east_up")));
    TestEqual(TEXT("jigsaw on a floor: front up, top opposite the look"),
        Value(B.R(), B.Place(TEXT("jigsaw"), {0, 0, 0}, EFoundationFace::Up, 0, 70), TEXT("orientation")),
        FName(TEXT("up_north")));

    // Slab halves (SlabBlock.getStateForPlacement): the top face places a bottom slab, the bottom face a top slab,
    // a side picks by the hit's height in the destination cell.
    for (const auto& [Face, Height, Type] : TArray<TTuple<EFoundationFace, double, const TCHAR*>>{
             {EFoundationFace::Up, 1.0, TEXT("bottom")}, {EFoundationFace::Down, 0.0, TEXT("top")},
             {EFoundationFace::East, .3, TEXT("bottom")}, {EFoundationFace::East, .5, TEXT("bottom")},
             {EFoundationFace::East, .51, TEXT("top")}, {EFoundationFace::North, .9, TEXT("top")}})
        TestEqual(FString::Printf(TEXT("slab on %s at %.2f"), FoundationGrid::FaceName(Face), Height),
            Value(B.R(), B.Place(TEXT("oak_slab"), {0, 0, 0}, Face, 0, 0, Height), TEXT("type")), FName(Type));
    // Completing a single slab (SlabBlock.canBeReplaced): only the same slab, only from the open side.
    B.Put({0, 0, 0}, TEXT("oak_slab[type=bottom,waterlogged=false]"));
    TestEqual(TEXT("bottom slab clicked from above becomes double"),
        Value(B.R(), B.Place(TEXT("oak_slab"), {0, 0, 0}, EFoundationFace::Up, 0, 0, .5), TEXT("type")),
        FName(TEXT("double")));
    TestEqual(TEXT("in place"), B.LastDestination, FIntVector(0, 0, 0));
    TestEqual(TEXT("bottom slab clicked on its side, upper half (open air): double"),
        Value(B.R(), B.Place(TEXT("oak_slab"), {0, 0, 0}, EFoundationFace::East, 0, 0, .75), TEXT("type")),
        FName(TEXT("double")));
    B.Place(TEXT("oak_slab"), {0, 0, 0}, EFoundationFace::East, 0, 0, .25);
    TestEqual(TEXT("bottom slab clicked on its side, lower half: beside it"), B.LastDestination, FIntVector(1, 0, 0));
    B.Place(TEXT("stone_slab"), {0, 0, 0}, EFoundationFace::Up, 0, 0, .5);
    TestEqual(TEXT("another slab never merges"), B.LastDestination, FIntVector(0, 0, 1));
    B.Put({0, 0, 0}, TEXT("oak_slab[type=top,waterlogged=false]"));
    TestEqual(TEXT("top slab clicked from below becomes double"),
        Value(B.R(), B.Place(TEXT("oak_slab"), {0, 0, 0}, EFoundationFace::Down, 0, 0, .5), TEXT("type")),
        FName(TEXT("double")));
    // A single slab next to the clicked block is completed too (canPlace with replaceClicked false).
    B.Put({0, 0, 0}, TEXT("stone"));
    B.Put({1, 0, 0}, TEXT("oak_slab[type=bottom,waterlogged=false]"));
    TestEqual(TEXT("slab beside the clicked block completes"),
        Value(B.R(), B.Place(TEXT("oak_slab"), {0, 0, 0}, EFoundationFace::East, 0, 0, .9), TEXT("type")),
        FName(TEXT("double")));
    B.Put({1, 0, 0}, TEXT("oak_slab[type=double,waterlogged=false]"));
    FFoundationPlacementContext Full;
    Full.ClickedCell = {0, 0, 0};
    Full.ClickedFace = EFoundationFace::East;
    TestFalse(TEXT("a double slab accepts nothing"),
        ResolveDestination(B.World, *B.World.FindBlock(TEXT("oak_slab")), true, Full));
    // Water and air are replaceable; stone is not.
    B.Put({5, 0, 0}, TEXT("water[level=0]"));
    B.Place(TEXT("stone"), {5, 0, 0}, EFoundationFace::Up);
    TestEqual(TEXT("water is replaced in place"), B.LastDestination, FIntVector(5, 0, 0));

    // Leaves: persistent, distance 1 beside a log, 7 alone. Grass under snow is snowy.
    B.Put({0, 5, 0}, TEXT("oak_log[axis=y]"));
    const FFoundationBlockState Leaves = B.Place(TEXT("oak_leaves"), {0, 5, 0}, EFoundationFace::East);
    TestTrue(TEXT("leaves beside a log"),
        Value(B.R(), Leaves, TEXT("persistent")) == TEXT("true") &&
            Value(B.R(), Leaves, TEXT("distance")) == TEXT("1"));
    TestEqual(TEXT("leaves alone"),
        Value(B.R(), B.Place(TEXT("oak_leaves"), {-10, -10, 0}, EFoundationFace::Up), TEXT("distance")),
        FName(TEXT("7")));
    B.Put({3, 3, 0}, TEXT("stone"));
    B.Put({3, 3, 2}, TEXT("snow_block"));
    TestEqual(TEXT("grass under snow"),
        Value(B.R(), B.Place(TEXT("grass_block"), {3, 3, 0}, EFoundationFace::Up), TEXT("snowy")), FName(TEXT("true")));
    TestEqual(TEXT("plain block keeps its default"), B.Place(TEXT("stone"), {-5, -5, 0}, EFoundationFace::Up),
        B.World.FindBlock(TEXT("stone"))->GetDefaultState());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBlocksShapesTest, "Unnamed.Foundation.Blocks.Shapes", BlockTestFlags)

bool FFoundationBlocksShapesTest::RunTest(const FString&)
{
    FBlocks B;
    FBox Box;
    TestTrue(TEXT("bottom slab Z 0-0.5"),
        B.R().GetCollisionShape(B.State(TEXT("oak_slab[type=bottom,waterlogged=false]")), Box) &&
            Box == FBox(FVector::ZeroVector, FVector(1, 1, .5)));
    TestTrue(TEXT("top slab Z 0.5-1"),
        B.R().GetSelectionShape(B.State(TEXT("oak_slab[type=top,waterlogged=false]")), Box) &&
            Box == FBox(FVector(0, 0, .5), FVector::OneVector));
    TestTrue(TEXT("double slab full"),
        B.R().GetCollisionShape(B.State(TEXT("oak_slab[type=double,waterlogged=false]")), Box) &&
            Box == FBox(FVector::ZeroVector, FVector::OneVector));
    TestFalse(TEXT("water has no collision"), B.R().GetCollisionShape(B.State(TEXT("water[level=0]")), Box));
    TestFalse(TEXT("water has no selection"), B.R().GetSelectionShape(B.State(TEXT("water[level=3]")), Box));
    // Water's fluid view (26.3 FlowingFluid): source 8/9, level 3 flows at amount 5, falling water is 8.
    const FFoundationFluid Source = B.R().GetFluid(B.State(TEXT("water[level=0]")));
    const FFoundationFluid Flowing = B.R().GetFluid(B.State(TEXT("water[level=3]")));
    const FFoundationFluid Falling = B.R().GetFluid(B.State(TEXT("water[level=9]")));
    TestTrue(
        TEXT("source"), Source.bSource && Source.Amount == 8 && FMath::IsNearlyEqual(Source.GetHeight(false), 8. / 9));
    TestTrue(TEXT("flowing"),
        !Flowing.bSource && Flowing.Amount == 5 && FMath::IsNearlyEqual(Flowing.GetHeight(false), 5. / 9));
    TestTrue(
        TEXT("falling, full under water"), Falling.bFalling && Falling.Amount == 8 && Falling.GetHeight(true) == 1.);
    // Targeting and movement see the half: a ray over a bottom slab passes, one into its top face hits at z 50.
    B.Put({0, 0, 0}, TEXT("smooth_stone_slab[type=bottom,waterlogged=false]"));
    B.Put({2, 0, 0}, TEXT("water[level=0]"));
    FFoundationRay Ray;
    Ray.Eye = FVector(-150, 50, 75);
    Ray.Direction = FVector(1, 0, 0);
    TestEqual(TEXT("ray above a bottom slab and through water hits nothing"),
        FoundationTargeting::Resolve(Ray, B.World, {}, {}).Kind, EFoundationHitKind::None);
    Ray.Eye = FVector(50, 50, 300);
    Ray.Direction = FVector(0, 0, -1);
    const FFoundationHit Hit = FoundationTargeting::Resolve(Ray, B.World, {}, {});
    TestTrue(TEXT("down onto the slab's top face"),
        Hit.Kind == EFoundationHitKind::Block && Hit.Face == EFoundationFace::Up &&
            FMath::IsNearlyEqual(Hit.Point.Z, 50.));
    TArray<FBox> Boxes;
    B.World.GatherCollision(FBox(FVector(-100, -100, -100), FVector(400, 200, 200)), Boxes);
    TestTrue(
        TEXT("collision: the half slab only, no water"), Boxes.Num() == 1 && FMath::IsNearlyEqual(Boxes[0].Max.Z, 50.));
    return true;
}
#endif
