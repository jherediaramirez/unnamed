#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBreakEffects.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationPawn.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags EatingTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Step 10's lab: the project's rules (DA_Items), a finite inventory, foods written into the hotbar, the player
    // looking straight up at nothing, and hunger the test sets.
    struct FEatingLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        FFoundationInteraction I;
        int32 Player = 0;
        int64 StepIndex = 0;

        explicit FEatingLab(bool bItemUse = true)
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bInfiniteSupply = false;
            A.Options.bItemUse = bItemUse;
            Player = A.AddPlayer();
            A.Reset();
            Vitals.bNaturalRegeneration = false;
            I.PlayerId = Player;
            I.SetPreset(*UFoundationInteractionSettings::Get().FindPreset(TEXT("VanillaReference")), .05);
            I.Resync(A);
            NextStep();
            Exec(EFoundationCommandType::SetCreativeSlot, EFoundationHand::Main, 0, TEXT("apple"), 5);
            Exec(EFoundationCommandType::SetCreativeSlot, EFoundationHand::Main, 1, TEXT("cooked_beef"), 3);
            Exec(EFoundationCommandType::SetCreativeSlot, EFoundationHand::Main, 2, TEXT("stick"), 1);
            Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 0);
            Vitals.Food = 10;
            Vitals.Saturation = 0;
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }
        int32 Count(int32 Slot) { return P().Inventory.Hotbar[Slot].Count; }

        // The next reference step: facts (eye at 162 cm, aim straight up), then the authority's own step.
        void NextStep()
        {
            FFoundationPlayerFacts Facts;
            Facts.EyeCm = FVector(0, 0, 162);
            Facts.Aim = FVector::UpVector;
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
            A.Step();
        }

        FFoundationResult Exec(EFoundationCommandType Type, EFoundationHand Hand = EFoundationHand::Main,
            int32 Slot = 0, FName Item = NAME_None, int32 ItemCount = 0)
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
            return A.Execute(Player, Command, Vitals);
        }

        // Starts a use, then continues it Steps times, one per reference step; the last continue's result.
        FFoundationResult Eat(int32 Steps)
        {
            FFoundationResult Result = Exec(EFoundationCommandType::UseItemStart);
            for (int32 Index = 0; Index < Steps && Result.Outcome != EFoundationOutcome::Rejected; ++Index)
            {
                NextStep();
                Result = Exec(EFoundationCommandType::UseItemContinue);
            }
            return Result;
        }
    };

    TSharedPtr<FJsonObject> ReadItems()
    {
        FString Text;
        TSharedPtr<FJsonObject> Root;
        if (FFileHelper::LoadFileToString(
                Text, *(FPaths::ProjectDir() / TEXT("Reference/minecraft-items-26.3/items.json"))))
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
        return Root;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationItemsEatReferenceTest, "Unnamed.Foundation.Items.EatReference", EatingTestFlags)

