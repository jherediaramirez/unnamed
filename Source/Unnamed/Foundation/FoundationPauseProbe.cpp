#include "Foundation/FoundationRuntimeProbe.h"
#include "UnnamedLog.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationPanels.h"
#include "Menu/MenuVideoSettings.h"
#include "Foundation/FoundationPawn.h"
#include "Menu/MenuBrowserHost.h"
#include "Menu/MenuInputBridge.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SWebBrowser.h"
#include "UnrealClient.h"
#include <atomic>

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    // While the pause suite runs: counts LogWebBrowser errors and warnings (CEF's shared-texture failures) and stamps
    // the page's first log line ("offline menu ready", the generated script running).
    class FPauseLogWatch final : public FOutputDevice
    {
    public:
        FPauseLogWatch() { GLog->AddOutputDevice(this); }
        virtual ~FPauseLogWatch() override
        {
            if (GLog) GLog->RemoveOutputDevice(this);
        }
        virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
        {
            static const FName WebBrowser(TEXT("LogWebBrowser")), Menu(TEXT("LogUnnamedMenu"));
            const int32 Level = Verbosity & ELogVerbosity::VerbosityMask;
            if (Category == WebBrowser && Level <= ELogVerbosity::Error)
                ++WebErrors;
            else if (Category == WebBrowser && Level == ELogVerbosity::Warning)
                ++WebWarnings;
            else if (Category == Menu && FCString::Strstr(Text, TEXT("offline menu ready")))
            {
                ScriptSeconds = FPlatformTime::Seconds();
                ++ScriptCount;
            }
        }
        virtual bool CanBeUsedOnAnyThread() const override { return true; }

        std::atomic<int32> WebErrors{0}, WebWarnings{0}, ScriptCount{0};
        std::atomic<double> ScriptSeconds{0};
    };

    // One CSV row per recorded frame.
    struct FPauseFrame
    {
        FString Seq;
        int32 Index = 0;
        uint64 EngineFrame = 0;
        double Seconds = 0;
        bool bShown = false, bExists = false, bLoaded = false, bScript = false;
        // The page's reports (modes 1 and 2): its pause screen painted; shown since the last Show.
        bool bReady = false, bPageShown = false;
        int32 WebErrors = 0, WebWarnings = 0;
    };

    struct FPauseRun
    {
        TSharedPtr<FPauseLogWatch> Log = MakeShared<FPauseLogWatch>();
        TArray<FPauseFrame> Frames;
        // Per sequence: the press time and the page-script count at the press.
        TMap<FString, double> PressSeconds;
        TMap<FString, int32> PressScripts;
        // Log-only presses: ms from the press to the page script, the document loaded, the page's ready report and
        // its shown report (-1 = never).
        TArray<double> ScriptMs, LoadedMs, ReadyMs, ShownMs;
        int32 PressWebErrors = 0;
    };

    double PauseMedian(TArray<double> Values)
    {
        Values.RemoveAll([](double Value) { return Value < 0; });
        if (Values.IsEmpty()) return -1;
        Values.Sort();
        const int32 Mid = Values.Num() / 2;
        return Values.Num() % 2 ? Values[Mid] : (Values[Mid - 1] + Values[Mid]) / 2;
    }
}
#endif

