#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags BucketTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Step 17's lab: the project's rules (DA_Items) and blocks, a finite survival inventory without item entities, an
    // empty bucket in slot 0, and the player's eye (cell 0,0,1) looking along +X at the centre of Aimed (2,0,1), so a
    // pour against a block at Aimed lands in Beside (1,0,1).
    struct FBucketLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        FFoundationInteraction I;
        int32 Player = 0;
        int64 StepIndex = 0;
        FVector Eye = FVector(50, 50, 150);
        FIntVector Aimed = FIntVector(2, 0, 1);
        FIntVector Beside = FIntVector(1, 0, 1);
        // Crouching (the facts' secondary use).
        bool bSneak = false;

        explicit FBucketLab(bool bItemUse = true)
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bInfiniteSupply = false;
            A.Options.bItemUse = bItemUse;
            A.Options.bItemEntities = false;
            Player = A.AddPlayer();
            A.Reset();
            Vitals.bNaturalRegeneration = false;
            Vitals.Food = 10;
            I.PlayerId = Player;
            I.SetPreset(*UFoundationInteractionSettings::Get().FindPreset(TEXT("VanillaReference")), .05);
            I.Resync(A);
            NextStep();
            Give(0, TEXT("bucket"));
            Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 0);
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }
        const FFoundationBlockRegistry& Registry() { return A.World.GetRegistry(); }

        void NextStep()
        {
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = Eye - FVector(0, 0, 162);
            Facts.EyeCm = Eye;
            Facts.Aim = (FoundationGrid::CellBox(Aimed, A.World.OriginCm).GetCenter() - Eye).GetSafeNormal();
            Facts.Step = ++StepIndex;
            Facts.bSecondaryUse = bSneak;
            A.SetFacts(Player, Facts);
            A.Step();
        }

        // A block's default state (with one property changed when given) at Cell.
        FFoundationBlockState Put(const FIntVector& Cell, FName Id, FName Property = NAME_None, FName Value = NAME_None)
        {
            FFoundationBlockState State = A.World.FindBlock(Id)->GetDefaultState();
            if (!Property.IsNone()) State = Registry().WithValue(State, Property, Value);
            A.World.Set(Cell, State);
            return State;
        }
        // A fluid at a level (0 the source).
        FFoundationBlockState PutFluid(const FIntVector& Cell, FName Id, int32 Level)
        {
            const FFoundationBlockState State = {uint16(A.World.FindBlock(Id)->GetDefaultState().Id + Level)};
            A.World.Set(Cell, State);
            return State;
        }

        FFoundationResult Exec(EFoundationCommandType Type, EFoundationHand Hand = EFoundationHand::Main,
            int32 Slot = 0, FName Item = NAME_None, int32 ItemCount = 0)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = I.NextSequence++;
            Command.Type = Type;
            Command.Hand = Hand;
            Command.Item = P().Inventory.GetHand(Hand).GetRef();
            Command.Slot = Slot;
            Command.CreativeItem = Item;
            Command.CreativeCount = ItemCount;
            return A.Execute(Player, Command, Vitals);
        }
        void Give(int32 Slot, FName Item, int32 Count = 1)
        {
            Exec(EFoundationCommandType::SetCreativeSlot, EFoundationHand::Main, Slot, Item, Count);
        }

        // The bucket's use: the client's item-use command (UseOn passes for a bucket).
        FFoundationResult Use(EFoundationHand Hand = EFoundationHand::Main)
        {
            NextStep();
            return Exec(EFoundationCommandType::UseItemStart, Hand);
        }

        FName BlockAt(const FIntVector& Cell) { return Registry().Save(A.World.Get(Cell).State).Id; }
        FName Value(const FIntVector& Cell, FName Property)
        {
            return Registry().GetValue(A.World.Get(Cell).State, Property);
        }
        bool IsSource(const FIntVector& Cell, bool bLava)
        {
            const FFoundationFluid Fluid = Registry().GetFluid(A.World.Get(Cell).State);
            return Fluid.bSource && (bLava ? Fluid.bLava : Fluid.bWater) &&
                Registry().GetBlock(A.World.Get(Cell).State).IsFluid();
        }
        FName HandItem(EFoundationHand Hand = EFoundationHand::Main)
        {
            const FFoundationItemInstance& Stack = P().Inventory.GetHand(Hand);
            return Stack.IsEmpty() ? NAME_None : A.Rules.GetItem(Stack.Item).Id;
        }
        int32 HandCount() { return P().Inventory.GetHand(EFoundationHand::Main).Count; }
        // How many of an item the inventory holds, every slot.
        int32 CountItem(FName Id)
        {
            int32 Count = 0;
            for (int32 Slot = 0; Slot < FFoundationInventory::Slots; ++Slot)
                if (const FFoundationItemInstance* Stack = P().Inventory.GetSlot(Slot);
                    Stack && !Stack->IsEmpty() && A.Rules.GetItem(Stack->Item).Id == Id)
                    Count += Stack->Count;
            return Count;
        }
        int32 CountEvents(EFoundationEventKind Kind)
        {
            int32 Count = 0;
            for (const FFoundationEvent& Event : A.GetEvents())
                Count += Event.Kind == Kind;
            return Count;
        }
        const FFoundationEvent* LastEvent(EFoundationEventKind Kind)
        {
            const TArray<FFoundationEvent>& Events = A.GetEvents();
            for (int32 Index = Events.Num() - 1; Index >= 0; --Index)
                if (Events[Index].Kind == Kind) return &Events[Index];
            return nullptr;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsBucketTest, "Unnamed.Foundation.Items.Bucket", BucketTestFlags)

