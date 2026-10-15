#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationPlantRules.h"
#include "HAL/IConsoleManager.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"

namespace
{
    TAutoConsoleVariable<int32> CVarBlocksFamilies(TEXT("foundation.Blocks.Families"), 1,
        TEXT("1: survival B1's block families (stairs, doors, beds, chests and the rest, DA_Blocks' bAppendedFamily) "
             "are in the creative inventory and the Playground galleries' tails, and place their default state. 0: "
             "they are absent and refuse placing, as before they were imported."),
        ECVF_Default);

    const FBox FullShape(FVector::ZeroVector, FVector::OneVector);
    const FBox BottomSlabShape(FVector::ZeroVector, FVector(1, 1, .5));
    const FBox TopSlabShape(FVector(0, 0, .5), FVector::OneVector);
    // A full cube holds a torch on top and on every side (EFoundationFace bits West..South and Up).
    constexpr uint8 TorchSidesAndTop = 0b101111;

    bool GetSolidShape(EFoundationBlockShape Shape, FBox& OutBox)
    {
        switch (Shape)
        {
        case EFoundationBlockShape::Full:
            OutBox = FullShape;
            return true;
        case EFoundationBlockShape::SlabBottom:
            OutBox = BottomSlabShape;
            return true;
        case EFoundationBlockShape::SlabTop:
            OutBox = TopSlabShape;
            return true;
        default:
            return false;
        }
    }

    FBox MakeBox(const float* P)
    {
        return FBox(FVector(P[0], P[1], P[2]), FVector(P[3], P[4], P[5]));
    }

    int32 CountStates(TConstArrayView<FFoundationBlockAssetProperty> Properties)
    {
        int32 Count = 1;
        for (const FFoundationBlockAssetProperty& Property : Properties)
            Count *= FMath::Max(1, Property.Values.Num());
        return Count;
    }
}

bool FoundationBlockFamilies::AreEnabled()
{
    return CVarBlocksFamilies.GetValueOnAnyThread() != 0;
}

bool FoundationBlockFamilies::IsHidden(const FFoundationBlockType* Block)
{
    return Block && Block->bAppendedFamily && !AreEnabled();
}

FFoundationModelQuad FoundationBlockModels::Decode(int32 Code, const float* Vertices)
{
    FFoundationModelQuad Quad;
    for (int32 Corner = 0; Corner < 4; ++Corner)
    {
        const float* V = Vertices + Corner * 5;
        Quad.Positions[Corner] = FVector3f(V[0], V[1], V[2]);
        Quad.UVs[Corner] = FVector2f(V[3], V[4]);
    }
    Quad.Texture = Code & 0x3FF;
    Quad.TintIndex = (Code >> 10 & 3) - 1;
    Quad.CullFace = (Code >> 12 & 7) - 1;
    Quad.Facing = EFoundationFace(FMath::Min(Code >> 15 & 7, 5));
    Quad.Shade = EFoundationFace(FMath::Min(Code >> 18 & 7, 5));
    Quad.Light = Code >> 21 & 15;
    return Quad;
}

FVector2f FoundationBlockFaces::GetCanonicalUV(EFoundationFace Face, const FVector3f& P)
{
    // Minecraft's defaultFaceUV with FaceInfo vertex order, in Unreal axes (X east, Y south, Z up).
    switch (Face)
    {
    case EFoundationFace::Down:
        return {P.X, 1 - P.Y};
    case EFoundationFace::Up:
        return {P.X, P.Y};
    case EFoundationFace::North:
        return {1 - P.X, 1 - P.Z};
    case EFoundationFace::South:
        return {P.X, 1 - P.Z};
    case EFoundationFace::West:
        return {P.Y, 1 - P.Z};
    case EFoundationFace::East:
        return {1 - P.Y, 1 - P.Z};
    default:
        return {0, 0};
    }
}

FVector2f FoundationBlockFaces::TransformUV(int32 Transform, FVector2f UV)
{
    if (Transform & 4) UV.X = 1 - UV.X;
    for (int32 Turn = 0; Turn < (Transform & 3); ++Turn)
        UV = FVector2f(UV.Y, 1 - UV.X);
    return UV;
}

