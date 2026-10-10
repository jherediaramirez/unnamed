#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItemAssets.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags MiningTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // One row of the black-box truth table (Reference/minecraft-items-26.3/facts/break_times.json): ticks and drops
    // for the hand and the 35 tools in the table's "tools" order; ticks 0 is instant and -1 unbreakable.
    struct FBreakRow
    {
        FString Id;
        double Hardness = 0;
        bool bRequiresCorrectTool = false;
        TArray<int32> Ticks, Drops;
    };

    // The tool ids in column order ("hand" first) and the rows of the blocks in the block reference.
    bool ReadImportedRows(TArray<FString>& OutTools, TArray<FBreakRow>& OutRows)
    {
        FString Text;
        TSharedPtr<FJsonObject> Table;
        const FString Path = FPaths::ProjectDir() / TEXT("Reference/minecraft-items-26.3/facts/break_times.json");
        if (!FFileHelper::LoadFileToString(Text, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Table) || !Table)
            return false;
        const TArray<TSharedPtr<FJsonValue>>* Blocks = nullptr;
        if (!Table->TryGetStringArrayField(TEXT("tools"), OutTools) || !Table->TryGetArrayField(TEXT("blocks"), Blocks))
            return false;
        for (const TSharedPtr<FJsonValue>& Value : *Blocks)
        {
            const TSharedPtr<FJsonObject> Block = Value->AsObject();
            bool bImported = false;
            if (!Block || !Block->TryGetBoolField(TEXT("imported"), bImported) || !bImported) continue;
            FBreakRow& Row = OutRows.AddDefaulted_GetRef();
            Row.Id = Block->GetStringField(TEXT("id"));
            Row.Hardness = Block->GetNumberField(TEXT("hardness"));
            Row.bRequiresCorrectTool = Block->GetBoolField(TEXT("requires_correct_tool"));
            for (const TSharedPtr<FJsonValue>& Tick : Block->GetArrayField(TEXT("ticks")))
                Row.Ticks.Add(int32(Tick->AsNumber()));
            for (const TSharedPtr<FJsonValue>& Drop : Block->GetArrayField(TEXT("drops")))
                Row.Drops.Add(int32(Drop->AsNumber()));
        }
        return true;
    }

    // Puts an item (INDEX_NONE: nothing, the bare hand) in hotbar slot 1 and selects it.
    void HoldItem(FFoundationPlayer& Player, int32 Item)
    {
        Player.Inventory.SetSlot(
            0, Item == INDEX_NONE ? FFoundationItemInstance() : Player.Inventory.MakeStack(Item, 1));
        Player.Inventory.Select(0);
    }

    // The authority's step count for a block and the held tool, as MineStart and MineContinue decide it: -1
    // unbreakable, 0 the instant path, otherwise GetMiningDelta accumulated until CompletesBreak. Before and After are
    // the progress before and after the last step.
    struct FSteps
    {
        int32 Steps = 0;
        double Before = 0, After = 0;
    };

    FSteps CountSteps(const FFoundationAuthority& A, const FFoundationPlayer& Player, const FFoundationBlockType& Block)
    {
        FSteps Out;
        if (Block.Hardness < 0)
        {
            Out.Steps = -1;
            return Out;
        }
        const double Delta = A.GetMiningDelta(Player, Block);
        if (Block.Hardness == 0 || A.Rules.CompletesBreak(Delta)) return Out;
        double Progress = 0;
        while (!A.Rules.CompletesBreak(Progress) && Out.Steps < 1000000)
        {
            Out.Before = Progress;
            Progress += Delta;
            ++Out.Steps;
        }
        Out.After = Progress;
        return Out;
    }

    // A small lab on the imported blocks: the project's rules, an anchor floor at z 0 and one player standing west of
    // the cell (0, 0, 0) and aiming at its west face (the interaction tests' stance).
    struct FMiningLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        int32 Player = 0;
        uint32 Sequence = 1;
        int64 StepIndex = 0;

        explicit FMiningLab(bool bInfinite)
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            FFoundationStatic Floor;
            Floor.Box = FBox(FVector(-2000, -2000, -100), FVector(2000, 2000, 0));
            Floor.Label = TEXT("floor");
            Floor.bAnchor = true;
            A.Statics.Add(Floor);
            A.Options.bInfiniteSupply = bInfinite;
            A.Options.bToolWear = !bInfinite;
            Player = A.AddPlayer();
            A.Reset();
            Vitals.bNaturalRegeneration = false;
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = FVector(-150, 50, 0);
            Facts.EyeCm = Facts.FeetCm + FVector(0, 0, 162);
            Facts.Aim = FVector(150, 0, -112).GetSafeNormal();
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }

        void NextStep()
        {
            FFoundationPlayerFacts Facts = P().Facts;
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
            A.Step();
        }

        FFoundationResult Run(EFoundationCommandType Type)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = Sequence++;
            Command.Type = Type;
            Command.Hand = EFoundationHand::Main;
            Command.Item = P().Inventory.GetHand(EFoundationHand::Main).GetRef();
            Command.Target = A.Resolve(P().Facts);
            return A.Execute(Player, Command, Vitals);
        }

        // None holds nothing: the bare hand.
        void Hold(FName Item) { HoldItem(P(), Item.IsNone() ? INDEX_NONE : A.Rules.FindItem(Item)); }
        void Put(FName Block) { A.World.Set(FIntVector::ZeroValue, A.World.FindBlock(Block)->GetDefaultState()); }
        bool Solid() const { return !A.World.Get(FIntVector::ZeroValue).State.IsAir(); }

        int32 Count(FName Item)
        {
            const int32 Index = A.Rules.FindItem(Item);
            const FFoundationInventory& Inventory = P().Inventory;
            int32 Total = !Inventory.Offhand.IsEmpty() && Inventory.Offhand.Item == Index ? Inventory.Offhand.Count : 0;
            for (const FFoundationItemInstance& Stack : Inventory.Hotbar)
                if (!Stack.IsEmpty() && Stack.Item == Index) Total += Stack.Count;
            return Total;
        }
    };

    // Steps until the block at the origin breaks through the command path (a start, then one continue per step):
    // 0 instant, -1 unbreakable, -2 anything else.
    int32 MineSteps(FMiningLab& L, int32 Limit)
    {
        FFoundationResult R = L.Run(EFoundationCommandType::MineStart);
        if (R.bBroken) return 0;
        if (!R.IsCommitted()) return R.Reason == EFoundationReason::Unbreakable ? -1 : -2;
        for (int32 Step = 1; Step <= Limit; ++Step)
        {
            R = L.Run(EFoundationCommandType::MineContinue);
            if (R.bBroken) return Step;
            if (!R.IsCommitted()) return -2;
            L.NextStep();
        }
        return -2;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMiningTiersTest, "Unnamed.Foundation.Mining.Tiers", MiningTestFlags)

