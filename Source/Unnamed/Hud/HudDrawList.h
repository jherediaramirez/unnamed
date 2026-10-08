#pragma once
#include "Hud/HudAssets.h"
#include "Hud/HudModel.h"

struct FHudDraw
{
    FName Sprite;
    FIntRect Rect;
    FBox2f UV = FBox2f(FVector2f(0), FVector2f(1));
    FLinearColor Color = FLinearColor::White;
    // When set, Rect is a 16 x 16 slot and the view paints this block's 3D item model at animation Tick.
    FName Block;
    int64 Tick = 0;
    bool operator==(const FHudDraw& Other) const
    {
        return Sprite == Other.Sprite && Rect == Other.Rect && UV == Other.UV && Color == Other.Color &&
            Block == Other.Block && Tick == Other.Tick;
    }
};

namespace HudDrawList
{
    UNNAMED_API int32 MeasureText(const FString& Text, const TArray<FHudGlyph>& Glyphs);
    UNNAMED_API TArray<FHudDraw> Build(const FHudSnapshot& Snapshot, const FHudPresentation& Presentation,
        const FHudLayout& Layout, int64 Tick, const TArray<FHudGlyph>& Glyphs);
}