void FoundationBlockFaces::GetFrame(
    const FFoundationBlockTextureAsset& Texture, int64 Tick, int32& OutLayer, int32& OutNextLayer, float& OutBlend)
{
    OutLayer = OutNextLayer = Texture.FirstLayer;
    OutBlend = 0;
    if (!Texture.IsAnimated() || Texture.FrameTicks.Num() != Texture.FrameIndices.Num()) return;
    int64 Cycle = 0;
    for (const int32 Ticks : Texture.FrameTicks)
        Cycle += FMath::Max(1, Ticks);
    // Each tick advances subFrame; the frame moves on once subFrame reaches its time, so after Tick ticks the
    // position in the cycle is Tick mod Cycle.
    int64 Position = ((Tick % Cycle) + Cycle) % Cycle;
    for (int32 Frame = 0; Frame < Texture.FrameIndices.Num(); ++Frame)
    {
        const int32 Ticks = FMath::Max(1, Texture.FrameTicks[Frame]);
        if (Position < Ticks)
        {
            const int32 Next = (Frame + 1) % Texture.FrameIndices.Num();
            OutLayer = Texture.FirstLayer + Texture.FrameIndices[Frame];
            OutNextLayer = Texture.FirstLayer + Texture.FrameIndices[Next];
            OutBlend = Texture.bInterpolate ? float(Position) / float(Ticks) : 0.f;
            return;
        }
        Position -= Ticks;
    }
}

TSharedRef<const FFoundationBlockRegistry> FFoundationBlockRegistry::GetDefault()
{
    static TSharedPtr<const FFoundationBlockRegistry> Default;
    if (!Default)
    {
        const UFoundationBlockAssets* Assets =
            Cast<UFoundationBlockAssets>(UUnnamedProjectSettings::GetBlockAssetPath(TEXT("DA_Blocks")).TryLoad());
        if (!Assets)
            UE_LOG(LogUnnamedFoundation, Warning,
                TEXT("Block registry asset %s is missing: only air and project blocks exist. Run ImportBlocks.py."),
                *UUnnamedProjectSettings::GetBlockAssetPath(TEXT("DA_Blocks")).ToString());
        if (Assets) const_cast<UFoundationBlockAssets*>(Assets)->AddToRoot();
        Default = Build(Assets, UFoundationInteractionSettings::Get().Blocks);
    }
    return Default.ToSharedRef();
}

