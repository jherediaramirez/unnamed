#pragma once
#include "Hud/HudModel.h"

enum class EHudPreviewSequence : uint8
{
    None,
    DamageHeal,
    AbsorptionLoss,
    AirCycle,
    ExperienceGain,
    AttackRecharge
};

// Deliberately isolated test data. Its scripts change only these values, never a pawn or gameplay system.
struct FHudPreview
{
    FHudPreview();
    FHudSnapshot Values;
    EHudPreviewSequence Sequence = EHudPreviewSequence::None;
    int64 SequenceTick = 0;
    void Reset();
    void Start(EHudPreviewSequence Next);
    void Step();
};