// -FoundationSuite=pause: what the screen shows around ESC. A fixed bright Playground view with the HUD on, then
// sequence A (the first ESC after load, frames -5..+45), B (Back to Game's native path, -2..+10) and C (a second ESC
// 2 s later, -5..+45). Every recorded tick requests a screenshot (<report>-pause-<seq>-f<+/-NNN>.png) and adds a row to
// <report>-pause-frames.csv: the engine frame, ms since the press, the menu shown/created, SWebBrowser::IsLoaded, the
// page script having run since the press, the running LogWebBrowser error/warning counts, and (foundation.Pause.Menu 1
// and 2) the page's ready and shown reports. Screenshots slow every
// frame, so timings are read in ms. -PauseFramesNoShots records five presses without screenshots instead
// (<report>-pause-timing.csv; medians in the report's "interaction" object).
void UFoundationRuntimeProbe::AddPauseCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    const bool bShots = !FParse::Param(FCommandLine::Get(), TEXT("PauseFramesNoShots"));
    const FString Base = FPaths::ChangeExtension(Output, TEXT(""));
    TSharedRef<FPauseRun> Run = MakeShared<FPauseRun>();
    UMenuBrowserHost* Host = Controller->GetMenuHost();
    // foundation.Pause.Menu as this world read it: 0 has no page reports, so "painted" is the document loaded.
    const int32 Mode = Host->GetPauseMode();

    auto Sample = [Run, Host](const FString& Seq, int32 Index)
    {
        FPauseFrame Frame;
        Frame.Seq = Seq;
        Frame.Index = Index;
        Frame.EngineFrame = GFrameCounter;
        Frame.Seconds = FPlatformTime::Seconds();
        const TSharedPtr<SWebBrowser> Browser = Host->GetBrowser();
        Frame.bShown = Host->IsOpen();
        Frame.bExists = Browser.IsValid();
        Frame.bLoaded = Browser.IsValid() && Browser->IsLoaded();
        const int32* AtPress = Run->PressScripts.Find(Seq);
        Frame.bScript = AtPress && Run->Log->ScriptCount > *AtPress;
        Frame.bReady = Host->IsPageReady();
        Frame.bPageShown = Host->GetPageShownSeconds() > 0;
        Frame.WebErrors = Run->Log->WebErrors;
        Frame.WebWarnings = Run->Log->WebWarnings;
        Run->Frames.Add(Frame);
        return Frame;
    };
    auto Press = [Run](const FString& Seq)
    {
        Run->PressSeconds.Add(Seq, FPlatformTime::Seconds());
        Run->PressScripts.Add(Seq, Run->Log->ScriptCount);
    };
    auto Escape = [this]
    {
        PressKey(EKeys::Escape, true);
        PressKey(EKeys::Escape, false);
    };
    // The same bright daytime view every time: the dev grid at eye height, HUD on.
    auto View = [this]
    {
        ResetPlayer({-250, -350, 0});
        Controller->SetControlRotation((FVector(250, 0, 0) - Pawn->GetEyePosition()).Rotation());
        Controller->GetHud()->bHideHud = Controller->GetHud()->bOverlay = Controller->GetHud()->bShowCollision = false;
    };

    // The log-only mode still leaves one picture of the view (the runner requires a capture).
    AddCase(TEXT("pause settle"), 3., View,
        [bShots, Base]
        {
            if (!bShots) FScreenshotRequest::RequestScreenshot(Base + TEXT("-pause-timing-view.png"), true, false);
        });
    if (bShots)
    {
        // One recorded sequence: Act runs at frame 0, before that frame's row and screenshot.
        auto Sequence = [this, Run, Base, Sample, Press](
                            const FString& Seq, int32 First, int32 Last, TFunction<void()> Act)
        {
            AddCase(
                TEXT("pause frames ") + Seq, 60.,
                [this, Base, Sample, Press, Seq, First, Last, Act]
                {
                    TSharedRef<int32> Next = MakeShared<int32>(First);
                    FrameHook = [this, Base, Sample, Press, Seq, Last, Act, Next]
                    {
                        const int32 Index = (*Next)++;
                        // The case ends a tick after the last frame, so the next case's start never shows in it.
                        if (Index > Last)
                        {
                            Cases[Phase].Duration = 0;
                            return;
                        }
                        if (Index == 0)
                        {
                            Press(Seq);
                            Act();
                        }
                        Sample(Seq, Index);
                        FScreenshotRequest::RequestScreenshot(Base +
                                FString::Printf(TEXT("-pause-%s-f%s%03d.png"), *Seq, Index < 0 ? TEXT("-") : TEXT("+"),
                                    FMath::Abs(Index)),
                            true, false);
                    };
                },
                [this, Run, Seq, First, Last]
                {
                    const int32 Recorded =
                        Run->Frames.FilterByPredicate([&Seq](const FPauseFrame& Frame) { return Frame.Seq == Seq; })
                            .Num();
                    Record(TEXT("pause frames ") + Seq + TEXT(" recorded"), Recorded == Last - First + 1,
                        FString::Printf(TEXT("%d frames"), Recorded));
                });
        };
        Sequence(TEXT("A"), -5, 45, Escape);
        AddCase(
            TEXT("pause A open"), .5, [] {},
            [this, Host] { Record(TEXT("pause A: menu open"), Host->IsOpen(), TEXT("ESC opened the pause menu")); });
        Sequence(TEXT("B"), -2, 10, [this] { Controller->ResumeFoundation(); });
        AddCase(TEXT("pause gap"), 2., View, [] {});
        Sequence(TEXT("C"), -5, 45, Escape);
        AddCase(TEXT("pause C resume"), .5, [this] { Controller->ResumeFoundation(); }, [] {});
    }
    else
    {
        // Log-only: five presses, each until the page is shown (mode 0: its script ran and the document loaded; or
        // 5 s), then Back to Game and 2 s to settle.
        for (int32 Round = 0; Round < 5; ++Round)
        {
            const FString Seq = FString::Printf(TEXT("T%d"), Round + 1);
            AddCase(
                TEXT("pause timing ") + Seq, 6.,
                [this, Run, Sample, Press, Escape, Seq, Mode]
                {
                    TSharedRef<int32> Next = MakeShared<int32>(0);
                    FrameHook = [this, Run, Sample, Press, Escape, Seq, Next, Mode]
                    {
                        const int32 Index = (*Next)++;
                        if (Index == 0)
                        {
                            Press(Seq);
                            Escape();
                        }
                        const FPauseFrame Frame = Sample(Seq, Index);
                        if (Mode == 0 ? Frame.bScript && Frame.bLoaded : Frame.bPageShown) Cases[Phase].Duration = 0;
                    };
                },
                [this, Run, Seq, Host, Mode]
                {
                    const double Pressed = Run->PressSeconds.FindRef(Seq);
                    double Script = -1, Loaded = -1;
                    for (const FPauseFrame& Frame : Run->Frames)
                    {
                        if (Frame.Seq != Seq) continue;
                        if (Script < 0 && Frame.bScript) Script = (Run->Log->ScriptSeconds - Pressed) * 1000;
                        if (Loaded < 0 && Frame.bLoaded) Loaded = (Frame.Seconds - Pressed) * 1000;
                    }
                    // A kept-alive page was ready before the press (its ready time is the pre-warm's).
                    const double Ready =
                        Host->GetPageReadySeconds() >= Pressed ? (Host->GetPageReadySeconds() - Pressed) * 1000 : -1;
                    const double Shown =
                        Host->GetPageShownSeconds() > 0 ? (Host->GetPageShownSeconds() - Pressed) * 1000 : -1;
                    Run->ScriptMs.Add(Script);
                    Run->LoadedMs.Add(Loaded);
                    Run->ReadyMs.Add(Ready);
                    Run->ShownMs.Add(Shown);
                    Record(TEXT("pause timing ") + Seq, Mode == 0 ? Script >= 0 && Loaded >= 0 : Shown >= 0,
                        FString::Printf(
                            TEXT("mode %d: page script %.1f ms, document loaded %.1f ms, page ready %.1f ms, "
                                 "page shown %.1f ms after the press (-1 = none)"),
                            Mode, Script, Loaded, Ready, Shown));
                });
            AddCase(
                TEXT("pause timing resume ") + Seq, 2.,
                [this, View]
                {
                    Controller->ResumeFoundation();
                    View();
                },
                [] {});
        }
    }
    AddCase(
        TEXT("pause report"), .1, [] {},
        [this, Run, Base, Mode]
        {
            FString Csv =
                TEXT("seq,frame,engine_frame,ms,shown,exists,loaded,script,web_errors,web_warnings,mode,ready,"
                     "page_shown\n");
            for (const FPauseFrame& Frame : Run->Frames)
            {
                const double* Pressed = Run->PressSeconds.Find(Frame.Seq);
                Csv += FString::Printf(TEXT("%s,%d,%llu,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d\n"), *Frame.Seq, Frame.Index,
                    Frame.EngineFrame,
                    Pressed ? *FString::Printf(TEXT("%.2f"), (Frame.Seconds - *Pressed) * 1000) : TEXT(""),
                    int32(Frame.bShown), int32(Frame.bExists), int32(Frame.bLoaded), int32(Frame.bScript),
                    Frame.WebErrors, Frame.WebWarnings, Mode, int32(Frame.bReady), int32(Frame.bPageShown));
            }
            const bool bTiming = !Run->ScriptMs.IsEmpty();
            FFileHelper::SaveStringToFile(
                Csv, *(Base + (bTiming ? TEXT("-pause-timing.csv") : TEXT("-pause-frames.csv"))));
            if (!Extra) Extra = MakeShared<FJsonObject>();
            auto Pause = MakeShared<FJsonObject>();
            Pause->SetNumberField(TEXT("web_browser_errors"), Run->Log->WebErrors);
            Pause->SetNumberField(TEXT("web_browser_warnings"), Run->Log->WebWarnings);
            Pause->SetNumberField(TEXT("mode"), Mode);
            if (bTiming)
            {
                Pause->SetNumberField(TEXT("median_ms_to_page_script"), PauseMedian(Run->ScriptMs));
                Pause->SetNumberField(TEXT("median_ms_to_document_loaded"), PauseMedian(Run->LoadedMs));
                Pause->SetNumberField(TEXT("median_ms_to_page_ready"), PauseMedian(Run->ReadyMs));
                Pause->SetNumberField(TEXT("median_ms_to_page_shown"), PauseMedian(Run->ShownMs));
            }
            Extra->SetObjectField(TEXT("pause"), Pause);
            UE_LOG(LogUnnamedFoundation, Display, TEXT("PAUSE PROBE: %d frames, %d LogWebBrowser errors, %d warnings"),
                Run->Frames.Num(), Run->Log->WebErrors.load(), Run->Log->WebWarnings.load());
        });
