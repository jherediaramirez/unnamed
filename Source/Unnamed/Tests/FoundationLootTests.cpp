#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationLoot.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags LootTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Reference/minecraft-items-26.3/loot_tables.json in DA_Items' shape: JSON null or "" is None.
    bool LootTests_ReadTables(
        TArray<FFoundationLootTableAsset>& OutTables, TArray<FFoundationLootPredicateAsset>& OutPredicates)
    {
        FString Text;
        TSharedPtr<FJsonObject> Root;
        const FString Path = FPaths::ProjectDir() / TEXT("Reference/minecraft-items-26.3/loot_tables.json");
        if (!FFileHelper::LoadFileToString(Text, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
            return false;
        for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("predicates")))
        {
            const TSharedPtr<FJsonObject> Object = Value->AsObject();
            FFoundationLootPredicateAsset& Predicate = OutPredicates.AddDefaulted_GetRef();
            Predicate.Name = FName(*Object->GetStringField(TEXT("name")));
            for (const TSharedPtr<FJsonValue>& Item : Object->GetArrayField(TEXT("items")))
                Predicate.Items.Add(FName(*Item->AsString()));
            for (const TSharedPtr<FJsonValue>& Enchantment : Object->GetArrayField(TEXT("enchantments")))
            {
                Predicate.Enchantments.Add(FName(*Enchantment->AsObject()->GetStringField(TEXT("id"))));
                Predicate.EnchantmentMinLevels.Add(int32(Enchantment->AsObject()->GetNumberField(TEXT("min_level"))));
            }
        }
        for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("tables")))
        {
            const TSharedPtr<FJsonObject> Object = Value->AsObject();
            FFoundationLootTableAsset& Table = OutTables.AddDefaulted_GetRef();
            Table.Block = FName(*Object->GetStringField(TEXT("block")));
            for (const TSharedPtr<FJsonValue>& PoolValue : Object->GetArrayField(TEXT("pools")))
            {
                FFoundationLootPoolAsset& Pool = Table.Pools.AddDefaulted_GetRef();
                Pool.Rolls = int32(PoolValue->AsObject()->GetNumberField(TEXT("rolls")));
                for (const TSharedPtr<FJsonValue>& CandidateValue :
                    PoolValue->AsObject()->GetArrayField(TEXT("candidates")))
                {
                    const TSharedPtr<FJsonObject> Entry = CandidateValue->AsObject();
                    FFoundationLootCandidateAsset& Candidate = Pool.Candidates.AddDefaulted_GetRef();
                    Candidate.Item = FName(*Entry->GetStringField(TEXT("item")));
                    for (const TSharedPtr<FJsonValue>& Condition : Entry->GetArrayField(TEXT("conditions")))
                        Candidate.Conditions.Add(FName(*Condition->AsString()));
                    const TArray<TSharedPtr<FJsonValue>>& CountRange = Entry->GetArrayField(TEXT("count"));
                    Candidate.CountMin = int32(CountRange[0]->AsNumber());
                    Candidate.CountMax = int32(CountRange[1]->AsNumber());
                    if (Entry->HasTypedField<EJson::Object>(TEXT("bonus")))
                    {
                        const TSharedPtr<FJsonObject> Bonus = Entry->GetObjectField(TEXT("bonus"));
                        Candidate.BonusFormula = FName(*Bonus->GetStringField(TEXT("formula")));
                        Candidate.BonusEnchantment = FName(*Bonus->GetStringField(TEXT("enchantment")));
                        Candidate.BonusMultiplier = float(Bonus->GetNumberField(TEXT("bonus_multiplier")));
                    }
                    Candidate.bExplosionDecay = Entry->GetBoolField(TEXT("explosion_decay"));
                }
            }
        }
        return true;
    }

    // One row of Reference/minecraft-items-26.3/facts/break_times.json: the drops flag for the hand and the 35 tools.
    struct FLootBreakRow
    {
        FString Id;
        TArray<int32> Drops;
    };

    bool LootTests_ReadRows(TArray<FString>& OutTools, TArray<FLootBreakRow>& OutRows)
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
            FLootBreakRow& Row = OutRows.AddDefaulted_GetRef();
            Row.Id = Block->GetStringField(TEXT("id"));
            for (const TSharedPtr<FJsonValue>& Drop : Block->GetArrayField(TEXT("drops")))
                Row.Drops.Add(int32(Drop->AsNumber()));
        }
        return true;
    }

    // Item counts by item index over the hotbar and the offhand.
    TMap<int32, int32> LootTests_Counts(const FFoundationInventory& Inventory)
    {
        TMap<int32, int32> Counts;
        for (const FFoundationItemInstance& Stack : Inventory.Hotbar)
            if (!Stack.IsEmpty()) Counts.FindOrAdd(Stack.Item) += Stack.Count;
        if (!Inventory.Offhand.IsEmpty()) Counts.FindOrAdd(Inventory.Offhand.Item) += Inventory.Offhand.Count;
        return Counts;
    }

    // What changed between two counts: item index to the difference, zero differences left out.
    TMap<int32, int32> LootTests_Gains(const TMap<int32, int32>& Before, const TMap<int32, int32>& After)
    {
        TMap<int32, int32> Gains;
        for (const TPair<int32, int32>& Pair : After)
            if (const int32 Gain = Pair.Value - Before.FindRef(Pair.Key)) Gains.Add(Pair.Key, Gain);
        for (const TPair<int32, int32>& Pair : Before)
            if (!After.Contains(Pair.Key)) Gains.Add(Pair.Key, -Pair.Value);
        return Gains;
    }

    bool LootTests_Same(const TArray<FFoundationLootDrop>& A, const TArray<FFoundationLootDrop>& B)
    {
        if (A.Num() != B.Num()) return false;
        for (int32 Index = 0; Index < A.Num(); ++Index)
            if (A[Index].Item != B[Index].Item || A[Index].Count != B[Index].Count) return false;
        return true;
    }

    bool LootTests_SameTally(const TMap<FName, int32>& A, const TMap<FName, int32>& B)
    {
        if (A.Num() != B.Num()) return false;
        for (const TPair<FName, int32>& Pair : A)
            if (const int32* Other = B.Find(Pair.Key); !Other || *Other != Pair.Value) return false;
        return true;
    }

    // The mining tests' lab (FoundationMiningTests.cpp's FMiningLab without the target): the project's rules, an
    // anchor floor at z 0 and one player standing west of the cell (0, 0, 0), aiming at its west face.
    struct FLootLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        int32 Player = 0;
        uint32 Sequence = 1;
        int64 StepIndex = 0;

        explicit FLootLab(bool bInfinite)
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

        // Empties the hotbar, then holds Item (None: the bare hand) in slot 1.
        void ClearAndHold(FName Item)
        {
            FFoundationInventory& Inventory = P().Inventory;
            for (int32 Slot = 0; Slot < FFoundationInventory::HotbarSlots; ++Slot)
                Inventory.SetSlot(Slot, FFoundationItemInstance());
            const int32 Index = Item.IsNone() ? INDEX_NONE : A.Rules.FindItem(Item);
            if (Index != INDEX_NONE) Inventory.SetSlot(0, Inventory.MakeStack(Index, 1));
            Inventory.Select(0);
        }

        void Put(FName Block) { A.World.Set(FIntVector::ZeroValue, A.World.FindBlock(Block)->GetDefaultState()); }
        bool Solid() const { return !A.World.Get(FIntVector::ZeroValue).State.IsAir(); }
        int32 Count(FName Item) { return LootTests_Counts(P().Inventory).FindRef(A.Rules.FindItem(Item)); }
    };

    // Mines the block at the origin through the command path (a start, then one continue per step) and returns the
    // result that broke it, or the first that was not committed.
    FFoundationResult LootTests_Mine(FLootLab& L, int32 Limit)
    {
        FFoundationResult R = L.Run(EFoundationCommandType::MineStart);
        if (R.bBroken || !R.IsCommitted()) return R;
        for (int32 Step = 1; Step <= Limit; ++Step)
        {
            R = L.Run(EFoundationCommandType::MineContinue);
            if (R.bBroken || !R.IsCommitted()) return R;
            L.NextStep();
        }
        return R;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationLootTablesTest, "Unnamed.Foundation.Loot.Tables", LootTestFlags)