bool FFoundationItemsEatReferenceTest::RunTest(const FString&)
{
    // The native food table is items.json's food and consumable rows, exactly: every row with food is in it, with its
    // nutrition, saturation and time (consume_seconds x 20; `consumable: {}` is 1.6 s), and nothing else.
    const TSharedPtr<FJsonObject> Reference = ReadItems();
    if (!TestTrue(TEXT("items.json"), Reference.IsValid())) return false;
    int32 Rows = 0;
    for (const TSharedPtr<FJsonValue>& Value : Reference->GetArrayField(TEXT("items")))
    {
        const TSharedPtr<FJsonObject> Item = Value->AsObject();
        const TSharedPtr<FJsonObject>* Food = nullptr;
        const TSharedPtr<FJsonObject>* Consumable = nullptr;
        const bool bFood = Item->TryGetObjectField(TEXT("food"), Food);
        const bool bConsumable = Item->TryGetObjectField(TEXT("consumable"), Consumable);
        const FString Id = Item->GetStringField(TEXT("id"));
        const FFoundationConsumable* Row = FoundationConsumables::FindReference(*Id);
        if (!bFood)
        {
            TestNull(Id + TEXT(" has no food row"), Row);
            continue;
        }
        ++Rows;
        if (!TestNotNull(Id + TEXT(" has a food row"), Row) || !TestTrue(Id + TEXT(" is consumable"), bConsumable))
            continue;
        double Seconds = 1.6;
        (*Consumable)->TryGetNumberField(TEXT("consume_seconds"), Seconds);
        TestEqual(Id + TEXT(" nutrition"), Row->Nutrition, int32((*Food)->GetNumberField(TEXT("nutrition"))));
        TestEqual(Id + TEXT(" saturation"), Row->Saturation, float((*Food)->GetNumberField(TEXT("saturation"))));
        TestEqual(Id + TEXT(" ticks"), Row->ConsumeTicks, FMath::RoundToInt32(Seconds * 20));
    }
    TestEqual(TEXT("19 foods in items.json (survival 20a added 11, T1 8a2 2)"), Rows, 19);
    TestEqual(TEXT("the table holds only them"), FoundationConsumables::GetReferenceTable().Num(), Rows);
    TestEqual(TEXT("the default time is 32 ticks"), FoundationConsumables::DefaultTicks, 32);

    // The project's definitions read it: apple 4 / 2.4, bread 5 / 6.0, cooked beef 8 / 12.8; a stick eats nothing.
    FFoundationRules Rules;
    UFoundationInteractionSettings::Get().BuildRules(Rules);
    const auto Check = [&](const TCHAR* Id, int32 Nutrition, float Saturation)
    {
        const int32 Index = Rules.FindItem(Id);
        const FFoundationConsumable Food = Rules.GetItem(Index).GetConsumable();
        TestTrue(FString(Id) + TEXT(" eats as the reference"),
            Index != INDEX_NONE && Food.Nutrition == Nutrition && FMath::IsNearlyEqual(Food.Saturation, Saturation) &&
                Food.ConsumeTicks == 32);
    };
    Check(TEXT("apple"), 4, 2.4f);
    Check(TEXT("bread"), 5, 6.f);
    Check(TEXT("cooked_beef"), 8, 12.8f);
    TestFalse(
        TEXT("a stick is not consumable"), Rules.GetItem(Rules.FindItem(TEXT("stick"))).GetConsumable().IsValid());
    TestFalse(TEXT("the bare hand is not consumable"), Rules.Hand.GetConsumable().IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsEatTest, "Unnamed.Foundation.Items.Eat", EatingTestFlags)

bool FFoundationItemsEatTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("DA_Items"), UFoundationItemAssets::LoadDefault())) return false;
    {
        // 32 ticks: nothing until the 32nd continue, then hunger 10 + 4, saturation 2.4, one apple fewer.
        FEatingLab Lab;
        const FFoundationResult Start = Lab.Exec(EFoundationCommandType::UseItemStart);
        TestTrue(TEXT("an apple starts"), Start.IsCommitted() && Start.UseDuration == 32 && Lab.P().Use.bActive);
        FFoundationResult Result;
        for (int32 Tick = 1; Tick <= 31; ++Tick)
        {
            Lab.NextStep();
            Result = Lab.Exec(EFoundationCommandType::UseItemContinue);
        }
        TestTrue(TEXT("31 continues do not finish"), Result.IsCommitted() && !Result.bAte && Result.UseTicks == 31);
        TestEqual(TEXT("still 5 apples at 31"), Lab.Count(0), 5);
        TestEqual(TEXT("still hunger 10 at 31"), Lab.Vitals.Food, 10);
        Lab.NextStep();
        Result = Lab.Exec(EFoundationCommandType::UseItemContinue);
        TestTrue(TEXT("the 32nd finishes"), Result.IsCommitted() && Result.bAte && Result.bItemConsumed);
        TestEqual(TEXT("hunger 14"), Lab.Vitals.Food, 14);
        TestEqual(TEXT("saturation 2.4"), Lab.Vitals.Saturation, 2.4f);
        TestEqual(TEXT("4 apples"), Lab.Count(0), 4);
        TestFalse(TEXT("the use ended"), Lab.P().Use.bActive);
        TestEqual(TEXT("one eaten"), Lab.A.ItemsEaten, 1);
        // The events: Eating on the effect steps (8, 12, ... 28: six), then Ate with the nutrition.
        int32 Eating = 0, Ate = 0;
        for (const FFoundationEvent& Event : Lab.A.GetEvents())
        {
            Eating += Event.Kind == EFoundationEventKind::Eating;
            if (Event.Kind == EFoundationEventKind::Ate) Ate += Event.Amount == 4 ? 1 : 100;
        }
        TestEqual(TEXT("six eating effect steps"), Eating, 6);
        TestEqual(TEXT("one Ate event of 4"), Ate, 1);
        // One continue per step: a second in the same step is refused and does not advance.
        Lab.Eat(3);
        TestEqual(TEXT("a repeat in one step is refused"), Lab.Exec(EFoundationCommandType::UseItemContinue).Reason,
            EFoundationReason::StepLimit);
        TestEqual(TEXT("still 3 ticks"), Lab.P().Use.Ticks, 3);
    }
    {
        // Full hunger refuses and starts nothing; any lower hunger eats.
        FEatingLab Lab;
        Lab.Vitals.Food = 20;
        const FFoundationResult Result = Lab.Exec(EFoundationCommandType::UseItemStart);
        TestTrue(TEXT("full hunger refuses"),
            Result.Outcome == EFoundationOutcome::Rejected && Result.Reason == EFoundationReason::NotHungry &&
                !Lab.P().Use.bActive);
        Lab.Vitals.Food = 19;
        TestTrue(TEXT("hunger 19 eats"), Lab.Exec(EFoundationCommandType::UseItemStart).IsCommitted());
    }
    {
        // Saturation is capped at the new hunger: cooked beef (8, 12.8) at hunger 2 gives 10 and 10, at 12 gives 20
        // and 12.8.
        FEatingLab Lab;
        Lab.Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 1);
        Lab.Vitals.Food = 2;
        TestTrue(TEXT("beef eaten"), Lab.Eat(32).bAte);
        TestEqual(TEXT("hunger 2 + 8"), Lab.Vitals.Food, 10);
        TestEqual(TEXT("saturation capped at 10"), Lab.Vitals.Saturation, 10.f);
        Lab.Vitals.Food = 12;
        Lab.Vitals.Saturation = 0;
        TestTrue(TEXT("beef eaten again"), Lab.Eat(32).bAte);
        TestEqual(TEXT("hunger 20"), Lab.Vitals.Food, 20);
        TestEqual(TEXT("saturation 12.8 under the cap"), Lab.Vitals.Saturation, 12.8f);
        TestEqual(TEXT("one beef left"), Lab.Count(1), 1);
    }
    {
        // Release cancels: nothing consumed, no food, nothing left to continue.
        FEatingLab Lab;
        Lab.Eat(20);
        const FFoundationResult Release = Lab.Exec(EFoundationCommandType::UseItemRelease);
        TestTrue(TEXT("release cancels"),
            Release.Outcome == EFoundationOutcome::Canceled && Release.Reason == EFoundationReason::Released &&
                Release.UseTicks == 20);
        TestEqual(TEXT("5 apples after a release"), Lab.Count(0), 5);
        TestEqual(TEXT("hunger 10 after a release"), Lab.Vitals.Food, 10);
        Lab.NextStep();
        TestEqual(TEXT("nothing to continue"), Lab.Exec(EFoundationCommandType::UseItemContinue).Reason,
            EFoundationReason::NotUsing);
        // A slot change cancels, and so does a step without a continue.
        Lab.Eat(5);
        Lab.Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 2);
        TestFalse(TEXT("a slot change cancels"), Lab.P().Use.bActive);
        Lab.Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 0);
        Lab.Eat(5);
        Lab.NextStep();
        Lab.NextStep();
        TestFalse(TEXT("a missed step cancels"), Lab.P().Use.bActive);
        TestEqual(TEXT("still 5 apples"), Lab.Count(0), 5);
        // A stick passes.
        Lab.Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 2);
        TestEqual(
            TEXT("a stick passes"), Lab.Exec(EFoundationCommandType::UseItemStart).Outcome, EFoundationOutcome::Pass);
    }
    {
        // Switch 0 (foundation.Items.Use): the authority passes food, and the client holding use at the sky sends
        // nothing, as before.
        FEatingLab Lab(false);
        TestEqual(
            TEXT("switch 0 passes"), Lab.Exec(EFoundationCommandType::UseItemStart).Outcome, EFoundationOutcome::Pass);
        const int32 Commands = Lab.I.Commands;
        Lab.I.Press(EFoundationInput::Use, 0);
        for (int32 Step = 0; Step < 40; ++Step)
        {
            Lab.NextStep();
            Lab.I.BeginUpdate();
            Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        }
        TestEqual(TEXT("switch 0 sends no command"), Lab.I.Commands, Commands);
        TestEqual(TEXT("switch 0 eats nothing"), Lab.Count(0), 5);
        TestEqual(TEXT("switch 0 hunger"), Lab.Vitals.Food, 10);
    }
    {
        // The client: holding use at the sky eats in 32 steps after the start, then again after the held repeat;
        // letting go mid-way cancels.
        FEatingLab Lab;
        Lab.I.Press(EFoundationInput::Use, 0);
        int32 Steps = 0;
        while (Lab.Count(0) == 5 && Steps < 60)
        {
            Lab.NextStep();
            Lab.I.BeginUpdate();
            Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
            ++Steps;
        }
        TestEqual(TEXT("the client eats in 33 steps (start, then 32)"), Steps, 33);
        TestEqual(TEXT("client hunger 14"), Lab.Vitals.Food, 14);
        TestFalse(TEXT("the client's use ended"), Lab.I.bUsingItem);
        for (int32 Step = 0; Step < 12; ++Step)
        {
            Lab.NextStep();
            Lab.I.BeginUpdate();
            Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        }
        TestTrue(TEXT("still holding: a second apple is under way"), Lab.I.bUsingItem && Lab.P().Use.bActive);
        Lab.I.Release(EFoundationInput::Use);
        Lab.NextStep();
        Lab.I.BeginUpdate();
        Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        TestTrue(TEXT("letting go cancels"), !Lab.I.bUsingItem && !Lab.P().Use.bActive);
        TestEqual(TEXT("4 apples after the canceled second"), Lab.Count(0), 4);
        // A slot change while eating ends it quietly: no refused continue.
        Lab.I.Press(EFoundationInput::Use, 0);
        for (int32 Step = 0; Step < 5; ++Step)
        {
            Lab.NextStep();
            Lab.I.BeginUpdate();
            Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        }
        TestTrue(TEXT("eating again"), Lab.I.bUsingItem);
        const int32 Rejections = Lab.A.Rejections;
        Lab.I.SelectSlot(Lab.A, Lab.Vitals, 2);
        Lab.NextStep();
        Lab.I.BeginUpdate();
        Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        TestTrue(TEXT("a slot change ends the client's meal"), !Lab.I.bUsingItem && !Lab.P().Use.bActive);
        TestEqual(TEXT("with no refused command"), Lab.A.Rejections, Rejections);
    }
    return true;
}

