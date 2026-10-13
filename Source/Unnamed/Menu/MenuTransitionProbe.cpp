// Dev-only menu transition probe (Scripts/TestMenuTransitions.ps1; inert without -TransitionProbe).
// -TransitionProbe=<base> records the frames around every menu transition in one process, across map travel: S the game
// start (menu map), G Singleplayer, P Play Selected World (travel + loading screen), I/J the creative inventory
// open/close, K ESC, O Options, V Video Settings, W Done, C Controls, D Done, Y Done (back to Game Menu), R Return to
// Main Menu, H Singleplayer again, M Create New World (screen), N Create New World (a voxel world: travel + loading
// screen), E ESC there, F Return to Main Menu from it, Q Quit Game. It drives the real page (DOM clicks through
// ExecuteJavascript) and native ESC/E through the input router. Every recorded frame requests a screenshot
// (<base>-pause-<SEQ>-f<+/-NNN>.png, the pause tools' names) and appends a row to <base>-pause-frames.csv.
// -TransitionNoShots: the same script with no screenshots (real-time ms). -TransitionLoops=N repeats G..F N times.
// -TransitionGuiScale=N adds U: GUI Scale set to N on the world's Video Settings screen. -TransitionIdle=N adds Z: N
// uncapped frames in the Playground, then X restores the 60 fps cap.
#include "UnnamedLog.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationInputRouter.h"
#include "Foundation/FoundationPanels.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Menu/MenuBrowserHost.h"
#include "Menu/MenuBrowserSubsystem.h"
#include "Menu/MenuPlayerController.h"
#include "Menu/MenuVideoSettings.h"
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
    // Reports the page's screen, panorama drawn and pause-hidden whenever one changes (each animation frame), tagged
    // with the injection it belongs to. Injecting again (a kept title browser watched again after a world) takes a new
    // tag and reports the current state at once; reports under an old tag (a hidden page) are ignored.
    const TCHAR* WatchJs = TEXT(
        "(t=>{window.__tpTag=t;window.__tpLast='';if(window.__tp)return;window.__tp=1;const f=()=>{let s='?',p='?';"
        "try{s=current.name;p=String(!!(panoramaLoaded||panoramaFallback))}catch(e){}const h=document."
        "documentElement.classList.contains('pause-hidden');const k=s+' '+p+' '+h;if(k!==window.__tpLast){window."
        "__tpLast=k;console.log('TRANSITION PROBE page '+window.__tpTag+' '+k)}requestAnimationFrame(f)};"
        "requestAnimationFrame(f)})");

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
            FString Tag, State;
            if (!FString(Page + 22).Split(TEXT(" "), &Tag, &State)) return;
            FScopeLock Lock(&Mutex);
            if (Tag != FString::FromInt(WatchTag)) return;
            PageState = State;
            ++PageReports;
        }
        virtual bool CanBeUsedOnAnyThread() const override { return true; }
        FString GetPage()
        {
            FScopeLock Lock(&Mutex);
            return PageState;
        }
        // A new watcher: only its reports count from now on.
        int32 ResetPage()
        {
            FScopeLock Lock(&Mutex);
            PageState.Reset();
            return ++WatchTag;
        }
        std::atomic<int32> WebErrors{0}, PageReports{0};
        int32 WatchTag = 0;

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
            FParse::Value(FCommandLine::Get(), TEXT("TransitionGuiScale="), GuiScale);
            FParse::Value(FCommandLine::Get(), TEXT("TransitionIdle="), IdleFrames);
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
        // -TransitionGuiScale=N: on the world's Video Settings screen, U sets GUI Scale to N (then Return to Main Menu
        // shows whether the title follows it).
        int32 GuiScale = 0;
        // -TransitionIdle=N: Z records N uncapped frames in the Playground (frame time with the hidden title page).
        int32 IdleFrames = 0;
        FTransitionLog Log;
        TArray<FStep> Steps;
        int32 StepIndex = 0, Index = 0, DoneAt = -1;
        bool bRecording = false, bHeader = false, bStarted = false;
        double StepEntered = 0, ActSeconds = 0, LastRowSeconds = 0, Started = 0;
        uint64 LastFrame = 0;
        // Weak, so a new browser at a freed one's address still counts as new.
        TWeakPtr<SWebBrowser> Watched;
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
        static UMenuBrowserSubsystem* Titles()
        {
            APlayerController* P = Player();
            return P && P->GetGameInstance() ? P->GetGameInstance()->GetSubsystem<UMenuBrowserSubsystem>() : nullptr;
        }
        FString Screen() { return Log.GetPage().Left(Log.GetPage().Find(TEXT(" "))); }
        // The title with its panorama, usable: no loading overlay over it (menu.Title.Keep).
        bool MenuSettled()
        {
            return Cast<AMenuPlayerController>(Player()) && Browser().IsValid() && Browser()->IsLoaded() &&
                Log.GetPage().StartsWith(TEXT("main true")) && !(Titles() && Titles()->IsOverlayShown());
        }
        // One small number per browser seen (1 = the first), so the CSV shows whether the menu map's browser survived a
        // trip. Weak pointers: a freed browser never matches a new one at the same address.
        int32 BrowserId(const TSharedPtr<SWebBrowser>& B)
        {
            if (!B.IsValid()) return 0;
            for (int32 Seen = 0; Seen < BrowserIds.Num(); ++Seen)
                if (BrowserIds[Seen].HasSameObject(B.Get()) && BrowserIds[Seen].IsValid()) return Seen + 1;
            BrowserIds.Add(B);
            return BrowserIds.Num();
        }
        TArray<TWeakPtr<SWebBrowser>> BrowserIds;
        bool PageOn(const TCHAR* Name) { return Log.GetPage().StartsWith(FString(Name) + TEXT(" ")); }

        void Event(const FString& What)
        {
            Append(FString::Printf(TEXT("event,,%llu,%.2f,,,,,,,,,,,,,,,,%s\n"), GFrameCounter,
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
                                                   "ms,browser_id,cover,title_ack,note\n"),
                    *Csv);
            }
            FFileHelper::SaveStringToFile(
                Row, *Csv, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
        }

        void Build()
        {
            auto Gameplay = [] { return Foundation() && Foundation()->IsGameplay(); };
            Steps.Add({TEXT("S"), 0, 600, 8, 0, [] { return true; }, [] {}, [this] { return MenuSettled(); }});
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
                if (IdleFrames > 0)
                {
                    Steps.Add({TEXT("Z") + N, 0, IdleFrames, 0, 3., Gameplay,
                        [] { IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->Set(0, ECVF_SetByCode); },
                        [] { return false; }});
                    Steps.Add({TEXT("X") + N, 0, 0, 0, 0., [] { return true; },
                        [] { IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->Set(60, ECVF_SetByCode); },
                        [] { return true; }});
                }
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
                if (GuiScale > 0)
                    Steps.Add({TEXT("U") + N, 3, 20, 6, 1.5, [this] { return !Screen().IsEmpty(); },
                        [this] { Js(FString::Printf(TEXT("MenuVideo.set('guiScale',%d)"), GuiScale)); },
                        [this] { return Host() && Host()->GetVideoSettings()->GetGuiScale() == GuiScale; }});
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
            if (!(Watched.HasSameObject(B.Get()) && Watched.IsValid()) && (!B.IsValid() || B->IsLoaded()))
            {
                Watched = B;
                const int32 Tag = Log.ResetPage();
                if (B.IsValid()) B->ExecuteJavascript(FString::Printf(TEXT("%s(%d)"), WatchJs, Tag));
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
            UMenuBrowserSubsystem* T = Titles();
            Append(FString::Printf(TEXT("%s,%d,%llu,%.2f,%.2f,%s,%d,%s,%d,%d,%d,%d,%d,%s,%d,%.1f,%d,%d,%d,\n"), *Seq,
                Index, GFrameCounter, (Now - ActSeconds) * 1000,
                LastRowSeconds > 0 ? (Now - LastRowSeconds) * 1000 : 0., *Map, int32(B.IsValid()), *Visibility,
                int32(B.IsValid() && B->IsLoaded()), int32(H && H->IsOpen()),
                int32(F && F->GetHud()->IsLoadingScreenShown()), int32(F && F->GetPanels()->IsOpen()),
                int32(F && F->IsGameplay()), *Log.GetPage(), Log.WebErrors.load(), (Now - GStartTime) * 1000,
                BrowserId(B), int32(T && T->IsOverlayShown()), int32(T && T->IsTitlePainted())));
            LastRowSeconds = Now;
        }
    };

    FTransitionProbe GTransitionProbe;
}
#endif