TSharedRef<FFoundationBlockRegistry> FFoundationBlockRegistry::Build(
    const UFoundationBlockAssets* Assets, TArrayView<const FFoundationBlockDefinition> ProjectBlocks)
{
    TSharedRef<FFoundationBlockRegistry> Registry = MakeShared<FFoundationBlockRegistry>();
    Registry->Assets = Assets;

    FFoundationBlockType Air;
    Air.Id = TEXT("air");
    Air.DisplayName = TEXT("Air");
    Air.bReplaceable = true;
    const EFoundationBlockShape AirShape = EFoundationBlockShape::None;
    const int32 AirFaces[FoundationBlockFaces::CodesPerState] = {};
    Registry->AddBlock(MoveTemp(Air), MakeArrayView(&AirShape, 1), AirFaces);

    if (Assets)
    {
        const int32 QuadCount = Assets->ModelQuadCodes.Num();
        if (Assets->ModelQuadVertices.Num() == QuadCount * FoundationBlockModels::FloatsPerQuad)
            for (int32 Quad = 0; Quad < QuadCount; ++Quad)
            {
                FFoundationModelQuad& Decoded =
                    Registry->ModelQuads.Add_GetRef(FoundationBlockModels::Decode(Assets->ModelQuadCodes[Quad],
                        &Assets->ModelQuadVertices[Quad * FoundationBlockModels::FloatsPerQuad]));
                // A texture outside the asset's list draws white rather than reading past it.
                if (!Assets->Textures.IsValidIndex(Decoded.Texture))
                {
                    UE_LOG(LogUnnamedFoundation, Error, TEXT("Block registry: model quad %d has no texture %d"), Quad,
                        Decoded.Texture);
                    Decoded.Texture = 0;
                }
            }
        else
            UE_LOG(LogUnnamedFoundation, Error, TEXT("Block registry: model quad arrays disagree; models skipped"));
        for (const FFoundationBlockModelAsset& Model : Assets->Models)
        {
            const bool bValid = Model.FirstQuad >= 0 && Model.QuadCount >= 0 &&
                Model.FirstQuad + Model.QuadCount <= Registry->ModelQuads.Num();
            if (!bValid)
                UE_LOG(LogUnnamedFoundation, Error,
                    TEXT("Block registry: model %d has no valid quad range; drawn empty"), Registry->ModelRanges.Num());
            Registry->ModelRanges.Add(bValid ? FInt32Vector2(Model.FirstQuad, Model.QuadCount) : FInt32Vector2(0, 0));
            Registry->ModelOcclusion.Add(Model.bAmbientOcclusion);
        }
    }

    int32 NextProtocol = 1, NextState = 1;
    if (Assets)
        for (const FFoundationBlockAsset& Source : Assets->Blocks)
        {
            const int32 States = CountStates(Source.Properties);
            if (Source.Id == TEXT("air") || Source.Id.IsNone()) continue;
            if (Source.StateShapes.Num() != States ||
                Source.StateFaces.Num() != States * FoundationBlockFaces::CodesPerState ||
                Source.DefaultStateId < Source.MinStateId || Source.DefaultStateId >= Source.MinStateId + States ||
                Registry->ById.Contains(Source.Id) || Registry->ByProtocol.Contains(Source.ProtocolId) ||
                !Registry->IsValidModelData(Source, States))
            {
                UE_LOG(LogUnnamedFoundation, Error, TEXT("Block registry: skipping malformed or duplicate block %s"),
                    *Source.Id.ToString());
                continue;
            }
            FFoundationBlockType Block;
            Block.Id = Source.Id;
            Block.DisplayName = Source.DisplayName;
            Block.ProtocolId = Source.ProtocolId;
            Block.MinStateId = Source.MinStateId;
            Block.StateCount = States;
            Block.DefaultStateId = Source.DefaultStateId;
            Block.Properties = Source.Properties;
            Block.Hardness = Source.Hardness;
            Block.BlastResistance = Source.BlastResistance;
            Block.bRequiresCorrectToolForDrops = Source.bRequiresCorrectToolForDrops;
            Block.bReplaceable = Source.bReplaceable;
            Block.LightEmission = Source.LightEmission;
            Block.Tags = Source.Tags;
            Block.Layer = Source.Layer;
            Block.Placement = Source.Placement;
            Block.Tint = Source.Tint;
            Block.Color = FLinearColor(Source.ParticleColor);
            Block.Item = Source.Item;
            Block.DropItem = Source.Item;
            Block.ItemTint = Source.ItemTint;
            Block.ItemMaxStack = Source.ItemMaxStack;
            Block.ItemWallBlock = Source.ItemWallBlock;
            Block.ItemIconTile = Source.ItemIconTile;
            Block.ItemIconModel = Source.ItemIconModel;
            Block.ItemIconRotation = Source.ItemIconRotation;
            Block.ItemIconTranslation = Source.ItemIconTranslation;
            Block.ItemIconScale = Source.ItemIconScale;
            Block.OffsetKind = Source.OffsetKind;
            Block.OffsetMaxHorizontal = Source.OffsetMaxHorizontal;
            Block.OffsetMinVertical = Source.OffsetMinVertical;
            Block.bOutlineMovesWithOffset = Source.bOutlineMovesWithOffset;
            Block.bCollisionMovesWithOffset = Source.bCollisionMovesWithOffset;
            Block.bAppendedFamily = Source.bAppendedFamily;
            NextProtocol = FMath::Max(NextProtocol, Source.ProtocolId + 1);
            NextState = FMath::Max(NextState, Source.MinStateId + States);
            const int32 TableOffset = Registry->Shapes.Num();
            Registry->AddBlock(MoveTemp(Block), Source.StateShapes, Source.StateFaces);
            Registry->AddModelData(Source, TableOffset, States);
        }

    // Project blocks number after all of 26.3 (1,286 blocks, 35,723 states), imported or not: one full-cube state
    // drawn with texture 0 (plain white) tinted by their color.
    NextProtocol = FMath::Max(NextProtocol, 1286);
    NextState = FMath::Max(NextState, 35723);
    for (const FFoundationBlockDefinition& Definition : ProjectBlocks)
    {
        if (Definition.Id.IsNone() || Registry->ById.Contains(Definition.Id) || NextState > MAX_uint16)
        {
            UE_LOG(LogUnnamedFoundation, Warning, TEXT("Block registry: project block %s skipped (empty or taken id)"),
                *Definition.Id.ToString());
            continue;
        }
        FFoundationBlockType Block;
        Block.Id = Definition.Id;
        Block.DisplayName = Definition.DisplayName;
        Block.ProtocolId = NextProtocol++;
        Block.MinStateId = Block.DefaultStateId = NextState++;
        Block.Hardness = Definition.Hardness;
        Block.bRequiresCorrectToolForDrops = Definition.bRequiresCorrectToolForDrops;
        Block.bReplaceable = Definition.bReplaceable;
        Block.bNeedsSupportBelow = Definition.bNeedsSupportBelow;
        Block.Tags = Definition.Tags;
        Block.Color = Definition.Color;
        Block.Tint = Definition.Color.ToFColor(true);
        Block.DropItem = Definition.DropItem;
        Block.DropCount = Definition.DropCount;
        const EFoundationBlockShape Shape = EFoundationBlockShape::Full;
        int32 Codes[FoundationBlockFaces::CodesPerState] = {};
        for (int32 Face = 0; Face < 6; ++Face)
            Codes[Face] = int32(FoundationBlockFaces::TintBit | FoundationBlockFaces::CullBit);
        Registry->AddBlock(MoveTemp(Block), MakeArrayView(&Shape, 1), Codes);
    }

    // Standing-and-wall pairs (torch and wall_torch): both blocks follow the torch rule, and the wall block, which has
    // no item of its own, drops its standing block's item (the reference's vanilla_item: a wall torch drops a torch).
    for (FFoundationBlockType& Standing : Registry->Blocks)
    {
        const int32* Wall = Standing.ItemWallBlock.IsNone() ? nullptr : Registry->ById.Find(Standing.ItemWallBlock);
        if (!Wall) continue;
        FFoundationBlockType& WallBlock = Registry->Blocks[*Wall];
        Standing.bTorchRule = WallBlock.bTorchRule = true;
        if (WallBlock.DropItem.IsNone()) WallBlock.DropItem = Standing.DropItem;
    }
    // The coral fans' pairs (FoundationPlantRules::GetWallBlock) place the same way but keep the plants' survival
    // rules; a wall fan drops what its fan drops, as the wall torch does.
    for (FFoundationBlockType& Standing : Registry->Blocks)
    {
        const FName WallId = FoundationPlantRules::GetWallBlock(Standing.Id);
        const int32* Wall = WallId.IsNone() || !Standing.ItemWallBlock.IsNone() ? nullptr : Registry->ById.Find(WallId);
        if (!Wall) continue;
        Standing.ItemWallBlock = WallId;
        FFoundationBlockType& WallBlock = Registry->Blocks[*Wall];
        if (WallBlock.DropItem.IsNone()) WallBlock.DropItem = Standing.DropItem;
    }
    return Registry;
}