bool FFoundationLootTablesTest::RunTest(const FString&)
{
    TArray<FFoundationLootTableAsset> Tables;
    TArray<FFoundationLootPredicateAsset> Predicates;
    const UFoundationItemAssets* Assets = UFoundationItemAssets::LoadDefault();
    if (!TestTrue(TEXT("loot_tables.json and DA_Items"), LootTests_ReadTables(Tables, Predicates) && Assets))
        return false;
    TestEqual(TEXT("loot_tables.json: 18 tables"), Tables.Num(), 18);
    TestEqual(TEXT("loot_tables.json: 1 predicate"), Predicates.Num(), 1);

    // DA_Items against the reference, in order and field by field.
    int32 Wrong = 0;
    auto Expect = [this, &Wrong](bool bSame, const FString& What)
    {
        if (!bSame && Wrong++ < 30) AddError(What);
    };
    Expect(Assets->LootTables.Num() == Tables.Num(), TEXT("DA_Items: table count"));
    for (int32 T = 0; T < FMath::Min(Assets->LootTables.Num(), Tables.Num()); ++T)
    {
        const FFoundationLootTableAsset& Got = Assets->LootTables[T];
        const FFoundationLootTableAsset& Want = Tables[T];
        const FString Where = FString::Printf(TEXT("DA_Items table %d (%s)"), T, *Want.Block.ToString());
        Expect(Got.Block == Want.Block, Where + TEXT(": block ") + Got.Block.ToString());
        Expect(Got.Pools.Num() == Want.Pools.Num(), Where + TEXT(": pool count"));
        for (int32 P = 0; P < FMath::Min(Got.Pools.Num(), Want.Pools.Num()); ++P)
        {
            Expect(Got.Pools[P].Rolls == Want.Pools[P].Rolls, Where + TEXT(": rolls"));
            const TArray<FFoundationLootCandidateAsset>& GotCandidates = Got.Pools[P].Candidates;
            const TArray<FFoundationLootCandidateAsset>& WantCandidates = Want.Pools[P].Candidates;
            Expect(GotCandidates.Num() == WantCandidates.Num(), Where + TEXT(": candidate count"));
            for (int32 C = 0; C < FMath::Min(GotCandidates.Num(), WantCandidates.Num()); ++C)
            {
                const FFoundationLootCandidateAsset& G = GotCandidates[C];
                const FFoundationLootCandidateAsset& W = WantCandidates[C];
                const FString At = Where + FString::Printf(TEXT(" candidate %d: "), C);
                Expect(G.Item == W.Item, At + TEXT("item ") + G.Item.ToString());
                Expect(G.Conditions == W.Conditions, At + TEXT("conditions"));
                Expect(G.CountMin == W.CountMin && G.CountMax == W.CountMax,
                    At + FString::Printf(TEXT("count %d-%d"), G.CountMin, G.CountMax));
                Expect(G.BonusFormula == W.BonusFormula, At + TEXT("bonus formula ") + G.BonusFormula.ToString());
                Expect(G.BonusEnchantment == W.BonusEnchantment,
                    At + TEXT("bonus enchantment ") + G.BonusEnchantment.ToString());
                Expect(G.BonusMultiplier == W.BonusMultiplier, At + TEXT("bonus multiplier"));
                Expect(G.bExplosionDecay == W.bExplosionDecay, At + TEXT("explosion decay"));
            }
        }
    }
    Expect(Assets->LootPredicates.Num() == Predicates.Num(), TEXT("DA_Items: predicate count"));
    for (int32 P = 0; P < FMath::Min(Assets->LootPredicates.Num(), Predicates.Num()); ++P)
    {
        const FFoundationLootPredicateAsset& G = Assets->LootPredicates[P];
        const FFoundationLootPredicateAsset& W = Predicates[P];
        const FString At = FString::Printf(TEXT("DA_Items predicate %d (%s): "), P, *W.Name.ToString());
        Expect(G.Name == W.Name, At + TEXT("name ") + G.Name.ToString());
        Expect(G.Items == W.Items, At + TEXT("items"));
        Expect(G.Enchantments == W.Enchantments, At + TEXT("enchantments"));
        Expect(G.EnchantmentMinLevels == W.EnchantmentMinLevels, At + TEXT("enchantment min levels"));
    }
    TestEqual(TEXT("DA_Items' loot equals loot_tables.json"), Wrong, 0);

    // BuildRules: every table but nether_gold_ore's (gold_nugget is not an item).
    FFoundationRules Rules;
    GetDefault<UFoundationInteractionSettings>()->BuildRules(Rules);
    TestEqual(TEXT("BuildRules attaches 17 tables"), Rules.LootTables.Num(), 17);
    TestEqual(TEXT("gold_nugget is not an item"), Rules.FindItem(TEXT("gold_nugget")), INDEX_NONE);
    TestFalse(TEXT("nether_gold_ore has no table"), Rules.LootTables.Contains(TEXT("nether_gold_ore")));
    FFoundationRules Fresh;
    Fresh.Items = Rules.Items;
    TestEqual(TEXT("AddLoot on the same items skips 1 table"), Assets->AddLoot(Fresh), 1);
    TestEqual(TEXT("AddLoot on the same items attaches 17 tables"), Fresh.LootTables.Num(), 17);

    // The one predicate: Silk Touch I or more, any item.
    const int32 Silk = Rules.LootPredicates.IndexOfByPredicate(
        [](const FFoundationLootPredicate& Predicate) { return Predicate.Name == TEXT("tool/can_silk_touch"); });
    if (!TestTrue(TEXT("the rules hold tool/can_silk_touch"), Silk != INDEX_NONE)) return false;
    const FFoundationLootPredicate& SilkTouch = Rules.LootPredicates[Silk];
    TestEqual(TEXT("one predicate in the rules"), Rules.LootPredicates.Num(), 1);
    TestTrue(TEXT("can_silk_touch lists no items"), SilkTouch.Items.IsEmpty());
    TestTrue(TEXT("can_silk_touch requires silk_touch at level 1 or more"),
        SilkTouch.Enchantments.Num() == 1 && SilkTouch.Enchantments[0].Key == TEXT("silk_touch") &&
            SilkTouch.Enchantments[0].Value == 1);

    // Each attached table: [the block's own item on Silk Touch; the drop item unconditioned].
    int32 Attached = 0;
    for (const FFoundationLootTableAsset& Want : Tables)
    {
        if (Want.Block == TEXT("nether_gold_ore")) continue;
        const FString Block = Want.Block.ToString();
        const FFoundationLootTable* Got = Rules.LootTables.Find(Want.Block);
        if (!TestNotNull(Block + TEXT(" is attached"), Got)) continue;
        ++Attached;
        if (!TestTrue(Block + TEXT(": one pool of one roll with two candidates"),
                Got->Pools.Num() == 1 && Got->Pools[0].Rolls == 1 && Got->Pools[0].Candidates.Num() == 2 &&
                    Want.Pools.Num() == 1 && Want.Pools[0].Candidates.Num() == 2))
            continue;
        const FFoundationLootCandidate& Own = Got->Pools[0].Candidates[0];
        const FFoundationLootCandidate& Drop = Got->Pools[0].Candidates[1];
        const FFoundationLootCandidateAsset& WantDrop = Want.Pools[0].Candidates[1];
        TestTrue(Block + TEXT(": candidate 0 is its own item on can_silk_touch"),
            Own.Item != INDEX_NONE && Own.Item == Rules.FindItem(Want.Block) && Own.Conditions == TArray<int32>{Silk});
        TestTrue(Block + TEXT(": candidate 1 is ") + WantDrop.Item.ToString() + TEXT(" without conditions"),
            Drop.Item != INDEX_NONE && Drop.Item == Rules.FindItem(WantDrop.Item) && Drop.Conditions.IsEmpty());
        const EFoundationLootBonus Bonus = WantDrop.BonusFormula == TEXT("ore_drops") ? EFoundationLootBonus::OreDrops
            : WantDrop.BonusFormula == TEXT("uniform_bonus_count") ? EFoundationLootBonus::UniformBonusCount
                                                                   : EFoundationLootBonus::None;
        TestTrue(Block + TEXT(": the drop's count, bonus and decay as the asset"),
            Drop.CountMin == WantDrop.CountMin && Drop.CountMax == WantDrop.CountMax && Drop.Bonus == Bonus &&
                Drop.BonusEnchantment == WantDrop.BonusEnchantment &&
                Drop.BonusMultiplier == WantDrop.BonusMultiplier && Drop.bExplosionDecay == WantDrop.bExplosionDecay);
    }
    TestEqual(TEXT("17 attached tables checked"), Attached, 17);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationLootDropsTest, "Unnamed.Foundation.Loot.Drops", LootTestFlags)

