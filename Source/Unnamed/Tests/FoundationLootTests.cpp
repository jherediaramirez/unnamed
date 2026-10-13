#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationLoot.h"
#include "Foundation/FoundationPlantRules.h"
#include "Algo/AllOf.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags LootTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Reference/minecraft-items-26.3/loot_tables.json in DA_Items' shape, built the way
    // Scripts/Editor/ItemImportData.py function loot_table does (condition nodes parent first, depth first; a
    // candidate's tests before its modifiers; a pool's candidates, then its tests, then its modifiers; a table's pools,
    // then its modifiers). JSON null or "" is None.
    FName LootTests_Name(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        FString Text;
        return Object->TryGetStringField(Field, Text) ? FName(*Text) : FName();
    }

    TArray<FName> LootTests_Names(const TArray<TSharedPtr<FJsonValue>>& Values)
    {
        TArray<FName> Names;
        for (const TSharedPtr<FJsonValue>& Value : Values)
            Names.Add(FName(*Value->AsString()));
        return Names;
    }

    struct FLootBuilder
    {
        FFoundationLootTableAsset& Table;

        int32 Node(const TSharedPtr<FJsonValue>& Value)
        {
            const int32 Index = Table.Conditions.Num();
            FFoundationLootConditionAsset Made;
            FString Predicate;
            if (Value->TryGetString(Predicate))
            {
                Made.Type = TEXT("predicate");
                Made.Name = FName(*Predicate);
                Table.Conditions.Add(Made);
                return Index;
            }
            const TSharedPtr<FJsonObject> C = Value->AsObject();
            const FString Type = C->GetStringField(TEXT("type"));
            Made.Type = FName(*Type);
            Table.Conditions.Add(Made);
            if (Type == TEXT("any_of") || Type == TEXT("all_of"))
            {
                for (const TSharedPtr<FJsonValue>& Term : C->GetArrayField(TEXT("terms")))
                {
                    const int32 Child = Node(Term);
                    Made.Terms.Add(Child);
                }
            }
            else if (Type == TEXT("inverted"))
            {
                const int32 Child = Node(C->TryGetField(TEXT("term")));
                Made.Terms.Add(Child);
            }
            else if (Type == TEXT("match_block") || Type == TEXT("location_check"))
            {
                Made.Name = LootTests_Name(C, TEXT("block"));
                for (const TPair<FString, TSharedPtr<FJsonValue>>& State : C->GetObjectField(TEXT("state"))->Values)
                {
                    Made.StateKeys.Add(FName(*State.Key));
                    Made.StateValues.Add(FName(*State.Value->AsString()));
                }
                if (Type == TEXT("location_check"))
                    for (const TSharedPtr<FJsonValue>& Offset : C->GetArrayField(TEXT("offset")))
                        Made.Offset.Add(int32(Offset->AsNumber()));
            }
            else if (Type == TEXT("table_bonus"))
            {
                Made.Name = LootTests_Name(C, TEXT("enchantment"));
                for (const TSharedPtr<FJsonValue>& Chance : C->GetArrayField(TEXT("chances")))
                    Made.Chances.Add(float(Chance->AsNumber()));
            }
            else if (Type == TEXT("random_chance"))
                Made.Chances.Add(float(C->GetNumberField(TEXT("chance"))));
            else if (Type == TEXT("match_tool"))
            {
                Made.Name = LootTests_Name(C, TEXT("tag"));
                Made.Items = LootTests_Names(C->GetArrayField(TEXT("items")));
            }
            else if (Type == TEXT("entity_properties"))
                Made.Name = LootTests_Name(C, TEXT("entity"));
            // The children went in after this node's slot; the slot takes the finished node.
            Table.Conditions[Index] = Made;
            return Index;
        }

        FFoundationLootModifierAsset Modifier(const TSharedPtr<FJsonObject>& M)
        {
            FFoundationLootModifierAsset Out;
            Out.Type = LootTests_Name(M, TEXT("type"));
            const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
            if (M->TryGetArrayField(TEXT("conditions"), Conditions))
                for (const TSharedPtr<FJsonValue>& Condition : *Conditions)
                    Out.Tests.Add(Node(Condition));
            const FString Type = Out.Type.ToString();
            if (Type == TEXT("set_count"))
            {
                const TArray<TSharedPtr<FJsonValue>>& Count = M->GetArrayField(TEXT("count"));
                Out.Min = int32(Count[0]->AsNumber());
                Out.Max = int32(Count[1]->AsNumber());
                Out.bAdd = M->GetBoolField(TEXT("add"));
            }
            else if (Type == TEXT("limit_count"))
            {
                Out.bHasMin = M->HasTypedField<EJson::Number>(TEXT("min"));
                Out.bHasMax = M->HasTypedField<EJson::Number>(TEXT("max"));
                Out.Min = Out.bHasMin ? int32(M->GetNumberField(TEXT("min"))) : 0;
                Out.Max = Out.bHasMax ? int32(M->GetNumberField(TEXT("max"))) : 0;
            }
            else if (Type == TEXT("apply_bonus"))
            {
                Out.BonusFormula = LootTests_Name(M, TEXT("formula"));
                Out.BonusEnchantment = LootTests_Name(M, TEXT("enchantment"));
                Out.BonusMultiplier = float(M->GetNumberField(TEXT("bonus_multiplier")));
            }
            return Out;
        }

        TArray<FFoundationLootModifierAsset> Modifiers(const TSharedPtr<FJsonObject>& Owner)
        {
            TArray<FFoundationLootModifierAsset> Out;
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            if (Owner->TryGetArrayField(TEXT("modifiers"), Values))
                for (const TSharedPtr<FJsonValue>& Value : *Values)
                    Out.Add(Modifier(Value->AsObject()));
            return Out;
        }

        FFoundationLootCandidateAsset Candidate(const TSharedPtr<FJsonObject>& Entry)
        {
            FFoundationLootCandidateAsset Out;
            Out.Item = LootTests_Name(Entry, TEXT("item"));
            const TArray<TSharedPtr<FJsonValue>>* Count = nullptr;
            if (Entry->TryGetArrayField(TEXT("count"), Count))
            {
                Out.CountMin = int32((*Count)[0]->AsNumber());
                Out.CountMax = int32((*Count)[1]->AsNumber());
            }
            if (Entry->HasTypedField<EJson::Object>(TEXT("bonus")))
            {
                const TSharedPtr<FJsonObject> Bonus = Entry->GetObjectField(TEXT("bonus"));
                Out.BonusFormula = LootTests_Name(Bonus, TEXT("formula"));
                Out.BonusEnchantment = LootTests_Name(Bonus, TEXT("enchantment"));
                Out.BonusMultiplier = float(Bonus->GetNumberField(TEXT("bonus_multiplier")));
            }
            bool bDecay = false;
            Out.bExplosionDecay = Entry->TryGetBoolField(TEXT("explosion_decay"), bDecay) && bDecay;
            const TArray<TSharedPtr<FJsonValue>>& Conditions = Entry->GetArrayField(TEXT("conditions"));
            for (const TSharedPtr<FJsonValue>& Condition : Conditions)
            {
                FString Name;
                if (Condition->TryGetString(Name)) Out.Conditions.Add(FName(*Name));
            }
            for (const TSharedPtr<FJsonValue>& Condition : Conditions)
            {
                FString Name;
                if (!Condition->TryGetString(Name)) Out.Tests.Add(Node(Condition));
            }
            Out.Modifiers = Modifiers(Entry);
            return Out;
        }
    };

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
            FLootBuilder Builder{Table};
            Table.Block = FName(*Object->GetStringField(TEXT("block")));
            for (const TSharedPtr<FJsonValue>& PoolValue : Object->GetArrayField(TEXT("pools")))
            {
                const TSharedPtr<FJsonObject> PoolObject = PoolValue->AsObject();
                FFoundationLootPoolAsset Pool;
                Pool.Rolls = int32(PoolObject->GetNumberField(TEXT("rolls")));
                for (const TSharedPtr<FJsonValue>& CandidateValue : PoolObject->GetArrayField(TEXT("candidates")))
                    Pool.Candidates.Add(Builder.Candidate(CandidateValue->AsObject()));
                const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
                if (PoolObject->TryGetArrayField(TEXT("conditions"), Conditions))
                    for (const TSharedPtr<FJsonValue>& Condition : *Conditions)
                        Pool.Tests.Add(Builder.Node(Condition));
                Pool.Modifiers = Builder.Modifiers(PoolObject);
                Table.Pools.Add(MoveTemp(Pool));
            }
            Table.Modifiers = Builder.Modifiers(Object);
        }
        return true;
    }

    // Items the 26.3 loot tables name that this project does not define, and the tables that therefore keep "drops
    // itself": none since loot step 2b (the 23 drop items joined the item reference and location_check is evaluated,
    // so all 602 tables attach).
    const TArray<FName>& LootTests_SkippedPins()
    {
        static const TArray<FName> Pins;
        return Pins;
    }

    // The runtime's modifiers against an asset's (AddLoot's mapping).
    bool LootTests_SameModifiers(
        const TArray<FFoundationLootModifier>& Got, const TArray<FFoundationLootModifierAsset>& Want)
    {
        static const TMap<FName, EFoundationLootModifier> Types = {
            {TEXT("set_count"), EFoundationLootModifier::SetCount},
            {TEXT("apply_bonus"), EFoundationLootModifier::ApplyBonus},
            {TEXT("limit_count"), EFoundationLootModifier::LimitCount},
            {TEXT("explosion_decay"), EFoundationLootModifier::ExplosionDecay},
            {TEXT("copy_components"), EFoundationLootModifier::Copy},
            {TEXT("copy_state"), EFoundationLootModifier::Copy}};
        if (Got.Num() != Want.Num()) return false;
        for (int32 Index = 0; Index < Got.Num(); ++Index)
        {
            const FFoundationLootModifier& A = Got[Index];
            const FFoundationLootModifierAsset& B = Want[Index];
            const EFoundationLootModifier* Type = Types.Find(B.Type);
            const bool bLimit = B.Type == TEXT("limit_count");
            const EFoundationLootBonus Bonus = B.BonusFormula == TEXT("ore_drops") ? EFoundationLootBonus::OreDrops
                : B.BonusFormula == TEXT("uniform_bonus_count") ? EFoundationLootBonus::UniformBonusCount
                                                                : EFoundationLootBonus::None;
            if (!Type || A.Type != *Type || A.Conditions != B.Tests ||
                A.Min != (bLimit && !B.bHasMin ? MIN_int32 : B.Min) ||
                A.Max != (bLimit && !B.bHasMax ? MAX_int32 : B.Max) || A.bAdd != B.bAdd || A.Bonus != Bonus ||
                A.BonusEnchantment != B.BonusEnchantment || A.BonusMultiplier != B.BonusMultiplier)
                return false;
        }
        return true;
    }

    // One asset condition node against the runtime's.
    bool LootTests_SameCondition(const FFoundationLootCondition& Got, const FFoundationLootConditionAsset& Want,
        const TArray<FFoundationLootPredicate>& Predicates)
    {
        static const TMap<FName, EFoundationLootCondition> Types = {
            {TEXT("predicate"), EFoundationLootCondition::Predicate},
            {TEXT("survives_explosion"), EFoundationLootCondition::SurvivesExplosion},
            {TEXT("match_block"), EFoundationLootCondition::MatchBlock},
            {TEXT("any_of"), EFoundationLootCondition::AnyOf}, {TEXT("all_of"), EFoundationLootCondition::AllOf},
            {TEXT("inverted"), EFoundationLootCondition::Inverted},
            {TEXT("table_bonus"), EFoundationLootCondition::TableBonus},
            {TEXT("random_chance"), EFoundationLootCondition::RandomChance},
            {TEXT("match_tool"), EFoundationLootCondition::MatchTool},
            {TEXT("entity_properties"), EFoundationLootCondition::EntityProperties},
            {TEXT("location_check"), EFoundationLootCondition::LocationCheck}};
        const EFoundationLootCondition* Type = Types.Find(Want.Type);
        if (!Type || Got.Type != *Type || Got.Terms != Want.Terms || Got.Name != Want.Name ||
            Got.Chances != Want.Chances || Got.Items != Want.Items || Got.State.Num() != Want.StateKeys.Num() ||
            Want.StateKeys.Num() != Want.StateValues.Num())
            return false;
        // The jar's [x, y, z] (y up) is FIntVector(x, z, y) in cells; other kinds keep a zero offset.
        const FIntVector WantOffset = *Type == EFoundationLootCondition::LocationCheck && Want.Offset.Num() == 3
            ? FIntVector(Want.Offset[0], Want.Offset[2], Want.Offset[1])
            : FIntVector::ZeroValue;
        if (Got.Offset != WantOffset) return false;
        for (int32 Index = 0; Index < Got.State.Num(); ++Index)
            if (Got.State[Index].Key != Want.StateKeys[Index] || Got.State[Index].Value != Want.StateValues[Index])
                return false;
        return *Type != EFoundationLootCondition::Predicate ||
            (Predicates.IsValidIndex(Got.Predicate) && Predicates[Got.Predicate].Name == Want.Name);
    }

    // Two assets, field by field.
    bool LootTests_SameConditionAsset(const FFoundationLootConditionAsset& A, const FFoundationLootConditionAsset& B)
    {
        return A.Type == B.Type && A.Name == B.Name && A.Terms == B.Terms && A.StateKeys == B.StateKeys &&
            A.StateValues == B.StateValues && A.Chances == B.Chances && A.Items == B.Items && A.Offset == B.Offset;
    }

    bool LootTests_SameModifierAsset(const FFoundationLootModifierAsset& A, const FFoundationLootModifierAsset& B)
    {
        return A.Type == B.Type && A.Tests == B.Tests && A.Min == B.Min && A.Max == B.Max && A.bAdd == B.bAdd &&
            A.bHasMin == B.bHasMin && A.bHasMax == B.bHasMax && A.BonusFormula == B.BonusFormula &&
            A.BonusEnchantment == B.BonusEnchantment && A.BonusMultiplier == B.BonusMultiplier;
    }

    // A tally of what Table rolls for Tool: "item:count" to the number of rolls that dropped it, plus the rolls that
    // dropped nothing. Seeds are MakeSeed(LootSeed, cell, break) over three cells.
    struct FLootTally
    {
        int32 Rolls = 0;
        int32 Nothing = 0;
        TMap<FString, int32> Hits;
        int32 Of(const FString& Key) const { return Hits.FindRef(Key); }
    };

    FLootTally LootTests_Tally(const FFoundationRules& Rules, const FFoundationLootTable& Table,
        const FFoundationItemDefinition& Tool, int32 BreakCounts)
    {
        const FIntVector Cells[] = {FIntVector(0, 0, 0), FIntVector(5, -3, 12), FIntVector(-17, 40, -60)};
        FLootTally Tally;
        TArray<FFoundationLootDrop> Drops;
        for (const FIntVector& Cell : Cells)
            for (int32 Break = 0; Break < BreakCounts; ++Break)
            {
                FRandomStream Random(FoundationLoot::MakeSeed(Rules.LootSeed, Cell, Break));
                Drops.Reset();
                FoundationLoot::Roll(Table, Rules.LootPredicates, Tool, Random, Drops);
                ++Tally.Rolls;
                Tally.Nothing += Drops.IsEmpty();
                for (const FFoundationLootDrop& Drop : Drops)
                    ++Tally.Hits.FindOrAdd(
                        FString::Printf(TEXT("%s:%d"), *Rules.GetItem(Drop.Item).Id.ToString(), Drop.Count));
            }
        return Tally;
    }

    // A synthetic one-pool table whose one candidate (item index 7) takes Modifiers and Conditions.
    FFoundationLootTable LootTests_Synth(TArray<FFoundationLootModifier> Modifiers, TArray<int32> Conditions = {})
    {
        FFoundationLootTable Table;
        FFoundationLootCandidate& Candidate = Table.Pools.AddDefaulted_GetRef().Candidates.AddDefaulted_GetRef();
        Candidate.Item = 7;
        Candidate.Modifiers = MoveTemp(Modifiers);
        Candidate.Conditions = MoveTemp(Conditions);
        return Table;
    }

    int32 LootTests_AddCondition(
        FFoundationLootTable& Table, EFoundationLootCondition Type, TArray<int32> Terms = {}, FName Name = NAME_None)
    {
        FFoundationLootCondition& Condition = Table.Conditions.AddDefaulted_GetRef();
        Condition.Type = Type;
        Condition.Terms = MoveTemp(Terms);
        Condition.Name = Name;
        return Table.Conditions.Num() - 1;
    }

    FFoundationLootModifier LootTests_Modifier(
        EFoundationLootModifier Type, int32 Min, int32 Max, bool bAdd = false, TArray<int32> Conditions = {})
    {
        FFoundationLootModifier Modifier;
        Modifier.Type = Type;
        Modifier.Min = Min;
        Modifier.Max = Max;
        Modifier.bAdd = bAdd;
        Modifier.Conditions = MoveTemp(Conditions);
        return Modifier;
    }

    // One row of Reference/minecraft-items-26.3/facts/break_times.json: the drops flag for the hand and the 35 tools.
    struct FLootBreakRow
    {
        FString Id;
        TArray<int32> Drops;
        // Survival break time per column in ticks; -1 never breaks.
        TArray<int32> Ticks;
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
            for (const TSharedPtr<FJsonValue>& Tick : Block->GetArrayField(TEXT("ticks")))
                Row.Ticks.Add(int32(Tick->AsNumber()));
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

    // An ore-form candidate's set_count range: AddLoot puts it first (loot step 2a).
    FIntPoint LootTests_Range(const FFoundationLootCandidate& Candidate)
    {
        if (Candidate.Modifiers.IsEmpty() || Candidate.Modifiers[0].Type != EFoundationLootModifier::SetCount)
            return FIntPoint(1, 1);
        return FIntPoint(Candidate.Modifiers[0].Min, Candidate.Modifiers[0].Max);
    }

    // AddLoot's modifiers for an ore-form candidate: set_count's range, then the bonus and the decay when the asset has
    // them.
    bool LootTests_SameOreForm(
        const FFoundationLootCandidate& Got, const FFoundationLootCandidateAsset& Want, EFoundationLootBonus Bonus)
    {
        TArray<FFoundationLootModifier> Expected;
        Expected.AddDefaulted();
        Expected[0].Min = Want.CountMin;
        Expected[0].Max = Want.CountMax;
        if (Bonus != EFoundationLootBonus::None)
        {
            FFoundationLootModifier& Modifier = Expected.AddDefaulted_GetRef();
            Modifier.Type = EFoundationLootModifier::ApplyBonus;
            Modifier.Bonus = Bonus;
            Modifier.BonusEnchantment = Want.BonusEnchantment;
            Modifier.BonusMultiplier = Want.BonusMultiplier;
        }
        if (Want.bExplosionDecay) Expected.AddDefaulted_GetRef().Type = EFoundationLootModifier::ExplosionDecay;
        if (Got.Modifiers.Num() != Expected.Num()) return false;
        for (int32 Index = 0; Index < Expected.Num(); ++Index)
        {
            const FFoundationLootModifier& A = Got.Modifiers[Index];
            const FFoundationLootModifier& B = Expected[Index];
            if (A.Type != B.Type || A.Min != B.Min || A.Max != B.Max || A.bAdd || !A.Conditions.IsEmpty() ||
                A.Bonus != B.Bonus || A.BonusEnchantment != B.BonusEnchantment ||
                A.BonusMultiplier != B.BonusMultiplier)
                return false;
        }
        return true;
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

        // The block's default state at the origin, with one property set when Property is given.
        void Put(FName Block, FName Property = NAME_None, FName Value = NAME_None)
        {
            FFoundationBlockState State = A.World.FindBlock(Block)->GetDefaultState();
            if (!Property.IsNone()) State = A.World.GetRegistry().WithValue(State, Property, Value);
            A.World.Set(FIntVector::ZeroValue, State);
        }
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
    TestEqual(TEXT("loot_tables.json: 602 tables"), Tables.Num(), 602);
    TestEqual(TEXT("loot_tables.json: 2 predicates"), Predicates.Num(), 2);

    // DA_Items against the reference, in order and field by field.
    int32 Wrong = 0, Fields = 0;
    auto Expect = [this, &Wrong, &Fields](bool bSame, const FString& What)
    {
        ++Fields;
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
            const FFoundationLootPoolAsset& GotPool = Got.Pools[P];
            const FFoundationLootPoolAsset& WantPool = Want.Pools[P];
            const FString InPool = Where + FString::Printf(TEXT(" pool %d: "), P);
            Expect(GotPool.Rolls == WantPool.Rolls, InPool + TEXT("rolls"));
            Expect(GotPool.Tests == WantPool.Tests, InPool + TEXT("tests"));
            Expect(GotPool.Modifiers.Num() == WantPool.Modifiers.Num(), InPool + TEXT("modifier count"));
            for (int32 M = 0; M < FMath::Min(GotPool.Modifiers.Num(), WantPool.Modifiers.Num()); ++M)
                Expect(LootTests_SameModifierAsset(GotPool.Modifiers[M], WantPool.Modifiers[M]),
                    InPool + FString::Printf(TEXT("modifier %d"), M));
            const TArray<FFoundationLootCandidateAsset>& GotCandidates = GotPool.Candidates;
            const TArray<FFoundationLootCandidateAsset>& WantCandidates = WantPool.Candidates;
            Expect(GotCandidates.Num() == WantCandidates.Num(), InPool + TEXT("candidate count"));
            for (int32 C = 0; C < FMath::Min(GotCandidates.Num(), WantCandidates.Num()); ++C)
            {
                const FFoundationLootCandidateAsset& G = GotCandidates[C];
                const FFoundationLootCandidateAsset& W = WantCandidates[C];
                const FString At = InPool + FString::Printf(TEXT("candidate %d: "), C);
                Expect(G.Item == W.Item, At + TEXT("item ") + G.Item.ToString());
                Expect(G.Conditions == W.Conditions, At + TEXT("conditions"));
                Expect(G.CountMin == W.CountMin && G.CountMax == W.CountMax,
                    At + FString::Printf(TEXT("count %d-%d"), G.CountMin, G.CountMax));
                Expect(G.BonusFormula == W.BonusFormula, At + TEXT("bonus formula ") + G.BonusFormula.ToString());
                Expect(G.BonusEnchantment == W.BonusEnchantment,
                    At + TEXT("bonus enchantment ") + G.BonusEnchantment.ToString());
                Expect(G.BonusMultiplier == W.BonusMultiplier, At + TEXT("bonus multiplier"));
                Expect(G.bExplosionDecay == W.bExplosionDecay, At + TEXT("explosion decay"));
                Expect(G.Tests == W.Tests, At + TEXT("tests"));
                Expect(G.Modifiers.Num() == W.Modifiers.Num(), At + TEXT("modifier count"));
                for (int32 M = 0; M < FMath::Min(G.Modifiers.Num(), W.Modifiers.Num()); ++M)
                    Expect(LootTests_SameModifierAsset(G.Modifiers[M], W.Modifiers[M]),
                        At + FString::Printf(TEXT("modifier %d"), M));
            }
        }
        Expect(Got.Conditions.Num() == Want.Conditions.Num(), Where + TEXT(": condition node count"));
        for (int32 N = 0; N < FMath::Min(Got.Conditions.Num(), Want.Conditions.Num()); ++N)
            Expect(LootTests_SameConditionAsset(Got.Conditions[N], Want.Conditions[N]),
                Where + FString::Printf(TEXT(": condition node %d (%s)"), N, *Want.Conditions[N].Type.ToString()));
        Expect(Got.Modifiers.Num() == Want.Modifiers.Num(), Where + TEXT(": table modifier count"));
        for (int32 M = 0; M < FMath::Min(Got.Modifiers.Num(), Want.Modifiers.Num()); ++M)
            Expect(LootTests_SameModifierAsset(Got.Modifiers[M], Want.Modifiers[M]),
                Where + FString::Printf(TEXT(": table modifier %d"), M));
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
    AddInfo(FString::Printf(TEXT("DA_Items vs the reference: %d fields compared"), Fields));
    TestTrue(TEXT("the predicates are tool/can_silk_touch and tool/can_shear"),
        Predicates.Num() == 2 && Predicates[0].Name == TEXT("tool/can_silk_touch") &&
            Predicates[1].Name == TEXT("tool/can_shear"));
    TestTrue(TEXT("can_shear: shears, no enchantments"),
        Predicates.Num() == 2 && Predicates[1].Items == TArray<FName>{TEXT("shears")} &&
            Predicates[1].Enchantments.IsEmpty());

    // BuildRules: every table whose items, predicates and condition kinds the project defines.
    FFoundationRules Rules;
    GetDefault<UFoundationInteractionSettings>()->BuildRules(Rules);
    FFoundationRules Fresh;
    Fresh.Items = Rules.Items;
    const int32 Skipped = Assets->AddLoot(Fresh);
    TestEqual(TEXT("AddLoot on the same items skips 0 tables (loot step 2b)"), Skipped, 0);
    TestEqual(TEXT("AddLoot on the same items attaches 602 tables"), Fresh.LootTables.Num(), 602);
    TestEqual(TEXT("BuildRules attaches 602 tables (602 - 0)"), Rules.LootTables.Num(), 602);
    TestTrue(TEXT("gold_nugget is an item (loot step 2b)"), Rules.FindItem(TEXT("gold_nugget")) != INDEX_NONE);
    TestEqual(TEXT("two predicates in the rules"), Rules.LootPredicates.Num(), 2);

    // The skipped set, derived at runtime and pinned: empty since loot step 2b.
    TSet<FName> SkippedBlocks;
    for (int32 T = 0; T < Tables.Num(); ++T)
        if (!Rules.LootTables.Contains(Tables[T].Block)) SkippedBlocks.Add(Tables[T].Block);
    const TArray<FName>& Pins = LootTests_SkippedPins();
    TestEqual(TEXT("0 pins"), Pins.Num(), 0);
    TestEqual(TEXT("0 skipped blocks"), SkippedBlocks.Num(), 0);
    for (const FName Pin : Pins)
        TestTrue(Pin.ToString() + TEXT(" is skipped"), SkippedBlocks.Contains(Pin));
    for (const FName Block : SkippedBlocks)
        TestTrue(Block.ToString() + TEXT(" is pinned as skipped"), Pins.Contains(Block));
    // The two double plants use location_check and are attached: each runtime table holds exactly two LocationCheck
    // nodes, the lower half looking one cell up for the upper half and the upper half one cell down for the lower
    // (the jar's offsets [0, 1, 0] and [0, -1, 0] are FIntVector(0, 0, 1) and (0, 0, -1) in cells).
    for (const FName Block : {FName(TEXT("tall_grass")), FName(TEXT("large_fern"))})
    {
        TestTrue(Block.ToString() + TEXT(" uses location_check"),
            Tables.ContainsByPredicate(
                [Block](const FFoundationLootTableAsset& Table)
                {
                    return Table.Block == Block &&
                        Table.Conditions.ContainsByPredicate([](const FFoundationLootConditionAsset& Condition)
                            { return Condition.Type == TEXT("location_check"); });
                }));
        const FFoundationLootTable* Plant = Rules.LootTables.Find(Block);
        if (!TestNotNull(Block.ToString() + TEXT(" is attached"), Plant)) continue;
        TArray<const FFoundationLootCondition*> Checks;
        for (const FFoundationLootCondition& Condition : Plant->Conditions)
            if (Condition.Type == EFoundationLootCondition::LocationCheck) Checks.Add(&Condition);
        if (!TestEqual(Block.ToString() + TEXT(": two LocationCheck nodes"), Checks.Num(), 2)) continue;
        TestTrue(Block.ToString() + TEXT(": one cell up is the upper half"),
            Checks[0]->Offset == FIntVector(0, 0, 1) && Checks[0]->Name == Block &&
                Checks[0]->State == TArray<TPair<FName, FName>>{{TEXT("half"), TEXT("upper")}});
        TestTrue(Block.ToString() + TEXT(": one cell down is the lower half"),
            Checks[1]->Offset == FIntVector(0, 0, -1) && Checks[1]->Name == Block &&
                Checks[1]->State == TArray<TPair<FName, FName>>{{TEXT("half"), TEXT("lower")}});
    }

    // The runtime tables: Index is the JSON position; pools, candidates, nodes and modifiers as AddLoot maps them.
    int32 RuntimeWrong = 0, Checked = 0;
    auto Same = [this, &RuntimeWrong](bool bSame, const FString& What)
    {
        if (!bSame && RuntimeWrong++ < 30) AddError(What);
    };
    for (int32 T = 0; T < Tables.Num(); ++T)
    {
        const FFoundationLootTableAsset& Want = Tables[T];
        const FFoundationLootTable* Got = Rules.LootTables.Find(Want.Block);
        if (!Got) continue;
        ++Checked;
        const FString Where = FString::Printf(TEXT("runtime table %d (%s): "), T, *Want.Block.ToString());
        Same(Got->Index == T, Where + FString::Printf(TEXT("Index %d"), Got->Index));
        Same(Got->Pools.Num() == Want.Pools.Num(), Where + TEXT("pool count"));
        int32 Names = 0;
        for (const FFoundationLootPoolAsset& Pool : Want.Pools)
            for (const FFoundationLootCandidateAsset& Candidate : Pool.Candidates)
                Names += Candidate.Conditions.Num();
        Same(Got->Conditions.Num() == Want.Conditions.Num() + Names, Where + TEXT("node count"));
        for (int32 N = 0; N < FMath::Min(Got->Conditions.Num(), Want.Conditions.Num()); ++N)
            Same(LootTests_SameCondition(Got->Conditions[N], Want.Conditions[N], Rules.LootPredicates),
                Where + FString::Printf(TEXT("node %d (%s)"), N, *Want.Conditions[N].Type.ToString()));
        Same(LootTests_SameModifiers(Got->Modifiers, Want.Modifiers), Where + TEXT("table modifiers"));
        int32 NextName = Want.Conditions.Num();
        for (int32 P = 0; P < FMath::Min(Got->Pools.Num(), Want.Pools.Num()); ++P)
        {
            const FFoundationLootPool& GotPool = Got->Pools[P];
            const FFoundationLootPoolAsset& WantPool = Want.Pools[P];
            const FString InPool = Where + FString::Printf(TEXT("pool %d: "), P);
            Same(GotPool.Rolls == WantPool.Rolls, InPool + TEXT("rolls"));
            Same(GotPool.Conditions == WantPool.Tests, InPool + TEXT("conditions"));
            Same(LootTests_SameModifiers(GotPool.Modifiers, WantPool.Modifiers), InPool + TEXT("modifiers"));
            Same(GotPool.Candidates.Num() == WantPool.Candidates.Num(), InPool + TEXT("candidate count"));
            for (int32 C = 0; C < FMath::Min(GotPool.Candidates.Num(), WantPool.Candidates.Num()); ++C)
            {
                const FFoundationLootCandidate& G = GotPool.Candidates[C];
                const FFoundationLootCandidateAsset& W = WantPool.Candidates[C];
                const FString At = InPool + FString::Printf(TEXT("candidate %d: "), C);
                Same(G.Item != INDEX_NONE && G.Item == Rules.FindItem(W.Item), At + TEXT("item ") + W.Item.ToString());
                TArray<int32> WantConditions;
                for (const FName Name : W.Conditions)
                {
                    WantConditions.Add(NextName);
                    Same(Got->Conditions.IsValidIndex(NextName) &&
                            Got->Conditions[NextName].Type == EFoundationLootCondition::Predicate &&
                            Got->Conditions[NextName].Name == Name &&
                            Rules.LootPredicates.IsValidIndex(Got->Conditions[NextName].Predicate) &&
                            Rules.LootPredicates[Got->Conditions[NextName].Predicate].Name == Name,
                        At + TEXT("predicate node ") + Name.ToString());
                    ++NextName;
                }
                WantConditions.Append(W.Tests);
                Same(G.Conditions == WantConditions, At + TEXT("conditions"));
                if (!W.Modifiers.IsEmpty())
                    Same(LootTests_SameModifiers(G.Modifiers, W.Modifiers), At + TEXT("modifiers"));
                else
                {
                    const EFoundationLootBonus Bonus = W.BonusFormula == TEXT("ore_drops")
                        ? EFoundationLootBonus::OreDrops
                        : W.BonusFormula == TEXT("uniform_bonus_count") ? EFoundationLootBonus::UniformBonusCount
                                                                        : EFoundationLootBonus::None;
                    Same(LootTests_SameOreForm(G, W, Bonus), At + TEXT("ore-form modifiers"));
                }
            }
        }
    }
    TestEqual(TEXT("602 runtime tables checked"), Checked, 602);
    TestEqual(TEXT("the runtime tables equal the assets"), RuntimeWrong, 0);

    // The predicates.
    const int32 Silk = Rules.LootPredicates.IndexOfByPredicate(
        [](const FFoundationLootPredicate& Predicate) { return Predicate.Name == TEXT("tool/can_silk_touch"); });
    const int32 Shear = Rules.LootPredicates.IndexOfByPredicate(
        [](const FFoundationLootPredicate& Predicate) { return Predicate.Name == TEXT("tool/can_shear"); });
    if (!TestTrue(
            TEXT("the rules hold tool/can_silk_touch and tool/can_shear"), Silk != INDEX_NONE && Shear != INDEX_NONE))
        return false;
    const FFoundationLootPredicate& SilkTouch = Rules.LootPredicates[Silk];
    TestTrue(TEXT("can_silk_touch lists no items"), SilkTouch.Items.IsEmpty());
    TestTrue(TEXT("can_silk_touch requires silk_touch at level 1 or more"),
        SilkTouch.Enchantments.Num() == 1 && SilkTouch.Enchantments[0].Key == TEXT("silk_touch") &&
            SilkTouch.Enchantments[0].Value == 1);
    TestTrue(TEXT("can_shear lists shears and no enchantments"),
        Rules.LootPredicates[Shear].Items == TArray<FName>{TEXT("shears")} &&
            Rules.LootPredicates[Shear].Enchantments.IsEmpty());
    const int32 Shears = Rules.FindItem(TEXT("shears"));
    // shears may not be an item yet: then nothing passes can_shear.
    int32 ShearPasses = FoundationLoot::Passes(Rules.LootPredicates[Shear], Rules.Hand);
    for (const FFoundationItemDefinition& Item : Rules.Items)
        ShearPasses += FoundationLoot::Passes(Rules.LootPredicates[Shear], Item);
    TestEqual(TEXT("can_shear passes for the shears item only (once, or never when it is not defined)"), ShearPasses,
        Shears == INDEX_NONE ? 0 : 1);
    AddInfo(Shears == INDEX_NONE ? TEXT("shears is not an item yet: can_shear never passes")
                                 : TEXT("shears is an item: can_shear passes for it"));

    // The ores (items S5b, Index < LegacyTables) keep their shape: [the block's own item on Silk Touch; the drop item
    // unconditioned]; nether_gold_ore (table 17) is attached too since loot step 2b (gold_nugget is an item).
    TestTrue(TEXT("nether_gold_ore has a table"), Rules.LootTables.Contains(TEXT("nether_gold_ore")));
    int32 Attached = 0;
    for (int32 T = 0; T < FoundationLoot::LegacyTables; ++T)
    {
        const FFoundationLootTableAsset& Want = Tables[T];
        const FString Block = Want.Block.ToString();
        const FFoundationLootTable* Got = Rules.LootTables.Find(Want.Block);
        if (!TestNotNull(Block + TEXT(" is attached"), Got)) continue;
        ++Attached;
        TestEqual(Block + TEXT(": Index is its position"), Got->Index, T);
        if (!TestTrue(Block + TEXT(": one pool of one roll with two candidates"),
                Got->Pools.Num() == 1 && Got->Pools[0].Rolls == 1 && Got->Pools[0].Candidates.Num() == 2 &&
                    Want.Pools.Num() == 1 && Want.Pools[0].Candidates.Num() == 2))
            continue;
        const FFoundationLootCandidate& Own = Got->Pools[0].Candidates[0];
        const FFoundationLootCandidate& Drop = Got->Pools[0].Candidates[1];
        const FFoundationLootCandidateAsset& WantDrop = Want.Pools[0].Candidates[1];
        TestTrue(Block + TEXT(": candidate 0 is its own item on can_silk_touch"),
            Own.Item != INDEX_NONE && Own.Item == Rules.FindItem(Want.Block) && Own.Conditions.Num() == 1 &&
                Got->Conditions[Own.Conditions[0]].Predicate == Silk);
        TestTrue(Block + TEXT(": candidate 1 is ") + WantDrop.Item.ToString() + TEXT(" without conditions"),
            Drop.Item != INDEX_NONE && Drop.Item == Rules.FindItem(WantDrop.Item) && Drop.Conditions.IsEmpty());
        const EFoundationLootBonus Bonus = WantDrop.BonusFormula == TEXT("ore_drops") ? EFoundationLootBonus::OreDrops
            : WantDrop.BonusFormula == TEXT("uniform_bonus_count") ? EFoundationLootBonus::UniformBonusCount
                                                                   : EFoundationLootBonus::None;
        TestTrue(Block + TEXT(": the drop's count, bonus and decay as the asset"),
            LootTests_SameOreForm(Drop, WantDrop, Bonus));
    }
    TestEqual(TEXT("18 attached ore tables checked"), Attached, 18);

    // nether_gold_ore against loot_tables.json: Silk Touch gives itself, otherwise gold_nugget x2-6 with ore_drops
    // (multiplier 0) and explosion decay.
    const FFoundationLootTable* NetherGold = Rules.LootTables.Find(TEXT("nether_gold_ore"));
    if (TestNotNull(TEXT("nether_gold_ore's table"), NetherGold) && NetherGold->Pools.Num() == 1 &&
        NetherGold->Pools[0].Candidates.Num() == 2)
    {
        const FFoundationLootCandidate& Own = NetherGold->Pools[0].Candidates[0];
        const FFoundationLootCandidate& Nugget = NetherGold->Pools[0].Candidates[1];
        TestEqual(TEXT("nether_gold_ore is table 17"), NetherGold->Index, 17);
        TestTrue(TEXT("nether_gold_ore: Silk Touch gives itself"),
            Own.Item == Rules.FindItem(TEXT("nether_gold_ore")) && Own.Conditions.Num() == 1 &&
                NetherGold->Conditions[Own.Conditions[0]].Predicate == Silk);
        TestTrue(TEXT("nether_gold_ore: otherwise gold_nugget without conditions"),
            Nugget.Item == Rules.FindItem(TEXT("gold_nugget")) && Nugget.Conditions.IsEmpty());
        TestTrue(TEXT("nether_gold_ore: gold_nugget count 2-6"), LootTests_Range(Nugget) == FIntPoint(2, 6));
        TestTrue(TEXT("nether_gold_ore: ore_drops multiplier 0, then explosion decay"),
            Nugget.Modifiers.Num() == 3 && Nugget.Modifiers[1].Bonus == EFoundationLootBonus::OreDrops &&
                Nugget.Modifiers[1].BonusMultiplier == 0.f &&
                Nugget.Modifiers[2].Type == EFoundationLootModifier::ExplosionDecay);
    }
    else
        AddError(TEXT("nether_gold_ore is not one pool of two candidates"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationLootDropsTest, "Unnamed.Foundation.Loot.Drops", LootTestFlags)

bool FFoundationLootDropsTest::RunTest(const FString&)
{
    TArray<FString> Tools;
    TArray<FLootBreakRow> Rows;
    const UFoundationItemAssets* Assets = UFoundationItemAssets::LoadDefault();
    if (!TestTrue(TEXT("break_times.json and DA_Items' 18 tables"),
            LootTests_ReadRows(Tools, Rows) && Assets && Assets->LootTables.Num() >= FoundationLoot::LegacyTables))
        return false;
    TestEqual(TEXT("the hand and 35 tools"), Tools.Num(), 36);

    // Tallies through the command path, and the same computed from break_times.json and the rules.
    int32 Entries = 0, Eligible = 0, DataEntries = 0, DataEligible = 0, Wrong = 0;
    TMap<FName, int32> DropsPerItem, DataDropsPerItem;
    auto Fail = [this, &Wrong](const FString& What)
    {
        if (Wrong++ < 30) AddError(What);
    };
    // Items S5b's ores: the tables before LegacyTables (the pins below are theirs).
    for (const FFoundationLootTableAsset& Asset :
        TConstArrayView<FFoundationLootTableAsset>(Assets->LootTables.GetData(), FoundationLoot::LegacyTables))
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
        TestNotNull(Block + TEXT(" has a table (every ore, since loot step 2b)"), Table);
        const FFoundationLootCandidate* Candidate =
            Table && Table->Pools.Num() == 1 && Table->Pools[0].Candidates.Num() == 2 ? &Table->Pools[0].Candidates[1]
                                                                                      : nullptr;
        if (Table && !Candidate)
        {
            AddError(Block + TEXT(": not one pool of two candidates"));
            continue;
        }
        // Every ore has a table now; were one missing, the block would drop itself: its own item x1.
        const int32 Own = Rules.FindItem(Asset.Block);
        const int32 DropItem = Candidate ? Candidate->Item : Own;
        const int32 Min = Candidate ? LootTests_Range(*Candidate).X : 1,
                    Max = Candidate ? LootTests_Range(*Candidate).Y : 1;
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

    // Pinned from break_times.json and loot_tables.json (18 ores x 36 columns; pickaxes of the ore's tier). Since loot
    // step 2b nether_gold_ore has a table: its 7 eligible entries (the 7 pickaxes) roll gold_nugget, not itself, so the
    // tally entry is 7 x gold_nugget (derived from its table; the other nine kinds are unchanged, 75 + 7 = 82).
    const TMap<FName, int32> PinnedDrops = {{TEXT("coal"), 14}, {TEXT("raw_copper"), 10}, {TEXT("raw_iron"), 10},
        {TEXT("raw_gold"), 6}, {TEXT("diamond"), 6}, {TEXT("emerald"), 6}, {TEXT("lapis_lazuli"), 10},
        {TEXT("redstone"), 6}, {TEXT("quartz"), 7}, {TEXT("gold_nugget"), 7}};
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
    int32 Ores = 0;
    for (const TPair<FName, FFoundationLootTable>& Pair : Rules.LootTables)
        Ores += Pair.Value.Index < FoundationLoot::LegacyTables;
    if (!TestTrue(TEXT("18 ore tables and an iron pickaxe"), Ores == 18 && IronIndex != INDEX_NONE)) return false;
    const FFoundationItemDefinition& IronPickaxe = Rules.Items[IronIndex];

    // Every attached table with a non-silk tool, 6,000 break counts at three cells.
    const FIntVector Cells[] = {FIntVector(0, 0, 0), FIntVector(5, -3, 12), FIntVector(-17, 40, -60)};
    constexpr int32 BreakCounts = 6000;
    TSet<FName> Spread;
    TArray<FFoundationLootDrop> Drops;
    for (const TPair<FName, FFoundationLootTable>& Pair : Rules.LootTables)
    {
        if (Pair.Value.Index >= FoundationLoot::LegacyTables) continue;
        const FString Block = Pair.Key.ToString();
        if (!TestTrue(Block + TEXT(": one pool of two candidates"),
                Pair.Value.Pools.Num() == 1 && Pair.Value.Pools[0].Candidates.Num() == 2))
            continue;
        const FFoundationLootCandidate& Drop = Pair.Value.Pools[0].Candidates[1];
        const FName Item = Rules.GetItem(Drop.Item).Id;
        TArray<int32> Hits;
        const FIntPoint Range = LootTests_Range(Drop);
        Hits.SetNumZeroed(FMath::Max(1, Range.Y - Range.X + 1));
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
                else if (Drops[0].Count < Range.X || Drops[0].Count > Range.Y)
                    ++Outside;
                else
                    ++Hits[Drops[0].Count - Range.X];
            }
        const FString What = Block + TEXT(" (") + Item.ToString() + TEXT(")");
        TestEqual(What + TEXT(": one stack of the drop per roll"), WrongItem, 0);
        TestEqual(What + TEXT(": no count outside the range"), Outside, 0);
        if (Range.Y > Range.X)
            Spread.Add(Item);
        else
            TestTrue(What + TEXT(": always 1"), Range == FIntPoint(1, 1) && Hits[0] == Total);
        const double Uniform = double(Total) / Hits.Num();
        for (int32 Value = 0; Value < Hits.Num(); ++Value)
        {
            const FString Count = FString::Printf(TEXT(": x%d"), Range.X + Value);
            TestTrue(What + Count + TEXT(" reached"), Hits[Value] > 0);
            if (Hits.Num() > 1)
                TestTrue(What + Count + FString::Printf(TEXT(" %d of %d within 10%% of uniform"), Hits[Value], Total),
                    FMath::Abs(Hits[Value] - Uniform) <= .1 * Uniform);
        }
    }
    // gold_nugget (nether_gold_ore, x2-6 uniform, 1/5 each at level 0) joined the ranged counts in loot step 2b.
    TestTrue(TEXT("ranged counts: raw_copper, lapis_lazuli, redstone and gold_nugget only"),
        Spread.Num() == 4 && Spread.Contains(TEXT("raw_copper")) && Spread.Contains(TEXT("lapis_lazuli")) &&
            Spread.Contains(TEXT("redstone")) && Spread.Contains(TEXT("gold_nugget")));

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

    // Chances over many breaks (loot steps 2a and 2b): 3 cells x 20,000 break counts per table, within 5 sigma.
    {
        constexpr int32 Breaks = 20000;
        const FFoundationItemDefinition& Hand = Rules.Hand;
        auto Near = [this](const FString& What, int32 Hits, int32 N, double P)
        {
            const double Sigma = FMath::Sqrt(N * P * (1 - P));
            AddInfo(FString::Printf(TEXT("%s: %d of %d = %.5f, expected %.5f"), *What, Hits, N, double(Hits) / N, P));
            TestTrue(What +
                    FString::Printf(
                        TEXT(" within 5 sigma (%d of %d, expected %.1f +- %.1f)"), Hits, N, N * P, 5 * Sigma),
                FMath::Abs(Hits - N * P) <= 5 * Sigma + 1e-6);
        };
        // azalea_leaves by hand: the azalea at table_bonus 0.05 and the stick at 0.02, one or two, apart from the
        // block itself (it needs shears or Silk Touch).
        if (const FFoundationLootTable* Azalea = Rules.LootTables.Find(TEXT("azalea_leaves")))
        {
            const FLootTally Tally = LootTests_Tally(Rules, *Azalea, Hand, Breaks);
            Near(TEXT("azalea_leaves by hand: azalea"), Tally.Of(TEXT("azalea:1")), Tally.Rolls, 0.05);
            Near(TEXT("azalea_leaves by hand: stick x1"), Tally.Of(TEXT("stick:1")), Tally.Rolls, 0.01);
            Near(TEXT("azalea_leaves by hand: stick x2"), Tally.Of(TEXT("stick:2")), Tally.Rolls, 0.01);
            TestEqual(TEXT("azalea_leaves by hand never drops itself"), Tally.Of(TEXT("azalea_leaves:1")), 0);
            TestEqual(TEXT("azalea_leaves by hand drops only azalea and sticks"), Tally.Hits.Num(), 3);
            if (const int32 Shears = Rules.FindItem(TEXT("shears")); Shears != INDEX_NONE)
            {
                const FLootTally Sheared = LootTests_Tally(Rules, *Azalea, Rules.Items[Shears], 2000);
                TestEqual(TEXT("azalea_leaves with shears: itself every time, nothing else"),
                    Sheared.Of(TEXT("azalea_leaves:1")), Sheared.Rolls);
                TestEqual(TEXT("azalea_leaves with shears: one kind of drop"), Sheared.Hits.Num(), 1);
            }
        }
        else
            AddError(TEXT("azalea_leaves has no table"));
        if (const FFoundationLootTable* Mangrove = Rules.LootTables.Find(TEXT("mangrove_leaves")))
        {
            const FLootTally Tally = LootTests_Tally(Rules, *Mangrove, Hand, Breaks);
            Near(TEXT("mangrove_leaves by hand: stick x1"), Tally.Of(TEXT("stick:1")), Tally.Rolls, 0.01);
            Near(TEXT("mangrove_leaves by hand: stick x2"), Tally.Of(TEXT("stick:2")), Tally.Rolls, 0.01);
            TestEqual(TEXT("mangrove_leaves by hand drops only sticks"), Tally.Hits.Num(), 2);
        }
        else
            AddError(TEXT("mangrove_leaves has no attached table"));
        // brown_mushroom_block by hand: set_count uniform in [-6, 2], then limit_count min 0.
        if (const FFoundationLootTable* Mushroom = Rules.LootTables.Find(TEXT("brown_mushroom_block")))
        {
            const FLootTally Tally = LootTests_Tally(Rules, *Mushroom, Hand, Breaks);
            Near(TEXT("brown_mushroom_block by hand: brown_mushroom x1"), Tally.Of(TEXT("brown_mushroom:1")),
                Tally.Rolls, 1. / 9);
            Near(TEXT("brown_mushroom_block by hand: brown_mushroom x2"), Tally.Of(TEXT("brown_mushroom:2")),
                Tally.Rolls, 1. / 9);
            Near(TEXT("brown_mushroom_block by hand: nothing"), Tally.Nothing, Tally.Rolls, 7. / 9);
            TestEqual(TEXT("brown_mushroom_block by hand drops only mushrooms x1-2"), Tally.Hits.Num(), 2);
        }
        else
            AddError(TEXT("brown_mushroom_block has no table"));
        // dead_bush by hand: sticks, set_count 0 to 2.
        if (const FFoundationLootTable* Bush = Rules.LootTables.Find(TEXT("dead_bush")))
        {
            const FLootTally Tally = LootTests_Tally(Rules, *Bush, Hand, Breaks);
            Near(TEXT("dead_bush by hand: stick x1"), Tally.Of(TEXT("stick:1")), Tally.Rolls, 1. / 3);
            Near(TEXT("dead_bush by hand: stick x2"), Tally.Of(TEXT("stick:2")), Tally.Rolls, 1. / 3);
            Near(TEXT("dead_bush by hand: nothing"), Tally.Nothing, Tally.Rolls, 1. / 3);
            TestEqual(TEXT("dead_bush by hand drops only sticks"), Tally.Hits.Num(), 2);
        }
        else
            AddError(TEXT("dead_bush has no table"));
        // gravel by hand (loot step 2b): exactly one item per roll; flint at table_bonus level 0 (0.1), else gravel.
        if (const FFoundationLootTable* Gravel = Rules.LootTables.Find(TEXT("gravel")))
        {
            const FLootTally Tally = LootTests_Tally(Rules, *Gravel, Hand, Breaks);
            Near(TEXT("gravel by hand: flint"), Tally.Of(TEXT("flint:1")), Tally.Rolls, 0.1);
            Near(TEXT("gravel by hand: gravel"), Tally.Of(TEXT("gravel:1")), Tally.Rolls, 0.9);
            TestEqual(TEXT("gravel by hand: always exactly one item"),
                Tally.Of(TEXT("flint:1")) + Tally.Of(TEXT("gravel:1")), Tally.Rolls);
            TestEqual(TEXT("gravel by hand: never nothing"), Tally.Nothing, 0);
            TestEqual(TEXT("gravel by hand drops only flint and gravel"), Tally.Hits.Num(), 2);
        }
        else
            AddError(TEXT("gravel has no table"));
        // oak_leaves by hand: the sapling at 0.05, the apple at 0.005 and the stick at 0.02 (x1 or x2, 0.01 each),
        // apart from the block itself (it needs shears or Silk Touch).
        if (const FFoundationLootTable* Oak = Rules.LootTables.Find(TEXT("oak_leaves")))
        {
            const FLootTally Tally = LootTests_Tally(Rules, *Oak, Hand, Breaks);
            Near(TEXT("oak_leaves by hand: oak_sapling"), Tally.Of(TEXT("oak_sapling:1")), Tally.Rolls, 0.05);
            Near(TEXT("oak_leaves by hand: apple"), Tally.Of(TEXT("apple:1")), Tally.Rolls, 0.005);
            Near(TEXT("oak_leaves by hand: stick x1"), Tally.Of(TEXT("stick:1")), Tally.Rolls, 0.01);
            Near(TEXT("oak_leaves by hand: stick x2"), Tally.Of(TEXT("stick:2")), Tally.Rolls, 0.01);
            TestEqual(TEXT("oak_leaves by hand never drops itself"), Tally.Of(TEXT("oak_leaves:1")), 0);
            TestEqual(TEXT("oak_leaves by hand drops only saplings, apples and sticks"), Tally.Hits.Num(), 4);
        }
        else
            AddError(TEXT("oak_leaves has no table"));
        // short_grass by hand: wheat_seeds at random_chance 0.125 (uniform_bonus_count adds 0 at level 0), never the
        // block (it needs shears).
        if (const FFoundationLootTable* Grass = Rules.LootTables.Find(TEXT("short_grass")))
        {
            const FLootTally Tally = LootTests_Tally(Rules, *Grass, Hand, Breaks);
            Near(TEXT("short_grass by hand: wheat_seeds"), Tally.Of(TEXT("wheat_seeds:1")), Tally.Rolls, 0.125);
            Near(TEXT("short_grass by hand: nothing"), Tally.Nothing, Tally.Rolls, 0.875);
            TestEqual(TEXT("short_grass by hand never drops itself"), Tally.Of(TEXT("short_grass:1")), 0);
            TestEqual(TEXT("short_grass by hand drops only wheat_seeds x1"), Tally.Hits.Num(), 1);
        }
        else
            AddError(TEXT("short_grass has no table"));
    }

    // Synthetic tables, built in code.
    {
        const FFoundationItemDefinition& Hand = Rules.Hand;
        const TArray<FFoundationLootPredicate> NoPredicates;
        const TArray<TPair<FName, FName>> NoState;
        FRandomStream Dice(1);
        auto Near = [this](const FString& What, int32 Hits, int32 N, double P)
        {
            const double Sigma = FMath::Sqrt(N * P * (1 - P));
            TestTrue(What +
                    FString::Printf(
                        TEXT(" within 5 sigma (%d of %d, expected %.1f +- %.1f)"), Hits, N, N * P, 5 * Sigma),
                FMath::Abs(Hits - N * P) <= 5 * Sigma + 1e-6);
        };
        auto Roll = [&](const FFoundationLootTable& Table, FName Block, const TArray<TPair<FName, FName>>& State,
                        bool bEntity, const FFoundationItemDefinition& Tool, int32 Seed)
        {
            TArray<FFoundationLootDrop> Out;
            FRandomStream Random(Seed);
            FoundationLoot::Roll(Table, NoPredicates, FFoundationLootContext{Tool, Block, State, bEntity}, Random, Out);
            return Out;
        };
        auto Passes = [&](const FFoundationLootTable& Table, int32 Index, FName Block,
                          const TArray<TPair<FName, FName>>& State, bool bEntity, const FFoundationItemDefinition& Tool)
        {
            return FoundationLoot::Test(
                Table, Index, NoPredicates, FFoundationLootContext{Tool, Block, State, bEntity}, Dice);
        };

        // inverted, any_of and all_of: truth tables over true (survives_explosion) and false (its inversion).
        {
            FFoundationLootTable T;
            using E = EFoundationLootCondition;
            const int32 True = LootTests_AddCondition(T, E::SurvivesExplosion);
            const int32 False = LootTests_AddCondition(T, E::Inverted, {True});
            const int32 AnyNone = LootTests_AddCondition(T, E::AnyOf);
            const int32 AllNone = LootTests_AddCondition(T, E::AllOf);
            const int32 AnyFT = LootTests_AddCondition(T, E::AnyOf, {False, True});
            const int32 AnyFF = LootTests_AddCondition(T, E::AnyOf, {False, False});
            const int32 AllTF = LootTests_AddCondition(T, E::AllOf, {True, False});
            const int32 AllTT = LootTests_AddCondition(T, E::AllOf, {True, True});
            const int32 InvNone = LootTests_AddCondition(T, E::Inverted);
            const int32 InvTwo = LootTests_AddCondition(T, E::Inverted, {False, False});
            const int32 InvAnyFT = LootTests_AddCondition(T, E::Inverted, {AnyFT});
            const int32 InvAnyFF = LootTests_AddCondition(T, E::Inverted, {AnyFF});
            const int32 InvInvTrue = LootTests_AddCondition(T, E::Inverted, {False});
            const int32 Nested = LootTests_AddCondition(T, E::AllOf, {AnyFT, InvAnyFF, InvInvTrue});
            const int32 BadTerm = LootTests_AddCondition(T, E::AnyOf, {99});
            const TPair<int32, bool> Table[] = {{True, true}, {False, false}, {AnyNone, false}, {AllNone, true},
                {AnyFT, true}, {AnyFF, false}, {AllTF, false}, {AllTT, true}, {InvNone, false}, {InvTwo, false},
                {InvAnyFT, false}, {InvAnyFF, true}, {InvInvTrue, true}, {Nested, true}, {BadTerm, false}, {99, false},
                {-1, false}};
            int32 Mismatch = 0;
            for (const TPair<int32, bool>& Row : Table)
                if (Passes(T, Row.Key, TEXT("stone"), NoState, true, Hand) != Row.Value)
                {
                    ++Mismatch;
                    AddError(FString::Printf(
                        TEXT("truth table: node %d should be %s"), Row.Key, Row.Value ? TEXT("true") : TEXT("false")));
                }
            TestEqual(TEXT("truth table mismatches"), Mismatch, 0);
        }

        // match_block with and without state; entity_properties; match_tool.
        {
            FFoundationLootTable T;
            using E = EFoundationLootCondition;
            const int32 Stone = LootTests_AddCondition(T, E::MatchBlock, {}, TEXT("stone"));
            const int32 StoneDouble = LootTests_AddCondition(T, E::MatchBlock, {}, TEXT("stone"));
            T.Conditions[StoneDouble].State = {{TEXT("type"), TEXT("double")}};
            const int32 StoneBoth = LootTests_AddCondition(T, E::MatchBlock, {}, TEXT("stone"));
            T.Conditions[StoneBoth].State = {{TEXT("type"), TEXT("double")}, {TEXT("waterlogged"), TEXT("true")}};
            const int32 Dirt = LootTests_AddCondition(T, E::MatchBlock, {}, TEXT("dirt"));
            const TArray<TPair<FName, FName>> Double = {{TEXT("type"), TEXT("double")}};
            const TArray<TPair<FName, FName>> Bottom = {{TEXT("type"), TEXT("bottom")}};
            const TArray<TPair<FName, FName>> Wet = {
                {TEXT("waterlogged"), TEXT("true")}, {TEXT("type"), TEXT("double")}};
            // Rows: stone with no state, double, bottom, double and waterlogged; then dirt.
            struct FRow
            {
                const TArray<TPair<FName, FName>>* State;
                FName Block;
                bool bStone, bDouble, bBoth, bDirt;
            };
            const FRow Rows[] = {{&NoState, TEXT("stone"), true, false, false, false},
                {&Double, TEXT("stone"), true, true, false, false}, {&Bottom, TEXT("stone"), true, false, false, false},
                {&Wet, TEXT("stone"), true, true, true, false}, {&Double, TEXT("dirt"), false, false, false, true},
                {&NoState, TEXT("dirt"), false, false, false, true}};
            int32 Mismatch = 0;
            for (const FRow& Row : Rows)
            {
                const bool Got[] = {Passes(T, Stone, Row.Block, *Row.State, true, Hand),
                    Passes(T, StoneDouble, Row.Block, *Row.State, true, Hand),
                    Passes(T, StoneBoth, Row.Block, *Row.State, true, Hand),
                    Passes(T, Dirt, Row.Block, *Row.State, true, Hand)};
                const bool Want[] = {Row.bStone, Row.bDouble, Row.bBoth, Row.bDirt};
                for (int32 Index = 0; Index < 4; ++Index)
                    if (Got[Index] != Want[Index])
                    {
                        ++Mismatch;
                        AddError(FString::Printf(TEXT("match_block row %s %d: node %d should be %d"),
                            *Row.Block.ToString(), int32(Row.State->Num()), Index, int32(Want[Index])));
                    }
            }
            TestEqual(TEXT("match_block: 24 cells, with and without state"), Mismatch, 0);

            const int32 Entity = LootTests_AddCondition(T, E::EntityProperties, {}, TEXT("this"));
            TestTrue(
                TEXT("entity_properties passes for an entity"), Passes(T, Entity, TEXT("stone"), NoState, true, Hand));
            TestFalse(
                TEXT("entity_properties fails without one"), Passes(T, Entity, TEXT("stone"), NoState, false, Hand));

            const int32 Tool = LootTests_AddCondition(T, E::MatchTool);
            T.Conditions[Tool].Items = {TEXT("iron_pickaxe")};
            const int32 Stone2 = Rules.FindItem(TEXT("stone_pickaxe"));
            TestTrue(TEXT("match_tool passes for a listed tool"),
                Passes(T, Tool, TEXT("stone"), NoState, true, IronPickaxe));
            TestFalse(TEXT("match_tool fails for the hand"), Passes(T, Tool, TEXT("stone"), NoState, true, Hand));
            TestTrue(TEXT("match_tool fails for another tool"),
                Stone2 != INDEX_NONE && !Passes(T, Tool, TEXT("stone"), NoState, true, Rules.Items[Stone2]));
            const int32 NoTool = LootTests_AddCondition(T, E::MatchTool);
            TestFalse(
                TEXT("match_tool with no items fails"), Passes(T, NoTool, TEXT("stone"), NoState, true, IronPickaxe));
        }

        // location_check (loot step 2b): the block and state GetBlockAt reports at the condition's offset.
        {
            FFoundationLootTable T;
            using E = EFoundationLootCondition;
            const int32 Check = LootTests_AddCondition(T, E::LocationCheck, {}, TEXT("tall_grass"));
            T.Conditions[Check].Offset = FIntVector(0, 0, 1);
            T.Conditions[Check].State = {{TEXT("half"), TEXT("upper")}};
            FFoundationLootContext Context{Hand, TEXT("tall_grass"), NoState, true};
            TestFalse(TEXT("location_check fails with GetBlockAt unset"),
                FoundationLoot::Test(T, Check, NoPredicates, Context, Dice));

            FIntVector Asked(99, 99, 99);
            int32 Calls = 0;
            FName Found = TEXT("tall_grass");
            TArray<TPair<FName, FName>> FoundState = {{TEXT("half"), TEXT("upper")}};
            bool bKnown = false;
            Context.GetBlockAt = [&](const FIntVector& Offset, FName& Block, TArray<TPair<FName, FName>>& State)
            {
                Asked = Offset;
                ++Calls;
                Block = Found;
                State = FoundState;
                return bKnown;
            };
            auto Pass = [&] { return FoundationLoot::Test(T, Check, NoPredicates, Context, Dice); };
            TestFalse(TEXT("location_check fails where GetBlockAt returns false (the right block and state)"), Pass());
            bKnown = true;
            Found = TEXT("dirt");
            TestFalse(TEXT("location_check fails on the wrong block"), Pass());
            Found = TEXT("tall_grass");
            FoundState = {{TEXT("half"), TEXT("lower")}};
            TestFalse(TEXT("location_check fails on the right block with the wrong state"), Pass());
            FoundState = {};
            TestFalse(TEXT("location_check fails on the right block with no state"), Pass());
            FoundState = {{TEXT("half"), TEXT("upper")}};
            Calls = 0;
            Asked = FIntVector(99, 99, 99);
            TestTrue(TEXT("location_check passes on the right block and state"), Pass());
            TestEqual(TEXT("location_check asks GetBlockAt once"), Calls, 1);
            TestTrue(TEXT("location_check hands GetBlockAt the condition's offset (0, 0, 1)"),
                Asked == T.Conditions[Check].Offset && Asked == FIntVector(0, 0, 1));
            FoundState = {{TEXT("waterlogged"), TEXT("false")}, {TEXT("half"), TEXT("upper")}};
            TestTrue(TEXT("location_check passes when the state holds the wanted pair among others"), Pass());
            T.Conditions[Check].Offset = FIntVector(0, 0, -1);
            T.Conditions[Check].State.Reset();
            TestTrue(TEXT("location_check with no wanted state passes on the block alone"), Pass());
            TestTrue(
                TEXT("location_check hands GetBlockAt the changed offset (0, 0, -1)"), Asked == FIntVector(0, 0, -1));
        }

        // random_chance and table_bonus (level 0): the fraction of passes.
        {
            FFoundationLootTable T;
            using E = EFoundationLootCondition;
            const int32 Eighth = LootTests_AddCondition(T, E::RandomChance);
            T.Conditions[Eighth].Chances = {0.125f};
            const int32 Never = LootTests_AddCondition(T, E::RandomChance);
            T.Conditions[Never].Chances = {0.f};
            const int32 Always = LootTests_AddCondition(T, E::RandomChance);
            T.Conditions[Always].Chances = {1.f};
            const int32 Bonus = LootTests_AddCondition(T, E::TableBonus, {}, TEXT("fortune"));
            T.Conditions[Bonus].Chances = {0.25f, 0.5f};
            int32 EighthHits = 0, NeverHits = 0, AlwaysHits = 0, BonusHits = 0;
            constexpr int32 N = 60000;
            for (int32 Break = 0; Break < N; ++Break)
            {
                FRandomStream Stream(FoundationLoot::MakeSeed(0, FIntVector(2, 3, 4), Break));
                const FFoundationLootContext Context{Hand, TEXT("stone"), {}, true};
                EighthHits += FoundationLoot::Test(T, Eighth, NoPredicates, Context, Stream);
                NeverHits += FoundationLoot::Test(T, Never, NoPredicates, Context, Stream);
                AlwaysHits += FoundationLoot::Test(T, Always, NoPredicates, Context, Stream);
                BonusHits += FoundationLoot::Test(T, Bonus, NoPredicates, Context, Stream);
            }
            AddInfo(FString::Printf(TEXT("random_chance 0.125: %d of %d = %.5f; table_bonus level 0 (0.25): %d = %.5f"),
                EighthHits, N, double(EighthHits) / N, BonusHits, double(BonusHits) / N));
            Near(TEXT("random_chance 0.125"), EighthHits, N, 0.125);
            Near(TEXT("table_bonus at level 0 takes the first chance (0.25)"), BonusHits, N, 0.25);
            TestEqual(TEXT("random_chance 0 never passes"), NeverHits, 0);
            TestEqual(TEXT("random_chance 1 always passes"), AlwaysHits, N);

            // As a candidate's condition: the drop follows the chance.
            FFoundationLootTable Candidate = LootTests_Synth({}, {Eighth});
            Candidate.Conditions = T.Conditions;
            int32 Dropped = 0;
            for (int32 Break = 0; Break < N; ++Break)
                Dropped += !Roll(Candidate, TEXT("stone"), NoState, true, Hand, Break).IsEmpty();
            Near(TEXT("a candidate on random_chance 0.125 drops"), Dropped, N, 0.125);
        }

        // set_count add (+1 per matching match_block, then -1), through the candidate, pool and table modifiers.
        {
            FFoundationLootTable T = LootTests_Synth({});
            using E = EFoundationLootCondition;
            const int32 Stone = LootTests_AddCondition(T, E::MatchBlock, {}, TEXT("stone"));
            T.Pools[0].Candidates[0].Modifiers = {
                LootTests_Modifier(EFoundationLootModifier::SetCount, 1, 1, true, {Stone}),
                LootTests_Modifier(EFoundationLootModifier::SetCount, 1, 1, true, {Stone}),
                LootTests_Modifier(EFoundationLootModifier::SetCount, -1, -1, true)};
            TArray<FFoundationLootDrop> Out = Roll(T, TEXT("stone"), NoState, true, Hand, 5);
            TestTrue(TEXT("1 + 1 + 1 - 1 = 2 on a match"), Out.Num() == 1 && Out[0].Item == 7 && Out[0].Count == 2);
            TestTrue(
                TEXT("1 - 1 = 0 drops nothing off a match"), Roll(T, TEXT("dirt"), NoState, true, Hand, 5).IsEmpty());
            // The pool's modifiers follow the candidate's, then the table's: 2 + 10 = 12, clamped to 11.
            T.Pools[0].Modifiers = {LootTests_Modifier(EFoundationLootModifier::SetCount, 10, 10, true)};
            T.Modifiers = {LootTests_Modifier(EFoundationLootModifier::LimitCount, MIN_int32, 11)};
            Out = Roll(T, TEXT("stone"), NoState, true, Hand, 5);
            TestTrue(TEXT("pool +10 then table limit 11: 2 -> 12 -> 11"), Out.Num() == 1 && Out[0].Count == 11);
            Out = Roll(T, TEXT("dirt"), NoState, true, Hand, 5);
            TestTrue(TEXT("pool +10 then table limit 11: 0 -> 10"), Out.Num() == 1 && Out[0].Count == 10);
            // Rolls repeat the pool.
            T.Pools[0].Rolls = 3;
            T.Pools[0].Modifiers.Reset();
            T.Modifiers.Reset();
            Out = Roll(T, TEXT("stone"), NoState, true, Hand, 5);
            TestTrue(TEXT("3 rolls: three stacks of 2"), Out.Num() == 3 && Out[0].Count == 2 && Out[2].Count == 2);
        }

        // limit_count: both bounds, only a minimum, only a maximum, over a uniform [-5, 10] (16 values).
        {
            constexpr int32 N = 48000;
            auto Run = [&](int32 Min, int32 Max)
            {
                const FFoundationLootTable T =
                    LootTests_Synth({LootTests_Modifier(EFoundationLootModifier::SetCount, -5, 10),
                        LootTests_Modifier(EFoundationLootModifier::LimitCount, Min, Max)});
                FLootTally Tally;
                for (int32 Break = 0; Break < N; ++Break)
                {
                    const TArray<FFoundationLootDrop> Out = Roll(T, TEXT("stone"), NoState, true, Hand, 1000 + Break);
                    ++Tally.Rolls;
                    Tally.Nothing += Out.IsEmpty();
                    for (const FFoundationLootDrop& Drop : Out)
                        ++Tally.Hits.FindOrAdd(FString::FromInt(Drop.Count));
                }
                return Tally;
            };
            const FLootTally Both = Run(2, 4);
            Near(TEXT("limit_count 2..4: 2"), Both.Of(TEXT("2")), N, 8. / 16);
            Near(TEXT("limit_count 2..4: 3"), Both.Of(TEXT("3")), N, 1. / 16);
            Near(TEXT("limit_count 2..4: 4"), Both.Of(TEXT("4")), N, 7. / 16);
            TestTrue(TEXT("limit_count 2..4: never nothing, never outside"), Both.Nothing == 0 && Both.Hits.Num() == 3);
            const FLootTally Min = Run(0, MAX_int32);
            Near(TEXT("limit_count min 0: nothing"), Min.Nothing, N, 6. / 16);
            Near(TEXT("limit_count min 0: 10"), Min.Of(TEXT("10")), N, 1. / 16);
            TestEqual(TEXT("limit_count min 0: counts 1..10"), Min.Hits.Num(), 10);
            const FLootTally Max = Run(MIN_int32, 3);
            Near(TEXT("limit_count max 3: nothing (values <= 0)"), Max.Nothing, N, 6. / 16);
            Near(TEXT("limit_count max 3: 1"), Max.Of(TEXT("1")), N, 1. / 16);
            Near(TEXT("limit_count max 3: 3"), Max.Of(TEXT("3")), N, 8. / 16);
            TestEqual(TEXT("limit_count max 3: counts 1..3"), Max.Hits.Num(), 3);
        }

        // Determinism: the same seed rolls the same drops, a random table too.
        {
            FFoundationLootTable T = LootTests_Synth({LootTests_Modifier(EFoundationLootModifier::SetCount, 1, 9)});
            const int32 Chance = LootTests_AddCondition(T, EFoundationLootCondition::RandomChance);
            T.Conditions[Chance].Chances = {0.5f};
            T.Pools[0].Candidates[0].Conditions = {Chance};
            T.Pools[0].Rolls = 4;
            int32 SameRolls = 0, NextDiffers = 0;
            for (int32 Seed = 0; Seed < 200; ++Seed)
            {
                const TArray<FFoundationLootDrop> A = Roll(T, TEXT("stone"), NoState, true, Hand, Seed);
                SameRolls += LootTests_Same(A, Roll(T, TEXT("stone"), NoState, true, Hand, Seed));
                NextDiffers += !LootTests_Same(A, Roll(T, TEXT("stone"), NoState, true, Hand, Seed + 1));
            }
            TestEqual(TEXT("the same seed rolls the same drops, 200 times"), SameRolls, 200);
            TestTrue(TEXT("the next seed differs in most of 200 rolls"), NextDiffers > 100);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationLootBlocksTest, "Unnamed.Foundation.Loot.Blocks", LootTestFlags)

bool FFoundationLootBlocksTest::RunTest(const FString&)
{
    const UFoundationItemAssets* Assets = UFoundationItemAssets::LoadDefault();
    IConsoleVariable* AllBlocks = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Loot.AllBlocks"));
    if (!TestTrue(TEXT("DA_Items and foundation.Loot.AllBlocks"), Assets && AllBlocks)) return false;
    TestEqual(TEXT("foundation.Loot.AllBlocks is 1 (all tables roll)"), AllBlocks->GetInt(), 1);
    FLootLab L(false);
    const FFoundationRules& Rules = L.A.Rules;
    int32 Cases = 0;

    // Mines Block (in State's property) with Tool through the authority and checks the drops, in the result and the
    // inventory, against Want (item to count), or that the block stays when bStays.
    auto Check = [&](FName Block, FName Tool, const TMap<FName, int32>& Want, FName Property = NAME_None,
                     FName Value = NAME_None, bool bStays = false)
    {
        ++Cases;
        const FString What = Block.ToString() +
            (Property.IsNone() ? FString()
                               : TEXT(" (") + Property.ToString() + TEXT("=") + Value.ToString() + TEXT(")")) +
            TEXT(" with ") + (Tool.IsNone() ? FString(TEXT("the hand")) : Tool.ToString());
        if (!TestNotNull(What + TEXT(": the block exists"), L.A.World.FindBlock(Block))) return;
        if (!Tool.IsNone() && !TestTrue(What + TEXT(": the tool exists"), Rules.FindItem(Tool) != INDEX_NONE)) return;
        L.ClearAndHold(Tool);
        L.Put(Block, Property, Value);
        L.NextStep();
        const TMap<int32, int32> Before = LootTests_Counts(L.P().Inventory);
        const FFoundationResult R = LootTests_Mine(L, 8000);
        const TMap<int32, int32> Gains = LootTests_Gains(Before, LootTests_Counts(L.P().Inventory));
        if (bStays)
        {
            TestTrue(What + TEXT(": never breaks in survival"), !R.bBroken && L.Solid() && R.Drops.IsEmpty());
            return;
        }
        if (!TestTrue(What + FString::Printf(TEXT(": broken (%s)"), LexToString(R.Reason)), R.bBroken && !L.Solid()))
            return;
        TMap<FName, int32> Dropped, Gained;
        for (const FFoundationLootDrop& Drop : R.Drops)
            Dropped.FindOrAdd(Rules.GetItem(Drop.Item).Id) += Drop.Count;
        for (const TPair<int32, int32>& Pair : Gains)
            Gained.Add(Rules.GetItem(Pair.Key).Id, Pair.Value);
        // Shovels and pickaxes wear nothing into the counts; only items move.
        TestTrue(
            What + FString::Printf(TEXT(": drops (%d stacks) are as wanted (%d kinds)"), R.Drops.Num(), Want.Num()),
            LootTests_SameTally(Dropped, Want));
        TestTrue(What + TEXT(": the inventory gains the drops"), LootTests_SameTally(Gained, Want));
    };
    const TMap<FName, int32> Nothing;
    auto One = [](FName Item, int32 Count = 1) { return TMap<FName, int32>{{Item, Count}}; };

    // Stone: cobblestone with each pickaxe; by hand it breaks and gives nothing.
    for (const TCHAR* Pickaxe : {TEXT("wooden_pickaxe"), TEXT("stone_pickaxe"), TEXT("copper_pickaxe"),
             TEXT("iron_pickaxe"), TEXT("golden_pickaxe"), TEXT("diamond_pickaxe"), TEXT("netherite_pickaxe")})
        Check(TEXT("stone"), Pickaxe, One(TEXT("cobblestone")));
    Check(TEXT("stone"), NAME_None, Nothing);
    Check(TEXT("deepslate"), TEXT("iron_pickaxe"), One(TEXT("cobbled_deepslate")));
    Check(TEXT("deepslate"), NAME_None, Nothing);

    // Dirt from the grassy blocks, by hand and with a shovel.
    for (const TCHAR* Block : {TEXT("grass_block"), TEXT("podzol"), TEXT("mycelium")})
    {
        Check(Block, NAME_None, One(TEXT("dirt")));
        Check(Block, TEXT("iron_shovel"), One(TEXT("dirt")));
    }

    // Silk-Touch-only blocks give nothing without it.
    for (const TCHAR* Block : {TEXT("glass"), TEXT("white_stained_glass"), TEXT("ice")})
    {
        Check(Block, NAME_None, Nothing);
        Check(Block, TEXT("iron_pickaxe"), Nothing);
    }

    // Slabs: one, or two when double.
    for (const TCHAR* Slab : {TEXT("stone_slab"), TEXT("oak_slab")})
    {
        const FName Tool = FName(Slab) == TEXT("stone_slab") ? FName(TEXT("iron_pickaxe")) : NAME_None;
        Check(Slab, Tool, One(Slab), TEXT("type"), TEXT("bottom"));
        Check(Slab, Tool, One(Slab), TEXT("type"), TEXT("top"));
        Check(Slab, Tool, One(Slab, 2), TEXT("type"), TEXT("double"));
    }

    // Indestructible or nothing to give.
    Check(TEXT("budding_amethyst"), TEXT("iron_pickaxe"), Nothing);
    Check(TEXT("bedrock"), NAME_None, Nothing, NAME_None, NAME_None, true);
    Check(TEXT("bedrock"), TEXT("netherite_pickaxe"), Nothing, NAME_None, NAME_None, true);
    TestTrue(TEXT("budding_amethyst has a table"), Rules.LootTables.Contains(TEXT("budding_amethyst")));

    // The skipped tables (derived at runtime, pinned): none since loot step 2b, so no block keeps "drops itself".
    TSet<FName> Skipped;
    for (const FFoundationLootTableAsset& Table : Assets->LootTables)
        if (!Rules.LootTables.Contains(Table.Block)) Skipped.Add(Table.Block);
    const TArray<FName>& Pins = LootTests_SkippedPins();
    TestEqual(TEXT("0 skipped tables"), Skipped.Num(), 0);
    TestTrue(TEXT("the skipped set is the 0 pinned"),
        Skipped.Num() == Pins.Num() && Algo::AllOf(Pins, [&Skipped](FName Pin) { return Skipped.Contains(Pin); }));

    // Tables that roll (loot step 2b): clay and the snow block give their drop item, not themselves (Silk Touch never
    // passes).
    Check(TEXT("clay"), NAME_None, One(TEXT("clay_ball"), 4));
    Check(TEXT("clay"), TEXT("iron_shovel"), One(TEXT("clay_ball"), 4));
    Check(TEXT("snow_block"), TEXT("iron_shovel"), One(TEXT("snowball"), 4));
    Check(TEXT("snow_block"), NAME_None, Nothing);

    // Mines Block Rounds times with Tool (each round is a new break count, so a new roll) and checks every round's
    // drops against Allowed (item to its count range, one stack per item, MinStacks to MaxStacks stacks) and the
    // inventory against the drops. Returns the stacks dropped over all rounds.
    auto Within = [&](FName Block, FName Tool, const TMap<FName, FIntPoint>& Allowed, int32 MinStacks, int32 MaxStacks,
                      int32 Rounds)
    {
        ++Cases;
        const FString What =
            Block.ToString() + TEXT(" with ") + (Tool.IsNone() ? FString(TEXT("the hand")) : Tool.ToString());
        int32 Stacks = 0, Wrong = 0;
        if (!TestNotNull(What + TEXT(": the block exists"), L.A.World.FindBlock(Block))) return 0;
        for (int32 Round = 0; Round < Rounds; ++Round)
        {
            L.ClearAndHold(Tool);
            L.Put(Block);
            L.NextStep();
            const TMap<int32, int32> Before = LootTests_Counts(L.P().Inventory);
            const FFoundationResult R = LootTests_Mine(L, 8000);
            const TMap<int32, int32> Gains = LootTests_Gains(Before, LootTests_Counts(L.P().Inventory));
            if (!R.bBroken || L.Solid() || R.Drops.Num() < MinStacks || R.Drops.Num() > MaxStacks ||
                Gains.Num() != R.Drops.Num())
            {
                ++Wrong;
                continue;
            }
            TSet<FName> Seen;
            for (const FFoundationLootDrop& Drop : R.Drops)
            {
                const FName Id = Rules.GetItem(Drop.Item).Id;
                const FIntPoint* Range = Allowed.Find(Id);
                Wrong += !Range || Seen.Contains(Id) || Drop.Count < Range->X || Drop.Count > Range->Y ||
                    Gains.FindRef(Drop.Item) != Drop.Count;
                Seen.Add(Id);
            }
            Stacks += R.Drops.Num();
        }
        TestEqual(What + FString::Printf(TEXT(": %d rounds, every drop as the table allows"), Rounds), Wrong, 0);
        return Stacks;
    };
    const int32 GravelStacks = Within(
        TEXT("gravel"), NAME_None, {{TEXT("gravel"), FIntPoint(1, 1)}, {TEXT("flint"), FIntPoint(1, 1)}}, 1, 1, 40);
    TestEqual(TEXT("gravel by hand: exactly one stack every round (gravel or flint)"), GravelStacks, 40);
    Within(TEXT("gravel"), TEXT("iron_shovel"), {{TEXT("gravel"), FIntPoint(1, 1)}, {TEXT("flint"), FIntPoint(1, 1)}},
        1, 1, 40);
    // oak_leaves by hand: oak_sapling, stick x1-2 or apple, possibly nothing, never the leaves.
    Within(TEXT("oak_leaves"), NAME_None,
        {{TEXT("oak_sapling"), FIntPoint(1, 1)}, {TEXT("stick"), FIntPoint(1, 2)}, {TEXT("apple"), FIntPoint(1, 1)}}, 0,
        3, 60);
    // short_grass by hand: nothing or wheat_seeds, never the grass (it needs shears).
    Within(TEXT("short_grass"), NAME_None, {{TEXT("wheat_seeds"), FIntPoint(1, 1)}}, 0, 1, 60);

    // tall_grass and large_fern (location_check): both halves on a support block at (-1, 0, 0); then one half is
    // mined by hand through the authority, aimed at. The half
    // that breaks sees the other through location_check, so it rolls wheat_seeds (1 in 8) and nothing else, and the
    // other half pops with no drops of its own.
    {
        const FIntVector Support(-1, 0, 0), LowerCell(-1, 0, 1), UpperCell(-1, 0, 2);
        // The lab's cells are 100 cm (its feet at (-150, 50, 0) stand in cell (-2, 0, 0)).
        constexpr double BlockCm = 100;
        const FVector LabAim = L.P().Facts.Aim;
        const int32 Seeds = Rules.FindItem(TEXT("wheat_seeds"));
        TestTrue(TEXT("wheat_seeds is an item"), Seeds != INDEX_NONE);
        auto IsHalf = [&](const FIntVector& Cell, FName Block, FName Value)
        {
            const FFoundationBlockState State = L.A.World.Get(Cell).State;
            return L.A.World.GetBlock(State).Id == Block &&
                L.A.World.GetRegistry().GetProperties(State).Contains(TPair<FName, FName>(TEXT("half"), Value));
        };
        // One place-and-break; returns the stacks dropped (0 or 1).
        auto PlaceAndBreak = [&](FName Block, bool bUpper)
        {
            ++Cases;
            const FString What = Block.ToString() + (bUpper ? TEXT(" upper half") : TEXT(" lower half")) +
                TEXT(" by hand (break ") + FString::FromInt(L.A.Breaks) + TEXT(")");
            const FIntVector Mined = bUpper ? UpperCell : LowerCell, Other = bUpper ? LowerCell : UpperCell;
            L.ClearAndHold(NAME_None);
            L.A.World.Set(Support, L.A.World.FindBlock(TEXT("grass_block"))->GetDefaultState());
            // Both halves written as placement writes them (the default state is the lower half; GetOtherHalf gives
            // the upper); placing double plants is the plant tests' subject.
            const FFoundationBlockState Lower = L.A.World.FindBlock(Block)->GetDefaultState();
            FIntVector UpperAt;
            FFoundationBlockState Upper;
            L.A.World.Set(LowerCell, Lower);
            if (FoundationPlantRules::GetOtherHalf(L.A.World, LowerCell, Lower, UpperAt, Upper) && UpperAt == UpperCell)
                L.A.World.Set(UpperCell, Upper);
            if (!TestTrue(What + TEXT(": both halves written"),
                    IsHalf(LowerCell, Block, TEXT("lower")) && IsHalf(UpperCell, Block, TEXT("upper"))))
                return 0;
            // Aim at the middle of the half to mine, then back at the lab's cell.
            FFoundationPlayerFacts Facts = L.P().Facts;
            Facts.Aim =
                (FVector((Mined.X + .5) * BlockCm, (Mined.Y + .5) * BlockCm, (Mined.Z + .5) * BlockCm) - Facts.EyeCm)
                    .GetSafeNormal();
            Facts.Step = ++L.StepIndex;
            L.A.SetFacts(L.Player, Facts);
            L.NextStep();
            const TMap<int32, int32> Before = LootTests_Counts(L.P().Inventory);
            const FFoundationResult R = LootTests_Mine(L, 8000);
            const TMap<int32, int32> Gains = LootTests_Gains(Before, LootTests_Counts(L.P().Inventory));
            Facts.Aim = LabAim;
            Facts.Step = ++L.StepIndex;
            L.A.SetFacts(L.Player, Facts);
            TestTrue(What + TEXT(": the half broke and the other half is gone"),
                R.bBroken && L.A.World.Get(Mined).State.IsAir() && L.A.World.Get(Other).State.IsAir());
            TestTrue(What + TEXT(": drops only wheat_seeds x1, in the inventory too"),
                R.Drops.Num() <= 1 && Gains.Num() == R.Drops.Num() &&
                    Algo::AllOf(R.Drops, [&](const FFoundationLootDrop& Drop)
                        { return Drop.Item == Seeds && Drop.Count == 1 && Gains.FindRef(Seeds) == 1; }));
            return R.Drops.Num();
        };
        // 64 rounds, the lower and the upper half in turn: seeing no wheat_seeds at 1 in 8 has chance (7/8)^64, about
        // 2e-4 (the rolls are fixed by the cell and the break count, so the outcome is the same every run).
        for (const FName Block : {FName(TEXT("tall_grass")), FName(TEXT("large_fern"))})
        {
            int32 SeedStacks = 0;
            for (int32 Round = 0; Round < 64; ++Round)
                SeedStacks += PlaceAndBreak(Block, Round % 2 == 1);
            AddInfo(
                FString::Printf(TEXT("%s: wheat_seeds in %d of 64 place-and-breaks"), *Block.ToString(), SeedStacks));
            TestTrue(
                Block.ToString() + TEXT(": wheat_seeds dropped at least once in 64 breaks (location_check passes)"),
                SeedStacks >= 1);
        }
    }
    AddInfo(FString::Printf(TEXT("%d block cases through the authority"), Cases));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationLootSwitchTest, "Unnamed.Foundation.Loot.Switch", LootTestFlags)

bool FFoundationLootSwitchTest::RunTest(const FString&)
{
    TArray<FString> Tools;
    TArray<FLootBreakRow> Rows;
    const UFoundationItemAssets* Assets = UFoundationItemAssets::LoadDefault();
    IConsoleVariable* AllBlocks = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Loot.AllBlocks"));
    if (!TestTrue(TEXT("break_times.json, DA_Items and foundation.Loot.AllBlocks"),
            LootTests_ReadRows(Tools, Rows) && Assets && AllBlocks && Assets->LootTables.Num() == 602))
        return false;
    // Restores the variable however this test ends.
    struct FRestore
    {
        IConsoleVariable* Variable;
        int32 Previous;
        ~FRestore() { Variable->Set(Previous, ECVF_SetByCode); }
    } Restore{AllBlocks, AllBlocks->GetInt()};
    AllBlocks->Set(0, ECVF_SetByCode);
    if (!TestEqual(TEXT("foundation.Loot.AllBlocks set to 0"), AllBlocks->GetInt(), 0)) return false;

    FLootLab L(false);
    const FFoundationRules& Rules = L.A.Rules;
    int32 Entries = 0, Rolled = 0, Itself = 0, Unbreakable = 0, Wrong = 0, Blocks = 0;
    // One error per failing block (the first), so a block that fails everywhere reads as one line.
    FName CurrentBlock;
    TSet<FName> Reported, NotHitBlocks;
    int32 NotHit = 0;
    auto Fail = [this, &Wrong, &CurrentBlock, &Reported](const FString& What)
    {
        ++Wrong;
        if (Reported.Num() < 60 && !Reported.Contains(CurrentBlock))
        {
            Reported.Add(CurrentBlock);
            AddError(What);
        }
    };
    for (int32 T = 0; T < Assets->LootTables.Num(); ++T)
    {
        const FName Block = Assets->LootTables[T].Block;
        CurrentBlock = Block;
        const FLootBreakRow* Row =
            Rows.FindByPredicate([&Block](const FLootBreakRow& Each) { return Each.Id == Block.ToString(); });
        const FFoundationBlockType* Type = L.A.World.FindBlock(Block);
        if (!Row || !Type || Row->Drops.Num() != Tools.Num() || Row->Ticks.Num() != Tools.Num()) continue;
        ++Blocks;
        const FFoundationLootTable* Table = Rules.LootTables.Find(Block);
        const bool bLegacy = Table && Table->Index < FoundationLoot::LegacyTables;
        const int32 DropItem = Rules.FindItem(Type->DropItem);
        // The default state, and the double state of a block that has one.
        const FFoundationBlockState Default = Type->GetDefaultState();
        const FFoundationBlockState Double = L.A.World.GetRegistry().WithValue(Default, TEXT("type"), TEXT("double"));
        for (int32 Variant = 0; Variant < (Double.Id != Default.Id ? 2 : 1); ++Variant)
        {
            const bool bDouble = Variant == 1;
            for (int32 Column = 0; Column < Tools.Num(); ++Column)
            {
                const FString What =
                    Block.ToString() + (bDouble ? TEXT(" (double)") : TEXT("")) + TEXT(" with ") + Tools[Column];
                L.ClearAndHold(Column == 0 ? NAME_None : FName(*Tools[Column]));
                L.Put(Block, bDouble ? FName(TEXT("type")) : NAME_None, bDouble ? FName(TEXT("double")) : NAME_None);
                L.NextStep();
                ++Entries;
                if (Row->Ticks[Column] < 0)
                {
                    ++Unbreakable;
                    const FFoundationResult R = LootTests_Mine(L, 6);
                    if (R.bBroken || !L.Solid()) Fail(What + TEXT(": breaks, but break_times.json says never"));
                    continue;
                }
                const FFoundationResult R = LootTests_Mine(L, Row->Ticks[Column] + 20);
                if (!R.bBroken || L.Solid())
                {
                    // Small or waterlogged plants the lab's aim line does not hit (or that cannot stand there).
                    ++NotHit;
                    NotHitBlocks.Add(Block);
                    continue;
                }
                if (!Row->Drops[Column])
                {
                    if (!R.Drops.IsEmpty())
                        Fail(What + FString::Printf(TEXT(": %d drops; not harvestable"), R.Drops.Num()));
                    continue;
                }
                if (bLegacy)
                {
                    ++Rolled;
                    const FFoundationLootCandidate& Candidate = Table->Pools[0].Candidates[1];
                    const FIntPoint Range = LootTests_Range(Candidate);
                    if (R.Drops.Num() != 1 || R.Drops[0].Item != Candidate.Item || R.Drops[0].Count < Range.X ||
                        R.Drops[0].Count > Range.Y)
                        Fail(What +
                            FString::Printf(TEXT(": %d drops; want the ore roll (%s x%d-%d)"), R.Drops.Num(),
                                *Rules.GetItem(Candidate.Item).Id.ToString(), Range.X, Range.Y));
                    continue;
                }
                ++Itself;
                const int32 Count = Type->DropCount * (bDouble ? 2 : 1);
                const bool bWant = DropItem != INDEX_NONE && Count > 0;
                if (bWant ? (R.Drops.Num() != 1 || R.Drops[0].Item != DropItem || R.Drops[0].Count != Count)
                          : !R.Drops.IsEmpty())
                    Fail(What +
                        FString::Printf(TEXT(": %d drops (first %s x%d); want %s x%d"), R.Drops.Num(),
                            R.Drops.IsEmpty() ? TEXT("none") : *Rules.GetItem(R.Drops[0].Item).Id.ToString(),
                            R.Drops.IsEmpty() ? 0 : R.Drops[0].Count, *Type->DropItem.ToString(), bWant ? Count : 0));
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d blocks, %d entries: %d ore rolls, %d drops-itself, %d unbreakable"), Blocks,
        Entries, Rolled, Itself, Unbreakable));
    AddInfo(FString::Printf(
        TEXT("%d entries not hit by the lab's aim line (%d blocks, left unchecked)"), NotHit, NotHitBlocks.Num()));
    FString NotHitNames;
    for (const FName Name : NotHitBlocks)
        NotHitNames += Name.ToString() + TEXT(" ");
    AddInfo(NotHitNames);
    TestEqual(TEXT("every entry follows the S5b rule"), Wrong, 0);
    // Pinned from the first run (loot step 2a), re-derived for loot step 2b: nether_gold_ore is attached now, so the
    // ore rolls are all of .Drops' 82 eligible entries (75 + nether gold ore's 7 pickaxe entries), and those 7 leave
    // the drops-itself count: 13,615 - 7 = 13,608. The entries and the 828 not-hit entries do not change.
    TestEqual(TEXT("blocks"), Blocks, 602);
    TestEqual(TEXT("entries"), Entries, 25308);
    TestEqual(TEXT("ore rolls"), Rolled, 82);
    TestEqual(TEXT("drops-itself entries"), Itself, 13608);
    TestEqual(TEXT("entries the lab's aim cannot hit"), NotHit, 828);
    TestEqual(TEXT("blocks the lab's aim cannot hit"), NotHitBlocks.Num(), 23);

    // At 1 stone gives cobblestone again, at 0 it gives itself (no pickaxe table).
    L.ClearAndHold(TEXT("iron_pickaxe"));
    L.Put(TEXT("stone"));
    L.NextStep();
    FFoundationResult R = LootTests_Mine(L, 1000);
    const FName StoneDrop = L.A.World.FindBlock(TEXT("stone"))->DropItem;
    TestTrue(TEXT("at 0, stone with a pickaxe drops its own DropItem ") + StoneDrop.ToString(),
        R.bBroken && R.Drops.Num() == 1 && Rules.GetItem(R.Drops[0].Item).Id == StoneDrop);
    AllBlocks->Set(1, ECVF_SetByCode);
    L.ClearAndHold(TEXT("iron_pickaxe"));
    L.Put(TEXT("stone"));
    L.NextStep();
    R = LootTests_Mine(L, 1000);
    TestTrue(TEXT("at 1, stone with a pickaxe drops cobblestone"),
        R.bBroken && R.Drops.Num() == 1 && Rules.GetItem(R.Drops[0].Item).Id == TEXT("cobblestone"));
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