void FFoundationBlockRegistry::AddBlock(
    FFoundationBlockType&& Block, TConstArrayView<EFoundationBlockShape> BlockShapes, TConstArrayView<int32> BlockFaces)
{
    const int32 Index = Blocks.Num();
    Block.TableOffset = Shapes.Num();
    Shapes.Append(BlockShapes.GetData(), BlockShapes.Num());
    for (const int32 Code : BlockFaces)
        Faces.Add(uint32(Code));
    if (StateBlocks.Num() < Block.MinStateId + Block.StateCount)
    {
        const int32 Old = StateBlocks.Num();
        StateBlocks.SetNum(Block.MinStateId + Block.StateCount);
        for (int32 State = Old; State < StateBlocks.Num(); ++State)
            StateBlocks[State] = INDEX_NONE;
    }
    for (int32 State = 0; State < Block.StateCount; ++State)
        StateBlocks[Block.MinStateId + State] = Index;
    for (int32 State = 0; State < BlockShapes.Num(); ++State)
    {
        StateModelStart.Add(INDEX_NONE);
        StateBoxRanges.Add(FIntVector::ZeroValue);
        StateLight.Add(Block.LightEmission);
        StateWater.Add(0);
        // Imported blocks overwrite it from the asset (AddModelData).
        StateSupport.Add(BlockShapes[State] == EFoundationBlockShape::Full ? TorchSidesAndTop : 0);
    }
    ById.Add(Block.Id, Index);
    ByProtocol.Add(Block.ProtocolId, Index);
    Blocks.Add(MoveTemp(Block));
}

