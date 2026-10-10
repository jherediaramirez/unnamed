#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelLightSources.h"

class AActor;
class UPointLightComponent;

// The torch light system's pool (session T3): the light sources of every column on screen, handed over by
// UVoxelWorldSubsystem as each column's mesh is applied (FVoxelColumnMesh::Lights), and up to voxel.LightCount movable
// point lights around the camera, picked by VoxelLightSources::PickLights. AVoxelWorld owns it; its light components
// live on the world's actor, held for the GC by the actor's UPROPERTY array the pool is given. Game thread.
//
// Live only while r.MegaLights.EnableForProject is 1 and voxel.Lights is on; otherwise it holds no light component,
// so the frame is the one the world drew before the pool existed. It re-picks only when something changed: the
// sources of a column within reach of the camera, the camera by a block or more since the last pick, or a setting
// (either switch included). A light keeps its component by its pick's key, so a light that stays is not touched and
// one that changed is only moved or retuned.
class UNNAMED_API FVoxelLights
{
public:
    // What the pool runs with: AVoxelWorld reads them from the console each frame (ReadSettings).
    struct FSettings
    {
        // r.MegaLights.EnableForProject is 1 and voxel.Lights is on.
        bool bLive = false;
        // voxel.LightCount: the most lights, at most VoxelLightSources::MaxLights.
        int32 Count = 64;
        // voxel.LightRadius: sources farther than this from the camera (blocks) get no light.
        double Radius = 48;
        // voxel.LightIntensity: candelas of one level-14 source; a light gets it times its pick's weight. 600 cd at
        // 2,200 K with inverse-squared falloff is warm A, Jesus's pick in the torch look round (T3b).
        float IntensityCd = 600;
        // voxel.LightFalloff: 0 for inverse-squared falloff in candelas (IntensityCd); above 0 (at most 16) the lights'
        // falloff exponent instead, with Brightness as their unitless intensity.
        float Falloff = 0;
        // voxel.LightBrightness: the unitless intensity of one level-14 source while Falloff is above 0; a light gets
        // it times its pick's weight.
        float Brightness = 700;
        // voxel.LightTemperature: the lights' colour temperature (K).
        float Temperature = 2200;

        bool operator==(const FSettings& Other) const = default;
    };

    // Owner: the actor the lights attach to and their outer; Components: the owner's UPROPERTY array that keeps them
    // from the GC, one per slot. A null owner (tests) keeps the slots and picks without components.
    FVoxelLights(AActor* InOwner, TArray<TObjectPtr<UPointLightComponent>>& InComponents);
    FVoxelLights(const FVoxelLights&) = delete;
    FVoxelLights& operator=(const FVoxelLights&) = delete;

    // The console now: voxel.Lights and r.MegaLights.EnableForProject (bLive), voxel.LightCount, voxel.LightRadius,
    // voxel.LightIntensity, voxel.LightFalloff, voxel.LightBrightness and voxel.LightTemperature.
    static FSettings ReadSettings();
    // voxel.LightMinLevel: the light level a state needs for a light (VoxelLightSources::MakeTable). A change makes a
    // new light table, which columns use from their next mesh (voxel.Regenerate meshes them all again).
    static int32 ReadMinLevel();
    // voxel.TorchGlow (0-15, default DefaultTorchGlow): the glow the torch-rule models are meshed with
    // (VoxelLightSources::MakeTorchGlowTable); the world reads it when it is built and at voxel.Regenerate, the
    // Playground as it makes a torch's instance (FoundationBlockMeshes::GetDrawnLight). The flame glows on its own
    // (FoundationBlockMeshes::TorchFlameLight). 1 is warm A, the whole torch at 1, as Jesus picked it (T3b); 0 leaves
    // only the flame glowing, but the stick then reads black in a cave (the torch's top face shadows it from its own
    // light), so it waits for a stick glow under level 1 (T3c); 14 is the whole torch's old white glow.
    static constexpr int32 DefaultTorchGlow = 1;
    static int32 ReadTorchGlow();
    // -VoxelTorchGlow=N on the command line sets voxel.TorchGlow (by command line, so the console still wins later).
    static void SetTorchGlowFromCommandLine();

    // A column's sources as its latest mesh found them; the same sources again change nothing.
    void SetColumn(FIntPoint Column, TArray<FVoxelLightSource>&& LightSources);
    // The column left the screen: it gives no light.
    void ClearColumn(FIntPoint Column);
    // Forgets every column and destroys the components (streaming stopped or started again).
    void Reset();
    // World position of cell (0, 0, 0)'s minimum corner (AVoxelWorld::OriginCm), used by the next placements. It
    // never moves the components: on a world rebase the engine shifts them with the actor's root.
    void SetOrigin(const FVector& InOriginCm);

    // Once a frame with the camera (cm) and the settings: re-picks and updates the components when needed. True when
    // it re-picked.
    bool Update(const FVector& CameraCm, const FSettings& Settings);

    bool IsLive() const { return bLive; }
    int32 GetColumnCount() const { return Columns.Num(); }
    int32 GetSourceCount() const { return SourceCount; }
    // The last pick, nearest the camera first.
    TConstArrayView<FVoxelLightPick> GetPicks() const { return Picks; }
    // Per slot (component, when there is an owner): the key of the pick it shows, or unset while free.
    TArray<TOptional<uint64>> GetSlotKeys() const;
    int32 GetComponentCount() const;
    // Re-picks since the pool was made.
    int64 GetUpdateCount() const { return UpdateCount; }
    // The last update's game-thread ms (a re-pick, or the switch off).
    double GetLastMs() const { return LastMs; }
    // voxel.LightsInfo: whether it is live, columns and sources known, picks, components, the last update's changes
    // and the last and peak update times.
    FString Describe() const;

private:
    struct FSlot
    {
        bool bUsed = false;
        uint64 Key = 0;
        FVector Centroid = FVector::ZeroVector;
        float Weight = 0;
        uint8 Level = 0;
    };
    // What the last update did to the components.
    struct FChanges
    {
        int32 Kept = 0, Moved = 0, Retuned = 0, Added = 0, Freed = 0, Created = 0, Destroyed = 0;
    };

    void Assign(const FSettings& Settings, bool bRetuneAll);
    void Show(int32 Slot, const FVoxelLightPick& Pick, const FSettings& Settings, bool bRetuneAll);
    void Free(int32 Slot);
    UPointLightComponent* GetComponent(int32 Slot) const;
    UPointLightComponent* CreateComponent(const FSettings& Settings);
    void DestroyComponents();
    // The column's footprint lies within Radius of the camera (blocks) on the ground plane.
    static bool IsInReach(FIntPoint Column, const FVector& Camera, double Radius);

    TWeakObjectPtr<AActor> Owner;
    bool bHasOwner = false;
    TArray<TObjectPtr<UPointLightComponent>>& Components;
    FVector OriginCm = FVector::ZeroVector;

    TMap<FIntPoint, TArray<FVoxelLightSource>> Columns;
    int32 SourceCount = 0;
    // A column's sources within reach of the last pick changed.
    bool bDirty = false;

    bool bLive = false;
    bool bPicked = false;
    FSettings LastSettings;
    // The camera of the last pick (blocks).
    FVector LastCamera = FVector::ZeroVector;
    TArray<FVoxelLightSource> Gathered;
    TArray<FVoxelLightPick> Picks;
    TArray<FSlot> Slots;
    int32 GatheredCount = 0;

    FChanges LastChanges;
    int64 UpdateCount = 0;
    double LastMs = 0;
    double PeakMs = 0;
};
