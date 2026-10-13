// Dev-only menu transition probe (Scripts/TestMenuTransitions.ps1; inert without -TransitionProbe).
// -TransitionProbe=<base> records the frames around every menu transition in one process, across map travel: S the game
// start (menu map), G Singleplayer, P Play Selected World (travel + loading screen), I/J the creative inventory
// open/close, K ESC, O Options, V Video Settings, W Done, C Controls, D Done, Y Done (back to Game Menu), R Return to
// Main Menu, H Singleplayer again, M Create New World (screen), N Create New World (a voxel world: travel + loading
// screen), E ESC there, F Return to Main Menu from it, Q Quit Game. It drives the real page (DOM clicks through
// ExecuteJavascript) and native ESC/E through the input router. Every recorded frame requests a screenshot
// (<base>-pause-<SEQ>-f<+/-NNN>.png, the pause tools' names) and appends a row to <base>-pause-frames.csv.
// -TransitionNoShots: the same script with no screenshots (real-time ms). -TransitionLoops=N repeats G..F N times.
#include "UnnamedLog.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationInputRouter.h"
#include "Foundation/FoundationPanels.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Menu/MenuBrowserHost.h"
#include "Menu/MenuPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SWebBrowser.h"
#include "UObject/UObjectGlobals.h"
#include "UnrealClient.h"
#include <atomic>

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    const TCHAR* ClickJs = TEXT("(l=>{const b=[...document.querySelectorAll('button')].find(e=>e.textContent.trim()."
                                "startsWith(l));console.log('TRANSITION PROBE click '+l+' '+(b?(b.disabled?'disabled':"
                                "'ok'):'missing'));b&&!b.disabled&&b.click()})");
    // Reports the page's screen, panorama drawn and pause-hidden whenever one changes (each animation frame).
    const TCHAR* WatchJs = TEXT(
        "(()=>{if(window.__tp)return;window.__tp=1;let last='';const f=()=>{let s='?',p='?';try{s=current.name;"
        "p=String(!!(panoramaLoaded||panoramaFallback))}catch(e){}const h=document.documentElement.classList."
        "contains('pause-hidden');const k=s+' '+p+' '+h;if(k!==last){last=k;console.log('TRANSITION PROBE page '+k)}"
        "requestAnimationFrame(f)};requestAnimationFrame(f)})()");

    class FTransitionLog final : public FOutputDevice
    {
    public:
        virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
        {
            static const FName WebBrowser(TEXT("LogWebBrowser"));
            if (Category == WebBrowser && (Verbosity & ELogVerbosity::VerbosityMask) <= ELogVerbosity::Error)
                ++WebErrors;
            const TCHAR* Page = FCString::Strstr(Text, TEXT("TRANSITION PROBE page "));
            if (!Page) return;
            FScopeLock Lock(&Mutex);
            PageState = Page + 22;
            ++PageReports;
        }
        virtual bool CanBeUsedOnAnyThread() const override { return true; }
        FString GetPage()
        {
            FScopeLock Lock(&Mutex);
            return PageState;
        }
        void ResetPage()
        {
            FScopeLock Lock(&Mutex);
            PageState.Reset();
        }
        std::atomic<int32> WebErrors{0}, PageReports{0};

    private:
        FCriticalSection Mutex;
        FString PageState;
    };

    struct FStep
    {
        FString Seq;
        int32 Pre = 3, Cap = 20, Tail = 6;
        double Settle = 1.5;
        TFunction<bool()> Ready;
        TFunction<void()> Act;
        TFunction<bool()> Done;
    };

    class FTransitionProbe
    {
    public:
        FTransitionProbe()
        {
            if (!FParse::Value(FCommandLine::Get(), TEXT("TransitionProbe="), Base)) return;
            bShots = !FParse::Param(FCommandLine::Get(), TEXT("TransitionNoShots"));
            FParse::Value(FCommandLine::Get(), TEXT("TransitionLoops="), Loops);
            Csv = Base + TEXT("-pause-frames.csv");
            GLog->AddOutputDevice(&Log);
            FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FTransitionProbe::OnTick);
            FCoreUObjectDelegates::PreLoadMap.AddLambda([this](const FString& Map) { Event(TEXT("preload ") + Map); });
            FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda([this](UWorld*) { Event(TEXT("postload")); });
            FCoreDelegates::OnPreExit.AddLambda(
                [this]
                {
                    Event(TEXT("pre-exit"));
                    GLog->RemoveOutputDevice(&Log);
                    FWorldDelegates::OnWorldPostActorTick.RemoveAll(this);
                });
            Build();
        }

    private:
        FString Base, Csv;
        bool bShots = true;
        int32 Loops = 1;
        FTransitionLog Log;
        TArray<FStep> Steps;
        int32 StepIndex = 0, Index = 0, DoneAt = -1;
        bool bRecording = false, bHeader = false, bStarted = false;
        double StepEntered = 0, ActSeconds = 0, LastRowSeconds = 0, Started = 0;
        uint64 LastFrame = 0;
        SWebBrowser* Watched = nullptr;
        FString ActPage;

        static APlayerController* Player()
        {
            UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
            return World ? World->GetFirstPlayerController() : nullptr;
        }
        static AFoundationController* Foundation() { return Cast<AFoundationController>(Player()); }
        static UMenuBrowserHost* Host()
        {
            APlayerController* P = Player();
            return P ? P->FindComponentByClass<UMenuBrowserHost>() : nullptr;
        }
        static TSharedPtr<SWebBrowser> Browser()
        {
            UMenuBrowserHost* H = Host();
            return H ? H->GetBrowser() : nullptr;
        }
        static void Js(const FString& Code)
        {
            if (TSharedPtr<SWebBrowser> B = Browser()) B->ExecuteJavascript(Code);
        }
        static void Click(const TCHAR* Label) { Js(FString(ClickJs) + TEXT("('") + Label + TEXT("')")); }
        FString Screen() { return Log.GetPage().Left(Log.GetPage().Find(TEXT(" "))); }
        bool MenuSettled()
        {
            return Cast<AMenuPlayerController>(Player()) && Browser().IsValid() && Browser()->IsLoaded() &&
                Log.GetPage().StartsWith(TEXT("main true"));
        }
        bool PageOn(const TCHAR* Name) { return Log.GetPage().StartsWith(FString(Name) + TEXT(" ")); }

        void Event(const FString& What)
        {
            Append(FString::Printf(TEXT("event,,%llu,%.2f,,,,,,,,,,,,,%s\n"), GFrameCounter,
                (FPlatformTime::Seconds() - ActSeconds) * 1000, *What));
            UE_LOG(LogUnnamedMenu, Display, TEXT("TRANSITION PROBE event %s"), *What);
        }
        void Append(const FString& Row)
        {
            if (!bHeader)
            {
                bHeader = true;
                FFileHelper::SaveStringToFile(TEXT("seq,frame,engine_frame,ms,dt_ms,map,browser,visibility,loaded,host_"
                                                   "open,loading_screen,panels_open,gameplay,page,web_errors,uptime_"
                                                   "ms,note\n"),
                    *Csv);
            }
            FFileHelper::SaveStringToFile(
                Row, *Csv, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
        }

        void Build()
        {
            auto Gameplay = [] { return Foundation() && Foundation()->IsGameplay(); };
            Steps.Add({TEXT("S"), 0, 90, 8, 0, [] { return true; }, [] {}, [this] { return MenuSettled(); }});
            for (int32 Loop = 0; Loop < Loops; ++Loop)
            {
                const FString N = Loops > 1 ? FString::FromInt(Loop + 1) : TEXT("");
                Steps.Add({TEXT("G") + N, 3, 20, 5, 3., [this] { return MenuSettled(); },
                    [] { Click(TEXT("Singleplayer")); }, [this] { return PageOn(TEXT("worlds")); }});
                Steps.Add({TEXT("P") + N, 3, 240, 8, 1.5, [this] { return PageOn(TEXT("worlds")); },
                    []
                    {
                        Js(TEXT("document.querySelector('[data-world=\"playground\"]')?.click()"));
                        Click(TEXT("Play Selected World"));
                    },
                    [Gameplay] { return Gameplay(); }});
                Steps.Add({TEXT("I") + N, 3, 20, 6, 3., [Gameplay] { return Gameplay() && Host()->IsPageReady(); },
                    [] { Foundation()->HandleAction(TEXT("key.inventory"), true); },
                    [] { return Foundation()->GetPanels()->IsInventoryOpen(); }});
                Steps.Add({TEXT("J") + N, 3, 20, 6, 1.5, [] { return Foundation()->GetPanels()->IsInventoryOpen(); },
                    []
                    {
                        const FKey Key = Foundation()->GetInputSubsystem()->GetCurrentKey(TEXT("key.inventory"));
                        Foundation()->GetInputRouter()->RouteKey(Key, true);
                        Foundation()->GetInputRouter()->RouteKey(Key, false);
                    },
                    [Gameplay] { return Gameplay(); }});
                Steps.Add({TEXT("K") + N, 3, 20, 6, 1.5, Gameplay,
                    []
                    {
                        Foundation()->GetInputRouter()->RouteKey(EKeys::Escape, true);
                        Foundation()->GetInputRouter()->RouteKey(EKeys::Escape, false);
                    },
                    [this]
                    {
                        return Host()->IsOpen() && PageOn(TEXT("foundationPause")) &&
                            Log.GetPage().EndsWith(TEXT(" false"));
                    }});
                auto Page = [this](const TCHAR* Seq, const TCHAR* Label, const TCHAR* From)
                {
                    // From: the screen it starts on (nullptr: wherever the previous step left it).
                    const FString FromName = From ? From : TEXT("");
                    Steps.Add({FString(Seq), 3, 20, 6, 1.5,
                        [this, FromName] { return FromName.IsEmpty() ? !Screen().IsEmpty() : PageOn(*FromName); },
                        [this, Label]
                        {
                            ActPage = Screen();
                            Click(Label);
                        },
                        [this] { return !Screen().IsEmpty() && Screen() != ActPage; }});
                };
                Page(*(TEXT("O") + N), TEXT("Options"), TEXT("foundationPause"));
                Page(*(TEXT("V") + N), TEXT("Video Settings"), TEXT("options"));
                Page(*(TEXT("W") + N), TEXT("Done"), nullptr);
                Page(*(TEXT("C") + N), TEXT("Controls"), TEXT("options"));
                Page(*(TEXT("D") + N), TEXT("Done"), nullptr);
                Page(*(TEXT("Y") + N), TEXT("Done"), TEXT("options"));
                Steps.Add({TEXT("R") + N, 3, 240, 8, 1.5, [this] { return PageOn(TEXT("foundationPause")); },
                    [] { Click(TEXT("Return to Main Menu")); }, [this] { return MenuSettled(); }});
                // A new voxel world: its loading screen, then ESC and Return to Main Menu from it.
                Steps.Add({TEXT("H") + N, 3, 20, 5, 3., [this] { return MenuSettled(); },
                    [] { Click(TEXT("Singleplayer")); }, [this] { return PageOn(TEXT("worlds")); }});
                Page(*(TEXT("M") + N), TEXT("Create New World"), TEXT("worlds"));
                Steps.Add({TEXT("N") + N, 3, 400, 8, 1.5,
                    [this] { return !Screen().IsEmpty() && !PageOn(TEXT("worlds")) && !PageOn(TEXT("main")); },
                    [] { Click(TEXT("Create New World")); }, [Gameplay] { return Gameplay(); }});
                Steps.Add({TEXT("E") + N, 3, 20, 6, 3., [Gameplay] { return Gameplay() && Host()->IsPageReady(); },
                    []
                    {
                        Foundation()->GetInputRouter()->RouteKey(EKeys::Escape, true);
                        Foundation()->GetInputRouter()->RouteKey(EKeys::Escape, false);
                    },
                    [this]
                    {
                        return Host()->IsOpen() && PageOn(TEXT("foundationPause")) &&
                            Log.GetPage().EndsWith(TEXT(" false"));
                    }});
                Steps.Add({TEXT("F") + N, 3, 240, 8, 1.5, [this] { return PageOn(TEXT("foundationPause")); },
                    [] { Click(TEXT("Return to Main Menu")); }, [this] { return MenuSettled(); }});
            }
            Steps.Add({TEXT("Q"), 3, 60, 1000, 3., [this] { return MenuSettled(); }, [] { Click(TEXT("Quit Game")); },
                [] { return false; }});
        }

        void OnTick(UWorld* World, ELevelTick, float)
        {
            if (!World || !World->IsGameWorld() || !GEngine->GameViewport ||
                GEngine->GameViewport->GetWorld() != World || GFrameCounter == LastFrame)
                return;
            LastFrame = GFrameCounter;
            const double Now = FPlatformTime::Seconds();
            if (!bStarted)
            {
                bStarted = true;
                Started = Now;
                IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))->Set(0, ECVF_SetByCode);
                IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->Set(60, ECVF_SetByCode);
            }
            // A new browser (map travel, or a fresh one) gets the page watcher once its document has loaded.
            TSharedPtr<SWebBrowser> B = Browser();
            if (B.Get() != Watched && (!B.IsValid() || B->IsLoaded()))
            {
                Watched = B.Get();
                Log.ResetPage();
                if (B.IsValid()) B->ExecuteJavascript(WatchJs);
            }
            if (Now - Started > 600 || StepIndex >= Steps.Num())
            {
                if (StepIndex < Steps.Num()) Event(TEXT("watchdog exit"));
                FPlatformMisc::RequestExitWithStatus(false, StepIndex < Steps.Num() ? 3 : 0);
                return;
            }
            FStep& Step = Steps[StepIndex];
            if (!bRecording)
            {
                if (StepEntered == 0) StepEntered = Now;
                if (Now - StepEntered > 30)
                {
                    Event(TEXT("ready timeout; skipped ") + Step.Seq);
                    StepEntered = 0;
                    ++StepIndex;
                    return;
                }
                if (Now - StepEntered < Step.Settle || !Step.Ready()) return;
                bRecording = true;
                Index = -Step.Pre;
                DoneAt = -1;
                if (Step.Pre == 0) ActSeconds = Step.Seq == TEXT("S") && Host() ? Host()->GetStartSeconds() : Now;
            }
            if (Index == 0)
            {
                if (Step.Seq != TEXT("S")) ActSeconds = Now;
                UE_LOG(LogUnnamedMenu, Display, TEXT("TRANSITION PROBE act %s"), *Step.Seq);
                Step.Act();
            }
            if (Index >= 0 && DoneAt < 0 && Step.Done())
            {
                DoneAt = Index;
                UE_LOG(LogUnnamedMenu, Display, TEXT("TRANSITION PROBE done %s at frame %d, %.1f ms"), *Step.Seq, Index,
                    (Now - ActSeconds) * 1000);
            }
            Row(Step.Seq, Now);
            if (bShots)
                FScreenshotRequest::RequestScreenshot(Base +
                        FString::Printf(TEXT("-pause-%s-f%s%03d.png"), *Step.Seq, Index < 0 ? TEXT("-") : TEXT("+"),
                            FMath::Abs(Index)),
                    true, false);
            ++Index;
            if ((DoneAt >= 0 && Index > DoneAt + Step.Tail) || Index > Step.Cap)
            {
                if (DoneAt < 0) Event(TEXT("cap reached in ") + Step.Seq);
                bRecording = false;
                StepEntered = 0;
                ++StepIndex;
            }
        }

        void Row(const FString& Seq, double Now)
        {
            APlayerController* P = Player();
            AFoundationController* F = Foundation();
            UMenuBrowserHost* H = Host();
            TSharedPtr<SWebBrowser> B = Browser();
            const FString Map = P && P->GetWorld() ? P->GetWorld()->GetMapName() : TEXT("");
            const FString Visibility = B.IsValid() ? B->GetVisibility().ToString() : TEXT("");
            Append(FString::Printf(TEXT("%s,%d,%llu,%.2f,%.2f,%s,%d,%s,%d,%d,%d,%d,%d,%s,%d,%.1f,\n"), *Seq, Index,
                GFrameCounter, (Now - ActSeconds) * 1000, LastRowSeconds > 0 ? (Now - LastRowSeconds) * 1000 : 0., *Map,
                int32(B.IsValid()), *Visibility, int32(B.IsValid() && B->IsLoaded()), int32(H && H->IsOpen()),
                int32(F && F->GetHud()->IsLoadingScreenShown()), int32(F && F->GetPanels()->IsOpen()),
                int32(F && F->IsGameplay()), *Log.GetPage(), Log.WebErrors.load(), (Now - GStartTime) * 1000));
            LastRowSeconds = Now;
        }
    };

    FTransitionProbe GTransitionProbe;
}
#endif
