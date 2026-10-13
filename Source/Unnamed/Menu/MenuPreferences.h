#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MenuPreferences.generated.h"

class FJsonObject;
class FJsonValue;

// Desired client preferences survive menu/controller changes. Feature effects have
// their own owners; this subsystem never changes engine, input or world state.
UCLASS()
class UNNAMED_API UMenuPreferences : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    UFUNCTION()
    FString GetState();
    UFUNCTION()
    FString ApplyChanges(const FString& ChangesJson);
    UFUNCTION()
    FString ResetPreferences();
    bool FlushForExit(FString& OutError);

    // Native feature adapters can read their desired value when implemented.
    TSharedPtr<const FJsonValue> FindPreference(const FString& Id) const;
    // Bumped by every change: the kept title page (menu.Title.Keep) repaints after one made in a world.
    int32 GetRevision() const { return Revision; }

#if WITH_DEV_AUTOMATION_TESTS
    void InitializeForTesting(const FString& Path, bool bSave);
#endif
private:
    void Load(const FString& Path, bool bSave);
    TSharedPtr<FJsonObject> Defaults() const;
    bool ApplyPatch(const TSharedPtr<FJsonObject>& Patch, TSharedPtr<FJsonObject>& Target, bool bLoading);
    bool ValidateSetting(const FString& Id, const TSharedPtr<FJsonValue>& Value) const;
    bool Save(const TSharedPtr<FJsonObject>& Value);
    FString Snapshot(bool bSuccess = true) const;

    TSharedPtr<FJsonObject> Schema;
    TSharedPtr<FJsonObject> State;
    FString Filename;
    FString LastError;
    bool bPersistent = false;
    bool bReady = false;
    bool bProtectedFile = false;
    int32 Revision = 0;
};