namespace
{
    // The value report's eat transform samples (value-holder reports/reference/data/client.json, eating.hand_transform,
    // arm RIGHT: native applyEatTransform, a 32-tick use, partial tick 0): remaining ticks, column-major 4 x 4.
    struct FEatSample
    {
        int32 Remaining;
        double Matrix[16];
    };
    const FEatSample EatSamples[] = {
        {32,
            {-0.4740932, -0.6114166, 0.63356566, 0.0, -0.124524966, 0.75890106, 0.6391891, 0.0, -0.87162447, 0.22414045,
                -0.43592644, 0.0, -0.77714044, 0.647617, -0.0, 1.0}},
        {31, {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, -0.0, 0.0, 1.0}},
        {30,
            {0.6138119, 0.29537335, -0.732113, 0.0, -0.10827053, 0.9501021, 0.2925465, 0.0, 0.7819926, -0.10030228,
                0.6151643, 0.0, 0.34539464, -0.28782886, 0.0, 1.0}},
        {29,
            {0.30442134, 0.41429263, -0.85772324, 0.0, 0.011727035, 0.8987631, 0.4382776, 0.0, 0.95246524, -0.14347963,
                0.26874426, 0.0, 0.49495494, -0.41246244, 0.0, 1.0}},
        {28,
            {0.1722618, 0.46173638, -0.87012947, 0.0, 0.09053054, 0.87217486, 0.48074436, 0.0, 0.9808823, -0.1615872,
                0.10844119, 0.0, 0.5579421, -0.46495172, 0.0, 1.0}},
        {27,
            {0.11958145, 0.48061404, -0.86874074, 0.0, 0.1265787, 0.8604985, 0.49347764, 0.0, 0.9847224, -0.16897485,
                0.04206416, 0.0, 0.5836932, -0.486411, 0.0, 1.0}},
        {26,
            {0.09903419, 0.48800272, -0.8672056, 0.0, 0.14138357, 0.8557478, 0.497701, 0.0, 0.9849888, -0.17189805,
                0.01575272, 0.0, 0.5938917, -0.4949097, 0.0, 1.0}},
        {25,
            {0.091227286, 0.49081734, -0.86647326, 0.0, 0.14712127, 0.8539108, 0.49919102, 0.0, 0.9849025, -0.1730165,
                0.0056902925, 0.0, 0.5977951, -0.4981626, 0.0, 1.0}},
        {24,
            {0.08835831, 0.49185392, -0.86618274, 0.0, 0.14924608, 0.85323036, 0.49972346, 0.0, 0.9848444, -0.17342912,
                0.0019827273, 0.0, 0.59923536, -0.42865208, 0.0, 1.0}},
        {23,
            {0.08735246, 0.49222127, -0.8660761, 0.0, 0.14999478, 0.8529888, 0.49991173, 0.0, 0.98482037, -0.17357542,
                0.0006800521, 0.0, 0.59974605, -0.39978835, 0.0, 1.0}},
        {22,
            {0.08680199, 0.49234596, -0.86606055, 0.0, 0.15036987, 0.85290664, 0.49993914, 0.0, 0.9848118, -0.17362511,
                0.0, 0.0, 0.5999195, -0.42922902, 0.0, 1.0}},
        {21,
            {0.08681743, 0.4923864, -0.866036, 0.0, 0.15037955, 0.85288, 0.49998164, 0.0, 0.984809, -0.17364123, 0.0,
                0.0, 0.59997576, -0.4999798, 0.0, 1.0}},
        {20,
            {0.08682219, 0.49239892, -0.8660284, 0.0, 0.15038255, 0.8528718, 0.49999478, 0.0, 0.9848081, -0.1736462,
                0.0, 0.0, 0.5999931, -0.42928356, 0.0, 1.0}},
        {19,
            {0.08682358, 0.49240252, -0.8660262, 0.0, 0.15038343, 0.85286945, 0.49999857, 0.0, 0.98480785, -0.17364766,
                0.0, 0.0, 0.5999982, -0.39999846, 0.0, 1.0}},
        {18,
            {0.08682396, 0.49240354, -0.8660256, 0.0, 0.15038365, 0.8528688, 0.49999964, 0.0, 0.9848078, -0.17364804,
                0.0, 0.0, 0.59999955, -0.42928892, 0.0, 1.0}},
        {17,
            {0.08682406, 0.49240378, -0.86602545, 0.0, 0.15038371, 0.85286856, 0.4999999, 0.0, 0.9848077, -0.17364815,
                0.0, 0.0, 0.5999999, -0.4999999, 0.0, 1.0}},
        {16,
            {0.086824074, 0.49240384, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.49999997, 0.0, 0.9848077, -0.17364816,
                0.0, 0.0, 0.59999996, -0.42928928, 0.0, 1.0}},
        {15,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.4, 0.0, 1.0}},
        {14,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.4292893, 0.0, 1.0}},
        {13,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.5, 0.0, 1.0}},
        {12,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.4292893, 0.0, 1.0}},
        {11,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.4, 0.0, 1.0}},
        {10,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.4292893, 0.0, 1.0}},
        {9,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.5, 0.0, 1.0}},
        {8,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.4292893, 0.0, 1.0}},
        {7,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.4, 0.0, 1.0}},
        {6,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.4292893, 0.0, 1.0}},
        {5,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.5, 0.0, 1.0}},
        {4,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.4292893, 0.0, 1.0}},
        {3,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.4, 0.0, 1.0}},
        {2,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.4292893, 0.0, 1.0}},
        {1,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.5, 0.0, 1.0}},
        {0,
            {0.08682409, 0.49240386, -0.8660254, 0.0, 0.15038373, 0.8528685, 0.5, 0.0, 0.9848077, -0.17364818, -0.0,
                0.0, 0.6, -0.4292893, 0.0, 1.0}},
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsEatPolishTest, "Unnamed.Foundation.Items.EatPolish", EatingTestFlags)