bool FFoundationBlockRegistry::IsValidModelData(const FFoundationBlockAsset& Source, int32 States) const
{
    // Every array is empty or covers every state; model parts index existing models; box counts match the boxes.
    if (!Source.StateLight.IsEmpty() && Source.StateLight.Num() != States) return false;
    if (!Source.StateWater.IsEmpty() && Source.StateWater.Num() != States) return false;
    if (!Source.StateSupport.IsEmpty() && Source.StateSupport.Num() != States) return false;
    if (!Source.StateBoxCounts.IsEmpty())
    {
        if (Source.StateBoxCounts.Num() != States * 2) return false;
        int32 BoxCount = 0;
        for (const int32 Count : Source.StateBoxCounts)
        {
            if (Count < 0) return false;
            BoxCount += Count;
        }
        if (Source.StateBoxes.Num() != BoxCount * 6) return false;
    }
    if (Source.StateModels.IsEmpty()) return true;
    int32 Read = 0;
    const auto Next = [&Source, &Read](int32& Out)
    {
        if (!Source.StateModels.IsValidIndex(Read)) return false;
        Out = Source.StateModels[Read++];
        return true;
    };
    for (int32 State = 0; State < States; ++State)
    {
        int32 Parts, Alternatives, Model;
        // Zero parts is a state vanilla draws nothing for (a wall with no post and no sides).
        if (Source.StateShapes[State] != EFoundationBlockShape::Model || !Next(Parts) || Parts < 0) return false;
        for (int32 Part = 0; Part < Parts; ++Part)
        {
            if (!Next(Alternatives) || Alternatives < 1) return false;
            for (int32 Alternative = 0; Alternative < Alternatives; ++Alternative)
                if (!Next(Model) || !ModelRanges.IsValidIndex(Model)) return false;
        }
    }
    return Read == Source.StateModels.Num();
}

void FFoundationBlockRegistry::AddModelData(const FFoundationBlockAsset& Source, int32 TableOffset, int32 States)
{
    int32 Read = 0, BoxRead = 0;
    for (int32 State = 0; State < States; ++State)
    {
        const int32 Entry = TableOffset + State;
        if (!Source.StateModels.IsEmpty())
        {
            // Copy this state's encoding: parts, then per part the alternative count and the models.
            StateModelStart[Entry] = ModelParts.Num();
            const int32 Parts = Source.StateModels[Read];
            int32 End = Read + 1;
            for (int32 Part = 0; Part < Parts; ++Part)
                End += 1 + Source.StateModels[End];
            ModelParts.Append(&Source.StateModels[Read], End - Read);
            Read = End;
        }
        if (!Source.StateBoxCounts.IsEmpty())
        {
            const int32 Outline = Source.StateBoxCounts[State * 2], Collision = Source.StateBoxCounts[State * 2 + 1];
            StateBoxRanges[Entry] = FIntVector(Boxes.Num(), Outline, Collision);
            for (int32 Box = 0; Box < Outline + Collision; ++Box, ++BoxRead)
                Boxes.Add(MakeBox(&Source.StateBoxes[BoxRead * 6]));
        }
        if (!Source.StateLight.IsEmpty()) StateLight[Entry] = Source.StateLight[State];
        if (!Source.StateWater.IsEmpty()) StateWater[Entry] = Source.StateWater[State];
        if (!Source.StateSupport.IsEmpty()) StateSupport[Entry] = Source.StateSupport[State];
    }
}

bool FFoundationBlockRegistry::GetModelParts(
    FFoundationBlockState State, TArray<TConstArrayView<int32>>& OutParts) const
{
    OutParts.Reset();
    const FFoundationBlockType* Block;
    const int32 Local = GetLocalIndex(State, Block);
    const int32 Start = StateModelStart.IsValidIndex(Block->TableOffset + Local)
        ? StateModelStart[Block->TableOffset + Local]
        : INDEX_NONE;
    if (Start == INDEX_NONE) return false;
    int32 Read = Start + 1;
    for (int32 Part = 0; Part < ModelParts[Start]; ++Part)
    {
        OutParts.Add(TConstArrayView<int32>(&ModelParts[Read + 1], ModelParts[Read]));
        Read += 1 + ModelParts[Read];
    }
    return true;
}

TConstArrayView<FFoundationModelQuad> FFoundationBlockRegistry::GetModelQuads(int32 Model) const
{
    if (!ModelRanges.IsValidIndex(Model)) return {};
    return TConstArrayView<FFoundationModelQuad>(ModelQuads.GetData() + ModelRanges[Model].X, ModelRanges[Model].Y);
}

