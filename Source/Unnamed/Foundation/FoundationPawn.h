#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Foundation/FoundationActions.h"
#include "Foundation/FoundationSimulation.h"
#include "FoundationPawn.generated.h"

class AFoundationLab;
class UBoxComponent;
class UCameraComponent;

UCLASS()
class UNNAMED_API AFoundationPawn : public APawn
{
    GENERATED_BODY()
public:
    AFoundationPawn();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;
    FFoundationSimulation Simulation;
    FFoundationIntent LastIntent;
    FFoundationEligibility Eligibility;
    FVector EyePosition() const;
    FFoundationTarget Target() const;
    void SetSimulationEnabled(bool Enabled);

    void SetPerspective(int32 Value) { Perspective = Value; }

    int32 GetPerspective() const { return Perspective; }

    UCameraComponent* GetCamera() const { return Camera; }

    AFoundationLab* GetLab() const { return Lab.Get(); }

private:
    UPROPERTY()
    TObjectPtr<UBoxComponent> Collision;
    UPROPERTY()
    TObjectPtr<UCameraComponent> Camera;
    TWeakObjectPtr<AFoundationLab> Lab;
    bool bSimulate = true;
    bool bProbeCreated = false;
    int32 Perspective = 0;
};
