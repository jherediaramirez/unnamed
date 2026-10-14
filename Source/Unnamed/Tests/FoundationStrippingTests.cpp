#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationStripping.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags StripTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Step 14's lab: the project's rules (DA_Items) and blocks, a finite inventory with tool wear, an iron axe in slot
    // 0, an apple in slot 1 and a stick in slot 2, and the player's eye looking at the cell it aims at.
    struct FStripLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        FFoundationInteraction I;
        int32 Player = 0;
        int64 StepIndex = 0;
        FVector Eye = FVector(50, 50, 150);
        FIntVector Aimed = FIntVector(2, 0, 1);

        explicit FStripLab(bool bItemUse = true)
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bInfiniteSupply = false;
            A.Options.bToolWear = true;
            A.Options.bItemUse = bItemUse;
            Player = A.AddPlayer();
            A.Reset();
            Vitals.bNaturalRegeneration = false;
            Vitals.Food = 10;
            I.PlayerId = Player;
            I.SetPreset(*UFoundationInteractionSettings::Get().FindPreset(TEXT("VanillaReference")), .05);
            I.Resync(A);
            NextStep();
            Exec(EFoundationCommandType::SetCreativeSlot, EFoundationHand::Main, 0, TEXT("iron_axe"), 1);
            Exec(EFoundationCommandType::SetCreativeSlot, EFoundationHand::Main, 1, TEXT("apple"), 5);
            Exec(EFoundationCommandType::SetCreativeSlot, EFoundationHand::Main, 2, TEXT("stick"), 1);
            Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 0);
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }
        const FFoundationBlockRegistry& Registry() { return A.World.GetRegistry(); }

        // The next reference step, aiming at the centre of Aimed.
        void NextStep()
        {
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = Eye - FVector(0, 0, 162);
            Facts.EyeCm = Eye;
            Facts.Aim = (FoundationGrid::CellBox(Aimed, A.World.OriginCm).GetCenter() - Eye).GetSafeNormal();
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
            A.Step();
        }

        // Puts a block (Id, with Axis when given) at Aimed, then steps so the facts see it.
        FFoundationBlockState Put(FName Id, FName Axis = NAME_None)
        {
            FFoundationBlockState State = A.World.FindBlock(Id)->GetDefaultState();
            if (!Axis.IsNone()) State = Registry().WithValue(State, TEXT("axis"), Axis);
            A.World.Set(Aimed, State);
            NextStep();
            return State;
        }

        FFoundationResult Exec(EFoundationCommandType Type, EFoundationHand Hand = EFoundationHand::Main,
            int32 Slot = 0, FName Item = NAME_None, int32 ItemCount = 0, int32 Damage = 0)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            // The client's own counter, so direct commands and the client's never share a sequence.
            Command.Sequence = I.NextSequence++;
            Command.Type = Type;
            Command.Hand = Hand;
            Command.Item = P().Inventory.GetHand(Hand).GetRef();
            Command.Slot = Slot;
            Command.CreativeItem = Item;
            Command.CreativeCount = ItemCount;
            Command.CreativeDamage = Damage;
            if (Type == EFoundationCommandType::UseOn) Command.Target = A.Resolve(P().Facts);
            return A.Execute(Player, Command, Vitals);
        }

        FFoundationResult Use(EFoundationHand Hand = EFoundationHand::Main)
        {
            return Exec(EFoundationCommandType::UseOn, Hand);
        }

        FName BlockAt() { return Registry().Save(A.World.Get(Aimed).State).Id; }
        int32 Damage(int32 Slot = 0) { return P().Inventory.Hotbar[Slot].Damage; }
        int32 CountEvents(EFoundationEventKind Kind)
        {
            int32 Count = 0;
            for (const FFoundationEvent& Event : A.GetEvents())
                Count += Event.Kind == Kind;
            return Count;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationItemsStripReferenceTest, "Unnamed.Foundation.Items.StripReference", StripTestFlags)