bool FFoundationBlockRegistry::IsModelAmbientOccluded(int32 Model) const
{
    return ModelOcclusion.IsValidIndex(Model) && ModelOcclusion[Model];
}

void FFoundationBlockRegistry::GetOutlineBoxes(FFoundationBlockState State, TArray<FBox>& OutBoxes) const
{
    OutBoxes.Reset();
    const FFoundationBlockType* Block;
    const int32 Local = GetLocalIndex(State, Block);
    const FIntVector Range = StateBoxRanges[Block->TableOffset + Local];
    if (Range.Y + Range.Z > 0)
    {
        OutBoxes.Append(Boxes.GetData() + Range.X, Range.Y);
        return;
    }
    FBox Box;
    if (GetSelectionShape(State, Box)) OutBoxes.Add(Box);
}

void FFoundationBlockRegistry::GetCollisionBoxes(FFoundationBlockState State, TArray<FBox>& OutBoxes) const
{
    OutBoxes.Reset();
    const FFoundationBlockType* Block;
    const int32 Local = GetLocalIndex(State, Block);
    const FIntVector Range = StateBoxRanges[Block->TableOffset + Local];
    if (Range.Y + Range.Z > 0)
    {
        OutBoxes.Append(Boxes.GetData() + Range.X + Range.Y, Range.Z);
        return;
    }
    FBox Box;
    if (GetCollisionShape(State, Box)) OutBoxes.Add(Box);
}

int32 FFoundationBlockRegistry::GetLightEmission(FFoundationBlockState State) const
{
    const FFoundationBlockType* Block;
    const int32 Local = GetLocalIndex(State, Block);
    return StateLight[Block->TableOffset + Local];
}

uint8 FFoundationBlockRegistry::GetTorchSupport(FFoundationBlockState State) const
{
    const FFoundationBlockType* Block;
    const int32 Local = GetLocalIndex(State, Block);
    return StateSupport[Block->TableOffset + Local];
}

const FFoundationBlockType* FFoundationBlockRegistry::FindBlock(FName Id) const
{
    const int32* Index = ById.Find(Id);
    return Index ? &Blocks[*Index] : nullptr;
}

const FFoundationBlockType* FFoundationBlockRegistry::FindByProtocolId(int32 ProtocolId) const
{
    const int32* Index = ByProtocol.Find(ProtocolId);
    return Index ? &Blocks[*Index] : nullptr;
}

bool FFoundationBlockRegistry::IsKnown(FFoundationBlockState State) const
{
    return StateBlocks.IsValidIndex(State.Id) && StateBlocks[State.Id] != INDEX_NONE;
}

const FFoundationBlockType& FFoundationBlockRegistry::GetBlock(FFoundationBlockState State) const
{
    return IsKnown(State) ? Blocks[StateBlocks[State.Id]] : Blocks[0];
}

int32 FFoundationBlockRegistry::GetLocalIndex(FFoundationBlockState State, const FFoundationBlockType*& OutBlock) const
{
    OutBlock = &GetBlock(State);
    return IsKnown(State) ? State.Id - OutBlock->MinStateId : 0;
}

FName FFoundationBlockRegistry::GetValue(FFoundationBlockState State, FName Property) const
{
    const FFoundationBlockType* Block;
    int32 Local = GetLocalIndex(State, Block);
    for (int32 Index = Block->Properties.Num() - 1; Index >= 0; --Index)
    {
        const FFoundationBlockAssetProperty& P = Block->Properties[Index];
        const int32 Count = FMath::Max(1, P.Values.Num());
        if (P.Name == Property) return P.Values.IsValidIndex(Local % Count) ? P.Values[Local % Count] : NAME_None;
        Local /= Count;
    }
    return NAME_None;
}

TArray<TPair<FName, FName>> FFoundationBlockRegistry::GetProperties(FFoundationBlockState State) const
{
    const FFoundationBlockType* Block;
    int32 Local = GetLocalIndex(State, Block);
    TArray<TPair<FName, FName>> Result;
    Result.SetNum(Block->Properties.Num());
    for (int32 Index = Block->Properties.Num() - 1; Index >= 0; --Index)
    {
        const FFoundationBlockAssetProperty& P = Block->Properties[Index];
        const int32 Count = FMath::Max(1, P.Values.Num());
        Result[Index] = {P.Name, P.Values.IsValidIndex(Local % Count) ? P.Values[Local % Count] : NAME_None};
        Local /= Count;
    }
    return Result;
}