bool FFoundationLootDropsTest::RunTest(const FString&)
{
    TArray<FString> Tools;
    TArray<FLootBreakRow> Rows;
    const UFoundationItemAssets* Assets = UFoundationItemAssets::LoadDefault();
    if (!TestTrue(TEXT("break_times.json and DA_Items' 18 tables"),
            LootTests_ReadRows(Tools, Rows) && Assets && Assets->LootTables.Num() == 18))
        return false;
    TestEqual(TEXT("the hand and 35 tools"), Tools.Num(), 36);

    // Tallies through the command path, and the same computed from break_times.json and the rules.
    int32 Entries = 0, Eligible = 0, DataEntries = 0, DataEligible = 0, Wrong = 0;
    TMap<FName, int32> DropsPerItem, DataDropsPerItem;
    auto Fail = [this, &Wrong](const FString& What)
    {
        if (Wrong++ < 30) AddError(What);
    };
    for (const FFoundationLootTableAsset& Asset : Assets->LootTables)
    {
        const FString Block = Asset.Block.ToString();
        const FLootBreakRow* Row =
            Rows.FindByPredicate([&Block](const FLootBreakRow& Each) { return Each.Id == Block; });
        if (!TestNotNull(Block + TEXT(" in break_times.json"), Row) ||
            !TestEqual(Block + TEXT(": a drops flag per tool"), Row->Drops.Num(), Tools.Num()))
            continue;
        FLootLab L(false);
        const FFoundationRules& Rules = L.A.Rules;
        const FFoundationLootTable* Table = Rules.LootTables.Find(Asset.Block);
        TestTrue(Block + TEXT(" has a table unless nether_gold_ore"),
            (Table != nullptr) == !(Asset.Block == TEXT("nether_gold_ore")));
        const FFoundationLootCandidate* Candidate =
            Table && Table->Pools.Num() == 1 && Table->Pools[0].Candidates.Num() == 2 ? &Table->Pools[0].Candidates[1]
                                                                                      : nullptr;
        if (Table && !Candidate)
        {
            AddError(Block + TEXT(": not one pool of two candidates"));
            continue;
        }
        // Without a table the block drops itself: its own item x1.
        const int32 Own = Rules.FindItem(Asset.Block);
        const int32 DropItem = Candidate ? Candidate->Item : Own;
        const int32 Min = Candidate ? Candidate->CountMin : 1, Max = Candidate ? Candidate->CountMax : 1;
        if (!TestTrue(Block + TEXT(": its item and its drop exist"), Own != INDEX_NONE && DropItem != INDEX_NONE))
            continue;
        const FName DropName = Rules.Items[DropItem].Id;
        for (int32 Column = 0; Column < Tools.Num(); ++Column)
        {
            ++DataEntries;
            if (Row->Drops[Column])
            {
                ++DataEligible;
                ++DataDropsPerItem.FindOrAdd(DropName);
            }
            L.ClearAndHold(Column == 0 ? NAME_None : FName(*Tools[Column]));
            L.Put(Asset.Block);
            L.NextStep();
            const TMap<int32, int32> Before = LootTests_Counts(L.P().Inventory);
            const FFoundationResult R = LootTests_Mine(L, 1000);
            const TMap<int32, int32> Gains = LootTests_Gains(Before, LootTests_Counts(L.P().Inventory));
            const FString What = Block + TEXT(" with ") + Tools[Column];
            ++Entries;
            if (!R.bBroken || L.Solid())
            {
                Fail(What + FString::Printf(TEXT(": not broken (%s)"), LexToString(R.Reason)));
                continue;
            }
            if (!Row->Drops[Column])
            {
                if (!R.Drops.IsEmpty() || !Gains.IsEmpty())
                    Fail(What +
                        FString::Printf(
                            TEXT(": %d drops, %d items gained; the table says none"), R.Drops.Num(), Gains.Num()));
                continue;
            }
            ++Eligible;
            for (const FFoundationLootDrop& Drop : R.Drops)
                ++DropsPerItem.FindOrAdd(Rules.GetItem(Drop.Item).Id);
            const bool bOne = R.Drops.Num() == 1 && R.Drops[0].Item == DropItem && R.Drops[0].Count >= Min &&
                R.Drops[0].Count <= Max && (Table == nullptr || R.Drops[0].Item != Own);
            const bool bGained =
                R.Drops.Num() == 1 && Gains.Num() == 1 && Gains.FindRef(R.Drops[0].Item) == R.Drops[0].Count;
            if (!bOne || !bGained)
                Fail(What +
                    FString::Printf(TEXT(": %d drops (first %s x%d), %d items gained; want one %s x%d-%d"),
                        R.Drops.Num(), R.Drops.IsEmpty() ? TEXT("none") : *Rules.GetItem(R.Drops[0].Item).Id.ToString(),
                        R.Drops.IsEmpty() ? 0 : R.Drops[0].Count, Gains.Num(), *DropName.ToString(), Min, Max));
        }
    }
    TestEqual(TEXT("every entry as the table"), Wrong, 0);

    // Pinned from break_times.json and loot_tables.json (18 ores x 36 columns; pickaxes of the ore's tier).
    const TMap<FName, int32> PinnedDrops = {{TEXT("coal"), 14}, {TEXT("raw_copper"), 10}, {TEXT("raw_iron"), 10},
        {TEXT("raw_gold"), 6}, {TEXT("diamond"), 6}, {TEXT("emerald"), 6}, {TEXT("lapis_lazuli"), 10},
        {TEXT("redstone"), 6}, {TEXT("quartz"), 7}, {TEXT("nether_gold_ore"), 7}};
    TestEqual(TEXT("648 entries"), Entries, 648);
    TestEqual(TEXT("82 eligible entries"), Eligible, 82);
    TestTrue(TEXT("drops per item as pinned"), LootTests_SameTally(DropsPerItem, PinnedDrops));
    TestEqual(TEXT("the data: 648 entries"), DataEntries, 648);
    TestEqual(TEXT("the data: 82 eligible entries"), DataEligible, 82);
    TestTrue(TEXT("the data: drops per item as pinned"), LootTests_SameTally(DataDropsPerItem, PinnedDrops));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationLootCountsTest, "Unnamed.Foundation.Loot.Counts", LootTestFlags)

