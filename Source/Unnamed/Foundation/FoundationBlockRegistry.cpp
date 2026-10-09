#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"

namespace
{
    const FBox FullShape(FVector::ZeroVector, FVector::OneVector);
    const FBox BottomSlabShape(FVector::ZeroVector, FVector(1, 1, .5));
    const FBox TopSlabShape(FVector(0, 0, .5), FVector::OneVector);

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

    int32 CountStates(TConstArrayView<FFoundationBlockAssetProperty> Properties)
    {
        int32 Count = 1;
        for (const FFoundationBlockAssetProperty& Property : Properties)
            Count *= FMath::Max(1, Property.Values.Num());
        return Count;
    }
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

    int32 NextProtocol = 1, NextState = 1;
    if (Assets)
        for (const FFoundationBlockAsset& Source : Assets->Blocks)
        {
            const int32 States = CountStates(Source.Properties);
            if (Source.Id == TEXT("air") || Source.Id.IsNone()) continue;
            if (Source.StateShapes.Num() != States ||
                Source.StateFaces.Num() != States * FoundationBlockFaces::CodesPerState ||
                Source.DefaultStateId < Source.MinStateId || Source.DefaultStateId >= Source.MinStateId + States ||
                Registry->ById.Contains(Source.Id) || Registry->ByProtocol.Contains(Source.ProtocolId))
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
            NextProtocol = FMath::Max(NextProtocol, Source.ProtocolId + 1);
            NextState = FMath::Max(NextState, Source.MinStateId + States);
            Registry->AddBlock(MoveTemp(Block), Source.StateShapes, Source.StateFaces);
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
    ById.Add(Block.Id, Index);
    ByProtocol.Add(Block.ProtocolId, Index);
    Blocks.Add(MoveTemp(Block));
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
    if (const int32 Layers = GetFoundationShapeLayers(Shape))
    {
        OutBox = FBox(FVector::ZeroVector, FVector(1, 1, Layers / 8.));
        return true;
    }
    return GetSolidShape(Shape, OutBox);
}

bool FFoundationBlockRegistry::GetCollisionShape(FFoundationBlockState State, FBox& OutBox) const
{
    const EFoundationBlockShape Shape = GetShape(State);
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
    if (!Block.IsFluid()) return Fluid;
    Fluid.bWater = Block.IsWater();
    Fluid.bLava = Block.IsLava();
    Fluid.Level = State.Id - Block.MinStateId;
    Fluid.bSource = Fluid.Level == 0;
    Fluid.bFalling = Fluid.Level >= 8;
    Fluid.Amount = Fluid.bSource || Fluid.bFalling ? 8 : 8 - Fluid.Level;
    return Fluid;
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