bool FFoundationBlockRegistry::TryMakeState(const FFoundationBlockType& Block,
    TConstArrayView<TPair<FName, FName>> Properties, FFoundationBlockState& OutState,
    TArray<FString>* OutWarnings) const
{
    // Start from the default state's values, as Minecraft's readBlockState does, then apply what is given.
    TArray<int32> Indices;
    TArray<TPair<FName, FName>> Defaults = GetProperties(Block.GetDefaultState());
    for (int32 Index = 0; Index < Block.Properties.Num(); ++Index)
        Indices.Add(Block.Properties[Index].Values.IndexOfByKey(Defaults[Index].Value));
    bool bExact = true;
    for (const TPair<FName, FName>& Given : Properties)
    {
        const int32 Property = Block.FindProperty(Given.Key);
        const int32 Value =
            Property == INDEX_NONE ? INDEX_NONE : Block.Properties[Property].Values.IndexOfByKey(Given.Value);
        if (Value == INDEX_NONE)
        {
            bExact = false;
            if (OutWarnings)
                OutWarnings->Add(FString::Printf(TEXT("%s: %s %s=%s; kept the default"), *Block.Id.ToString(),
                    Property == INDEX_NONE ? TEXT("unknown property") : TEXT("unknown value"), *Given.Key.ToString(),
                    *Given.Value.ToString()));
            continue;
        }
        Indices[Property] = Value;
    }
    int32 Local = 0;
    for (int32 Index = 0; Index < Block.Properties.Num(); ++Index)
        Local = Local * FMath::Max(1, Block.Properties[Index].Values.Num()) + FMath::Max(0, Indices[Index]);
    OutState = {uint16(Block.MinStateId + Local)};
    return bExact;
}

FFoundationBlockState FFoundationBlockRegistry::WithValue(
    FFoundationBlockState State, FName Property, FName Value) const
{
    const FFoundationBlockType& Block = GetBlock(State);
    TArray<TPair<FName, FName>> Properties = GetProperties(State);
    for (TPair<FName, FName>& Pair : Properties)
        if (Pair.Key == Property) Pair.Value = Value;
    FFoundationBlockState Result;
    return TryMakeState(Block, Properties, Result) ? Result : State;
}

EFoundationBlockShape FFoundationBlockRegistry::GetShape(FFoundationBlockState State) const
{
    const FFoundationBlockType* Block;
    const int32 Local = GetLocalIndex(State, Block);
    return Shapes[Block->TableOffset + Local];
}

bool FFoundationBlockRegistry::GetSelectionShape(FFoundationBlockState State, FBox& OutBox) const
{
    const EFoundationBlockShape Shape = GetShape(State);
    if (Shape == EFoundationBlockShape::Model) return GetModelBounds(State, false, OutBox);
    if (const int32 Layers = GetFoundationShapeLayers(Shape))
    {
        OutBox = FBox(FVector::ZeroVector, FVector(1, 1, Layers / 8.));
        return true;
    }
    return GetSolidShape(Shape, OutBox);
}

bool FFoundationBlockRegistry::GetModelBounds(FFoundationBlockState State, bool bCollision, FBox& OutBox) const
{
    // A model state's outline or collision boxes as one bounding box, for callers that take a single box.
    const FFoundationBlockType* Block;
    const int32 Local = GetLocalIndex(State, Block);
    const FIntVector Range = StateBoxRanges[Block->TableOffset + Local];
    OutBox = FBox(ForceInit);
    for (int32 Box = 0; Box < (bCollision ? Range.Z : Range.Y); ++Box)
        OutBox += Boxes[Range.X + (bCollision ? Range.Y : 0) + Box];
    return OutBox.IsValid != 0;
}

bool FFoundationBlockRegistry::GetCollisionShape(FFoundationBlockState State, FBox& OutBox) const
{
    const EFoundationBlockShape Shape = GetShape(State);
    if (Shape == EFoundationBlockShape::Model) return GetModelBounds(State, true, OutBox);
    // Snow layers collide one layer lower than they show, as in 26.3, so a single layer has no collision.
    if (const int32 Layers = GetFoundationShapeLayers(Shape))
    {
        OutBox = FBox(FVector::ZeroVector, FVector(1, 1, (Layers - 1) / 8.));
        return Layers > 1;
    }
    return GetSolidShape(Shape, OutBox);
}

