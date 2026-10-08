#include "Hud/HudPreview.h"

FHudPreview::FHudPreview()
{
    Reset();
}
void FHudPreview::Reset()
{
    Values = FHudSnapshot{};
    Values.Resources = Values.Items = EHudDataSource::Preview;
    Values.Armor = 7;
    Values.Food = 17;
    Values.Level = 7;
    Values.Experience = .42;
    Values.AttackStrength = .6;
    Sequence = EHudPreviewSequence::None;
    SequenceTick = 0;
}
void FHudPreview::Start(EHudPreviewSequence Next)
{
    Reset();
    Sequence = Next;
    if (Next == EHudPreviewSequence::AbsorptionLoss) Values.Absorption = 5;
    if (Next == EHudPreviewSequence::AirCycle) Values.bUnderwater = true;
    if (Next == EHudPreviewSequence::AttackRecharge) Values.AttackStrength = 0;
}
void FHudPreview::Step()
{
    if (Sequence == EHudPreviewSequence::None) return;
    ++SequenceTick;
    switch (Sequence)
    {
    case EHudPreviewSequence::DamageHeal:
        Values.bDamageCooldown = true;
        if (SequenceTick == 10) Values.Health = 3;
        if (SequenceTick == 14) Values.Health = 9;
        if (SequenceTick == 45) Values.Health = 20;
        if (SequenceTick >= 70)
        {
            Values.bDamageCooldown = false;
            Sequence = EHudPreviewSequence::None;
        }
        break;
    case EHudPreviewSequence::AbsorptionLoss:
        if (SequenceTick == 15) Values.Absorption = 1;
        if (SequenceTick == 30)
        {
            Values.Absorption = 0;
            Sequence = EHudPreviewSequence::None;
        }
        break;
    case EHudPreviewSequence::AirCycle:
        // A recorded-value presentation sweep, including negative-air inputs. No drowning damage exists.
        Values.bUnderwater = SequenceTick <= 320;
        Values.Air = SequenceTick <= 320 ? 300 - int32(SequenceTick) : FMath::Min(300, int32(SequenceTick - 320) * 4);
        if (SequenceTick >= 395) Sequence = EHudPreviewSequence::None;
        break;
    case EHudPreviewSequence::ExperienceGain:
        Values.Experience = FMath::Fmod(.42 + SequenceTick * .02, 1.);
        Values.Level = 7 + FMath::FloorToInt(.42 + SequenceTick * .02);
        if (SequenceTick >= 60) Sequence = EHudPreviewSequence::None;
        break;
    case EHudPreviewSequence::AttackRecharge:
        Values.AttackStrength = FMath::Min(1., SequenceTick / 30.);
        Values.bLivingTarget = Values.bTargetAlive = Values.bTargetInRange = true;
        if (SequenceTick >= 50) Sequence = EHudPreviewSequence::None;
        break;
    default:
        break;
    }
}
