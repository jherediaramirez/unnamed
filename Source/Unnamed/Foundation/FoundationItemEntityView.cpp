#include "Foundation/FoundationItemEntityView.h"
#include "Algo/Count.h"
#include "Foundation/FoundationBreakEffects.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationItemMesh.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

void FFoundationItemEntityView::Update(
    const FFoundationAuthority& Authority, const AFoundationPawn* Pawn, double Alpha, double DeltaTime)
{
    using namespace FoundationItemEntities;
    const TArray<FFoundationItemEntity>& Entities = Authority.GetItemEntities();
    const FVector Origin = Authority.World.OriginCm;
    // Distance from the view: the player's camera, or a shot camera it is looking through.
    const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    const FVector Viewer = Controller && Controller->PlayerCameraManager
        ? Controller->PlayerCameraManager->GetCameraLocation()
        : Pawn ? Pawn->GetActorLocation()
               : FVector::ZeroVector;
    // The drops to draw: not frozen, in range, the nearest MaxVisuals.
    TArray<TPair<double, int32>> Wanted;
    for (int32 Index = 0; Index < Entities.Num(); ++Index)
    {
        const FFoundationItemEntity& Entity = Entities[Index];
        const double Distance = FVector::DistSquared(Origin + Entity.LocalCm, Viewer);
        if (!Entity.bFrozen && (!Pawn || Distance <= DrawCm * DrawCm)) Wanted.Add({Distance, Index});
    }
    if (Wanted.Num() > MaxVisuals)
    {
        Wanted.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
        Wanted.SetNum(MaxVisuals);
    }
    TSet<int32> Shown;
    for (const TPair<double, int32>& Pair : Wanted)
        Shown.Add(Entities[Pair.Value].Id);
    for (FVisual& Visual : Visuals)
        if (Visual.bUsed && Visual.FlyAge < 0 && !Shown.Contains(Visual.EntityId)) Release(Visual);
    Drawn = 0;
    for (const TPair<double, int32>& Pair : Wanted)
    {
        const FFoundationItemEntity& Entity = Entities[Pair.Value];
        FVisual& Visual = Acquire(Entity.Id);
        Visual.bDrawn = Visual.Item == Entity.Item ? Visual.bDrawn : SetItem(Visual, Authority, Entity.Item);
        if (!Visual.bDrawn)
        {
            // Nothing to draw yet (the assets not loaded): try again next frame.
            Visual.Item = INDEX_NONE;
            continue;
        }
        // Items fixes (Codex review 4, #4): one copy with foundation.Items.Entities 0, as before items S8b.
        if (const int32 Copies = Authority.Options.bItemEntities ? GetCopies(Entity.Count) : 1; Copies != Visual.Copies)
            SetCopies(Visual, Copies);
        // Drawn between the last two steps; the look's clock runs with them.
        const double Age = Entity.Age - 1 + Alpha;
        const double Bob = BobCm * FMath::Sin(Age / BobTicks + Entity.BobOffset) + BobCm;
        const double Spin = Age / SpinTicks + Entity.BobOffset;
        Visual.Root->SetWorldLocationAndRotation(
            Origin + FMath::Lerp(Entity.PreviousLocalCm, Entity.LocalCm, Alpha) + FVector(0, 0, Bob + Visual.LiftCm),
            FRotator(0, FMath::RadiansToDegrees(Spin), 0));
        ++Drawn;
    }
    // Whole stacks picked up fly to the player's centre, then go.
    const double FlySeconds = FlyTicks * GetDefault<UFoundationProfile>()->TickSeconds;
    for (FVisual& Visual : Visuals)
    {
        if (!Visual.bUsed || Visual.FlyAge < 0) continue;
        Visual.FlyAge += DeltaTime;
        if (!Pawn || Visual.FlyAge >= FlySeconds)
        {
            Release(Visual);
            continue;
        }
        const FVector Center = Pawn->GetActorLocation() + FVector(0, 0, Pawn->Simulation.HeightCm * .5);
        Visual.Root->SetWorldLocation(FMath::Lerp(Visual.FlyFrom, Center, Visual.FlyAge / FlySeconds));
    }
}

