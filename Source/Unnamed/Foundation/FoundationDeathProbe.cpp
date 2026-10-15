#include "Foundation/FoundationRuntimeProbe.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
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
            *bClicked = Click(EHudDeathButton::Respawn);
            // No Slate viewport: the button's own call, so the remaining checks still run.
            if (Pawn->IsDeathScreenActive()) Pawn->RequestRespawn();
        },
        [this, Spawn, bClicked, GameplayInput]
        {
            Record(TEXT("death 1: the click on Respawn respawns"), *bClicked && !Pawn->Vitals.IsDead(),
                FString::Printf(TEXT("click sent %d"), *bClicked));
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
