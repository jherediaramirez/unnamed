#pragma once

#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationContainer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace FoundationShiftCraft
{
    // survival2.r23.crafting_rules (SURVIVAL-RUN2-FINDINGS-26.3 R23): a shift-click on the result of 2 oak logs, with
    // hotbar 8 holding Slot8 oak planks. bFull: every other slot of the main grid and hotbar holds 64 dirt (hotbar 0
    // too unless bSlot0Free). The table's columns: the logs and the result left, hotbar 8 and 0 after, the planks
    // thrown.
    struct FCase
    {
        const TCHAR* Name;
        int32 Slot8 = 0;
        bool bFull = false, bSlot0Free = false;
        int32 LogsLeft = 0, ResultLeft = 0, Slot8After = 0, Slot0After = 0, Dropped = 0;
    };
    inline constexpr FCase Cases[] = {
        {TEXT("empty"), 0, false, false, 0, 0, 8, 0, 0},
        {TEXT("merge_then_empty"), 63, true, true, 0, 0, 64, 7, 0},
        {TEXT("one_free_for_four"), 63, true, false, 1, 4, 64, 64, 3},
        {TEXT("three_free_for_four"), 61, true, false, 1, 4, 64, 64, 1},
        {TEXT("full"), 64, true, false, 2, 4, 64, 64, 0},
    };

    // Runs every case in a fresh FLab (item entities on, so the overflow is thrown). Open opens the lab's menu and
    // returns its container id; Grid gives the first grid slot (the logs).
    template <typename FLab, typename FOpen, typename FGrid>
    void Run(FAutomationTestBase& Test, const TCHAR* Menu, FOpen Open, FGrid Grid)
    {
        for (const FCase& Case : Cases)
        {
            FLab L;
            L.A.Options.bItemEntities = true;
            const int32 Container = Open(L);
            FFoundationInventory& I = L.Inv();
            for (int32 Slot = 0; Slot < FFoundationInventory::FirstArmor; ++Slot)
                I.SetSlot(Slot,
                    Case.bFull && Slot != 8 && !(Slot == 0 && Case.bSlot0Free) ? I.MakeStack(L.Item(TEXT("dirt")), 64)
                                                                               : FFoundationItemInstance());
            if (Case.Slot8 > 0) I.SetSlot(8, I.MakeStack(L.Item(TEXT("oak_planks")), Case.Slot8));
            Grid(L) = I.MakeStack(L.Item(TEXT("oak_log")), 2);
            FFoundationCommand Command;
            Command.SessionId = L.A.GetSessionId();
            Command.Sequence = 1000;
            Command.Type = EFoundationCommandType::ContainerClick;
            Command.ContainerId = Container;
            Command.Slot = FFoundationContainerMenu::ResultSlot;
            Command.ClickType = EFoundationClickType::QuickMove;
            const bool bCommitted = L.A.Execute(L.Player, Command, L.Vitals).IsCommitted();
            int32 Dropped = 0;
            for (const FFoundationItemEntity& Entity : L.A.GetItemEntities())
                Dropped += Entity.Item == L.Item(TEXT("oak_planks")) ? Entity.Count : 0;
            const FFoundationItemInstance& Slot0 = L.Inv().Hotbar[0];
            const int32 Slot0Planks = Slot0.IsEmpty() || Slot0.Item != L.Item(TEXT("oak_planks")) ? 0 : Slot0.Count;
            const bool bSlot0 =
                Case.bFull && !Case.bSlot0Free ? L.Holds(Slot0, TEXT("dirt"), 64) : Slot0Planks == Case.Slot0After;
            Test.TestTrue(
                FString::Printf(TEXT("%s %s: logs %d, result %d, hotbar 8 %d, hotbar 0 %d, %d thrown"), Menu, Case.Name,
                    Case.LogsLeft, Case.ResultLeft, Case.Slot8After, Case.Slot0After, Case.Dropped),
                bCommitted && L.Holds(Grid(L), TEXT("oak_log"), Case.LogsLeft) &&
                    L.Holds(L.Menu(FFoundationContainerMenu::ResultSlot), TEXT("oak_planks"), Case.ResultLeft) &&
                    L.Holds(L.Inv().Hotbar[8], TEXT("oak_planks"), Case.Slot8After) && bSlot0 &&
                    Dropped == Case.Dropped);
        }
    }
}
#endif
