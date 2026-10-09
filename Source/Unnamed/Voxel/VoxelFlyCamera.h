#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelFlyCamera.generated.h"

class APlayerController;
class UCameraComponent;

// voxel.Fly's free camera: detached from the player, who holds still (no simulation, no gameplay input), so the
// streaming rings follow the camera across the world. The movement keys (W, A, S, D) fly along the view, Space and
// Left Ctrl rise and sink, Left Shift goes four times as fast, and the mouse looks around. Captures fly it along a
// scripted path instead (FVoxelFlight), with the keys ignored.
UCLASS()
class UNNAMED_API AVoxelFlyCamera : public AActor
{
    GENERATED_BODY()
public:
    AVoxelFlyCamera();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // Starts the camera at the player's view, or stops it and gives the view back to the player. A speed above 0
    // sets blocks per second. Returns the camera, or null when it stopped.
    static AVoxelFlyCamera* Toggle(UWorld* World, double BlocksPerSecond);
    static AVoxelFlyCamera* Find(const UWorld* World);

    UCameraComponent* GetCamera() const { return Camera; }

    double BlocksPerSecond = 40;
    // Keys and mouse ignored: something else moves the camera (captures).
    bool bScripted = false;

private:
    UPROPERTY(VisibleAnywhere, Category = "Voxel")
    TObjectPtr<UCameraComponent> Camera;
    TWeakObjectPtr<APlayerController> Controller;
};