void FFoundationItemEntityView::OnPickedUp(const FFoundationEvent& Event, const AFoundationPawn* Pawn)
{
    // The pickup sound at the drop, every pickup (foundation.Sound.Events): vanilla's call-site volume 0.2 and
    // pitch 1.6 to 3.4 (facts section 2; drawn uniformly here, provisional).
    if (FFoundationBreakEffects* Effects = View.GetBreakEffects())
        Effects->PlayEvent(TEXT("entity.item.pickup"), Event.LocationCm, .2f, FMath::FRandRange(1.6f, 3.4f));
    const int32* Found = ByEntity.Find(Event.EntityId);
    if (!Found || !Event.bWholeStack) return;
    FVisual& Visual = Visuals[*Found];
    if (!Pawn || Pawn->Interaction.PlayerId != Event.Player || !Visual.bDrawn)
    {
        Release(Visual);
        return;
    }
    // The entity is gone: the visual is no longer its, only flying.
    ByEntity.Remove(Event.EntityId);
    Visual.EntityId = 0;
    Visual.FlyAge = 0;
    Visual.FlyFrom = Visual.Root->GetComponentLocation();
}

int32 FFoundationItemEntityView::GetDrawnCopies(int32 EntityId) const
{
    const int32* Found = ByEntity.Find(EntityId);
    if (!Found || !Visuals[*Found].bDrawn) return 0;
    const FVisual& Visual = Visuals[*Found];
    if (Visual.bBlock) return Visual.BlockParts > 0 ? Visual.Blocks[0]->GetInstanceCount() : 0;
    return Algo::CountIf(Visual.SpriteCopies, [](const UStaticMeshComponent* Copy) { return Copy->IsVisible(); }) +
        (Visual.Sprite && Visual.Sprite->IsVisible() ? 1 : 0);
}

int32 FFoundationItemEntityView::GetFlyingCount() const
{
    return Algo::CountIf(Visuals, [](const FVisual& Visual) { return Visual.bUsed && Visual.FlyAge >= 0; });
}

FString FFoundationItemEntityView::Describe() const
{
    FString Text;
    for (const FVisual& Visual : Visuals)
    {
        if (!Visual.bUsed) continue;
        Text += FString::Printf(TEXT("[drop %d at %s root %d:"), Visual.EntityId,
            *Visual.Root->GetComponentLocation().ToCompactString(), Visual.Root->IsVisible());
        TArray<const UStaticMeshComponent*, TInlineAllocator<4>> Parts(Visual.Blocks);
        if (Visual.Sprite) Parts.Add(Visual.Sprite);
        Parts.Append(Visual.SpriteCopies);
        for (const UStaticMeshComponent* Part : Parts)
            if (Part->IsVisible())
                Text += FString::Printf(TEXT(" %s %s %s"), *GetNameSafe(Part->GetStaticMesh()),
                    *GetNameSafe(Part->GetMaterial(0)), *Part->Bounds.GetBox().ToString());
        Text += TEXT("]");
    }
    return Text;
}

FFoundationItemEntityView::FVisual& FFoundationItemEntityView::Acquire(int32 EntityId)
{
    if (const int32* Found = ByEntity.Find(EntityId)) return Visuals[*Found];
    int32 Index = Visuals.IndexOfByPredicate([](const FVisual& Visual) { return !Visual.bUsed; });
    if (Index == INDEX_NONE)
    {
        Index = Visuals.AddDefaulted();
        AActor* Owner = View.GetOwner();
        USceneComponent* Root = NewObject<USceneComponent>(Owner);
        Root->SetMobility(EComponentMobility::Movable);
        Root->SetupAttachment(Owner->GetRootComponent());
        Root->RegisterComponent();
        Root->SetUsingAbsoluteLocation(true);
        Root->SetUsingAbsoluteRotation(true);
        Root->SetUsingAbsoluteScale(true);
        View.Owned.Add(Root);
        Visuals[Index].Root = Root;
    }
    FVisual& Visual = Visuals[Index];
    // Released visuals hid their root with their parts; SetItem shows the parts the item uses.
    Visual.Root->SetVisibility(true, false);
    Visual.bUsed = true;
    Visual.EntityId = EntityId;
    Visual.FlyAge = -1;
    ByEntity.Add(EntityId, Index);
    return Visual;
}

