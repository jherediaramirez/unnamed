#include "Foundation/FoundationRuntimeProbe.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Hud/HudDrawList.h"
#include "Hud/HudPresenter.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

// The death screen (step 11a) in the interaction suite: with foundation.Death.Screen 1 a survival player with a few
// stacks dies by /kill; the death message reaches the chat, the screen holds (buttons disabled for 20 ticks, then
// enabled), every stack drops as one item entity, and a real Slate click on the HUD's Respawn rectangle (refused
// before tick 20) brings the player back at the spawn with gameplay input. Switch 0 is the control: the message, no
// drops, no screen, and the old auto-respawn after 20 ticks.
void UFoundationRuntimeProbe::AddDeathCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    auto SetSwitch = [](int32 Value)
    {
        if (IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Death.Screen")))
            Switch->Set(Value, ECVF_SetByCode);
    };
    auto Shot = [this](const TCHAR* Name)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-death-") + Name + TEXT(".png"), true, false);
    };
    auto Player = [this]() -> FFoundationPlayer*
    {
        FFoundationAuthority* Authority = Pawn->GetAuthority();
        return Authority ? Authority->FindPlayer(Pawn->Interaction.PlayerId) : nullptr;
    };
    auto StackCount = [Player]
    {
        const FFoundationPlayer* P = Player();
        if (!P) return -1;
        int32 Stacks = !P->Inventory.Offhand.IsEmpty() + !P->Inventory.Carried.IsEmpty();
        for (const TArray<FFoundationItemInstance>* Slots :
            {&P->Inventory.Hotbar, &P->Inventory.Main, &P->Inventory.Armor, &P->Inventory.Craft})
            for (const FFoundationItemInstance& Stack : *Slots)
                Stacks += !Stack.IsEmpty();
        return Stacks;
    };
    // Item entities newer than FirstId (the ones the death dropped).
    auto NewEntities = [this](int32 FirstId)
    {
        const FFoundationAuthority* Authority = Pawn->GetAuthority();
        int32 Count = 0;
        if (Authority)
            for (const FFoundationItemEntity& Entity : Authority->GetItemEntities())
                Count += Entity.Id >= FirstId;
        return Count;
    };
    auto NextEntityId = [this]
    {
        int32 Id = 1;
        if (const FFoundationAuthority* Authority = Pawn->GetAuthority())
            for (const FFoundationItemEntity& Entity : Authority->GetItemEntities())
                Id = FMath::Max(Id, Entity.Id + 1);
        return Id;
    };
    auto Spawn = [this]
    {
        const AFoundationLab* Lab = Pawn->GetLab();
        return Lab ? Lab->GetSafeSpawn(*GetDefault<UFoundationProfile>()) : FVector::ZeroVector;
    };
    // Survival, two metres from the spawn, with three stacks in the hotbar and main grid; then /kill through the chat.
    auto Die = [this, Player, Spawn]
    {
        ResetPlayer(Spawn() + FVector(200, 0, 0));
        Controller->GetPanels()->OpenText(false);
        // Survival's lab rules, as /gamemode survival sets them (the command changes nothing when the vitals already
        // say survival, which the Playground's creative lab can).
        Pawn->Vitals.Mode = EFoundationPlayerMode::Survival;
        FFoundationAuthority* Authority = Pawn->GetAuthority();
        if (FFoundationPlayer* P = Player(); P && Authority)
        {
            Authority->Options.bInfiniteSupply = P->Inventory.bInfiniteSupply = false;
            Authority->Options.bToolWear = true;
            Authority->Options.bCreativeInventory = false;
            for (TArray<FFoundationItemInstance>* Slots :
                {&P->Inventory.Hotbar, &P->Inventory.Main, &P->Inventory.Armor, &P->Inventory.Craft})
                for (FFoundationItemInstance& Stack : *Slots)
                    Stack = {};
            P->Inventory.Offhand = P->Inventory.Carried = {};
            const TCHAR* Items[] = {TEXT("stone"), TEXT("dirt"), TEXT("oak_log")};
            const int32 Counts[] = {64, 23, 7}, Slots[] = {0, 1, 2};
            for (int32 Index = 0; Index < 3; ++Index)
                P->Inventory.SetSlot(
                    Slots[Index], P->Inventory.MakeStack(Authority->Rules.FindItem(Items[Index]), Counts[Index]));
            ++P->Inventory.Revision;
        }
        Controller->SubmitChat(TEXT("/kill"));
        Controller->GetPanels()->Close();
    };
    auto ChatHas = [this](const TCHAR* Text)
    {
        const FFoundationChat& Chat = Controller->GetChat();
        for (int32 Index = 0; Index < Chat.Num(); ++Index)
            if (Chat.GetText(Index) == Text) return true;
        return false;
    };
    // A left press (and release) at a death button's centre through Slate, as the player's click: the viewport's
    // geometry maps the HUD's GUI rectangle (HudDrawList::GetDeathButton) to screen space, and Slate routes the press
    // to the controller's death widget (HandleDeathMouse). False when there is no Slate viewport to click.
    auto Click = [this](EHudDeathButton Button)
    {
        if (!FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport) return false;
        const TSharedPtr<SViewport> Viewport = GEngine->GameViewport->GetGameViewportWidget();
        if (!Viewport) return false;
        const FGeometry& Geometry = Viewport->GetCachedGeometry();
        const int32 Scale = FMath::Max(1, Controller->GetHud()->GetPresenter()->GetGuiScale());
        FIntPoint Size;
        Controller->GetViewportSize(Size.X, Size.Y);
        const FIntRect Rect = HudDrawList::GetDeathButton(HudModel::GetLogicalSize(Size, Scale), Button);
        const float SlateScale = FMath::Max(Geometry.GetAccumulatedLayoutTransform().GetScale(), .01f);
        const FVector2f Pixels = FVector2f(Rect.Min + Rect.Max) * .5f * float(Scale);
        const FVector2D Screen = FVector2D(Geometry.LocalToAbsolute(FVector2D(Pixels / SlateScale)));
        FSlateApplication& Slate = FSlateApplication::Get();
        const FModifierKeysState Modifiers;
        const uint32 Pointer = FSlateApplicationBase::CursorPointerIndex;
        Slate.ProcessMouseMoveEvent(FPointerEvent(Pointer, Screen, Screen, TSet<FKey>(), EKeys::Invalid, 0, Modifiers));
        TSet<FKey> Pressed;
        Pressed.Add(EKeys::LeftMouseButton);
        Slate.ProcessMouseButtonDownEvent(
            nullptr, FPointerEvent(Pointer, Screen, Screen, Pressed, EKeys::LeftMouseButton, 0, Modifiers));
        Slate.ProcessMouseButtonUpEvent(
            FPointerEvent(Pointer, Screen, Screen, TSet<FKey>(), EKeys::LeftMouseButton, 0, Modifiers));
        return true;
    };
    auto GameplayInput = [this]
    {
        const UUnnamedInputSubsystem* Input = Controller->GetInputSubsystem();
        return Input && Input->IsGameplayEnabled();
    };

    // What the cases change, put back by the restore case.
    auto Previous = MakeShared<int32>(1);
    auto SavedInventory = MakeShared<TOptional<FFoundationInventory>>();
    auto SavedMode = MakeShared<EFoundationPlayerMode>(EFoundationPlayerMode::Creative);
    auto SavedOptions = MakeShared<TOptional<FFoundationLabOptions>>();
    auto bSavedHideHud = MakeShared<bool>(false);
    auto FirstId = MakeShared<int32>(0);
    auto ShotTicks = MakeShared<int32>(-1);
    auto bClicked = MakeShared<bool>(false);

    AddCase(
        TEXT("death 1: /kill with three stacks"), .25,
        [this, SetSwitch, Player, Die, NextEntityId, Previous, SavedInventory, SavedMode, SavedOptions, bSavedHideHud,
            FirstId]
        {
            if (const IConsoleVariable* Switch =
                    IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Death.Screen")))
                *Previous = Switch->GetInt();
            SetSwitch(1);
            *bSavedHideHud = Controller->GetHud()->bHideHud;
            Controller->GetHud()->bHideHud = false;
            *SavedMode = Pawn->Vitals.Mode;
            if (const FFoundationAuthority* Authority = Pawn->GetAuthority()) *SavedOptions = Authority->Options;
            if (const FFoundationPlayer* P = Player()) *SavedInventory = P->Inventory;
            Controller->ClearChat();
            *FirstId = NextEntityId();
            Die();
        },
        [this, ChatHas, StackCount, NewEntities, FirstId, GameplayInput]
        {
            Record(TEXT("death 1: the chat shows the death message"), ChatHas(TEXT("Player was killed")),
                Pawn->GetDeathMessage());
            Record(TEXT("death 1: the death screen is active"),
                Pawn->IsDeathScreenActive() && Pawn->GetDeathTicks() < 20,
                FString::Printf(TEXT("active %d, death ticks %d"), Pawn->IsDeathScreenActive(), Pawn->GetDeathTicks()));
            Record(TEXT("death 1: one item entity per stack, the inventory empty"),
                NewEntities(*FirstId) == 3 && StackCount() == 0,
                FString::Printf(TEXT("%d new item entities, %d stacks left"), NewEntities(*FirstId), StackCount()));
            const FHudSnapshot& S = Controller->GetHud()->GetPresenter()->GetSnapshot();
            Record(TEXT("death 1: the HUD draws the screen with its buttons disabled, no gameplay input"),
                S.bDeathScreen && !S.bDeathButtonsEnabled && !Controller->IsGameplay() && !GameplayInput(),
                FString::Printf(TEXT("screen %d, enabled %d, gameplay %d, input %d"), S.bDeathScreen,
                    S.bDeathButtonsEnabled, Controller->IsGameplay(), GameplayInput()));
        });
    AddCase(
        TEXT("death 1: capture disabled"), .3,
        [this, Shot, ShotTicks]
        {
            *ShotTicks = Pawn->GetDeathTicks();
            Shot(TEXT("1-screen"));
        },
        [this, Click, ShotTicks]
        {
            Record(TEXT("death 1: the disabled capture before tick 20"), *ShotTicks >= 0 && *ShotTicks < 20,
                FString::Printf(TEXT("death ticks %d at the request"), *ShotTicks));
            const int32 Ticks = Pawn->GetDeathTicks();
            const bool bRequested = Pawn->RequestRespawn();
            const bool bSent = Ticks < 20 && Click(EHudDeathButton::Respawn);
            Record(TEXT("death 1: Respawn refused before tick 20 (RequestRespawn and a click)"),
                Ticks < 20 && !bRequested && bSent && Pawn->IsDeathScreenActive(),
                FString::Printf(TEXT("death ticks %d, requested %d, click sent %d, still active %d"), Ticks, bRequested,
                    bSent, Pawn->IsDeathScreenActive()));
        });
    AddCase(
        TEXT("death 1: buttons enable"), 1.0, [] {},
        [this]
        {
            const FHudSnapshot& S = Controller->GetHud()->GetPresenter()->GetSnapshot();
            Record(TEXT("death 1: the buttons enable at tick 20"),
                Pawn->IsDeathScreenActive() && Pawn->GetDeathTicks() >= 20 && S.bDeathScreen && S.bDeathButtonsEnabled,
                FString::Printf(TEXT("death ticks %d, screen %d, enabled %d"), Pawn->GetDeathTicks(), S.bDeathScreen,
                    S.bDeathButtonsEnabled));
        });
    AddCase(TEXT("death 1: capture enabled"), .3, [Shot] { Shot(TEXT("1-enabled")); }, [] {});
    AddCase(
        TEXT("death 1: click Respawn"), .4,
        [this, Click, bClicked]
        {
            // Slate routes the click synchronously, so whether it respawned is read before the fallback below.
            *bClicked = Click(EHudDeathButton::Respawn) && !Pawn->Vitals.IsDead();
            // No Slate viewport, or a click that did nothing: the button's own call, so the remaining checks still run.
            if (Pawn->IsDeathScreenActive()) Pawn->RequestRespawn();
        },
        [this, Spawn, bClicked, GameplayInput]
        {
            Record(TEXT("death 1: the click on Respawn respawns"), *bClicked,
                FString::Printf(TEXT("alive after the click alone %d"), *bClicked));
            const double Off = FVector::Dist2D(Pawn->Simulation.Feet, Spawn());
            const FHudSnapshot& S = Controller->GetHud()->GetPresenter()->GetSnapshot();
            Record(TEXT("death 1: alive at the spawn, gameplay input back, the screen gone"),
                !Pawn->Vitals.IsDead() && Off < 1 && Controller->IsGameplay() && GameplayInput() &&
                    !Pawn->IsDeathScreenActive() && !S.bDeathScreen,
                FString::Printf(TEXT("dead %d, %.1f cm from the spawn, gameplay %d, input %d, screen %d"),
                    Pawn->Vitals.IsDead(), Off, Controller->IsGameplay(), GameplayInput(), S.bDeathScreen));
        });
    AddCase(
        TEXT("death 0: /kill with three stacks"), .25,
        [this, SetSwitch, Die, NextEntityId, FirstId]
        {
            SetSwitch(0);
            Controller->ClearChat();
            *FirstId = NextEntityId();
            Die();
        },
        [this, ChatHas, StackCount, NewEntities, FirstId]
        {
            const FHudSnapshot& S = Controller->GetHud()->GetPresenter()->GetSnapshot();
            Record(TEXT("death 0: the death message, no drops, no screen"),
                ChatHas(TEXT("Player was killed")) && Pawn->Vitals.IsDead() && NewEntities(*FirstId) == 0 &&
                    StackCount() == 3 && !Pawn->IsDeathScreenActive() && !S.bDeathScreen,
                FString::Printf(TEXT("message '%s', dead %d, %d new item entities, %d stacks, screen %d/%d"),
                    *Pawn->GetDeathMessage(), Pawn->Vitals.IsDead(), NewEntities(*FirstId), StackCount(),
                    Pawn->IsDeathScreenActive(), S.bDeathScreen));
        });
    AddCase(TEXT("death 0: capture"), .3, [Shot] { Shot(TEXT("0")); }, [] {});
    AddCase(
        TEXT("death 0: auto-respawn"), 1.0, [] {},
        [this, Spawn]
        {
            const double Off = FVector::Dist2D(Pawn->Simulation.Feet, Spawn());
            Record(TEXT("death 0: the old auto-respawn after 20 ticks"),
                !Pawn->Vitals.IsDead() && Off < 1 && Controller->IsGameplay(),
                FString::Printf(TEXT("dead %d, %.1f cm from the spawn, gameplay %d"), Pawn->Vitals.IsDead(), Off,
                    Controller->IsGameplay()));
        });

    // Step 11b: the hurt camera roll and red tint, the first-person fire overlay, and the chat bar's capture pair. Each
    // case restores what it changed; captures are named like the death ones (-hurt-1.png, -fire-1.png, -chatbar-1.png).
    auto FeelShot = [this](const TCHAR* Name)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-") + Name + TEXT(".png"), true, false);
    };
    auto CameraRoll = [this]
    {
        const UCameraComponent* Camera = Pawn->GetCamera();
        return Camera ? double(FRotator::NormalizeAxis(Camera->GetComponentRotation().Roll)) : 0.;
    };
    auto Snapshot = [this]() -> const FHudSnapshot& { return Controller->GetHud()->GetPresenter()->GetSnapshot(); };
    // Survival in the dry open, full health, no fire, the switch set; returns the previous switch value.
    auto FeelSetup = [this, SetSwitch, Spawn](int32 Value)
    {
        SetSwitch(Value);
        Controller->GetPanels()->Close();
        ResetPlayer(Spawn() + FVector(200, 0, 0));
        Pawn->Vitals.Mode = EFoundationPlayerMode::Survival;
        Pawn->Vitals.Health = 20.f;
        Pawn->Vitals.FireTicks = 0;
        Pawn->Vitals.DamageCooldown = 0;
    };
    auto SetChatSwitch = [](int32 Value)
    {
        if (IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Chat.Commands")))
            Switch->Set(Value, ECVF_SetByCode);
    };
    auto SavedChatSwitch = MakeShared<int32>(1);
    auto HurtRoll = MakeShared<double>(0.);
    auto bHurtTint = MakeShared<bool>(false);
    auto bHurtApplied = MakeShared<bool>(false);

    for (const int32 Value : {1, 0})
    {
        const FString Tag = FString::Printf(TEXT("hurt %d"), Value);
        AddCase(
            Tag + TEXT(": hit"), .1,
            [this, Value, FeelSetup, bHurtApplied]
            {
                FeelSetup(Value);
                *bHurtApplied = Pawn->Vitals.Hurt(EFoundationDamage::Generic, 2.f);
            },
            [this, Value, CameraRoll, Snapshot, bHurtApplied, HurtRoll, bHurtTint]
            {
                *HurtRoll = CameraRoll();
                *bHurtTint = Snapshot().bDamageTint;
                Record(FString::Printf(TEXT("hurt %d: after the hit"), Value),
                    *bHurtApplied &&
                        // No screen tint either way: foundation.Hurt.ScreenTint is 0 (vanilla) by default.
                        (Value ? (FMath::Abs(*HurtRoll) > .5 && !*bHurtTint)
                               : (FMath::IsNearlyZero(*HurtRoll, .01) && !*bHurtTint)),
                    FString::Printf(TEXT("applied %d, roll %.2f deg, tint %d, cooldown %d"), *bHurtApplied, *HurtRoll,
                        *bHurtTint, Pawn->Vitals.DamageCooldown));
            });
        // About two ticks after the hit (the screenshot shows the frame before its request's tick).
        AddCase(
            Tag + TEXT(": capture"), .15, [Value, FeelShot] { FeelShot(Value ? TEXT("hurt-1") : TEXT("hurt-0")); },
            [] {});
        AddCase(
            Tag + TEXT(": settled"), .7, [] {},
            [this, Value, CameraRoll, Snapshot]
            {
                const double Roll = CameraRoll();
                Record(FString::Printf(TEXT("hurt %d: the roll and tint are gone after 10+ ticks"), Value),
                    FMath::IsNearlyZero(Roll, .01) && !Snapshot().bDamageTint,
                    FString::Printf(TEXT("roll %.3f deg, tint %d, cooldown %d"), Roll, Snapshot().bDamageTint,
                        Pawn->Vitals.DamageCooldown));
            });
    }
    for (const int32 Value : {1, 0})
    {
        AddCase(
            FString::Printf(TEXT("fire %d: burning"), Value), .2,
            [this, Value, FeelSetup]
            {
                FeelSetup(Value);
                Pawn->Vitals.FireTicks = 100;
            },
            [this, Value, Snapshot]
            {
                const FHudSnapshot& S = Snapshot();
                Record(FString::Printf(TEXT("fire %d: the fire overlay %s while burning"), Value,
                           Value ? TEXT("shows") : TEXT("is off")),
                    S.bOnFire == (Value == 1),
                    FString::Printf(TEXT("fire ticks %d, on fire %d, first person %d"), Pawn->Vitals.FireTicks,
                        S.bOnFire, S.bFirstPerson));
            });
        AddCase(
            FString::Printf(TEXT("fire %d: capture"), Value), .3, [Value, FeelShot]
            { FeelShot(Value ? TEXT("fire-1") : TEXT("fire-0")); }, [this] { Pawn->Vitals.FireTicks = 0; });
        AddCase(
            FString::Printf(TEXT("fire %d: out"), Value), .2, [] {},
            [this, Value, Snapshot]
            {
                Record(FString::Printf(TEXT("fire %d: the overlay is gone once the fire is out"), Value),
                    !Snapshot().bOnFire && Pawn->Vitals.FireTicks == 0,
                    FString::Printf(TEXT("fire ticks %d, on fire %d"), Pawn->Vitals.FireTicks, Snapshot().bOnFire));
            });
    }
    // R17-1: toggle the real fire switch after the pawn has ignited from a world fire block. The dry interval is
    // longer than a burn period, so stale burning cannot pass just because its next damage tick has not arrived.
    const FIntVector FireSwitchCell(20, -30, 0);
    const FVector FireSwitchFeet(2050, -2950, 0);
    auto SavedFireCell = MakeShared<TOptional<FFoundationBlockState>>();
    auto SavedFireSwitches = MakeShared<TPair<int32, int32>>(1, 1);
    auto SavedFireRegen = MakeShared<bool>(true);
    auto FireSwitchHealth = MakeShared<float>(0);
    auto FireSwitchBurns = MakeShared<int32>(0);
    auto FireSwitchSteps = MakeShared<int64>(0);
    auto FireSwitchExtinguishes = MakeShared<int32>(0);
    auto SetFireSwitch = [](const TCHAR* Name, int32 Value)
    {
        if (IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(Name))
            Switch->Set(Value, ECVF_SetByCode);
    };
    AddCase(
        TEXT("fire switch: ignite from contact"), 1.2,
        [this, FeelSetup, FireSwitchCell, FireSwitchFeet, SavedFireCell, SavedFireSwitches, SavedFireRegen,
            SetFireSwitch]
        {
            for (const TPair<const TCHAR*, int32*> Switch :
                {TPair<const TCHAR*, int32*>(TEXT("foundation.Fire"), &SavedFireSwitches->Key),
                    {TEXT("foundation.FireBlocks"), &SavedFireSwitches->Value}})
            {
                if (const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Switch.Key))
                    *Switch.Value = CVar->GetInt();
                SetFireSwitch(Switch.Key, 1);
            }
            *SavedFireRegen = Pawn->Vitals.bNaturalRegeneration;
            FeelSetup(1);
            Pawn->Vitals.bNaturalRegeneration = false;
            ResetPlayer(FireSwitchFeet);
            FFoundationAuthority* A = Pawn->GetAuthority();
            const FFoundationBlockType* Fire = A ? A->World.FindBlock(TEXT("fire")) : nullptr;
            if (!Fire || !A->World.IsKnown(FireSwitchCell))
            {
                Record(TEXT("fire switch: fixture"), false, TEXT("missing known cell or fire block"));
                return;
            }
            SavedFireCell->Emplace(A->World.Get(FireSwitchCell).State);
            A->World.Set(FireSwitchCell, Fire->GetDefaultState());
        },
        [this, Snapshot, FeelShot]
        {
            Record(TEXT("fire switch: contact ignited the player"),
                Pawn->Vitals.IsOnFire() && Pawn->Vitals.FireBlockHurts > 0 && Snapshot().bOnFire,
                Pawn->Vitals.Describe());
            FeelShot(TEXT("fire-switch-on"));
        });
    AddCase(TEXT("fire switch: flush on capture"), .2, [] {}, [] {});
    AddCase(
        TEXT("fire switch: disable existing burning"), 1.2,
        [this, FireSwitchFeet, SetFireSwitch, FireSwitchHealth, FireSwitchBurns, FireSwitchSteps]
        {
            ResetPlayer(FireSwitchFeet + FVector(300, 0, 0), false);
            SetFireSwitch(TEXT("foundation.Fire"), 0);
            Pawn->Vitals.DamageCooldown = 0;
            *FireSwitchHealth = Pawn->Vitals.Health;
            *FireSwitchBurns = Pawn->Vitals.BurnHurts;
            *FireSwitchSteps = Pawn->ReferenceSteps;
        },
        [this, Snapshot, FeelShot, FireSwitchHealth, FireSwitchBurns, FireSwitchSteps]
        {
            const int64 Steps = Pawn->ReferenceSteps - *FireSwitchSteps;
            Record(TEXT("fire switch: off clears existing burning and damage"),
                Steps >= FFoundationVitals::BurnIntervalTicks && Pawn->Vitals.FireTicks == 0 &&
                    Pawn->Vitals.Health == *FireSwitchHealth && Pawn->Vitals.BurnHurts == *FireSwitchBurns &&
                    !Snapshot().bOnFire,
                FString::Printf(TEXT("%lld steps; health %.1f -> %.1f; burns %d -> %d; fire %d; overlay %d"), Steps,
                    *FireSwitchHealth, Pawn->Vitals.Health, *FireSwitchBurns, Pawn->Vitals.BurnHurts,
                    Pawn->Vitals.FireTicks, Snapshot().bOnFire));
            FeelShot(TEXT("fire-switch-off"));
        });
    AddCase(TEXT("fire switch: flush off capture"), .2, [] {}, [] {});
    AddCase(
        TEXT("fire switch: disabled contact"), .3,
        [this, FireSwitchFeet, FireSwitchHealth]
        {
            ResetPlayer(FireSwitchFeet, false);
            *FireSwitchHealth = Pawn->Vitals.Health;
        },
        [this, FireSwitchHealth]
        {
            Record(TEXT("fire switch: off prevents new fire and contact damage"),
                Pawn->Vitals.FireTicks == 0 && Pawn->Vitals.Health == *FireSwitchHealth, Pawn->Vitals.Describe());
        });
    AddCase(
        TEXT("fire switch: re-enable contact"), 1.2,
        [this, FireSwitchFeet, SetFireSwitch]
        {
            ResetPlayer(FireSwitchFeet);
            SetFireSwitch(TEXT("foundation.Fire"), 1);
        },
        [this, Snapshot, FeelShot]
        {
            Record(TEXT("fire switch: on restores ignition and the overlay"),
                Pawn->Vitals.IsOnFire() && Pawn->Vitals.Health < Pawn->Vitals.MaxHealth && Snapshot().bOnFire,
                Pawn->Vitals.Describe());
            FeelShot(TEXT("fire-switch-restored"));
        });
    AddCase(TEXT("fire switch: flush restored capture"), .2, [] {}, [] {});
    AddCase(
        TEXT("fire switch: water still extinguishes"), .3,
        [this, FireSwitchExtinguishes]
        {
            *FireSwitchExtinguishes = Pawn->Vitals.Extinguishes;
            ResetPlayer({1150, 550, 0}, false);
        },
        [this, Snapshot, FireSwitchExtinguishes]
        {
            Record(TEXT("fire switch: water still extinguishes with fire on"),
                Pawn->Vitals.FireTicks == 0 && Pawn->Vitals.Extinguishes == *FireSwitchExtinguishes + 1 &&
                    !Snapshot().bOnFire,
                Pawn->Vitals.Describe());
        });
    AddCase(
        TEXT("fire switch: restore fixture"), .1,
        [this, FeelSetup, FireSwitchCell, SavedFireCell, SavedFireSwitches, SavedFireRegen, SetFireSwitch]
        {
            if (FFoundationAuthority* A = Pawn->GetAuthority(); A && SavedFireCell->IsSet())
                A->World.Set(FireSwitchCell, SavedFireCell->GetValue());
            SetFireSwitch(TEXT("foundation.Fire"), SavedFireSwitches->Key);
            SetFireSwitch(TEXT("foundation.FireBlocks"), SavedFireSwitches->Value);
            FeelSetup(1);
            Pawn->Vitals.bNaturalRegeneration = *SavedFireRegen;
        },
        [] {});
    for (const int32 Value : {1, 0})
    {
        AddCase(
            FString::Printf(TEXT("chatbar %d: type"), Value), .4,
            [this, Value, SetChatSwitch, SavedChatSwitch, FeelSetup, FeelShot]
            {
                if (const IConsoleVariable* Switch =
                        IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Chat.Commands")))
                    if (Value == 1) *SavedChatSwitch = Switch->GetInt();
                SetChatSwitch(Value);
                FeelSetup(1);
                Pawn->Vitals.Mode = EFoundationPlayerMode::Creative;
                Controller->GetPanels()->OpenText(false);
                if (FSlateApplication::IsInitialized())
                    for (const TCHAR Char : FString(TEXT("hello world")))
                        FSlateApplication::Get().ProcessKeyCharEvent(
                            FCharacterEvent(Char, FModifierKeysState(), 0, false));
            },
            [this, Value, Snapshot]
            {
                const FHudSnapshot& S = Snapshot();
                const FString Typed = Controller->GetPanels()->GetTypedText();
                Record(FString::Printf(TEXT("chatbar %d: the typed text reached the box"), Value),
                    Controller->GetPanels()->IsTextOpen() && Typed == TEXT("hello world") &&
                        (Value == 0 || (S.bChatOpen && S.ChatInput == TEXT("hello world"))),
                    FString::Printf(TEXT("open %d, typed '%s', hud open %d, hud input '%s', caret %d"),
                        Controller->GetPanels()->IsTextOpen(), *Typed, S.bChatOpen, *S.ChatInput, S.CaretIndex));
            });
        // The capture follows the typing by a few frames: a screenshot shows the frame before its request's tick.
        AddCase(
            FString::Printf(TEXT("chatbar %d: capture"), Value), .3,
            [Value, FeelShot] { FeelShot(Value ? TEXT("chatbar-1") : TEXT("chatbar-0")); }, [] {});
        AddCase(
            FString::Printf(TEXT("chatbar %d: close"), Value), .2,
            [this, SetChatSwitch, SavedChatSwitch, Value]
            {
                Controller->GetPanels()->Close();
                if (Value == 0) SetChatSwitch(*SavedChatSwitch);
            },
            [this, Value]
            {
                Record(FString::Printf(TEXT("chatbar %d: closed"), Value), !Controller->GetPanels()->IsTextOpen(),
                    FString::Printf(TEXT("open %d"), Controller->GetPanels()->IsTextOpen()));
            });
    }
    AddCase(
        TEXT("feel: restore"), .2,
        [this, Previous, SavedMode, FeelSetup]
        {
            FeelSetup(*Previous);
            Pawn->Vitals.Mode = *SavedMode;
            Controller->ClearChat();
        },
        [] {});
    AddCase(
        TEXT("death: restore"), .2,
        [this, SetSwitch, Player, Previous, SavedInventory, SavedMode, SavedOptions, bSavedHideHud]
        {
            SetSwitch(*Previous);
            Controller->ClearChat();
            Controller->GetHud()->bHideHud = *bSavedHideHud;
            Pawn->Vitals.Mode = *SavedMode;
            if (FFoundationAuthority* Authority = Pawn->GetAuthority())
            {
                if (SavedOptions->IsSet()) Authority->Options = SavedOptions->GetValue();
                Authority->RemoveItemEntities();
                Authority->RemoveExperienceOrbs();
            }
            if (FFoundationPlayer* P = Player(); P && SavedInventory->IsSet())
            {
                const int32 Revision = P->Inventory.Revision;
                P->Inventory = SavedInventory->GetValue();
                P->Inventory.Revision = Revision + 1;
            }
        },
        [] {});
#endif
}
