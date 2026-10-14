#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationItemEntityView.h"
#include "Blocks/VoxelBlockTable.h"
#include "Foundation/FoundationBlockMeshes.h"
#include "Foundation/FoundationBlockTextureTable.h"
#include "Foundation/FoundationBreakEffects.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationItemMesh.h"
#include "Foundation/FoundationPlayground.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2DArray.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "UObject/ConstructorHelpers.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"
#include "Voxel/VoxelFluidSystem.h"
#include "Voxel/VoxelLights.h"
#include "Voxel/VoxelLook.h"
#include "Voxel/VoxelWorld.h"

namespace
{
    TAutoConsoleVariable<float> CVarHeldGlow(TEXT("foundation.Held.Glow"), 16000.f,
        TEXT("Held items' glow (items S4): emissive = sprite colour x this (nits), so a sprite turned from the "
             "sun still reads near its own colours, as vanilla lights held items in view space. 16000 brings the "
             "tour's items to their sprites' luma under the 75,000 lux sun at EV100 14.5. 0: lit by the scene alone."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarHeldLight(TEXT("foundation.Held.Light"), FoundationHeldLight::DefaultMode,
        TEXT("Held items' light in a generated world: 1 by the light at the player (block light from the torch pool, "
             "sky light from the open sky, the brighter), dark in a dark cave as vanilla; 0 the fixed "
             "foundation.Held.Glow whatever the light (items S4)."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarPlacePreview(TEXT("foundation.Place.Preview"), FoundationPlace::DefaultPreviewMode,
        TEXT("Destination preview: 0 none, as vanilla (the target's outline only); 1 the destination box, its fill and "
             "the "
             "placed and rejected flashes (the look until 2026-10-11)."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarHandFit(TEXT("foundation.Hand.Fit"), 1,
        TEXT(
            "Held item: 1 placed as vanilla's first-person frames show it (probe P1) and drawn at a fixed FOV of 70 "
            "whatever Options > FOV says; 0 the provisional pose from before (pivot 42, 24, -24; a block turned 10 and "
            "35 degrees), drawn at the view's FOV."),
        ECVF_Default);
    // foundation.Hand.Fit 1: the pivot (cm, view space) and its scale, fitted to P1's frames at FOV 70 (12 px RMS over
    // the unclipped edges of an apple, stick, iron pickaxe and sword and a stone; the old pose 150 px; record
    // Docs/Records/2026-10-12-Feel-B.md). Only pivot / scale is fixed by the frames: scale 2 draws a model block as a
    // world block. A held block takes block/block.json's firstperson_righthand display (rotation 0 45 0, scale 0.4), as
    // sprite items take their models'.
    const FVector HandFitPivot(71.9, 56.2, -52.2);
    constexpr double HandFitScale = 2;
    const FVector HandRestPivot(42, 24, -24);

    constexpr double InteractionViewBlockCm = UFoundationProfile::BlockCm;
    constexpr uint8 WaterLayer = 3;
    const TCHAR* LayerMaterialNames[] = {
        TEXT("M_BlockOpaque"), TEXT("M_BlockCutout"), TEXT("M_BlockTranslucent"), TEXT("M_BlockWater")};

    int32 FindTexture(const UFoundationBlockAssets* Assets, FName Name)
    {
        return Assets ? Assets->Textures.IndexOfByPredicate(
                            [Name](const FFoundationBlockTextureAsset& Texture) { return Texture.Name == Name; })
                      : INDEX_NONE;
    }

    // A model state holding water (seagrass, kelp, waterlogged plants).
    bool IsHeldWater(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
    {
        return Registry.GetShape(State) == EFoundationBlockShape::Model && Registry.GetFluid(State).Amount > 0;
    }
    // Water itself, or a state holding it.
    bool HoldsWater(const FFoundationBlockRegistry& Registry, FFoundationBlockState State)
    {
        return Registry.GetBlock(State).IsWater() || IsHeldWater(Registry, State);
    }

    // A block's position offset at a cell in centimetres (FoundationBlockOffsets, as the voxel world's table and the
    // block world's boxes move it).
    FVector GetModelOffsetCm(const FFoundationBlockType& Type, const FIntVector& Cell)
    {
        return FoundationBlockOffsets::GetOffset(Type, Cell) * InteractionViewBlockCm;
    }

    FLinearColor TargetColor(bool bDead)
    {
        return bDead ? FLinearColor(.25f, .25f, .27f) : FLinearColor(.9f, .35f, .15f);
    }
}

UFoundationInteractionView::UFoundationInteractionView()
{
    PrimaryComponentTick.bCanEverTick = true;
    // After the pawn's steps (post physics): this frame's commits and aim are what gets drawn.
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

AFoundationLab* UFoundationInteractionView::GetLab() const
{
    return Cast<AFoundationLab>(GetOwner());
}

AFoundationPawn* UFoundationInteractionView::GetLocalPawn() const
{
    const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    return Controller ? Cast<AFoundationPawn>(Controller->GetPawn()) : nullptr;
}

UMaterialInstanceDynamic* UFoundationInteractionView::MakeMaterial(FLinearColor Color, bool bSurface)
{
    AFoundationLab* Lab = GetLab();
    // World-fixed geometry shares the lab's dev grid; moving or tiny cubes (held items, particles, the
    // mining overlay) keep the plain material, where a world-aligned grid would swim.
    UMaterialInterface* Base = bSurface && Lab && Lab->GetSurfaceMaterial()
        ? Lab->GetSurfaceMaterial()
        : LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Lab);
    Material->SetVectorParameterValue(TEXT("Color"), Color);
    Owned.Add(Material);
    return Material;
}

UStaticMeshComponent* UFoundationInteractionView::MakeCube(USceneComponent* Parent, FLinearColor Color, bool bSurface)
{
    AActor* Owner = GetOwner();
    auto* Mesh = NewObject<UStaticMeshComponent>(Owner);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetupAttachment(Parent ? Parent : Owner->GetRootComponent());
    Mesh->RegisterComponent();
    Mesh->SetMaterial(0, MakeMaterial(Color, bSurface));
    Owned.Add(Mesh);
    return Mesh;
}

void UFoundationInteractionView::BeginPlay()
{
    Super::BeginPlay();
    AActor* Owner = GetOwner();
    Lines = NewObject<ULineBatchComponent>(Owner, TEXT("InteractionLines"));
    Lines->SetupAttachment(Owner->GetRootComponent());
    Lines->RegisterComponent();
    GridLines = NewObject<ULineBatchComponent>(Owner, TEXT("InteractionGrid"));
    GridLines->SetupAttachment(Owner->GetRootComponent());
    GridLines->RegisterComponent();
    AFoundationLab* Lab = GetLab();
    if (!Lab || !Lab->GetAuthority()) return;
    const FFoundationAuthority& Authority = *Lab->GetAuthority();
    // The dev grid material draws the cell lines; only the jump-stack pad, matching its sign, is drawn once
    // and never flushed.
    const FVector Origin = Authority.World.OriginCm;
    const FVector Pad[4] = {Origin + FVector(0, -600, .3), Origin + FVector(200, -600, .3),
        Origin + FVector(200, -400, .3), Origin + FVector(0, -400, .3)};
    for (int32 Corner = 0; Corner < 4 && bLabFixtures; ++Corner)
        GridLines->DrawLine(Pad[Corner], Pad[(Corner + 1) % 4], FLinearColor(.95f, .8f, .2f), SDPG_World, 2.f, 0);

    MiningOverlay = MakeCube(nullptr, FLinearColor::White);
    MiningMaterial = Cast<UMaterialInstanceDynamic>(MiningOverlay->GetMaterial(0));
    MiningOverlay->SetVisibility(false);
    for (const FFoundationTrainingTarget& Target : Authority.Targets)
    {
        FTargetVisual Visual;
        Visual.Body = MakeCube(nullptr, TargetColor(false), true);
        Visual.Material = Cast<UMaterialInstanceDynamic>(Visual.Body->GetMaterial(0));
        Visual.Body->SetWorldLocation(Target.Box.GetCenter());
        Visual.Body->SetWorldScale3D(Target.Box.GetSize() / InteractionViewBlockCm);
        Visual.Body->SetCastShadow(true);
        Visual.Label = NewObject<UTextRenderComponent>(Owner);
        Visual.Label->SetupAttachment(Owner->GetRootComponent());
        Visual.Label->RegisterComponent();
        Visual.Label->SetWorldSize(16);
        Visual.Label->SetHorizontalAlignment(EHTA_Center);
        Visual.Label->SetWorldLocationAndRotation(
            FVector(Target.Box.GetCenter().X, Target.Box.Min.Y - 4, Target.Box.Max.Z + 30), FRotator(0, -90, 0));
        Owned.Add(Visual.Label);
        TargetVisuals.Add(Visual);
    }
    auto MakeLabel = [this, Owner](FVector Location)
    {
        auto* Label = NewObject<UTextRenderComponent>(Owner);
        Label->SetupAttachment(Owner->GetRootComponent());
        Label->RegisterComponent();
        Label->SetWorldSize(13);
        Label->SetHorizontalAlignment(EHTA_Center);
        Label->SetTextRenderColor(FColor(255, 223, 138));
        Label->SetWorldLocationAndRotation(Location, FRotator(0, 0, 0));
        Owned.Add(Label);
        return Label;
    };
    if (bLabFixtures)
    {
        PresetLabel = MakeLabel(Origin + FVector(-290, -150, 112));
        SupplyLabel = MakeLabel(Origin + FVector(-290, 250, 112));
    }
    // Effect pools up front: the first break, critical or hit of a session would otherwise register
    // components in the middle of play.
    for (int32 Index = 0; Index < MaxParticles; ++Index)
        Particles.Add_GetRef({MakeCube(nullptr, FLinearColor::White)}).Mesh->SetVisibility(false);
    for (int32 Index = 0; Index < MaxNumbers; ++Index)
    {
        auto* Label = NewObject<UTextRenderComponent>(Owner);
        Label->SetupAttachment(Owner->GetRootComponent());
        Label->RegisterComponent();
        Label->SetWorldSize(22);
        Label->SetHorizontalAlignment(EHTA_Center);
        Label->SetVisibility(false);
        Owned.Add(Label);
        Numbers.Add({Label});
    }
    LoadBlockAssets();
    // The breaking effects' pools (foundation.Break.*), hidden until a switch turns one on.
    BreakEffects = MakeShared<FFoundationBreakEffects>(*this);
    BreakEffects->Initialize(*Lab->GetAuthority());
    ItemEntityView = MakeShared<FFoundationItemEntityView>(*this);
    RebuildBlocks(*Lab->GetAuthority());
}

int32 UFoundationInteractionView::GetInstanceCount() const
{
    int32 Count = 0;
    for (const auto& Pair : Sections)
        Count += Pair.Value.Instances.Num();
    return Count;
}

void UFoundationInteractionView::LoadBlockAssets()
{
    const UFoundationBlockAssets* Assets =
        GetLab() && GetLab()->GetAuthority() ? GetLab()->GetAuthority()->World.GetRegistry().GetAssets() : nullptr;
    for (int32 Kind = 0; Kind < FoundationBlockMeshes::Count; ++Kind)
        BlockMeshes.Add(FoundationBlockMeshes::Build(this, Kind));
    TextureTable = NewObject<UFoundationBlockTextureTable>(this);
    TextureTable->Initialize(Assets);
    // One material per layer, sharing the animation table. Without imported assets every block falls back to
    // the dev grid (the Playground stays usable before ImportBlocks.py has run).
    for (const TCHAR* Name : LayerMaterialNames)
    {
        UMaterialInterface* Base =
            Assets ? Cast<UMaterialInterface>(UUnnamedProjectSettings::GetBlockAssetPath(Name).TryLoad()) : nullptr;
        if (!Base)
        {
            LayerMaterials.Add(MakeMaterial(FLinearColor::White, true));
            continue;
        }
        UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, this);
        Material->SetTextureParameterValue(TEXT("TextureTable"), TextureTable->GetTexture());
        Owned.Add(Material);
        LayerMaterials.Add(Material);
    }
}

void UFoundationInteractionView::MakeCustomData(
    const FFoundationBlockRegistry& Registry, FFoundationBlockState State, TArrayView<float> Out)
{
    check(Out.Num() == CustomDataFloats);
    for (float& Value : Out)
        Value = 0;
    const FFoundationBlockType& Type = Registry.GetBlock(State);
    if (Type.IsWater())
    {
        // Water: the still texture on top and bottom, the flow texture (its own array) on the sides; tinted.
        const UFoundationBlockAssets* Assets = Registry.GetAssets();
        const int32 Still = FMath::Max(0, FindTexture(Assets, TEXT("water_still")));
        const int32 Flow = FMath::Max(0, FindTexture(Assets, TEXT("water_flow")));
        for (int32 Face = 0; Face < 6; ++Face)
            Out[Face] =
                float(uint32(Face >= int32(EFoundationFace::Down) ? Still : Flow) | FoundationBlockFaces::TintBit);
        const FColor Water = Assets ? Assets->WaterColor : FColor(0x3F, 0x76, 0xE4);
        Out[7] = float(uint32(Water.R) << 16 | uint32(Water.G) << 8 | Water.B);
        return;
    }
    if (Type.IsLava())
    {
        // Still lava: its animated still texture on every face, untinted, drawn opaque so its light shows.
        const int32 Still = FMath::Max(0, FindTexture(Registry.GetAssets(), TEXT("lava_still")));
        for (int32 Face = 0; Face < 6; ++Face)
            Out[Face] = float(uint32(Still));
        Out[8] = float(Type.LightEmission);
        return;
    }
    if (const uint32* Codes = Registry.GetFaces(State))
        for (int32 Index = 0; Index < FoundationBlockFaces::CodesPerState; ++Index)
            Out[Index] = float(Codes[Index]);
    // 24-bit codes and colors are exact in a float. The light is the state's (sea pickles and cave vines vary; torches
    // glow at voxel.TorchGlow, their flames at their element light).
    Out[7] = float(uint32(Type.Tint.R) << 16 | uint32(Type.Tint.G) << 8 | Type.Tint.B);
    Out[8] = float(FoundationBlockMeshes::GetDrawnLight(Registry, State));
}

void UFoundationInteractionView::MakeFluidSurfaceData(const FFoundationBlockRegistry& Registry,
    FFoundationBlockState State, const VoxelFluidSurface::FSurface& Surface, int32 Mode, TArrayView<float> Out)
{
    MakeCustomData(Registry, State, Out);
    const FFoundationBlockType& Type = Registry.GetBlock(State);
    const int32 Up = int32(EFoundationFace::Up);
    const bool bFlowTop = FoundationBlockMeshes::IsFlowTop(Surface);
    if (Type.IsWater())
    {
        // Water's sides already take the flow texture, tinted.
        if (bFlowTop) Out[Up] = Out[int32(EFoundationFace::West)];
        return;
    }
    const int32 Flow = Type.IsLava() ? FindTexture(Registry.GetAssets(), TEXT("lava_flow")) : INDEX_NONE;
    if (Flow == INDEX_NONE) return;
    if (bFlowTop) Out[Up] = float(uint32(Flow));
    if (Mode >= 2 || Registry.GetFluid(State).Level != 0)
        for (int32 Face = 0; Face < int32(EFoundationFace::Down); ++Face)
            Out[Face] = float(uint32(Flow));
}

UStaticMesh* UFoundationInteractionView::GetBlockMesh(const FFoundationBlockRegistry& Registry, int32 Mesh)
{
    if (Mesh >= BlockMeshes.Num()) BlockMeshes.SetNum(Mesh + 1);
    if (!BlockMeshes[Mesh] && Mesh >= FoundationBlockMeshes::Count &&
        Mesh < FoundationBlockMeshes::Count + Registry.GetModelCount())
        BlockMeshes[Mesh] = FoundationBlockMeshes::BuildModel(
            this, FoundationBlockMeshes::GetDrawnModelQuads(Registry, Mesh - FoundationBlockMeshes::Count));
    return BlockMeshes[Mesh];
}

uint16 UFoundationInteractionView::GetFluidSurfaceMesh(
    const FFoundationBlockRegistry& Registry, const VoxelFluidSurface::FSurface& Surface)
{
    const uint32 Key = FoundationBlockMeshes::GetFluidSurfaceKey(Surface);
    if (const uint16* Found = FluidSurfaceMeshes.Find(Key)) return *Found;
    const int32 Mesh = FMath::Max(BlockMeshes.Num(), FoundationBlockMeshes::Count + Registry.GetModelCount());
    check(Mesh <= MAX_uint16);
    BlockMeshes.SetNum(Mesh + 1);
    BlockMeshes[Mesh] = FoundationBlockMeshes::BuildFluidSurface(this, Surface);
    return FluidSurfaceMeshes.Add(Key, uint16(Mesh));
}

UInstancedStaticMeshComponent* UFoundationInteractionView::MakeBlockComponent(
    USceneComponent* Parent, UStaticMesh* Mesh, int32 Layer)
{
    AActor* Owner = GetOwner();
    auto* Component = NewObject<UInstancedStaticMeshComponent>(Owner);
    Component->SetStaticMesh(Mesh);
    Component->SetMobility(EComponentMobility::Movable);
    // Movement and targeting use the logical grid; the engine needs no collision for these.
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetNumCustomDataFloats(CustomDataFloats);
    Component->SetupAttachment(Parent ? Parent : Owner->GetRootComponent());
    Component->RegisterComponent();
    Component->SetMaterial(0, LayerMaterials[Layer]);
    if (Layer >= int32(EFoundationRenderLayer::Translucent)) Component->SetCastShadow(Layer != WaterLayer);
    return Component;
}

void UFoundationInteractionView::AddInstance(
    const FFoundationBlockWorld& World, const FIntVector& Cell, FFoundationBlockState State)
{
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    const FIntVector Section = FoundationGrid::SectionOf(Cell);
    // The mesh pivot is the cell's minimum corner.
    const FVector CellMin = FoundationGrid::CellMin(Cell, World.OriginCm);
    float Data[CustomDataFloats];
    DrawnKeys.Add(Cell);
    if (Registry.GetShape(State) == EFoundationBlockShape::Model)
    {
        // One instance per part, each part's alternative picked and the whole state moved as the voxel world's mesher
        // does (VoxelModels), so the two agree cell for cell. The layer is the block's own (moss carpet is opaque).
        const FFoundationBlockType& Type = Registry.GetBlock(State);
        MakeCustomData(Registry, State, Data);
        const FTransform Moved(CellMin + GetModelOffsetCm(Type, Cell));
        TArray<TConstArrayView<int32>> Parts;
        Registry.GetModelParts(State, Parts);
        for (int32 Part = 0; Part < Parts.Num(); ++Part)
        {
            const TConstArrayView<int32> Alternatives = Parts[Part];
            if (Alternatives.IsEmpty()) continue;
            const int32 Model =
                Alternatives[VoxelModels::PickAlternative(Cell.X, Cell.Y, Cell.Z, Part, Alternatives.Num())];
            AddPart({Section, uint16(FoundationBlockMeshes::Count + Model), uint8(Type.Layer)}, Cell, Moved, Data,
                Registry);
        }
        // Held water is drawn as a water source around the model, as the voxel world draws it.
        const FFoundationBlockType* Water = IsHeldWater(Registry, State) ? Registry.FindBlock(TEXT("water")) : nullptr;
        if (!Water) return;
        State = Water->GetDefaultState();
    }
    const FFoundationBlockType& Type = Registry.GetBlock(State);
    const FFoundationBlockState Above = World.Get(Cell + FIntVector(0, 0, 1)).State;
    const bool bFluidAbove =
        Type.IsWater() ? HoldsWater(Registry, Above) : Type.IsFluid() && Registry.GetBlock(Above).Id == Type.Id;
    // A sloping surface draws the voxel mesher's geometry, on a mesh shared by every surface of the same corners.
    VoxelFluidSurface::FSurface Surface;
    const int32 Mode = FVoxelFluidSystem::GetSlopedTops();
    if (Type.IsFluid() &&
        FoundationBlockMeshes::MakeFluidSurface(
            Registry, [&World](const FIntVector& At) { return World.Get(At).State; }, Cell, Mode,
            FVoxelFluidSystem::GetOpenCorners(), Surface))
    {
        MakeFluidSurfaceData(Registry, State, Surface, Mode, Data);
        AddPart({Section, GetFluidSurfaceMesh(Registry, Surface), Type.IsWater() ? WaterLayer : uint8(Type.Layer)},
            Cell, FTransform(CellMin), Data, Registry);
        return;
    }
    MakeCustomData(Registry, State, Data);
    AddPart({Section, uint16(FoundationBlockMeshes::GetKind(Registry, State, bFluidAbove)),
                Type.IsWater() ? WaterLayer : uint8(Type.Layer)},
        Cell, FTransform(CellMin), Data, Registry);
}

void UFoundationInteractionView::AddPart(const FSectionKey& Key, const FIntVector& Cell, const FTransform& Instance,
    TArrayView<const float> Data, const FFoundationBlockRegistry& Registry)
{
    // A model with no quad left draws nothing (Unnamed.Foundation.Blocks.ModelMeshes finds none).
    UStaticMesh* Mesh = GetBlockMesh(Registry, Key.Mesh);
    if (!Mesh) return;
    FSectionMesh& Section = Sections.FindOrAdd(Key);
    if (!Section.Mesh)
    {
        Section.Mesh = MakeBlockComponent(nullptr, Mesh, Key.Layer);
        SectionComponents.Add(Section.Mesh);
    }
    FTransform Local = Instance;
    Local.AddToTranslation(-Section.Mesh->GetComponentLocation());
    const FPrimitiveInstanceId Id = Section.Mesh->AddInstanceById(Local, false);
    Section.Mesh->SetCustomDataById(Id, Data);
    Section.Instances.Add(Cell, Id);
    DrawnKeys.FindChecked(Cell).AddUnique(Key);
}

void UFoundationInteractionView::RemoveInstance(const FIntVector& Cell)
{
    TArray<FSectionKey, TInlineAllocator<2>> Keys;
    if (!DrawnKeys.RemoveAndCopyValue(Cell, Keys)) return;
    TArray<FPrimitiveInstanceId, TInlineAllocator<4>> Ids;
    for (const FSectionKey& Key : Keys)
    {
        FSectionMesh* Section = Sections.Find(Key);
        if (!Section) continue;
        Ids.Reset();
        Section->Instances.MultiFind(Cell, Ids);
        Section->Instances.Remove(Cell);
        for (const FPrimitiveInstanceId Id : Ids)
            Section->Mesh->RemoveInstanceById(Id);
    }
}

void UFoundationInteractionView::RefreshFluidsAround(const FFoundationBlockWorld& World, const FIntVector& Cell)
{
    for (int32 DZ = -1; DZ <= 0; ++DZ)
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
            {
                const FIntVector At = Cell + FIntVector(DX, DY, DZ);
                if (At == Cell) continue;
                const FFoundationBlockState State = World.Get(At).State;
                if (!FoundationBlockMeshes::HoldsFluid(World.GetRegistry(), State) || !DrawnKeys.Contains(At)) continue;
                RemoveInstance(At);
                AddInstance(World, At, State);
            }
}

void UFoundationInteractionView::RebuildBlocks(FFoundationAuthority& Authority)
{
    if (!bDrawBlocks)
    {
        Authority.World.TakeChanges();
        Authority.World.bChangesOverflowed = false;
        DrawnRevision = Authority.World.Revision;
        return;
    }
    for (auto& Pair : Sections)
    {
        Pair.Value.Mesh->ClearInstances();
        Pair.Value.Instances.Reset();
    }
    DrawnKeys.Reset();
    FFoundationBlockWorld& World = Authority.World;
    World.TakeChanges();
    World.bChangesOverflowed = false;
    World.GetStorage().ForEachSolid(World.BoundsMin, World.BoundsMax,
        [this, &World](const FIntVector& Cell, const FFoundationCell& Value)
        { AddInstance(World, Cell, Value.State); });
    DrawnRevision = World.Revision;
}

void UFoundationInteractionView::ApplyChanges(FFoundationAuthority& Authority)
{
    FFoundationBlockWorld& World = Authority.World;
    if (World.bChangesOverflowed || !bDrawBlocks)
    {
        RebuildBlocks(Authority);
        return;
    }
    // In order, so each change's "before" is what is drawn now.
    for (const FFoundationBlockChange& Change : World.TakeChanges())
    {
        // Whatever is drawn there: a neighbour's refresh may have drawn the cell's later state already.
        RemoveInstance(Change.Cell);
        if (!Change.After.IsAir()) AddInstance(World, Change.Cell, Change.After);
        RefreshFluidsAround(World, Change.Cell);
    }
    DrawnRevision = World.Revision;
}

void UFoundationInteractionView::Burst(
    const FVector& Center, FLinearColor Color, int32 Count, double Size, double Speed, uint32 Seed)
{
    FRandomStream Random(static_cast<int32>(Seed));
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FParticle* Free =
            Particles.FindByPredicate([](const FParticle& Particle) { return Particle.Age >= Particle.Life; });
        if (!Free) return;
        Free->Mesh->SetVisibility(true);
        if (auto* Material = Cast<UMaterialInstanceDynamic>(Free->Mesh->GetMaterial(0)))
            Material->SetVectorParameterValue(TEXT("Color"), Color);
        Free->Velocity = (Random.VRand() + FVector(0, 0, .8)).GetSafeNormal() * Speed * Random.FRandRange(.6f, 1.f);
        Free->Age = 0;
        Free->Life = Random.FRandRange(.45f, .75f);
        Free->Size = Size;
        Free->Mesh->SetWorldLocation(Center + Random.VRand() * 20);
        Free->Mesh->SetWorldScale3D(FVector(Size / InteractionViewBlockCm));
    }
}

void UFoundationInteractionView::ShowNumber(const FVector& Location, const FString& Text, FColor Color)
{
    FFloatingText* Free =
        Numbers.FindByPredicate([](const FFloatingText& Number) { return Number.Age >= Number.Life; });
    if (!Free) return;
    Free->Text->SetText(FText::FromString(Text));
    Free->Text->SetTextRenderColor(Color);
    Free->Text->SetVisibility(true);
    Free->Start = Location;
    Free->Age = 0;
    Free->Life = .9;
}

int32 FoundationPlace::GetPreviewMode()
{
    return CVarPlacePreview.GetValueOnGameThread();
}

bool FoundationPlace::ShouldDrawPreview(int32 Mode, const FFoundationPreview& Preview)
{
    return Mode == 1 && Preview.bPlacement;
}

bool FoundationHand::IsFitOn()
{
    return CVarHandFit.GetValueOnGameThread() != 0;
}

double FoundationHeldLight::Brightness(double Level)
{
    const auto Curve = [](double F) { return F / (4 - 3 * F); };
    return Floor + (1 - Floor) * Curve(FMath::Clamp(Level, 0., 15.) / 15) / Curve(1);
}

double FoundationHeldLight::LightGlow(double Level, double Glow, double DayEV)
{
    return FMath::Pow(Brightness(Level), 2.2) * Glow * FMath::Pow(2., -(13.5 + DayEV));
}

int32 FoundationHeldLight::ReadMode()
{
    return CVarHeldLight.GetValueOnGameThread();
}

FTransform FoundationHand::MakeHeldBlockDisplay()
{
    FFoundationItemDisplayAsset Display;
    Display.Rotation = FVector(0, 45, 0);
    Display.Scale = FVector(.4);
    FTransform Transform =
        FoundationItemMesh::MakeDisplayTransform(Display, UFoundationInteractionView::HeldModelBlockCm);
    // The display scales model blocks; the cube mesh spans a world block.
    Transform.SetScale3D(
        Transform.GetScale3D() * (UFoundationInteractionView::HeldModelBlockCm / InteractionViewBlockCm));
    return Transform;
}

void UFoundationInteractionView::AddFlash(const FBox& Box, FLinearColor Color, double Life, bool bCross)
{
    Flashes.Add({Box, Color, 0, Life, bCross});
    ++StartedFlashes;
}

void UFoundationInteractionView::HandleEvents(const FFoundationAuthority& Authority)
{
    // Placed and rejected flashes belong to the destination preview (foundation.Place.Preview 1); vanilla has neither.
    const bool bPreviewFlashes = FoundationPlace::GetPreviewMode() == 1;
    const FVector Origin = Authority.World.OriginCm;
    for (const FFoundationEvent& Event : Authority.GetEvents())
    {
        if (Event.Serial <= LastEvent) continue;
        LastEvent = Event.Serial;
        const FBox Cell = FoundationGrid::CellBox(Event.Cell, Origin);
        switch (Event.Kind)
        {
        case EFoundationEventKind::Placed:
            if (bPreviewFlashes) AddFlash(Cell.ExpandBy(1), FLinearColor(1, 1, 1), .15);
            if (BreakEffects) BreakEffects->OnBlockPlaced(Authority, Event);
            break;
        case EFoundationEventKind::Broken:
        case EFoundationEventKind::Popped:
            if (!BreakEffects || !BreakEffects->OnBlockRemoved(Authority, Event))
                Burst(Cell.GetCenter(), Authority.World.GetBlock(Event.State).Color, 10, 12, 260, uint32(Event.Serial));
            break;
        case EFoundationEventKind::Rejected:
            if (bPreviewFlashes) AddFlash(Cell.ExpandBy(.5), FLinearColor(.95f, .15f, .1f), .3, true);
            break;
        case EFoundationEventKind::Interacted:
            AddFlash(Cell.ExpandBy(2), FLinearColor(1, .85f, .2f), .3);
            break;
        case EFoundationEventKind::Hit:
            if (const FFoundationTrainingTarget* Target = Authority.Targets.FindByPredicate(
                    [&Event](const FFoundationTrainingTarget& Candidate) { return Candidate.Id == Event.EntityId; }))
            {
                const int32 Index = int32(Target - Authority.Targets.GetData());
                if (TargetVisuals.IsValidIndex(Index)) TargetVisuals[Index].HitAge = 0;
                const FVector Top(Target->Box.GetCenter().X, Target->Box.Min.Y - 10, Target->Box.Max.Z + 60);
                ShowNumber(Top,
                    FString::Printf(TEXT("-%.1f%s"), Event.Amount, Event.bCritical ? TEXT(" CRIT") : TEXT("")),
                    Event.bCritical ? FColor(255, 220, 40) : FColor(255, 90, 70));
                if (Event.bCritical)
                    Burst(Top - FVector(0, 0, 40), FLinearColor(1, .85f, .2f), 8, 5, 320, uint32(Event.Serial));
            }
            break;
        case EFoundationEventKind::PickedUp:
            if (ItemEntityView) ItemEntityView->OnPickedUp(Event, GetLocalPawn());
            break;
        case EFoundationEventKind::Eating:
        case EFoundationEventKind::Ate:
            // Eating (step 10): entity.generic.eat at volume 0.5-1.0 and pitch 0.8-1.2, every effect step and once more
            // at the end, then entity.player.burp at 0.5 and 0.9-1.0 (the burp at the end: wiki "Food"; the volumes and
            // pitches are provisional, unverified, to check against the wiki's Sounds.json table). No crumbs yet: the
            // break particles draw block textures only (known issue).
            if (BreakEffects)
            {
                BreakEffects->PlayEvent(TEXT("entity.generic.eat"), Event.LocationCm, .5f + .5f * FMath::FRand(),
                    (FMath::FRand() - FMath::FRand()) * .2f + 1.f);
                if (Event.Kind == EFoundationEventKind::Ate)
                    BreakEffects->PlayEvent(
                        TEXT("entity.player.burp"), Event.LocationCm, .5f, FMath::FRand() * .1f + .9f);
            }
            break;
        case EFoundationEventKind::Stripped:
            // Axe stripping (step 14): item.axe.strip from the block, volume 1 and pitch 1 (provisional until a vanilla
            // capture).
            if (BreakEffects) BreakEffects->PlayEvent(TEXT("item.axe.strip"), Cell.GetCenter(), 1.f, 1.f);
            break;
        case EFoundationEventKind::ToolBroke:
            if (const AFoundationPawn* Pawn = GetLocalPawn())
                Burst(Pawn->GetEyePosition() + Pawn->GetControlRotation().Vector() * 60, FLinearColor(.7f, .7f, .72f),
                    8, 4, 150, uint32(Event.Serial));
            break;
        default:
            break;
        }
    }
}

void UFoundationInteractionView::UpdateTargets(const FFoundationAuthority& Authority, double DeltaTime)
{
    for (int32 Index = 0; Index < FMath::Min(TargetVisuals.Num(), Authority.Targets.Num()); ++Index)
    {
        FTargetVisual& Visual = TargetVisuals[Index];
        const FFoundationTrainingTarget& Target = Authority.Targets[Index];
        const bool bDead = Target.Health.IsDead();
        Visual.HitAge += DeltaTime;
        const float Flash = FMath::Clamp(1.f - float(Visual.HitAge) / .25f, 0.f, 1.f);
        Visual.Material->SetVectorParameterValue(
            TEXT("Color"), FMath::Lerp(TargetColor(bDead), FLinearColor(1, .05f, .05f), Flash));
        // A downed target leans over (presentation only; its box stays put).
        Visual.Body->SetWorldRotation(FRotator(bDead ? -70 : 0, 0, 0));
        const FString Text = FString::Printf(TEXT("%s\nHP %.1f / %.0f%s"), *Target.Label, Target.Health.Health,
            Target.Health.MaxHealth, bDead ? TEXT("\nDOWN - reset the lab") : TEXT(""));
        if (Text != Visual.LastText)
        {
            Visual.LastText = Text;
            Visual.Label->SetText(FText::FromString(Text));
            Visual.Label->SetTextRenderColor(bDead ? FColor(170, 170, 170) : FColor(255, 255, 255));
        }
    }
}

void UFoundationInteractionView::UpdateLabels(const FFoundationAuthority& Authority)
{
    if (!PresetLabel || !SupplyLabel) return;
    PresetLabel->SetText(FText::FromString(Authority.Options.PlacementPreset.ToString()));
    SupplyLabel->SetText(FText::FromString(Authority.Options.bInfiniteSupply
            ? TEXT("INFINITE, no wear")
            : FString::Printf(TEXT("FINITE %d + wear"), Authority.Options.FiniteBlockCount)));
}

void UFoundationInteractionView::UpdateEffects(double DeltaTime)
{
    for (FParticle& Particle : Particles)
    {
        if (Particle.Age >= Particle.Life) continue;
        Particle.Age += DeltaTime;
        if (Particle.Age >= Particle.Life)
        {
            Particle.Mesh->SetVisibility(false);
            continue;
        }
        Particle.Velocity.Z -= 980 * DeltaTime;
        Particle.Mesh->AddWorldOffset(Particle.Velocity * DeltaTime);
        Particle.Mesh->SetWorldScale3D(
            FVector(Particle.Size / InteractionViewBlockCm * (1. - Particle.Age / Particle.Life)));
    }
    for (FFloatingText& Number : Numbers)
    {
        if (Number.Age >= Number.Life) continue;
        Number.Age += DeltaTime;
        Number.Text->SetVisibility(Number.Age < Number.Life);
        Number.Text->SetWorldLocationAndRotation(
            Number.Start + FVector(0, 0, 80 * Number.Age / Number.Life), FRotator(0, -90, 0));
    }
    for (FFlash& Flash : Flashes)
        Flash.Age += DeltaTime;
    Flashes.RemoveAll([](const FFlash& Flash) { return Flash.Age >= Flash.Life; });
}

void UFoundationInteractionView::DrawBox(const FBox& Box, FLinearColor Color, float Thickness)
{
    const FVector& A = Box.Min;
    const FVector& B = Box.Max;
    const FVector Corners[8] = {{A.X, A.Y, A.Z}, {B.X, A.Y, A.Z}, {B.X, B.Y, A.Z}, {A.X, B.Y, A.Z}, {A.X, A.Y, B.Z},
        {B.X, A.Y, B.Z}, {B.X, B.Y, B.Z}, {A.X, B.Y, B.Z}};
    const int32 Edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& Edge : Edges)
        Lines->DrawLine(Corners[Edge[0]], Corners[Edge[1]], Color, SDPG_World, Thickness, 0);
}

void UFoundationInteractionView::DrawSelection(
    const FFoundationAuthority& Authority, const AFoundationPawn* Pawn, bool bVisible)
{
    Lines->Flush();
    bOutlineVisible = bPreviewVisible = false;
    for (const FFlash& Flash : Flashes)
    {
        const FLinearColor Color = Flash.Color * (1.f - float(Flash.Age / Flash.Life)) + FLinearColor(0, 0, 0, 1);
        DrawBox(Flash.Box.ExpandBy(4 * Flash.Age / Flash.Life), Color, 1.5f);
        if (Flash.bCross)
        {
            Lines->DrawLine(Flash.Box.Min, Flash.Box.Max, Color, SDPG_World, 1.5f, 0);
            Lines->DrawLine(FVector(Flash.Box.Min.X, Flash.Box.Max.Y, Flash.Box.Min.Z),
                FVector(Flash.Box.Max.X, Flash.Box.Min.Y, Flash.Box.Max.Z), Color, SDPG_World, 1.5f, 0);
        }
    }
    if (!Pawn || !bVisible) return;
    const FFoundationPreview& Preview = Pawn->Interaction.Preview;
    const FFoundationHit& Hit = Preview.Target;
    if (!Hit.bInRange) return;
    // The target outline: a block's outline boxes, an anchor's clicked cell, other geometry or an entity.
    FBox Outline(ForceInit);
    TArray<FBox> Outlines;
    FLinearColor OutlineColor(.02f, .02f, .02f);
    if (Hit.Kind == EFoundationHitKind::Block)
        Authority.World.GetOutlineBoxes(Hit.Cell, Authority.World.Get(Hit.Cell).State, Outlines);
    else if (Hit.Kind == EFoundationHitKind::Static && Authority.Statics.IsValidIndex(Hit.StaticIndex))
    {
        const FFoundationStatic& Static = Authority.Statics[Hit.StaticIndex];
        Outline = Static.bAnchor && Static.Interaction.IsNone()
            ? FoundationGrid::CellBox(Hit.Cell, Authority.World.OriginCm)
            : Static.Box;
        if (Preview.bInteraction) OutlineColor = FLinearColor(1, .85f, .2f);
    }
    else if (Hit.Kind == EFoundationHitKind::Entity)
        if (const FFoundationTrainingTarget* Target = Authority.Targets.FindByPredicate(
                [&Hit](const FFoundationTrainingTarget& Candidate) { return Candidate.Id == Hit.EntityId; }))
        {
            Outline = Target->Box;
            OutlineColor = Target->Health.IsDead() ? FLinearColor(.4f, .4f, .4f) : FLinearColor(.9f, .1f, .05f);
        }
    if (Outline.IsValid) Outlines.Add(Outline);
    // A block's outline can come from the breaking effects instead (foundation.Break.Outline).
    if (!(Hit.Kind == EFoundationHitKind::Block && BreakEffects && BreakEffects->SubmitOutline(Outlines)))
        for (const FBox& Box : Outlines)
            DrawBox(Box.ExpandBy(.3), OutlineColor, .9f);
    bOutlineVisible = !Outlines.IsEmpty();
    // The destination preview only with foundation.Place.Preview 1: vanilla shows the target's outline alone. The
    // preview's data (Interaction.Preview) is kept either way: placement and the F3 line read it.
    if (!FoundationPlace::ShouldDrawPreview(FoundationPlace::GetPreviewMode(), Preview)) return;
    // Destination preview: white when it would place, orange while only the player's body is in the
    // way, red otherwise. Never solid: the block appears only from the committed change. A valid placement
    // outlines the state it would place (a slab's half).
    FBox Cell = FoundationGrid::CellBox(Preview.Cell, Authority.World.OriginCm);
    if (Preview.bValid) Authority.World.GetSelectionBox(Preview.Cell, Preview.State, Cell);
    Cell = Cell.ExpandBy(-.5);
    const FLinearColor Color = Preview.bValid                   ? FLinearColor(.95f, .97f, 1.f)
        : Preview.Reason == EFoundationReason::ObstructedBySelf ? FLinearColor(1, .55f, .1f)
                                                                : FLinearColor(.95f, .12f, .08f);
    bPreviewVisible = true;
    if (Preview.Reason == EFoundationReason::ObstructedBySelf || Cell.IsInside(Pawn->GetEyePosition()))
    {
        // The player stands in it: only the face it would grow from, not a cage around the camera.
        const FVector Normal = FoundationGrid::FaceNormal(Hit.Face);
        const FVector Center = Cell.GetCenter() - Normal * 49.5;
        const FVector U = FMath::Abs(Normal.Z) > .5 ? FVector(49.5, 0, 0) : FVector(0, 0, 49.5);
        const FVector V = FVector::CrossProduct(Normal, U.GetSafeNormal()) * 49.5;
        const FVector Corners[4] = {Center - U - V, Center + U - V, Center + U + V, Center - U + V};
        for (int32 Corner = 0; Corner < 4; ++Corner)
            Lines->DrawLine(Corners[Corner], Corners[(Corner + 1) % 4], Color, SDPG_World, 1.5f, 0);
        return;
    }
    DrawBox(Cell, Color, 1.2f);
    Lines->DrawSolidBox(
        Cell, FTransform::Identity, Color.ToFColor(true).WithAlpha(Preview.bValid ? 40 : 28), SDPG_World, 0);
    if (!Preview.bValid)
    {
        Lines->DrawLine(Cell.Min, Cell.Max, Color, SDPG_World, 1.2f, 0);
        Lines->DrawLine(FVector(Cell.Min.X, Cell.Max.Y, Cell.Min.Z), FVector(Cell.Max.X, Cell.Min.Y, Cell.Max.Z), Color,
            SDPG_World, 1.2f, 0);
    }
}

void UFoundationInteractionView::DrawCracks(
    const FFoundationAuthority& Authority, const FFoundationInteraction& Interaction)
{
    // Cracks spread over every face from its centre, one more branch per stage of ten (an original
    // pattern, fixed per cell so it grows rather than flickers).
    const int32 Stage = FMath::Clamp(FMath::FloorToInt32(Interaction.MiningProgress * 10), 0, 9);
    const FBox Cell = FoundationGrid::CellBox(Interaction.MiningCell, Authority.World.OriginCm).ExpandBy(.6);
    FRandomStream Random(static_cast<int32>(GetTypeHash(Interaction.MiningCell)));
    const FLinearColor Ink(.08f, .08f, .09f);
    for (int32 Face = 0; Face < 6; ++Face)
    {
        const FVector Normal = FoundationGrid::FaceNormal(EFoundationFace(Face));
        const FVector Center = Cell.GetCenter() + Normal * Cell.GetExtent();
        const FVector U = (FMath::Abs(Normal.Z) > .5 ? FVector(1, 0, 0) : FVector(0, 0, 1));
        const FVector V = FVector::CrossProduct(Normal, U);
        for (int32 Branch = 0; Branch <= Stage; ++Branch)
        {
            FVector Point = Center;
            double Angle = Random.FRandRange(0.f, 2 * PI);
            for (int32 Segment = 0; Segment < 3; ++Segment)
            {
                Angle += Random.FRandRange(-.6f, .6f);
                const double Length = Random.FRandRange(8.f, 16.f);
                FVector Next = Point + (U * FMath::Cos(Angle) + V * FMath::Sin(Angle)) * Length;
                Next = Center + (Next - Center).GetClampedToMaxSize(48);
                Lines->DrawLine(Point, Next, Ink, SDPG_World, .7f, 0);
                Point = Next;
            }
        }
    }
}

bool UFoundationInteractionView::IsHeldItemVisible() const
{
    return HandPivot && HandPivot->IsVisible();
}

void UFoundationInteractionView::UpdateHeldItem(
    const FFoundationAuthority& Authority, AFoundationPawn* Pawn, bool bVisible, double DeltaTime)
{
    if (!Pawn) return;
    if (HeldOwner.Get() != Pawn)
    {
        // The held item on the player's camera; first-person rendering keeps it out of walls. The pivot's rest
        // (42, 24, -24) is provisional until probe P1 measures vanilla's hand.
        HeldOwner = Pawn;
        UCameraComponent* Camera = Pawn->GetCamera();
        Camera->SetEnableFirstPersonScale(true);
        Camera->SetFirstPersonScale(.5f);
        HandPivot = NewObject<USceneComponent>(GetOwner());
        HandPivot->SetupAttachment(Camera);
        HandPivot->RegisterComponent();
        HandPivot->SetRelativeLocation(FVector(42, 24, -24));
        Owned.Add(HandPivot);
        // One component per part of the model default state with the most parts (a cube needs one), drawn anew.
        HeldBlocks.Reset();
        HeldState = {MAX_uint16};
        AppliedHandFit = -1;
        int32 MostParts = BlockMeshes.IsEmpty() ? 0 : 1;
        const FFoundationBlockRegistry& Registry = Authority.World.GetRegistry();
        TArray<TConstArrayView<int32>> Parts;
        for (int32 Block = 1; MostParts && Block < Registry.GetBlockCount(); ++Block)
            if (Registry.GetModelParts(Registry.GetBlockAt(Block).GetDefaultState(), Parts))
                MostParts = FMath::Max(MostParts, Parts.Num());
        for (int32 Index = 0; Index < MostParts; ++Index)
        {
            UInstancedStaticMeshComponent* Held =
                MakeBlockComponent(HandPivot, BlockMeshes[FoundationBlockMeshes::Cube], 0);
            Held->SetCastShadow(false);
            Held->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
            Owned.Add(Held);
            HeldBlocks.Add(Held);
        }
        HeldItem = NewObject<UStaticMeshComponent>(GetOwner());
        HeldItem->SetMobility(EComponentMobility::Movable);
        HeldItem->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        HeldItem->SetupAttachment(HandPivot);
        HeldItem->RegisterComponent();
        HeldItem->SetCastShadow(false);
        HeldItem->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
        Owned.Add(HeldItem);
        HeldItemId = HeldSprite = NAME_None;
    }
    const FFoundationPlayer* Player = Authority.FindPlayer(Pawn->Interaction.PlayerId);
    const FFoundationItemInstance* Main = Player ? &Player->Inventory.GetHand(EFoundationHand::Main) : nullptr;
    const FFoundationItemDefinition* InHand = Main && !Main->IsEmpty() ? &Authority.Rules.GetItem(Main->Item) : nullptr;
    const FName Icon = InHand ? InHand->Icon : NAME_None;
    const bool bShow = bVisible && Pawn->GetPerspective() == 0 && !Pawn->Vitals.IsDead();
    if (!HeldBlocks.IsEmpty())
    {
        // The held block's default state, drawn with its own meshes and layer around the pivot: its mesh kind, or one
        // instance per part of its model (each part's first alternative), centred on what it draws.
        const FFoundationBlockType* Type =
            Icon == TEXT("block") ? Authority.World.FindBlock(InHand->PlacesBlock) : nullptr;
        const FFoundationBlockState State = Type ? Type->GetDefaultState() : FFoundationBlockState{};
        if (State != HeldState)
        {
            HeldState = State;
            const FFoundationBlockRegistry& Registry = Authority.World.GetRegistry();
            TArray<UStaticMesh*, TInlineAllocator<4>> Meshes;
            FBox Drawn(ForceInit);
            GetStateMeshes(Registry, State, Meshes, Drawn);
            UMaterialInterface* Material = GetLayerMaterial(Type);
            float Data[CustomDataFloats];
            MakeCustomData(Registry, State, Data);
            for (int32 Index = 0; Index < HeldBlocks.Num(); ++Index)
            {
                UInstancedStaticMeshComponent* Held = HeldBlocks[Index];
                Held->ClearInstances();
                if (!Meshes.IsValidIndex(Index)) continue;
                Held->SetStaticMesh(Meshes[Index]);
                Held->SetMaterial(0, Material);
                Held->SetCustomDataById(Held->AddInstanceById(FTransform(-Drawn.GetCenter()), false), Data);
            }
        }
        for (UInstancedStaticMeshComponent* Held : HeldBlocks)
            Held->SetVisibility(bShow && Type != nullptr && Held->GetInstanceCount() > 0);
    }
    // Any other item: its first-person model from DA_Items.
    const FName ItemId = InHand && Icon != TEXT("block") ? InHand->Id : NAME_None;
    if (ItemId != HeldItemId) SetHeldItem(ItemId);
    // The glow: fixed (foundation.Held.Glow), or by the light at the eye (foundation.Held.Light 1, generated worlds).
    float Glow = CVarHeldGlow.GetValueOnGameThread(), LightGlow = 0;
    const AVoxelWorld* VoxelWorld = FoundationHeldLight::ReadMode() == 1 ? AVoxelWorld::Find(GetWorld()) : nullptr;
    const TSharedPtr<const FVoxelBlockTable> Table = VoxelWorld ? VoxelWorld->GetBlockTable() : nullptr;
    if (Table && VoxelWorld->GetLights())
    {
        // Entering the mode (or a new world) probes at once and shows the level outright, without easing from a stale
        // one.
        const bool bEntered = !bHeldLightLive;
        bHeldLightLive = true;
        const double Now = GetWorld()->GetTimeSeconds();
        if (bEntered || Now >= HeldProbeDue)
        {
            HeldProbeDue = Now + FoundationHeldLight::ProbeSeconds;
            const FVector Eye =
                (Pawn->GetCamera()->GetComponentLocation() - VoxelWorld->OriginCm) / UFoundationProfile::BlockCm;
            const FIntVector Cell(FMath::FloorToInt32(Eye.X), FMath::FloorToInt32(Eye.Y), FMath::FloorToInt32(Eye.Z));
            // Sky light as the exposure reads the open sky (VoxelLook::SkyPinFor), block light from the torch pool. The
            // sky counts as 15 at any hour: the runtime has no sky_light_level yet (night: the day-night work).
            const double Sky = 15 *
                VoxelLook::SkyPinFor(VoxelLook::SkyOpenness(Eye, [VoxelWorld, &Table](const FIntVector& At)
                    { return VoxelLook::BlocksSky(Table->Get(VoxelWorld->GetState(At))); }));
            HeldLevelTarget = FMath::Max(Sky, double(VoxelWorld->GetLights()->GetBlockLevel(Cell)));
            if (bEntered) HeldLevel = HeldLevelTarget;
        }
        HeldLevel =
            FMath::FInterpConstantTo(HeldLevel, HeldLevelTarget, DeltaTime, FoundationHeldLight::LevelsPerSecond);
        LightGlow = float(FoundationHeldLight::LightGlow(HeldLevel, Glow, FVoxelLook::Read().DayEV));
        Glow = 0;
    }
    else
        bHeldLightLive = false;
    if (!ItemMaterials.IsEmpty() && (Glow != AppliedGlow || LightGlow != AppliedLightGlow))
    {
        AppliedGlow = Glow;
        AppliedLightGlow = LightGlow;
        for (UMaterialInstanceDynamic* Material : ItemMaterials)
            if (Material)
            {
                Material->SetScalarParameterValue(TEXT("Glow"), Glow);
                Material->SetScalarParameterValue(TEXT("LightGlow"), LightGlow);
            }
    }
    HeldItem->SetVisibility(bShow && HeldItem->GetStaticMesh() != nullptr);
    HandPivot->SetVisibility(bShow, false);
    // Swing: a cosmetic prediction on each press, continuous while mining. It decides nothing.
    if (Pawn->Interaction.PressSerial != LastPressSerial)
    {
        LastPressSerial = Pawn->Interaction.PressSerial;
        SwingAge = 0;
    }
    SwingAge += DeltaTime;
    MiningSwing = Pawn->Interaction.bMining ? MiningSwing + DeltaTime : 0;
    double Swing = SwingAge < .25   ? FMath::Sin(SwingAge / .25 * PI)
        : Pawn->Interaction.bMining ? FMath::Abs(FMath::Sin(MiningSwing / .3 * PI))
                                    : 0.;
    // foundation.Break.Swing: the swing clock's 6-tick swings instead, in today's pose.
    if (BreakEffects) BreakEffects->GetSwing(Swing);
    // Eating (step 10): the main-hand item eases toward the mouth in 0.1 s and bobs once per effect step (4 ticks).
    // The project's own motion, provisional until a vanilla capture; it replaces the swing while it shows.
    const bool bEating = Pawn->Interaction.bUsingItem && Pawn->Interaction.UseHand == EFoundationHand::Main;
    EatBlend = FMath::Clamp(EatBlend + (bEating ? DeltaTime : -DeltaTime) / .1, 0., 1.);
    EatAge = bEating ? EatAge + DeltaTime : 0;
    const double Bob = FMath::Abs(FMath::Sin(EatAge / .2 * PI));
    Swing *= 1 - EatBlend;
    // foundation.Hand.Fit: the rest pose, the pivot's scale and the held block's display.
    const int32 HandFit = FoundationHand::IsFitOn() ? 1 : 0;
    if (HandFit != AppliedHandFit)
    {
        AppliedHandFit = HandFit;
        HandPivot->SetRelativeScale3D(FVector(HandFit ? HandFitScale : 1.));
        for (UInstancedStaticMeshComponent* Held : HeldBlocks)
            Held->SetRelativeTransform(HandFit ? FoundationHand::MakeHeldBlockDisplay()
                                               : FTransform(FRotator(10, 35, 0), FVector::ZeroVector, FVector(.11)));
    }
    HandPivot->SetRelativeLocationAndRotation((HandFit ? HandFitPivot : HandRestPivot) +
            FVector(Swing * 6, -Swing * 8, Swing * 4) + FVector(-3, -10, 5 - 2.5 * Bob) * EatBlend,
        FRotator(-55 * Swing - 15 * EatBlend, -10 * Swing - 35 * EatBlend, 0));
}

void UFoundationInteractionView::SetHeldItem(FName Id)
{
    HeldItemId = Id;
    HeldSprite = NAME_None;
    UStaticMesh* Mesh = nullptr;
    UMaterialInterface* Material = nullptr;
    FTransform Display;
    FFoundationItemModelView Model;
    const UFoundationItemAssets* Items = Id.IsNone() ? nullptr : UFoundationItemAssets::LoadDefault();
    if (Items && Items->FindModelView(Id, TEXT("firstperson_righthand"), Model))
    {
        const FFoundationItemTextureAsset& Texture = *Model.Texture;
        UStaticMesh* Cached = GetItemMesh(Texture);
        Material = Cached ? GetItemMaterial(*Items, Texture.Width == 32) : nullptr;
        if (Material)
        {
            Mesh = Cached;
            HeldSprite = Texture.Name;
            if (Model.Display) Display = FoundationItemMesh::MakeDisplayTransform(*Model.Display, HeldModelBlockCm);
        }
    }
    // The mesh first, so the material override lands on its slot.
    HeldItem->SetStaticMesh(Mesh);
    if (!Mesh) return;
    HeldItem->SetMaterial(0, Material);
    HeldItem->SetRelativeTransform(Display);
}

void UFoundationInteractionView::GetStateMeshes(const FFoundationBlockRegistry& Registry, FFoundationBlockState State,
    TArray<UStaticMesh*, TInlineAllocator<4>>& OutMeshes, FBox& OutDrawn)
{
    OutDrawn = FBox(ForceInit);
    TArray<TConstArrayView<int32>> Parts;
    if (Registry.GetModelParts(State, Parts))
    {
        for (const TConstArrayView<int32> Alternatives : Parts)
            if (UStaticMesh* Mesh = Alternatives.IsEmpty()
                    ? nullptr
                    : GetBlockMesh(Registry, FoundationBlockMeshes::Count + Alternatives[0]))
            {
                OutMeshes.Add(Mesh);
                OutDrawn += Mesh->GetBoundingBox();
            }
        return;
    }
    const int32 Kind = FoundationBlockMeshes::GetKind(Registry, State, false);
    const FBox Unit = FoundationBlockMeshes::GetUnitBox(Kind);
    OutMeshes.Add(BlockMeshes[Kind]);
    OutDrawn = FBox(Unit.Min * InteractionViewBlockCm, Unit.Max * InteractionViewBlockCm);
}

UMaterialInterface* UFoundationInteractionView::GetLayerMaterial(const FFoundationBlockType* Type) const
{
    return LayerMaterials[Type && Type->IsWater() ? WaterLayer
                                                  : uint8(Type ? Type->Layer : EFoundationRenderLayer::Opaque)];
}

UStaticMesh* UFoundationInteractionView::GetItemMesh(const FFoundationItemTextureAsset& Texture)
{
    TObjectPtr<UStaticMesh>& Cached = ItemMeshes.FindOrAdd(Texture.Name);
    TArray<bool> Opaque;
    if (!Cached && FoundationItemMesh::ReadMask(Texture, Opaque))
        Cached = FoundationItemMesh::BuildMesh(this,
            FoundationItemMesh::BuildQuads(Texture.Width, Texture.Height, Opaque), Texture.Layer, HeldModelBlockCm);
    return Cached;
}

UMaterialInterface* UFoundationInteractionView::GetItemMaterial(const UFoundationItemAssets& Items, bool b32)
{
    if (ItemMaterials.IsEmpty())
    {
        // The arrays are soft references, loaded here: the first time an item is held.
        ItemMaterials.SetNum(2);
        for (int32 Index = 0; Index < 2; ++Index)
            ItemMaterials[Index] = MakeItemMaterial(Items, Index == 1);
    }
    return ItemMaterials[b32 ? 1 : 0];
}

UMaterialInstanceDynamic* UFoundationInteractionView::MakeItemMaterial(const UFoundationItemAssets& Items, bool b32)
{
    const FSoftObjectPath Path = UUnnamedProjectSettings::GetItemAssetPath(TEXT("M_HeldItem"));
    UMaterialInterface* Base = Cast<UMaterialInterface>(Path.TryLoad());
    if (!Base)
    {
        UE_LOG(LogUnnamedFoundation, Warning,
            TEXT("%s is missing: held items are not drawn. Run CreateItemMaterials.py."), *Path.ToString());
        return nullptr;
    }
    const TSoftObjectPtr<UTexture2DArray>& Array = b32 ? Items.ItemTextures32 : Items.ItemTextures;
    UTexture2DArray* Sprites = Array.LoadSynchronous();
    if (!Sprites)
    {
        UE_LOG(LogUnnamedFoundation, Warning, TEXT("Item sprite array %s is missing: its items are not drawn."),
            *Array.ToString());
        return nullptr;
    }
    UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, this);
    Material->SetTextureParameterValue(TEXT("Sprites"), Sprites);
    return Material;
}

void UFoundationInteractionView::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AFoundationLab* Lab = GetLab();
    if (!Lab || !Lab->GetAuthority()) return;
    const double Start = FPlatformTime::Seconds();
    FFoundationAuthority& Authority = *Lab->GetAuthority();
    if (BreakEffects) BreakEffects->BeginFrame(Authority);
    if (TextureTable)
        TextureTable->SetTick(FMath::FloorToInt64(GetWorld()->GetTimeSeconds() * FoundationBlockFaces::TicksPerSecond));
    ApplyChanges(Authority);
    HandleEvents(Authority);
    UpdateTargets(Authority, DeltaTime);
    UpdateLabels(Authority);
    UpdateEffects(DeltaTime);
    AFoundationPawn* Pawn = GetLocalPawn();
    const auto* Controller = Pawn ? Cast<AFoundationController>(Pawn->GetController()) : nullptr;
    // F1 hides the outline, preview and hand with the HUD; the menu hides them while paused.
    const bool bVisible = Controller && Controller->IsGameplay() && !Controller->GetHud()->bHideHud;
    DrawSelection(Authority, Pawn && Pawn->HasInteraction() ? Pawn : nullptr, bVisible);
    // The break in progress: the block darkens through ten stages.
    const bool bMining = bVisible && Pawn && Pawn->Interaction.bMining;
    // The darkening overlay is opaque: only the untextured fallback uses it; textured blocks show the cracks.
    MiningOverlay->SetVisibility(bMining && !Authority.World.GetRegistry().GetAssets());
    // The breaking effects follow the reference steps even with the HUD hidden (hit sounds); they draw only when
    // visible. With foundation.Break.Cracks on they draw the cracks instead of the lines below.
    const bool bBreakCracks = BreakEffects &&
        BreakEffects->UpdateMining(
            Authority, Pawn && Pawn->HasInteraction() ? &Pawn->Interaction : nullptr, bVisible, DeltaTime);
    if (bMining)
    {
        const FFoundationInteraction& Interaction = Pawn->Interaction;
        const int32 Stage = FMath::Clamp(FMath::FloorToInt32(Interaction.MiningProgress * 10), 0, 9);
        // The crack lines carry the stage on the block's own shape. Vanilla's destroy-stage textures are later
        // polish.
        FBox Cell = FoundationGrid::CellBox(Interaction.MiningCell, Authority.World.OriginCm);
        Authority.World.GetSelectionBox(
            Interaction.MiningCell, Authority.World.Get(Interaction.MiningCell).State, Cell);
        MiningOverlay->SetWorldLocation(Cell.GetCenter());
        MiningOverlay->SetWorldScale3D(Cell.GetSize() / InteractionViewBlockCm * 1.006);
        MiningMaterial->SetVectorParameterValue(
            TEXT("Color"), FMath::Lerp(FLinearColor(.92f, .92f, .92f), FLinearColor(.35f, .35f, .37f), Stage / 9.f));
        if (!bBreakCracks) DrawCracks(Authority, Interaction);
    }
    if (Pawn && Pawn->HasInteraction()) UpdateHeldItem(Authority, Pawn, bVisible, DeltaTime);
    // Item entities draw between the last two reference steps, as the camera does.
    if (ItemEntityView)
        ItemEntityView->Update(Authority, Pawn,
            Pawn ? FMath::Clamp(Pawn->Simulation.Accumulator / GetDefault<UFoundationProfile>()->TickSeconds, 0., 1.)
                 : 1.,
            DeltaTime);
    if (BreakEffects) BreakEffects->EndFrame(Authority, DeltaTime);
    LastViewMs = (FPlatformTime::Seconds() - Start) * 1000.;
}