void FFoundationItemEntityView::Release(FVisual& Visual)
{
    if (Visual.EntityId) ByEntity.Remove(Visual.EntityId);
    Visual.bUsed = Visual.bDrawn = false;
    Visual.EntityId = 0;
    Visual.Item = INDEX_NONE;
    Visual.Copies = 0;
    Visual.FlyAge = -1;
    // Hides its parts; SetItem shows the ones the next item uses.
    Visual.Root->SetVisibility(false, true);
}

bool FFoundationItemEntityView::SetItem(FVisual& Visual, const FFoundationAuthority& Authority, int32 Item)
{
    Visual.Item = Item;
    Visual.Copies = 0;
    for (UInstancedStaticMeshComponent* Block : Visual.Blocks)
        Block->SetVisibility(false);
    if (Visual.Sprite) Visual.Sprite->SetVisibility(false);
    for (UStaticMeshComponent* Copy : Visual.SpriteCopies)
        Copy->SetVisibility(false);
    const FFoundationItemDefinition& Definition = Authority.Rules.GetItem(Item);
    if (Definition.Icon == TEXT("block"))
    {
        // A block item: its block's default state, as the held block draws it, in block/block.json's ground display.
        const FFoundationBlockType* Type = Authority.World.FindBlock(Definition.PlacesBlock);
        if (!Type || View.BlockMeshes.IsEmpty() || View.LayerMaterials.IsEmpty()) return false;
        const FFoundationBlockRegistry& Registry = Authority.World.GetRegistry();
        const FFoundationBlockState State = Type->GetDefaultState();
        TArray<UStaticMesh*, TInlineAllocator<4>> Meshes;
        FBox Box(ForceInit);
        View.GetStateMeshes(Registry, State, Meshes, Box);
        if (Meshes.IsEmpty()) return false;
        UMaterialInterface* Material = View.GetLayerMaterial(Type);
        float Data[UFoundationInteractionView::CustomDataFloats];
        UFoundationInteractionView::MakeCustomData(Registry, State, Data);
        FFoundationItemDisplayAsset Ground;
        Ground.Translation = FVector(0, 3, 0);
        Ground.Scale = FVector(.25);
        const FTransform Display = FoundationItemMesh::MakeDisplayTransform(Ground, UFoundationProfile::BlockCm);
        while (Visual.Blocks.Num() < Meshes.Num())
        {
            UInstancedStaticMeshComponent* Block = View.MakeBlockComponent(Visual.Root, Meshes[0], 0);
            View.Owned.Add(Block);
            Visual.Blocks.Add(Block);
        }
        for (int32 Index = 0; Index < Meshes.Num(); ++Index)
        {
            UInstancedStaticMeshComponent* Block = Visual.Blocks[Index];
            Block->ClearInstances();
            Block->SetStaticMesh(Meshes[Index]);
            Block->SetMaterial(0, Material);
            Block->SetRelativeTransform(Display);
            Block->SetVisibility(true);
        }
        Visual.bBlock = true;
        Visual.BlockParts = Meshes.Num();
        Visual.BlockCenter = Box.GetCenter();
        Visual.BlockData = TArray<float>(Data, UE_ARRAY_COUNT(Data));
        Visual.Display = Display;
        Visual.LiftCm = .25 * Ground.Scale.Y * UFoundationProfile::BlockCm;
        return true;
    }
    // Any other item: its extruded sprite in the item model's ground display. The cached mesh is built at the hand's
    // HeldModelBlockCm per model block, so it is scaled up to the world's.
    const UFoundationItemAssets* Items = UFoundationItemAssets::LoadDefault();
    FFoundationItemModelView Model;
    if (!Items || !Items->FindModelView(Definition.Id, TEXT("ground"), Model)) return false;
    UStaticMesh* Mesh = View.GetItemMesh(*Model.Texture);
    if (!Mesh) return false;
    UMaterialInstanceDynamic*& Material = SpriteMaterials[Model.Texture->Width == 32 ? 1 : 0];
    if (!Material)
    {
        // The hand's material without its glow: lit by the scene.
        Material = View.MakeItemMaterial(*Items, Model.Texture->Width == 32);
        if (!Material) return false;
        Material->SetScalarParameterValue(TEXT("Glow"), 0);
        Material->SetScalarParameterValue(TEXT("LightGlow"), 0);
        View.Owned.Add(Material);
    }
    if (!Visual.Sprite)
    {
        Visual.Sprite = NewObject<UStaticMeshComponent>(View.GetOwner());
        Visual.Sprite->SetMobility(EComponentMobility::Movable);
        Visual.Sprite->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Visual.Sprite->SetupAttachment(Visual.Root);
        Visual.Sprite->RegisterComponent();
        View.Owned.Add(Visual.Sprite);
    }
    Visual.Sprite->SetStaticMesh(Mesh);
    Visual.Sprite->SetMaterial(0, Material);
    const FTransform Display = Model.Display
        ? FoundationItemMesh::MakeDisplayTransform(*Model.Display, UFoundationProfile::BlockCm)
        : FTransform::Identity;
    Visual.bBlock = false;
    Visual.SpriteTransform = FTransform(FQuat::Identity, FVector::ZeroVector,
                                 FVector(UFoundationProfile::BlockCm / UFoundationInteractionView::HeldModelBlockCm)) *
        Display;
    Visual.Sprite->SetRelativeTransform(Visual.SpriteTransform);
    Visual.Sprite->SetVisibility(true);
    Visual.LiftCm = .25 * (Model.Display ? Model.Display->Scale.Y : 1.) * UFoundationProfile::BlockCm;
    return true;
}