FFoundationFluid FFoundationBlockRegistry::GetFluid(FFoundationBlockState State) const
{
    FFoundationFluid Fluid;
    const FFoundationBlockType& Block = GetBlock(State);
    if (!Block.IsFluid())
    {
        // Waterlogged and always-water model states (kelp, seagrass) hold water: a source when the amount is 8.
        const int32 Water = StateWater[Block.TableOffset + (IsKnown(State) ? State.Id - Block.MinStateId : 0)];
        if (Water > 0)
        {
            Fluid.bWater = true;
            Fluid.bSource = Water == 8;
            Fluid.Amount = Water;
            Fluid.Level = Water == 8 ? 0 : 8 - Water;
        }
        return Fluid;
    }
    Fluid.bWater = Block.IsWater();
    Fluid.bLava = Block.IsLava();
    Fluid.Level = State.Id - Block.MinStateId;
    Fluid.bSource = Fluid.Level == 0;
    Fluid.bFalling = Fluid.Level >= 8;
    Fluid.Amount = Fluid.bSource || Fluid.bFalling ? 8 : 8 - Fluid.Level;
    return Fluid;
}

FFoundationBlockState FFoundationBlockRegistry::GetRemainder(FFoundationBlockState State) const
{
    static const FName Waterlogged(TEXT("waterlogged")), True(TEXT("true"));
    if (!IsKnown(State) || GetBlock(State).IsFluid()) return {};
    // Model states carry their water in the table; other waterlogged states (slabs) only in the property.
    if (!GetFluid(State).bSource && GetValue(State, Waterlogged) != True) return {};
    const FFoundationBlockType* Water = FindBlock(TEXT("water"));
    return Water ? Water->GetDefaultState() : FFoundationBlockState{};
}

const uint32* FFoundationBlockRegistry::GetFaces(FFoundationBlockState State) const
{
    const FFoundationBlockType* Block;
    const int32 Local = GetLocalIndex(State, Block);
    if (Block->IsAir()) return nullptr;
    return &Faces[(Block->TableOffset + Local) * FoundationBlockFaces::CodesPerState];
}

FFoundationSavedState FFoundationBlockRegistry::Save(FFoundationBlockState State) const
{
    return {GetBlock(State).Id, GetProperties(State)};
}

bool FFoundationBlockRegistry::Load(
    const FFoundationSavedState& Saved, FFoundationBlockState& OutState, TArray<FString>* OutWarnings) const
{
    const FFoundationBlockType* Block = FindBlock(Saved.Id);
    if (!Block)
    {
        if (OutWarnings) OutWarnings->Add(FString::Printf(TEXT("unknown block %s"), *Saved.Id.ToString()));
        return false;
    }
    TryMakeState(*Block, Saved.Properties, OutState, OutWarnings);
    return true;
}

FString FFoundationBlockRegistry::ToString(FFoundationBlockState State) const
{
    const FFoundationSavedState Saved = Save(State);
    FString Text = Saved.Id.ToString();
    if (Saved.Properties.IsEmpty()) return Text;
    TArray<FString> Parts;
    for (const TPair<FName, FName>& Pair : Saved.Properties)
        Parts.Add(Pair.Key.ToString() + TEXT("=") + Pair.Value.ToString());
    return Text + TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
}

bool FFoundationBlockRegistry::Parse(const FString& Text, FFoundationBlockState& OutState) const
{
    FString Id = Text.TrimStartAndEnd(), Body;
    if (Id.StartsWith(TEXT("minecraft:"))) Id.RightChopInline(10);
    int32 Open;
    if (Id.FindChar(TEXT('['), Open))
    {
        if (!Id.EndsWith(TEXT("]"))) return false;
        Body = Id.Mid(Open + 1, Id.Len() - Open - 2);
        Id.LeftInline(Open);
    }
    FFoundationSavedState Saved;
    Saved.Id = FName(*Id);
    TArray<FString> Pairs;
    Body.ParseIntoArray(Pairs, TEXT(","));
    for (const FString& Pair : Pairs)
    {
        FString Key, Value;
        if (!Pair.Split(TEXT("="), &Key, &Value)) return false;
        Saved.Properties.Add({FName(*Key.TrimStartAndEnd()), FName(*Value.TrimStartAndEnd())});
    }
    TArray<FString> Warnings;
    return Load(Saved, OutState, &Warnings) && Warnings.IsEmpty();
}
