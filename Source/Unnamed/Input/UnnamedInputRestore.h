#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UnnamedInputRestore.generated.h"

// Puts the input settings' backup back at startup, before the first local player exists: Enhanced Input reads the
// save in its LocalPlayer subsystem's Initialize, and a missing or empty slot would give every binding its default.
UCLASS()
class UNNAMED_API UUnnamedInputRestore : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
};
