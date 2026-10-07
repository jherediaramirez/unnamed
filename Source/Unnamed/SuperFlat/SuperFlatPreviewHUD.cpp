#include "SuperFlat/SuperFlatPreviewHUD.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

void ASuperFlatPreviewHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !GEngine) return;
    DrawRect(FLinearColor(0.015f, 0.022f, 0.028f, 0.8f), 16, 16, 550, 64);
    DrawText(
        TEXT("SUPER FLAT  |  2 km x 2 km  |  Grid: 1 m / 10 m"), FLinearColor::White, 28, 24, GEngine->GetSmallFont());
    DrawText(TEXT("Preview camera: WASD + mouse, E/Q up/down  |  Esc: Menu"), FLinearColor(0.75f, 0.85f, 0.9f), 28, 47,
        GEngine->GetSmallFont());
}
