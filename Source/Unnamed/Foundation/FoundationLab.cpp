#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationProfile.h"
#include "UnnamedProjectSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AFoundationLab::AFoundationLab()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Cube = CubeMesh.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    Material = BasicMaterial.Object;
}

void AFoundationLab::AddSolid(const FString& Label, FVector Minimum, FVector Maximum, FLinearColor Color, bool bCaption)
{
    const FBox Box(Minimum, Maximum);
    Primitives.Add({Box, Label, EFoundationTargetKind::Block, false});
    CollisionBoxes.Add(Box);
    AddMesh(Box, Color, true);
    if (bCaption) AddLabel(Label, Box);
}

void AFoundationLab::AddWater(FString Label, FVector Minimum, FVector Maximum)
{
    const FBox Box(Minimum, Maximum);
    WaterBoxes.Add(Box);
    // Opaque from outside; its faces cull from inside, so the view stays clear underwater.
    AddMesh(Box, FoundationTint::Water, false);
    AddLabel(Label, Box);
}

UStaticMeshComponent* AFoundationLab::AddMesh(const FBox& Box, FLinearColor Color, bool bCollides)
{
    auto* Mesh = NewObject<UStaticMeshComponent>(this);
    Mesh->SetStaticMesh(Cube);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetupAttachment(RootComponent);
    Mesh->RegisterComponent();
    Mesh->SetWorldLocation(Box.GetCenter());
    Mesh->SetWorldScale3D(Box.GetSize() / 100.);
    if (bCollides)
    {
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    }
    else
    {
        // Water: no collision, and no solid-looking shadow across the floor.
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(false);
    }
    if (Material)
    {
        auto* Dynamic = UMaterialInstanceDynamic::Create(Material, this);
        Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
        Mesh->SetMaterial(0, Dynamic);
    }
    return Mesh;
}

UTextRenderComponent* AFoundationLab::AddLabel(const FString& Label, const FBox& Box)
{
    auto* Text = NewObject<UTextRenderComponent>(this);
    Text->SetupAttachment(RootComponent);
    Text->RegisterComponent();
    Text->SetText(FText::FromString(Label));
    Text->SetWorldSize(18);
    Text->SetHorizontalAlignment(EHTA_Center);
    Text->SetWorldLocation({Box.GetCenter().X, Box.Min.Y - 4, Box.Max.Z + 25});
    Text->SetWorldRotation({0, -90, 0});
    return Text;
}

void AFoundationLab::BeginPlay()
{
    Super::BeginPlay();
    // Both materials take the same "Color" tint; the dev grid is the project surface when it exists.
    if (UMaterialInterface* Grid = UUnnamedProjectSettings::Get().DevGridMaterial.LoadSynchronous()) Material = Grid;
    BuildLayout();
}

void AFoundationLab::BuildLayout()
{
    AddGround();
    BuildMovementCourse();
}

void AFoundationLab::AddGround()
{
    // The dev grid's 1 m and 10 m lines are the measuring scale; red follows +X (east), green +Y (south).
    AddSolid(TEXT("ground"), {-GroundHalfExtentCm, -GroundHalfExtentCm, -100},
        {GroundHalfExtentCm, GroundHalfExtentCm, 0}, FoundationTint::Ground, false);
}

void AFoundationLab::BuildMovementCourse()
{
    const FVector O = CourseOrigin;
    const auto Course = [this, O](const TCHAR* Label, FVector Minimum, FVector Maximum)
    { AddSolid(Label, O + Minimum, O + Maximum, FoundationTint::Course, true); };
    Course(TEXT("50 cm step"), {300, -250, 0}, {500, -50, 50});
    Course(TEXT("60 cm step"), {600, -250, 0}, {800, -50, 60});
    Course(TEXT("61 cm blocked"), {900, -250, 0}, {1100, -50, 61});
    Course(TEXT("100 cm jump"), {1200, -250, 0}, {1400, -50, 100});
    Course(TEXT("155 cm ceiling: crouch"), {300, 300, 155}, {800, 700, 210});
    Course(TEXT("gap 59 cm BLOCKED"), {-700, 300, 0}, {-600, 900, 220});
    Course(TEXT("gap wall"), {-541, 300, 0}, {-441, 900, 220});
    Course(TEXT("gap 61 cm CLEAR"), {-380, 300, 0}, {-280, 900, 220});
    Course(TEXT("raised ledge +2 blocks"), {-1500, -1000, 0}, {-1000, -500, 200});
    Course(TEXT("ledge access 50"), {-1800, -800, 0}, {-1700, -600, 50});
    Course(TEXT("ledge access100"), {-1700, -800, 0}, {-1600, -600, 100});
    Course(TEXT("ledge access150"), {-1600, -800, 0}, {-1500, -600, 150});
    Course(TEXT("corner comparison"), {-600, -600, 0}, {-500, -500, 200});
    // Player vitals fixtures, east of the steps: jump up one block at a time, then drop five blocks
    // (fall damage); stand in the water column to use air and drown.
    for (int32 I = 1; I <= 4; ++I)
        Course(
            *FString::Printf(TEXT("jump stair %d"), I), {1400. + I * 100, -1000, 0}, {1500. + I * 100, -800, I * 100.});
    Course(TEXT("5-block drop: fall damage"), {1900, -1000, 0}, {2200, -800, 500});
    AddWater(TEXT("water column: eyes underwater use air"), O + FVector(1500, 300, 0), O + FVector(1800, 600, 300));
}

void AFoundationLab::GatherCollision(const FBox& Region, TArray<FBox>& Out) const
{
    // The primitive course is small; every cuboid is offered and the solver ignores distant ones.
    Out.Append(CollisionBoxes);
}

void AFoundationLab::ApplyLogicalOffset(const FVector& Offset)
{
    for (FBox& Box : CollisionBoxes)
        Box = Box.ShiftBy(Offset);
    for (auto& Item : Primitives)
        Item.Box = Item.Box.ShiftBy(Offset);
    for (FBox& Box : WaterBoxes)
        Box = Box.ShiftBy(Offset);
}

bool AFoundationLab::IsInWater(FVector Point) const
{
    return WaterBoxes.ContainsByPredicate([&Point](const FBox& Water) { return Water.IsInside(Point); });
}

bool AFoundationLab::IsInWater(const FBox& Body) const
{
    const FBox Inner = Body.ExpandBy(-.1);
    return WaterBoxes.ContainsByPredicate([&Inner](const FBox& Water) { return Water.Intersect(Inner); });
}

FFoundationTarget AFoundationLab::TraceTarget(FVector Eye, FVector Direction, const UFoundationProfile& Profile) const
{
    FFoundationTarget Best;
    double BestDistance = FMath::Max(Profile.BlockReachCm, Profile.EntityReachCm) + 100;
    for (const auto& Item : Primitives)
    {
        const FVector End = Eye + Direction * BestDistance;
        FVector Hit, Normal;
        float Time = 0;
        if (FMath::LineExtentBoxIntersection(Item.Box, Eye, End, FVector::ZeroVector, Hit, Normal, Time))
        {
            const double Distance = (Hit - Eye).Size();
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                Best.Kind = Item.Kind;
                Best.Name = Item.Label;
                Best.Hit = Hit;
                Best.Normal = Normal;
                Best.DistanceCm = Distance;
                Best.ReachCm =
                    Item.Kind == EFoundationTargetKind::Entity ? Profile.EntityReachCm : Profile.BlockReachCm;
                Best.bInRange = Distance < Best.ReachCm - .0001;
                Best.bInteractive = Item.bInteractive;
            }
        }
    }
    return Best;
}