bool FFoundationItemsStripReferenceTest::RunTest(const FString&)
{
    // The strip table's targets are the block reference's stripped_* blocks, exactly; each pair is "x" and
    // "stripped_x"; both sides are registry blocks with the same properties (the axis is kept).
    FString Text;
    TSharedPtr<FJsonObject> Manifest;
    if (FFileHelper::LoadFileToString(
            Text, *(FPaths::ProjectDir() / TEXT("Reference/minecraft-blocks-26.3/block_manifest.json"))))
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest);
    if (!TestTrue(TEXT("block_manifest.json"), Manifest.IsValid())) return false;
    TSet<FName> Stripped;
    for (const TSharedPtr<FJsonValue>& Value : Manifest->GetArrayField(TEXT("blocks")))
    {
        const FString Id = Value->AsObject()->GetStringField(TEXT("id"));
        if (Id.StartsWith(TEXT("stripped_"))) Stripped.Add(FName(*Id));
    }
    TestEqual(TEXT("25 stripped blocks in the reference"), Stripped.Num(), 25);
    const TConstArrayView<TPair<FName, FName>> Table = FoundationStripping::GetTable();
    TestEqual(TEXT("one pair per stripped block"), Table.Num(), Stripped.Num());
    const TSharedRef<const FFoundationBlockRegistry> Shared = FFoundationBlockRegistry::GetDefault();
    const FFoundationBlockRegistry& Registry = *Shared;
    TSet<FName> Seen;
    for (const TPair<FName, FName>& Pair : Table)
    {
        const FString Key = Pair.Key.ToString();
        TestTrue(FString::Printf(TEXT("%s strips to a reference stripped block"), *Key), Stripped.Contains(Pair.Value));
        TestFalse(FString::Printf(TEXT("%s once"), *Key), Seen.Contains(Pair.Value));
        Seen.Add(Pair.Value);
        TestEqual(FString::Printf(TEXT("%s's pair name"), *Key), Pair.Value.ToString(), TEXT("stripped_") + Key);
        TestEqual(FString::Printf(TEXT("%s found"), *Key), FoundationStripping::FindStripped(Pair.Key), Pair.Value);
        const FFoundationBlockType* From = Registry.FindBlock(Pair.Key);
        const FFoundationBlockType* To = Registry.FindBlock(Pair.Value);
        if (!TestTrue(FString::Printf(TEXT("%s and its stripped block are imported"), *Key), From && To)) continue;
        const FFoundationSavedState Saved = Registry.Save(From->GetDefaultState());
        FFoundationSavedState Swapped = Saved;
        Swapped.Id = Pair.Value;
        FFoundationBlockState Loaded;
        TArray<FString> Warnings;
        TestTrue(FString::Printf(TEXT("%s's properties load on its stripped block"), *Key),
            Registry.Load(Swapped, Loaded, &Warnings) && Warnings.IsEmpty() &&
                Registry.Save(Loaded).Properties == Saved.Properties);
    }
    TestEqual(TEXT("nothing else strips"), FoundationStripping::FindStripped(TEXT("stone")), FName());
    TestEqual(TEXT("a stripped log does not strip again"), FoundationStripping::FindStripped(TEXT("stripped_oak_log")),
        FName());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsStripTest, "Unnamed.Foundation.Items.Strip", StripTestFlags)