void FFoundationItemEntityView::SetCopies(FVisual& Visual, int32 Copies)
{
    using namespace FoundationItemEntities;
    Visual.Copies = Copies;
    // Offsets in the root's space: none for the first copy, then fixed by the entity.
    FRandomStream Random(int32(GetTypeHash(Visual.EntityId)));
    TArray<FVector, TInlineAllocator<5>> Offsets{FVector::ZeroVector};
    const double Spread = Visual.bBlock ? CopySpreadCm : CopySpreadCm * .5;
    while (Offsets.Num() < Copies)
        Offsets.Add(FVector(Random.FRandRange(-1, 1), Random.FRandRange(-1, 1), Random.FRandRange(-1, 1)) * Spread);
    if (Visual.bBlock)
    {
        for (int32 Part = 0; Part < Visual.BlockParts; ++Part)
        {
            UInstancedStaticMeshComponent* Block = Visual.Blocks[Part];
            Block->ClearInstances();
            for (const FVector& Offset : Offsets)
                Block->SetCustomDataById(
                    Block->AddInstanceById(
                        FTransform(Visual.Display.InverseTransformVector(Offset) - Visual.BlockCenter), false),
                    Visual.BlockData);
        }
        return;
    }
    while (Visual.SpriteCopies.Num() < Copies - 1)
    {
        UStaticMeshComponent* Copy = NewObject<UStaticMeshComponent>(View.GetOwner());
        Copy->SetMobility(EComponentMobility::Movable);
        Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Copy->SetupAttachment(Visual.Root);
        Copy->RegisterComponent();
        View.Owned.Add(Copy);
        Visual.SpriteCopies.Add(Copy);
    }
    for (int32 Index = 0; Index < Visual.SpriteCopies.Num(); ++Index)
    {
        UStaticMeshComponent* Copy = Visual.SpriteCopies[Index];
        const bool bShown = Index + 1 < Copies;
        Copy->SetVisibility(bShown);
        if (!bShown) continue;
        Copy->SetStaticMesh(Visual.Sprite->GetStaticMesh());
        Copy->SetMaterial(0, Visual.Sprite->GetMaterial(0));
        Copy->SetRelativeTransform(Visual.SpriteTransform * FTransform(Offsets[Index + 1]));
    }
}
