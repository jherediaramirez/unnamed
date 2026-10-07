#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SuperFlatGameMode.generated.h"

// Inspection only. Replace the stock spectator pawn when the FPS character is ready.
UCLASS()
class UNNAMED_API ASuperFlatGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ASuperFlatGameMode();
};
