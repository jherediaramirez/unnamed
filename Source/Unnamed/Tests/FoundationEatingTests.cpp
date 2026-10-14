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
    TestEqual(TEXT("six foods in items.json"), Rows, 6);
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
#endif