#endif
}

// The pause menu's life in this world's foundation.Pause.Menu mode (all and pause suites). Mode 1: the pre-warmed
// browser is ready and collapsed; ESC shows it and the page reports shown within 100 ms; Back to Game collapses it and
// keeps it; the second ESC shows the same browser, and the page shows a key binding changed natively while it was
// hidden (the input store never polls; F11 cannot change the window in these offscreen runs; the GUI scale is reported
// too, but the video store's own poll may catch that up on its own). Modes 0 and 2: ESC makes a browser and Back to
// Game destroys it.
void UFoundationRuntimeProbe::AddPauseMenuCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    UMenuBrowserHost* Host = Controller->GetMenuHost();
    const int32 Mode = Host->GetPauseMode();
    TSharedRef<SWebBrowser*> Opened = MakeShared<SWebBrowser*>(nullptr);
    auto Escape = [this]
    {
        PressKey(EKeys::Escape, true);
        PressKey(EKeys::Escape, false);
    };
    // Holds a case until the page reports shown (modes 1 and 2) or its 3 s pass.
    auto WaitShown = [this, Host, Mode]
    {
        FrameHook = [this, Host, Mode]
        {
            if (Mode != 0 && Host->GetPageShownSeconds() > 0) Cases[Phase].Duration = 0;
        };
    };
    auto ShownMs = [Host] { return (Host->GetPageShownSeconds() - Host->GetStartSeconds()) * 1000; };
    AddCase(
        TEXT("pause menu gameplay"), Mode == 1 ? 10. : .3,
        [this, Host, Mode]
        {
            if (Controller->GetPanels()->IsOpen()) Controller->GetPanels()->Close();
            Controller->ResumeFoundation();
            FrameHook = [this, Host, Mode]
            {
                if (Mode != 1 || Host->IsPageReady()) Cases[Phase].Duration = 0;
            };
        },
        [this, Host, Mode]
        {
            const TSharedPtr<SWebBrowser> Browser = Host->GetBrowser();
            if (Mode == 1)
                Record(TEXT("pause menu pre-warmed"),
                    Browser.IsValid() && Host->IsPageReady() && !Host->IsOpen() &&
                        Browser->GetVisibility() == EVisibility::Collapsed && Controller->IsGameplay(),
                    TEXT("page ready (ms: the \"Pause menu ready\" log line); browser collapsed while playing"));
            else
                Record(TEXT("pause menu: no browser while playing"), !Browser.IsValid() && Controller->IsGameplay(),
                    FString::Printf(TEXT("mode %d"), Mode));
        });
    AddCase(
        TEXT("pause menu open"), Mode == 0 ? .5 : 3.,
        [Escape, WaitShown]
        {
            Escape();
            WaitShown();
        },
        [this, Host, Mode, Opened, ShownMs]
        {
            *Opened = Host->GetBrowser().Get();
            Record(TEXT("ESC shows the pause menu"), Host->IsOpen() && Host->HasBrowser() && !Controller->IsGameplay(),
                FString::Printf(TEXT("mode %d"), Mode));
            if (Mode == 1)
                Record(TEXT("pause menu shown within 100 ms of ESC"),
                    Host->GetPageShownSeconds() > 0 && ShownMs() <= 100, FString::Printf(TEXT("%.1f ms"), ShownMs()));
            if (Mode == 2)
                Record(TEXT("fresh pause page shows itself once painted"), Host->GetPageShownSeconds() > 0,
                    FString::Printf(TEXT("%.1f ms"), ShownMs()));
        });
    AddCase(
        TEXT("pause menu resume"), .3, [this] { Controller->ResumeFoundation(); },
        [this, Host, Mode, Opened]
        {
            const TSharedPtr<SWebBrowser> Browser = Host->GetBrowser();
            if (Mode == 1)
                Record(TEXT("Back to Game keeps the browser, collapsed"),
                    !Host->IsOpen() && Browser.IsValid() && Browser.Get() == *Opened &&
                        Browser->GetVisibility() == EVisibility::Collapsed && Controller->IsGameplay(),
                    TEXT("kept alive"));
            else
                Record(TEXT("Back to Game destroys the browser"),
                    !Host->IsOpen() && !Browser.IsValid() && Controller->IsGameplay(),
                    FString::Printf(TEXT("mode %d"), Mode));
        });
    AddCase(
        TEXT("pause menu again"), Mode == 0 ? .5 : 3.,
        [this, Host, Escape, WaitShown]
        {
            // Settings changed natively while the menu is hidden, in the same frame as ESC.
            Host->GetVideoSettings()->ApplySetting(TEXT("guiScale"), TEXT("{\"value\":3}"));
            InputBridge->ApplyBinding(TEXT("key.jump"), TEXT("MouseScrollUp"));
            Escape();
            WaitShown();
        },
        [this, Host, Mode, Opened, ShownMs]
        {
            Record(TEXT("second ESC shows the pause menu"), Host->IsOpen(), FString::Printf(TEXT("mode %d"), Mode));
            if (Mode == 1)
            {
                Record(TEXT("second ESC reuses the kept browser"), Host->GetBrowser().Get() == *Opened,
                    TEXT("same SWebBrowser"));
                Record(TEXT("shown page re-read settings changed while hidden"),
                    Host->GetPageShownState().Contains(TEXT("\"jump\":\"MouseScrollUp\"")),
                    FString::Printf(TEXT("page reports %s %.1f ms after ESC"), *Host->GetPageShownState(), ShownMs()));
            }
            Host->GetVideoSettings()->ApplySetting(TEXT("guiScale"), TEXT("{\"value\":0}"));
            InputBridge->ResetBinding(TEXT("key.jump"));
            Controller->ResumeFoundation();
        });
#endif
}
