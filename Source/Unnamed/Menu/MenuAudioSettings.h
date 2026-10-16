#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "MenuAudioSettings.generated.h"

class UMenuBassShelfPreset;

// Applies the Music & Sounds preferences to the game's audio (survival step 13a2): Master Volume (sound_master) as the
// audio device's primary volume, and Bass (sound_bass, beyond vanilla) as a low shelf on the master submix
// (MenuBassShelfPreset.h). It re-reads them whenever UMenuPreferences changes. The other category sliders are saved
// but not applied yet. Without an audio device (NullRHI tests) it only keeps the desired values.
UCLASS()
class UNNAMED_API UMenuAudioSettings : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    virtual void Tick(float DeltaTime) override;
    virtual ETickableTickType GetTickableTickType() const override;
    virtual bool IsTickable() const override { return !IsTemplate(); }
    virtual TStatId GetStatId() const override;

    // The shelf's gain for a sound_bass value (0-1) with menu.Audio.Bass on (Switch true): value x 5 dB.
    static float GetBassGainDb(double Preference, bool bSwitch);
    // Desired and applied state, for tests and probes.
    float GetMasterVolume() const { return MasterVolume; }
    float GetBassGainDb() const { return BassGainDb; }
    bool IsBassAttached() const { return bBassAttached; }
    // How many times Apply ran (tests and probes).
    int32 GetApplyCount() const { return ApplyCount; }
    // Re-reads the preferences and applies them now.
    void Apply();
    // Bound to every map load: the engine resets the transient primary volume to 1 as it loads a map
    // (UnrealEngine.cpp LoadMap), so the saved Master Volume goes back on.
    void HandleMapLoaded(UWorld* World);
    // Gives the preset GainDb for every instance: the live ones (UpdateSettings) and the first one, which the engine
    // initialises from the preset's Settings property (USoundEffectPreset::AddEffectInstance).
    static void SetBassPresetGain(UMenuBassShelfPreset& Preset, float GainDb);

private:
    // Removes the shelf from the device it was added to (by id, so it works without a world).
    void DetachBass();

    UPROPERTY(Transient)
    TObjectPtr<UMenuBassShelfPreset> BassPreset;
    int32 AppliedRevision = -1;
    int32 AppliedSwitch = -1;
    int32 ApplyCount = 0;
    FDelegateHandle MapLoadedHandle;
    float MasterVolume = 1;
    float BassGainDb = 0;
    bool bBassAttached = false;
    // The device Apply last saw (INDEX_NONE: none) and the one the shelf is on.
    uint32 AppliedDeviceId = INDEX_NONE;
    uint32 AttachedDeviceId = INDEX_NONE;
};
