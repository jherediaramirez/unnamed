#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationExperienceOrbs.h"
#include "Foundation/FoundationInteractionSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
// Survival step 12: experience orbs. Values are literals from the value report (evidence ids in the test names'
// comments), never the constants under test.
namespace ExperienceOrbTestsLocal
{
    // The project's rules and blocks, a floor at z 0, one survival player with the switch on.
    struct FOrbLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        int32 Player = 0;
        uint32 Sequence = 1;
        int64 StepIndex = 0;

        FOrbLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::Build(
                FFoundationBlockRegistry::GetDefault()->GetAssets(), TArray<FFoundationBlockDefinition>()));
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            FFoundationStatic Floor;
            Floor.Box = FBox(FVector(-2000, -2000, -100), FVector(2000, 2000, 0));
            Floor.Label = TEXT("floor");
            Floor.bAnchor = true;
            A.Statics.Add(Floor);
            A.Options.bInfiniteSupply = false;
            A.Options.bExperienceOrbs = true;
            Player = A.AddPlayer();
            A.Reset();
            Vitals.bNaturalRegeneration = false;
            Stand(FVector(-1500, -1500, 0));
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }

        void Stand(FVector Feet, FVector Aim = FVector(1, 0, 0))
        {
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = Feet;
            Facts.HeightCm = 180;
            Facts.EyeCm = Feet + FVector(0, 0, 162);
            Facts.Aim = Aim.GetSafeNormal();
            Facts.bGrounded = true;
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
        }

        void NextStep()
        {
            FFoundationPlayerFacts Facts = P().Facts;
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
            A.Step();
        }

        void Steps(int32 Count)
        {
            for (int32 Index = 0; Index < Count; ++Index)
                NextStep();
        }

        void Hold(FName Item)
        {
            P().Inventory.SetSlot(0, P().Inventory.MakeStack(A.Rules.FindItem(Item), 1));
            P().Inventory.Select(0);
        }

        FFoundationResult Run(EFoundationCommandType Type)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Type = Type;
            Command.Hand = EFoundationHand::Main;
            Command.Item = P().Inventory.GetHand(EFoundationHand::Main).GetRef();
            Command.Target = A.Resolve(P().Facts);
            Command.Sequence = Sequence++;
            return A.Execute(Player, Command, Vitals);
        }

        // Puts Block at the cell (0, 0, 0), aims at it from 1.5 blocks away and mines it; whether it broke.
        bool Break(FName Block)
        {
            const FFoundationBlockType* Type = A.World.FindBlock(Block);
            if (!Type) return false;
            A.World.Set(FIntVector::ZeroValue, Type->GetDefaultState());
            Stand({-150, 50, 0}, FVector(150, 0, -112));
            FFoundationResult R = Run(EFoundationCommandType::MineStart);
            for (int32 Step = 0; Step < 2000 && !R.bBroken && R.IsCommitted(); ++Step)
            {
                NextStep();
                R = Run(EFoundationCommandType::MineContinue);
            }
            return R.bBroken;
        }

        // The points in orbs, plus those already taken but not claimed.
        int32 OrbTotal() const
        {
            int32 Total = A.FindPlayer(Player)->PendingExperience;
            for (const FFoundationExperienceOrb& Orb : A.GetExperienceOrbs())
                Total += Orb.Value * Orb.Count;
            return Total;
        }

        // One resting orb of Value at a position (cm, the floor at z 0), its id (Id if given); not new unless bNew.
        int32 Put(const FVector& At, int32 Value, int32 Id = 0, bool bNew = false)
        {
            const int32 Before = A.GetExperienceOrbs().Num();
            A.SpawnExperience(At + FVector(0, 0, 25), Value, 1);
            if (A.GetExperienceOrbs().Num() != Before + 1) return 0;
            FFoundationExperienceOrb& Orb = const_cast<FFoundationExperienceOrb&>(A.GetExperienceOrbs().Last());
            Orb.LocalCm = Orb.PreviousLocalCm = At;
            Orb.VelocityCm = FVector::ZeroVector;
            Orb.Id = Id > 0 ? Id : Orb.Id;
            Orb.bNew = bNew;
            return Orb.Id;
        }

        const FFoundationExperienceOrb* Find(int32 Id) const
        {
            return A.GetExperienceOrbs().FindByPredicate(
                [Id](const FFoundationExperienceOrb& O) { return O.Id == Id; });
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationExperienceOrbRulesTest, "Unnamed.Foundation.Items.Xp.Rules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationExperienceOrbRulesTest::RunTest(const FString&)
{
    using namespace FoundationExperienceOrbs;
    // xp.value_icons: thresholds 1, 3, 7, 17, 37, 73, 149, 307, 617, 1237, 2477.
    for (const TTuple<int32, int32>& Case : TArray<TTuple<int32, int32>>{{1, 0}, {2, 0}, {3, 1}, {6, 1}, {7, 2},
             {16, 2}, {17, 3}, {36, 3}, {37, 4}, {72, 4}, {73, 5}, {148, 5}, {149, 6}, {306, 6}, {307, 7}, {616, 7},
             {617, 8}, {1236, 8}, {1237, 9}, {2476, 9}, {2477, 10}, {99999, 10}})
        TestEqual(*FString::Printf(TEXT("icon for %d"), Case.Get<0>()), GetIcon(Case.Get<0>()), Case.Get<1>());
    for (const TTuple<int32, int32>& Case :
        TArray<TTuple<int32, int32>>{{1, 1}, {2, 1}, {3, 3}, {6, 3}, {7, 7}, {20, 17}, {40, 37}, {3000, 2477}})
        TestEqual(*FString::Printf(TEXT("split of %d"), Case.Get<0>()), GetSplitValue(Case.Get<0>()), Case.Get<1>());
    // xp.value: R = floor((sin(t/2) + 1) x 0.5 x 255), G 255, B = floor((sin(t/2 + 4 pi/3) + 1) x 0.1 x 255), alpha
    // 128.
    TestEqual(TEXT("colour at 0"), GetColor(0), FColor(127, 255, 3, 128));
    TestEqual(TEXT("colour at pi"), GetColor(UE_DOUBLE_PI), FColor(255, 255, 12, 128));
    // xp.ore_drops: coal 0-2; diamond and emerald 3-7; lapis and quartz 2-5; redstone 1-5; nether gold 0-1; iron,
    // copper and gold 0 (no range).
    for (const TTuple<FName, int32, int32>& Case : TArray<TTuple<FName, int32, int32>>{{TEXT("coal_ore"), 0, 2},
             {TEXT("deepslate_coal_ore"), 0, 2}, {TEXT("diamond_ore"), 3, 7}, {TEXT("deepslate_diamond_ore"), 3, 7},
             {TEXT("emerald_ore"), 3, 7}, {TEXT("deepslate_emerald_ore"), 3, 7}, {TEXT("lapis_ore"), 2, 5},
             {TEXT("deepslate_lapis_ore"), 2, 5}, {TEXT("nether_quartz_ore"), 2, 5}, {TEXT("redstone_ore"), 1, 5},
             {TEXT("deepslate_redstone_ore"), 1, 5}, {TEXT("nether_gold_ore"), 0, 1}})
    {
        int32 Min = -1, Max = -1;
        TestTrue(*FString::Printf(TEXT("%s pays %d-%d"), *Case.Get<0>().ToString(), Case.Get<1>(), Case.Get<2>()),
            GetBlockExperience(Case.Get<0>(), Min, Max) && Min == Case.Get<1>() && Max == Case.Get<2>());
    }
    for (const TCHAR* Block : {TEXT("iron_ore"), TEXT("copper_ore"), TEXT("gold_ore"), TEXT("stone")})
    {
        int32 Min = -1, Max = -1;
        TestFalse(*FString::Printf(TEXT("%s pays nothing"), Block), GetBlockExperience(Block, Min, Max));
    }
    // xp.attraction: normalize(delta) x (1 - |delta| / 8)^2 x 0.1 blocks per tick, none from 8 blocks.
    TestTrue(TEXT("pull at 4 blocks: 0.025 blocks"),
        GetAttraction(FVector::ZeroVector, FVector(400, 0, 0)).Equals(FVector(2.5, 0, 0), 1e-9));
    TestTrue(TEXT("pull at 7.99 blocks"), GetAttraction(FVector::ZeroVector, FVector(0, 799, 0)).Y > 0);
    TestTrue(TEXT("no pull at 8 blocks"), GetAttraction(FVector::ZeroVector, FVector(0, 0, 800)).IsZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationExperienceOrbOreTest, "Unnamed.Foundation.Items.Xp.Ore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationExperienceOrbOreTest::RunTest(const FString&)
{
    using namespace ExperienceOrbTestsLocal;
    {
        // Forty diamond ore breaks with an iron pickaxe each pay 3 to 7, both ends seen (xp.ore_drops).
        FOrbLab L;
        L.Hold(TEXT("iron_pickaxe"));
        int32 Low = MAX_int32, High = MIN_int32, Orbs = 0;
        bool bAllBroke = true;
        for (int32 Index = 0; Index < 40; ++Index)
        {
            const int32 Before = L.OrbTotal();
            bAllBroke &= L.Break(TEXT("diamond_ore"));
            const int32 Paid = L.OrbTotal() - Before;
            Low = FMath::Min(Low, Paid);
            High = FMath::Max(High, Paid);
        }
        Orbs = L.A.GetExperienceOrbs().Num();
        TestTrue(TEXT("diamond: every break"), bAllBroke);
        TestEqual(TEXT("diamond: lowest 3"), Low, 3);
        TestEqual(TEXT("diamond: highest 7"), High, 7);
        TestTrue(TEXT("diamond: orbs counted"), L.A.OrbsSpawned >= 40 && Orbs > 0);
        for (const FFoundationExperienceOrb& Orb : L.A.GetExperienceOrbs())
            if (!TestTrue(TEXT("orb values are 1, 3 or 7"), Orb.Value == 1 || Orb.Value == 3 || Orb.Value == 7)) break;
    }
    {
        // Iron ore pays nothing; nor does diamond ore broken by hand (it cannot harvest it; with a pickaxe it always
        // pays).
        FOrbLab L;
        L.Hold(TEXT("iron_pickaxe"));
        TestTrue(TEXT("iron ore broke"), L.Break(TEXT("iron_ore")));
        L.P().Inventory.SetSlot(0, {});
        TestTrue(TEXT("diamond ore broke by hand"), L.Break(TEXT("diamond_ore")));
        TestEqual(TEXT("no orbs"), L.A.GetExperienceOrbs().Num(), 0);
    }
    {
        // Switch off: no orbs.
        FOrbLab L;
        L.A.Options.bExperienceOrbs = false;
        L.Hold(TEXT("iron_pickaxe"));
        TestTrue(TEXT("off: broke"), L.Break(TEXT("diamond_ore")));
        TestEqual(TEXT("off: no orbs"), L.A.GetExperienceOrbs().Num(), 0);
    }
    {
        // The infinite supply (creative) breaks pay nothing.
        FOrbLab L;
        L.A.Options.bInfiniteSupply = true;
        L.A.Reset();
        L.Hold(TEXT("iron_pickaxe"));
        TestTrue(TEXT("infinite supply: broke"), L.Break(TEXT("diamond_ore")));
        TestEqual(TEXT("infinite supply: no orbs"), L.A.GetExperienceOrbs().Num(), 0);
    }
    {
        // A total splits into the orb values, largest first; the same seed spawns the same orbs.
        FOrbLab L, M;
        TestEqual(TEXT("20 is two orbs"), L.A.SpawnExperience(FVector(0, 0, 50), 20, 7), 2);
        M.A.SpawnExperience(FVector(0, 0, 50), 20, 7);
        const TArray<FFoundationExperienceOrb>& Orbs = L.A.GetExperienceOrbs();
        TestTrue(TEXT("17 then 3"), Orbs.Num() == 2 && Orbs[0].Value == 17 && Orbs[1].Value == 3);
        TestTrue(TEXT("deterministic"),
            M.A.GetExperienceOrbs().Num() == 2 && M.A.GetExperienceOrbs()[0].LocalCm == Orbs[0].LocalCm &&
                M.A.GetExperienceOrbs()[1].VelocityCm == Orbs[1].VelocityCm);
        TestEqual(TEXT("0 is no orb"), L.A.SpawnExperience(FVector::ZeroVector, 0, 7), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationExperienceOrbPickupTest, "Unnamed.Foundation.Items.Xp.Pickup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationExperienceOrbPickupTest::RunTest(const FString&)
{
    using namespace ExperienceOrbTestsLocal;
    {
        // An orb 4 blocks away is pulled in and taken; the points reach the vitals through TakeExperience.
        FOrbLab L;
        L.Stand(FVector(0, 0, 0));
        const int32 Id = L.Put(FVector(400, 0, 0), 7);
        L.NextStep();
        const FFoundationExperienceOrb* Orb = L.Find(Id);
        TestTrue(TEXT("pulled toward the player"), Orb && Orb->VelocityCm.X < 0);
        int32 Steps = 1;
        while (L.Find(Id) && Steps < 200)
        {
            L.NextStep();
            ++Steps;
        }
        TestFalse(TEXT("taken"), L.Find(Id) != nullptr);
        TestEqual(TEXT("OrbsPickedUp"), L.A.OrbsPickedUp, 1);
        const FFoundationEvent& Event = L.A.GetEvents().Last();
        TestTrue(TEXT("the event"),
            Event.Kind == EFoundationEventKind::ExperiencePickedUp && Event.Count == 7 && Event.EntityId == Id &&
                Event.bWholeStack);
        const int32 Points = L.A.TakeExperience(L.Player);
        TestEqual(TEXT("7 points"), Points, 7);
        TestEqual(TEXT("taken once"), L.A.TakeExperience(L.Player), 0);
        L.Vitals.AddExperience(Points);
        TestTrue(TEXT("level 1, no progress (level 0 needs 7)"), L.Vitals.Level == 1 && L.Vitals.Progress == 0);
    }
    {
        // 8 blocks away (feet to orb): not followed, it stays.
        FOrbLab L;
        L.Stand(FVector(0, 0, 0));
        const int32 Id = L.Put(FVector(800, 0, 0), 1);
        L.Steps(19);
        const FFoundationExperienceOrb* Orb = L.Find(Id);
        TestTrue(TEXT("8 blocks: not pulled"), Orb && Orb->LocalCm.X == 800);
    }
    {
        // 7.9 blocks away: followed on the first step.
        FOrbLab L;
        L.Stand(FVector(0, 0, 0));
        const int32 Id = L.Put(FVector(790, 0, 0), 1);
        L.NextStep();
        const FFoundationExperienceOrb* Orb = L.Find(Id);
        TestTrue(TEXT("7.9 blocks: pulled"), Orb && Orb->LocalCm.X < 790);
    }
    {
        // One orb every 2 steps: three orbs at the feet go on steps 1, 3 and 5.
        FOrbLab L;
        L.Stand(FVector(0, 0, 0));
        for (const double X : {0., 10., 20.})
            L.Put(FVector(X, 0, 0), 1);
        TArray<int32> Left;
        for (int32 Step = 1; Step <= 6; ++Step)
        {
            L.NextStep();
            Left.Add(L.A.GetExperienceOrbs().Num());
        }
        TestTrue(TEXT("left after each step: 2, 2, 1, 1, 0, 0"), Left == TArray<int32>{2, 2, 1, 1, 0, 0});
    }
    {
        // A merged orb (count 2) is taken one value at a time.
        FOrbLab L;
        const int32 Id = L.Put(FVector(0, 0, 0), 3, 1);
        L.Put(FVector(30, 0, 0), 3, 41);
        L.Steps(20);
        const FFoundationExperienceOrb* Orb = L.Find(Id);
        TestTrue(TEXT("merged into a count of 2"), L.A.GetExperienceOrbs().Num() == 1 && Orb && Orb->Count == 2);
        L.Stand(FVector(0, 0, 0));
        L.NextStep();
        TestTrue(TEXT("one taken, the orb stays"),
            L.Find(Id) && L.Find(Id)->Count == 1 && !L.A.GetEvents().Last().bWholeStack);
        L.Steps(2);
        TestFalse(TEXT("the second taken"), L.Find(Id) != nullptr);
        TestEqual(TEXT("6 points"), L.A.TakeExperience(L.Player), 6);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationExperienceOrbMergeTest, "Unnamed.Foundation.Items.Xp.Merge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationExperienceOrbMergeTest::RunTest(const FString&)
{
    using namespace ExperienceOrbTestsLocal;
    {
        // Same value inside the box, ids 40 apart: the scan runs at tickCount % 20 == 1 (R23, survival2.r23.xp_rules),
        // so two fresh orbs merge on their first step.
        FOrbLab L;
        L.Put(FVector(0, 0, 0), 3, 1);
        L.Put(FVector(60, 0, 0), 3, 41);
        L.NextStep();
        TestTrue(TEXT("fresh orbs: merged on step 1"),
            L.A.GetExperienceOrbs().Num() == 1 && L.A.GetExperienceOrbs()[0].Count == 2 && L.A.OrbsMerged == 1);
    }
    {
        // Past their first step (tickCount 1), the next scan is at tickCount 21: 20 steps later, not before.
        FOrbLab L;
        for (const int32 Id : {L.Put(FVector(0, 0, 0), 3, 1), L.Put(FVector(60, 0, 0), 3, 41)})
            const_cast<FFoundationExperienceOrb*>(L.Find(Id))->Ticks = 1;
        L.Steps(19);
        TestEqual(TEXT("not at tickCount 2 to 20"), L.A.GetExperienceOrbs().Num(), 2);
        L.NextStep();
        TestTrue(TEXT("merged at tickCount 21"),
            L.A.GetExperienceOrbs().Num() == 1 && L.A.GetExperienceOrbs()[0].Count == 2 && L.A.OrbsMerged == 1);
    }
    for (const TTuple<int32, bool>& Case : TArray<TTuple<int32, bool>>{{2, false}, {21, false}, {81, true}})
    {
        // Codex review 8 #1 (Chinese wiki, Merging): old orbs merge only when their ids differ by a multiple of 40.
        FOrbLab L;
        L.Put(FVector(0, 0, 0), 3, 1);
        L.Put(FVector(30, 0, 0), 3, Case.Get<0>());
        L.Steps(40);
        TestEqual(*FString::Printf(TEXT("ids 1 and %d"), Case.Get<0>()), L.A.GetExperienceOrbs().Num(),
            Case.Get<1>() ? 1 : 2);
    }
    {
        // A new orb may merge whatever the ids, on a 2.5% roll: about 25 of 1,000 pairs, each the same every run.
        int32 Merges = 0;
        bool bStable = true;
        FFoundationExperienceOrb Seeker, Other;
        Seeker.bNew = true;
        for (int32 Id = 2; Id < 1002; ++Id)
        {
            Seeker.Id = Id;
            Other.Id = Id % 40 == 1 ? 1000000 : 1;
            const bool bMerge = FoundationExperienceOrbs::CanMerge(Seeker, Other);
            Merges += bMerge ? 1 : 0;
            bStable &= bMerge == FoundationExperienceOrbs::CanMerge(Seeker, Other);
        }
        TestTrue(*FString::Printf(TEXT("new orb merges %d of 1000 (10 to 45)"), Merges), Merges >= 10 && Merges <= 45);
        TestTrue(TEXT("the roll is a function of the pair"), bStable);
        Seeker.bNew = false;
        bool bNone = true;
        for (int32 Id = 2; Id < 1002; ++Id)
        {
            Seeker.Id = Id;
            Other.Id = Id % 40 == 1 ? 1000000 : 1;
            bNone &= !FoundationExperienceOrbs::CanMerge(Seeker, Other);
        }
        TestTrue(TEXT("an old orb never rolls"), bNone);
    }
    {
        // A new orb scans in the step it spawned: the pair (ids 1, 81) merges on step 1 when one is new.
        FOrbLab L;
        L.Put(FVector(0, 0, 0), 3, 1, true);
        L.Put(FVector(30, 0, 0), 3, 81);
        L.NextStep();
        TestEqual(TEXT("new orb's first-step scan"), L.A.GetExperienceOrbs().Num(), 1);
    }
    {
        // Codex review 14, finding 1 (R23, survival2.r23.xp_rules): a new orb (id 81, the seeker) finding an old one
        // (id 1) goes into it, so the merged orb is id 1 where id 1 was, its age reset to 0.
        FOrbLab L;
        L.Put(FVector(0, 0, 0), 3, 1);
        L.Put(FVector(30, 0, 0), 3, 81, true);
        const_cast<FFoundationExperienceOrb&>(L.A.GetExperienceOrbs()[0]).Age = 7;
        L.NextStep();
        const TArray<FFoundationExperienceOrb>& Orbs = L.A.GetExperienceOrbs();
        TestTrue(TEXT("lower id: one orb, id 1, count 2"), Orbs.Num() == 1 && Orbs[0].Id == 1 && Orbs[0].Count == 2);
        TestTrue(TEXT("lower id: where id 1 was"), Orbs.Num() == 1 && FMath::Abs(Orbs[0].LocalCm.X) < 5);
        TestTrue(TEXT("lower id: age 0"), Orbs.Num() == 1 && Orbs[0].Age == 0);
    }
    {
        // An old orb's own scan keeps the scanner (R23, survival2.r23.xp_merge: the caller stays, the other goes):
        // id 41, listed first, scans first on step 1 (tickCount 1) and takes in id 1, staying where it was.
        FOrbLab L;
        L.Put(FVector(30, 0, 0), 3, 41);
        L.Put(FVector(0, 0, 0), 3, 1);
        L.Steps(20);
        const TArray<FFoundationExperienceOrb>& Orbs = L.A.GetExperienceOrbs();
        TestTrue(TEXT("scan: id 41 stays, count 2"), Orbs.Num() == 1 && Orbs[0].Id == 41 && Orbs[0].Count == 2);
        TestTrue(TEXT("scan: where id 41 was"), Orbs.Num() == 1 && FMath::Abs(Orbs[0].LocalCm.X - 30) < 5);
    }
    for (const TTuple<double, int32, double, int32>& Case :
        TArray<TTuple<double, int32, double, int32>>{{30, 41, 30, 0}, {80, 1, 0, 1}})
    {
        // R23 (survival2.r23.xp_rules): newly awarded XP searches a 1 x 1 x 1 box around its position. New id 1 at
        // x 0 and old id 41 (past its scan, tickCount 5): at x 30 (inside the box) the new XP goes into id 41, age 0;
        // at x 80 (outside it, inside the merge box grown 0.5) the new orb's own first-step scan keeps id 1.
        FOrbLab L;
        L.Put(FVector(0, 0, 0), 3, 1, true);
        FFoundationExperienceOrb* Old =
            const_cast<FFoundationExperienceOrb*>(L.Find(L.Put(FVector(Case.Get<0>(), 0, 0), 3, 41)));
        Old->Ticks = Old->Age = 5;
        L.NextStep();
        const TArray<FFoundationExperienceOrb>& Orbs = L.A.GetExperienceOrbs();
        const FString At = FString::Printf(TEXT("old orb at x %g: "), Case.Get<0>());
        TestTrue(At + TEXT("one orb, the expected id"),
            Orbs.Num() == 1 && Orbs[0].Id == Case.Get<1>() && Orbs[0].Count == 2);
        TestTrue(At + TEXT("where it was"), Orbs.Num() == 1 && FMath::Abs(Orbs[0].LocalCm.X - Case.Get<2>()) < 5);
        TestTrue(At + TEXT("its age"), Orbs.Num() == 1 && Orbs[0].Age == Case.Get<3>());
    }
    {
        // Only the reward's search rolls: a new orb S whose roll passes against id 1 (ids not 40 apart) takes it in
        // inside the 1 x 1 x 1 box (x 30), but at x 80 its own first-step scan needs the id rule and leaves it.
        FFoundationExperienceOrb Seeker, Other;
        Seeker.bNew = true;
        Other.Id = 1;
        for (Seeker.Id = 2; Seeker.Id < 10000; ++Seeker.Id)
            if ((Seeker.Id - 1) % FoundationExperienceOrbs::MergeIdPeriod != 0 &&
                FoundationExperienceOrbs::CanMerge(Seeker, Other))
                break;
        if (!TestTrue(TEXT("a rolling seeker id exists"), Seeker.Id < 10000)) return false;
        for (const double X : {30., 80.})
        {
            FOrbLab L;
            L.Put(FVector(0, 0, 0), 3, Seeker.Id, true);
            FFoundationExperienceOrb* Old =
                const_cast<FFoundationExperienceOrb*>(L.Find(L.Put(FVector(X, 0, 0), 3, 1)));
            Old->Ticks = Old->Age = 5;
            L.NextStep();
            TestEqual(*FString::Printf(TEXT("rolling new orb %d, old orb at x %g"), Seeker.Id, X),
                L.A.GetExperienceOrbs().Num(), X < 50 ? 1 : 2);
        }
    }
    {
        // Through the roll: the first seeker id from 2 (against id 1, not 40 apart) whose roll passes. In reach on the
        // first step it merges; out of reach then and moved in before the second, it does not (the flag has cleared).
        FFoundationExperienceOrb Seeker, Other;
        Seeker.bNew = true;
        Other.Id = 1;
        int32 Rolled = 0;
        for (int32 Id = 2; Id < 2000 && Rolled == 0; ++Id)
        {
            Seeker.Id = Id;
            Rolled = Id % 40 != 1 && FoundationExperienceOrbs::CanMerge(Seeker, Other) ? Id : 0;
        }
        if (!TestTrue(TEXT("a rolled pair exists"), Rolled > 0)) return false;
        FOrbLab L;
        L.Put(FVector(30, 0, 0), 3, 1);
        L.Put(FVector(0, 0, 0), 3, Rolled, true);
        L.NextStep();
        TestEqual(TEXT("the roll merges a new orb"), L.A.GetExperienceOrbs().Num(), 1);
        FOrbLab M;
        const int32 Far = M.Put(FVector(600, 0, 0), 3, 1);
        M.Put(FVector(0, 0, 0), 3, Rolled, true);
        M.NextStep();
        const_cast<FFoundationExperienceOrb*>(M.Find(Far))->LocalCm = FVector(30, 0, 0);
        M.NextStep();
        TestEqual(TEXT("no roll on the second step"), M.A.GetExperienceOrbs().Num(), 2);
    }
    for (const TTuple<double, bool>& Case : TArray<TTuple<double, bool>>{{99., true}, {100., false}})
    {
        // The box (0.5 blocks) grown 0.5 on every axis: 99 cm apart merge, 100 (touching) do not.
        FOrbLab L;
        L.Put(FVector(0, 0, 0), 3, 1);
        L.Put(FVector(Case.Get<0>(), 0, 0), 3, 41);
        L.Steps(40);
        TestEqual(*FString::Printf(TEXT("%.0f cm apart"), Case.Get<0>()), L.A.GetExperienceOrbs().Num(),
            Case.Get<1>() ? 1 : 2);
    }
    {
        // Different values never merge.
        FOrbLab L;
        L.Put(FVector(0, 0, 0), 3, 1);
        L.Put(FVector(30, 0, 0), 1, 41);
        L.Steps(40);
        TestEqual(TEXT("different values"), L.A.GetExperienceOrbs().Num(), 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationExperienceOrbDespawnTest, "Unnamed.Foundation.Items.Xp.Despawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationExperienceOrbDespawnTest::RunTest(const FString&)
{
    using namespace ExperienceOrbTestsLocal;
    // Gravity 0.03 blocks per tick squared then drag 0.98 (xp.free_flight): one step from rest falls 3 cm, and the
    // speed left is -2.94.
    FOrbLab L;
    const int32 Falling = L.Put(FVector(500, 500, 300), 1);
    L.NextStep();
    const FFoundationExperienceOrb* Orb = L.Find(Falling);
    TestTrue(TEXT("one step's fall"),
        Orb && FMath::IsNearlyEqual(Orb->LocalCm.Z, 297., 1e-9) &&
            FMath::IsNearlyEqual(Orb->VelocityCm.Z, -2.94, 1e-9));
    // Despawn at 6,000 steps of age (provisional, wiki).
    FOrbLab D;
    const int32 Id = D.Put(FVector(0, 0, 0), 1);
    D.Steps(5999);
    TestTrue(TEXT("there at 5,999"), D.Find(Id) != nullptr);
    D.NextStep();
    TestFalse(TEXT("gone at 6,000"), D.Find(Id) != nullptr);
    TestEqual(TEXT("OrbsDespawned"), D.A.OrbsDespawned, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationExperienceOrbBounceTest, "Unnamed.Foundation.Items.Xp.Bounce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationExperienceOrbBounceTest::RunTest(const FString&)
{
    using namespace ExperienceOrbTestsLocal;
    // R23 (survival2.r23.xp_rules): a downward landing whose pre-move speed is below -gravity bounces at 0.4 of it.
    // An orb 1 cm over the floor moving down at 20 cm per step lands at 23 (gravity 3 added) and leaves at 9.2; one
    // landing at 3 (from rest: exactly the gravity) stays down.
    FOrbLab L;
    const int32 Falling = L.Put(FVector(500, 500, 1), 1);
    const int32 Resting = L.Put(FVector(-500, 500, 0), 1);
    const_cast<FFoundationExperienceOrb*>(L.Find(Falling))->VelocityCm = FVector(0, 0, -20);
    L.NextStep();
    const FFoundationExperienceOrb* Orb = L.Find(Falling);
    TestTrue(TEXT("bounced at 0.4 of the landing speed"),
        Orb && FMath::IsNearlyEqual(Orb->LocalCm.Z, 0., 1e-6) && FMath::IsNearlyEqual(Orb->VelocityCm.Z, 9.2, 1e-9));
    L.NextStep();
    Orb = L.Find(Falling);
    TestTrue(TEXT("up the next step"), Orb && Orb->LocalCm.Z > 5);
    const FFoundationExperienceOrb* Still = L.Find(Resting);
    TestTrue(TEXT("a resting orb stays down"),
        Still && FMath::IsNearlyEqual(Still->LocalCm.Z, 0., 1e-6) && Still->VelocityCm.Z == 0 && Still->bOnGround);
    // The threshold's edge (one step of Move against the floor): landing at 3 stays down, at 3.1 leaves at 1.24; on
    // the ground, a 10 cm downward pull lands it at 13, which bounces too (R23 asks only the pre-move speed).
    const TArray<FBox> Floor = {FBox(FVector(-200, -200, -100), FVector(200, 200, 0))};
    const auto Land = [&Floor](double Vz, bool bOnGround, double PushZ)
    {
        FFoundationExperienceOrb O;
        O.LocalCm = O.PreviousLocalCm = FVector(0, 0, bOnGround ? 0 : 1);
        O.VelocityCm = FVector(0, 0, Vz);
        O.bOnGround = bOnGround;
        FoundationExperienceOrbs::Move(O, FVector::ZeroVector, Floor, FVector(0, 0, PushZ));
        return O;
    };
    const FFoundationExperienceOrb Below = Land(0, false, 0), Above = Land(-.1, false, 0);
    TestTrue(TEXT("landing at 3 stays down"), Below.bOnGround && Below.VelocityCm.Z == 0);
    TestTrue(TEXT("landing at 3.1 leaves at 1.24"), FMath::IsNearlyEqual(Above.VelocityCm.Z, 1.24, 1e-9));
    const FFoundationExperienceOrb Pulled = Land(0, true, -10);
    TestTrue(
        TEXT("on the ground under a 10 cm pull: leaves at 5.2"), FMath::IsNearlyEqual(Pulled.VelocityCm.Z, 5.2, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationExperienceOrbSoundsTest, "Unnamed.Foundation.Items.Xp.Sounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationExperienceOrbSoundsTest::RunTest(const FString&)
{
    using namespace FoundationExperienceOrbs;
    // survival.r5.rules: level-up volume 0.75 x min(level / 30, 1) on a gain reaching a multiple of 5, more than 100
    // ticks after the last one.
    TestEqual(TEXT("level 5 plays at 0.125"), GetLevelUpVolume(4, 5, 1000, -1), .125f);
    TestEqual(TEXT("passing 10 plays at 0.25"), GetLevelUpVolume(9, 12, 1000, -1), .25f);
    TestEqual(TEXT("level 40 plays at 0.75"), GetLevelUpVolume(39, 40, 1000, -1), .75f);
    TestEqual(TEXT("4 to 15 in one gain: the first multiple (5) at 0.125"), GetLevelUpVolume(4, 15, 1000, -1), .125f);
    TestEqual(TEXT("no multiple of 5"), GetLevelUpVolume(5, 6, 1000, -1), 0.f);
    TestEqual(TEXT("no gain"), GetLevelUpVolume(10, 5, 1000, -1), 0.f);
    TestEqual(TEXT("the first update"), GetLevelUpVolume(-1, 5, 1000, -1), 0.f);
    TestEqual(TEXT("10 ticks after the last: none"), GetLevelUpVolume(9, 10, 1010, 1000), 0.f);
    TestEqual(TEXT("100 ticks after: none"), GetLevelUpVolume(9, 10, 1100, 1000), 0.f);
    TestEqual(TEXT("101 ticks after: plays"), GetLevelUpVolume(9, 10, 1101, 1000), .25f);
    TestEqual(TEXT("a sound at step 0, 100 after: none"), GetLevelUpVolume(9, 10, 100, 0), 0.f);
    TestEqual(TEXT("a sound at step 0, 101 after: plays"), GetLevelUpVolume(9, 10, 101, 0), .25f);
    TestEqual(TEXT("a step before the last sound's (reset): plays"), GetLevelUpVolume(9, 10, 50, 1000), .25f);
    // Codex review 14, finding 2, and review 27, finding 2: each gain is judged at its own step. A frame at 1102 with
    // 9 -> 10 at 1100 (100 after a sound at 1000: none) and 10 -> 11 at 1102 (no multiple of 5) plays nothing; the
    // whole change 9 -> 11 judged at 1102 would have played.
    {
        int32 LastLevel = 9, LastSerial = 0;
        int64 LastSound = 1000;
        const TArray<FFoundationLevelGain> Gains = {{1100, 9, 10, 1}, {1102, 10, 11, 2}};
        TestEqual(TEXT("two gains in a frame: none"),
            GetLevelUpVolumes(Gains, 11, 1102, LastLevel, LastSound, LastSerial).Num(), 0);
        TestTrue(TEXT("two gains: judged, at level 11"), LastLevel == 11 && LastSerial == 2 && LastSound == 1000);
        TestEqual(TEXT("the whole change at the frame's step would play"), GetLevelUpVolume(9, 11, 1102, 1000), .25f);
        TestEqual(TEXT("the same gains again: nothing new"),
            GetLevelUpVolumes(Gains, 11, 1103, LastLevel, LastSound, LastSerial).Num(), 0);
    }
    {
        // 9 -> 10 at 1101 plays (101 after 1000) and times the next: 14 -> 15 at 1150 does not; a lab-panel level
        // (no gain) is judged at the frame's step; before the first frame nothing plays.
        int32 LastLevel = 9, LastSerial = 0;
        int64 LastSound = 1000;
        const TArray<float> Played =
            GetLevelUpVolumes({{1101, 9, 10, 1}, {1150, 14, 15, 2}}, 15, 1150, LastLevel, LastSound, LastSerial);
        TestTrue(TEXT("one sound, at 0.25, timed 1101"), Played == TArray<float>{.25f} && LastSound == 1101);
        LastLevel = 19;
        const TArray<float> Panel = GetLevelUpVolumes({}, 20, 1300, LastLevel, LastSound, LastSerial);
        TestTrue(TEXT("the lab panel's 19 -> 20 at the frame's step: 0.5"),
            Panel.Num() == 1 && FMath::IsNearlyEqual(Panel[0], .5f, 1e-6f) && LastSound == 1300);
        int32 First = -1, Serial = 0;
        int64 None = -1;
        TestEqual(TEXT("the first frame: none"),
            GetLevelUpVolumes({{5, 4, 5, 1}, {9, 9, 10, 2}}, 10, 9, First, None, Serial).Num(), 0);
        TestTrue(TEXT("the first frame: at level 10, both gains seen"), First == 10 && Serial == 2);
        // Review 19a2: a lab-panel level before a gain in the same frame is judged too (14 -> 15 at the frame's step,
        // then 15 -> 16 at the gain's); a new pawn's serials start again from 1.
        int32 PanelLevel = 14, PanelSerial = 7;
        int64 PanelSound = -1;
        const TArray<float> Both = GetLevelUpVolumes({{50, 15, 16, 1}}, 16, 60, PanelLevel, PanelSound, PanelSerial);
        TestTrue(TEXT("panel 14 -> 15 played at the frame's step, the gain judged"),
            Both.Num() == 1 && PanelSound == 60 && PanelLevel == 16 && PanelSerial == 1);
    }
    // survival.r5.rules: pickup pitch 0.9 + 0.35 x (U - V).
    TestEqual(TEXT("pitch at U = V"), GetPickupPitch(.3f, .3f), .9f);
    TestTrue(TEXT("pitch at U = 1, V = 0"), FMath::IsNearlyEqual(GetPickupPitch(1, 0), 1.25f, 1e-6f));
    TestTrue(TEXT("pitch at U = 0, V = 1"), FMath::IsNearlyEqual(GetPickupPitch(0, 1), .55f, 1e-6f));
    TestTrue(TEXT("pitch at U = 0.5, V = 0.1"), FMath::IsNearlyEqual(GetPickupPitch(.5f, .1f), 1.04f, 1e-6f));
    // survival.r5.play_calls: the pickup volume is 0.1.
    TestEqual(TEXT("pickup volume"), PickupVolume, .1f);
    return true;
}
#endif
