#include "Misc/AutomationTest.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationFireContact.h"
#include "Foundation/FoundationGrid.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    // A player's standing body (0.6 x 1.8 blocks) with its feet at Feet, world cm.
    FBox BodyAt(const FVector& Feet)
    {
        return FBox(Feet - FVector(30, 30, 0), Feet + FVector(30, 30, 180));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationFireContactTest, "Unnamed.Foundation.Vitals.FireContact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationFireContactTest::RunTest(const FString&)
{
    // Survival step 16c: what a body touches among fire, soul fire and a lava cauldron's lava (the 26.3 blocks).
    FFoundationBlockWorld World;
    World.SetRegistry(FFoundationBlockRegistry::GetDefault());
    World.BoundsMin = FIntVector(-8, -8, 0);
    World.BoundsMax = FIntVector(7, 7, 7);
    const FFoundationBlockType* Fire = World.FindBlock(TEXT("fire"));
    const FFoundationBlockType* SoulFire = World.FindBlock(TEXT("soul_fire"));
    const FFoundationBlockType* LavaCauldron = World.FindBlock(TEXT("lava_cauldron"));
    if (!TestTrue(TEXT("the 26.3 blocks exist"), Fire && SoulFire && LavaCauldron)) return false;
    auto Put = [&](const FIntVector& Cell, const FFoundationBlockType* Block)
    { World.Set(Cell, Block ? Block->GetDefaultState() : FFoundationBlockState()); };
    using FoundationFireContact::Get;

    // C1: feet in a fire cell.
    Put({0, 0, 1}, Fire);
    TestEqual(TEXT("C1: feet in fire"), Get(World, BodyAt({50, 50, 100})).FireDamage, 1);
    TestFalse(TEXT("C1: no lava"), Get(World, BodyAt({50, 50, 100})).bLavaCauldron);
    // C2: beside the cell with a 0.05 cm gap: the 0.1 cm shrink keeps it out.
    TestEqual(TEXT("C2: beside, 0.05 cm off"), Get(World, BodyAt({-30.05, 50, 100})).FireDamage, 0);
    TestEqual(TEXT("C2: overlapping by 1 cm"), Get(World, BodyAt({-29, 50, 100})).FireDamage, 1);
    // C3: soul fire above a block, the body standing in it: 2.
    Put({0, 0, 1}, SoulFire);
    TestEqual(TEXT("C3: soul fire"), Get(World, BodyAt({50, 50, 100})).FireDamage, 2);
    // C4: fire and soul fire both touched: the hotter wins (provisional), whichever cell is read first.
    Put({1, 0, 1}, Fire);
    TestEqual(TEXT("C4: soul fire beside fire"), Get(World, BodyAt({100, 50, 100})).FireDamage, 2);
    Put({0, 0, 1}, Fire);
    Put({1, 0, 1}, SoulFire);
    TestEqual(TEXT("C4: fire beside soul fire"), Get(World, BodyAt({100, 50, 100})).FireDamage, 2);
    Put({0, 0, 1}, nullptr);
    Put({1, 0, 1}, nullptr);
    // C5 and C6: a lava cauldron at (0, 0, 0): on its rim (feet at the cell's top) nothing; on its floor (4/16) lava.
    Put({0, 0, 0}, LavaCauldron);
    const FoundationFireContact::FContact Rim = Get(World, BodyAt({50, 50, 100}));
    TestTrue(TEXT("C5: on the rim, nothing"), !Rim.bLavaCauldron && Rim.FireDamage == 0);
    TestTrue(TEXT("C6: on the floor, lava"), Get(World, BodyAt({50, 50, 25})).bLavaCauldron);
    // Inside the rim's wall only (the lava starts 2/16 in): nothing.
    TestFalse(TEXT("C6: within the wall only"), Get(World, BodyAt({-20, 50, 25})).bLavaCauldron);
    // With the world moved (rebasing), the cells move with it.
    World.OriginCm = FVector(1000, -500, 0);
    TestTrue(TEXT("moved origin"), Get(World, BodyAt({1050, -450, 25})).bLavaCauldron);
    TestFalse(TEXT("moved origin: the old place is empty"), Get(World, BodyAt({50, 50, 25})).bLavaCauldron);
    return true;
}
#endif
