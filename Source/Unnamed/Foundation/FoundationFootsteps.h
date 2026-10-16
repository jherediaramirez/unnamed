#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockRegistry.h"

struct FFoundationBlockWorld;
class UFoundationBreakAssets;
class UFoundationSoundAssets;

// The player's footsteps (survival step 13a2): the block underfoot's step sound on the walked-distance cadence. The
// pawn advances the cadence once per reference step; the interaction view plays what it counted (PlayFootsteps).
namespace FoundationFootsteps
{
    // foundation.Player.Footsteps: 1 (default) plays them; 0 is before step 13a2 (the player makes no step sound).
    UNNAMED_API bool IsOn();

    // Provisional (minecraft.wiki "Sound"; R24 requested): the walked distance grows by the horizontal move x 0.6, in
    // blocks, and a step is due once it passes the next whole number (strictly), which then becomes floor(distance)
    // + 1.
    constexpr double DistanceScale = 0.6;
    // [survival.r5.play_calls] block.stone.step plays at 0.15 and 1 (F 3563): the sound type's volume x 0.15, its pitch
    // x 1.
    constexpr float VolumeScale = 0.15f;
    constexpr float PitchScale = 1.f;
    // Provisional (wiki): on a carpet or snow layer (#combination_step_sound_blocks) the block below also plays, at a
    // third of the volume and pitch x 0.8.
    constexpr float UnderVolumeScale = VolumeScale / 3;
    constexpr float UnderPitchScale = .8f;
    // The block stepped on is 0.2 blocks below the feet (provisional, wiki).
    constexpr double SupportDepthCm = 20;

    struct FState
    {
        double Distance = 0;
        double NextStep = 1;
    };

    struct FFacts
    {
        double HorizontalCm = 0;
        bool bGrounded = false;
        // The sneak pose: due steps pass silently, so leaving it plays no backlog (wiki "Sneaking").
        bool bSneaking = false;
        // Creative flight walks no distance.
        bool bFlying = false;
        // Swimming plays the swim sounds instead (not built): due steps pass silently.
        bool bInWater = false;
    };

    enum class EStep : uint8
    {
        None,
        Silent,
        Play
    };

    // One reference step. A due step waits for the ground (a jump lands on it).
    UNNAMED_API EStep Advance(FState& State, const FFacts& Facts);

    // What a step at FeetCm stands on: a #inside_step_sound_blocks block the feet are in (a lily pad: it alone plays);
    // else the cell 0.2 blocks below the feet, or under it a fence, wall or fence gate when that cell is air.
    // bCombination: the cell above the support is a carpet or snow layer, which plays first (Primary), the support
    // second (Under).
    struct FStepBlocks
    {
        FFoundationBlockState Primary;
        FFoundationBlockState Under;
        bool bCombination = false;
    };
    UNNAMED_API bool FindStepBlocks(const FFoundationBlockWorld& World, const FVector& FeetCm, FStepBlocks& Out);

    // A block's step sound: DA_Sounds' step event for the block at Index (DA_BreakAssets' order) at its sound type's
    // volume and pitch (DA_BreakAssets) times the scales above (the Under scales when bUnder). False when it is silent.
    struct FStepSound
    {
        FName Event;
        float Volume = 0;
        float Pitch = 0;
    };
    UNNAMED_API bool GetStepSound(const UFoundationBreakAssets& Break, const UFoundationSoundAssets& Sounds,
        int32 Index, bool bUnder, FStepSound& Out);
} // namespace FoundationFootsteps
