#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockPlacement.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationVitals.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Voxel/VoxelBlocks.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags TorchTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    TSharedPtr<FJsonObject> ReadTorchReference(const TCHAR* Name)
    {
        FString Text;
        TSharedPtr<FJsonObject> Json;
        const FString Path = FPaths::ProjectDir() / TEXT("Reference/minecraft-blocks-26.3") / Name;
        if (FFileHelper::LoadFileToString(Text, *Path))
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
        return Json;
    }

    struct FTorchTestWorld
    {
        FFoundationBlockWorld World;

        FTorchTestWorld()
        {
            World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            World.BoundsMin = FIntVector(-8);
            World.BoundsMax = FIntVector(7);
        }

        FFoundationBlockState State(const TCHAR* Text) const
        {
            FFoundationBlockState State;
            ensureMsgf(World.GetRegistry().Parse(Text, State), TEXT("parse %s"), Text);
            return State;
        }

        // The torch item's state for a plain click on Face of Support at the origin, the other cells air, looking level
        // south. False when nothing holds it.
        bool Survives(const TCHAR* Support, EFoundationFace Face, FFoundationBlockState* OutState = nullptr)
        {
            const FIntVector Destination = FoundationGrid::FaceOffset(Face);
            World.Set(FIntVector::ZeroValue, State(Support));
            World.Set(Destination, FFoundationBlockState());
            FFoundationPlacementContext Context;
            Context.ClickedFace = Face;
            Context.Destination = Destination;
            FFoundationBlockState Placed;
            const bool bPlaced = FoundationBlockPlacement::GetTorchState(
                World, *World.FindBlock(TEXT("torch")), *World.FindBlock(TEXT("wall_torch")), Context, Placed);
            if (OutState) *OutState = Placed;
            return bPlaced;
        }
    };

    // Probe P3's direction letters (D U N S W E) for EFoundationFace, and back.
    EFoundationFace ProbeFace(TCHAR Letter)
    {
        const int32 Index = FString(TEXT("WENSDU")).Find(FString::Chr(Letter), ESearchCase::CaseSensitive);
        return Index == INDEX_NONE ? EFoundationFace::None : EFoundationFace(Index);
    }

    FString ProbeLetters(const TStaticArray<EFoundationFace, 6>& Looks)
    {
        FString Text;
        for (const EFoundationFace Face : Looks)
            Text.AppendChar(TEXT("WENSDU?")[FMath::Min(int32(Face), 6)]);
        return Text;
    }

    // An authority with the project's blocks and rules, finite supply, one player holding the empty slot 4, and the
    // cells (-20, -20, 0) to (19, 19, 15): the ground below z 0 is unknown.
    struct FTorchLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        int32 Player = 0;
        uint32 Sequence = 1;
        int64 StepIndex = 0;

        FTorchLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.PlantSurvival = VoxelBlocks::GetPlantSurvival(A.World.GetRegistry());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bInfiniteSupply = false;
            Player = A.AddPlayer();
            A.Reset();
            Vitals.bNaturalRegeneration = false;
            P().Inventory.Select(3);
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }

        FFoundationBlockState State(const TCHAR* Text) const
        {
            FFoundationBlockState Parsed;
            ensureMsgf(A.World.GetRegistry().Parse(Text, Parsed), TEXT("parse %s"), Text);
            return Parsed;
        }

        void Put(FIntVector Cell, const TCHAR* Text) { A.World.Set(Cell, State(Text)); }

        bool Solid(FIntVector Cell) const { return !A.World.Get(Cell).State.IsAir(); }

        // Standing at Feet, the eye 162 cm up, aiming along Aim.
        void Stand(FVector Feet, FVector Aim)
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

        // Mines the aimed block through the commands: the steps to its break (0: at once), negative without one.
        int32 Mine(int32 Limit = 400)
        {
            FFoundationResult R = Run(EFoundationCommandType::MineStart);
            if (R.bBroken) return 0;
            for (int32 Step = 1; Step <= Limit; ++Step)
            {
                R = Run(EFoundationCommandType::MineContinue);
                if (R.bBroken) return Step;
                if (!R.IsCommitted()) return -Step;
                NextStep();
            }
            return -1;
        }

        // How many of an item the hotbar and offhand hold.
        int32 Count(const TCHAR* Item)
        {
            const int32 Index = A.Rules.FindItem(Item);
            const FFoundationInventory& Inventory = P().Inventory;
            int32 Total = !Inventory.Offhand.IsEmpty() && Inventory.Offhand.Item == Index ? Inventory.Offhand.Count : 0;
            for (const FFoundationItemInstance& Stack : Inventory.Hotbar)
                Total += !Stack.IsEmpty() && Stack.Item == Index ? Stack.Count : 0;
            return Total;
        }

        TArray<FFoundationEvent> PopsAfter(int64 Serial) const
        {
            return A.GetEvents().FilterByPredicate([Serial](const FFoundationEvent& Event)
                { return Event.Serial > Serial && Event.Kind == EFoundationEventKind::Popped; });
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBlocksSupportTest, "Unnamed.Foundation.Blocks.Support", TorchTestFlags)

