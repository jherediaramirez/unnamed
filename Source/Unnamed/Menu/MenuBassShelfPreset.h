#pragma once

#include "CoreMinimal.h"
#include "DSP/Filter.h"
#include "Sound/SoundEffectSubmix.h"
#include "MenuBassShelfPreset.generated.h"

// The Bass setting (survival step 13a2; beyond vanilla by Jesus's choice, 2026-10-15: "increase the bass of all the
// sounds ... same sounds"): one low shelf on the master submix, after every sound. Provisional values: corner 175 Hz,
// the engine's shelf slope (S = 1), gain = the sound_bass preference x 5 dB, and a makeup of -0.3 x gain against
// clipping. UMenuAudioSettings adds it to the master submix while the gain is above 0 and removes it at 0, so a 0
// slider is vanilla's output exactly.
namespace MenuBass
{
    constexpr float CornerHz = 175.f;
    constexpr float MaxGainDb = 5.f;
    constexpr float MakeupPerDb = -.3f;

    // The shelf itself, plain so tests can render through it: interleaved frames, one filter state per channel.
    class UNNAMED_API FShelf
    {
    public:
        void Init(float SampleRate, int32 NumChannels, float GainDb);
        void SetGainDb(float GainDb);
        // Out may be In.
        void Process(const float* In, float* Out, int32 NumFrames);
        float GetGainDb() const { return GainDb; }
        float GetMakeup() const { return Makeup; }
        int32 GetNumChannels() const { return Channels; }

    private:
        Audio::FBiquadFilter Filter;
        float GainDb = 0;
        float Makeup = 1;
        float Rate = 48000;
        int32 Channels = 0;
    };
} // namespace MenuBass

USTRUCT()
struct UNNAMED_API FMenuBassShelfSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category = "Bass")
    float GainDb = MenuBass::MaxGainDb;
};

class UNNAMED_API FMenuBassShelf : public FSoundEffectSubmix
{
public:
    virtual void Init(const FSoundEffectSubmixInitData& InitData) override;
    virtual void OnPresetChanged() override;
    virtual void OnProcessAudio(
        const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData) override;
    // Tests: how many blocks went through the shelf.
    int32 GetProcessedBlocks() const { return ProcessedBlocks; }

private:
    MenuBass::FShelf Shelf;
    float SampleRate = 48000;
    float PendingGainDb = MenuBass::MaxGainDb;
    int32 ProcessedBlocks = 0;
};

UCLASS()
class UNNAMED_API UMenuBassShelfPreset : public USoundEffectSubmixPreset
{
    GENERATED_BODY()
public:
    EFFECT_PRESET_METHODS(MenuBassShelf)

    UPROPERTY(EditAnywhere, Category = "Bass")
    FMenuBassShelfSettings Settings;
};