bool FFoundationItemsEatPolishTest::RunTest(const FString&)
{
    {
        // The periodic effect: elapsed > floor(duration x 0.21875), remaining a multiple of 4, before the last step
        // (eating.crumbs). 32: 8, 12, ..., 28; 20 (floor 4, so not 4): 8, 12, 16; 16: 4, 8, 12.
        const auto Ticks = [](int32 Duration)
        {
            TArray<int32> Out;
            for (int32 Tick = 0; Tick <= Duration + 4; ++Tick)
                if (FoundationItemUse::IsEffectTick(Tick, Duration)) Out.Add(Tick);
            return Out;
        };
        TestEqual(TEXT("32: 8 to 28"), Ticks(32), TArray<int32>{8, 12, 16, 20, 24, 28});
        TestEqual(TEXT("20: 8, 12, 16"), Ticks(20), TArray<int32>{8, 12, 16});
        TestEqual(TEXT("16: 4, 8, 12"), Ticks(16), TArray<int32>{4, 8, 12});
    }
    {
        // The eat transform's closed form against all 33 main-hand samples.
        double Worst = 0;
        int32 WorstRemaining = -1;
        for (const FEatSample& Sample : EatSamples)
        {
            const FMatrix M = FoundationHand::MakeEatMatrix(Sample.Remaining, 32);
            for (int32 Column = 0; Column < 4; ++Column)
                for (int32 Row = 0; Row < 4; ++Row)
                {
                    const double Error = FMath::Abs(M.M[Row][Column] - Sample.Matrix[Column * 4 + Row]);
                    if (Error > Worst)
                    {
                        Worst = Error;
                        WorstRemaining = Sample.Remaining;
                    }
                }
        }
        TestTrue(FString::Printf(TEXT("33 samples within 5e-4 (worst %.2e at remaining %d)"), Worst, WorstRemaining),
            Worst < 5e-4);
        TestEqual(TEXT("33 samples"), int32(UE_ARRAY_COUNT(EatSamples)), 33);
        // In the scene: settled (remaining 9: the jiggle at 0), the fitted pivot lands below the view's centre, ahead
        // and slightly left.
        const FTransform Settled = FoundationHand::MakeEatPivot(FVector(71.9, 56.2, -52.2), 9, 32);
        TestTrue(FString::Printf(TEXT("settled pivot %s"), *Settled.GetLocation().ToString()),
            Settled.GetLocation().Equals(FVector(74.8, -13.8, -54.4), .5));
        TestTrue(TEXT("at the end of the approach the pivot is untouched"),
            FoundationHand::MakeEatPivot(FVector(71.9, 56.2, -52.2), 31, 32)
                .Equals(FTransform(FVector(71.9, 56.2, -52.2)), 1e-3));
    }
    {
        // Crumbs: velocity ((U - 0.5) x 0.1, U x 0.1 + 0.1, 0) blocks per step, view-turned, then +0.05 up; spawned 0.8
        // blocks ahead, 3 cm (provisional).
        FRandomStream Random(7);
        bool bLevelInRange = true, bTurned = true;
        for (int32 Index = 0; Index < 200; ++Index)
        {
            const FoundationBreak::FParticleSpawn Level =
                FoundationBreak::SpawnCrumb(FVector(0, 0, 162), FRotator::ZeroRotator, Random);
            // Looking along x: across is y (+-1 block/s), up 3 to 5 blocks/s, none forward.
            bLevelInRange &= FMath::IsNearlyZero(Level.Velocity.X, 1e-3) && FMath::Abs(Level.Velocity.Y) <= 100 &&
                Level.Velocity.Z >= 300 - 1e-3 && Level.Velocity.Z <= 500 + 1e-3 &&
                FMath::IsNearlyEqual(Level.Location.X, 80., 1e-3) && Level.Size == 3;
            // Looking straight down: the view's up turns to forward (x); only the +0.05 stays up.
            const FoundationBreak::FParticleSpawn Down =
                FoundationBreak::SpawnCrumb(FVector(0, 0, 162), FRotator(-90, 0, 0), Random);
            bTurned &= Down.Velocity.X >= 200 - 1e-3 && Down.Velocity.X <= 400 + 1e-3 &&
                FMath::IsNearlyEqual(Down.Velocity.Z, 100., 1e-3);
        }
        TestTrue(TEXT("level view: across +-1, up 3-5 blocks/s"), bLevelInRange);
        TestTrue(TEXT("down view: the view's up turned forward, 1 block/s up"), bTurned);
        TestEqual(TEXT("5 per effect, 16 at completion"),
            FoundationBreak::CrumbsPerEffect * 100 + FoundationBreak::CrumbsAtCompletion, 516);
    }
    {
        // Codex review 1, finding 1: foundation.Items.Use 0 mid-meal ends it; nothing is eaten.
        FEatingLab Lab;
        Lab.Eat(10);
        TestTrue(TEXT("eating"), Lab.P().Use.bActive);
        Lab.A.Options.bItemUse = false;
        Lab.NextStep();
        const FFoundationResult Off = Lab.Exec(EFoundationCommandType::UseItemContinue);
        TestTrue(TEXT("switch 0: the continue cancels"),
            Off.Outcome == EFoundationOutcome::Canceled && Off.Reason == EFoundationReason::ItemUseOff);
        TestFalse(TEXT("the use ended"), Lab.P().Use.bActive);
        // More continues than a whole meal: one that survived the switch would be eaten.
        for (int32 Step = 0; Step < 40; ++Step)
        {
            Lab.NextStep();
            Lab.Exec(EFoundationCommandType::UseItemContinue);
        }
        TestEqual(TEXT("hunger still 10"), Lab.Vitals.Food, 10);
        TestEqual(TEXT("still 5 apples"), Lab.Count(0), 5);
        TestEqual(TEXT("none eaten"), Lab.A.ItemsEaten, 0);
    }
    return true;
}

