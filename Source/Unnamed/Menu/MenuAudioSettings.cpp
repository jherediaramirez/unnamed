#include "Menu/MenuAudioSettings.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioMixerDevice.h"
#include "Engine/Engine.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Menu/MenuBassShelfPreset.h"
#include "Menu/MenuPreferences.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    TAutoConsoleVariable<int32> CVarBass(TEXT("menu.Audio.Bass"), 1,
        TEXT("Bass (step 13a2, beyond vanilla): 1 the sound_bass preference's low shelf on the master submix (175 Hz, "
             "value x 5 dB); 0 never attaches it, vanilla's output whatever the slider says."),
        ECVF_Default);

    double ReadNumber(const UMenuPreferences* Preferences, const TCHAR* Id, double Default)
    {
        if (!Preferences) return Default;
        const TSharedPtr<const FJsonValue> Value = Preferences->FindPreference(Id);
        double Number = Default;
        return Value.IsValid() && Value->TryGetNumber(Number) ? Number : Default;
    }
} // namespace

void UMenuAudioSettings::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UMenuPreferences>();
    BassPreset = NewObject<UMenuBassShelfPreset>(this);
    MapLoadedHandle =
        FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMenuAudioSettings::HandleMapLoaded);
}

void UMenuAudioSettings::Deinitialize()
{
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(MapLoadedHandle);
    DetachBass();
    Super::Deinitialize();
}

ETickableTickType UMenuAudioSettings::GetTickableTickType() const
{
    return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

TStatId UMenuAudioSettings::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UMenuAudioSettings, STATGROUP_Tickables);
}

float UMenuAudioSettings::GetBassGainDb(double Preference, bool bSwitch)
{
    return bSwitch ? float(FMath::Clamp(Preference, 0., 1.)) * MenuBass::MaxGainDb : 0.f;
}

void UMenuAudioSettings::Tick(float DeltaTime)
{
    const UMenuPreferences* Preferences = GetGameInstance()->GetSubsystem<UMenuPreferences>();
    const int32 Revision = Preferences ? Preferences->GetRevision() : 0;
    UWorld* World = GetGameInstance()->GetWorld();
    const FAudioDevice* Device = World ? World->GetAudioDeviceRaw() : nullptr;
    const uint32 DeviceId = Device ? uint32(Device->DeviceID) : uint32(INDEX_NONE);
    // A new audio device (the first world, or a swap) gets the current values too.
    if (Revision != AppliedRevision || CVarBass.GetValueOnGameThread() != AppliedSwitch || DeviceId != AppliedDeviceId)
        Apply();
}

void UMenuAudioSettings::Apply()
{
    const UMenuPreferences* Preferences = GetGameInstance()->GetSubsystem<UMenuPreferences>();
    ++ApplyCount;
    AppliedRevision = Preferences ? Preferences->GetRevision() : 0;
    AppliedSwitch = CVarBass.GetValueOnGameThread();
    MasterVolume = float(FMath::Clamp(ReadNumber(Preferences, TEXT("sound_master"), 1.), 0., 1.));
    BassGainDb = GetBassGainDb(ReadNumber(Preferences, TEXT("sound_bass"), 1.), AppliedSwitch != 0);
    UWorld* World = GetGameInstance()->GetWorld();
    FAudioDevice* Device = World ? World->GetAudioDeviceRaw() : nullptr;
    AppliedDeviceId = Device ? uint32(Device->DeviceID) : uint32(INDEX_NONE);
    // No device for now (a world change): the shelf stays where it is.
    if (!Device) return;
    Device->SetTransientPrimaryVolume(MasterVolume);
    if (!BassPreset) return;
    if (bBassAttached && AttachedDeviceId != AppliedDeviceId) DetachBass();
    if (BassGainDb > 0)
    {
        FMenuBassShelfSettings Settings;
        Settings.GainDb = BassGainDb;
        BassPreset->UpdateSettings(Settings);
        if (!bBassAttached)
        {
            // Never two shelves: a removal first (nothing when absent), queued before the add.
            UAudioMixerBlueprintLibrary::RemoveMasterSubmixEffect(World, BassPreset);
            UAudioMixerBlueprintLibrary::AddMasterSubmixEffect(World, BassPreset);
            AttachedDeviceId = AppliedDeviceId;
            bBassAttached = true;
        }
    }
    else
        DetachBass();
}

void UMenuAudioSettings::HandleMapLoaded(UWorld* World)
{
    // Every map load, any world: Apply only touches this game instance's world's device.
    Apply();
}

void UMenuAudioSettings::DetachBass()
{
    if (bBassAttached && BassPreset && GEngine)
        if (FAudioDeviceManager* Manager = GEngine->GetAudioDeviceManager())
            if (FAudioDevice* Device = Manager->GetAudioDeviceRaw(Audio::FDeviceId(AttachedDeviceId)))
                static_cast<Audio::FMixerDevice*>(Device)->RemoveMasterSubmixEffect(BassPreset->GetUniqueID());
    bBassAttached = false;
    AttachedDeviceId = INDEX_NONE;
}