bool FFoundationBlocksSupportTest::RunTest(const FString&)
{
    // Every imported state holds a torch on exactly the faces facts/block_support.json measured.
    const TSharedPtr<FJsonObject> Support = ReadTorchReference(TEXT("facts/block_support.json"));
    if (!TestTrue(TEXT("block_support.json"), Support.IsValid())) return false;
    const FFoundationBlockRegistry& Registry = *FFoundationBlockRegistry::GetDefault();
    int32 States = 0, Different = 0;
    for (const TSharedPtr<FJsonValue>& Value : Support->GetArrayField(TEXT("blocks")))
    {
        const TSharedPtr<FJsonObject> Block = Value->AsObject();
        const int32 First = int32(Block->GetNumberField(TEXT("first_state")));
        const TArray<TSharedPtr<FJsonValue>>& Masks = Block->GetArrayField(TEXT("support"));
        for (int32 Index = 0; Index < Masks.Num(); ++Index, ++States)
        {
            FFoundationBlockState State;
            State.Id = uint16(First + Index);
            if (Registry.GetTorchSupport(State) != uint8(Masks[Index]->AsNumber()) && ++Different <= 5)
                AddError(FString::Printf(TEXT("%s state %d holds a torch on mask %d, not %d"),
                    *Block->GetStringField(TEXT("id")), State.Id, Registry.GetTorchSupport(State),
                    int32(Masks[Index]->AsNumber())));
        }
    }
    TestEqual(TEXT("every imported state"), States, 4424);
    TestEqual(TEXT("states whose support differs"), Different, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBlocksTorchTest, "Unnamed.Foundation.Blocks.Torch", TorchTestFlags)

bool FFoundationBlocksTorchTest::RunTest(const FString&)
{
    using namespace FoundationBlockPlacement;
    // One torch item (the wall torch has none); it places the wall torch against a side face.
    FFoundationRules Rules;
    UFoundationInteractionSettings::Get().BuildRules(Rules);
    const FFoundationItemDefinition* Torch =
        Rules.Items.FindByPredicate([](const FFoundationItemDefinition& Item) { return Item.Id == TEXT("torch"); });
    if (!TestNotNull(TEXT("torch item"), Torch)) return false;
    TestTrue(TEXT("torch item: places torch, wall_torch on a side, stacks to 64"),
        Torch->PlacesBlock == TEXT("torch") && Torch->PlacesWallBlock == TEXT("wall_torch") && Torch->MaxStack == 64);
    TestFalse(TEXT("no wall_torch item"),
        Rules.Items.ContainsByPredicate([](const FFoundationItemDefinition& Item)
            { return Item.Id == TEXT("wall_torch") || Item.PlacesBlock == TEXT("wall_torch"); }));
    // The torch and the five coral fans (S7b, Jesus's answer to Q7): the reference's standing-and-wall items.
    TestEqual(TEXT("items with a wall block"),
        Rules.Items
            .FilterByPredicate([](const FFoundationItemDefinition& Item) { return !Item.PlacesWallBlock.IsNone(); })
            .Num(),
        6);

    // A plain click on stone: the floor torch on top, on a side the wall torch facing the clicked side, away from its
    // wall (whatever the look; Blocks.TorchProbe checks every look).
    FTorchTestWorld T;
    TestTrue(TEXT("wall torch rule"),
        T.World.FindBlock(TEXT("wall_torch"))->Placement == EFoundationPlacementRule::WallAttached);
    FFoundationBlockState Placed;
    TestTrue(TEXT("floor torch on the top face"),
        T.Survives(TEXT("stone"), EFoundationFace::Up, &Placed) && Placed == T.State(TEXT("torch")));
    for (const EFoundationFace Face :
        {EFoundationFace::West, EFoundationFace::East, EFoundationFace::North, EFoundationFace::South})
        TestTrue(FString::Printf(TEXT("wall torch on the %s face"), FoundationGrid::FaceName(Face)),
            T.Survives(TEXT("stone"), Face, &Placed) &&
                Placed == T.State(*FString::Printf(TEXT("wall_torch[facing=%s]"), FoundationGrid::FaceName(Face))));

    // Survival from the support table (minecraft.wiki "Torch": leaves are no support; upside-down slabs hold a torch
    // on top). Columns: top, east side.
    struct FCase
    {
        const TCHAR* Support;
        bool bTop, bSide;
    };
    for (const FCase& Case : {FCase{TEXT("stone"), true, true}, FCase{TEXT("glass"), true, true},
             FCase{TEXT("oak_leaves[persistent=true]"), false, false},
             FCase{TEXT("oak_slab[type=bottom]"), false, false}, FCase{TEXT("oak_slab[type=top]"), true, false},
             FCase{TEXT("oak_slab[type=double]"), true, true}, FCase{TEXT("air"), false, false},
             FCase{TEXT("torch"), false, false}})
    {
        TestEqual(FString::Printf(TEXT("torch on top of %s"), Case.Support),
            T.Survives(Case.Support, EFoundationFace::Up), Case.bTop);
        TestEqual(FString::Printf(TEXT("wall torch on the east of %s"), Case.Support),
            T.Survives(Case.Support, EFoundationFace::East), Case.bSide);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationBlocksTorchSurvivalTest, "Unnamed.Foundation.Blocks.TorchSurvival", TorchTestFlags)

bool FFoundationBlocksTorchSurvivalTest::RunTest(const FString&)
{
    // The registry: the standing-and-wall pair follows the torch rule, and the wall torch (no item) drops a torch.
    const FFoundationBlockRegistry& Registry = *FFoundationBlockRegistry::GetDefault();
    const FFoundationBlockType* Torch = Registry.FindBlock(TEXT("torch"));
    const FFoundationBlockType* Wall = Registry.FindBlock(TEXT("wall_torch"));
    if (!TestTrue(TEXT("torch and wall_torch"), Torch && Wall)) return false;
    int32 Ruled = 0;
    for (int32 Index = 0; Index < Registry.GetBlockCount(); ++Index)
        Ruled += Registry.GetBlockAt(Index).bTorchRule;
    TestTrue(TEXT("the torch rule: torch and wall_torch only"), Torch->bTorchRule && Wall->bTorchRule && Ruled == 2);
    TestTrue(TEXT("a wall torch has no item and drops a torch"),
        Wall->Item.IsNone() && Wall->DropItem == TEXT("torch") && Torch->DropItem == TEXT("torch"));

    // S7's cascade (one level, Popped, no drop) with the support table. Dirt (hardness 0.5, by hand) is mined: from
    // (-150, 50, 0) this aim meets the west face of (0, 0, 1).
    const FVector Feet(-150, 50, 0), Aim(200, 0, -12);
    const FIntVector Mined(0, 0, 1), Above(0, 0, 2), East(1, 0, 1);
    {
        FTorchLab L;
        L.Put(Mined, TEXT("dirt"));
        L.Put(Above, TEXT("torch"));
        L.Stand(Feet, Aim);
        const int64 Mark = L.A.GetEventSerial();
        TestTrue(TEXT("the block under a floor torch mined"), L.Mine() > 0 && !L.Solid(Mined));
        const TArray<FFoundationEvent> Pops = L.PopsAfter(Mark);
        TestTrue(TEXT("the floor torch pops"), !L.Solid(Above) && L.A.Pops == 1);
        TestTrue(TEXT("one Popped event, for the torch"),
            Pops.Num() == 1 && Pops[0].Cell == Above && Pops[0].State.Id == L.State(TEXT("torch")).Id);
        TestTrue(TEXT("the pop drops nothing; the mined dirt comes back"),
            L.Count(TEXT("torch")) == 0 && L.Count(TEXT("dirt")) == 1);
    }
    {
        FTorchLab L;
        L.Put(Mined, TEXT("dirt"));
        L.Put(East, TEXT("wall_torch[facing=east]"));
        L.Stand(Feet, Aim);
        const int64 Mark = L.A.GetEventSerial();
        TestTrue(TEXT("a wall torch's wall mined"), L.Mine() > 0 && !L.Solid(Mined));
        const TArray<FFoundationEvent> Pops = L.PopsAfter(Mark);
        TestTrue(TEXT("the wall torch pops"),
            !L.Solid(East) && Pops.Num() == 1 && Pops[0].Cell == East &&
                Pops[0].State.Id == L.State(TEXT("wall_torch[facing=east]")).Id);
        TestEqual(TEXT("the wall torch's pop drops nothing"), L.Count(TEXT("torch")), 0);
    }
    {
        // Beside and above the mined block, each held by another block.
        FTorchLab L;
        L.Put(Mined, TEXT("dirt"));
        L.Put({0, 1, 0}, TEXT("dirt"));
        L.Put({0, 1, 1}, TEXT("torch"));
        L.Put({-1, 0, 2}, TEXT("dirt"));
        L.Put(Above, TEXT("wall_torch[facing=east]"));
        L.Stand(Feet, Aim);
        TestTrue(TEXT("a block beside a floor torch and under a wall torch mined"), L.Mine() > 0 && !L.Solid(Mined));
        TestTrue(TEXT("neither torch pops"), L.Solid({0, 1, 1}) && L.Solid(Above) && L.A.Pops == 0);
    }
    {
        // The ground below z 0 is unknown: a torch standing on it stays when its neighbour is mined.
        FTorchLab L;
        L.Put({9, 1, 0}, TEXT("torch"));
        L.Put({9, 0, 0}, TEXT("dirt"));
        L.Stand({750, 50, 0}, FVector(150, 0, -112));
        TestTrue(TEXT("the neighbour of a torch over an unknown cell mined"), L.Mine() > 0 && !L.Solid({9, 0, 0}));
        TestTrue(TEXT("a torch over an unknown cell stays"), L.Solid({9, 1, 0}) && L.A.Pops == 0);
    }
    {
        // Breaking a wall torch (hardness 0, at once) in the finite fixture returns one torch.
        FTorchLab L;
        L.Put({2, 0, 1}, TEXT("stone"));
        L.Put(East, TEXT("wall_torch[facing=west]"));
        L.Stand(Feet, Aim);
        const FFoundationHit Hit = L.A.Resolve(L.P().Facts);
        TestTrue(TEXT("the aim meets the wall torch"), Hit.Kind == EFoundationHitKind::Block && Hit.Cell == East);
        TestEqual(TEXT("a wall torch breaks at once"), L.Mine(), 0);
        TestTrue(TEXT("breaking a wall torch returns one torch"),
            !L.Solid(East) && L.Solid({2, 0, 1}) && L.Count(TEXT("torch")) == 1 && L.A.Pops == 0);
    }
    {
        // Pick block on a wall torch selects the hotbar's torch.
        FTorchLab L;
        L.Put({2, 0, 1}, TEXT("stone"));
        L.Put(East, TEXT("wall_torch[facing=west]"));
        L.P().Inventory.SetSlot(5, L.P().Inventory.MakeStack(L.A.Rules.FindItem(TEXT("torch")), 64));
        L.P().Inventory.Select(0);
        L.Stand(Feet, Aim);
        FFoundationInteraction I;
        I.PlayerId = L.Player;
        I.Resync(L.A);
        I.Target = L.A.Resolve(L.P().Facts);
        TestTrue(TEXT("pick block on a wall torch selects the torch"),
            I.Pick(L.A, L.Vitals) && L.P().Inventory.Selected == 5);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationBlocksTorchProbeTest, "Unnamed.Foundation.Blocks.TorchProbe", TorchTestFlags)

bool FFoundationBlocksTorchProbeTest::RunTest(const FString&)
{
    using namespace FoundationBlockPlacement;
    // Probe P3's matrix (Tests/Probes/README.md): 26.3's torch item clicked at 312 looks on every face of stone and of
    // short grass, with every set of supports around the destination D.
    const FString Path = FPaths::ProjectDir() / TEXT("Tests/Probes/torch-placement-26.3.json");
    FString Text;
    TSharedPtr<FJsonObject> Matrix;
    if (!FFileHelper::LoadFileToString(Text, *Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Matrix) || !Matrix.IsValid())
    {
        AddError(FString::Printf(TEXT("probe P3's matrix is missing or unreadable: %s"), *Path));
        return false;
    }
    const double Start = FPlatformTime::Seconds();
    const TArray<TSharedPtr<FJsonValue>>& Looks = Matrix->GetArrayField(TEXT("looks"));
    if (!TestEqual(TEXT("looks"), Looks.Num(), 312)) return false;
    TestTrue(TEXT("no others or anomalies"),
        Matrix->GetArrayField(TEXT("others")).IsEmpty() && Matrix->GetArrayField(TEXT("anomalies")).IsEmpty());
    int32 Mismatches = 0;
    auto Mismatch = [this, &Mismatches](const FString& What)
    {
        if (++Mismatches <= 10) AddError(What);
    };

    // (a) The look orders, ties included: nearest, and each click's (plain 'p', replacing 'r').
    TArray<FVector> Aims;
    int32 Orders = 0;
    for (const TSharedPtr<FJsonValue>& Value : Looks)
    {
        const TSharedPtr<FJsonObject> Look = Value->AsObject();
        const double Yaw = Look->GetNumberField(TEXT("yaw")), Pitch = Look->GetNumberField(TEXT("pitch"));
        const FString Nearest = ProbeLetters(GetNearestLooks(Yaw, Pitch));
        if (Nearest != Look->GetStringField(TEXT("nearest")))
            Mismatch(FString::Printf(TEXT("yaw %.0f pitch %.0f: nearest %s, measured %s"), Yaw, Pitch, *Nearest,
                *Look->GetStringField(TEXT("nearest"))));
        ++Orders;
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Click : Look->GetObjectField(TEXT("ctx"))->Values)
        {
            FFoundationPlacementContext Context;
            Context.Yaw = Yaw;
            Context.Pitch = Pitch;
            Context.ClickedFace = ProbeFace(Click.Key[1]);
            Context.bReplaceClicked = Click.Key[0] == TEXT('B');
            const FString Order =
                ProbeLetters(GetClickLooks(Context)) + (Context.bReplaceClicked ? TEXT("r@D") : TEXT("p@D"));
            if (Order != Click.Value->AsString())
                Mismatch(FString::Printf(TEXT("yaw %.0f pitch %.0f click %s: %s, measured %s"), Yaw, Pitch, *Click.Key,
                    *Order, *Click.Value->AsString()));
            ++Orders;
        }
        // The aim that GetYawPitch reads back as this look (FoundationBlockPlacement.h).
        const double YawRadians = FMath::DegreesToRadians(Yaw), PitchRadians = FMath::DegreesToRadians(Pitch);
        Aims.Add(FVector(-FMath::Sin(YawRadians) * FMath::Cos(PitchRadians),
            FMath::Cos(YawRadians) * FMath::Cos(PitchRadians), -FMath::Sin(PitchRadians)));
    }
    TestEqual(TEXT("look orders checked (312 nearest, 12 clicks each)"), Orders, 312 * 13);

    // (b) Every entry through the authority: the probe's cells around D, its hit (the clicked face's centre), the
    // player's aim at the look, then CheckPlacement.
    FTorchLab L;
    L.P().Inventory.SetSlot(3, L.P().Inventory.MakeStack(L.A.Rules.FindItem(TEXT("torch")), 64));
    L.Stand(FVector(-1500, -1500, 0), FVector(1, 0, 0));
    FFoundationBlockWorld& World = L.A.World;
    const FIntVector D(0, 0, 5);
    const FFoundationBlockState Stone = L.State(TEXT("stone")), Grass = L.State(TEXT("grass_block[snowy=false]"));
    TMap<FFoundationBlockState, TCHAR> Codes = {{L.State(TEXT("torch")), TEXT('F')}};
    for (const TCHAR* Facing : {TEXT("north"), TEXT("south"), TEXT("east"), TEXT("west")})
        Codes.Add(L.State(*FString::Printf(TEXT("wall_torch[facing=%s]"), Facing)), Facing[0]);
    // Config characters: stone (case B: the grass block) at D's below, north, south, west and east.
    const EFoundationFace Around[5] = {EFoundationFace::Down, EFoundationFace::North, EFoundationFace::South,
        EFoundationFace::West, EFoundationFace::East};
    int32 Rows = 0, Entries = 0;
    for (const TSharedPtr<FJsonValue>& Value : Matrix->GetArrayField(TEXT("results")))
    {
        const TSharedPtr<FJsonObject> Row = Value->AsObject();
        const bool bGrass = Row->GetStringField(TEXT("case")) == TEXT("B");
        const FString Face = Row->GetStringField(TEXT("face")), Config = Row->GetStringField(TEXT("cfg"));
        const FString Measured = Row->GetStringField(TEXT("r"));
        for (int32 X = -1; X <= 1; ++X)
            for (int32 Y = -1; Y <= 1; ++Y)
                for (int32 Z = -1; Z <= 1; ++Z)
                    World.Set(D + FIntVector(X, Y, Z), FFoundationBlockState());
        for (int32 Index = 0; Index < 5; ++Index)
            if (Config[Index] != TEXT('-'))
                World.Set(D + FoundationGrid::FaceOffset(Around[Index]), bGrass && Index == 0 ? Grass : Stone);
        FFoundationHit Hit;
        Hit.Kind = EFoundationHitKind::Block;
        Hit.Face = ProbeFace(Face[0]);
        Hit.Cell = bGrass ? D : D - FoundationGrid::FaceOffset(Hit.Face);
        Hit.Point = FoundationGrid::CellBox(Hit.Cell, World.OriginCm).GetCenter() +
            FoundationGrid::FaceNormal(Hit.Face) * UFoundationProfile::BlockCm * .5;
        Hit.bInRange = true;
        World.Set(Hit.Cell, bGrass ? L.State(TEXT("short_grass")) : Stone);
        ++Rows;
        for (int32 Index = 0; Index < Measured.Len() && Index < Aims.Num(); ++Index, ++Entries)
        {
            L.P().Facts.Aim = Aims[Index];
            FIntVector Destination;
            FFoundationBlockState Placed;
            const EFoundationReason Reason =
                L.A.CheckPlacement(L.P(), EFoundationHand::Main, Hit, Destination, &Placed);
            const TCHAR* Code = Codes.Find(Placed);
            const TCHAR Got = Destination != D                         ? TEXT('?')
                : Reason == EFoundationReason::CannotSurvive           ? TEXT('-')
                : Reason == EFoundationReason::None && Code != nullptr ? *Code
                                                                       : TEXT('?');
            if (Got != Measured[Index])
                Mismatch(FString::Printf(TEXT("case %s face %s cfg %s yaw %d pitch %d: %c (%s), measured %c"),
                    *Row->GetStringField(TEXT("case")), *Face, *Config, Index / 13 * 15, Index % 13 * 15 - 90, Got,
                    LexToString(Reason), Measured[Index]));
        }
    }
    TestEqual(TEXT("matrix rows"), Rows, 208);
    TestEqual(TEXT("matrix entries checked through the authority"), Entries, 208 * 312);
    TestEqual(TEXT("mismatches"), Mismatches, 0);
    AddInfo(
        FString::Printf(TEXT("TorchProbe: %d look orders, %d rows, %d entries through CheckPlacement, %d mismatches, "
                             "%.2f s"),
            Orders, Rows, Entries, Mismatches, FPlatformTime::Seconds() - Start));
    return true;
}
#endif