bool FFoundationLootCountsTest::RunTest(const FString&)
{
    FFoundationRules Rules;
    GetDefault<UFoundationInteractionSettings>()->BuildRules(Rules);
    const int32 IronIndex = Rules.FindItem(TEXT("iron_pickaxe"));
    if (!TestTrue(TEXT("17 tables and an iron pickaxe"), Rules.LootTables.Num() == 17 && IronIndex != INDEX_NONE))
        return false;
    const FFoundationItemDefinition& IronPickaxe = Rules.Items[IronIndex];

    // Every attached table with a non-silk tool, 6,000 break counts at three cells.
    const FIntVector Cells[] = {FIntVector(0, 0, 0), FIntVector(5, -3, 12), FIntVector(-17, 40, -60)};
    constexpr int32 BreakCounts = 6000;
    TSet<FName> Spread;
    TArray<FFoundationLootDrop> Drops;
    for (const TPair<FName, FFoundationLootTable>& Pair : Rules.LootTables)
    {
        const FString Block = Pair.Key.ToString();
        if (!TestTrue(Block + TEXT(": one pool of two candidates"),
                Pair.Value.Pools.Num() == 1 && Pair.Value.Pools[0].Candidates.Num() == 2))
            continue;
        const FFoundationLootCandidate& Drop = Pair.Value.Pools[0].Candidates[1];
        const FName Item = Rules.GetItem(Drop.Item).Id;
        TArray<int32> Hits;
        Hits.SetNumZeroed(FMath::Max(1, Drop.CountMax - Drop.CountMin + 1));
        int32 Total = 0, WrongItem = 0, Outside = 0;
        for (const FIntVector& Cell : Cells)
            for (int32 Break = 0; Break < BreakCounts; ++Break)
            {
                FRandomStream Random(FoundationLoot::MakeSeed(Rules.LootSeed, Cell, Break));
                Drops.Reset();
                FoundationLoot::Roll(Pair.Value, Rules.LootPredicates, IronPickaxe, Random, Drops);
                ++Total;
                if (Drops.Num() != 1 || Drops[0].Item != Drop.Item)
                    ++WrongItem;
                else if (Drops[0].Count < Drop.CountMin || Drops[0].Count > Drop.CountMax)
                    ++Outside;
                else
                    ++Hits[Drops[0].Count - Drop.CountMin];
            }
        const FString What = Block + TEXT(" (") + Item.ToString() + TEXT(")");
        TestEqual(What + TEXT(": one stack of the drop per roll"), WrongItem, 0);
        TestEqual(What + TEXT(": no count outside the range"), Outside, 0);
        if (Drop.CountMax > Drop.CountMin)
            Spread.Add(Item);
        else
            TestTrue(What + TEXT(": always 1"), Drop.CountMin == 1 && Drop.CountMax == 1 && Hits[0] == Total);
        const double Uniform = double(Total) / Hits.Num();
        for (int32 Value = 0; Value < Hits.Num(); ++Value)
        {
            const FString Count = FString::Printf(TEXT(": x%d"), Drop.CountMin + Value);
            TestTrue(What + Count + TEXT(" reached"), Hits[Value] > 0);
            if (Hits.Num() > 1)
                TestTrue(What + Count + FString::Printf(TEXT(" %d of %d within 10%% of uniform"), Hits[Value], Total),
                    FMath::Abs(Hits[Value] - Uniform) <= .1 * Uniform);
        }
    }
    TestTrue(TEXT("ranged counts: raw_copper, lapis_lazuli and redstone only"),
        Spread.Num() == 3 && Spread.Contains(TEXT("raw_copper")) && Spread.Contains(TEXT("lapis_lazuli")) &&
            Spread.Contains(TEXT("redstone")));

    // Determinism: a pure function of (LootSeed, cell, break count).
    const FFoundationLootTable* Lapis = Rules.LootTables.Find(TEXT("lapis_ore"));
    if (!TestNotNull(TEXT("lapis_ore's table"), Lapis)) return false;
    int32 Same = 0, Differ = 0;
    for (int32 Break = 0; Break < 100; ++Break)
    {
        const FIntVector Cell(3, 4, 5);
        TArray<FFoundationLootDrop> First, Second, Other;
        FRandomStream RandomFirst(FoundationLoot::MakeSeed(0, Cell, Break));
        FRandomStream RandomSecond(FoundationLoot::MakeSeed(0, Cell, Break));
        FRandomStream RandomOther(FoundationLoot::MakeSeed(1, Cell, Break));
        FoundationLoot::Roll(*Lapis, Rules.LootPredicates, IronPickaxe, RandomFirst, First);
        FoundationLoot::Roll(*Lapis, Rules.LootPredicates, IronPickaxe, RandomSecond, Second);
        FoundationLoot::Roll(*Lapis, Rules.LootPredicates, IronPickaxe, RandomOther, Other);
        Same += LootTests_Same(First, Second);
        Differ += !LootTests_Same(First, Other);
    }
    TestEqual(TEXT("the same seed, cell and break count roll the same 100 times"), Same, 100);
    TestTrue(TEXT("LootSeed 1 differs from 0 in 100 rolls"), Differ > 0);
    // Pinned from FoundationLoot.cpp's SplitMix64 chain (computed in Python, s5b/loot_facts.py).
    TestEqual(TEXT("MakeSeed(0, (0, 0, 0), 0)"), FoundationLoot::MakeSeed(0, FIntVector(0, 0, 0), 0), 1801442398);
    TestEqual(TEXT("MakeSeed(7, (1, -2, 3), 5)"), FoundationLoot::MakeSeed(7, FIntVector(1, -2, 3), 5), 1327075690);

    // Predicates: no item carries enchantments, so can_silk_touch never passes.
    const FFoundationLootPredicate* Silk = Rules.LootPredicates.FindByPredicate(
        [](const FFoundationLootPredicate& Predicate) { return Predicate.Name == TEXT("tool/can_silk_touch"); });
    if (!TestNotNull(TEXT("tool/can_silk_touch"), Silk)) return false;
    FFoundationLootPredicate OnlyIron;
    OnlyIron.Name = TEXT("test/only_iron_pickaxe");
    OnlyIron.Items = {TEXT("iron_pickaxe")};
    FFoundationLootPredicate SilkOne;
    SilkOne.Name = TEXT("test/silk_touch_1");
    SilkOne.Enchantments.Emplace(TEXT("silk_touch"), 1);
    int32 SilkPasses = 0, OnlyIronPasses = 0, SilkOnePasses = 0;
    TArray<const FFoundationItemDefinition*> Holders = {&Rules.Hand};
    for (const FFoundationItemDefinition& Item : Rules.Items)
        Holders.Add(&Item);
    for (const FFoundationItemDefinition* Item : Holders)
    {
        SilkPasses += FoundationLoot::Passes(*Silk, *Item);
        OnlyIronPasses += FoundationLoot::Passes(OnlyIron, *Item);
        SilkOnePasses += FoundationLoot::Passes(SilkOne, *Item);
    }
    TestEqual(TEXT("can_silk_touch fails for every item and the hand"), SilkPasses, 0);
    TestEqual(TEXT("Items {iron_pickaxe} passes once"), OnlyIronPasses, 1);
    TestTrue(TEXT("Items {iron_pickaxe} passes for the iron pickaxe"), FoundationLoot::Passes(OnlyIron, IronPickaxe));
    TestEqual(TEXT("Enchantments {silk_touch 1} fails for every item and the hand"), SilkOnePasses, 0);

    // ApplyBonus: neutral at level 0; Fortune's spans at levels 1-3.
    FRandomStream Random(20261011);
    int32 NotNeutral = 0;
    for (int32 Roll = 0; Roll < 1000; ++Roll)
    {
        const int32 Count = 1 + Roll % 9;
        NotNeutral += FoundationLoot::ApplyBonus(EFoundationLootBonus::OreDrops, 0, 0, Count, Random) != Count;
        NotNeutral += FoundationLoot::ApplyBonus(EFoundationLootBonus::UniformBonusCount, 1, 0, Count, Random) != Count;
    }
    TestEqual(TEXT("level 0 is neutral for ore_drops and uniform_bonus_count"), NotNeutral, 0);
    auto Span = [&Random](EFoundationLootBonus Bonus, float Multiplier, int32 Level, int32 Count)
    {
        TSet<int32> Seen;
        for (int32 Roll = 0; Roll < 4000; ++Roll)
            Seen.Add(FoundationLoot::ApplyBonus(Bonus, Multiplier, Level, Count, Random));
        return Seen;
    };
    auto Spans = [](const TSet<int32>& Seen, int32 Min, int32 Max, int32 Stride)
    {
        if (Seen.Num() != (Max - Min) / Stride + 1) return false;
        for (int32 Value = Min; Value <= Max; Value += Stride)
            if (!Seen.Contains(Value)) return false;
        return true;
    };
    for (int32 Level = 1; Level <= 3; ++Level)
        TestTrue(FString::Printf(TEXT("ore_drops at level %d on 1 spans [1, %d]"), Level, Level + 1),
            Spans(Span(EFoundationLootBonus::OreDrops, 0, Level, 1), 1, Level + 1, 1));
    TestTrue(TEXT("ore_drops at level 3 on lapis' 9: 9, 18, 27 and 36 (Fortune III max 36)"),
        Spans(Span(EFoundationLootBonus::OreDrops, 0, 3, 9), 9, 36, 9));
    TestTrue(TEXT("uniform_bonus_count x1 at level 3 on 5 spans [5, 8] (redstone's Fortune III max 8)"),
        Spans(Span(EFoundationLootBonus::UniformBonusCount, 1, 3, 5), 5, 8, 1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationLootInventoryFullTest, "Unnamed.Foundation.Loot.InventoryFull", LootTestFlags)

bool FFoundationLootInventoryFullTest::RunTest(const FString&)
{
    FLootLab L(false);
    FFoundationRules& Rules = L.A.Rules;
    const int32 LapisItem = Rules.FindItem(TEXT("lapis_lazuli")), Dirt = Rules.FindItem(TEXT("dirt"));
    const int32 StoneIndex = Rules.FindItem(TEXT("stone_pickaxe"));
    const FFoundationLootTable* Table = Rules.LootTables.Find(TEXT("lapis_ore"));
    if (!TestTrue(TEXT("lapis_lazuli, dirt, a stone pickaxe and lapis_ore's table"),
            LapisItem != INDEX_NONE && Dirt != INDEX_NONE && StoneIndex != INDEX_NONE && Table))
        return false;
    const FFoundationItemDefinition StonePickaxe = Rules.Items[StoneIndex];
    const FIntVector Cell = FIntVector::ZeroValue;
    // The authority's roll for the next break at the lab cell under a loot seed.
    auto Predict = [&](uint64 Seed)
    {
        FRandomStream Random(FoundationLoot::MakeSeed(Seed, Cell, L.A.Breaks));
        TArray<FFoundationLootDrop> Drops;
        FoundationLoot::Roll(*Table, Rules.LootPredicates, StonePickaxe, Random, Drops);
        return Drops.Num() == 1 && Drops[0].Item == LapisItem ? Drops[0].Count : -1;
    };

    // Only 4 lapis fit: the pickaxe, 60 of 64 lapis, then full stacks of dirt (the offhand takes only merges).
    L.ClearAndHold(TEXT("stone_pickaxe"));
    FFoundationInventory& Inventory = L.P().Inventory;
    Inventory.SetSlot(1, Inventory.MakeStack(LapisItem, 60));
    for (int32 Slot = 2; Slot < FFoundationInventory::HotbarSlots; ++Slot)
        Inventory.SetSlot(Slot, Inventory.MakeStack(Dirt, Rules.Items[Dirt].MaxStack));
    if (!TestEqual(TEXT("room for 4 lapis"), Inventory.GetSpaceFor(LapisItem, Rules.Items[LapisItem].MaxStack), 4))
        return false;

    // The first loot seed whose roll is above 4.
    uint64 Seed = 0;
    while (Seed < 1000 && Predict(Seed) <= 4)
        ++Seed;
    const int32 Predicted = Predict(Seed);
    if (!TestTrue(TEXT("a loot seed rolling more than 4 lapis"), Predicted > 4)) return false;
    AddInfo(FString::Printf(TEXT("LootSeed %llu rolls %d lapis at break %d"), Seed, Predicted, L.A.Breaks));
    Rules.LootSeed = Seed;

    // Refused twice: the block stays, nothing moves, and the second refusal rolls the same.
    const int32 Breaks = L.A.Breaks;
    const TMap<int32, int32> Full = LootTests_Counts(Inventory);
    L.Put(TEXT("lapis_ore"));
    for (int32 Attempt = 1; Attempt <= 2; ++Attempt)
    {
        L.NextStep();
        const FFoundationResult R = LootTests_Mine(L, 1000);
        const FString What = FString::Printf(TEXT("full, attempt %d: "), Attempt);
        TestTrue(What + TEXT("refused with InventoryFull"),
            R.Outcome == EFoundationOutcome::Rejected && R.Reason == EFoundationReason::InventoryFull);
        TestFalse(What + TEXT("not broken"), R.bBroken);
        TestTrue(What + TEXT("no drops"), R.Drops.IsEmpty());
        TestTrue(What + TEXT("the block stays"), L.Solid());
        TestEqual(What + TEXT("Breaks unchanged"), L.A.Breaks, Breaks);
        TestTrue(What + TEXT("nothing added"), LootTests_Gains(Full, LootTests_Counts(L.P().Inventory)).IsEmpty());
        TestEqual(What + TEXT("the same roll"), Predict(Seed), Predicted);
    }

    // Room made: the retry breaks and returns the predicted count.
    L.P().Inventory.SetSlot(2, FFoundationItemInstance());
    L.NextStep();
    const int32 LapisBefore = L.Count(TEXT("lapis_lazuli"));
    FFoundationResult R = LootTests_Mine(L, 1000);
    TestTrue(TEXT("with room: broken"), R.bBroken && !L.Solid());
    TestEqual(TEXT("with room: Breaks + 1"), L.A.Breaks, Breaks + 1);
    TestTrue(FString::Printf(TEXT("with room: lapis_lazuli x%d returned"), Predicted),
        R.Drops.Num() == 1 && R.Drops[0].Item == LapisItem && R.Drops[0].Count == Predicted);
    TestEqual(TEXT("with room: the inventory gains the roll"), L.Count(TEXT("lapis_lazuli")) - LapisBefore, Predicted);

    // A new loot seed changes the prediction, and the authority follows it.
    const int32 OldSeedRoll = Predict(Seed);
    uint64 NewSeed = Seed + 1;
    while (NewSeed < Seed + 1000 && Predict(NewSeed) == OldSeedRoll)
        ++NewSeed;
    const int32 NewSeedRoll = Predict(NewSeed);
    TestTrue(TEXT("another loot seed changes the prediction"), NewSeedRoll > 0 && NewSeedRoll != OldSeedRoll);
    Rules.LootSeed = NewSeed;
    L.ClearAndHold(TEXT("stone_pickaxe"));
    L.Put(TEXT("lapis_ore"));
    L.NextStep();
    R = LootTests_Mine(L, 1000);
    TestTrue(FString::Printf(TEXT("LootSeed %llu: lapis_lazuli x%d, not x%d"), NewSeed, NewSeedRoll, OldSeedRoll),
        R.bBroken && R.Drops.Num() == 1 && R.Drops[0].Item == LapisItem && R.Drops[0].Count == NewSeedRoll);
    TestEqual(TEXT("the new seed's roll gained"), L.Count(TEXT("lapis_lazuli")), NewSeedRoll);
    return true;
}
#endif