// Codex review 35, finding 2: a zombie hit that kills the player on the step its apple would finish ends the step
// before the interaction step (AFoundationPawn::TakeHits marks the facts dead), so the apple is not eaten and stays in
// the inventory the death drops [survival.r6.death_items]. Mutation: the facts left alive (the apple eaten, 4 left).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsEatKilledTest, "Unnamed.Foundation.Items.EatKilled", EatingTestFlags)

bool FFoundationItemsEatKilledTest::RunTest(const FString&)
{
    FEatingLab Lab;
    Lab.I.Press(EFoundationInput::Use, 0);
    // The client eats on its 33rd step (start, then 32; Unnamed.Foundation.Items.Eat): 32 steps leave it unfinished.
    for (int32 Step = 0; Step < 32; ++Step)
    {
        Lab.NextStep();
        Lab.I.BeginUpdate();
        Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
    }
    if (!TestEqual(TEXT("not eaten yet"), Lab.Count(0), 5)) return false;
    Lab.NextStep();
    Lab.I.BeginUpdate();
    Lab.Vitals.Health = 1;
    Lab.P().MobHits.Add({1, 3.f, TEXT("Zombie"), FVector(200, 0, 0)});
    FFoundationSimulation Simulation;
    AFoundationPawn::TakeHits(Lab.A, Lab.Player, Lab.Vitals, Simulation);
    TestTrue(TEXT("the hit kills"), Lab.Vitals.IsDead());
    TestFalse(TEXT("the facts say dead"), Lab.P().Facts.bAlive);
    Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
    TestEqual(TEXT("the apple is not eaten: 5 to drop"), Lab.Count(0), 5);
    TestEqual(TEXT("none eaten"), Lab.A.ItemsEaten, 0);
    return true;
}
#endif
