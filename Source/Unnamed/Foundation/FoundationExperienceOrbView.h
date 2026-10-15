#pragma once

#include "CoreMinimal.h"
// FoundationAuthority.h needs it first.
#include "Containers/StaticArray.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Foundation/FoundationAuthority.h"

class AFoundationPawn;
class UFoundationInteractionView;
class UInstancedStaticMeshComponent;

// The experience orbs' look (survival step 12): presentation only, like the view that owns it. Each orb within DrawCm
// of the player is a camera-facing 0.3-block quad of its icon from T_ExperienceOrb (M_ExperienceOrb, under EntityRoot),
// tinted by the colour pulse and drawn between the last two reference steps (value report `xp.value`,
// `xp.value_icons`); a frozen orb is hidden. The sound hooks: entity.experience_orb.pickup on a pickup, and
// entity.player.levelup by FoundationExperienceOrbs::GetLevelUpVolume's rule (the local player's level reaching a
// multiple of 5, at most once per 100 steps).
class FFoundationExperienceOrbView
{
public:
    explicit FFoundationExperienceOrbView(UFoundationInteractionView& InView) : View(InView) {}

    // Draws this frame's orbs; Alpha is the fraction of a reference step since the last one.
    void Update(const FFoundationAuthority& Authority, const AFoundationPawn* Pawn, double Alpha);
    // An ExperiencePickedUp event (the view's HandleEvents): the pickup sound hook.
    void OnPickedUp(const FFoundationEvent& Event);
    // Probes: orbs drawn this frame, the icons and colours drawn (custom data 0-2 per orb), and the sound hook calls.
    int32 GetDrawnCount() const { return Drawn; }
    const TArray<float>& GetDrawnData() const { return Data; }
    int32 PickupSounds = 0, LevelUpSounds = 0;
    // The quads' component state (probes): registered, visible, instances, bounds, material, the first instance.
    FString Describe() const;

    // Provisional, as the item entities: orbs farther than this are not drawn; at most MaxOrbs, nearest first.
    static constexpr double DrawCm = 6400;
    static constexpr int32 MaxOrbs = 256;
    // Custom data floats per orb: the icon, the pulse's sRGB red and blue.
    static constexpr int32 DataFloats = 3;

    // The quads' component and material, made once at BeginPlay (the view's, as the break effects'); false without
    // M_ExperienceOrb.
    bool Prepare();

private:
    UFoundationInteractionView& View;
    // Every quad is made up front at zero scale and reused by id, as the break particles are.
    UInstancedStaticMeshComponent* Quads = nullptr;
    TArray<FPrimitiveInstanceId> Ids;
    int32 Shown = 0;
    bool bTried = false;
    int32 Drawn = 0;
    int32 LastLevel = -1;
    // The local player's step when the last level-up sound played (-1: none yet).
    int64 LastLevelUpStep = -1;
    // The last level gain judged (FFoundationLevelGain::Serial).
    int32 LastGainSerial = 0;
    TArray<float> Data;
};
