#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationSimulation.h"
#include "Foundation/FoundationTags.h"
#include "HAL/PlatformTime.h"

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
        void SetBlocks(TArray<FFoundationBlockDefinition> Extra) { A.World.SetRegistry(MakeRegistry(Extra)); }

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
#endif
