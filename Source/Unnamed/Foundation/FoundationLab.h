#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Foundation/FoundationActions.h"
#include "FoundationLab.generated.h"

class UFoundationProfile;
class UStaticMesh;
class UMaterialInterface;

struct FFoundationPrimitive
{
    FBox Box;
    FString Label;
    EFoundationTargetKind Kind;
    bool bInteractive = false;
};
UCLASS()
class UNNAMED_API AFoundationLab : public AActor
{
    GENERATED_BODY()
public:
    AFoundationLab();
    virtual void BeginPlay() override;
    TArray<FBox> CollisionBoxes;
    TArray<FFoundationPrimitive> Primitives;
    // Nearest primitive along the view ray, with reach checked against the profile.
    FFoundationTarget TraceTarget(FVector Eye, FVector Direction, const UFoundationProfile& Profile) const;

private:
    void AddPrimitive(FString Label, FVector Minimum, FVector Maximum,
        EFoundationTargetKind Kind = EFoundationTargetKind::Block, bool bInteractive = false);
    UPROPERTY()
    TObjectPtr<UStaticMesh> Cube;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> Material;
};