bool FFoundationMiningTiersTest::RunTest(const FString&)
{
    TArray<FString> Tools;
    TArray<FBreakRow> Rows;
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    if (!TestTrue(TEXT("break_times.json, DA_Items and the block registry"),
            ReadImportedRows(Tools, Rows) && UFoundationItemAssets::LoadDefault() && Registry->GetAssets()))
        return false;
    if (!TestEqual(TEXT("the hand and the 35 tools"), Tools.Num(), 36) ||
        !TestEqual(TEXT("the hand first"), Tools[0], FString(TEXT("hand"))) ||
        !TestEqual(TEXT("618 imported blocks"), Rows.Num(), 618))
        return false;
    FFoundationAuthority A;
    UFoundationInteractionSettings::Get().BuildRules(A.Rules);
    FFoundationPlayer& Player = *A.FindPlayer(A.AddPlayer());
    TArray<int32> Items = {INDEX_NONE};
    for (int32 Column = 1; Column < Tools.Num(); ++Column)
    {
        Items.Add(A.Rules.FindItem(*Tools[Column]));
        TestTrue(Tools[Column] + TEXT(" is an item"), Items.Last() != INDEX_NONE);
    }

    // Every entry: the authority's steps and drop eligibility against the table, and the table against exact arithmetic
    // on the data's decimals (1 / d = hardness x divisor / speed, both scaled by 100 to whole numbers).
    // ExactOne: instant entries with d = 1 exactly; HairInstant: those of them whose float hardness leaves d a hair
    // under 1; FloatUnder: every instant entry whose float d is under 1.
    int32 Entries = 0, Unbreakable = 0, Instant = 0, ExactOne = 0, HairInstant = 0, FloatUnder = 0, Progressive = 0,
          WholeInverse = 0;
    int32 FactErrors = 0, TickErrors = 0, DropErrors = 0, ExactErrors = 0, SingleLonger = 0, SingleShorter = 0;
    int64 TotalTicks = 0;
    // The worst |progress - 1| after a whole-inverse entry's last step, and the least 1 - progress one step before any
    // other entry's last: the completion tolerance must cover the first and stay far below the second.
    double WholeWorst = 0, MarginLeast = 1;
    for (const FBreakRow& Row : Rows)
    {
        const FFoundationBlockType* Block = Registry->FindBlock(*Row.Id);
        if (!TestNotNull(*(Row.Id + TEXT(" in the block registry")), Block) ||
            !TestTrue(Row.Id + TEXT(": a column per tool"), Row.Ticks.Num() == 36 && Row.Drops.Num() == 36))
            continue;
        if (Block->Hardness != float(Row.Hardness) || Block->bRequiresCorrectToolForDrops != Row.bRequiresCorrectTool)
            if (FactErrors++ < 20)
                AddError(FString::Printf(TEXT("%s: hardness %g, needs the tool %d; the table says %g, %d"), *Row.Id,
                    Block->Hardness, Block->bRequiresCorrectToolForDrops, Row.Hardness, Row.bRequiresCorrectTool));
        for (int32 Column = 0; Column < Tools.Num(); ++Column)
        {
            ++Entries;
            HoldItem(Player, Items[Column]);
            const FFoundationItemDefinition& Tool = A.GetHandItem(Player, EFoundationHand::Main);
            const FSteps Steps = CountSteps(A, Player, *Block);
            const int32 Drops = Block->Hardness >= 0 && FFoundationAuthority::CanHarvest(Tool, *Block) ? 1 : 0;
            const int32 Ticks = Row.Ticks[Column];
            if (Steps.Steps != Ticks && TickErrors++ < 20)
                AddError(FString::Printf(
                    TEXT("%s with %s: %d steps, the table says %d"), *Row.Id, *Tools[Column], Steps.Steps, Ticks));
            if (Drops != Row.Drops[Column] && DropErrors++ < 20)
                AddError(FString::Printf(TEXT("%s with %s: drops %d, the table says %d"), *Row.Id, *Tools[Column],
                    Drops, Row.Drops[Column]));
            Unbreakable += Ticks < 0;
            // The sword's instant-mine speed (FLT_MAX) stands in as any speed of at least 10^6.
            const double Speed = Tool.GetMiningSpeed(Block->Tags);
            const int64 Hardness100 = FMath::RoundToInt64(Row.Hardness * 100);
            const int64 Speed100 = Speed >= 1e6 ? MAX_int64 : FMath::RoundToInt64(Speed * 100);
            const int64 Numerator = Hardness100 * (Row.Drops[Column] ? 30 : 100);
            if (Ticks == 0)
            {
                ++Instant;
                // Exactly: hardness 0, or d >= 1.
                if (Hardness100 > 0 && Numerator > Speed100 && ExactErrors++ < 20)
                    AddError(FString::Printf(TEXT("%s with %s: exact %lld / %lld, but the table says instant"), *Row.Id,
                        *Tools[Column], Numerator, Speed100));
                ExactOne += Hardness100 > 0 && Numerator == Speed100;
                const bool bFloatUnder = Hardness100 > 0 && A.GetMiningDelta(Player, *Block) < 1;
                FloatUnder += bFloatUnder;
                HairInstant += bFloatUnder && Numerator == Speed100;
            }
            else if (Ticks > 0)
            {
                ++Progressive;
                TotalTicks += Ticks;
                if (Speed100 <= 0 || Speed100 == MAX_int64 || (Numerator + Speed100 - 1) / Speed100 != Ticks)
                {
                    if (ExactErrors++ < 20)
                        AddError(FString::Printf(TEXT("%s with %s: exact %lld / %lld, the table says %d ticks"),
                            *Row.Id, *Tools[Column], Numerator, Speed100, Ticks));
                }
                else if (Numerator % Speed100 == 0)
                {
                    ++WholeInverse;
                    WholeWorst = FMath::Max(WholeWorst, FMath::Abs(Steps.After - 1));
                }
                else
                    MarginLeast = FMath::Min(MarginLeast, 1 - Steps.Before);
            }
            // For probe P5 (README, "exact arithmetic"): the ticks if the game summed a single-precision d in single
            // precision (wiki "Breaking": "All operations are floating-point"), against the exact table.
            if (Ticks > 0)
            {
                const float Single = float(Tool.GetMiningSpeed(Block->Tags)) / Block->Hardness /
                    (Row.Drops[Column] ? A.Rules.EligibleDivisor : A.Rules.IneligibleDivisor);
                int32 SingleTicks = 0;
                for (float Progress = 0; Progress < 1.f && SingleTicks <= Ticks + 1; Progress += Single)
                    ++SingleTicks;
                SingleLonger += SingleTicks > Ticks;
                SingleShorter += SingleTicks < Ticks;
            }
        }
    }
    TestEqual(TEXT("22,248 entries"), Entries, 22248);
    TestEqual(TEXT("block facts as the table"), FactErrors, 0);
    TestEqual(TEXT("steps as the table"), TickErrors, 0);
    TestEqual(TEXT("drops as the table"), DropErrors, 0);
    TestEqual(TEXT("the table as exact arithmetic"), ExactErrors, 0);
    // Tallies of the hash-checked table: tripwires for a reference rebuild.
    TestEqual(TEXT("unbreakable entries (8 blocks x 36)"), Unbreakable, 288);
    TestEqual(TEXT("instant entries"), Instant, 2672);
    TestEqual(TEXT("progressive entries"), Progressive, 19288);
    TestEqual(TEXT("their ticks"), TotalTicks, int64(3073507));
    // Counted here from the data (exact arithmetic and the float hardness), pinned at items S5.
    TestEqual(TEXT("whole-inverse entries"), WholeInverse, 17305);
    TestEqual(TEXT("instant at d = 1 exactly"), ExactOne, 27);
    TestEqual(TEXT("of which d is a hair under 1 in floats: instant within the tolerance"), HairInstant, 27);
    TestEqual(TEXT("no other instant entry has a float d under 1"), FloatUnder, HairInstant);
    const double Tolerance = A.Rules.CompletionTolerance;
    AddInfo(FString::Printf(TEXT("Whole-inverse entries: %d of %d progressive; worst |progress - 1| at their last step "
                                 "%.3g; least margin before the last step elsewhere %.3g (tolerance %g)"),
        WholeInverse, Progressive, WholeWorst, MarginLeast, Tolerance));
    AddInfo(FString::Printf(TEXT("Single-precision steps would take one tick more on %d entries and fewer on %d (probe "
                                 "P5 decides; the project follows the exact table)"),
        SingleLonger, SingleShorter));
    TestTrue(TEXT("whole-inverse entries reach 1 within a tenth of the tolerance"), WholeWorst < Tolerance / 10);
    TestTrue(TEXT("the tolerance cannot end any other entry a step early"), MarginLeast > Tolerance * 10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMiningScenarioTest, "Unnamed.Foundation.Mining.Scenario", MiningTestFlags)

bool FFoundationMiningScenarioTest::RunTest(const FString&)
{
    TArray<FString> Tools;
    TArray<FBreakRow> Rows;
    if (!TestTrue(TEXT("break_times.json and DA_Items"),
            ReadImportedRows(Tools, Rows) && UFoundationItemAssets::LoadDefault()))
        return false;
    auto FindRow = [&Rows](const TCHAR* Id)
    { return Rows.FindByPredicate([Id](const FBreakRow& Row) { return Row.Id == Id; }); };

    // Iron ore in the finite lab: a wooden pickaxe mines it at its full speed for nothing, a stone one takes it; both
    // wear. The return is the block's drop (the provisional drops-itself rule until S5b's loot tables).
    const FBreakRow* Ore = FindRow(TEXT("iron_ore"));
    const int32 Wooden = Tools.IndexOfByKey(TEXT("wooden_pickaxe")), Stone = Tools.IndexOfByKey(TEXT("stone_pickaxe"));
    if (!TestTrue(TEXT("iron ore and both pickaxes in the table"), Ore && Wooden != INDEX_NONE && Stone != INDEX_NONE))
        return false;
    TestTrue(TEXT("the table: wooden 150 ticks without a drop, stone 23 with one"),
        Ore->Ticks[Wooden] == 150 && Ore->Drops[Wooden] == 0 && Ore->Ticks[Stone] == 23 && Ore->Drops[Stone] == 1);
    for (const int32 Column : {Wooden, Stone})
    {
        FMiningLab L(false);
        L.Hold(*Tools[Column]);
        L.Put(TEXT("iron_ore"));
        const FName Drop = L.A.World.FindBlock(TEXT("iron_ore"))->DropItem;
        const int32 Before = L.Count(Drop);
        const FString What = Tools[Column] + TEXT(" on iron ore: ");
        TestEqual(What + TEXT("steps"), MineSteps(L, 1000), Ore->Ticks[Column]);
        TestFalse(What + TEXT("broken"), L.Solid());
        TestEqual(What + TEXT("returned"), L.Count(Drop) - Before, Ore->Drops[Column]);
        TestEqual(What + TEXT("wear 1"), L.P().Inventory.Hotbar[0].Damage, 1);
    }

    // The command path against the table on full cubes of every kind (shovel, hoe, axe, pickaxe tiers, unbreakable,
    // instant), for the hand and all 35 tools.
    const TCHAR* const Blocks[] = {TEXT("dirt"), TEXT("oak_leaves"), TEXT("oak_log"), TEXT("stone"), TEXT("deepslate"),
        TEXT("iron_ore"), TEXT("diamond_ore"), TEXT("obsidian"), TEXT("bedrock"), TEXT("tnt")};
    int32 Checked = 0, Wrong = 0;
    for (const TCHAR* Id : Blocks)
    {
        const FBreakRow* Row = FindRow(Id);
        if (!TestNotNull(Id, Row)) continue;
        FMiningLab L(true);
        for (int32 Column = 0; Column < Tools.Num(); ++Column)
        {
            L.Hold(Column == 0 ? NAME_None : FName(*Tools[Column]));
            L.Put(Id);
            L.NextStep();
            const int32 Steps = MineSteps(L, 20000);
            ++Checked;
            if (Steps != Row->Ticks[Column] && Wrong++ < 20)
                AddError(FString::Printf(
                    TEXT("%s with %s: %d steps, the table says %d"), Id, *Tools[Column], Steps, Row->Ticks[Column]));
        }
    }
    TestEqual(TEXT("360 entries through the command path"), Checked, 360);
    TestEqual(TEXT("all as the table"), Wrong, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMiningCreativeTest, "Unnamed.Foundation.Mining.Creative", MiningTestFlags)

bool FFoundationMiningCreativeTest::RunTest(const FString&)
{
    const UFoundationItemAssets* Assets = UFoundationItemAssets::LoadDefault();
    if (!TestNotNull(TEXT("DA_Items"), Assets)) return false;
    // The data: can_destroy_blocks_in_creative is false for the seven swords only; the wiki's tag rule adds the spears.
    FFoundationRules Rules;
    UFoundationInteractionSettings::Get().BuildRules(Rules);
    const FGameplayTag Swords = FGameplayTag::RequestGameplayTag(TEXT("Item.Swords"), false);
    const FGameplayTag Spears = FGameplayTag::RequestGameplayTag(TEXT("Item.Spears"), false);
    TestTrue(TEXT("the spear rule"), Spears.IsValid() && Rules.CreativeNoBreakTags.HasTagExact(Spears));
    int32 NoBreak = 0, Copied = 0;
    for (const FFoundationItemAsset& Asset : Assets->Items)
    {
        const int32 Index = Rules.FindItem(Asset.Id);
        if (!Rules.Items.IsValidIndex(Index)) continue;
        const FFoundationItemDefinition& Item = Rules.Items[Index];
        Copied += Item.bCanDestroyBlocksInCreative == Asset.bCanDestroyBlocksInCreative;
        TestEqual(Asset.Id.ToString() + TEXT(": the data's flag only on swords"), !Asset.bCanDestroyBlocksInCreative,
            Item.Tags.HasTagExact(Swords));
        const bool bBreaks = Rules.CanDestroyInCreative(Item);
        NoBreak += !bBreaks;
        TestEqual(Asset.Id.ToString() + TEXT(": breaks in creative unless a sword or a spear"), bBreaks,
            !Item.Tags.HasTagExact(Swords) && !Item.Tags.HasTagExact(Spears));
    }
    TestEqual(TEXT("the flag copied for all 63"), Copied, 63);
    TestEqual(TEXT("7 swords and 7 spears break nothing in creative"), NoBreak, 14);

    // The control: in survival an iron pickaxe takes stone in 8 steps, returns it and wears.
    FMiningLab S(false);
    S.Hold(TEXT("iron_pickaxe"));
    S.Put(TEXT("stone"));
    const int32 Stones = S.Count(TEXT("stone"));
    TestEqual(TEXT("survival: 8 steps"), MineSteps(S, 100), 8);
    TestTrue(TEXT("survival: returned and worn"),
        S.Count(TEXT("stone")) == Stones + 1 && S.P().Inventory.Hotbar[0].Damage == 1);
    // Creative: at once, with no return and no wear (the same finite lab, tool wear on).
    FMiningLab L(false);
    L.Vitals.Mode = EFoundationPlayerMode::Creative;
    L.Hold(TEXT("iron_pickaxe"));
    L.Put(TEXT("stone"));
    const int32 Before = L.Count(TEXT("stone"));
    FFoundationResult R = L.Run(EFoundationCommandType::MineStart);
    TestTrue(TEXT("creative: stone breaks at once"),
        R.IsCommitted() && R.bBroken && R.bInstant && !L.Solid() && !L.P().Mining.bActive);
    TestTrue(TEXT("creative: no return, no wear"),
        L.Count(TEXT("stone")) == Before && !R.bItemReturned && !R.bToolWorn && L.P().Inventory.Hotbar[0].Damage == 0);
    for (const TCHAR* Id : {TEXT("bedrock"), TEXT("obsidian")})
    {
        L.NextStep();
        L.Put(Id);
        R = L.Run(EFoundationCommandType::MineStart);
        TestTrue(FString(TEXT("creative: ")) + Id + TEXT(" breaks at once"), R.bBroken && !L.Solid());
    }
    L.Hold(NAME_None);
    L.NextStep();
    L.Put(TEXT("obsidian"));
    TestTrue(TEXT("creative: the bare hand too"), L.Run(EFoundationCommandType::MineStart).bBroken);
    // A full inventory (the hotbar and the offhand) refuses no creative break, since nothing is returned, and an axe on
    // a log wears nothing.
    const int32 Dirt = L.A.Rules.FindItem(TEXT("dirt"));
    for (int32 Slot = 1; Slot < FFoundationInventory::HotbarSlots; ++Slot)
        L.P().Inventory.SetSlot(Slot, L.P().Inventory.MakeStack(Dirt, 64));
    L.P().Inventory.Offhand = L.P().Inventory.MakeStack(Dirt, 64);
    L.Hold(TEXT("iron_axe"));
    L.NextStep();
    L.Put(TEXT("oak_log"));
    R = L.Run(EFoundationCommandType::MineStart);
    TestTrue(TEXT("creative: an axe on a log with a full inventory breaks it, unworn"),
        R.bBroken && !L.Solid() && L.P().Inventory.Hotbar[0].Damage == 0);
    // A sword or a spear breaks nothing in creative; clearing the tag rule lets the spear break (the wiki's rule is one
    // switch away). In survival the sword mines.
    for (const TCHAR* Item : {TEXT("iron_sword"), TEXT("iron_spear")})
    {
        L.Hold(Item);
        L.NextStep();
        L.Put(TEXT("stone"));
        R = L.Run(EFoundationCommandType::MineStart);
        TestTrue(FString(TEXT("creative: ")) + Item + TEXT(" refused"),
            R.Outcome == EFoundationOutcome::Rejected && R.Reason == EFoundationReason::CreativeCannotBreak &&
                L.Solid() && !L.P().Mining.bActive);
    }
    L.A.Rules.CreativeNoBreakTags.Reset();
    L.NextStep();
    TestTrue(TEXT("creative: the spear breaks without the tag rule"), L.Run(EFoundationCommandType::MineStart).bBroken);
    L.Vitals.Mode = EFoundationPlayerMode::Survival;
    L.Hold(TEXT("iron_sword"));
    L.NextStep();
    L.Put(TEXT("stone"));
    R = L.Run(EFoundationCommandType::MineStart);
    TestTrue(TEXT("survival: the sword starts a timed break"), R.IsCommitted() && !R.bBroken && L.P().Mining.bActive);
    // A survival break that ends after a switch to creative ends as a creative break: no return, no wear.
    FMiningLab Switch(false);
    Switch.Hold(TEXT("iron_pickaxe"));
    Switch.Put(TEXT("stone"));
    const int32 Kept = Switch.Count(TEXT("stone"));
    Switch.Run(EFoundationCommandType::MineStart);
    for (int32 Step = 0; Step < 4; ++Step)
    {
        Switch.Run(EFoundationCommandType::MineContinue);
        Switch.NextStep();
    }
    Switch.Vitals.Mode = EFoundationPlayerMode::Creative;
    R = {};
    for (int32 Step = 0; Step < 10 && !R.bBroken; ++Step)
    {
        R = Switch.Run(EFoundationCommandType::MineContinue);
        Switch.NextStep();
    }
    TestTrue(TEXT("a survival break finished in creative: broken, no return, no wear"),
        R.bBroken && R.MiningSteps == 8 && Switch.Count(TEXT("stone")) == Kept &&
            Switch.P().Inventory.Hotbar[0].Damage == 0);

    // Through the interaction, the steps (of 20) at which a block breaks: holding attack in creative breaks one every
    // 6 (5 of delay, then the next start; wiki "Creative"), clicking breaks one every step, and in survival an instant
    // block (TNT) breaks every step while held. Starts counts the MineStart commands sent.
    auto Breaks = [](EFoundationPlayerMode Mode, const TCHAR* Block, bool bClick,
                      const TCHAR* Item = TEXT("iron_pickaxe"), int32* Starts = nullptr)
    {
        FMiningLab M(true);
        M.Vitals.Mode = Mode;
        M.Hold(Item);
        FFoundationInteraction I;
        I.PlayerId = M.Player;
        I.SetPreset(*UFoundationInteractionSettings::Get().FindPreset(TEXT("VanillaReference")), .05);
        I.Resync(M.A);
        I.SelectSlot(M.A, M.Vitals, 0);
        M.Put(Block);
        TArray<int32> Steps;
        I.Press(EFoundationInput::Attack, 1);
        for (int32 Index = 0; Index < 20; ++Index)
        {
            if (bClick && Index > 0)
            {
                I.Release(EFoundationInput::Attack);
                I.Press(EFoundationInput::Attack, 1);
            }
            I.BeginUpdate();
            M.NextStep();
            I.StepOnce(M.A, M.Vitals, true, 0);
            if (!M.Solid())
            {
                Steps.Add(Index);
                M.Put(Block);
            }
        }
        if (Starts)
            *Starts = I.History
                          .FilterByPredicate([](const FFoundationResult& Result)
                              { return Result.Type == EFoundationCommandType::MineStart; })
                          .Num();
        return Steps;
    };
    const TArray<int32> Held = Breaks(EFoundationPlayerMode::Creative, TEXT("stone"), false);
    TestTrue(FString::Printf(TEXT("creative, held: a break every 6 steps (%s)"),
                 *FString::JoinBy(Held, TEXT(" "), [](int32 Step) { return FString::FromInt(Step); })),
        Held == TArray<int32>{0, 6, 12, 18});
    TestEqual(TEXT("creative, clicked: a break every step"),
        Breaks(EFoundationPlayerMode::Creative, TEXT("stone"), true).Num(), 20);
    TestEqual(TEXT("survival, held on TNT: a break every step"),
        Breaks(EFoundationPlayerMode::Survival, TEXT("tnt"), false).Num(), 20);
    // Held with a sword in creative: refused once, then not asked again while held.
    int32 Starts = 0;
    const bool bNone =
        Breaks(EFoundationPlayerMode::Creative, TEXT("stone"), false, TEXT("iron_sword"), &Starts).IsEmpty();
    TestTrue(FString::Printf(TEXT("creative, held with a sword: no break, one refused start (%d)"), Starts),
        bNone && Starts == 1);
    return true;
}
#endif