bool FFoundationItemsBucketTest::RunTest(const FString&)
{
    {
        // Survival: an empty bucket takes a water source and a lava source; the full bucket replaces it in the hand.
        FBucketLab Lab;
        TestTrue(TEXT("the reference items exist"),
            Lab.A.Rules.FindItem(TEXT("water_bucket")) != INDEX_NONE &&
                Lab.A.Rules.FindItem(TEXT("lava_bucket")) != INDEX_NONE);
        Lab.PutFluid(Lab.Aimed, TEXT("water"), 0);
        FFoundationResult Result = Lab.Use();
        TestTrue(TEXT("water fills"), Result.IsCommitted() && Result.bFilled && Result.Cell == Lab.Aimed);
        TestTrue(TEXT("the water source is gone"), Lab.A.World.Get(Lab.Aimed).State.IsAir());
        TestEqual(TEXT("a water bucket in hand"), Lab.HandItem(), FName(TEXT("water_bucket")));
        TestEqual(TEXT("no empty bucket left"), Lab.CountItem(TEXT("bucket")), 0);
        const FFoundationEvent* Filled = Lab.LastEvent(EFoundationEventKind::Filled);
        TestTrue(TEXT("Filled names the water bucket at the player's feet"),
            Filled && Filled->Cell == Lab.Aimed && Lab.A.Rules.Items[Filled->Item].Id == TEXT("water_bucket") &&
                Filled->LocationCm.Equals(Lab.P().Facts.FeetCm));
        // A full bucket aimed at nothing passes.
        TestEqual(TEXT("pouring at nothing passes"), Lab.Use().Outcome, EFoundationOutcome::Pass);
        Lab.Give(0, TEXT("bucket"));
        Lab.PutFluid(Lab.Aimed, TEXT("lava"), 0);
        Result = Lab.Use();
        TestTrue(TEXT("lava fills"), Result.bFilled && Lab.A.World.Get(Lab.Aimed).State.IsAir());
        TestEqual(TEXT("a lava bucket in hand"), Lab.HandItem(), FName(TEXT("lava_bucket")));
        TestEqual(TEXT("two Filled events"), Lab.CountEvents(EFoundationEventKind::Filled), 2);
        TestEqual(TEXT("two bucket uses"), Lab.A.BucketUses, 2);
        // Flowing water and stone are not sources: the empty bucket passes and nothing changes.
        Lab.Give(0, TEXT("bucket"));
        const FFoundationBlockState Flowing = Lab.PutFluid(Lab.Aimed, TEXT("water"), 3);
        TestEqual(TEXT("flowing water passes"), Lab.Use().Outcome, EFoundationOutcome::Pass);
        TestEqual(TEXT("flowing water stays"), Lab.A.World.Get(Lab.Aimed).State, Flowing);
        Lab.Put(Lab.Aimed, TEXT("stone"));
        TestEqual(TEXT("stone passes"), Lab.Use().Outcome, EFoundationOutcome::Pass);
        TestEqual(TEXT("still an empty bucket"), Lab.HandItem(), FName(TEXT("bucket")));
    }
    {
        // Pouring against a block puts the source beside it and leaves an empty bucket; lava the same.
        FBucketLab Lab;
        Lab.Give(0, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("stone"));
        FFoundationResult Result = Lab.Use();
        TestTrue(TEXT("water pours"), Result.IsCommitted() && Result.bEmptied && Result.Cell == Lab.Beside);
        TestTrue(TEXT("a water source beside the stone"), Lab.IsSource(Lab.Beside, false));
        TestEqual(TEXT("the stone stays"), Lab.BlockAt(Lab.Aimed), FName(TEXT("stone")));
        TestEqual(TEXT("an empty bucket in hand"), Lab.HandItem(), FName(TEXT("bucket")));
        const FFoundationEvent* Emptied = Lab.LastEvent(EFoundationEventKind::Emptied);
        TestTrue(TEXT("Emptied names the water bucket and the cell"),
            Emptied && Emptied->Cell == Lab.Beside && Lab.A.Rules.Items[Emptied->Item].Id == TEXT("water_bucket"));
        // The empty bucket takes it back: the source is nearer than the stone.
        TestTrue(TEXT("the poured source fills again"), Lab.Use().bFilled && Lab.A.World.Get(Lab.Beside).State.IsAir());
        Lab.Give(0, TEXT("lava_bucket"));
        Lab.PutFluid(Lab.Beside, TEXT("water"), 2);
        Result = Lab.Use();
        TestTrue(TEXT("lava pours over flowing water"), Result.bEmptied && Lab.IsSource(Lab.Beside, true));
        TestEqual(TEXT("lava: an empty bucket"), Lab.HandItem(), FName(TEXT("bucket")));
        // A replaceable plant is replaced in its own cell.
        Lab.Give(0, TEXT("water_bucket"));
        Lab.A.World.Set(Lab.Beside, {});
        Lab.Put(Lab.Aimed, TEXT("short_grass"));
        Lab.Put(Lab.Aimed - FIntVector(0, 0, 1), TEXT("grass_block"));
        Result = Lab.Use();
        TestTrue(TEXT("water replaces short grass"),
            Result.bEmptied && Result.Cell == Lab.Aimed && Lab.IsSource(Lab.Aimed, false));
    }
    {
        // A stack of empty buckets: one goes, the filled bucket goes into the inventory; with no room (and no item
        // entities) the fill is refused and nothing changes.
        FBucketLab Lab;
        Lab.A.Options.bFullInventory = false;
        Lab.Give(0, TEXT("bucket"), 16);
        Lab.PutFluid(Lab.Aimed, TEXT("water"), 0);
        TestTrue(TEXT("a stack fills"), Lab.Use().bFilled);
        TestEqual(TEXT("15 empty buckets in hand"), Lab.HandCount(), 15);
        TestEqual(TEXT("the water bucket added"), Lab.CountItem(TEXT("water_bucket")), 1);
        for (int32 Slot = 1; Slot < FFoundationInventory::HotbarSlots; ++Slot)
            Lab.Give(Slot, TEXT("stone"), 64);
        Lab.Give(FFoundationInventory::OffhandSlot, TEXT("stone"), 64);
        Lab.PutFluid(Lab.Aimed, TEXT("water"), 0);
        const FFoundationResult Result = Lab.Use();
        TestTrue(TEXT("a full inventory refuses"),
            Result.Outcome == EFoundationOutcome::Rejected && Result.Reason == EFoundationReason::InventoryFull);
        TestTrue(TEXT("refused: the source stays"), Lab.IsSource(Lab.Aimed, false));
        TestEqual(TEXT("refused: 15 buckets"), Lab.HandCount(), 15);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationItemsBucketModesTest, "Unnamed.Foundation.Items.BucketModes", BucketTestFlags)

bool FFoundationItemsBucketModesTest::RunTest(const FString&)
{
    {
        // Creative keeps the used bucket: a fill adds one filled bucket only when none is held; a pour keeps the full
        // bucket (minecraft.wiki "Bucket").
        FBucketLab Lab;
        Lab.Vitals.Mode = EFoundationPlayerMode::Creative;
        Lab.PutFluid(Lab.Aimed, TEXT("water"), 0);
        TestTrue(TEXT("creative fills"), Lab.Use().bFilled && Lab.A.World.Get(Lab.Aimed).State.IsAir());
        TestEqual(TEXT("creative: the empty bucket stays in hand"), Lab.HandItem(), FName(TEXT("bucket")));
        TestEqual(TEXT("creative: one water bucket added"), Lab.CountItem(TEXT("water_bucket")), 1);
        Lab.PutFluid(Lab.Aimed, TEXT("water"), 0);
        TestTrue(TEXT("creative fills again"), Lab.Use().bFilled);
        TestEqual(TEXT("creative: still one water bucket"), Lab.CountItem(TEXT("water_bucket")), 1);
        TestEqual(TEXT("creative: still one empty bucket"), Lab.CountItem(TEXT("bucket")), 1);
        Lab.Give(0, TEXT("lava_bucket"));
        Lab.Put(Lab.Aimed, TEXT("stone"));
        TestTrue(TEXT("creative pours"), Lab.Use().bEmptied && Lab.IsSource(Lab.Beside, true));
        TestEqual(TEXT("creative: the lava bucket stays"), Lab.HandItem(), FName(TEXT("lava_bucket")));
        TestEqual(TEXT("creative: no empty bucket from the pour"), Lab.CountItem(TEXT("bucket")), 0);
    }
    {
        // Adventure: the bucket passes and the source stays.
        FBucketLab Lab;
        Lab.Vitals.Mode = EFoundationPlayerMode::Adventure;
        Lab.PutFluid(Lab.Aimed, TEXT("water"), 0);
        TestEqual(TEXT("adventure passes"), Lab.Use().Outcome, EFoundationOutcome::Pass);
        TestTrue(TEXT("adventure: the source stays"), Lab.IsSource(Lab.Aimed, false));
        TestEqual(TEXT("adventure: no bucket uses"), Lab.A.BucketUses, 0);
    }
    {
        // Switch 0 (foundation.Items.Use): the bucket passes and the client sends nothing.
        FBucketLab Lab(false);
        Lab.PutFluid(Lab.Aimed, TEXT("water"), 0);
        TestEqual(TEXT("switch 0 passes"), Lab.Use().Outcome, EFoundationOutcome::Pass);
        TestTrue(TEXT("switch 0: the source stays"), Lab.IsSource(Lab.Aimed, false));
        Lab.I.Press(EFoundationInput::Use, 0);
        Lab.NextStep();
        Lab.I.BeginUpdate();
        Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        Lab.I.Release(EFoundationInput::Use);
        TestTrue(TEXT("switch 0: a press through the client leaves the source"), Lab.IsSource(Lab.Aimed, false));
    }
    for (const TCHAR* Preset : {TEXT("VanillaReference"), TEXT("LucidFluid")})
    {
        // The client: aiming at a water source with nothing solid behind it, one press fills (UseOn has no target, the
        // item use runs), holds no use, and holding waits the held repeat before the next use, under the attempt
        // cadence and the commit cadence alike.
        FBucketLab Lab;
        Lab.I.SetPreset(*UFoundationInteractionSettings::Get().FindPreset(Preset), .05);
        AddInfo(Preset);
        Lab.PutFluid(Lab.Aimed, TEXT("water"), 0);
        Lab.I.Press(EFoundationInput::Use, 0);
        Lab.NextStep();
        Lab.I.BeginUpdate();
        Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        TestEqual(TEXT("the client fills"), Lab.HandItem(), FName(TEXT("water_bucket")));
        TestFalse(TEXT("an instant use: nothing held"), Lab.P().Use.bActive || Lab.I.bUsingItem);
        // A stone behind the emptied cell: the held use pours against it only after the repeat.
        Lab.Put(Lab.Aimed + FIntVector(1, 0, 0), TEXT("stone"));
        Lab.NextStep();
        Lab.I.BeginUpdate();
        Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        TestTrue(TEXT("the next step does not pour"),
            Lab.A.World.Get(Lab.Aimed).State.IsAir() && Lab.HandItem() == TEXT("water_bucket"));
        // Steps after the fill until the pour: exactly the held repeat.
        int32 Steps = 1;
        while (Lab.HandItem() == TEXT("water_bucket") && Steps < 20)
        {
            Lab.NextStep();
            Lab.I.BeginUpdate();
            Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
            ++Steps;
        }
        Lab.I.Release(EFoundationInput::Use);
        TestEqual(TEXT("held, it pours after the repeat"), Lab.HandItem(), FName(TEXT("bucket")));
        TestEqual(TEXT("the pour waits the held repeat"), Steps, Lab.I.HeldRepeatSteps);
        TestTrue(TEXT("the source is back"), Lab.IsSource(Lab.Aimed, false));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationItemsBucketBlockUseTest, "Unnamed.Foundation.Items.BucketBlockUse", BucketTestFlags)

bool FFoundationItemsBucketBlockUseTest::RunTest(const FString&)
{
    // A bucket and a block's own use (survival step 7, foundation.Blocks.Use): through the client, a water bucket
    // aimed at a crafting table opens the table and keeps its water, as vanilla's block use comes first; crouching,
    // the block use is skipped and the bucket pours beside the table.
    for (const bool bSneak : {false, true})
    {
        FBucketLab Lab;
        Lab.A.Options.bBlockUse = true;
        Lab.bSneak = bSneak;
        Lab.Give(0, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("crafting_table"));
        Lab.NextStep();
        Lab.I.Press(EFoundationInput::Use, 0);
        Lab.NextStep();
        Lab.I.BeginUpdate();
        Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        Lab.I.Release(EFoundationInput::Use);
        const bool bOpen = Lab.P().OpenMenu.IsOpen() && Lab.P().OpenMenu.Kind == EFoundationMenuKind::Crafting;
        if (!bSneak)
        {
            TestTrue(TEXT("the table opens"), bOpen);
            TestEqual(TEXT("the water bucket stays full"), Lab.HandItem(), FName(TEXT("water_bucket")));
            TestTrue(TEXT("nothing poured"), Lab.A.World.Get(Lab.Beside).State.IsAir());
        }
        else
        {
            TestFalse(TEXT("crouching: the table stays closed"), bOpen);
            TestTrue(TEXT("crouching: the bucket pours beside the table"), Lab.IsSource(Lab.Beside, false));
            TestEqual(TEXT("crouching: an empty bucket"), Lab.HandItem(), FName(TEXT("bucket")));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationItemsBucketReplaceTest, "Unnamed.Foundation.Items.BucketReplace", BucketTestFlags)

bool FFoundationItemsBucketReplaceTest::RunTest(const FString&)
{
    // Codex review 13 #2: water replaces a block fluids wash away (a torch, a flower), which pops its drop as an item
    // entity (minecraft.wiki "Water Bucket"); without item entities it goes without one, as before.
    for (const FName Block : {FName(TEXT("torch")), FName(TEXT("dandelion"))})
        for (const bool bEntities : {true, false})
        {
            FBucketLab Lab;
            if (!TestNotNull(*Block.ToString(), Lab.A.World.FindBlock(Block))) return false;
            Lab.A.Options.bItemEntities = bEntities;
            Lab.Put(FIntVector(Lab.Aimed.X, Lab.Aimed.Y, Lab.Aimed.Z - 1), TEXT("stone"));
            Lab.Put(Lab.Aimed, Block);
            Lab.Give(0, TEXT("water_bucket"));
            const FFoundationResult Result = Lab.Use();
            TestTrue(FString::Printf(TEXT("water replaces the %s"), *Block.ToString()),
                Result.bEmptied && Result.Cell == Lab.Aimed && Lab.IsSource(Lab.Aimed, false));
            const TArray<FFoundationItemEntity>& Items = Lab.A.GetItemEntities();
            const bool bDropped =
                Items.Num() == 1 && Lab.A.Rules.GetItem(Items[0].Item).Id == Block && Items[0].Count == 1;
            if (bEntities)
                TestTrue(FString::Printf(TEXT("the %s pops its drop"), *Block.ToString()), bDropped);
            else
                TestEqual(TEXT("without item entities, no drop"), Items.Num(), 0);
        }
    {
        // Codex review 13 #3: a double slab takes no water (minecraft.wiki "Slab"); the water goes beside it.
        FBucketLab Lab;
        Lab.Give(0, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("oak_slab"), TEXT("type"), TEXT("double"));
        const FFoundationResult Result = Lab.Use();
        TestTrue(TEXT("poured beside the double slab"),
            Result.bEmptied && Result.Cell == Lab.Beside && Lab.IsSource(Lab.Beside, false));
        TestEqual(TEXT("the double slab stays dry"), Lab.Value(Lab.Aimed, TEXT("waterlogged")), FName(TEXT("false")));
        TestEqual(TEXT("and double"), Lab.Value(Lab.Aimed, TEXT("type")), FName(TEXT("double")));
    }
    {
        // Codex review 13 #1: the poured bottom slab holds a water source, as the voxel table and meshes read it.
        FBucketLab Lab;
        Lab.Give(0, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("oak_slab"), TEXT("type"), TEXT("bottom"));
        TestTrue(TEXT("the slab takes the water"), Lab.Use().bEmptied);
        const FFoundationFluid Fluid = Lab.Registry().GetFluid(Lab.A.World.Get(Lab.Aimed).State);
        TestTrue(TEXT("the waterlogged bottom slab holds a water source"), Fluid.bWater && Fluid.bSource);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationItemsBucketWaterlogTest, "Unnamed.Foundation.Items.BucketWaterlog", BucketTestFlags)

bool FFoundationItemsBucketWaterlogTest::RunTest(const FString&)
{
    {
        // A water bucket waterlogs the clicked slab in place; the empty bucket takes the water back out.
        FBucketLab Lab;
        Lab.Give(0, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("oak_slab"), TEXT("waterlogged"), TEXT("false"));
        FFoundationResult Result = Lab.Use();
        TestTrue(TEXT("the slab takes the water"), Result.bEmptied && Result.Cell == Lab.Aimed);
        TestEqual(TEXT("still a slab"), Lab.BlockAt(Lab.Aimed), FName(TEXT("oak_slab")));
        TestEqual(TEXT("waterlogged"), Lab.Value(Lab.Aimed, TEXT("waterlogged")), FName(TEXT("true")));
        TestTrue(TEXT("nothing beside"), Lab.A.World.Get(Lab.Beside).State.IsAir());
        Result = Lab.Use();
        TestTrue(TEXT("the empty bucket takes the slab's water"), Result.bFilled && Result.Cell == Lab.Aimed);
        TestEqual(TEXT("dry again"), Lab.Value(Lab.Aimed, TEXT("waterlogged")), FName(TEXT("false")));
        TestEqual(TEXT("the slab stays"), Lab.BlockAt(Lab.Aimed), FName(TEXT("oak_slab")));
        TestEqual(TEXT("a water bucket"), Lab.HandItem(), FName(TEXT("water_bucket")));
        // A dry slab gives nothing.
        Lab.Give(0, TEXT("bucket"));
        TestEqual(TEXT("a dry slab passes"), Lab.Use().Outcome, EFoundationOutcome::Pass);
        // Lava never waterlogs: it goes beside the slab.
        Lab.Give(0, TEXT("lava_bucket"));
        Result = Lab.Use();
        TestTrue(
            TEXT("lava goes beside"), Result.bEmptied && Result.Cell == Lab.Beside && Lab.IsSource(Lab.Beside, true));
        TestEqual(TEXT("the slab stays dry"), Lab.Value(Lab.Aimed, TEXT("waterlogged")), FName(TEXT("false")));
    }
    {
        // The eye inside a water source: the empty bucket takes that water, before the stone it looks at.
        FBucketLab Lab;
        const FIntVector EyeCell(0, 0, 1);
        Lab.PutFluid(EyeCell, TEXT("water"), 0);
        Lab.Put(Lab.Aimed, TEXT("stone"));
        const FFoundationResult Result = Lab.Use();
        TestTrue(TEXT("fills from the eye's cell"), Result.bFilled && Result.Cell == EyeCell);
        TestTrue(TEXT("the eye's cell is air"), Lab.A.World.Get(EyeCell).State.IsAir());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationItemsBucketExtinguishTest, "Unnamed.Foundation.Items.BucketExtinguish", BucketTestFlags)

bool FFoundationItemsBucketExtinguishTest::RunTest(const FString&)
{
    // The fluid system's flush (SettleAfterFluid): lava or water hardened into stone, obsidian, cobblestone or basalt
    // sends Extinguished; other changes and switch 0 do not.
    FBucketLab Lab;
    const FFoundationBlockState Lava = Lab.PutFluid(Lab.Aimed, TEXT("lava"), 0);
    const FFoundationBlockState Water = Lab.PutFluid(Lab.Beside, TEXT("water"), 0);
    int32 Expected = 0;
    for (const TCHAR* Made : {TEXT("obsidian"), TEXT("cobblestone"), TEXT("stone"), TEXT("basalt")})
    {
        Lab.Put(Lab.Aimed, Made);
        Lab.A.SettleAfterFluid(Lab.Aimed, Lava, false);
        TestEqual(FString::Printf(TEXT("lava to %s extinguishes"), Made),
            Lab.CountEvents(EFoundationEventKind::Extinguished), ++Expected);
    }
    const FFoundationEvent* Event = Lab.LastEvent(EFoundationEventKind::Extinguished);
    TestTrue(TEXT("the event names the cell and the block"),
        Event && Event->Cell == Lab.Aimed && Event->State == Lab.A.World.Get(Lab.Aimed).State);
    // Water under falling lava hardening into stone counts too.
    Lab.Put(Lab.Beside, TEXT("stone"));
    Lab.A.SettleAfterFluid(Lab.Beside, Water, false);
    TestEqual(TEXT("water to stone extinguishes"), Lab.CountEvents(EFoundationEventKind::Extinguished), ++Expected);
    // Lava spreading into air, lava draining, a solid block changing and a washed-away plant do not.
    Lab.PutFluid(Lab.Aimed, TEXT("lava"), 2);
    Lab.A.SettleAfterFluid(Lab.Aimed, {}, false);
    Lab.A.World.Set(Lab.Aimed, {});
    Lab.A.SettleAfterFluid(Lab.Aimed, Lava, false);
    Lab.Put(Lab.Aimed, TEXT("stone"));
    Lab.A.SettleAfterFluid(Lab.Aimed, Lab.A.World.FindBlock(TEXT("dirt"))->GetDefaultState(), false);
    Lab.PutFluid(Lab.Aimed, TEXT("water"), 1);
    Lab.A.SettleAfterFluid(Lab.Aimed, Lab.A.World.FindBlock(TEXT("short_grass"))->GetDefaultState(), true);
    TestEqual(TEXT("no other extinguish"), Lab.CountEvents(EFoundationEventKind::Extinguished), Expected);
    // Switch 0 (foundation.Items.Use): no event.
    FBucketLab Off(false);
    Off.Put(Off.Aimed, TEXT("obsidian"));
    Off.A.SettleAfterFluid(Off.Aimed, Lava, false);
    TestEqual(TEXT("switch 0: no extinguish"), Off.CountEvents(EFoundationEventKind::Extinguished), 0);
    return true;
}

namespace
{
    // One press of use through the client (UseOn first, then the item's use), released after the step.
    void PressUse(FBucketLab& Lab)
    {
        Lab.NextStep();
        Lab.I.Press(EFoundationInput::Use, 0);
        Lab.NextStep();
        Lab.I.BeginUpdate();
        Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        Lab.I.Release(EFoundationInput::Use);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsCauldronTest, "Unnamed.Foundation.Items.Cauldron", BucketTestFlags)

bool FFoundationItemsCauldronTest::RunTest(const FString&)
{
    // Survival step 16c (minecraft.wiki "Cauldron"): buckets fill and empty a cauldron at Aimed, through the client.
    static const FName Level(TEXT("level"));
    auto ItemOf = [](FBucketLab& Lab, const FFoundationEvent* Event)
    { return Event ? Lab.A.Rules.Items[Event->Item].Id : FName(); };
    {
        // K1, K3: a water bucket fills an empty cauldron to level 3 and leaves an empty bucket; the empty bucket takes
        // the full cauldron back.
        FBucketLab Lab;
        Lab.Give(0, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("cauldron"));
        PressUse(Lab);
        TestEqual(TEXT("K1: a water cauldron"), Lab.BlockAt(Lab.Aimed), FName(TEXT("water_cauldron")));
        TestEqual(TEXT("K1: level 3"), Lab.Value(Lab.Aimed, Level), FName(TEXT("3")));
        TestEqual(TEXT("K1: an empty bucket"), Lab.HandItem(), FName(TEXT("bucket")));
        TestTrue(TEXT("K1: nothing poured beside"), Lab.A.World.Get(Lab.Beside).State.IsAir());
        const FFoundationEvent* Emptied = Lab.LastEvent(EFoundationEventKind::Emptied);
        TestTrue(TEXT("K1: Emptied names the water bucket at the cauldron (item.bucket.empty)"),
            Emptied && Emptied->Cell == Lab.Aimed && ItemOf(Lab, Emptied) == TEXT("water_bucket"));
        PressUse(Lab);
        TestEqual(TEXT("K3: an empty cauldron"), Lab.BlockAt(Lab.Aimed), FName(TEXT("cauldron")));
        TestEqual(TEXT("K3: a water bucket"), Lab.HandItem(), FName(TEXT("water_bucket")));
        const FFoundationEvent* Filled = Lab.LastEvent(EFoundationEventKind::Filled);
        TestTrue(TEXT("K3: Filled names the water bucket at the cauldron's centre (item.bucket.fill)"),
            Filled && Filled->Cell == Lab.Aimed && ItemOf(Lab, Filled) == TEXT("water_bucket") &&
                Filled->LocationCm.Equals(FoundationGrid::CellBox(Lab.Aimed, Lab.A.World.OriginCm).GetCenter()));
        TestEqual(TEXT("two bucket uses"), Lab.A.BucketUses, 2);
    }
    {
        // K2, K4: lava the same.
        FBucketLab Lab;
        Lab.Give(0, TEXT("lava_bucket"));
        Lab.Put(Lab.Aimed, TEXT("cauldron"));
        PressUse(Lab);
        TestEqual(TEXT("K2: a lava cauldron"), Lab.BlockAt(Lab.Aimed), FName(TEXT("lava_cauldron")));
        TestEqual(TEXT("K2: an empty bucket"), Lab.HandItem(), FName(TEXT("bucket")));
        TestEqual(TEXT("K2: Emptied names the lava bucket (item.bucket.empty_lava)"),
            ItemOf(Lab, Lab.LastEvent(EFoundationEventKind::Emptied)), FName(TEXT("lava_bucket")));
        PressUse(Lab);
        TestEqual(TEXT("K4: an empty cauldron"), Lab.BlockAt(Lab.Aimed), FName(TEXT("cauldron")));
        TestEqual(TEXT("K4: a lava bucket"), Lab.HandItem(), FName(TEXT("lava_bucket")));
        TestEqual(TEXT("K4: Filled names the lava bucket (item.bucket.fill_lava)"),
            ItemOf(Lab, Lab.LastEvent(EFoundationEventKind::Filled)), FName(TEXT("lava_bucket")));
    }
    for (const TCHAR* Partial : {TEXT("1"), TEXT("2")})
    {
        // K5: an empty bucket on a water cauldron below level 3 does nothing (no fill, no pour).
        FBucketLab Lab;
        const FFoundationBlockState Before = Lab.Put(Lab.Aimed, TEXT("water_cauldron"), Level, Partial);
        PressUse(Lab);
        TestEqual(TEXT("K5: unchanged"), Lab.A.World.Get(Lab.Aimed).State, Before);
        TestEqual(TEXT("K5: still an empty bucket"), Lab.HandItem(), FName(TEXT("bucket")));
        TestEqual(TEXT("K5: no bucket use"), Lab.A.BucketUses, 0);
    }
    {
        // Q2 (provisional): a full bucket on a cauldron holding something else replaces the contents, full.
        FBucketLab Lab;
        Lab.Give(0, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("lava_cauldron"));
        PressUse(Lab);
        TestTrue(TEXT("Q2: water over lava"),
            Lab.BlockAt(Lab.Aimed) == TEXT("water_cauldron") && Lab.Value(Lab.Aimed, Level) == TEXT("3"));
        Lab.Give(0, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("water_cauldron"), Level, TEXT("1"));
        PressUse(Lab);
        TestTrue(TEXT("Q2: water tops up level 1"),
            Lab.Value(Lab.Aimed, Level) == TEXT("3") && Lab.HandItem() == TEXT("bucket"));
        Lab.Give(0, TEXT("lava_bucket"));
        PressUse(Lab);
        TestTrue(TEXT("Q2: lava over water"),
            Lab.BlockAt(Lab.Aimed) == TEXT("lava_cauldron") && Lab.HandItem() == TEXT("bucket"));
    }
    {
        // K6: Creative keeps the bucket; taking adds the filled bucket only when none is held.
        FBucketLab Lab;
        Lab.Vitals.Mode = EFoundationPlayerMode::Creative;
        Lab.Give(0, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("cauldron"));
        PressUse(Lab);
        TestEqual(TEXT("K6: filled"), Lab.BlockAt(Lab.Aimed), FName(TEXT("water_cauldron")));
        TestEqual(TEXT("K6: the water bucket kept"), Lab.HandItem(), FName(TEXT("water_bucket")));
        TestEqual(TEXT("K6: no empty bucket added"), Lab.CountItem(TEXT("bucket")), 0);
    }
    {
        // Adventure: no block changes; the bucket's own use passes too (step 17).
        FBucketLab Lab;
        Lab.Vitals.Mode = EFoundationPlayerMode::Adventure;
        Lab.Give(0, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("cauldron"));
        PressUse(Lab);
        TestTrue(TEXT("Adventure: nothing"),
            Lab.BlockAt(Lab.Aimed) == TEXT("cauldron") && Lab.HandItem() == TEXT("water_bucket") &&
                Lab.A.World.Get(Lab.Beside).State.IsAir());
    }
    {
        // K7: crouching with the bucket skips the cauldron: the bucket pours beside it (step 17).
        FBucketLab Lab;
        Lab.bSneak = true;
        Lab.Give(0, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("cauldron"));
        PressUse(Lab);
        TestEqual(TEXT("K7: the cauldron stays empty"), Lab.BlockAt(Lab.Aimed), FName(TEXT("cauldron")));
        TestTrue(TEXT("K7: water beside it"), Lab.IsSource(Lab.Beside, false));
    }
    {
        // K8: foundation.Items.Use 0: nothing; foundation.Cauldrons 0: the bucket pours beside, as step 17.
        FBucketLab Off(false);
        Off.Give(0, TEXT("water_bucket"));
        Off.Put(Off.Aimed, TEXT("cauldron"));
        PressUse(Off);
        TestTrue(TEXT("K8: Items.Use 0, nothing"),
            Off.BlockAt(Off.Aimed) == TEXT("cauldron") && Off.HandItem() == TEXT("water_bucket") &&
                Off.A.World.Get(Off.Beside).State.IsAir());
        FBucketLab NoCauldrons;
        NoCauldrons.A.Options.bCauldrons = false;
        NoCauldrons.Give(0, TEXT("water_bucket"));
        NoCauldrons.Put(NoCauldrons.Aimed, TEXT("cauldron"));
        PressUse(NoCauldrons);
        TestEqual(TEXT("K8: Cauldrons 0, the cauldron stays empty"), NoCauldrons.BlockAt(NoCauldrons.Aimed),
            FName(TEXT("cauldron")));
        TestTrue(TEXT("K8: Cauldrons 0, water beside it"), NoCauldrons.IsSource(NoCauldrons.Beside, false));
    }
    {
        // K9: a stack of empty buckets: one goes and the filled bucket is added; with no room (and no item entities)
        // the take is refused and nothing changes.
        FBucketLab Lab;
        Lab.A.Options.bFullInventory = false;
        Lab.Give(0, TEXT("bucket"), 2);
        Lab.Put(Lab.Aimed, TEXT("water_cauldron"), Level, TEXT("3"));
        PressUse(Lab);
        TestTrue(TEXT("K9: a stack takes"),
            Lab.BlockAt(Lab.Aimed) == TEXT("cauldron") && Lab.HandCount() == 1 &&
                Lab.CountItem(TEXT("water_bucket")) == 1);
        Lab.Give(0, TEXT("bucket"), 2);
        for (int32 Slot = 1; Slot < FFoundationInventory::HotbarSlots; ++Slot)
            Lab.Give(Slot, TEXT("stone"), 64);
        Lab.Give(FFoundationInventory::OffhandSlot, TEXT("stone"), 64);
        const FFoundationBlockState Full = Lab.Put(Lab.Aimed, TEXT("water_cauldron"), Level, TEXT("3"));
        const int32 Uses = Lab.A.BucketUses;
        PressUse(Lab);
        TestTrue(TEXT("K9: a full inventory refuses"),
            Lab.A.World.Get(Lab.Aimed).State == Full && Lab.HandCount() == 2 && Lab.A.BucketUses == Uses);
    }
    {
        // K10: the offhand's bucket fills it while the main hand is empty.
        FBucketLab Lab;
        Lab.Exec(EFoundationCommandType::SelectSlot, EFoundationHand::Main, 1);
        Lab.Give(FFoundationInventory::OffhandSlot, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("cauldron"));
        PressUse(Lab);
        TestEqual(TEXT("K10: filled from the offhand"), Lab.BlockAt(Lab.Aimed), FName(TEXT("water_cauldron")));
        TestEqual(
            TEXT("K10: an empty bucket in the offhand"), Lab.HandItem(EFoundationHand::Off), FName(TEXT("bucket")));
    }
    {
        // K11: held, the client fills once and waits the held repeat before taking the water back.
        FBucketLab Lab;
        Lab.Give(0, TEXT("water_bucket"));
        Lab.Put(Lab.Aimed, TEXT("cauldron"));
        Lab.NextStep();
        Lab.I.Press(EFoundationInput::Use, 0);
        Lab.NextStep();
        Lab.I.BeginUpdate();
        Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
        TestEqual(TEXT("K11: the press fills"), Lab.BlockAt(Lab.Aimed), FName(TEXT("water_cauldron")));
        int32 Steps = 0;
        while (Lab.HandItem() == TEXT("bucket") && Steps < 20)
        {
            Lab.NextStep();
            Lab.I.BeginUpdate();
            Lab.I.StepOnce(Lab.A, Lab.Vitals, true, 0);
            ++Steps;
        }
        Lab.I.Release(EFoundationInput::Use);
        TestEqual(TEXT("K11: held, it takes the water back"), Lab.BlockAt(Lab.Aimed), FName(TEXT("cauldron")));
        TestEqual(TEXT("K11: after the held repeat"), Steps, Lab.I.HeldRepeatSteps);
    }
    return true;
}
#endif
