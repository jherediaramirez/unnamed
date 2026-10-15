#include "Misc/AutomationTest.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionView.h"
#include "HAL/IConsoleManager.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationSimulation.h"
#include "Foundation/FoundationTags.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Voxel/VoxelBlocks.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EFoundationHand Main = EFoundationHand::Main;
    constexpr EFoundationHand OffHand = EFoundationHand::Off;

    // The interaction mechanics run on a stand-in block with the retired Dummy Block's numbers (hardness 1.5,
    // pickaxe-mineable, no correct tool needed: hand 45 steps, iron pickaxe 8), so their arithmetic stays the
    // same; the imported 26.3 blocks have their own tests (FoundationBlocksTests.cpp).
    FFoundationBlockDefinition MakeTestBlock(const TCHAR* Id = TEXT("lab_block"))
    {
        FFoundationBlockDefinition Block;
        Block.Id = Id;
        Block.DisplayName = TEXT("Lab Block");
        Block.Hardness = 1.5f;
        Block.Tags = FGameplayTagContainer(FoundationTags::BlockMineablePickaxe);
        Block.DropItem = TEXT("lab_block");
        return Block;
    }

    // The imported registry plus the stand-in and any extra project blocks.
    TSharedRef<const FFoundationBlockRegistry> MakeRegistry(TArray<FFoundationBlockDefinition> Extra = {})
    {
        Extra.Insert(MakeTestBlock(), 0);
        return FFoundationBlockRegistry::Build(FFoundationBlockRegistry::GetDefault()->GetAssets(), Extra);
    }

    // A small lab: the project's rules with the stand-in block in slot 1, a 40x40 anchor floor at z 0 and one
    // registered player.
    struct FLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        int32 Player = 0;
        uint32 Sequence = 1;
        int64 StepIndex = 0;

        explicit FLab(bool bInfinite = true)
        {
            const UFoundationInteractionSettings& Settings = UFoundationInteractionSettings::Get();
            Settings.BuildRules(A.Rules);
            FFoundationItemDefinition Item;
            Item.Id = TEXT("lab_block");
            Item.DisplayName = TEXT("Lab Block");
            Item.Tags = FGameplayTagContainer(FoundationTags::ItemBlock);
            Item.MaxStack = 64;
            Item.PlacesBlock = TEXT("lab_block");
            Item.Icon = TEXT("block");
            A.Rules.Items.Add(Item);
            A.Rules.Loadout[0] = TEXT("lab_block");
            A.World.SetRegistry(MakeRegistry());
            A.PlantSurvival = VoxelBlocks::GetPlantSurvival(A.World.GetRegistry());
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
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }

        // Standing (or crouched) facts with the gameplay eye straight above the feet.
        FFoundationPlayerFacts& Stand(FVector Feet, FVector Aim, bool bGrounded = true, bool bCrouched = false)
        {
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = Feet;
            Facts.HeightCm = bCrouched ? 150 : 180;
            Facts.EyeCm = Feet + FVector(0, 0, bCrouched ? 127 : 162);
            Facts.Aim = Aim.GetSafeNormal();
            Facts.bGrounded = bGrounded;
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
            return P().Facts;
        }

        void Facts(TFunctionRef<void(FFoundationPlayerFacts&)> Edit)
        {
            FFoundationPlayerFacts Facts = P().Facts;
            Edit(Facts);
            A.SetFacts(Player, Facts);
        }

        void NextStep()
        {
            Facts([this](FFoundationPlayerFacts& F) { F.Step = ++StepIndex; });
            A.Step();
        }

        FFoundationCommand Make(EFoundationCommandType Type, EFoundationHand Hand = Main)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Type = Type;
            Command.Hand = Hand;
            Command.Item = P().Inventory.GetHand(Hand).GetRef();
            Command.Target = A.Resolve(P().Facts);
            FIntVector Destination;
            EFoundationReason Ignored;
            if (A.GetDestination(Command.Target, Destination, Ignored) && A.World.IsKnown(Destination))
            {
                Command.Destination = Destination;
                Command.DestinationRevision = A.World.Get(Destination).Revision;
            }
            return Command;
        }

        FFoundationResult Run(FFoundationCommand Command)
        {
            if (Command.Sequence == 0) Command.Sequence = Sequence++;
            return A.Execute(Player, Command, Vitals);
        }

        FFoundationResult Use(EFoundationHand Hand = Main) { return Run(Make(EFoundationCommandType::UseOn, Hand)); }

        // Replaces the blocks (registry) with the imported ones, the stand-in and these.
        void SetBlocks(TArray<FFoundationBlockDefinition> Extra)
        {
            A.World.SetRegistry(MakeRegistry(Extra));
            A.PlantSurvival = VoxelBlocks::GetPlantSurvival(A.World.GetRegistry());
        }

        void Put(FIntVector Cell, FName Id = TEXT("lab_block"))
        {
            A.World.Set(Cell, A.World.FindBlock(Id)->GetDefaultState());
        }

        bool Solid(FIntVector Cell) const { return !A.World.Get(Cell).State.IsAir(); }

        void Hold(int32 Slot) { P().Inventory.Select(Slot); }
    };

    const FVector DownVector(0, 0, -1);

    // Steps until a break, mining the targeted block through the command path (start then continues).
    int32 MineSteps(FLab& L, int32 Limit = 2000)
    {
        FFoundationResult R = L.Run(L.Make(EFoundationCommandType::MineStart));
        if (R.bBroken) return 0;
        for (int32 Step = 1; Step <= Limit; ++Step)
        {
            R = L.Run(L.Make(EFoundationCommandType::MineContinue));
            if (R.bBroken) return Step;
            if (!R.IsCommitted()) return -Step;
            L.NextStep();
        }
        UE_LOG(LogTemp, Warning, TEXT("MineSteps: no break in %d steps, progress %f"), Limit, L.P().Mining.Progress);
        return -1;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractionGridTest, "Unnamed.Foundation.Interaction.Grid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationInteractionGridTest::RunTest(const FString&)
{
    using namespace FoundationGrid;
    TestEqual(TEXT("zero"), CellAt(FVector(0), FVector(0)), FIntVector(0));
    TestEqual(TEXT("just below a boundary"), CellAt(FVector(99.999, 0, 0), FVector(0)), FIntVector(0));
    TestEqual(
        TEXT("a boundary belongs to the cell above it"), CellAt(FVector(100, 0, 0), FVector(0)), FIntVector(1, 0, 0));
    TestEqual(TEXT("negative floors"), CellAt(FVector(-.001, -100, -100.001), FVector(0)), FIntVector(-1, -1, -2));
    TestEqual(
        TEXT("moved origin (rebasing)"), CellAt(FVector(1050, -50, 0), FVector(1000, -100, -50)), FIntVector(0, 0, 0));
    TestEqual(TEXT("cell box min"), CellBox({-2, 3, -1}, FVector(10, 0, 0)).Min, FVector(-190, 300, -100));
    TestEqual(TEXT("section floor division"), SectionOf({-1, -16, -17}), FIntVector(-1, -1, -2));
    TestEqual(TEXT("section positive"), SectionOf({15, 16, 31}), FIntVector(0, 1, 1));
    const FIntVector Offsets[] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    for (int32 Face = 0; Face < 6; ++Face)
    {
        TestEqual(TEXT("face offset"), FaceOffset(EFoundationFace(Face)), Offsets[Face]);
        TestEqual(TEXT("face from axis"), FaceFromAxis(Face / 2, Face % 2 == 1), EFoundationFace(Face));
    }
    FFoundationSectionStorage Storage;
    Storage.Set({-1, -1, -1}, {{1}, 7});
    Storage.Set({16, 0, 0}, {{1}, 8});
    TestEqual(TEXT("two sections"), Storage.GetSectionCount(), 2);
    TestEqual(TEXT("solid count"), Storage.GetSolidCount(), 2);
    TestEqual(TEXT("revision kept"), Storage.Get({-1, -1, -1}).Revision, int64(7));
    int32 Visited = 0;
    Storage.ForEachSolid(
        {-5, -5, -5}, {20, 2, 2}, [&Visited](const FIntVector&, const FFoundationCell&) { ++Visited; });
    TestEqual(TEXT("range visit across sections"), Visited, 2);
    Storage.Set({-1, -1, -1}, {{0}, 9});
    TestEqual(TEXT("air keeps its revision"), Storage.Get({-1, -1, -1}).Revision, int64(9));
    TestEqual(TEXT("air is not solid"), Storage.GetSolidCount(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractionPlacePreviewTest, "Unnamed.Foundation.Interaction.PlacePreview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationInteractionPlacePreviewTest::RunTest(const FString& Parameters)
{
    // Vanilla draws no destination preview (Jesus, 2026-10-11): off unless foundation.Place.Preview is 1.
    TestEqual(TEXT("default mode"), FoundationPlace::DefaultPreviewMode, 0);
    const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Place.Preview"));
    if (TestNotNull(TEXT("foundation.Place.Preview exists"), Variable) &&
        (Variable->GetFlags() & ECVF_SetByMask) == ECVF_SetByConstructor)
        TestEqual(TEXT("the switch starts at the default"), Variable->GetInt(), 0);
    FFoundationPreview Placement;
    Placement.bPlacement = true;
    FFoundationPreview Other;
    Other.bInteraction = true;
    for (const bool bValid : {true, false})
    {
        Placement.bValid = bValid;
        TestFalse(TEXT("mode 0 draws no placement"), FoundationPlace::ShouldDrawPreview(0, Placement));
        TestTrue(TEXT("mode 1 draws a placement, valid or not"), FoundationPlace::ShouldDrawPreview(1, Placement));
    }
    TestFalse(TEXT("mode 1 draws no box without a placement"), FoundationPlace::ShouldDrawPreview(1, Other));
    TestFalse(TEXT("mode 0 draws no box without a placement"), FoundationPlace::ShouldDrawPreview(0, Other));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractionTargetingTest, "Unnamed.Foundation.Interaction.Targeting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationInteractionTargetingTest::RunTest(const FString&)
{
    FFoundationBlockWorld World;
    World.SetRegistry(MakeRegistry());
    World.BoundsMin = FIntVector(-20);
    World.BoundsMax = FIntVector(19);
    const FFoundationBlockState Block = World.FindBlock(TEXT("lab_block"))->GetDefaultState();
    auto Resolve = [&World](FVector Eye, FVector Direction, TArray<FFoundationStatic> Statics = {},
                       TArray<FFoundationEntityShape> Entities = {})
    {
        FFoundationRay Ray;
        Ray.Eye = Eye;
        Ray.Direction = Direction;
        return FoundationTargeting::Resolve(Ray, World, Statics, Entities);
    };
    // All six faces of a negative-coordinate cell, each giving the adjacent destination.
    const FIntVector Cell(-3, -2, -1);
    World.Set(Cell, Block);
    const FVector Center = FoundationGrid::CellBox(Cell, World.OriginCm).GetCenter();
    for (int32 Face = 0; Face < 6; ++Face)
    {
        const FVector Normal = FoundationGrid::FaceNormal(EFoundationFace(Face));
        const FFoundationHit Hit = Resolve(Center + Normal * 250, -Normal);
        TestTrue(TEXT("face hit"),
            Hit.Kind == EFoundationHitKind::Block && Hit.Cell == Cell && Hit.Face == EFoundationFace(Face) &&
                FMath::IsNearlyEqual(Hit.DistanceCm, 200., 1e-9));
        TestEqual(TEXT("negative destination"), Hit.Cell + FoundationGrid::FaceOffset(Hit.Face),
            Cell + FoundationGrid::FaceOffset(EFoundationFace(Face)));
    }
    World.Set(Cell, {});
    // Strict reach: 4.5 blocks exactly is out, a hair less is in.
    World.Set({5, 0, 0}, Block);
    TestFalse(TEXT("block at exactly 450 cm"), Resolve({50, 50, 50}, {1, 0, 0}).bInRange);
    TestTrue(TEXT("block at 449.99 cm"), Resolve({50.01, 50, 50}, {1, 0, 0}).bInRange);
    TestTrue(TEXT("nothing is picked beyond the 4.5-block pick"),
        Resolve({40, 50, 50}, {1, 0, 0}).Kind == EFoundationHitKind::None);
    // Entities: 3 blocks strict, and they win only when strictly nearer.
    FFoundationEntityShape Entity;
    Entity.Id = 7;
    Entity.Box = FBox(FVector(350, 20, 0), FVector(410, 80, 180));
    FFoundationHit Hit = Resolve({50, 50, 50}, {1, 0, 0}, {}, {Entity});
    TestTrue(TEXT("entity nearer than the block"), Hit.Kind == EFoundationHitKind::Entity && Hit.EntityId == 7);
    TestFalse(TEXT("entity at exactly 3 blocks is out of reach, kind kept"), Hit.bInRange);
    TestTrue(TEXT("entity at 2.9999 blocks"), Resolve({50.01, 50, 50}, {1, 0, 0}, {}, {Entity}).bInRange);
    Entity.Box = FBox(FVector(500, 20, 0), FVector(560, 80, 180));
    TestTrue(TEXT("exact tie: the block wins"),
        Resolve({50, 50, 50}, {1, 0, 0}, {}, {Entity}).Kind == EFoundationHitKind::Block);
    // Occlusion by lab geometry; anchors give the cell behind the face.
    FFoundationStatic Wall;
    Wall.Box = FBox(FVector(300, -100, 0), FVector(310, 200, 300));
    Hit = Resolve({50, 50, 50}, {1, 0, 0}, {Wall});
    TestTrue(TEXT("nearest static occludes the block"), Hit.Kind == EFoundationHitKind::Static && Hit.StaticIndex == 0);
    FFoundationStatic Floor;
    Floor.Box = FBox(FVector(-1000, -1000, -100), FVector(1000, 1000, 0));
    Floor.bAnchor = true;
    Hit = Resolve({-250, -350, 162}, {0, 0, -1}, {Floor});
    TestTrue(TEXT("floor anchor cell below a negative point"),
        Hit.Kind == EFoundationHitKind::Static && Hit.Cell == FIntVector(-3, -4, -1) &&
            Hit.Face == EFoundationFace::Up);
    // Edge crossing: X steps first; a box entered exactly at its edge reports the X face.
    World.Set({5, 0, 0}, {});
    World.Set({1, 1, 0}, Block);
    Hit = Resolve({50, 50, 50}, {1, 1, 0});
    TestTrue(TEXT("diagonal through an edge"),
        Hit.Kind == EFoundationHitKind::Block && Hit.Cell == FIntVector(1, 1, 0) && Hit.Face == EFoundationFace::West);
    World.Set({1, 0, 0}, Block);
    Hit = Resolve({50, 50, 50}, {1, 1, 0});
    TestTrue(TEXT("X neighbor is visited before Y"), Hit.Cell == FIntVector(1, 0, 0));
    // Starting inside a block does not select it; a miss selects nothing.
    TestTrue(TEXT("eye inside a block skips it"), Resolve({150, 50, 50}, {0, 1, 0}).Cell == FIntVector(1, 1, 0));
    TestTrue(TEXT("miss"), Resolve({50, 50, 50}, {0, 0, 1}).Kind == EFoundationHitKind::None);
    // Rebasing moves the grid with the world: the same relative ray hits the same cell.
    World.OriginCm = FVector(-12345, 6789, 100);
    Hit = Resolve(FVector(50, 50, 50) + World.OriginCm, {1, 0, 0});
    TestTrue(TEXT("rebased grid"), Hit.Cell == FIntVector(1, 0, 0) && FMath::IsNearlyEqual(Hit.DistanceCm, 50., 1e-6));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractionPlacementTest, "Unnamed.Foundation.Interaction.Placement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationInteractionPlacementTest::RunTest(const FString&)
{
    FLab L;
    // Standing two blocks back, looking at the floor in front: the cell above the clicked one.
    L.Stand({-150, 50, 0}, FVector(1, 0, 0) * 150 + FVector(0, 0, -162 + 1));
    FFoundationResult R = L.Use();
    TestTrue(TEXT("placed above the anchor face"), R.bPlaced && R.Cell == FIntVector(0, 0, 0) && L.Solid({0, 0, 0}));
    TestEqual(TEXT("infinite supply keeps the count"), L.P().Inventory.Hotbar[0].Count, 64);
    const int64 Revision = L.A.World.Revision;
    // A retransmission returns the stored result and applies nothing.
    FFoundationCommand Again = L.Make(EFoundationCommandType::UseOn);
    Again.Sequence = R.Sequence;
    const FFoundationResult Repeat = L.Run(Again);
    TestTrue(TEXT("duplicate returns the original"), Repeat.bDuplicate && Repeat.bPlaced && Repeat.Cell == R.Cell);
    TestEqual(TEXT("duplicate applies nothing"), L.A.World.Revision, Revision);
    // Same step: one placement only.
    L.Stand({-150, 50, 0}, FVector(140, 0, -61) - FVector(0, 0, 0));
    TestTrue(TEXT("the next face"), L.A.Resolve(L.P().Facts).Cell == FIntVector(0, 0, 0));
    TestTrue(TEXT("first placement this step"), L.Use().bPlaced);
    R = L.Use();
    TestTrue(TEXT("second placement in one step"), R.Reason == EFoundationReason::StepLimit);
    // Claims are only claims.
    L.NextStep();
    FFoundationCommand Claim = L.Make(EFoundationCommandType::UseOn);
    Claim.Target.Cell += FIntVector(1, 0, 0);
    TestEqual(TEXT("target claim mismatch"), L.Run(Claim).Reason, EFoundationReason::TargetMismatch);
    Claim = L.Make(EFoundationCommandType::UseOn);
    Claim.Target.Revision -= 1;
    TestEqual(TEXT("stale anchor revision"), L.Run(Claim).Reason, EFoundationReason::StaleRevision);
    Claim = L.Make(EFoundationCommandType::UseOn);
    Claim.Destination += FIntVector(0, 0, 1);
    TestEqual(TEXT("destination claim mismatch"), L.Run(Claim).Reason, EFoundationReason::TargetMismatch);
    Claim = L.Make(EFoundationCommandType::UseOn);
    Claim.Item.Revision += 1;
    TestEqual(TEXT("item revision claim"), L.Run(Claim).Reason, EFoundationReason::ItemChanged);
    // Self overlap: contact is fine, penetration is not.
    FLab S;
    S.Stand({50, 50, 100}, DownVector);
    S.Put({0, 0, -0});
    S.A.World.Set({0, 0, 0}, {});
    S.Stand({250, 50, 0}, FVector(-150, 0, -161));
    TestTrue(
        TEXT("aim at the floor cell under a raised player"), S.A.Resolve(S.P().Facts).Cell == FIntVector(0, 0, -1));
    S.A.Statics.Add({FBox(FVector(-30 + 50, -30 + 50, 100), FVector(30 + 50, 30 + 50, 101)), TEXT("probe"), false});
    S.A.Statics.Pop();
    FLab C;
    // The other player is at (50,50) with feet exactly on the destination's top: contact, not obstruction.
    const int32 Other = C.A.AddPlayer();
    FFoundationPlayerFacts OtherFacts;
    OtherFacts.FeetCm = FVector(50, 50, 100);
    C.A.SetFacts(Other, OtherFacts);
    C.Stand({250, 50, 0}, FVector(-200, 0, -162 + 1));
    R = C.Use();
    TestTrue(TEXT("feet touching the new top is contact"), R.bPlaced && R.Cell == FIntVector(0, 0, 0));
    OtherFacts.FeetCm.Z = 99.999;
    C.A.SetFacts(Other, OtherFacts);
    C.NextStep();
    C.Stand({250, 50, 0}, FVector(-200, 0, -162 + 1) + FVector(0, 100, 0));
    C.Facts([](FFoundationPlayerFacts& F) {});
    FIntVector Destination;
    FFoundationHit Synthetic = C.A.Resolve(C.P().Facts);
    Synthetic.Cell = FIntVector(0, 0, -1);
    Synthetic.Face = EFoundationFace::Up;
    C.A.World.Set({0, 0, 0}, {});
    TestEqual(TEXT("another body penetrating by 0.001 cm"), C.A.CheckPlacement(C.P(), Main, Synthetic, Destination),
        EFoundationReason::ObstructedByEntity);
    // The placing player's own body.
    FLab Self;
    Self.Stand({50, 50, 0}, DownVector);
    R = Self.Use();
    TestEqual(TEXT("own body blocks the cell it stands in"), R.Reason, EFoundationReason::ObstructedBySelf);
    Self.Stand({50, 50, 100}, DownVector);
    TestEqual(TEXT("own feet exactly on the top: allowed"), Self.Use().bPlaced, true);
    // Lab geometry, targets and bounds.
    FLab G;
    G.A.Statics.Add({FBox(FVector(130, 0, 0), FVector(160, 100, 50)), TEXT("post"), false});
    G.Stand({-150, 50, 0}, FVector(300, 0, -161));
    TestEqual(TEXT("non-anchor lab geometry seeds nothing"), G.Use().Reason, EFoundationReason::NotAnchor);
    G.Stand({-150, 50, 0}, FVector(330, 0, -161));
    R = G.Use();
    TestTrue(TEXT("partial lab geometry obstructs"),
        R.Reason == EFoundationReason::ObstructedByStatic || R.bPlaced == false);
    FFoundationTrainingTarget Target;
    Target.Id = 1;
    Target.Box = FBox(FVector(420, 20, 0), FVector(480, 80, 180));
    G.A.Targets.Add(Target);
    G.A.Statics.Empty();
    G.A.Statics.Add({FBox(FVector(-2000, -2000, -100), FVector(2000, 2000, 0)), TEXT("floor"), true});
    G.Stand({150, 50, 0}, FVector(300, 0, -161));
    G.Stand({50, 50, 0}, FVector(400, 0, -100));
    G.A.Targets[0].Box = FBox(FVector(400, 20, 0), FVector(460, 80, 180));
    FFoundationHit FloorHit;
    FloorHit.Kind = EFoundationHitKind::Static;
    FloorHit.StaticIndex = 0;
    FloorHit.Cell = FIntVector(4, 0, -1);
    FloorHit.Face = EFoundationFace::Up;
    FloorHit.bInRange = true;
    TestEqual(TEXT("training target obstructs"), G.A.CheckPlacement(G.P(), Main, FloorHit, Destination),
        EFoundationReason::ObstructedByEntity);
    FloorHit.Cell = FIntVector(19, 0, 15);
    TestEqual(TEXT("outside build bounds"), G.A.CheckPlacement(G.P(), Main, FloorHit, Destination),
        EFoundationReason::OutsideBounds);
    // Replaceable blocks are their own destination.
    FLab Rep;
    FFoundationBlockDefinition Grass;
    Grass.Id = TEXT("lab_grass");
    Grass.bReplaceable = true;
    Rep.SetBlocks({Grass});
    Rep.Put({0, 0, 0}, TEXT("lab_grass"));
    Rep.Stand({-150, 50, 0}, FVector(150, 0, -112));
    TestTrue(TEXT("aim at the replaceable block"), Rep.A.Resolve(Rep.P().Facts).Cell == FIntVector(0, 0, 0));
    R = Rep.Use();
    TestTrue(TEXT("replaced in place"),
        R.bPlaced && R.Cell == FIntVector(0, 0, 0) &&
            Rep.A.World.GetBlock(Rep.A.World.Get({0, 0, 0}).State).Id == TEXT("lab_block"));
    // Finite supply: the identical path consumes; the last block empties the slot.
    FLab F(false);
    F.P().Inventory.Hotbar[0].Count = 2;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        F.Stand({-150 - Index * 100., 50, 0}, FVector(150, 0, -161));
        TestTrue(TEXT("finite placement"), F.Use().bItemConsumed);
    }
    TestTrue(TEXT("empty slot loses its identity"),
        F.P().Inventory.Hotbar[0].IsEmpty() && F.P().Inventory.Hotbar[0].InstanceId == 0);
    F.Stand({-450, 50, 0}, FVector(150, 0, -161));
    TestEqual(TEXT("empty hands pass"), F.Use().Outcome, EFoundationOutcome::Pass);
    // Offhand placement after the main hand passes; an explicit failure never reaches the offhand.
    FLab O;
    O.P().Inventory.SwapHands();
    O.Hold(2);
    O.Stand({-150, 50, 0}, FVector(150, 0, -161));
    TestEqual(TEXT("sword passes"), O.Use(Main).Outcome, EFoundationOutcome::Pass);
    TestTrue(TEXT("offhand places"), O.Use(OffHand).bPlaced);
    // Interactions win, crouch with an item bypasses them, and a reset invalidates the session.
    FLab I;
    I.A.Statics.Add({FBox(FVector(100, 0, 0), FVector(200, 100, 100)), TEXT("reset"), true, TEXT("lab.reset")});
    I.Stand({-50, 50, 0}, FVector(1, 0, -.5));
    const int32 Session = I.A.GetSessionId();
    FFoundationCommand Old = I.Make(EFoundationCommandType::UseOn);
    R = I.Use();
    TestTrue(TEXT("interaction consumes use"), R.bInteracted && R.bReset && !R.bPlaced);
    TestNotEqual(TEXT("reset starts a new session"), I.A.GetSessionId(), Session);
    Old.Sequence = 99;
    TestEqual(TEXT("old session commands are stale"), I.Run(Old).Reason, EFoundationReason::StaleSession);
    I.Stand({-50, 50, 0}, FVector(1, 0, -.5));
    I.Facts([](FFoundationPlayerFacts& Facts) { Facts.bSecondaryUse = true; });
    R = I.Use();
    TestTrue(TEXT("crouch with an item places instead"), R.bPlaced && R.Cell == FIntVector(0, 0, 0));
    // A bounded result cache: evicted sequences are refused rather than replayed.
    FLab E;
    E.A.Rules.ResultCacheSize = 4;
    for (int32 Index = 0; Index < 6; ++Index)
        E.Run(E.Make(EFoundationCommandType::SwapHands));
    FFoundationCommand Evicted = E.Make(EFoundationCommandType::SwapHands);
    Evicted.Sequence = 1;
    TestEqual(TEXT("evicted sequence"), E.Run(Evicted).Reason, EFoundationReason::ExpiredSequence);
    TestEqual(TEXT("cache bounded"), E.P().Results.Num(), 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractionMiningTest, "Unnamed.Foundation.Interaction.Mining",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationInteractionMiningTest::RunTest(const FString&)
{
    // Nominal grounded steps: hand 45, iron pickaxe 8 (excluding the post-break delay).
    for (const auto& [Slot, Grounded, Water, Expected] : TArray<TTuple<int32, bool, bool, int32>>{{3, true, false, 45},
             {1, true, false, 8}, {3, false, false, 225}, {1, false, false, 38}, {3, true, true, 225},
             {3, false, true, 1125}, {2, true, false, 45}, {0, true, false, 45}})
    {
        FLab L;
        L.Hold(Slot);
        L.Put({0, 0, 0});
        L.Stand({-150, 50, 0}, FVector(150, 0, -112), Grounded);
        L.Facts([Water](FFoundationPlayerFacts& F) { F.bEyesInWater = Water; });
        TestEqual(FString::Printf(TEXT("mining steps slot %d grounded %d water %d"), Slot + 1, Grounded, Water),
            MineSteps(L), Expected);
        TestFalse(TEXT("broken"), L.Solid({0, 0, 0}));
    }
    // Face changes keep progress; another cell, a new revision or another item restarts.
    FLab L;
    L.Hold(3);
    L.Put({0, 0, 0});
    L.Put({0, 1, 0});
    L.Stand({-150, 50, 0}, FVector(150, 0, -112));
    L.Run(L.Make(EFoundationCommandType::MineStart));
    for (int32 Step = 0; Step < 10; ++Step)
    {
        L.Run(L.Make(EFoundationCommandType::MineContinue));
        L.NextStep();
    }
    const double Ten = L.P().Mining.Progress;
    TestTrue(TEXT("ten steps of hand progress"), FMath::IsNearlyEqual(Ten, 10. / 45., 1e-9));
    L.Stand({50, 50, 230}, DownVector);
    TestTrue(TEXT("same cell from above"), L.A.Resolve(L.P().Facts).Face == EFoundationFace::Up);
    FFoundationResult R = L.Run(L.Make(EFoundationCommandType::MineContinue));
    TestTrue(TEXT("face change keeps progress"), R.IsCommitted() && R.Progress > Ten);
    TestEqual(TEXT("a second continue in one step"), L.Run(L.Make(EFoundationCommandType::MineContinue)).Reason,
        EFoundationReason::StepLimit);
    L.Hold(1);
    L.NextStep();
    R = L.Run(L.Make(EFoundationCommandType::MineContinue));
    TestTrue(TEXT("another stack cancels"), R.Outcome == EFoundationOutcome::Canceled && !L.P().Mining.bActive);
    L.Run(L.Make(EFoundationCommandType::MineStart));
    L.NextStep();
    L.A.World.Set({0, 0, 0}, L.A.World.FindBlock(TEXT("lab_block"))->GetDefaultState());
    R = L.Run(L.Make(EFoundationCommandType::MineContinue));
    TestTrue(TEXT("a replaced block cancels"), R.Outcome == EFoundationOutcome::Canceled);
    L.Run(L.Make(EFoundationCommandType::MineStart));
    L.Vitals.AttackTicker = 9;
    R = L.Run(L.Make(EFoundationCommandType::MineAbort));
    TestTrue(TEXT("abort restarts attack recovery"),
        R.Outcome == EFoundationOutcome::Canceled && R.bChargeReset && L.Vitals.AttackTicker == 0);
    // Instant, unbreakable and immutable.
    FLab H;
    FFoundationBlockDefinition Soft = MakeTestBlock(TEXT("lab_soft")), Hard = MakeTestBlock(TEXT("lab_bedrock")),
                               Strict = MakeTestBlock(TEXT("lab_stone"));
    Soft.Hardness = 0;
    Hard.Hardness = -1;
    Strict.bRequiresCorrectToolForDrops = true;
    const TArray<FFoundationBlockDefinition> Blocks = {Soft, Hard, Strict};
    H.SetBlocks(Blocks);
    H.Put({0, 0, 0}, TEXT("lab_soft"));
    H.Stand({-150, 50, 0}, FVector(150, 0, -112));
    R = H.Run(H.Make(EFoundationCommandType::MineStart));
    TestTrue(TEXT("hardness zero breaks at once"), R.bBroken && R.bInstant && !H.P().Mining.bActive);
    H.Put({0, 0, 0}, TEXT("lab_bedrock"));
    H.NextStep();
    TestEqual(TEXT("negative hardness never breaks"), H.Run(H.Make(EFoundationCommandType::MineStart)).Reason,
        EFoundationReason::Unbreakable);
    H.Stand({-150, 50, 0}, FVector(100, 0, -161));
    TestEqual(TEXT("lab geometry is immutable"), H.Run(H.Make(EFoundationCommandType::MineStart)).Reason,
        EFoundationReason::ImmutableGeometry);
    // Speed and drop eligibility are separate: the hand mines a correct-tool block at divisor 100.
    for (const auto& [Slot, Expected, bDrop] : TArray<TTuple<int32, int32, bool>>{{3, 150, false}, {1, 8, true}})
    {
        FLab D(false);
        D.SetBlocks(Blocks);
        D.Put({0, 0, 0}, TEXT("lab_stone"));
        D.Hold(Slot);
        D.P().Inventory.Hotbar[0].Count = 10;
        D.Stand({-150, 50, 0}, FVector(150, 0, -112));
        const int32 Before = D.P().Inventory.Hotbar[0].Count;
        TestEqual(TEXT("correct-tool block steps"), MineSteps(D), Expected);
        TestEqual(TEXT("drop only with the correct tool"), D.P().Inventory.Hotbar[0].Count - Before, bDrop ? 1 : 0);
    }
    // Finite fixture: return, wear and the final durability point, each once.
    FLab F(false);
    F.Hold(1);
    F.P().Inventory.Hotbar[0].Count = 10;
    F.Put({0, 0, 0});
    F.Stand({-150, 50, 0}, FVector(150, 0, -112));
    TestEqual(TEXT("finite pickaxe"), MineSteps(F), 8);
    TestEqual(TEXT("item returned once"), F.P().Inventory.Hotbar[0].Count, 11);
    TestEqual(TEXT("pickaxe wear 1 per block"), F.P().Inventory.Hotbar[1].Damage, 1);
    F.P().Inventory.Hotbar[1].Damage = 249;
    F.Put({0, 0, 0});
    F.NextStep();
    MineSteps(F);
    TestTrue(TEXT("the last point breaks the pickaxe"), F.P().Inventory.Hotbar[1].IsEmpty());
    TestEqual(TEXT("and still returns the block"), F.P().Inventory.Hotbar[0].Count, 12);
    FLab W(false);
    W.Hold(2);
    W.Put({0, 0, 0});
    W.Stand({-150, 50, 0}, FVector(150, 0, -112));
    MineSteps(W);
    TestEqual(TEXT("sword wears 2 per block"), W.P().Inventory.Hotbar[2].Damage, 2);
    // No room for the return: refused, block kept, nothing spent.
    FLab Full(false);
    Full.Hold(1);
    for (int32 Slot = 3; Slot < 9; ++Slot)
        Full.P().Inventory.SetSlot(Slot, Full.P().Inventory.MakeStack(1, 1));
    Full.P().Inventory.Hotbar[0].Count = 64;
    Full.P().Inventory.Offhand = Full.P().Inventory.MakeStack(2, 1);
    Full.Put({0, 0, 0});
    Full.Stand({-150, 50, 0}, FVector(150, 0, -112));
    const int32 Steps = MineSteps(Full);
    TestTrue(TEXT("inventory full refuses the break"),
        Steps < 0 && Full.Solid({0, 0, 0}) && Full.P().Inventory.Hotbar[1].Damage == 0);
    TestEqual(TEXT("and says why"), Full.P().Results[Full.Sequence - 1].Reason, EFoundationReason::InventoryFull);
    // Infinite supply suppresses wear through the fixture flag, not another algorithm.
    FLab Inf;
    Inf.Hold(1);
    Inf.Put({0, 0, 0});
    Inf.Stand({-150, 50, 0}, FVector(150, 0, -112));
    MineSteps(Inf);
    TestEqual(TEXT("no wear in the infinite lab"), Inf.P().Inventory.Hotbar[1].Damage, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractionCombatTest, "Unnamed.Foundation.Interaction.Combat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationInteractionCombatTest::RunTest(const FString&)
{
    auto MakeLab = [](bool bInfinite = true)
    {
        auto L = MakeUnique<FLab>(bInfinite);
        FFoundationTrainingTarget Target;
        Target.Id = 1;
        Target.Box = FBox(FVector(170, 20, 0), FVector(230, 80, 180));
        L->A.Targets.Add(Target);
        L->A.Reset();
        L->Stand({0, 50, 0}, FVector(1, 0, 0));
        return L;
    };
    auto Attack = [](FLab& L, int32 Ticker)
    {
        L.Vitals.AttackTicker = Ticker;
        return L.Run(L.Make(EFoundationCommandType::Attack));
    };
    // Sword 6 at 1.6/s (12.5-tick recharge); the attack samples half a tick ahead.
    auto L = MakeLab();
    L->Hold(2);
    L->Vitals.AttackSpeed = 1.6f;
    FFoundationResult R = Attack(*L, 13);
    TestTrue(TEXT("full sword hit"), R.IsCommitted() && R.DamageApplied == 6 && R.Strength == 1);
    TestEqual(TEXT("target health"), L->A.Targets[0].Health.Health, 14.f);
    TestEqual(TEXT("issued attack restarts recovery"), L->Vitals.AttackTicker, 0);
    L->NextStep();
    R = Attack(*L, 13);
    TestTrue(TEXT("weaker or equal inside the protected window is ignored"),
        R.IsCommitted() && R.Reason == EFoundationReason::TargetCooldown && R.DamageApplied == 0 && R.bChargeReset);
    TestEqual(TEXT("6 then 6 leaves 6"), L->A.Targets[0].DamageTaken, 6.f);
    // 6 then 9 leaves 9: only the stronger difference.
    L = MakeLab();
    L->Hold(2);
    L->Vitals.AttackSpeed = 1.6f;
    Attack(*L, 13);
    L->Facts(
        [](FFoundationPlayerFacts& F)
        {
            F.bGrounded = false;
            F.FallDistanceCm = 40;
        });
    R = Attack(*L, 13);
    TestTrue(TEXT("falling critical 9 inside the window adds 3"), R.bCritical && R.Damage == 9 && R.DamageApplied == 3);
    TestEqual(TEXT("total 9"), L->A.Targets[0].DamageTaken, 9.f);
    // 6 then 4: nothing. After ten ticks a hit lands in full again.
    L = MakeLab();
    L->Hold(1);
    L->Vitals.AttackSpeed = 1.2f;
    TestEqual(TEXT("pickaxe full hit"), Attack(*L, 17).DamageApplied, 4.f);
    L->Hold(2);
    L->Vitals.AttackSpeed = 1.6f;
    for (int32 Tick = 0; Tick < 9; ++Tick)
        L->NextStep();
    TestEqual(TEXT("a stronger hit after nine ticks adds the difference"), Attack(*L, 13).DamageApplied, 2.f);
    L = MakeLab();
    L->Hold(1);
    L->Vitals.AttackSpeed = 1.2f;
    Attack(*L, 17);
    for (int32 Tick = 0; Tick < 10; ++Tick)
        L->NextStep();
    TestEqual(TEXT("at ten remaining a hit applies fully"), Attack(*L, 17).DamageApplied, 4.f);
    // Charge curve: hand (5 ticks) at ticker 2 samples 0.5: 1 x (0.2 + 0.8 x 0.25) = 0.4.
    L = MakeLab();
    L->Hold(3);
    L->Vitals.AttackSpeed = 4;
    R = Attack(*L, 2);
    TestTrue(TEXT("half charge"), FMath::IsNearlyEqual(R.Strength, .5f) && FMath::IsNearlyEqual(R.Damage, .4f, 1e-6f));
    // Critical conditions, each at full charge.
    struct FCase
    {
        const TCHAR* Name;
        bool bGrounded, bSprinting, bWater;
        double Fall;
        int32 Ticker;
        bool bCritical;
    };
    const FCase Cases[] = {{TEXT("falling"), false, false, false, 30, 13, true},
        {TEXT("rising (no fall distance)"), false, false, false, 0, 13, false},
        {TEXT("grounded"), true, false, false, 30, 13, false}, {TEXT("sprinting"), false, true, false, 30, 13, false},
        {TEXT("in water"), false, false, true, 30, 13, false},
        {TEXT("charge 0.92 > 0.9"), false, false, false, 30, 11, true},
        {TEXT("charge 0.84"), false, false, false, 30, 10, false}};
    for (const FCase& Case : Cases)
    {
        L = MakeLab();
        L->Hold(2);
        L->Vitals.AttackSpeed = 1.6f;
        L->Facts(
            [&Case](FFoundationPlayerFacts& F)
            {
                F.bGrounded = Case.bGrounded;
                F.bSprinting = Case.bSprinting;
                F.bBodyInWater = Case.bWater;
                F.FallDistanceCm = Case.Fall;
            });
        R = Attack(*L, Case.Ticker);
        TestEqual(FString(TEXT("critical: ")) + Case.Name, R.bCritical, Case.bCritical);
    }
    // Whiff, walls, reach and death.
    L = MakeLab();
    L->Stand({0, 50, 0}, FVector(-1, 0, 0));
    L->Vitals.AttackTicker = 7;
    R = L->Run(L->Make(EFoundationCommandType::Whiff));
    TestTrue(TEXT("a whiff restarts recovery"), R.IsCommitted() && R.bChargeReset && L->Vitals.AttackTicker == 0);
    L->Stand({0, 50, 0}, FVector(1, 0, 0));
    TestEqual(TEXT("whiff with a target in reach is a mismatch"), L->Run(L->Make(EFoundationCommandType::Whiff)).Reason,
        EFoundationReason::TargetMismatch);
    FFoundationCommand Claim = L->Make(EFoundationCommandType::Attack);
    L->A.Statics.Add({FBox(FVector(100, -100, 0), FVector(110, 200, 300)), TEXT("wall"), false});
    TestEqual(TEXT("a nearer wall blocks the attack"), L->Run(Claim).Reason, EFoundationReason::TargetMismatch);
    L->A.Statics.Pop();
    L->Stand({-140, 50, 0}, FVector(1, 0, 0));
    TestEqual(TEXT("entity reach 3 blocks"), L->Run(L->Make(EFoundationCommandType::Attack)).Reason,
        EFoundationReason::OutOfRange);
    L->Stand({0, 50, 0}, FVector(1, 0, 0));
    L->Hold(2);
    L->Vitals.AttackSpeed = 1.6f;
    for (int32 Hit = 0; Hit < 4; ++Hit)
    {
        R = Attack(*L, 13);
        for (int32 Tick = 0; Tick < 20; ++Tick)
            L->NextStep();
    }
    TestTrue(TEXT("target goes down at zero"), L->A.Targets[0].Health.IsDead());
    TestEqual(TEXT("a downed target is not attackable"), Attack(*L, 13).Reason, EFoundationReason::TargetDead);
    for (int32 Tick = 0; Tick < 100; ++Tick)
        L->NextStep();
    TestTrue(TEXT("no automatic respawn"), L->A.Targets[0].Health.IsDead());
    L->A.Reset();
    TestTrue(
        TEXT("explicit reset revives it"), !L->A.Targets[0].Health.IsDead() && L->A.Targets[0].Health.Health == 20);
    // Finite fixture: weapons wear per accepted hit only.
    auto Wear = MakeLab(false);
    Wear->Hold(2);
    Wear->Vitals.AttackSpeed = 1.6f;
    Attack(*Wear, 13);
    Attack(*Wear, 13);
    TestEqual(TEXT("sword 1 per accepted hit, none when ignored"), Wear->P().Inventory.Hotbar[2].Damage, 1);
    Wear->Hold(1);
    for (int32 Tick = 0; Tick < 20; ++Tick)
        Wear->NextStep();
    Attack(*Wear, 17);
    TestEqual(TEXT("pickaxe 2 per accepted hit"), Wear->P().Inventory.Hotbar[1].Damage, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractionStateTest, "Unnamed.Foundation.Interaction.StateMachine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationInteractionStateTest::RunTest(const FString&)
{
    const UFoundationInteractionSettings& Settings = UFoundationInteractionSettings::Get();
    auto Make = [&Settings](FLab& L, FName Preset)
    {
        FFoundationInteraction I;
        I.PlayerId = L.Player;
        I.SetPreset(*Settings.FindPreset(Preset), .05);
        I.Resync(L.A);
        return I;
    };
    auto Step = [](FLab& L, FFoundationInteraction& I, bool bLatest = true)
    {
        L.NextStep();
        I.StepOnce(L.A, L.Vitals, bLatest, 0);
    };
    // A fresh press acts only on an update's latest step.
    FLab L;
    FFoundationInteraction I = Make(L, TEXT("VanillaReference"));
    L.Stand({-150, 50, 0}, FVector(150, 0, -161));
    I.Press(EFoundationInput::Use, 1);
    I.Release(EFoundationInput::Use);
    Step(L, I, false);
    TestFalse(TEXT("not on a catch-up step"), L.Solid({0, 0, 0}));
    Step(L, I, true);
    TestTrue(TEXT("on the latest step, even after release"), L.Solid({0, 0, 0}));
    // A hold that begins with the press does not reach back to earlier catch-up steps either.
    FLab Back;
    I = Make(Back, TEXT("VanillaReference"));
    Back.Stand({-150, 50, 0}, FVector(150, 0, -161));
    I.Press(EFoundationInput::Use, 1);
    I.BeginUpdate();
    Step(Back, I, false);
    Step(Back, I, false);
    TestFalse(TEXT("held use waits for its press's step"), Back.Solid({0, 0, 0}));
    Step(Back, I, true);
    TestTrue(TEXT("then places once"), Back.Solid({0, 0, 0}) && I.Placed == 1);
    Back.Put({3, 0, 0});
    Back.Stand({150, 50, 0}, FVector(150, 0, -112));
    I.Release(EFoundationInput::Use);
    I.Press(EFoundationInput::Attack, 1);
    Step(Back, I, false);
    TestFalse(TEXT("held mining waits for its press's step"), I.bMining);
    Step(Back, I, true);
    TestTrue(TEXT("then starts"), I.bMining);
    // Held repeat every 4 steps; never twice in one update.
    FLab H;
    I = Make(H, TEXT("VanillaReference"));
    H.Stand({-350, 50, 0}, FVector(400, 0, -162));
    I.Press(EFoundationInput::Use, 1);
    TArray<int64> Commits;
    for (int32 Index = 0; Index < 13; ++Index)
    {
        I.BeginUpdate();
        const int32 Before = I.Placed;
        Step(H, I);
        if (I.Placed > Before) Commits.Add(I.Step);
    }
    TestTrue(TEXT("held repeat cadence 4"),
        Commits.Num() >= 3 && Commits[1] - Commits[0] == 4 && Commits[2] - Commits[1] == 4);
    FLab U;
    I = Make(U, TEXT("LucidFluid"));
    U.Stand({-350, 50, 0}, FVector(400, 0, -162));
    I.Press(EFoundationInput::Use, 1);
    I.BeginUpdate();
    for (int32 Index = 0; Index < 6; ++Index)
        Step(U, I, Index == 5);
    TestEqual(TEXT("one placement per update across catch-up steps"), I.Placed, 1);
    // Lucid: commits share a 2-step cadence.
    Commits.Reset();
    for (int32 Index = 0; Index < 8; ++Index)
    {
        I.BeginUpdate();
        const int32 Before = I.Placed;
        Step(U, I);
        if (I.Placed > Before) Commits.Add(I.Step);
    }
    TestTrue(TEXT("commit cadence 2"), Commits.Num() >= 2 && Commits[1] - Commits[0] == 2);
    // The retry: one candidate, fixed expiry, canceled by release or a target change.
    FLab R;
    I = Make(R, TEXT("LucidFluid"));
    R.Stand({50, 50, 0}, DownVector);
    I.Press(EFoundationInput::Use, 1);
    Step(R, I);
    TestTrue(TEXT("self-blocked candidate captured"), I.Retry.bActive && I.Retry.ExpiresStep == I.Step + 2);
    R.Facts(
        [](FFoundationPlayerFacts& F)
        {
            F.FeetCm.Z = 100.13;
            F.EyeCm.Z = 262.13;
        });
    Step(R, I);
    TestTrue(TEXT("placed once the body clears"), R.Solid({0, 0, 0}) && I.RetryCommits == 1);
    FLab Cancel;
    I = Make(Cancel, TEXT("LucidFluid"));
    Cancel.Stand({50, 50, 0}, DownVector);
    I.Press(EFoundationInput::Use, 1);
    Step(Cancel, I);
    I.Release(EFoundationInput::Use);
    Cancel.Facts(
        [](FFoundationPlayerFacts& F)
        {
            F.FeetCm.Z = 101;
            F.EyeCm.Z = 263;
        });
    Step(Cancel, I);
    TestFalse(TEXT("release before clearance places nothing"), Cancel.Solid({0, 0, 0}));
    FLab Turn;
    I = Make(Turn, TEXT("LucidFluid"));
    Turn.Stand({50, 50, 0}, DownVector);
    I.Press(EFoundationInput::Use, 1);
    Step(Turn, I);
    Turn.Facts(
        [](FFoundationPlayerFacts& F)
        {
            F.FeetCm.Z = 101;
            F.EyeCm.Z = 263;
            F.Aim = FVector(1, 0, -1).GetSafeNormal();
        });
    Step(Turn, I);
    TestFalse(TEXT("a turned-away candidate never places"), Turn.Solid({0, 0, 0}));
    FLab Late;
    I = Make(Late, TEXT("LucidFluid"));
    Late.Stand({50, 50, 0}, DownVector);
    I.Press(EFoundationInput::Use, 1);
    for (int32 Index = 0; Index < 3; ++Index)
        Step(Late, I);
    TestTrue(TEXT("expired and not refreshed by rechecks"), I.RetriesCanceled >= 1 || I.Retry.ExpiresStep > I.Step - 3);
    FLab Vanilla;
    I = Make(Vanilla, TEXT("VanillaReference"));
    Vanilla.Stand({50, 50, 0}, DownVector);
    I.Press(EFoundationInput::Use, 1);
    Step(Vanilla, I);
    TestFalse(TEXT("VanillaReference has no retry"), I.Retry.bActive);
    // Mining through the machine: 8 pickaxe steps, abort on release, use blocked while mining.
    FLab M;
    I = Make(M, TEXT("VanillaReference"));
    M.Hold(1);
    I.SelectSlot(M.A, M.Vitals, 1);
    M.Put({0, 0, 0});
    M.Stand({-150, 50, 0}, FVector(150, 0, -112));
    I.Press(EFoundationInput::Attack, 1);
    I.Press(EFoundationInput::Use, 1);
    int32 Steps = 0;
    while (M.Solid({0, 0, 0}) && Steps < 20)
    {
        Step(M, I);
        ++Steps;
        TestFalse(TEXT("no placement during a live break"), M.Solid({-1, 0, 0}) || M.A.Placements > 0);
    }
    TestEqual(TEXT("pickaxe break through the machine"), Steps, 8);
    TestEqual(TEXT("post-break delay"), I.MiningDelay, FFoundationInteraction::MiningDelaySteps);
    M.Put({0, 0, 0});
    for (int32 Index = 0; Index < 3; ++Index)
        Step(M, I);
    TestFalse(TEXT("delay holds the next break"), I.bMining);
    I.Release(EFoundationInput::Attack);
    Step(M, I);
    M.Vitals.AttackTicker = 5;
    I.Press(EFoundationInput::Attack, 1);
    Step(M, I);
    TestTrue(TEXT("fresh press starts"), I.bMining);
    I.Release(EFoundationInput::Attack);
    Step(M, I);
    TestTrue(TEXT("release aborts and restarts recovery"), !I.bMining && M.Vitals.AttackTicker == 0);
    // A held attack never repeats melee.
    FLab A;
    FFoundationTrainingTarget Target;
    Target.Id = 1;
    Target.Box = FBox(FVector(170, 20, 0), FVector(230, 80, 180));
    A.A.Targets.Add(Target);
    A.A.Reset();
    I = Make(A, TEXT("VanillaReference"));
    A.Stand({0, 50, 0}, FVector(1, 0, 0));
    I.Press(EFoundationInput::Attack, 1);
    for (int32 Index = 0; Index < 40; ++Index)
        Step(A, I);
    TestEqual(TEXT("one attack per press"), A.A.Targets[0].Hits, 1);
    // Cancel conditions and death.
    FLab D;
    I = Make(D, TEXT("LucidFluid"));
    D.Stand({50, 50, 0}, DownVector);
    I.Press(EFoundationInput::Use, 1);
    Step(D, I);
    I.CancelAll(D.A, D.Vitals, EFoundationReason::Canceled);
    TestTrue(TEXT("cancel drops held use and the retry"), !I.bUseHeld && !I.Retry.bActive);
    I.Press(EFoundationInput::Use, 1);
    D.Facts([](FFoundationPlayerFacts& F) { F.bAlive = false; });
    Step(D, I);
    D.Facts(
        [](FFoundationPlayerFacts& F)
        {
            F.bAlive = true;
            F.FeetCm.Z = 150;
            F.EyeCm.Z = 312;
        });
    I.Release(EFoundationInput::Use);
    Step(D, I);
    TestFalse(TEXT("a press made while dead never acts later"), D.Solid({0, 0, 0}));
    // A slot switch cancels the candidate.
    FLab Slot;
    I = Make(Slot, TEXT("LucidFluid"));
    Slot.Stand({50, 50, 0}, DownVector);
    I.Press(EFoundationInput::Use, 1);
    Step(Slot, I);
    I.SelectSlot(Slot.A, Slot.Vitals, 1);
    TestFalse(TEXT("slot switch cancels the retry"), I.Retry.bActive);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractionMovementTest, "Unnamed.Foundation.Interaction.JumpAndFall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationInteractionMovementTest::RunTest(const FString&)
{
    const UFoundationProfile& P = *GetDefault<UFoundationProfile>();
    const UFoundationInteractionSettings& Settings = UFoundationInteractionSettings::Get();
    // The pawn's order per step: movement against committed collision, then interaction, which commits
    // before the next movement step. StepsPerUpdate > 1 simulates a hitch (only the last step is latest).
    struct FRun
    {
        FLab L;
        FFoundationSimulation Sim;
        FFoundationInteraction I;
        FFoundationIntent Intent;
        FVector Aim = FVector(0, 0, -1);
        TArray<FBox> Static;
        int32 MaxPerUpdate = 0;

        FRun(FName Preset, FVector Feet, const UFoundationProfile& Profile, const UFoundationInteractionSettings& S)
        {
            Static = {FBox(FVector(-2000, -2000, -100), FVector(2000, 2000, 0))};
            Sim.Reset(Feet, Profile);
            Sim.bGrounded = Feet.Z == 0;
            I.PlayerId = L.Player;
            I.SetPreset(*S.FindPreset(Preset), Profile.TickSeconds);
            I.Resync(L.A);
        }

        void Update(const UFoundationProfile& Profile, int32 Steps = 1)
        {
            I.BeginUpdate();
            const int32 Before = I.Placed;
            for (int32 Index = 0; Index < Steps; ++Index)
            {
                TArray<FBox> Boxes = Static;
                L.A.World.GatherCollision(FFoundationSimulation::Bounds(Sim.Feet, 180, 60).ExpandBy(500), Boxes);
                Sim.Step(Intent, 0, Profile, Boxes);
                FFoundationPlayerFacts Facts;
                Facts.FeetCm = Sim.Feet;
                Facts.HeightCm = Sim.HeightCm;
                Facts.EyeCm =
                    Sim.Feet + FVector(0, 0, Sim.bCrouched ? Profile.CrouchingEyesCm : Profile.StandingEyesCm);
                Facts.Aim = Aim.GetSafeNormal();
                Facts.bGrounded = Sim.bGrounded;
                Facts.FallDistanceCm = Sim.FallDistanceCm;
                Facts.bSecondaryUse = Intent.bCrouch;
                Facts.Step = Sim.Steps;
                L.A.SetFacts(L.Player, Facts);
                L.A.Step();
                I.StepOnce(L.A, L.Vitals, Index == Steps - 1, 0);
            }
            MaxPerUpdate = FMath::Max(MaxPerUpdate, I.Placed - Before);
        }

        // Every placed block never overlapped the body that existed at its commit.
        int32 Column() const
        {
            int32 Height = 0;
            while (!L.A.World.Get({0, 0, Height}).State.IsAir())
                ++Height;
            return Height;
        }
    };
    for (const FName Preset : {FName(TEXT("VanillaReference")), FName(TEXT("LucidFluid"))})
        for (const int32 StepsPerUpdate : {1, 3})
        {
            // Jump-stack: look straight down, hold jump and use.
            FRun Run(Preset, FVector(50, 50, 0), P, Settings);
            Run.Intent.bJump = true;
            Run.I.Press(EFoundationInput::Use, 1);
            bool bOverlap = false;
            for (int32 Update = 0; Update < 120 / StepsPerUpdate; ++Update)
            {
                Run.Update(P, StepsPerUpdate);
                for (int32 Z = 0; Z < Run.Column(); ++Z)
                    bOverlap |= FFoundationSimulation::Bounds(Run.Sim.Feet, Run.Sim.HeightCm, 60)
                                    .ExpandBy(-1e-6)
                                    .Intersect(FoundationGrid::CellBox({0, 0, Z}, FVector::ZeroVector).ExpandBy(-1e-6));
            }
            const FString Name = Preset.ToString() + FString::Printf(TEXT(" x%d"), StepsPerUpdate);
            TestTrue(Name + TEXT(": tower of at least 6 in 6 s"), Run.Column() >= 6);
            TestFalse(Name + TEXT(": never inside a block"), bOverlap);
            TestTrue(Name + TEXT(": at most one placement per update"), Run.MaxPerUpdate <= 1);
            Run.Intent.bJump = false;
            Run.I.Release(EFoundationInput::Use);
            for (int32 Update = 0; Update < 40; ++Update)
                Run.Update(P);
            TestTrue(Name + TEXT(": stands on the top block"),
                Run.Sim.bGrounded && FMath::IsNearlyEqual(Run.Sim.Feet.Z, Run.Column() * 100., 1e-6));
        }
    // Descending: falling from 3.5 blocks with a single press while still clear; land on the new block.
    for (const FName Preset : {FName(TEXT("VanillaReference")), FName(TEXT("LucidFluid"))})
    {
        FRun Run(Preset, FVector(50, 50, 250), P, Settings);
        Run.Update(P);
        Run.Update(P);
        TestFalse(TEXT("falling"), Run.Sim.bGrounded);
        Run.I.Press(EFoundationInput::Use, 1);
        Run.I.Release(EFoundationInput::Use);
        Run.Update(P);
        TestTrue(TEXT("placed below while clear"), !Run.L.A.World.Get({0, 0, 0}).State.IsAir());
        for (int32 Update = 0; Update < 20; ++Update)
            Run.Update(P);
        TestTrue(
            TEXT("landed on the new block"), Run.Sim.bGrounded && FMath::IsNearlyEqual(Run.Sim.Feet.Z, 100., 1e-6));
    }
    // Break the supporting block: fall to the floor.
    {
        FRun Run(TEXT("VanillaReference"), FVector(50, 50, 100), P, Settings);
        Run.L.Put({0, 0, 0});
        Run.Sim.bGrounded = true;
        Run.I.SelectSlot(Run.L.A, Run.L.Vitals, 1);
        Run.I.Press(EFoundationInput::Attack, 1);
        for (int32 Update = 0; Update < 30; ++Update)
            Run.Update(P);
        TestTrue(TEXT("mined the block underfoot and fell"),
            Run.L.A.World.Get({0, 0, 0}).State.IsAir() && Run.Sim.bGrounded &&
                FMath::IsNearlyEqual(Run.Sim.Feet.Z, 0., 1e-6));
    }
    // A low ceiling: the head stops at 280 cm, the feet exactly at 100. Touching is contact, so the
    // LucidFluid retry places at that step; the reference's 4-step repeat misses the instant.
    for (const FName Preset : {FName(TEXT("VanillaReference")), FName(TEXT("LucidFluid"))})
    {
        FRun Run(Preset, FVector(50, 50, 0), P, Settings);
        Run.Static.Add(FBox(FVector(-100, -100, 280), FVector(200, 200, 300)));
        Run.Intent.bJump = true;
        Run.I.Press(EFoundationInput::Use, 1);
        for (int32 Update = 0; Update < 12; ++Update)
            Run.Update(P);
        const bool bPlaced = !Run.L.A.World.Get({0, 0, 0}).State.IsAir();
        TestEqual(Preset.ToString() + TEXT(": exact-contact placement under a low ceiling"), bPlaced,
            Preset == TEXT("LucidFluid"));
    }
    // Crouch at an edge: the crouched player stays on the ledge and builds outward from its side.
    {
        FRun Run(TEXT("VanillaReference"), FVector(50, 50, 100), P, Settings);
        Run.L.Put({0, 0, 0});
        Run.Sim.bGrounded = true;
        Run.Intent.bCrouch = true;
        Run.Intent.Move = FVector2D(0, 1);
        for (int32 Update = 0; Update < 40; ++Update)
            Run.Update(P);
        TestTrue(TEXT("crouch keeps the ledge"), Run.Sim.Feet.Z == 100 && Run.Sim.Feet.X < 130);
        Run.Intent.Move = FVector2D::ZeroVector;
        Run.Aim = FVector(-1, 0, -5);
        Run.I.Press(EFoundationInput::Use, 1);
        Run.I.Release(EFoundationInput::Use);
        Run.Update(P);
        TestTrue(TEXT("placed against the ledge side"), !Run.L.A.World.Get({1, 0, 0}).State.IsAir());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractionDataTest, "Unnamed.Foundation.Interaction.DataDriven",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationInteractionDataTest::RunTest(const FString&)
{
    const UFoundationInteractionSettings& Settings = UFoundationInteractionSettings::Get();
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    TestTrue(TEXT("no project blocks by default; dummy_block retired"),
        Settings.Blocks.IsEmpty() && !Registry->FindBlock(TEXT("dummy_block")));
    const FFoundationBlockType* Stone = Registry->FindBlock(TEXT("stone"));
    const FFoundationBlockType* Obsidian = Registry->FindBlock(TEXT("obsidian"));
    const FFoundationBlockType* IronOre = Registry->FindBlock(TEXT("iron_ore"));
    if (!TestTrue(TEXT("imported stone, obsidian and iron ore"), Stone && Obsidian && IronOre)) return false;
    TestTrue(TEXT("stone: 26.3 hardness 1.5, needs a pickaxe for drops"),
        Stone->Hardness == 1.5f && !Stone->bReplaceable && Stone->bRequiresCorrectToolForDrops &&
            Stone->Tags.HasTagExact(FoundationTags::BlockMineablePickaxe));
    TestTrue(
        TEXT("both presets"), Settings.FindPreset(TEXT("VanillaReference")) && Settings.FindPreset(TEXT("LucidFluid")));
    TestEqual(TEXT("default preset"), Settings.DefaultPreset, FName(TEXT("VanillaReference")));
    FFoundationRules Rules;
    Settings.BuildRules(Rules);
    TestEqual(TEXT("block reach"), Rules.BlockReachCm, 450.);
    TestEqual(TEXT("entity reach"), Rules.EntityReachCm, 300.);
    // Item id = block id: one block item per imported block with an item, placing it.
    const int32 StoneItem = Rules.FindItem(TEXT("stone"));
    TestTrue(TEXT("stone item places stone"),
        StoneItem != INDEX_NONE && Rules.Items[StoneItem].PlacesBlock == TEXT("stone") &&
            Rules.Items[StoneItem].MaxStack == 64);
    TestEqual(TEXT("loadout slot 1"), Rules.Loadout[0], FName(TEXT("stone")));
    TestEqual(TEXT("no item for water"), Rules.FindItem(TEXT("water")), int32(INDEX_NONE));
    if (!TestTrue(TEXT("the 26.3 iron tools (DA_Items)"),
            Rules.FindItem(TEXT("iron_pickaxe")) != INDEX_NONE && Rules.FindItem(TEXT("iron_sword")) != INDEX_NONE))
        return false;
    const FFoundationItemDefinition& Pickaxe = Rules.Items[Rules.FindItem(TEXT("iron_pickaxe"))];
    TestTrue(TEXT("iron pickaxe mines and fights"),
        Pickaxe.GetMiningSpeed(Stone->Tags) == 6 && Pickaxe.AttackDamage == 4 && Pickaxe.AttacksPerSecond == 1.2f &&
            Pickaxe.MaxDurability == 250);
    TestTrue(TEXT("iron tier: iron ore yes, obsidian no"),
        Pickaxe.IsCorrectForDrops(IronOre->Tags) && !Pickaxe.IsCorrectForDrops(Obsidian->Tags) &&
            Pickaxe.GetMiningSpeed(Obsidian->Tags) == 6);
    const FFoundationItemDefinition& Sword = Rules.Items[Rules.FindItem(TEXT("iron_sword"))];
    TestTrue(TEXT("iron sword mines stone at 1"), Sword.GetMiningSpeed(Stone->Tags) == 1 && Sword.AttackDamage == 6);
    // A project block and its item, added as data only, go through the same loop.
    FLab L(false);
    FFoundationBlockDefinition Brick = MakeTestBlock(TEXT("lab_brick"));
    Brick.DisplayName = TEXT("Lab Brick");
    Brick.Hardness = 3;
    Brick.bRequiresCorrectToolForDrops = true;
    Brick.DropItem = TEXT("lab_brick");
    L.SetBlocks({Brick});
    FFoundationItemDefinition BrickItem = L.A.Rules.Items[L.A.Rules.FindItem(TEXT("lab_block"))];
    BrickItem.Id = TEXT("lab_brick");
    BrickItem.PlacesBlock = TEXT("lab_brick");
    L.A.Rules.Items.Add(BrickItem);
    L.A.Rules.Loadout = {TEXT("lab_brick"), TEXT("iron_pickaxe")};
    L.A.Reset();
    L.Stand({-150, 50, 0}, FVector(150, 0, -161));
    TestTrue(TEXT("places the new block"), L.Use().bPlaced);
    const FFoundationBlockType& Placed = L.A.World.GetBlock(L.A.World.Get({0, 0, 0}).State);
    TestEqual(TEXT("as data"), Placed.Id, FName(TEXT("lab_brick")));
    TestTrue(TEXT("after the imported ids"), Placed.ProtocolId > Stone->ProtocolId && Placed.MinStateId >= 35723);
    L.Hold(1);
    L.Stand({-150, 50, 0}, FVector(150, 0, -112));
    TestEqual(TEXT("its own hardness: pickaxe 6/3/30 -> 15 steps"), MineSteps(L), 15);
    TestEqual(TEXT("and its own drop"), L.P().Inventory.Hotbar[0].Count, 64);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractionStressTest, "Unnamed.Foundation.Interaction.Stress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationInteractionStressTest::RunTest(const FString&)
{
    // 4096 cubes and 1000 place/break cycles through the command path, with a stress player whose facts
    // aim each command. Timing is reported, with a loose bound so slow machines do not fail.
    FLab L;
    L.A.World.BoundsMin = FIntVector(-8, -8, 0);
    L.A.World.BoundsMax = FIntVector(7, 7, 15);
    const double Start = FPlatformTime::Seconds();
    int32 Placed = 0;
    for (int32 Z = 0; Z < 16; ++Z)
        for (int32 Y = -8; Y < 8; ++Y)
            for (int32 X = -8; X < 8; ++X)
            {
                // Above the target cell, looking straight down at the cell below it.
                L.Stand(FVector(X * 100 + 50, Y * 100 + 50, Z * 100 + 120 - 162), DownVector);
                L.Facts([](FFoundationPlayerFacts& F) { F.FeetCm.Z += 5000; });
                Placed += L.Use().bPlaced ? 1 : 0;
            }
    const double BuildMs = (FPlatformTime::Seconds() - Start) * 1000.;
    TestEqual(TEXT("4096 cubes"), Placed, 4096);
    TestEqual(TEXT("solid count"), L.A.World.GetSolidCount(), 4096);
    const SIZE_T BuildBytes = L.A.World.GetAllocatedSize();
    TArray<FBox> Boxes;
    const double GatherStart = FPlatformTime::Seconds();
    for (int32 Index = 0; Index < 1000; ++Index)
    {
        Boxes.Reset();
        L.A.World.GatherCollision(FBox(FVector(-300, -300, 500), FVector(300, 300, 1300)), Boxes);
    }
    const double GatherUs = (FPlatformTime::Seconds() - GatherStart) * 1000.;
    // 1000 cycles on one cell with an instant-mining test tool.
    FFoundationItemDefinition Tool = L.A.Rules.Items[L.A.Rules.FindItem(TEXT("iron_pickaxe"))];
    Tool.Id = TEXT("stress_tool");
    for (FFoundationMiningRule& Rule : Tool.MiningRules)
        if (Rule.Speed > 0) Rule.Speed = 1000;
    L.A.Rules.Items.Add(Tool);
    L.P().Inventory.SetSlot(5, L.P().Inventory.MakeStack(L.A.Rules.FindItem(TEXT("stress_tool")), 1));
    L.A.World.Set({7, 7, 15}, {});
    const double CycleStart = FPlatformTime::Seconds();
    int32 Cycles = 0;
    for (int32 Index = 0; Index < 1000; ++Index)
    {
        L.Hold(0);
        L.Stand(FVector(750, 750, 1620 - 162 + 20), DownVector);
        L.Facts([](FFoundationPlayerFacts& F) { F.FeetCm.Z += 5000; });
        const bool bPlaced = L.Use().bPlaced;
        L.Hold(5);
        L.NextStep();
        const bool bBroken = L.Run(L.Make(EFoundationCommandType::MineStart)).bBroken;
        Cycles += bPlaced && bBroken ? 1 : 0;
    }
    const double CycleMs = (FPlatformTime::Seconds() - CycleStart) * 1000.;
    TestEqual(TEXT("1000 place/break cycles"), Cycles, 1000);
    TestTrue(TEXT("result cache stays bounded"), L.P().Results.Num() <= L.A.Rules.ResultCacheSize);
    TestTrue(TEXT("storage does not grow with cycles"), L.A.World.GetAllocatedSize() <= BuildBytes + 1024 * 1024);
    TestTrue(TEXT("loose time bound"), BuildMs < 5000 && CycleMs < 5000);
    AddInfo(FString::Printf(TEXT("build 4096: %.2f ms (%.2f us/cmd); gather 1000x: %.2f ms; 1000 cycles: %.2f ms "
                                 "(%.2f us/cycle); world %llu bytes; pending changes %d"),
        BuildMs, BuildMs * 1000. / 4096, GatherUs, CycleMs, CycleMs, uint64(L.A.World.GetAllocatedSize()),
        L.A.World.GetPendingChangeCount()));
    return true;
}

// Plants through the authority (phase 6, S7): a double plant is one unit (placed, mined and replaced together), pops
// drop nothing, plants are replaced in place, a faceless vine and a double plant without room are refused, and the
// rules switch off for A/B.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractionPlantsTest, "Unnamed.Foundation.Interaction.Plants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationInteractionPlantsTest::RunTest(const FString& Parameters)
{
    constexpr int32 Slot = 5;
    auto Give = [](FLab& L, const TCHAR* Item)
    {
        const int32 Index = L.A.Rules.FindItem(Item);
        L.P().Inventory.SetSlot(Slot, L.P().Inventory.MakeStack(Index, 64));
        L.Hold(Slot);
        return Index != INDEX_NONE;
    };
    auto Text = [](FLab& L, FIntVector Cell) { return L.A.World.GetRegistry().ToString(L.A.World.Get(Cell).State); };
    auto Pops = [](FLab& L, int64 After)
    {
        int32 Count = 0;
        for (const FFoundationEvent& Event : L.A.GetEvents())
            Count += Event.Serial > After && Event.Kind == EFoundationEventKind::Popped;
        return Count;
    };
    // From (-150, 50, 0) the eye is at z 162: this aim meets the top face of (0, 0, 0), these the middles of
    // (0, 0, 1) and (0, 0, 2).
    const FVector OnGround(200, 0, -62), AtLower(200, 0, -12), AtUpper(200, 0, 88);
    const FIntVector Ground(0, 0, 0), LowerCell(0, 0, 1), UpperCell(0, 0, 2);

    FLab L;
    L.Put(Ground, TEXT("grass_block"));
    TestTrue(TEXT("tall grass has an item"), Give(L, TEXT("tall_grass")));
    L.Stand({-150, 50, 0}, OnGround);
    FFoundationResult R = L.Use();
    TestTrue(TEXT("tall grass places"), R.bPlaced && R.Cell == LowerCell);
    TestEqual(TEXT("its lower half"), Text(L, LowerCell), FString(TEXT("tall_grass[half=lower]")));
    TestEqual(TEXT("and its upper half above"), Text(L, UpperCell), FString(TEXT("tall_grass[half=upper]")));
    // Mining either half takes both, the other half as a pop.
    int64 Mark = L.A.GetEventSerial();
    L.Stand({-150, 50, 0}, AtUpper);
    R = L.Run(L.Make(EFoundationCommandType::MineStart));
    TestTrue(TEXT("the upper half breaks at once"), R.bBroken && R.bInstant && R.Cell == UpperCell);
    TestTrue(TEXT("and takes the lower half"), !L.Solid(LowerCell) && !L.Solid(UpperCell) && Pops(L, Mark) == 1);
    L.NextStep();
    L.Stand({-150, 50, 0}, OnGround);
    TestTrue(TEXT("re-placed"), L.Use().bPlaced);
    Mark = L.A.GetEventSerial();
    L.Stand({-150, 50, 0}, AtLower);
    R = L.Run(L.Make(EFoundationCommandType::MineStart));
    TestTrue(TEXT("the lower half breaks"), R.bBroken && R.Cell == LowerCell);
    TestTrue(TEXT("and takes the upper half"), !L.Solid(LowerCell) && !L.Solid(UpperCell) && Pops(L, Mark) == 1);
    // No room for the upper half: refused.
    L.NextStep();
    L.Put(UpperCell, TEXT("stone"));
    L.Stand({-150, 50, 0}, OnGround);
    R = L.Use();
    TestTrue(TEXT("a block above refuses the double plant"),
        !R.bPlaced && R.Reason == EFoundationReason::DestinationOccupied && !L.Solid(LowerCell));
    FFoundationHit Ceiling;
    Ceiling.Kind = EFoundationHitKind::Block;
    Ceiling.Cell = FIntVector(3, 0, 14);
    Ceiling.Face = EFoundationFace::Up;
    Ceiling.bInRange = true;
    Ceiling.Point = FoundationGrid::CellBox(Ceiling.Cell, L.A.World.OriginCm).GetCenter() + FVector(0, 0, 50);
    L.Put(Ceiling.Cell, TEXT("grass_block"));
    FIntVector Destination;
    TestEqual(TEXT("the build limit refuses the upper half"), L.A.CheckPlacement(L.P(), Main, Ceiling, Destination),
        EFoundationReason::OutsideBounds);

    // A vine under a block holds its up face, which needs the upper face of the block above (Jesus's answer to Q8):
    // stone and a top slab hold it, a bottom slab does not.
    const FFoundationBlockRegistry& Registry = L.A.World.GetRegistry();
    auto Named = [&Registry](const TCHAR* Text)
    {
        FFoundationBlockState State;
        Registry.Parse(Text, State);
        return Registry.ToString(State);
    };
    TestTrue(TEXT("vine has an item"), Give(L, TEXT("vine")));
    FFoundationHit Underside = Ceiling;
    Underside.Cell = FIntVector(3, 0, 5);
    Underside.Face = EFoundationFace::Down;
    Underside.Point = FoundationGrid::CellBox(Underside.Cell, L.A.World.OriginCm).GetCenter() - FVector(0, 0, 50);
    FFoundationBlockState Hanging, Support;
    for (const TCHAR* Above : {TEXT("stone"), TEXT("stone_slab[type=top]"), TEXT("stone_slab[type=bottom]")})
    {
        Registry.Parse(Above, Support);
        L.A.World.Set(Underside.Cell, Support);
        const bool bHolds = FCString::Strcmp(Above, TEXT("stone_slab[type=bottom]")) != 0;
        TestEqual(FString::Printf(TEXT("a vine under %s"), Above),
            L.A.CheckPlacement(L.P(), Main, Underside, Destination, &Hanging),
            bHolds ? EFoundationReason::None : EFoundationReason::CannotSurvive);
        if (bHolds) TestEqual(TEXT("holding its up face"), Registry.ToString(Hanging), Named(TEXT("vine[up=true]")));
    }
    L.Put(Underside.Cell, TEXT("stone"));
    Underside.Face = EFoundationFace::South;
    Underside.Point = FoundationGrid::CellBox(Underside.Cell, L.A.World.OriginCm).GetCenter() + FVector(0, 50, 0);
    FFoundationBlockState Vine;
    TestEqual(TEXT("a vine on a side places"), L.A.CheckPlacement(L.P(), Main, Underside, Destination, &Vine),
        EFoundationReason::None);
    FFoundationBlockState North;
    L.A.World.GetRegistry().Parse(TEXT("vine[north=true]"), North);
    TestEqual(TEXT("holding the face toward the block"), L.A.World.GetRegistry().ToString(Vine),
        L.A.World.GetRegistry().ToString(North));

    // A coral fan's item places its wall fan on a side, facing away from it, and its floor fan on a top face (Jesus's
    // answer to Q7); the wall fan has no item of its own and drops the fan's.
    TestTrue(TEXT("tube coral fan has an item"), Give(L, TEXT("tube_coral_fan")));
    FFoundationBlockState Fan;
    TestEqual(TEXT("a coral fan on a side places"), L.A.CheckPlacement(L.P(), Main, Underside, Destination, &Fan),
        EFoundationReason::None);
    TestEqual(TEXT("as its wall fan facing away"), Registry.ToString(Fan),
        Named(TEXT("tube_coral_wall_fan[facing=south,waterlogged=false]")));
    Underside.Face = EFoundationFace::Up;
    Underside.Point = FoundationGrid::CellBox(Underside.Cell, L.A.World.OriginCm).GetCenter() + FVector(0, 0, 50);
    TestEqual(TEXT("a coral fan on a top face places"), L.A.CheckPlacement(L.P(), Main, Underside, Destination, &Fan),
        EFoundationReason::None);
    TestEqual(TEXT("as its floor fan"), Registry.ToString(Fan), Named(TEXT("tube_coral_fan[waterlogged=false]")));
    const FFoundationBlockType* WallFan = L.A.World.FindBlock(TEXT("tube_coral_wall_fan"));
    TestTrue(TEXT("the wall fan drops the fan's item and has none"),
        WallFan && WallFan->DropItem == TEXT("tube_coral_fan") &&
            L.A.Rules.FindItem(TEXT("tube_coral_wall_fan")) == INDEX_NONE);

    // Replaceable plants are their own destination; replacing a double plant's half pops the other half.
    FLab Rep;
    Rep.Put(Ground, TEXT("grass_block"));
    Rep.Put(LowerCell, TEXT("short_grass"));
    TestTrue(TEXT("stone has an item"), Give(Rep, TEXT("stone")));
    Rep.Stand({-150, 50, 0}, AtLower);
    R = Rep.Use();
    TestTrue(TEXT("stone replaces short grass in place"), R.bPlaced && R.Cell == LowerCell);
    TestEqual(TEXT("stone in the grass cell"), Text(Rep, LowerCell), FString(TEXT("stone")));
    FFoundationBlockState Lower, Upper;
    Rep.A.World.GetRegistry().Parse(TEXT("tall_grass[half=lower]"), Lower);
    Rep.A.World.GetRegistry().Parse(TEXT("tall_grass[half=upper]"), Upper);
    Rep.A.World.Set(LowerCell, Lower);
    Rep.A.World.Set(UpperCell, Upper);
    Mark = Rep.A.GetEventSerial();
    Rep.Stand({-150, 50, 0}, AtLower);
    R = Rep.Use();
    TestTrue(TEXT("stone replaces the lower half"),
        R.bPlaced && R.Cell == LowerCell && Text(Rep, LowerCell) == TEXT("stone"));
    TestTrue(TEXT("and the upper half pops"), !Rep.Solid(UpperCell) && Pops(Rep, Mark) == 1);
    // One snow layer is its own destination, two layers are not (Jesus's answer to Q5).
    FFoundationHit OnSnow = Ceiling;
    OnSnow.Cell = UpperCell;
    EFoundationReason Ignored;
    for (const int32 Layers : {1, 2})
    {
        FFoundationBlockState Snow;
        Rep.A.World.GetRegistry().Parse(*FString::Printf(TEXT("snow[layers=%d]"), Layers), Snow);
        Rep.A.World.Set(UpperCell, Snow);
        TestTrue(FString::Printf(TEXT("the destination on %d snow layers"), Layers),
            Rep.A.GetDestination(OnSnow, Destination, Ignored) &&
                Destination == (Layers == 1 ? UpperCell : UpperCell + FIntVector(0, 0, 1)));
    }
    // Placing on one layer: stone replaces it, snow goes into the cell above (stacking layers is not built).
    FFoundationBlockState OneLayer;
    Rep.A.World.GetRegistry().Parse(TEXT("snow[layers=1]"), OneLayer);
    Rep.A.World.Set(UpperCell, OneLayer);
    for (const TCHAR* Placing : {TEXT("stone"), TEXT("snow")})
    {
        TestTrue(FString::Printf(TEXT("%s has an item"), Placing), Give(Rep, Placing));
        const bool bStone = FCString::Strcmp(Placing, TEXT("stone")) == 0;
        TestTrue(FString::Printf(TEXT("%s on one snow layer"), Placing),
            Rep.A.CheckPlacement(Rep.P(), Main, OnSnow, Destination) == EFoundationReason::None &&
                Destination == (bStone ? UpperCell : UpperCell + FIntVector(0, 0, 1)));
    }

    // Pops drop nothing: mining one half rolls tall grass's loot table once (loot step 2b: by hand never the grass,
    // wheat seeds 1 in 8), and the other half pops without a roll of its own.
    FLab F(false);
    F.Put(Ground, TEXT("grass_block"));
    Give(F, TEXT("tall_grass"));
    F.Stand({-150, 50, 0}, OnGround);
    TestTrue(TEXT("finite tall grass places"), F.Use().bPlaced);
    TestEqual(TEXT("one item used"), F.P().Inventory.Hotbar[Slot].Count, 63);
    F.Stand({-150, 50, 0}, AtUpper);
    R = F.Run(F.Make(EFoundationCommandType::MineStart));
    TestTrue(TEXT("finite upper half breaks"), R.bBroken && !F.Solid(LowerCell));
    const int32 Seeds = F.A.Rules.FindItem(TEXT("wheat_seeds"));
    TestTrue(TEXT("one roll (no grass back, at most one wheat seed), none for the pop"),
        F.P().Inventory.Hotbar[Slot].Count == 63 && R.Drops.Num() <= 1 &&
            (R.Drops.IsEmpty() || (Seeds != INDEX_NONE && R.Drops[0].Item == Seeds && R.Drops[0].Count == 1)));

    // Instant mining from the facts: a flower at once, a vine (hardness 0.2) not.
    TestEqual(TEXT("dandelion is instant"), L.A.GetMiningDelta(L.P(), *L.A.World.FindBlock(TEXT("dandelion"))), 1.);
    TestTrue(TEXT("vine is not instant"), L.A.GetMiningDelta(L.P(), *L.A.World.FindBlock(TEXT("vine"))) < 1);

    // The switch: without plant rules a double plant writes one half and nothing pops.
    FLab Off;
    Off.A.Options.bPlantRules = false;
    Off.Put(Ground, TEXT("grass_block"));
    Give(Off, TEXT("tall_grass"));
    Off.Stand({-150, 50, 0}, OnGround);
    TestTrue(TEXT("rules off: placed"), Off.Use().bPlaced);
    TestTrue(TEXT("rules off: no upper half"), Off.Solid(LowerCell) && !Off.Solid(UpperCell));
    return true;
}

// Plant survival (phase 6, S7; S7b): rows pinned from scout H's measured support table (T1; none for leaves, mud, soul
// sand or 8-layer snow until the sturdy-face follow-up, Q2), then the cascade through the authority: one level deep, a
// double plant as one unit, stacks to their end (Q1), multiface blocks trimmed, unknown cells never popping.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBlocksSurvivalTest, "Unnamed.Foundation.Blocks.Survival",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationBlocksSurvivalTest::RunTest(const FString& Parameters)
{
    FLab L;
    if (!TestTrue(TEXT("plant survival rules built"), L.A.PlantSurvival.IsValid())) return false;
    const FFoundationBlockRegistry& Registry = L.A.World.GetRegistry();
    auto Parse = [&Registry](const TCHAR* Text)
    {
        FFoundationBlockState State;
        ensureMsgf(Registry.Parse(Text, State), TEXT("parse %s"), Text);
        return State;
    };
    // One row: the plant at P, its neighbours given by offset, everything else air.
    struct FNeighbour
    {
        FIntVector Offset;
        const TCHAR* Block;
    };
    const FIntVector P(0, 0, 5);
    auto Survives = [&](const TCHAR* Plant, const TArray<FNeighbour>& Around)
    {
        TMap<FIntVector, uint16> Cells;
        Cells.Add(P, Parse(Plant).Id);
        for (const FNeighbour& Cell : Around)
            Cells.Add(P + Cell.Offset, Parse(Cell.Block).Id);
        return L.A.PlantSurvival->Survives(Parse(Plant).Id, P.X, P.Y, P.Z,
            [&Cells](int32 X, int32 Y, int32 Z) { return Cells.FindRef(FIntVector(X, Y, Z)); });
    };
    const FIntVector Below(0, 0, -1), Above(0, 0, 1), North(0, -1, 0), South(0, 1, 0), East(1, 0, 0), West(-1, 0, 0);
    struct FRow
    {
        const TCHAR* Plant;
        TArray<FNeighbour> Around;
        bool bSurvives;
    };
    const FRow Rows[] = {
        // Ground plants (supports_vegetation, supports_dry_vegetation).
        {TEXT("dandelion"), {{Below, TEXT("grass_block")}}, true},
        {TEXT("dandelion"), {{Below, TEXT("podzol")}}, true},
        {TEXT("dandelion"), {{Below, TEXT("stone")}}, false},
        {TEXT("dandelion"), {{Below, TEXT("sand")}}, false},
        {TEXT("dandelion"), {}, false},
        {TEXT("short_grass"), {{Below, TEXT("moss_block")}}, true},
        {TEXT("dead_bush"), {{Below, TEXT("sand")}}, true},
        {TEXT("dead_bush"), {{Below, TEXT("terracotta")}}, true},
        {TEXT("dead_bush"), {{Below, TEXT("stone")}}, false},
        // Double plants: the lower half on soil, the upper half on its own lower half only.
        {TEXT("tall_grass[half=lower]"), {{Below, TEXT("grass_block")}}, true},
        {TEXT("tall_grass[half=upper]"), {{Below, TEXT("tall_grass[half=lower]")}}, true},
        {TEXT("tall_grass[half=upper]"), {{Below, TEXT("grass_block")}}, false},
        {TEXT("small_dripleaf[half=lower,waterlogged=false]"), {{Below, TEXT("clay")}}, true},
        {TEXT("small_dripleaf[half=lower,waterlogged=false]"), {{Below, TEXT("dirt")}}, false},
        {TEXT("small_dripleaf[half=lower,waterlogged=true]"), {{Below, TEXT("dirt")}}, true},
        // Tagged soils.
        {TEXT("azalea"), {{Below, TEXT("clay")}}, true},
        {TEXT("azalea"), {{Below, TEXT("stone")}}, false},
        {TEXT("bamboo"), {{Below, TEXT("gravel")}}, true},
        {TEXT("bamboo"), {{Below, TEXT("bamboo")}}, true},
        {TEXT("bamboo"), {{Below, TEXT("stone")}}, false},
        {TEXT("mangrove_propagule[hanging=false]"), {{Below, TEXT("dirt")}}, true},
        {TEXT("mangrove_propagule[hanging=true]"), {{Above, TEXT("mangrove_leaves")}}, true},
        {TEXT("mangrove_propagule[hanging=true]"), {{Above, TEXT("oak_leaves")}}, false},
        {TEXT("big_dripleaf"), {{Below, TEXT("clay")}}, true},
        {TEXT("big_dripleaf"), {{Below, TEXT("stone")}}, false},
        {TEXT("big_dripleaf_stem"), {{Below, TEXT("clay")}, {Above, TEXT("big_dripleaf")}}, true},
        {TEXT("big_dripleaf_stem"), {{Below, TEXT("clay")}}, false},
        // Cactus and sugar cane.
        {TEXT("cactus"), {{Below, TEXT("sand")}}, true},
        {TEXT("cactus"), {{Below, TEXT("sand")}, {East, TEXT("stone")}}, false},
        {TEXT("cactus"), {{Below, TEXT("dirt")}}, false},
        {TEXT("sugar_cane"), {{Below, TEXT("sand")}, {Below + East, TEXT("water")}}, true},
        {TEXT("sugar_cane"), {{Below, TEXT("sand")}}, false},
        {TEXT("sugar_cane"), {{Below, TEXT("sugar_cane")}}, true},
        // Water plants: the cell holds the plant's own water.
        {TEXT("lily_pad"), {{Below, TEXT("water")}}, true},
        {TEXT("lily_pad"), {{Below, TEXT("ice")}}, true},
        {TEXT("lily_pad"), {{Below, TEXT("stone")}}, false},
        {TEXT("seagrass"), {{Below, TEXT("stone")}}, true},
        {TEXT("seagrass"), {{Below, TEXT("magma_block")}}, false},
        {TEXT("kelp_plant"), {{Below, TEXT("kelp_plant")}}, true},
        // Sturdy floors and anything-but-air.
        {TEXT("leaf_litter"), {{Below, TEXT("stone")}}, true},
        {TEXT("leaf_litter"), {{Below, TEXT("stone_slab[type=bottom]")}}, false},
        {TEXT("sea_pickle"), {{Below, TEXT("stone_slab[type=top]")}}, true},
        {TEXT("moss_carpet"), {{Below, TEXT("water")}}, true},
        {TEXT("moss_carpet"), {}, false},
        {TEXT("tube_coral"), {{Below, TEXT("stone")}}, true},
        {TEXT("tube_coral"), {}, false},
        // Attached: a sturdy face toward the plant.
        {TEXT("amethyst_cluster[facing=up]"), {{Below, TEXT("stone")}}, true},
        {TEXT("amethyst_cluster[facing=up]"), {{Above, TEXT("stone")}}, false},
        {TEXT("amethyst_cluster[facing=north]"), {{South, TEXT("stone")}}, true},
        {TEXT("amethyst_cluster[facing=north]"), {{North, TEXT("stone")}}, false},
        {TEXT("shelf_mushroom[facing=east]"), {{West, TEXT("stone")}}, true},
        {TEXT("hanging_roots"), {{Above, TEXT("stone")}}, true},
        {TEXT("hanging_roots"), {{Above, TEXT("stone_slab[type=bottom]")}}, true},
        {TEXT("hanging_roots"), {{Above, TEXT("stone_slab[type=top]")}}, false},
        {TEXT("spore_blossom"), {{Above, TEXT("stone")}}, true},
        {TEXT("spore_blossom"), {}, false},
        {TEXT("cave_vines"), {{Above, TEXT("cave_vines_plant")}}, true},
        {TEXT("pale_hanging_moss"), {{Above, TEXT("pale_oak_leaves")}}, true},
        {TEXT("cocoa[facing=north]"), {{North, TEXT("jungle_log")}}, true},
        {TEXT("cocoa[facing=north]"), {{North, TEXT("oak_log")}}, false},
        {TEXT("cocoa[facing=north]"), {{South, TEXT("jungle_log")}}, false},
        // Pointed dripstone: a sturdy face, or a column pointing the same way.
        {TEXT("pointed_dripstone[vertical_direction=up]"), {{Below, TEXT("stone")}}, true},
        {TEXT("pointed_dripstone[vertical_direction=up]"), {{Below, TEXT("pointed_dripstone[vertical_direction=up]")}},
            true},
        {TEXT("pointed_dripstone[vertical_direction=up]"),
            {{Below, TEXT("pointed_dripstone[vertical_direction=down]")}}, false},
        {TEXT("pointed_dripstone[vertical_direction=down]"), {{Above, TEXT("stone")}}, true},
        // Multiface: glow lichen and sculk vein need every face, a vine any one; a vine's side face may hang from the
        // vine above, and its up face needs the upper face of the block above (Q8).
        {TEXT("vine[north=true]"), {{North, TEXT("stone")}}, true},
        {TEXT("vine[north=true]"), {}, false},
        {TEXT("vine[north=true]"), {{Above, TEXT("vine[north=true]")}}, true},
        {TEXT("vine[north=true]"), {{Above, TEXT("vine[east=true]")}}, false},
        {TEXT("vine[north=true,east=true]"), {{North, TEXT("stone")}}, true},
        {TEXT("vine[north=true,east=true]"), {}, false},
        {TEXT("vine[up=true]"), {}, false},
        {TEXT("vine[up=true]"), {{Above, TEXT("stone")}}, true},
        {TEXT("vine[up=true]"), {{Above, TEXT("stone_slab[type=top]")}}, true},
        {TEXT("vine[up=true]"), {{Above, TEXT("stone_slab[type=bottom]")}}, false},
        {TEXT("vine[up=true]"), {{Above, TEXT("oak_leaves")}}, true},
        {TEXT("glow_lichen[north=true,east=true]"), {{North, TEXT("stone")}}, false},
        {TEXT("glow_lichen[down=true]"), {{Below, TEXT("stone")}}, true},
        {TEXT("glow_lichen[up=true]"), {{Above, TEXT("stone_slab[type=bottom]")}}, true},
        {TEXT("glow_lichen[up=true]"), {{Above, TEXT("stone_slab[type=top]")}}, false},
        {TEXT("sculk_vein[west=true]"), {{West, TEXT("glass")}}, true},
        {TEXT("mangrove_roots"), {}, true},
    };
    int32 Wrong = 0;
    for (const FRow& Row : Rows)
        if (Survives(Row.Plant, Row.Around) != Row.bSurvives)
        {
            ++Wrong;
            AddError(
                FString::Printf(TEXT("%s: expected %s"), Row.Plant, Row.bSurvives ? TEXT("survives") : TEXT("pops")));
        }
    TestEqual(TEXT("support rows that differ"), Wrong, 0);

    // The whole matrix (Jesus's answer to scout H's Q3): the reference's facts/plant_survival.json, the game's own
    // canSurvive for every imported state with one imported state (or air) at one neighbour and air elsewhere, then
    // below with water sources around that support, then below in the dark. The table reads no light, so the last
    // probe repeats its answer for the one below. Each difference is counted by its cause.
    FString Json;
    TSharedPtr<FJsonObject> Matrix;
    if (FFileHelper::LoadFileToString(
            Json, *(FPaths::ProjectDir() / TEXT("Reference/minecraft-blocks-26.3/facts/plant_survival.json"))))
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Matrix);
    if (!TestValid(TEXT("plant_survival.json"), Matrix)) return false;
    const FVoxelPlantSurvival& Table = *L.A.PlantSurvival;
    int32 StateLimit = 0, UnknownIds = 0;
    for (int32 Index = 0; Index < Registry.GetBlockCount(); ++Index)
        StateLimit =
            FMath::Max(StateLimit, Registry.GetBlockAt(Index).MinStateId + Registry.GetBlockAt(Index).StateCount);
    TArray<TBitArray<>> Signatures;
    for (const TSharedPtr<FJsonValue>& Value : Matrix->GetArrayField(TEXT("signatures")))
    {
        TBitArray<>& Set = Signatures.Emplace_GetRef(false, StateLimit);
        for (const auto& Members : Value->AsObject()->Values)
        {
            const FFoundationBlockType* Block = L.A.World.FindBlock(*Members.Key);
            if (!Block && ++UnknownIds) continue;
            if (Members.Value->Type == EJson::String)
                for (int32 Index = 0; Index < Block->StateCount; ++Index)
                    Set[Block->MinStateId + Index] = true;
            else
                for (const TSharedPtr<FJsonValue>& Member : Members.Value->AsArray())
                {
                    const int32 Index = int32(Member->AsNumber());
                    if (Index < 0 || Index >= Block->StateCount)
                        ++UnknownIds;
                    else
                        Set[Block->MinStateId + Index] = true;
                }
        }
    }
    const auto& Subjects = Matrix->GetObjectField(TEXT("subjects"))->Values;
    TArray<uint16> Candidates = {0};
    for (const auto& Subject : Subjects)
        if (const FFoundationBlockType* Block = L.A.World.FindBlock(*Subject.Key))
            for (int32 Index = 0; Index < Block->StateCount; ++Index)
                Candidates.Add(uint16(Block->MinStateId + Index));
    const TCHAR* const Probes[8] = {
        TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south"), TEXT("down"), TEXT("up"), TEXT("wet"), TEXT("dark")};
    const FIntVector Steps[6] = {West, East, North, South, Below, Above};
    const uint16 Water = Parse(TEXT("water")).Id;
    // Why the table differs from the game:
    // - sturdy faces: a rule that reads them, against a support whose faces the follow-up of Q2 measures (leaves,
    //   mud, soul sand, snow, powder snow, and the model blocks, read here as never sturdy);
    // - light: the mushrooms at light 15 (the table reads none);
    // - snow layers: their own rule, not in the table;
    // - otherwise the rule itself, S7's kinds or S8's.
    auto Cause = [&](uint16 Subject, int32 Probe, uint16 Candidate) -> FString
    {
        const FFoundationBlockType& Support = Registry.GetBlock(FFoundationBlockState{Candidate});
        const EVoxelSurvival Kind = Table.GetRule(Subject).Kind;
        if (Kind == EVoxelSurvival::Mushroom && (Probe == 4 || Probe == 6)) return TEXT("light");
        if (Registry.GetBlock(FFoundationBlockState{Subject}).Id == TEXT("snow")) return TEXT("snow layers");
        const bool bReadsSturdy = Kind == EVoxelSurvival::SturdyBelow || Kind == EVoxelSurvival::CactusFlower ||
            Kind == EVoxelSurvival::Underwater || Kind == EVoxelSurvival::Attached || Kind == EVoxelSurvival::Pointed ||
            Kind == EVoxelSurvival::Multiface;
        const bool bMeasuredLater =
            Registry.GetShape(FFoundationBlockState{Candidate}) == EFoundationBlockShape::Model ||
            Support.Placement == EFoundationPlacementRule::Leaves || Support.Id == TEXT("mud") ||
            Support.Id == TEXT("soul_sand") || Support.Id == TEXT("snow") || Support.Id == TEXT("powder_snow");
        if (bReadsSturdy && bMeasuredLater) return TEXT("sturdy faces (Q2)");
        const bool bS7 = Kind == EVoxelSurvival::Attached || Kind == EVoxelSurvival::TagAt ||
            Kind == EVoxelSurvival::Multiface || Kind == EVoxelSurvival::Pointed ||
            Kind == EVoxelSurvival::DripleafStem;
        return bS7 ? TEXT("S7 rule") : TEXT("S8 rule");
    };
    TMap<FString, int32> Differences, Causes;
    int64 Checked = 0;
    int32 SubjectStates = 0;
    for (const auto& Subject : Subjects)
    {
        const FFoundationBlockType* Block = L.A.World.FindBlock(*Subject.Key);
        if (!Block && ++UnknownIds) continue;
        // The torches: Blocks.Support checks their own table against the reference.
        if (Block->bTorchRule) continue;
        const TArray<TSharedPtr<FJsonValue>>& StateRows = Subject.Value->AsArray();
        TestEqual(FString::Printf(TEXT("%s rows"), *Subject.Key), StateRows.Num(), Block->StateCount);
        for (int32 Index = 0; Index < FMath::Min(StateRows.Num(), Block->StateCount); ++Index)
        {
            const uint16 State = uint16(Block->MinStateId + Index);
            const TArray<TSharedPtr<FJsonValue>>& Row = StateRows[Index]->AsArray();
            const bool bAlways = Table.GetRule(State).Kind == EVoxelSurvival::Always;
            ++SubjectStates;
            for (int32 Probe = 0; Probe < 8; ++Probe)
            {
                const TBitArray<>& Expected = Signatures[int32(Row[Probe]->AsNumber())];
                const FIntVector At = P + Steps[Probe < 6 ? Probe : 4];
                for (const uint16 Candidate : Candidates)
                {
                    const bool bOurs = bAlways ||
                        Table.Survives(State, P.X, P.Y, P.Z,
                            [&](int32 X, int32 Y, int32 Z) -> uint16
                            {
                                const FIntVector Cell(X, Y, Z);
                                if (Cell == P) return State;
                                if (Cell == At) return Candidate;
                                const bool bBeside =
                                    Cell.Z == At.Z && FMath::Abs(Cell.X - At.X) + FMath::Abs(Cell.Y - At.Y) == 1;
                                return Probe == 6 && bBeside ? Water : uint16(0);
                            });
                    ++Checked;
                    if (bOurs == Expected[Candidate]) continue;
                    const FString Why = Cause(State, Probe, Candidate);
                    ++Causes.FindOrAdd(Why);
                    ++Differences.FindOrAdd(FString::Printf(TEXT("%s: %s %s %s, the table says %s"), *Why, *Subject.Key,
                        Probes[Probe], *Registry.GetBlock(FFoundationBlockState{Candidate}).Id.ToString(),
                        bOurs ? TEXT("survives") : TEXT("pops")));
                }
            }
        }
    }
    TestEqual(TEXT("matrix block ids or state indices not in the registry"), UnknownIds, 0);
    Differences.KeySort(TLess<FString>());
    for (const TPair<FString, int32>& Difference : Differences)
        AddInfo(FString::Printf(TEXT("%s (%d)"), *Difference.Key, Difference.Value));
    for (const TPair<FString, int32>& Count : Causes)
        AddInfo(FString::Printf(TEXT("cause %s: %d"), *Count.Key, Count.Value));
    AddInfo(FString::Printf(TEXT("matrix: %d subject states, %lld answers"), SubjectStates, Checked));
    // Pinned (S7b, the manager's calls under Jesus's away rule): S7's kinds agree everywhere; the rest are known
    // differences, each named in Docs/Blocks.md with when it is due.
    TestEqual(TEXT("differences from the S7 rules"), Causes.FindRef(TEXT("S7 rule")), 0);
    TestEqual(TEXT("differences from sturdy faces (Q2's follow-up)"), Causes.FindRef(TEXT("sturdy faces (Q2)")), 20931);
    TestEqual(TEXT("differences from light (the mushrooms)"), Causes.FindRef(TEXT("light")), 10360);
    TestEqual(TEXT("differences from snow layers (main lane, with snow)"), Causes.FindRef(TEXT("snow layers")), 205680);
    // S8's rules (main lane, small fixes): pale moss carpet with bottom=false takes any floor; an upper half stands on
    // its own upper half; a mushroom stands on powder snow or a test instance block in the dark. The lily pad's (on a
    // water source held in a block, 1,521) went to 0 with Codex review 13 #1 (every waterlogged state holds water;
    // matrix run Tests/Evidence/2026-10-15/Native-20261015-020856).
    TMap<FString, int32> S8;
    for (const TPair<FString, int32>& Difference : Differences)
        if (Difference.Key.StartsWith(TEXT("S8 rule: ")))
            S8.FindOrAdd(Difference.Key.Mid(
                9, Difference.Key.Find(TEXT(" "), ESearchCase::CaseSensitive, ESearchDir::FromStart, 9) - 9)) +=
                Difference.Value;
    const TMap<FString, int32> KnownS8 = {{TEXT("pale_moss_carpet"), 1055349}, {TEXT("small_dripleaf"), 192},
        {TEXT("tall_grass"), 3}, {TEXT("large_fern"), 3}, {TEXT("sunflower"), 3}, {TEXT("lilac"), 3},
        {TEXT("rose_bush"), 3}, {TEXT("peony"), 3}, {TEXT("tall_seagrass"), 3}, {TEXT("brown_mushroom"), 2},
        {TEXT("red_mushroom"), 2}};
    TestTrue(TEXT("differences from the S8 rules: only the known ones"), S8.OrderIndependentCompareEqual(KnownS8));

    // The cascade. Dirt (hardness 0.5, 15 steps by hand) is the support that gets mined.
    auto Put = [&L, &Parse](FIntVector Cell, const TCHAR* Text) { L.A.World.Set(Cell, Parse(Text)); };
    auto Text = [&L](FIntVector Cell) { return L.A.World.GetRegistry().ToString(L.A.World.Get(Cell).State); };
    // From (-150, 50, 0) this aim meets the west face of (0, 0, 1).
    const FVector AtCell(200, 0, -12);
    const FIntVector Support(0, 0, 1);
    L.Hold(3);
    // A flower and a double plant on the mined block pop, the double plant with both halves.
    Put(Support, TEXT("dirt"));
    Put(Support + FIntVector(0, 0, 1), TEXT("dandelion"));
    L.Stand({-150, 50, 0}, AtCell);
    int64 Mark = L.A.GetEventSerial();
    TestTrue(TEXT("dirt under the flower mined"), MineSteps(L) > 0);
    TestTrue(TEXT("the flower pops"), !L.Solid(Support + FIntVector(0, 0, 1)) && L.A.Pops == 1);
    Put(Support, TEXT("dirt"));
    Put(Support + FIntVector(0, 0, 1), TEXT("tall_grass[half=lower]"));
    Put(Support + FIntVector(0, 0, 2), TEXT("tall_grass[half=upper]"));
    L.NextStep();
    L.Stand({-150, 50, 0}, AtCell);
    TestTrue(TEXT("dirt under the tall grass mined"), MineSteps(L) > 0);
    TestTrue(TEXT("both halves pop"),
        !L.Solid(Support + FIntVector(0, 0, 1)) && !L.Solid(Support + FIntVector(0, 0, 2)) && L.A.Pops == 3);
    // A stack pops to its end (Jesus's answer to Q1): sugar cane, and a cactus with its flower, above the mined block.
    Put(Support, TEXT("dirt"));
    Put(Support + FIntVector(0, 1, 0), TEXT("water"));
    for (int32 Z = 1; Z <= 3; ++Z)
        Put(Support + FIntVector(0, 0, Z), TEXT("sugar_cane"));
    L.NextStep();
    L.Stand({-150, 50, 0}, AtCell);
    int32 PopsBefore = L.A.Pops;
    TestTrue(TEXT("dirt under the cane mined"), MineSteps(L) > 0);
    TestTrue(TEXT("every segment of the cane pops"),
        !L.Solid(Support + FIntVector(0, 0, 1)) && !L.Solid(Support + FIntVector(0, 0, 2)) &&
            !L.Solid(Support + FIntVector(0, 0, 3)) && L.A.Pops == PopsBefore + 3);
    L.A.World.Set(Support + FIntVector(0, 1, 0), {});
    Put(Support, TEXT("sand"));
    Put(Support + FIntVector(0, 0, 1), TEXT("cactus"));
    Put(Support + FIntVector(0, 0, 2), TEXT("cactus"));
    Put(Support + FIntVector(0, 0, 3), TEXT("cactus_flower"));
    L.NextStep();
    L.Stand({-150, 50, 0}, AtCell);
    PopsBefore = L.A.Pops;
    TestTrue(TEXT("sand under the cactus mined"), MineSteps(L) > 0);
    TestTrue(TEXT("the cactus and its flower pop"),
        !L.Solid(Support + FIntVector(0, 0, 1)) && !L.Solid(Support + FIntVector(0, 0, 3)) &&
            L.A.Pops == PopsBefore + 3);
    // Down a stack: a vine hanging under a vine goes with it. Kept clear of z 0: a plant there reads the unknown cell
    // below it and stays.
    const FIntVector Hanger(0, 0, 2);
    Put(Hanger, TEXT("dirt"));
    Put(Hanger + FIntVector(0, 1, 0), TEXT("vine[north=true]"));
    Put(Hanger + FIntVector(0, 1, -1), TEXT("vine[north=true]"));
    L.NextStep();
    // This aim meets the west face of (0, 0, 2).
    L.Stand({-150, 50, 0}, FVector(200, 0, 88));
    PopsBefore = L.A.Pops;
    TestTrue(TEXT("the hanging vine's block mined"), MineSteps(L) > 0 && !L.Solid(Hanger));
    TestTrue(TEXT("the vine on it and the vine under it pop"),
        !L.Solid(Hanger + FIntVector(0, 1, 0)) && !L.Solid(Hanger + FIntVector(0, 1, -1)) &&
            L.A.Pops == PopsBefore + 2);
    // Multiface: a vine on two blocks loses the face of the mined one; glow lichen the same.
    Put(Support, TEXT("dirt"));
    Put(FIntVector(1, 1, 1), TEXT("dirt"));
    Put(FIntVector(0, 1, 1), TEXT("vine[north=true,east=true]"));
    L.NextStep();
    L.Stand({-150, 50, 0}, AtCell);
    TestTrue(TEXT("the vine's north block mined"), MineSteps(L) > 0);
    TestEqual(TEXT("the vine keeps its east face"), Text({0, 1, 1}), Registry.ToString(Parse(TEXT("vine[east=true]"))));
    Put(Support, TEXT("dirt"));
    Put(FIntVector(0, 1, 1), TEXT("glow_lichen[north=true,down=true]"));
    Put(FIntVector(0, 1, 0), TEXT("dirt"));
    L.NextStep();
    L.Stand({-150, 50, 0}, AtCell);
    TestTrue(TEXT("the lichen's north block mined"), MineSteps(L) > 0);
    TestEqual(TEXT("the lichen keeps its floor face"), Text({0, 1, 1}),
        Registry.ToString(Parse(TEXT("glow_lichen[down=true]"))));
    // Placing beside a cactus pops it.
    L.A.World.Set({0, 1, 1}, {});
    Put({3, 0, 0}, TEXT("sand"));
    Put({3, 0, 1}, TEXT("cactus"));
    Put({2, 0, 0}, TEXT("dirt"));
    L.P().Inventory.SetSlot(6, L.P().Inventory.MakeStack(L.A.Rules.FindItem(TEXT("stone")), 64));
    L.Hold(6);
    L.NextStep();
    L.Stand({-150, 50, 0}, FVector(400, 0, -62));
    FFoundationResult R = L.Use();
    TestTrue(TEXT("a block placed beside the cactus"), R.bPlaced && R.Cell == FIntVector(2, 0, 1));
    TestFalse(TEXT("pops the cactus"), L.Solid({3, 0, 1}));
    // Unknown cells: the ground below z 0 is outside the bounds. A flower there never pops, and none is placed there.
    Put({9, 1, 0}, TEXT("dandelion"));
    Put({9, 0, 0}, TEXT("dirt"));
    PopsBefore = L.A.Pops;
    L.Hold(3);
    L.NextStep();
    L.Stand({750, 50, 0}, FVector(150, 0, -112));
    TestTrue(TEXT("the flower's neighbour mined"), MineSteps(L) > 0 && !L.Solid({9, 0, 0}));
    TestTrue(TEXT("a flower over an unknown cell stays"), L.Solid({9, 1, 0}) && L.A.Pops == PopsBefore);
    FFoundationHit Floor;
    Floor.Kind = EFoundationHitKind::Static;
    Floor.StaticIndex = 0;
    Floor.Cell = FIntVector(12, 0, -1);
    Floor.Face = EFoundationFace::Up;
    Floor.bInRange = true;
    Floor.Point = FVector(1250, 50, 0);
    const int32 Flower = L.A.Rules.FindItem(TEXT("dandelion"));
    L.P().Inventory.SetSlot(5, L.P().Inventory.MakeStack(Flower, 64));
    L.Hold(5);
    FIntVector Destination;
    TestEqual(TEXT("a flower on the lab floor (unknown below) is refused"),
        L.A.CheckPlacement(L.P(), Main, Floor, Destination), EFoundationReason::CannotSurvive);
    return true;
}

// Breaking or popping a block that holds water leaves its source (water and sky W2b; minecraft.wiki "Waterlogging"),
// through the command path: a waterlogged slab (the property) and seagrass (always water) broken by hand leave
// water[level=0], and so does seagrass that pops when the block under it is broken; a dry slab leaves air.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractionWaterRemainsTest, "Unnamed.Foundation.Interaction.WaterRemains",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationInteractionWaterRemainsTest::RunTest(const FString& Parameters)
{
    auto Text = [](FLab& L, FIntVector Cell) { return L.A.World.GetRegistry().ToString(L.A.World.Get(Cell).State); };
    auto Put = [](FLab& L, FIntVector Cell, const TCHAR* State)
    {
        FFoundationBlockState Parsed;
        const bool bParsed = L.A.World.GetRegistry().Parse(State, Parsed);
        L.A.World.Set(Cell, Parsed);
        return bParsed;
    };
    const FIntVector Ground(0, 0, 0), Above(0, 0, 1);
    struct FCase
    {
        const TCHAR* Block;
        const TCHAR* Left;
    };
    // Mined from above: the eye two cells over the block, looking straight down.
    const FCase Broken[] = {{TEXT("stone_slab[type=bottom,waterlogged=true]"), TEXT("water[level=0]")},
        {TEXT("seagrass"), TEXT("water[level=0]")}, {TEXT("stone_slab[type=bottom,waterlogged=false]"), TEXT("air")}};
    for (const FCase& Case : Broken)
    {
        FLab L;
        if (!TestTrue(FString::Printf(TEXT("%s parses"), Case.Block), Put(L, Ground, Case.Block))) continue;
        L.Stand(FVector(50, 50, 200), DownVector);
        TestTrue(FString::Printf(TEXT("%s breaks"), Case.Block), MineSteps(L) >= 0);
        TestEqual(FString::Printf(TEXT("broken %s leaves"), Case.Block), Text(L, Ground), FString(Case.Left));
    }
    // A pop: seagrass on stone, the stone mined from the side (the eye level with the stone's middle).
    FLab L;
    Put(L, Ground, TEXT("stone"));
    Put(L, Above, TEXT("seagrass"));
    L.Stand(FVector(-150, 50, 50 - 162), FVector(1, 0, 0));
    const int64 Serial = L.A.GetEventSerial();
    TestTrue(TEXT("the stone under seagrass breaks"), MineSteps(L) >= 0);
    int32 Pops = 0;
    for (const FFoundationEvent& Event : L.A.GetEvents())
        Pops += Event.Serial > Serial && Event.Kind == EFoundationEventKind::Popped;
    TestEqual(TEXT("the seagrass pops"), Pops, 1);
    TestEqual(TEXT("the broken stone leaves air"), Text(L, Ground), FString(TEXT("air")));
    TestEqual(TEXT("the popped seagrass leaves its water"), Text(L, Above), FString(TEXT("water[level=0]")));
    return true;
}
#endif
