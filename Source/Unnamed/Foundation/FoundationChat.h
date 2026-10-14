#pragma once

#include "CoreMinimal.h"
#include "Hud/HudModel.h"

// The chat's message list (step 15): command feedback, errors and plain chat, newest last. Display data only; the
// HUD draws what GetVisible returns. Times are FPlatformTime seconds, as UFoundationHud's frame clock.
class UNNAMED_API FFoundationChat
{
public:
    // minecraft.wiki "Chat": the history keeps 100 messages; a message shows for 10 s while the chat is closed.
    // The fade over its last second is provisional (linear; the exact curve is client code only).
    static constexpr int32 MaxMessages = 100;
    static constexpr double VisibleSeconds = 10;
    static constexpr double FadeSeconds = 1;

    void Add(TArray<FHudChatRun> Runs, double Now);
    void AddText(const FString& Text, FLinearColor Color, double Now) { Add({{Text, Color}}, Now); }
    void Clear()
    {
        Messages.Reset();
        ++Revision;
    }
    int32 Num() const { return Messages.Num(); }
    // Bumps on every Add and Clear, so the HUD rebuilds its lines only when they change or fade.
    int32 GetRevision() const { return Revision; }
    // Whether a closed chat still shows a message at Now (the newest is younger than 10 s).
    bool HasRecent(double Now) const { return !Messages.IsEmpty() && Now - Messages.Last().Time < VisibleSeconds; }
    // The text of message Index (oldest first), runs joined.
    FString GetText(int32 Index) const;

    // Closed: the messages younger than 10 s with their fade. Open: every message at full opacity. Oldest first; the
    // draw list keeps the newest rows that fit.
    TArray<FHudChatLine> GetVisible(double Now, bool bOpen) const;
    // 1 until the last second, then linear to 0 at 10 s; quantized to 1/20 so a closed chat redraws per HUD tick.
    static float GetAlpha(double Age);

private:
    struct FMessage
    {
        TArray<FHudChatRun> Runs;
        double Time = 0;
    };
    TArray<FMessage> Messages;
    int32 Revision = 0;
};

namespace FoundationChat
{
    // foundation.Chat.Commands: 0 records chat text locally as before (no chat list, no commands); 1 runs commands
    // and shows the chat list.
    UNNAMED_API bool AreCommandsEnabled();
    // Vanilla's chat colours (minecraft.wiki "Formatting codes"): white, red (errors), gray (error context).
    UNNAMED_API FLinearColor White();
    UNNAMED_API FLinearColor Red();
    UNNAMED_API FLinearColor Gray();
}
