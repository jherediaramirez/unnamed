#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationProfile.h"
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

void AFoundationLab::AddPrimitive(
    FString Label, FVector Minimum, FVector Maximum, EFoundationTargetKind Kind, bool bInteractive)
{
    const FBox Box(Minimum, Maximum);
    Primitives.Add({Box, Label, Kind, bInteractive});
    CollisionBoxes.Add(Box);
    auto* Mesh = NewObject<UStaticMeshComponent>(this);
    Mesh->SetStaticMesh(Cube);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetupAttachment(RootComponent);
    Mesh->RegisterComponent();
    Mesh->SetWorldLocation(Box.GetCenter());
    Mesh->SetWorldScale3D(Box.GetSize() / 100.);
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    if (Material)
    {
        auto* Dynamic = UMaterialInstanceDynamic::Create(Material, this);
        Dynamic->SetVectorParameterValue(TEXT("Color"),
            Kind == EFoundationTargetKind::Entity ? FLinearColor(.9f, .35f, .15f)
                : bInteractive                    ? FLinearColor(.2f, .6f, .8f)
                                                  : FLinearColor(.5f, .6f, .55f));
        Mesh->SetMaterial(0, Dynamic);
    }
    auto* Text = NewObject<UTextRenderComponent>(this);
    Text->SetupAttachment(RootComponent);
    Text->RegisterComponent();
    Text->SetText(FText::FromString(Label));
    Text->SetWorldSize(18);
    Text->SetHorizontalAlignment(EHTA_Center);
    Text->SetWorldLocation({Box.GetCenter().X, Box.Min.Y - 4, Box.Max.Z + 25});
    Text->SetWorldRotation({0, -90, 0});
}

void AFoundationLab::BeginPlay()
{
    Super::BeginPlay();
    AddPrimitive(TEXT("100 cm = 1 block | +X east, +Y south"), {-3000, -3000, -100}, {3000, 3000, 0});
    AddPrimitive(TEXT("50 cm step"), {300, -250, 0}, {500, -50, 50});
    AddPrimitive(TEXT("60 cm step"), {600, -250, 0}, {800, -50, 60});
    AddPrimitive(TEXT("61 cm blocked"), {900, -250, 0}, {1100, -50, 61});
    AddPrimitive(TEXT("100 cm jump"), {1200, -250, 0}, {1400, -50, 100});
    AddPrimitive(TEXT("155 cm ceiling: crouch"), {300, 300, 155}, {800, 700, 210});
    AddPrimitive(TEXT("gap 59 cm BLOCKED"), {-700, 300, 0}, {-600, 900, 220});
    AddPrimitive(TEXT("gap wall"), {-541, 300, 0}, {-441, 900, 220});
    AddPrimitive(TEXT("gap 61 cm CLEAR"), {-380, 300, 0}, {-280, 900, 220});
    AddPrimitive(TEXT("raised ledge +2 blocks"), {-1500, -1000, 0}, {-1000, -500, 200});
    AddPrimitive(TEXT("ledge access 50"), {-1800, -800, 0}, {-1700, -600, 50});
    AddPrimitive(TEXT("ledge access100"), {-1700, -800, 0}, {-1600, -600, 100});
    AddPrimitive(TEXT("ledge access150"), {-1600, -800, 0}, {-1500, -600, 150});
    AddPrimitive(
        TEXT("block fixture: 4.5 block reach"), {500, 1000, 0}, {600, 1100, 200}, EFoundationTargetKind::Block, true);
    AddPrimitive(
        TEXT("entity fixture: 3 block reach"), {-200, 1000, 0}, {-140, 1060, 180}, EFoundationTargetKind::Entity, true);
    AddPrimitive(TEXT("occluder"), {-200, 1300, 0}, {0, 1350, 230});
    AddPrimitive(TEXT("hidden entity"), {-160, 1400, 0}, {-100, 1460, 180}, EFoundationTargetKind::Entity, true);
    AddPrimitive(TEXT("corner comparison"), {-600, -600, 0}, {-500, -500, 200});
    for (int32 I = -10; I <= 10; ++I)
    {
        AddPrimitive(FString::Printf(TEXT("%d m"), I), {double(I * 100), -1500, -.2}, {double(I * 100 + 1), -1300, 0});
    }
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
