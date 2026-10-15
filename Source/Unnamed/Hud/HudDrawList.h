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
    // The chat's layout in GUI pixels (minecraft.wiki "Options", chat settings: width 320, focused height 180 and
    // unfocused 90, 9 px per line; the bottom offset is provisional): rows above the hotbar, 10 while closed and 20
    // while open.
    constexpr int32 ChatWidth = 320, ChatLineHeight = 9, ChatClosedRows = 10, ChatOpenRows = 20, ChatBottomOffset = 40;
    // Each message word-wrapped to ChatWidth, oldest row first; a row keeps its runs' colours.
    UNNAMED_API TArray<TArray<FHudChatRun>> WrapChat(
        const TArray<FHudChatLine>& Lines, const TArray<FHudGlyph>& Glyphs);
    // The fire overlay's sprite (block/fire_1.png: 16 x 512, 32 frames of 16 x 16; its .mcmeta sets no frametime, so
    // one frame per tick).
    constexpr int32 FireFrames = 32;
    // The caret's blink: 300 ms on, 300 ms off [survival.r4.caret].
    constexpr int32 CaretBlinkMs = 300;
    // The death screen's buttons (step 11a) in GUI pixels of a LogicalSize screen, one source for drawing and clicks
    // [survival.r6.death_layout]: 200 x 20 at (width / 2 - 100, height / 4 + 72) (Respawn) and height / 4 + 96 (Title
    // Screen); 640 x 360 gives (220, 162) and (220, 186). None: an empty rectangle.
    UNNAMED_API FIntRect GetDeathButton(FIntPoint LogicalSize, EHudDeathButton Button);
    // The button under Point (GUI pixels), as vanilla's isMouseOver: left/top edges inside, right/bottom outside.
    UNNAMED_API EHudDeathButton HitDeathButton(FIntPoint LogicalSize, FIntPoint Point);
    UNNAMED_API TArray<FHudDraw> Build(const FHudSnapshot& Snapshot, const FHudPresentation& Presentation,
        const FHudLayout& Layout, int64 Tick, const TArray<FHudGlyph>& Glyphs);
}
