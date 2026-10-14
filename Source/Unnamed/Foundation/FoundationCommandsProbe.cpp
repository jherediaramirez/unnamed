#include "Foundation/FoundationRuntimeProbe.h"
#include "Clock/VoxelWorldClock.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationPawn.h"
#include "Hud/HudPresenter.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Voxel/VoxelWorld.h"

// The chat commands (step 15) in the interaction suite (the Playground: no world clock) and the views suite (the
// generated world, with its clock): /give @s apple 3, /time set night and a chat line through the controller's chat,
// with the open chat captured, then the switch-0 control (the old local fixture: no chat list) captured the same way.
// The panel's Enter is a Slate commit, so the cases call the controller's SubmitChat, which that commit calls.
void UFoundationRuntimeProbe::AddCommandsCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    auto SetSwitch = [](int32 Value)
    {
        if (IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Chat.Commands")))
            Switch->Set(Value, ECVF_SetByCode);
    };
    auto Shot = [this](const TCHAR* Name)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-chat-") + Name + TEXT(".png"), true, false);
    };
    auto AppleCount = [this]
    {
        const FFoundationAuthority* Authority = Pawn->GetAuthority();
        const FFoundationPlayer* Player = Authority ? Authority->FindPlayer(Pawn->Interaction.PlayerId) : nullptr;
        if (!Player) return -1;
        const int32 Apple = Authority->Rules.FindItem(TEXT("apple"));
        int32 Count = Player->Inventory.Offhand.Item == Apple ? Player->Inventory.Offhand.Count : 0;
        for (const FFoundationItemInstance& Stack : Player->Inventory.Hotbar)
            if (Stack.Item == Apple) Count += Stack.Count;
        for (const FFoundationItemInstance& Stack : Player->Inventory.Main)
            if (Stack.Item == Apple) Count += Stack.Count;
        return Count;
    };
    auto LastLines = [this](int32 Count)
    {
        const FFoundationChat& Chat = Controller->GetChat();
        TArray<FString> Lines;
        for (int32 Index = FMath::Max(0, Chat.Num() - Count); Index < Chat.Num(); ++Index)
            Lines.Add(Chat.GetText(Index));
        return FString::Join(Lines, TEXT(" | "));
    };
    auto Before = MakeShared<int32>(0);
    auto Previous = MakeShared<int32>(1);
    // The time of day right after /time set night: the clock keeps running under the chat, as vanilla's.
    auto Night = MakeShared<int32>(-1);
    // What the cases change, put back by the restore case: the inventory, the clock, F1 and the chat.
    auto SavedInventory = MakeShared<TOptional<FFoundationInventory>>();
    auto SavedTotal = MakeShared<int64>(-1);
    auto bSavedHideHud = MakeShared<bool>(false);
    AddCase(
        TEXT("chat 1: type the commands"), .5,
        [this, SetSwitch, AppleCount, Before, Previous, Night, SavedInventory, SavedTotal, bSavedHideHud]
        {
            if (const IConsoleVariable* Switch =
                    IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Chat.Commands")))
                *Previous = Switch->GetInt();
            SetSwitch(1);
            *bSavedHideHud = Controller->GetHud()->bHideHud;
            Controller->GetHud()->bHideHud = false;
            if (const FFoundationAuthority* Authority = Pawn->GetAuthority())
                if (const FFoundationPlayer* Player = Authority->FindPlayer(Pawn->Interaction.PlayerId))
                    *SavedInventory = Player->Inventory;
            if (AVoxelWorld* Voxels = AVoxelWorld::Find(GetWorld()))
            {
                *SavedTotal = Voxels->GetClock().GetDayClock().TotalTicks;
                Voxels->GetClock().SetTotal(1000);
            }
            *Before = AppleCount();
            Controller->GetPanels()->OpenText(false);
            Controller->SubmitChat(TEXT("/give @s apple 3"));
            Controller->SubmitChat(TEXT("/time set night"));
            if (const AVoxelWorld* Voxels = AVoxelWorld::Find(GetWorld())) *Night = Voxels->GetClock().GetTimeOfDay();
            Controller->SubmitChat(TEXT("hello from the chat"));
        },
        [this, AppleCount, Before, LastLines, Night]
        {
            Record(TEXT("chat 1: /give @s apple 3 adds three apples"), AppleCount() == *Before + 3,
                FString::Printf(TEXT("%d -> %d"), *Before, AppleCount()));
            const AVoxelWorld* Voxels = AVoxelWorld::Find(GetWorld());
            const FString Expected =
                FString::Printf(TEXT("Gave 3 [Apple] to Player | %s | <Player> hello from the chat"),
                    Voxels ? TEXT("Set minecraft:overworld to time marker minecraft:night")
                           : TEXT("There is no default clock in dimension minecraft:overworld"));
            Record(TEXT("chat 1: vanilla feedback lines"), LastLines(3) == Expected, LastLines(3));
            if (Voxels)
                Record(TEXT("chat 1: /time set night sets the clock to 13000"), *Night == 13000,
                    FString::Printf(
                        TEXT("time of day %d right after, %d now"), *Night, Voxels->GetClock().GetTimeOfDay()));
            const FHudSnapshot& S = Controller->GetHud()->GetPresenter()->GetSnapshot();
            Record(TEXT("chat 1: the open chat shows the lines over the HUD"),
                S.bVisible && S.bChatOpen && S.Chat.Num() >= 3,
                FString::Printf(TEXT("visible %d, open %d, lines %d"), S.bVisible, S.bChatOpen, S.Chat.Num()));
        });
    AddCase(TEXT("chat 1: capture open"), .5, [Shot] { Shot(TEXT("1-open")); }, [] {});
    AddCase(
        TEXT("chat 1: closed, lines stay"), .5,
        [this, Shot]
        {
            Controller->GetPanels()->Close();
            Shot(TEXT("1-closed"));
        },
        [this]
        {
            const FHudSnapshot& S = Controller->GetHud()->GetPresenter()->GetSnapshot();
            Record(TEXT("chat 1: closed chat keeps the recent lines"), S.bVisible && !S.bChatOpen && S.Chat.Num() >= 3,
                FString::Printf(TEXT("lines %d"), S.Chat.Num()));
        });
    AddCase(
        TEXT("chat 0: control"), .5,
        [this, SetSwitch]
        {
            SetSwitch(0);
            Controller->GetPanels()->OpenText(false);
        },
        [this]
        {
            const FHudSnapshot& S = Controller->GetHud()->GetPresenter()->GetSnapshot();
            Record(TEXT("chat 0: the old fixture, no chat list"), S.Chat.IsEmpty() && !S.bChatOpen && !S.bVisible,
                FString::Printf(TEXT("visible %d, open %d, lines %d"), S.bVisible, S.bChatOpen, S.Chat.Num()));
        });
    AddCase(TEXT("chat 0: capture"), .5, [Shot] { Shot(TEXT("0-open")); }, [] {});
    AddCase(
        TEXT("chat: restore"), .2,
        [this, SetSwitch, Previous, SavedInventory, SavedTotal, bSavedHideHud]
        {
            Controller->GetPanels()->Close();
            SetSwitch(*Previous);
            Controller->ClearChat();
            Controller->GetHud()->bHideHud = *bSavedHideHud;
            if (FFoundationAuthority* Authority = Pawn->GetAuthority())
                if (FFoundationPlayer* Player = Authority->FindPlayer(Pawn->Interaction.PlayerId);
                    Player && SavedInventory->IsSet())
                {
                    const int32 Revision = Player->Inventory.Revision;
                    Player->Inventory = SavedInventory->GetValue();
                    Player->Inventory.Revision = Revision + 1;
                }
            if (AVoxelWorld* Voxels = AVoxelWorld::Find(GetWorld()); Voxels && *SavedTotal >= 0)
                Voxels->GetClock().SetTotal(*SavedTotal);
        },
        [] {});
#endif
}
