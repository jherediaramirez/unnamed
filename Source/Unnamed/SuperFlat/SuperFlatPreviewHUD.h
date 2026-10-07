#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SuperFlatPreviewHUD.generated.h"

UCLASS()
class UNNAMED_API ASuperFlatPreviewHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
