#include "Foundation/FoundationChat.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarChatCommands(TEXT("foundation.Chat.Commands"), 1,
        TEXT("Chat commands (survival step 15): 1 runs /gamemode, /give, /tp, /time and /kill typed in the chat and "
             "shows the chat lines on the HUD; 0 records chat text locally only, as before."),
        ECVF_Default);
}

bool FoundationChat::AreCommandsEnabled()
{
    return CVarChatCommands.GetValueOnAnyThread() != 0;
}

FLinearColor FoundationChat::White()
{
    return FLinearColor::White;
}

FLinearColor FoundationChat::Red()
{
    return FLinearColor(FColor(0xFF, 0x55, 0x55));
}

FLinearColor FoundationChat::Gray()
{
    return FLinearColor(FColor(0xAA, 0xAA, 0xAA));
}

void FFoundationChat::Add(TArray<FHudChatRun> Runs, double Now)
{
    Messages.Add({MoveTemp(Runs), Now});
    ++Revision;
    if (Messages.Num() > MaxMessages) Messages.RemoveAt(0, Messages.Num() - MaxMessages);
}

FString FFoundationChat::GetText(int32 Index) const
{
    FString Text;
    if (Messages.IsValidIndex(Index))
        for (const FHudChatRun& Run : Messages[Index].Runs)
            Text += Run.Text;
    return Text;
}

float FFoundationChat::GetAlpha(double Age)
{
    // [survival.r4.fade] t in whole ticks; 1 up to 180, then ((200 - t) / 20)^2 down to 0 at 200.
    const double Ticks = FMath::FloorToDouble(Age * 20. + 1e-6);
    if (Ticks <= FullTicks) return 1.f;
    const double Fade = FMath::Clamp((VisibleTicks - Ticks) / (VisibleTicks - FullTicks), 0., 1.);
    return float(Fade * Fade);
}

TArray<FHudChatLine> FFoundationChat::GetVisible(double Now, bool bOpen) const
{
    TArray<FHudChatLine> Lines;
    for (const FMessage& Message : Messages)
    {
        const float Alpha = bOpen ? 1.f : GetAlpha(Now - Message.Time);
        if (Alpha > 0) Lines.Add({Message.Runs, Alpha});
    }
    return Lines;
}
