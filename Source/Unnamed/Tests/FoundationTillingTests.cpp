#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockFamilies.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationSoundAssets.h"
#include "Foundation/FoundationTilling.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnnamedProjectSettings.h"

// Survival B5 (track T5): hoe tilling, shovel flattening, and farmland and dirt paths turning to dirt under a block.
// The oracle is the wiki's rows ("Hoe", "Shovel", "Dirt Path", "Rooted Dirt", fetched 2026-10-15), written here apart
// from FoundationTilling's tables; the survival set itself is .Interact.FarmlandSurvival's (family_survival.json).
#if WITH_DEV_AUTOMATION_TESTS
namespace FoundationTillingTests
{
    constexpr EAutomationTestFlags TillTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Sets a console variable for one test and puts it back.
    struct FTillCVarScope
    {
        IConsoleVariable* Variable;
        int32 Old;

        FTillCVarScope(const TCHAR* Name, int32 Value)
            : Variable(IConsoleManager::Get().FindConsoleVariable(Name)), Old(Variable ? Variable->GetInt() : 0)
        {
            if (Variable) Variable->Set(Value, ECVF_SetByCode);
        }
        ~FTillCVarScope()
        {
            if (Variable) Variable->Set(Old, ECVF_SetByCode);
        }
    };

    // The project's rules (DA_Items) and blocks, a finite inventory with tool wear and item entities, and a player
    // whose eye looks at Target's centre from 2.2 blocks out along a face (Look).
    struct FTillLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        FFoundationInteraction I;
        int32 Player = 0;
        int64 StepIndex = 0;
        FIntVector Target = FIntVector(0, 0, 5);
        EFoundationFace Look = EFoundationFace::West;
        // Aim at this point instead of Target's centre when set (a face's centre).
        TOptional<FVector> AimAt;
        TOptional<FVector> EyeAt;

        explicit FTillLab(bool bItemUse = true)
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bInfiniteSupply = false;
            A.Options.bToolWear = true;
            A.Options.bItemUse = bItemUse;
            A.Options.bItemEntities = true;
            Player = A.AddPlayer();
            A.Reset();
            Vitals.bNaturalRegeneration = false;
            Vitals.Food = 10;
            I.PlayerId = Player;
            I.SetPreset(*UFoundationInteractionSettings::Get().FindPreset(TEXT("VanillaReference")), .05);
            I.Resync(A);
            NextStep();
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }
        const FFoundationBlockRegistry& R() { return A.World.GetRegistry(); }

        FVector Eye() const
        {
            if (EyeAt) return *EyeAt;
            const FVector Centre = FoundationGrid::CellBox(Target, A.World.OriginCm).GetCenter();
            return Centre + FVector(FoundationGrid::FaceOffset(Look)) * 220.;
        }

        void NextStep()
        {
            FFoundationPlayerFacts Facts;
            Facts.EyeCm = Eye();
            Facts.FeetCm = Facts.EyeCm - FVector(0, 0, 162);
            const FVector Point = AimAt ? *AimAt : FoundationGrid::CellBox(Target, A.World.OriginCm).GetCenter();
            Facts.Aim = (Point - Facts.EyeCm).GetSafeNormal();
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
            A.Step();
        }

        void Set(const FIntVector& Cell, FName Id)
        {
            A.World.Set(Cell, Id.IsNone() ? FFoundationBlockState() : A.World.FindBlock(Id)->GetDefaultState());
        }
        FName At(const FIntVector& Cell) { return R().GetBlock(A.World.Get(Cell).State).Id; }

        FFoundationResult Exec(
            EFoundationCommandType Type, int32 Slot = 0, FName Item = NAME_None, int32 Count = 0, int32 Damage = 0)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = I.NextSequence++;
            Command.Type = Type;
            Command.Hand = EFoundationHand::Main;
            Command.Item = P().Inventory.GetHand(EFoundationHand::Main).GetRef();
            Command.Slot = Slot;
            Command.CreativeItem = Item;
            Command.CreativeCount = Count;
            Command.CreativeDamage = Damage;
            if (Type == EFoundationCommandType::UseOn)
            {
                Command.Target = A.Resolve(P().Facts);
                if (Command.Target.IsBlockLike())
                {
                    Command.Destination = Command.Target.Cell + FoundationGrid::FaceOffset(Command.Target.Face);
                    Command.DestinationRevision = A.World.Get(Command.Destination).Revision;
                }
            }
            return A.Execute(Player, Command, Vitals);
        }

        // Puts Item (Damage) in slot 0 and selects it.
        void Hold(FName Item, int32 Damage = 0)
        {
            Exec(EFoundationCommandType::SetCreativeSlot, 0, Item, 1, Damage);
            Exec(EFoundationCommandType::SelectSlot, 0);
        }

        FFoundationResult Use() { return Exec(EFoundationCommandType::UseOn); }
        int32 Damage() { return P().Inventory.Hotbar[0].Damage; }
        int32 CountEvents(EFoundationEventKind Kind, int64 After = 0)
        {
            int32 Count = 0;
            for (const FFoundationEvent& Event : A.GetEvents())
                Count += Event.Serial > After && Event.Kind == Kind;
            return Count;
        }
        int64 LastSerial() { return A.GetEvents().IsEmpty() ? 0 : A.GetEvents().Last().Serial; }
    };

    // The wiki's rows: what a hoe or shovel makes of From, NAME_None for nothing; bAnyFace for rooted dirt's hoe row.
    FName TillOracle(bool bHoe, FName From, bool& bAnyFace)
    {
        bAnyFace = false;
        if (bHoe)
        {
            if (From == TEXT("dirt") || From == TEXT("grass_block") || From == TEXT("dirt_path"))
                return TEXT("farmland");
            if (From == TEXT("coarse_dirt")) return TEXT("dirt");
            if (From == TEXT("rooted_dirt"))
            {
                bAnyFace = true;
                return TEXT("dirt");
            }
            return NAME_None;
        }
        for (const TCHAR* Id : {TEXT("grass_block"), TEXT("dirt"), TEXT("coarse_dirt"), TEXT("mycelium"),
                 TEXT("podzol"), TEXT("rooted_dirt")})
            if (From == Id) return TEXT("dirt_path");
        return NAME_None;
    }
}

