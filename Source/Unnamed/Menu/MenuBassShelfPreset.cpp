#include "Menu/MenuBassShelfPreset.h"

void MenuBass::FShelf::Init(float SampleRate, int32 NumChannels, float NewGainDb)
{
    Rate = SampleRate;
    Channels = FMath::Max(1, NumChannels);
    GainDb = NewGainDb;
    // The bandwidth is unused by the engine's shelves (slope S = 1).
    Filter.Init(Rate, Channels, Audio::EBiquadFilter::LowShelf, CornerHz, 1.f, GainDb);
    Makeup = FMath::Pow(10.f, MakeupPerDb * GainDb / 20.f);
}

void MenuBass::FShelf::SetGainDb(float NewGainDb)
{
    if (NewGainDb == GainDb) return;
    GainDb = NewGainDb;
    Filter.SetParams(Audio::EBiquadFilter::LowShelf, CornerHz, 1.f, GainDb);
    Makeup = FMath::Pow(10.f, MakeupPerDb * GainDb / 20.f);
}

void MenuBass::FShelf::Process(const float* In, float* Out, int32 NumFrames)
{
    const int32 Samples = NumFrames * Channels;
    Filter.ProcessAudio(In, Samples, Out);
    for (int32 Index = 0; Index < Samples; ++Index)
        Out[Index] *= Makeup;
}

void FMenuBassShelf::Init(const FSoundEffectSubmixInitData& InitData)
{
    SampleRate = InitData.SampleRate > 0 ? InitData.SampleRate : 48000.f;
}

void FMenuBassShelf::OnPresetChanged()
{
    GET_EFFECT_SETTINGS(MenuBassShelf);
    PendingGainDb = Settings.GainDb;
}

void FMenuBassShelf::OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData)
{
    if (!InData.AudioBuffer || !OutData.AudioBuffer || InData.NumChannels <= 0) return;
    if (Shelf.GetNumChannels() != InData.NumChannels)
        Shelf.Init(SampleRate, InData.NumChannels, PendingGainDb);
    else
        Shelf.SetGainDb(PendingGainDb);
    const int32 NumFrames = FMath::Min(InData.NumFrames, InData.AudioBuffer->Num() / InData.NumChannels);
    Shelf.Process(InData.AudioBuffer->GetData(), OutData.AudioBuffer->GetData(), NumFrames);
    ++ProcessedBlocks;
}
