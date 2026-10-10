#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2DArray.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockImport.h"
#include "Foundation/FoundationBlockMipmaps.h"
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

    const TCHAR* const FaceNames[6] = {
        TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south"), TEXT("down"), TEXT("up")};

    int32 FaceIndex(const FString& Name)
    {
        for (int32 Index = 0; Index < 6; ++Index)
            if (Name == FaceNames[Index]) return Index;
        return INDEX_NONE;
    }

    // Mojang block space (x east, y up, z south) to Unreal block space: (x, y, z) -> (x, z, y).
    FVector3f ToUnreal(const TArray<TSharedPtr<FJsonValue>>& Point)
    {
        return FVector3f(Point[0]->AsNumber(), Point[2]->AsNumber(), Point[1]->AsNumber());
    }

    FBox ToUnrealBox(const TArray<TSharedPtr<FJsonValue>>& Box)
    {
        return FBox(FVector(Box[0]->AsNumber(), Box[2]->AsNumber(), Box[1]->AsNumber()),
            FVector(Box[3]->AsNumber(), Box[5]->AsNumber(), Box[4]->AsNumber()));
    }

    // Registry boxes equal the facts' boxes, in order.
    bool BoxesMatch(const TArray<FBox>& Actual, const TArray<TSharedPtr<FJsonValue>>& Expected)
    {
        if (Actual.Num() != Expected.Num()) return false;
        for (int32 Index = 0; Index < Actual.Num(); ++Index)
        {
            const FBox Want = ToUnrealBox(Expected[Index]->AsArray());
            if (!Actual[Index].Min.Equals(Want.Min, 1e-5) || !Actual[Index].Max.Equals(Want.Max, 1e-5)) return false;
        }
        return true;
    }

    // The registry's model states (and blocks), counted from the blocks.
    int32 CountModelStates(const FFoundationBlockRegistry& Registry, int32* OutBlocks = nullptr)
    {
        int32 States = 0, Blocks = 0;
        for (int32 Index = 1; Index < Registry.GetBlockCount(); ++Index)
        {
            const FFoundationBlockType& Block = Registry.GetBlockAt(Index);
            const int32 Before = States;
            for (int32 Local = 0; Local < Block.StateCount; ++Local)
                States += Registry.GetShape({uint16(Block.MinStateId + Local)}) == EFoundationBlockShape::Model;
            Blocks += States > Before;
        }
        if (OutBlocks) *OutBlocks = Blocks;
        return States;
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
    // 614 blocks (429 cubes, 101 slabs, snow layers and 83 model blocks) plus air, water and lava, and no project
    // blocks by default.
    TestEqual(TEXT("air + 616 + water + lava"), Registry->GetBlockCount(), 619);
    int32 ModelBlocks = 0;
    CountModelStates(*Registry, &ModelBlocks);
    TestEqual(TEXT("blocks with shape Model"), ModelBlocks, 85);
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
    TestEqual(TEXT("states checked"), States, 4424);
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
    TestEqual(TEXT("every imported state"), States, 4424);
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
    int32 Faces = 0, Invalid = 0, ModelStates = 0, ModelCodes = 0;
    for (int32 Index = 1; Index < Registry->GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry->GetBlockAt(Index);
        if (Block.IsFluid()) continue;
        for (int32 Local = 0; Local < Block.StateCount; ++Local)
        {
            const FFoundationBlockState State{uint16(Block.MinStateId + Local)};
            const uint32* Codes = Registry->GetFaces(State);
            // Model states have no faces: their codes are all 0.
            if (Registry->GetShape(State) == EFoundationBlockShape::Model)
            {
                ++ModelStates;
                for (int32 Code = 0; Code < FoundationBlockFaces::CodesPerState; ++Code)
                    ModelCodes += Codes[Code] != 0;
                continue;
            }
            for (int32 Face = 0; Face < 6; ++Face, ++Faces)
                Invalid += !ValidTexture(FoundationBlockFaces::GetTexture(Codes[Face]));
            const uint32 Overlay = Codes[FoundationBlockFaces::OverlayIndex];
            if (Overlay & FoundationBlockFaces::OverlayBit)
                Invalid += !ValidTexture(FoundationBlockFaces::GetTexture(Overlay));
        }
    }
    // 4,392 states without the two fluids, of which the model states have no faces.
    TestEqual(TEXT("faces checked (non-model states x 6) plus model states x 6"), Faces + ModelStates * 6, 4392 * 6);
    TestTrue(TEXT("model states exist"), ModelStates > 0);
    TestEqual(TEXT("model state face codes that are not 0"), ModelCodes, 0);
    TestEqual(TEXT("faces without a valid array layer"), Invalid, 0);
    // Every model quad shows a real texture (not the importer's white texture 0) with a valid array layer.
    int32 Quads = 0, InvalidQuads = 0;
    for (int32 Model = 0; Model < Registry->GetModelCount(); ++Model)
        for (const FFoundationModelQuad& Quad : Registry->GetModelQuads(Model))
        {
            InvalidQuads += Quad.Texture == 0 || !ValidTexture(Quad.Texture);
            ++Quads;
        }
    TestTrue(TEXT("model quads checked"), Quads > 0);
    TestEqual(TEXT("model quads without a valid texture"), InvalidQuads, 0);
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
    // Mip 0 of every layer is the jar PNG's pixels: hashes recorded by an independent decoder (Pillow). The shelf
    // mushroom's 32 x 32 textures are the exception: their layer holds their own vanilla mip 1, checked below.
    int32 Checked = 0, Different = 0, Shelf = 0, ShelfDifferent = 0;
    for (const TSharedPtr<FJsonValue>& Value : Reference->GetArrayField(TEXT("textures")))
    {
        const TSharedPtr<FJsonObject> Texture = Value->AsObject();
        UTexture2DArray* Array = Texture->GetStringField(TEXT("array")) == TEXT("water_flow") ? Flow : Blocks;
        const int32 First = int32(Texture->GetNumberField(TEXT("first_layer")));
        if (int32(Texture->GetArrayField(TEXT("frame_size"))[0]->AsNumber()) != Array->Source.GetSizeX())
        {
            // The layer is level 1 of the 32 x 32 PNG's mip chain.
            const FString Name = Texture->GetStringField(TEXT("name"));
            TArray<uint8> Png, Layer;
            int32 Width = 0, Height = 0;
            FString Strategy = TEXT("auto");
            double Bias = FoundationBlockMipmaps::DefaultAlphaCutoffBias;
            Texture->TryGetStringField(TEXT("mipmap_strategy"), Strategy);
            Texture->TryGetNumberField(TEXT("alpha_cutoff_bias"), Bias);
            const FString Path = FPaths::ProjectDir() /
                TEXT("Reference/minecraft-blocks-26.3/assets/minecraft/textures/block") / (Name + TEXT(".png"));
            ++Shelf;
            TArray<TArray<uint8>> Mips;
            if (UFoundationBlockImport::DecodePngFile(Path, Png, Width, Height))
                Mips = FoundationBlockMipmaps::Generate(
                    Png, Width, Height, 2, FoundationBlockMipmaps::ParseStrategy(Strategy), float(Bias));
            if (Mips.Num() != 2 || !UFoundationBlockImport::ReadLayer(Array, First, Layer) || Layer != Mips[1])
            {
                ++ShelfDifferent;
                AddError(Name + TEXT(" is not level 1 of its 32x32 PNG"));
            }
            continue;
        }
        const TArray<TSharedPtr<FJsonValue>>& Hashes = Texture->GetArrayField(TEXT("layer_rgba_sha256"));
        for (int32 Frame = 0; Frame < Hashes.Num(); ++Frame, ++Checked)
            if (UFoundationBlockImport::HashLayer(Array, First + Frame) != Hashes[Frame]->AsString())
            {
                ++Different;
                AddError(FString::Printf(TEXT("%s frame %d differs"), *Texture->GetStringField(TEXT("name")), Frame));
            }
    }
    // 945 block frames and 48 flow frames (water 32, lava 16), less the two shelf mushroom frames.
    TestEqual(TEXT("layers checked"), Checked, 991);
    TestEqual(TEXT("layers whose pixels differ from the jar"), Different, 0);
    TestEqual(TEXT("shelf mushroom layers with their own mip 1"), Shelf, 2);
    TestEqual(TEXT("shelf mushroom layers that differ from level 1 of the 32x32 PNG"), ShelfDifferent, 0);

    // Mips 1-4 of every texture with an alpha cutoff bias or the strict_cutout strategy are 26.3's generated chain
    // from the asset's own mip 0, with the strategy and bias textures.json records.
    int32 MipTextures = 0, MipsChecked = 0, MipsDifferent = 0;
    for (const TSharedPtr<FJsonValue>& Value : Reference->GetArrayField(TEXT("textures")))
    {
        const TSharedPtr<FJsonObject> Texture = Value->AsObject();
        FString Strategy = TEXT("auto");
        double Bias = FoundationBlockMipmaps::DefaultAlphaCutoffBias;
        Texture->TryGetStringField(TEXT("mipmap_strategy"), Strategy);
        const bool bHasBias = Texture->TryGetNumberField(TEXT("alpha_cutoff_bias"), Bias);
        if (!bHasBias && Strategy != TEXT("strict_cutout")) continue;
        ++MipTextures;
        UTexture2DArray* Array = Texture->GetStringField(TEXT("array")) == TEXT("water_flow") ? Flow : Blocks;
        const int32 Size = Array->Source.GetSizeX();
        const FString Name = Texture->GetStringField(TEXT("name"));
        const int32 First = int32(Texture->GetNumberField(TEXT("first_layer")));
        const int32 Frames = int32(Texture->GetNumberField(TEXT("frame_count")));
        for (int32 Frame = 0; Frame < Frames; ++Frame)
        {
            TArray<uint8> Base;
            TArray<TArray<uint8>> Expected;
            if (UFoundationBlockImport::ReadLayerMip(Array, First + Frame, 0, Base))
                Expected = FoundationBlockMipmaps::Generate(
                    Base, Size, Size, 5, FoundationBlockMipmaps::ParseStrategy(Strategy), float(Bias));
            bool bSame = Expected.Num() == 5;
            for (int32 Mip = 1; bSame && Mip < 5; ++Mip, ++MipsChecked)
            {
                TArray<uint8> Actual;
                bSame =
                    UFoundationBlockImport::ReadLayerMip(Array, First + Frame, Mip, Actual) && Actual == Expected[Mip];
            }
            if (!bSame)
            {
                ++MipsDifferent;
                AddError(FString::Printf(TEXT("%s frame %d: mips differ from the generated chain (%s, bias %g)"), *Name,
                    Frame, *Strategy, Bias));
            }
        }
    }
    TestEqual(TEXT("textures with a cutoff bias or strict_cutout"), MipTextures, 25);
    TestTrue(TEXT("mip levels checked"), MipsChecked > 0);
    TestEqual(TEXT("frames whose mips differ from the generated chain"), MipsDifferent, 0);

    // Budgets: the face code's 10-bit texture field and D3D12's array slice limit.
    const int32 TextureCount = Assets->Textures.Num();
    const int32 TextureLimit = int32(FoundationBlockFaces::TextureMask) + 1;
    const int32 BlockSlices = Blocks->Source.GetNumSlices();
    AddInfo(FString::Printf(
        TEXT("textures %d of %d, block array slices %d of 2048"), TextureCount, TextureLimit, BlockSlices));
    TestTrue(TEXT("textures fit the face code's texture field"), TextureCount <= TextureLimit);
    TestTrue(TEXT("block array slices fit D3D12's array limit"), BlockSlices <= 2048);
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
    // Snow layers: the outline is k/8 high and the collision one layer lower (26.3), so one layer has none; eight
    // layers are a full cube. Powder snow is a solid cube for now.
    TestTrue(TEXT("three snow layers outline Z 0-3/8"),
        B.R().GetSelectionShape(B.State(TEXT("snow[layers=3]")), Box) &&
            Box == FBox(FVector::ZeroVector, FVector(1, 1, 3. / 8)));
    TestTrue(TEXT("three snow layers collide to Z 2/8"),
        B.R().GetCollisionShape(B.State(TEXT("snow[layers=3]")), Box) &&
            Box == FBox(FVector::ZeroVector, FVector(1, 1, 2. / 8)));
    TestFalse(TEXT("one snow layer has no collision"), B.R().GetCollisionShape(B.State(TEXT("snow[layers=1]")), Box));
    TestTrue(TEXT("one snow layer can be targeted"), B.R().GetSelectionShape(B.State(TEXT("snow[layers=1]")), Box));
    TestTrue(TEXT("eight snow layers are a full cube"),
        B.R().GetShape(B.State(TEXT("snow[layers=8]"))) == EFoundationBlockShape::Full);
    TestTrue(TEXT("powder snow is solid"),
        B.R().GetCollisionShape(B.State(TEXT("powder_snow")), Box) &&
            Box == FBox(FVector::ZeroVector, FVector::OneVector));
    // Lava is a fluid like water (still for now): no collision or selection, light 15.
    const FFoundationFluid Lava = B.R().GetFluid(B.State(TEXT("lava[level=0]")));
    TestTrue(TEXT("lava source"), Lava.bLava && !Lava.bWater && Lava.bSource && Lava.Amount == 8);
    TestFalse(TEXT("lava has no collision"), B.R().GetCollisionShape(B.State(TEXT("lava[level=0]")), Box));
    TestFalse(TEXT("lava has no selection"), B.R().GetSelectionShape(B.State(TEXT("lava[level=0]")), Box));
    TestEqual(TEXT("lava gives light 15"), B.R().GetBlock(B.State(TEXT("lava[level=0]"))).LightEmission, 15);
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

    // Every block's offset and every model state's boxes, light, fluid and replaceable flag against the facts
    // (Mojang coordinates, converted to Unreal's).
    const TSharedPtr<FJsonObject> Facts = ReadReference(TEXT("facts/block_state_facts.json"));
    if (!TestValid(TEXT("block_state_facts.json"), Facts)) return false;
    const FFoundationBlockRegistry& Registry = B.R();
    int32 Mismatches = 0, FactBlocks = 0, ModelStates = 0;
    auto Fail = [&](const FString& Message)
    {
        if (++Mismatches <= 20) AddError(Message);
    };
    for (const TSharedPtr<FJsonValue>& BlockValue : Facts->GetArrayField(TEXT("blocks")))
    {
        const TSharedPtr<FJsonObject> Entry = BlockValue->AsObject();
        const FString Id = Entry->GetStringField(TEXT("id"));
        const FFoundationBlockType* Block = Registry.FindBlock(*Id);
        ++FactBlocks;
        if (!Block)
        {
            Fail(Id + TEXT(" is not in the registry"));
            continue;
        }
        // Offset: null means none.
        const TSharedPtr<FJsonObject>* Offset = nullptr;
        EFoundationOffsetKind Kind = EFoundationOffsetKind::None;
        double MaxXZ = 0, MinY = 0;
        bool bOutlineMoves = false, bCollisionMoves = false;
        if (Entry->TryGetObjectField(TEXT("offset"), Offset))
        {
            Kind = (*Offset)->GetStringField(TEXT("kind")) == TEXT("xyz") ? EFoundationOffsetKind::XYZ
                                                                          : EFoundationOffsetKind::XZ;
            MaxXZ = (*Offset)->GetNumberField(TEXT("max_xz"));
            MinY = (*Offset)->GetNumberField(TEXT("min_y"));
            bOutlineMoves = (*Offset)->GetBoolField(TEXT("outline_moves"));
            bCollisionMoves = (*Offset)->GetBoolField(TEXT("collision_moves"));
        }
        if (Block->OffsetKind != Kind || !FMath::IsNearlyEqual(Block->OffsetMaxHorizontal, float(MaxXZ), 1e-5f) ||
            !FMath::IsNearlyEqual(Block->OffsetMinVertical, float(MinY), 1e-5f) ||
            Block->bOutlineMovesWithOffset != bOutlineMoves || Block->bCollisionMovesWithOffset != bCollisionMoves)
            Fail(Id + TEXT(" offset differs from the facts"));
        for (const TSharedPtr<FJsonValue>& StateValue : Entry->GetArrayField(TEXT("states")))
        {
            const TSharedPtr<FJsonObject> Fact = StateValue->AsObject();
            const FFoundationBlockState State{uint16(Fact->GetNumberField(TEXT("id")))};
            if (Registry.GetShape(State) != EFoundationBlockShape::Model) continue;
            ++ModelStates;
            const FString Label = FString::Printf(TEXT("%s state %d"), *Id, State.Id);
            TArray<FBox> Outline, Collision;
            Registry.GetOutlineBoxes(State, Outline);
            Registry.GetCollisionBoxes(State, Collision);
            if (!BoxesMatch(Outline, Fact->GetArrayField(TEXT("outline")))) Fail(Label + TEXT(" outline boxes differ"));
            if (!BoxesMatch(Collision, Fact->GetArrayField(TEXT("collision"))))
                Fail(Label + TEXT(" collision boxes differ"));
            if (Registry.GetLightEmission(State) != int32(Fact->GetNumberField(TEXT("light"))))
                Fail(Label + TEXT(" light differs"));
            if (Block->bReplaceable != Fact->GetBoolField(TEXT("replaceable")))
                Fail(Label + TEXT(" replaceable differs"));
            const TSharedPtr<FJsonObject>* Fluid = nullptr;
            const FFoundationFluid Water = Registry.GetFluid(State);
            if (Fact->TryGetObjectField(TEXT("fluid"), Fluid)
                    ? !Water.bWater || Water.Amount != int32((*Fluid)->GetNumberField(TEXT("amount")))
                    : Water.bWater)
                Fail(Label + TEXT(" fluid differs"));
        }
    }
    TestEqual(TEXT("facts blocks (every imported block)"), FactBlocks, 618);
    TestEqual(TEXT("model states checked against the facts"), ModelStates, CountModelStates(Registry));
    TestTrue(TEXT("model states exist"), ModelStates > 0);
    TestEqual(TEXT("offsets, boxes, light, fluids and replaceable flags that differ from the facts"), Mismatches, 0);
    return true;
}

// The baked models and the model states' parts against the reference.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBlocksModelsTest, "Unnamed.Foundation.Blocks.Models", BlockTestFlags)

bool FFoundationBlocksModelsTest::RunTest(const FString&)
{
    FBlocks B;
    const FFoundationBlockRegistry& Registry = B.R();
    const UFoundationBlockAssets* Assets = Registry.GetAssets();
    const TSharedPtr<FJsonObject> ModelsJson = ReadReference(TEXT("block_models.json"));
    const TSharedPtr<FJsonObject> StatesJson = ReadReference(TEXT("block_states.json"));
    const TSharedPtr<FJsonObject> ItemsJson = ReadReference(TEXT("items.json"));
    if (!TestNotNull(TEXT("assets"), Assets) || !TestValid(TEXT("block_models.json"), ModelsJson) ||
        !TestValid(TEXT("block_states.json"), StatesJson) || !TestValid(TEXT("items.json"), ItemsJson))
        return false;
    int32 Mismatches = 0;
    auto Fail = [&](const FString& Message)
    {
        if (++Mismatches <= 20) AddError(Message);
    };
    // Texture index by name, built once.
    TMap<FName, int32> TextureIndex;
    for (int32 Index = 0; Index < Assets->Textures.Num(); ++Index)
        TextureIndex.FindOrAdd(Assets->Textures[Index].Name, Index);

    // Every model and quad.
    const TArray<TSharedPtr<FJsonValue>>& Models = ModelsJson->GetArrayField(TEXT("models"));
    TestEqual(TEXT("reference models"), Models.Num(), 308);
    TestEqual(TEXT("registry models"), Registry.GetModelCount(), 308);
    TestEqual(TEXT("registry models equal the reference's"), Registry.GetModelCount(), Models.Num());
    int32 ReferenceQuads = 0, RegistryQuads = 0;
    for (int32 Model = 0; Model < Registry.GetModelCount(); ++Model)
        RegistryQuads += Registry.GetModelQuads(Model).Num();
    for (int32 Model = 0; Model < Models.Num() && Model < Registry.GetModelCount(); ++Model)
    {
        const TSharedPtr<FJsonObject> Entry = Models[Model]->AsObject();
        const TArray<TSharedPtr<FJsonValue>>& Quads = Entry->GetArrayField(TEXT("quads"));
        const TConstArrayView<FFoundationModelQuad> Actual = Registry.GetModelQuads(Model);
        ReferenceQuads += Quads.Num();
        if (Registry.IsModelAmbientOccluded(Model) != Entry->GetBoolField(TEXT("ao")))
            Fail(FString::Printf(TEXT("model %d ambient occlusion differs"), Model));
        if (Actual.Num() != Quads.Num())
        {
            Fail(FString::Printf(TEXT("model %d has %d quads, expected %d"), Model, Actual.Num(), Quads.Num()));
            continue;
        }
        for (int32 Index = 0; Index < Quads.Num(); ++Index)
        {
            const TSharedPtr<FJsonObject> Quad = Quads[Index]->AsObject();
            const FFoundationModelQuad& Got = Actual[Index];
            const TArray<TSharedPtr<FJsonValue>>& Vertices = Quad->GetArrayField(TEXT("v"));
            const TArray<TSharedPtr<FJsonValue>>& UVs = Quad->GetArrayField(TEXT("uv"));
            bool bSame = Vertices.Num() == 4 && UVs.Num() == 4;
            for (int32 Corner = 0; bSame && Corner < 4; ++Corner)
            {
                const TArray<TSharedPtr<FJsonValue>>& UV = UVs[Corner]->AsArray();
                bSame = Got.Positions[Corner].Equals(ToUnreal(Vertices[Corner]->AsArray()), 1e-5f) &&
                    Got.UVs[Corner].Equals(FVector2f(UV[0]->AsNumber(), UV[1]->AsNumber()), 1e-5f);
            }
            const int32* Texture = TextureIndex.Find(FName(*Quad->GetStringField(TEXT("t"))));
            FString Cull;
            const int32 CullFace = Quad->TryGetStringField(TEXT("cull"), Cull) ? FaceIndex(Cull) : INDEX_NONE;
            bSame = bSame && Texture && Got.Texture == *Texture &&
                Got.TintIndex == int32(Quad->GetNumberField(TEXT("tint"))) && Got.CullFace == CullFace &&
                int32(Got.Facing) == FaceIndex(Quad->GetStringField(TEXT("facing"))) &&
                int32(Got.Shade) == FaceIndex(Quad->GetStringField(TEXT("shade"))) &&
                Got.Light == int32(Quad->GetNumberField(TEXT("light")));
            if (!bSame) Fail(FString::Printf(TEXT("model %d quad %d differs from the reference"), Model, Index));
        }
    }
    TestEqual(TEXT("reference quads"), ReferenceQuads, 2440);
    TestEqual(TEXT("registry quads"), RegistryQuads, 2440);

    // Every model state's parts and alternatives.
    int32 ModelStates = 0;
    for (const TSharedPtr<FJsonValue>& BlockValue : StatesJson->GetArrayField(TEXT("blocks")))
    {
        const TSharedPtr<FJsonObject> Block = BlockValue->AsObject();
        for (const TSharedPtr<FJsonValue>& StateValue : Block->GetArrayField(TEXT("states")))
        {
            const TSharedPtr<FJsonObject> State = StateValue->AsObject();
            if (State->GetStringField(TEXT("shape")) != TEXT("model")) continue;
            ++ModelStates;
            const FFoundationBlockState Id{uint16(State->GetNumberField(TEXT("id")))};
            TArray<TConstArrayView<int32>> Parts;
            const TArray<TSharedPtr<FJsonValue>>& Expected = State->GetArrayField(TEXT("model"));
            bool bSame = Registry.GetModelParts(Id, Parts) && Parts.Num() == Expected.Num();
            for (int32 Part = 0; bSame && Part < Parts.Num(); ++Part)
            {
                const TArray<TSharedPtr<FJsonValue>>& Alternatives = Expected[Part]->AsArray();
                bSame = Parts[Part].Num() == Alternatives.Num();
                for (int32 Alternative = 0; bSame && Alternative < Alternatives.Num(); ++Alternative)
                    bSame = Parts[Part][Alternative] == int32(Alternatives[Alternative]->AsNumber());
            }
            if (!bSame)
                Fail(FString::Printf(
                    TEXT("%s state %d: model parts differ"), *Block->GetStringField(TEXT("id")), Id.Id));
        }
    }
    TestTrue(TEXT("reference model states"), ModelStates > 0);
    TestEqual(TEXT("registry model states equal the reference's"), CountModelStates(Registry), ModelStates);
    TestEqual(TEXT("model data that differs from the reference"), Mismatches, 0);

    // Per-state spot checks: parts, the first part's alternatives, quads of the first alternative of every part.
    auto Spot = [&](const TCHAR* Text, int32 PartCount, int32 Alternatives, int32 QuadCount)
    {
        TArray<TConstArrayView<int32>> Parts;
        if (!TestTrue(FString(Text) + TEXT(" has model parts"), Registry.GetModelParts(B.State(Text), Parts)) ||
            Parts.IsEmpty())
            return;
        int32 Quads = 0;
        for (const TConstArrayView<int32>& Part : Parts)
            Quads += Registry.GetModelQuads(Part[0]).Num();
        if (PartCount >= 0) TestEqual(FString(Text) + TEXT(" parts"), Parts.Num(), PartCount);
        if (Alternatives >= 0) TestEqual(FString(Text) + TEXT(" alternatives of part 0"), Parts[0].Num(), Alternatives);
        if (QuadCount >= 0) TestEqual(FString(Text) + TEXT(" quads"), Quads, QuadCount);
    };
    Spot(TEXT("poppy"), 1, 1, 4);
    Spot(TEXT("seagrass"), -1, -1, 8);
    Spot(TEXT("tube_coral_fan[waterlogged=true]"), -1, -1, 8);
    Spot(TEXT("sunflower[half=upper]"), -1, -1, 6);
    Spot(TEXT("lily_pad"), 1, 4, -1);
    Spot(TEXT("sea_pickle[pickles=4,waterlogged=true]"), 1, 4, -1);
    Spot(TEXT("vine[east=true,north=false,south=false,up=false,west=false]"), -1, -1, 2);

    // Hand expectation from the jar's block/coral_fan.json: the first element's up face has uv [0, 0, 16, 16] with
    // rotation 90, so the quad's UVs turn and its positions follow the element's rotated plane.
    TArray<TConstArrayView<int32>> CoralParts;
    if (TestTrue(TEXT("coral fan parts"),
            Registry.GetModelParts(B.State(TEXT("tube_coral_fan[waterlogged=true]")), CoralParts)) &&
        TestTrue(TEXT("coral fan has a part"), !CoralParts.IsEmpty() && !CoralParts[0].IsEmpty()))
    {
        const TConstArrayView<FFoundationModelQuad> Coral = Registry.GetModelQuads(CoralParts[0][0]);
        if (TestTrue(TEXT("coral fan model has a second quad"), Coral.Num() > 1))
        {
            const FFoundationModelQuad& Quad = Coral[1];
            const FVector2f ExpectedUV[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
            const FVector3f ExpectedPosition[4] = {
                {0.5f, 0, 0}, {0.5f, 1, 0}, {1.42388f, 1, 0.382683f}, {1.42388f, 0, 0.382683f}};
            TestEqual(TEXT("coral fan quad 1 faces up"), Quad.Facing, EFoundationFace::Up);
            for (int32 Corner = 0; Corner < 4; ++Corner)
            {
                TestTrue(FString::Printf(TEXT("coral fan quad 1 UV %d"), Corner),
                    Quad.UVs[Corner].Equals(ExpectedUV[Corner], 1e-4f));
                TestTrue(FString::Printf(TEXT("coral fan quad 1 position %d"), Corner),
                    Quad.Positions[Corner].Equals(ExpectedPosition[Corner], 1e-4f));
            }
        }
    }

    // Item icons: a sprite (an IconAtlas tile) or a baked model with its gui transform, never both.
    int32 IconItems = 0, IconMismatches = 0;
    for (const TSharedPtr<FJsonValue>& ItemValue : ItemsJson->GetArrayField(TEXT("items")))
    {
        const TSharedPtr<FJsonObject> Item = ItemValue->AsObject();
        const TSharedPtr<FJsonObject>* Icon = nullptr;
        if (!Item->TryGetObjectField(TEXT("icon"), Icon)) continue;
        ++IconItems;
        FString BlockId;
        if (!Item->TryGetStringField(TEXT("block"), BlockId)) BlockId = Item->GetStringField(TEXT("id"));
        const FFoundationBlockType* Block = Registry.FindBlock(*BlockId);
        bool bSame = Block != nullptr;
        if (bSame && (*Icon)->HasField(TEXT("model")))
        {
            const TSharedPtr<FJsonObject> Gui = (*Icon)->GetObjectField(TEXT("gui"));
            auto Vector = [&Gui](const TCHAR* Name)
            {
                const TArray<TSharedPtr<FJsonValue>>& V = Gui->GetArrayField(Name);
                return FVector3f(V[0]->AsNumber(), V[1]->AsNumber(), V[2]->AsNumber());
            };
            bSame = Block->ItemIconModel == int32((*Icon)->GetNumberField(TEXT("model"))) &&
                Block->ItemIconModel < Registry.GetModelCount() && Block->ItemIconTile == INDEX_NONE &&
                Block->ItemIconRotation.Equals(Vector(TEXT("rotation")), 1e-4f) &&
                Block->ItemIconTranslation.Equals(Vector(TEXT("translation")), 1e-4f) &&
                Block->ItemIconScale.Equals(Vector(TEXT("scale")), 1e-4f);
        }
        else if (bSame)
            bSame = Block->ItemIconTile >= 0 && Block->ItemIconModel == INDEX_NONE;
        if (!bSame)
        {
            ++IconMismatches;
            if (IconMismatches <= 20) AddError(BlockId + TEXT(" item icon differs from items.json"));
        }
    }
    int32 TileBlocks = 0, ModelIconBlocks = 0;
    for (int32 Index = 1; Index < Registry.GetBlockCount(); ++Index)
    {
        TileBlocks += Registry.GetBlockAt(Index).ItemIconTile >= 0;
        ModelIconBlocks += Registry.GetBlockAt(Index).ItemIconModel >= 0;
    }
    TestEqual(TEXT("items with an icon"), IconItems, 72);
    TestEqual(TEXT("blocks with an icon sprite tile"), TileBlocks, 62);
    TestEqual(TEXT("blocks with an icon model"), ModelIconBlocks, 10);
    TestEqual(TEXT("item icons that differ from items.json"), IconMismatches, 0);
    return true;
}
#endif