using namespace FoundationTillingTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationItemsTillReferenceTest, "Unnamed.Foundation.Items.TillReference", TillTestFlags)
bool FFoundationItemsTillReferenceTest::RunTest(const FString&)
{
    // Every block in both tables is imported; the tags hold the 7 hoes and the 7 shovels of the items reference.
    const TSharedRef<const FFoundationBlockRegistry> Shared = FFoundationBlockRegistry::GetDefault();
    for (const TConstArrayView<FoundationTilling::FConversion> Table :
        {FoundationTilling::GetHoeTable(), FoundationTilling::GetShovelTable()})
        for (const FoundationTilling::FConversion& Row : Table)
            TestTrue(Row.From.ToString() + TEXT(" -> ") + Row.To.ToString() + TEXT(" imported"),
                Shared->FindBlock(Row.From) && Shared->FindBlock(Row.To));
    TestEqual(TEXT("5 hoe rows"), FoundationTilling::GetHoeTable().Num(), 5);
    TestEqual(TEXT("6 shovel rows"), FoundationTilling::GetShovelTable().Num(), 6);
    // The view's two sounds are in DA_Sounds, with files.
    const UFoundationSoundAssets* Sounds = LoadObject<UFoundationSoundAssets>(
        nullptr, *UUnnamedProjectSettings::GetSoundAssetPath(TEXT("DA_Sounds")).ToString());
    if (TestNotNull(TEXT("DA_Sounds"), Sounds))
        for (const TCHAR* Name : {TEXT("item.hoe.till"), TEXT("item.shovel.flatten")})
        {
            const FFoundationSoundEvent* Event = Sounds->Events.FindByPredicate(
                [Name](const FFoundationSoundEvent& E) { return E.Name == FName(Name); });
            TestTrue(FString(Name) + TEXT(" in DA_Sounds with files"), Event && !Event->Files.IsEmpty());
        }
    if (!TestNotNull(TEXT("DA_Items"), UFoundationItemAssets::LoadDefault())) return false;
    FTillLab Lab;
    TestTrue(TEXT("hanging_roots is an item"), Lab.A.Rules.FindItem(TEXT("hanging_roots")) != INDEX_NONE);
    const FGameplayTag Hoes = FoundationTilling::GetHoesTag(), Shovels = FoundationTilling::GetShovelsTag();
    if (!TestTrue(TEXT("Item.Hoes and Item.Shovels are registered"), Hoes.IsValid() && Shovels.IsValid())) return false;
    TArray<FString> HoeIds, ShovelIds;
    for (const FFoundationItemDefinition& Item : Lab.A.Rules.Items)
    {
        if (Item.Tags.HasTagExact(Hoes)) HoeIds.Add(Item.Id.ToString());
        if (Item.Tags.HasTagExact(Shovels)) ShovelIds.Add(Item.Id.ToString());
    }
    HoeIds.Sort();
    ShovelIds.Sort();
    TestEqual(TEXT("the hoes (tags.json #minecraft:hoes)"), FString::Join(HoeIds, TEXT(",")),
        FString(TEXT("copper_hoe,diamond_hoe,golden_hoe,iron_hoe,netherite_hoe,stone_hoe,wooden_hoe")));
    TestEqual(TEXT("the shovels (tags.json #minecraft:shovels)"), FString::Join(ShovelIds, TEXT(",")),
        FString(TEXT(
            "copper_shovel,diamond_shovel,golden_shovel,iron_shovel,netherite_shovel,stone_shovel,wooden_shovel")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsTillTest, "Unnamed.Foundation.Items.Till", TillTestFlags)
bool FFoundationItemsTillTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("DA_Items"), UFoundationItemAssets::LoadDefault())) return false;
    // The matrix: every hoe and shovel x 9 blocks x 6 faces x 4 blocks above x 3 modes, against the wiki's rows.
    const TCHAR* Targets[] = {TEXT("dirt"), TEXT("grass_block"), TEXT("dirt_path"), TEXT("coarse_dirt"),
        TEXT("rooted_dirt"), TEXT("mycelium"), TEXT("podzol"), TEXT("stone"), TEXT("farmland")};
    const TCHAR* Aboves[] = {nullptr, TEXT("stone"), TEXT("water"), TEXT("short_grass")};
    const EFoundationPlayerMode Modes[] = {
        EFoundationPlayerMode::Survival, EFoundationPlayerMode::Creative, EFoundationPlayerMode::Adventure};
    FTillLab Lab;
    const FGameplayTag Hoes = FoundationTilling::GetHoesTag(), Shovels = FoundationTilling::GetShovelsTag();
    TArray<TPair<FName, bool>> Tools;
    for (const FFoundationItemDefinition& Item : Lab.A.Rules.Items)
        if (Item.Tags.HasTagExact(Hoes) || Item.Tags.HasTagExact(Shovels))
            Tools.Emplace(Item.Id, Item.Tags.HasTagExact(Hoes));
    TestEqual(TEXT("7 hoes and 7 shovels"), Tools.Num(), 14);
    const FIntVector Above = Lab.Target + FIntVector(0, 0, 1);
    int32 Checked = 0, Changed = 0, Skipped = 0, Wrong = 0;
    for (const EFoundationPlayerMode Mode : Modes)
    {
        Lab.Vitals.Mode = Mode;
        for (const TPair<FName, bool>& Tool : Tools)
            for (const TCHAR* Target : Targets)
                for (int32 Face = 0; Face < 6; ++Face)
                    for (const TCHAR* AboveId : Aboves)
                    {
                        Lab.Look = EFoundationFace(Face);
                        Lab.Set(Lab.Target, Target);
                        Lab.Set(Above, AboveId);
                        Lab.Hold(Tool.Key);
                        Lab.NextStep();
                        const FFoundationHit Hit = Lab.A.Resolve(Lab.P().Facts);
                        if (Hit.Cell != Lab.Target)
                        {
                            // Looking down through the block above hits that block first.
                            ++Skipped;
                            TestTrue(TEXT("only the top look is ever blocked"),
                                Lab.Look == EFoundationFace::Up && AboveId != nullptr);
                            Lab.Set(Lab.Target, NAME_None);
                            Lab.Set(Above, NAME_None);
                            continue;
                        }
                        TestEqual(TEXT("the hit face is the look's"), Hit.Face, Lab.Look);
                        bool bAnyFace = false;
                        FName Want = TillOracle(Tool.Value, Target, bAnyFace);
                        if (!bAnyFace && (Lab.Look == EFoundationFace::Down || AboveId != nullptr)) Want = NAME_None;
                        if (Mode == EFoundationPlayerMode::Adventure) Want = NAME_None;
                        const int64 Serial = Lab.LastSerial();
                        const FFoundationResult Result = Lab.Use();
                        const FName Got = Lab.At(Lab.Target);
                        const int32 WantWear = !Want.IsNone() && Mode == EFoundationPlayerMode::Survival ? 1 : 0;
                        const EFoundationEventKind Kind =
                            Tool.Value ? EFoundationEventKind::Tilled : EFoundationEventKind::Flattened;
                        const bool bOk = Want.IsNone()
                            ? Got == FName(Target) && !Result.bTilled && !Result.bFlattened && Lab.Damage() == 0 &&
                                Lab.CountEvents(EFoundationEventKind::Tilled, Serial) +
                                        Lab.CountEvents(EFoundationEventKind::Flattened, Serial) ==
                                    0
                            : Got == Want && Result.IsCommitted() && Result.Cell == Lab.Target &&
                                (Tool.Value ? Result.bTilled && !Result.bFlattened
                                            : Result.bFlattened && !Result.bTilled) &&
                                Result.State == Lab.A.World.Get(Lab.Target).State && Lab.Damage() == WantWear &&
                                Result.bToolWorn == (WantWear > 0) && Lab.CountEvents(Kind, Serial) == 1;
                        // Only rooted dirt's hoe row drops something (one hanging roots); nothing else spawns.
                        const int32 WantEntities =
                            !Want.IsNone() && Tool.Value && FName(Target) == TEXT("rooted_dirt") ? 1 : 0;
                        ++Checked;
                        Changed += !Want.IsNone();
                        if ((!bOk || Lab.A.GetItemEntities().Num() != WantEntities) && ++Wrong <= 20)
                            AddError(FString::Printf(
                                TEXT("mode %d, %s on %s from %s with %s above: got %s, wear %d, entities %d"),
                                int32(Mode), *Tool.Key.ToString(), Target,
                                FoundationGrid::FaceName(EFoundationFace(Face)), AboveId ? AboveId : TEXT("air"),
                                *Got.ToString(), Lab.Damage(), Lab.A.GetItemEntities().Num()));
                        Lab.Set(Lab.Target, NAME_None);
                        Lab.Set(Above, NAME_None);
                        // The next case starts with none.
                        Lab.A.RemoveItemEntities();
                    }
    }
    AddInfo(FString::Printf(
        TEXT("%d cases checked, %d changed the block, %d top looks blocked"), Checked, Changed, Skipped));
    TestEqual(TEXT("mismatches against the wiki's rows"), Wrong, 0);
    // 3 x 14 x 9 x 6 x 4 = 9,072 cases.
    TestEqual(TEXT("every case ran or was a blocked top look"), Checked + Skipped, 9072);
    // Stone and short grass block the top look (water does not): 3 modes x 14 tools x 9 blocks x 2.
    TestEqual(TEXT("top looks blocked"), Skipped, 3 * 14 * 9 * 2);
    // Per changing mode: hoes 7 x (3 farmland blocks + coarse dirt) x 5 faces + rooted dirt 7 x (6 x 4 - 2 blocked
    // top looks) = 294; shovels 7 x 6 blocks x 5 faces = 210; survival and creative.
    TestEqual(TEXT("cases that changed the block"), Changed, 2 * (294 + 210));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsTillRulesTest, "Unnamed.Foundation.Items.TillRules", TillTestFlags)
bool FFoundationItemsTillRulesTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("DA_Items"), UFoundationItemAssets::LoadDefault())) return false;
    {
        // Rooted dirt and a hoe: dirt and one hanging roots item entity out of the clicked face.
        FTillLab Lab;
        Lab.Look = EFoundationFace::West;
        Lab.Set(Lab.Target, TEXT("rooted_dirt"));
        Lab.Hold(TEXT("iron_hoe"));
        Lab.NextStep();
        const FFoundationResult Result = Lab.Use();
        TestTrue(TEXT("rooted dirt tills"), Result.bTilled);
        TestEqual(TEXT("rooted dirt: dirt"), Lab.At(Lab.Target), FName(TEXT("dirt")));
        const TArray<FFoundationItemEntity>& Entities = Lab.A.GetItemEntities();
        if (TestEqual(TEXT("one item entity"), Entities.Num(), 1))
        {
            TestEqual(TEXT("hanging roots"), Lab.A.Rules.Items[Entities[0].Item].Id, FName(TEXT("hanging_roots")));
            TestEqual(TEXT("one of them"), Entities[0].Count, 1);
            TestTrue(TEXT("out of the west face"),
                Entities[0].LocalCm.X < FoundationGrid::CellBox(Lab.Target, Lab.A.World.OriginCm).Min.X);
        }
        // Item entities off: the block still tills, nothing spawns.
        Lab.A.RemoveItemEntities();
        Lab.A.Options.bItemEntities = false;
        Lab.Set(Lab.Target, TEXT("rooted_dirt"));
        Lab.NextStep();
        TestTrue(TEXT("no item entities: tills"), Lab.Use().bTilled);
        TestEqual(TEXT("no item entities: none spawned"), Lab.A.GetItemEntities().Num(), 0);
    }
    {
        // A wall torch on the dirt's west side pops when the shovel flattens it: a path supports no torch
        // (block_support.json), and the till settles its neighbours.
        FTillLab Lab;
        Lab.Look = EFoundationFace::North;
        const FIntVector Torch = Lab.Target + FIntVector(-1, 0, 0);
        Lab.Set(Lab.Target, TEXT("dirt"));
        Lab.A.World.Set(Torch,
            Lab.R().WithValue(
                Lab.A.World.FindBlock(TEXT("wall_torch"))->GetDefaultState(), TEXT("facing"), TEXT("west")));
        Lab.Hold(TEXT("iron_shovel"));
        Lab.NextStep();
        const int64 Serial = Lab.LastSerial();
        TestTrue(TEXT("the dirt flattens"), Lab.Use().bFlattened);
        TestEqual(TEXT("the wall torch popped"), Lab.At(Torch), FName(TEXT("air")));
        TestEqual(TEXT("one Popped"), Lab.CountEvents(EFoundationEventKind::Popped, Serial), 1);
    }
    {
        // Wear off: no wear; the last point of a hoe and of a shovel uses it and breaks it once.
        FTillLab Lab;
        Lab.A.Options.bToolWear = false;
        Lab.Set(Lab.Target, TEXT("dirt"));
        Lab.Hold(TEXT("wooden_hoe"));
        Lab.NextStep();
        TestTrue(TEXT("wear off tills"), Lab.Use().bTilled);
        TestEqual(TEXT("wear off: no wear"), Lab.Damage(), 0);
        Lab.A.Options.bToolWear = true;
        const int32 HoeMax = Lab.A.Rules.GetItem(Lab.P().Inventory.Hotbar[0].Item).MaxDurability;
        Lab.Hold(TEXT("wooden_hoe"), HoeMax - 1);
        Lab.Set(Lab.Target, TEXT("dirt"));
        Lab.NextStep();
        const FFoundationResult HoeResult = Lab.Use();
        TestTrue(TEXT("the hoe's last point tills and breaks"), HoeResult.bTilled && HoeResult.bToolBroke);
        TestTrue(TEXT("the hoe is gone"), Lab.P().Inventory.Hotbar[0].IsEmpty());
        Lab.Hold(TEXT("wooden_shovel"));
        const int32 Max = Lab.A.Rules.GetItem(Lab.P().Inventory.Hotbar[0].Item).MaxDurability;
        Lab.Hold(TEXT("wooden_shovel"), Max - 1);
        Lab.Set(Lab.Target, TEXT("grass_block"));
        Lab.NextStep();
        const FFoundationResult Result = Lab.Use();
        TestTrue(TEXT("the last point flattens and breaks"), Result.bFlattened && Result.bToolBroke);
        TestTrue(TEXT("the shovel is gone"), Lab.P().Inventory.Hotbar[0].IsEmpty());
        TestEqual(TEXT("two ToolBroke"), Lab.CountEvents(EFoundationEventKind::ToolBroke), 2);
        TestEqual(TEXT("a dirt path"), Lab.At(Lab.Target), FName(TEXT("dirt_path")));
    }
    {
        // Switches: foundation.Items.Use 0, foundation.Blocks.Farmland 0 and foundation.Blocks.Families 0 (farmland
        // hidden) leave the hoe inert.
        for (int32 Case = 0; Case < 3; ++Case)
        {
            FTillCVarScope Farmland(TEXT("foundation.Blocks.Farmland"), Case == 1 ? 0 : 1);
            FTillCVarScope Families(TEXT("foundation.Blocks.Families"), Case == 2 ? 0 : 1);
            FTillLab Lab(Case != 0);
            Lab.Set(Lab.Target, TEXT("dirt"));
            Lab.Hold(TEXT("iron_hoe"));
            Lab.NextStep();
            const FFoundationResult Result = Lab.Use();
            TestFalse(FString::Printf(TEXT("switch case %d: no till"), Case), Result.bTilled);
            TestEqual(
                FString::Printf(TEXT("switch case %d: dirt stays"), Case), Lab.At(Lab.Target), FName(TEXT("dirt")));
            TestEqual(FString::Printf(TEXT("switch case %d: no wear"), Case), Lab.Damage(), 0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsTillRevertTest, "Unnamed.Foundation.Items.TillRevert", TillTestFlags)
bool FFoundationItemsTillRevertTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("DA_Items"), UFoundationItemAssets::LoadDefault())) return false;
    // The player stands two cells west of D = (0,0,6) and clicks the west face of a stone at (1,0,6): the held block
    // goes to D, above the block under test at (0,0,5).
    const FIntVector Under(0, 0, 5), D(0, 0, 6), Side(1, 0, 6);
    auto Setup = [&](FTillLab& Lab, FName UnderId)
    {
        Lab.EyeAt = FVector(-150, 50, 650);
        Lab.AimAt = FoundationGrid::CellBox(Side, Lab.A.World.OriginCm).GetCenter() - FVector(50, 0, 0);
        Lab.Set(Side, TEXT("stone"));
        Lab.Set(Under, UnderId);
        Lab.Set(Under - FIntVector(0, 0, 1), TEXT("stone"));
        Lab.NextStep();
    };
    for (const TCHAR* UnderId : {TEXT("farmland"), TEXT("dirt_path")})
    {
        // A stone placed on it: dirt in the same command, no pop, no drop.
        FTillLab Lab;
        Setup(Lab, UnderId);
        Lab.Hold(TEXT("stone"));
        Lab.NextStep();
        const int64 Serial = Lab.LastSerial();
        const FFoundationResult Result = Lab.Use();
        TestTrue(FString::Printf(TEXT("%s: the stone placed above"), UnderId),
            Result.bPlaced && Result.Cell == D && Lab.At(D) == TEXT("stone"));
        TestEqual(FString::Printf(TEXT("%s: turned to dirt"), UnderId), Lab.At(Under), FName(TEXT("dirt")));
        TestEqual(
            FString::Printf(TEXT("%s: no Popped"), UnderId), Lab.CountEvents(EFoundationEventKind::Popped, Serial), 0);
        TestEqual(FString::Printf(TEXT("%s: no drops"), UnderId), Lab.A.GetItemEntities().Num(), 0);
    }
    {
        // A fence gate (in the "up" set) keeps farmland.
        FTillLab Lab;
        Setup(Lab, TEXT("farmland"));
        Lab.Hold(TEXT("oak_fence_gate"));
        Lab.NextStep();
        TestTrue(TEXT("the gate placed"), Lab.Use().bPlaced && Lab.At(D) == TEXT("oak_fence_gate"));
        TestEqual(TEXT("farmland under a gate stays"), Lab.At(Under), FName(TEXT("farmland")));
    }
    {
        // Farmland placed under a stone places dirt; with air above, farmland.
        FTillLab Lab;
        Setup(Lab, NAME_None);
        Lab.Set(Under, TEXT("stone"));
        Lab.Set(D + FIntVector(0, 0, 1), TEXT("stone"));
        Lab.Hold(TEXT("farmland"));
        Lab.NextStep();
        TestTrue(TEXT("farmland under stone places"), Lab.Use().bPlaced);
        TestEqual(TEXT("farmland under stone: dirt"), Lab.At(D), FName(TEXT("dirt")));
        Lab.Set(D, NAME_None);
        Lab.Set(D + FIntVector(0, 0, 1), NAME_None);
        Lab.Hold(TEXT("farmland"));
        Lab.NextStep();
        TestTrue(TEXT("farmland under air places"), Lab.Use().bPlaced);
        TestEqual(TEXT("farmland under air: farmland"), Lab.At(D), FName(TEXT("farmland")));
    }
    {
        // foundation.Blocks.Farmland 0: farmland under a placed stone stays farmland (as before B5).
        FTillCVarScope Farmland(TEXT("foundation.Blocks.Farmland"), 0);
        FTillLab Lab;
        Setup(Lab, TEXT("farmland"));
        Lab.Hold(TEXT("stone"));
        Lab.NextStep();
        TestTrue(TEXT("switch 0: the stone placed"), Lab.Use().bPlaced);
        TestEqual(TEXT("switch 0: farmland stays"), Lab.At(Under), FName(TEXT("farmland")));
    }
    return true;
}
#endif
