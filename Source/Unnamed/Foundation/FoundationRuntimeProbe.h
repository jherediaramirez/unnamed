#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FoundationRuntimeProbe.generated.h"

class AFoundationPawn;
class AFoundationController;
class UMenuInputSettings;
class FJsonValue;

// Opt-in application-level verification. Uses the real controller/action pipeline,
// never OS event injection. Reports this distinction in its output.
UCLASS()

class UFoundationRuntimeProbe : public UActorComponent
{
    GENERATED_BODY()
public:
    UFoundationRuntimeProbe();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;

private:
    AFoundationPawn* Pawn = nullptr;
    AFoundationController* PC = nullptr;
    UPROPERTY()
    TObjectPtr<UMenuInputSettings> Bridge;

    struct FCase
    {
        FString Name;
        double Duration;
        TFunction<void()> Start, End;
    };

    TArray<FCase> Cases;
    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> PhaseRates;
    TArray<FKey> DownKeys;
    TArray<TPair<FKey, bool>> QueuedKeys;
    int32 DeferKeysUntilFrame = 0;
    FString Output;
    int32 Phase = -1, Failures = 0, RequestedFPS = 60, Frames = 0, PhaseStartFrame = 0;
    double Started = 0, TotalStarted = 0, PhaseStarted = 0, Apex = 0, LastZ = 0;
    int64 LaunchTick = -1, LandTick = -1;
    bool bDone = false;
    void Key(FKey Key, bool Down);
    void Reset(FVector Position = FVector(0, 0, 0));
    void Record(FString Name, bool Pass, FString Detail);
    void Measure(FString Name, double Actual, double Expected, double Tolerance);
    void Add(FString Name, double Duration, TFunction<void()> Start, TFunction<void()> End);
    void Finish();
};