bool FFoundationItemsStripTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("DA_Items"), UFoundationItemAssets::LoadDefault())) return false;
    {
        // Every axe carries #minecraft:axes; nothing else in slot reach does.
        FStripLab Lab;
        const FGameplayTag Axes = FoundationStripping::GetAxesTag();
        TestTrue(TEXT("Item.Axes is registered"), Axes.IsValid());
        int32 AxeCount = 0;
        for (const FFoundationItemDefinition& Item : Lab.A.Rules.Items)
            AxeCount += Item.Tags.HasTagExact(Axes);
        TestEqual(TEXT("the 7 axes carry Item.Axes"), AxeCount, 7);
    }
    {
        // An axe strips an oak log on each axis, keeps the axis, wears 1, and sends Stripped.
        FStripLab Lab;
        int32 Expected = 0;
        for (const TCHAR* Axis : {TEXT("x"), TEXT("y"), TEXT("z")})
        {
            Lab.Put(TEXT("oak_log"), Axis);
            const FFoundationResult Result = Lab.Use();
            TestTrue(FString::Printf(TEXT("axis %s strips"), Axis),
                Result.IsCommitted() && Result.bStripped && Result.bToolWorn && !Result.bToolBroke &&
                    Result.Cell == Lab.Aimed);
            TestEqual(FString::Printf(TEXT("axis %s: stripped_oak_log"), Axis), Lab.BlockAt(),
                FName(TEXT("stripped_oak_log")));
            TestEqual(FString::Printf(TEXT("axis %s kept"), Axis),
                Lab.Registry().GetValue(Lab.A.World.Get(Lab.Aimed).State, TEXT("axis")), FName(Axis));
            TestEqual(
                FString::Printf(TEXT("axis %s: result state"), Axis), Result.State, Lab.A.World.Get(Lab.Aimed).State);
            TestEqual(FString::Printf(TEXT("axis %s: one more wear"), Axis), Lab.Damage(), ++Expected);
            Lab.NextStep();
        }
        TestEqual(TEXT("three Stripped events"), Lab.CountEvents(EFoundationEventKind::Stripped), 3);
        TestEqual(TEXT("three strips"), Lab.A.Strips, 3);
        const FFoundationEvent& Last = Lab.A.GetEvents().Last();
        TestTrue(TEXT("the event names the axe and the stripped state"),
            Last.Kind == EFoundationEventKind::Stripped && Last.Cell == Lab.Aimed &&
                Lab.A.Rules.Items[Last.Item].Id == TEXT("iron_axe") && Last.State == Lab.A.World.Get(Lab.Aimed).State);
        // A stripped log, a stone block and a stick pass and change nothing.
        TestEqual(TEXT("a stripped log passes"), Lab.Use().Outcome, EFoundationOutcome::Pass);
        TestEqual(TEXT("no wear on a pass"), Lab.Damage(), 3);
        Lab.Put(TEXT("stone"));
        TestEqual(TEXT("stone passes"), Lab.Use().Outcome, EFoundationOutcome::Pass);
        TestEqual(TEXT("stone stays"), Lab.BlockAt(), FName(TEXT("stone")));
        Lab.Put(TEXT("crimson_hyphae"), TEXT("y"));
        Lab.Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 2);
        TestEqual(TEXT("a stick passes"), Lab.Use().Outcome, EFoundationOutcome::Pass);
        TestEqual(TEXT("hyphae stays with a stick"), Lab.BlockAt(), FName(TEXT("crimson_hyphae")));
        Lab.Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 0);
        TestTrue(TEXT("hyphae strips"), Lab.Use().bStripped);
        TestEqual(TEXT("stripped_crimson_hyphae"), Lab.BlockAt(), FName(TEXT("stripped_crimson_hyphae")));
        Lab.Put(TEXT("bamboo_block"), TEXT("x"));
        TestTrue(TEXT("bamboo block strips"), Lab.Use().bStripped);
        TestEqual(TEXT("stripped_bamboo_block"), Lab.BlockAt(), FName(TEXT("stripped_bamboo_block")));
    }
    {
        // Adventure: a plain axe passes and the log stays (survival.r1.modes).
        FStripLab Lab;
        Lab.Vitals.Mode = EFoundationPlayerMode::Adventure;
        Lab.Put(TEXT("oak_log"));
        const FFoundationResult Result = Lab.Use();
        TestTrue(TEXT("adventure passes"), Result.Outcome == EFoundationOutcome::Pass && !Result.bStripped);
        TestEqual(TEXT("adventure: the log stays"), Lab.BlockAt(), FName(TEXT("oak_log")));
        TestEqual(TEXT("adventure: no wear"), Lab.Damage(), 0);
        TestEqual(TEXT("adventure: no strips"), Lab.A.Strips, 0);
    }
    {
        // No wear in creative or with tool wear off; the last point breaks the axe once.
        FStripLab Lab;
        Lab.Vitals.Mode = EFoundationPlayerMode::Creative;
        Lab.Put(TEXT("oak_log"));
        TestTrue(TEXT("creative strips"), Lab.Use().bStripped);
        TestEqual(TEXT("creative: no wear"), Lab.Damage(), 0);
        Lab.Vitals.Mode = EFoundationPlayerMode::Survival;
        Lab.A.Options.bToolWear = false;
        Lab.Put(TEXT("oak_log"));
        TestTrue(TEXT("wear off strips"), Lab.Use().bStripped);
        TestEqual(TEXT("wear off: no wear"), Lab.Damage(), 0);
        Lab.A.Options.bToolWear = true;
        const int32 Max = Lab.A.Rules.GetItem(Lab.P().Inventory.Hotbar[0].Item).MaxDurability;
        Lab.Exec(EFoundationCommandType::SetCreativeSlot, EFoundationHand::Main, 0, TEXT("iron_axe"), 1, Max - 1);
        Lab.Put(TEXT("oak_log"));
        const FFoundationResult Result = Lab.Use();
        TestTrue(TEXT("the last point strips and breaks"), Result.bStripped && Result.bToolBroke);
        TestTrue(TEXT("the axe is gone"), Lab.P().Inventory.Hotbar[0].IsEmpty());
        TestEqual(TEXT("one ToolBroke"), Lab.CountEvents(EFoundationEventKind::ToolBroke), 1);
    }
    {
        // Switch 0 (foundation.Items.Use): an axe passes and the log stays.
        FStripLab Lab(false);
        Lab.Put(TEXT("oak_log"));
        TestEqual(TEXT("switch 0 passes"), Lab.Use().Outcome, EFoundationOutcome::Pass);
        TestEqual(TEXT("switch 0: the log stays"), Lab.BlockAt(), FName(TEXT("oak_log")));
        TestEqual(TEXT("switch 0: no wear"), Lab.Damage(), 0);
    }
    {
        // The client: a press with the axe in the main hand and an apple in the offhand strips and eats nothing; with a
        // stick in the main hand and the axe in the offhand, the offhand strips after the main hand's pass.
        FStripLab Lab;
        Lab.Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 1);
        Lab.Exec(EFoundationCommandType::SwapHands);
        Lab.Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 0);
        Lab.Put(TEXT("oak_log"), TEXT("y"));
        Lab.I.Press(EFoundationInput::Use, 0);
        Lab.NextStep();
        Lab.I.BeginUpdate();
        Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        TestEqual(TEXT("the client strips"), Lab.BlockAt(), FName(TEXT("stripped_oak_log")));
        // Checked before the release, which ends a client use by itself.
        TestFalse(TEXT("the strip starts no offhand meal"), Lab.P().Use.bActive || Lab.I.bUsingItem);
        Lab.I.Release(EFoundationInput::Use);
        Lab.Exec(EFoundationCommandType::SwapHands);
        Lab.Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 2);
        Lab.Put(TEXT("birch_wood"), TEXT("z"));
        Lab.NextStep();
        Lab.I.BeginUpdate();
        Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        Lab.I.Press(EFoundationInput::Use, 0);
        for (int32 Step = 0; Step < 2; ++Step)
        {
            Lab.NextStep();
            Lab.I.BeginUpdate();
            Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        }
        Lab.I.Release(EFoundationInput::Use);
        TestEqual(TEXT("the offhand axe strips"), Lab.BlockAt(), FName(TEXT("stripped_birch_wood")));
        TestEqual(TEXT("the offhand axe wore"), Lab.P().Inventory.Offhand.Damage, 2);
    }
    return true;
}
#endif
