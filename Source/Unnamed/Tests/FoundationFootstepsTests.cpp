#include "Misc/AutomationTest.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationBreakAssets.h"
#include "Foundation/FoundationFootsteps.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationSoundAssets.h"
#include "UnnamedProjectSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags FootstepTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    using FoundationFootsteps::EStep;

    // Walks Blocks straight at SpeedCmPerStep per reference step; counts the steps that play.
    int32 Walk(FoundationFootsteps::FState& State, double Blocks, double SpeedCmPerStep,
        const FoundationFootsteps::FFacts& Base)
    {
        int32 Played = 0;
        double Left = Blocks * UFoundationProfile::BlockCm;
        while (Left > 1e-9)
        {
            FoundationFootsteps::FFacts Facts = Base;
            Facts.HorizontalCm = FMath::Min(SpeedCmPerStep, Left);
            Left -= Facts.HorizontalCm;
            Played += FoundationFootsteps::Advance(State, Facts) == EStep::Play;
        }
        return Played;
    }
} // namespace

// The cadence (provisional, wiki): distance x 0.6, a step each time it passes the next whole number (strictly), per
// distance, not time; sneaking and swimming pass due steps silently, a jump keeps one due until it lands, flying walks
// nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationFootstepsCadenceTest, "Unnamed.Foundation.Footsteps.Cadence", FootstepTestFlags)

bool FFoundationFootstepsCadenceTest::RunTest(const FString& Parameters)
{
    FoundationFootsteps::FFacts Ground;
    Ground.bGrounded = true;
    // Walking 4.317 m/s and sprinting 5.612 m/s at 20 steps a second.
    const double WalkCm = 431.7 / 20, SprintCm = 561.2 / 20;
    {
        FoundationFootsteps::FState State;
        // 9.9 blocks: 5.94 walked; steps past 1, 2, 3, 4 and 5.
        TestEqual(TEXT("9.9 blocks walking: 5 steps"), Walk(State, 9.9, WalkCm, Ground), 5);
        TestEqual(TEXT("next step at 6"), State.NextStep, 6.);
        TestEqual(TEXT("0.2 further (6.06): the 6th"), Walk(State, .2, WalkCm, Ground), 1);
    }
    {
        FoundationFootsteps::FState State;
        TestEqual(TEXT("9.9 blocks sprinting: the same 5"), Walk(State, 9.9, SprintCm, Ground), 5);
    }
    {
        // 99.5 blocks: 59.7 walked, 59 steps; a step every 1/0.6 blocks.
        FoundationFootsteps::FState State;
        TestEqual(TEXT("99.5 blocks: 59 steps"), Walk(State, 99.5, WalkCm, Ground), 59);
    }
    {
        FoundationFootsteps::FState State;
        FoundationFootsteps::FFacts Sneak = Ground;
        Sneak.bSneaking = true;
        TestEqual(TEXT("sneaking 9.9 blocks: silent"), Walk(State, 9.9, WalkCm, Sneak), 0);
        // Leaving sneak plays no backlog of the 5 silenced steps: only the next whole number (6.06) plays.
        TestEqual(TEXT("after sneaking: one step, no burst"), Walk(State, .2, WalkCm, Ground), 1);
    }
    {
        FoundationFootsteps::FState State;
        FoundationFootsteps::FFacts Swim = Ground;
        Swim.bInWater = true;
        TestEqual(TEXT("in water: silent"), Walk(State, 10, WalkCm, Swim), 0);
    }
    {
        FoundationFootsteps::FState State;
        FoundationFootsteps::FFacts Fly = Ground;
        Fly.bFlying = true;
        Fly.bGrounded = false;
        TestEqual(TEXT("flying: nothing"), Walk(State, 10, WalkCm, Fly), 0);
        TestEqual(TEXT("flying walks no distance"), State.Distance, 0.);
    }
    {
        // A jump: the step falls due in the air and plays on landing, once.
        FoundationFootsteps::FState State;
        FoundationFootsteps::FFacts Air;
        TestEqual(TEXT("in the air: waits"), Walk(State, 2, WalkCm, Air), 0);
        FoundationFootsteps::FFacts Land = Ground;
        TestTrue(TEXT("landing plays the due step"), FoundationFootsteps::Advance(State, Land) == EStep::Play);
        TestTrue(TEXT("standing still: no more"), FoundationFootsteps::Advance(State, Land) == EStep::None);
    }
    return true;
}

// What the step stands on: the cell 0.2 blocks under the feet; a carpet or snow layer with its support below
// (combination); a fence under air; nothing over air.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationFootstepsBlocksTest, "Unnamed.Foundation.Footsteps.BlockUnderFeet", FootstepTestFlags)

bool FFoundationFootstepsBlocksTest::RunTest(const FString& Parameters)
{
    FFoundationBlockWorld World;
    World.SetRegistry(FFoundationBlockRegistry::GetDefault());
    World.BoundsMin = FIntVector(-1, -1, -1);
    World.BoundsMax = FIntVector(16, 4, 8);
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    auto State = [&Registry](const TCHAR* Text)
    {
        FFoundationBlockState Parsed;
        Registry.Parse(Text, Parsed);
        return Parsed;
    };
    auto Id = [&World](FFoundationBlockState S) { return World.GetBlock(S).Id.ToString(); };
    const double Cm = UFoundationProfile::BlockCm;
    auto Feet = [Cm](int32 X, double HeightBlocks) { return FVector((X + .5) * Cm, .5 * Cm, HeightBlocks * Cm); };
    FoundationFootsteps::FStepBlocks Found;

    // Stone floor, standing on it.
    World.Set({0, 0, 0}, State(TEXT("stone")));
    if (TestTrue(TEXT("stone: found"), FoundationFootsteps::FindStepBlocks(World, Feet(0, 1), Found)))
        TestTrue(TEXT("stone: stone, no combination"), Id(Found.Primary) == TEXT("stone") && !Found.bCombination);
    // A bottom slab: the feet at half a block, inside the slab's cell.
    World.Set({1, 0, 0}, State(TEXT("stone_slab[type=bottom]")));
    if (TestTrue(TEXT("bottom slab: found"), FoundationFootsteps::FindStepBlocks(World, Feet(1, .5), Found)))
        TestEqual(TEXT("bottom slab: the slab"), Id(Found.Primary), FString(TEXT("stone_slab")));
    // A top slab, its top at the cell's top.
    World.Set({2, 0, 0}, State(TEXT("oak_slab[type=top]")));
    if (TestTrue(TEXT("top slab: found"), FoundationFootsteps::FindStepBlocks(World, Feet(2, 1), Found)))
        TestEqual(TEXT("top slab: the slab"), Id(Found.Primary), FString(TEXT("oak_slab")));
    // A moss carpet on stone (wool carpets are not in the block set yet): the carpet first, then the stone
    // (combination).
    World.Set({3, 0, 0}, State(TEXT("stone")));
    World.Set({3, 0, 1}, State(TEXT("moss_carpet")));
    if (TestTrue(TEXT("carpet: found"), FoundationFootsteps::FindStepBlocks(World, Feet(3, 1 + 1. / 16), Found)))
        TestTrue(TEXT("carpet: carpet over stone"),
            Found.bCombination && Id(Found.Primary) == TEXT("moss_carpet") && Id(Found.Under) == TEXT("stone"));
    // One snow layer on grass: combination; four layers: the snow itself.
    World.Set({4, 0, 0}, State(TEXT("grass_block")));
    World.Set({4, 0, 1}, State(TEXT("snow[layers=1]")));
    if (TestTrue(TEXT("snow 1: found"), FoundationFootsteps::FindStepBlocks(World, Feet(4, 1.125), Found)))
        TestTrue(TEXT("snow 1: snow over grass"),
            Found.bCombination && Id(Found.Primary) == TEXT("snow") && Id(Found.Under) == TEXT("grass_block"));
    World.Set({5, 0, 0}, State(TEXT("grass_block")));
    World.Set({5, 0, 1}, State(TEXT("snow[layers=4]")));
    if (TestTrue(TEXT("snow 4: found"), FoundationFootsteps::FindStepBlocks(World, Feet(5, 1.5), Found)))
        TestTrue(TEXT("snow 4: the snow alone"), !Found.bCombination && Id(Found.Primary) == TEXT("snow"));
    // On top of a fence post (1.5 blocks): the cell under the feet is air, the fence below it.
    World.Set({6, 0, 0}, State(TEXT("oak_fence")));
    if (TestTrue(TEXT("fence: found"), FoundationFootsteps::FindStepBlocks(World, Feet(6, 1.5), Found)))
        TestEqual(TEXT("fence: the fence"), Id(Found.Primary), FString(TEXT("oak_fence")));
    // Over air (a jump's peak): nothing.
    TestFalse(TEXT("over air: nothing"), FoundationFootsteps::FindStepBlocks(World, Feet(0, 2), Found));
    return true;
}

namespace
{
    // A small world for the step lookups below, with a parser that reports blocks missing from the set.
    struct FFootstepsLookupWorld
    {
        FFoundationBlockWorld World;
        FAutomationTestBase& Test;

        explicit FFootstepsLookupWorld(FAutomationTestBase& InTest) : Test(InTest)
        {
            World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            World.BoundsMin = FIntVector(-1, -1, -1);
            World.BoundsMax = FIntVector(16, 4, 8);
        }
        void Set(const FIntVector& Cell, const TCHAR* Text)
        {
            FFoundationBlockState State;
            Test.TestTrue(
                FString::Printf(TEXT("%s is in the block set"), Text), World.GetRegistry().Parse(Text, State));
            World.Set(Cell, State);
        }
        FString Id(FFoundationBlockState State) const { return World.GetBlock(State).Id.ToString(); }
    };
} // namespace

// R38-3: a block of #inside_step_sound_blocks the feet are in plays instead of the block below (minecraft.wiki, Block
// tag (Java Edition), inside_step_sound_blocks): a lily pad over water, pink petals on grass.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationFootstepsInsideTest, "Unnamed.Foundation.Footsteps.InsideStepBlocks", FootstepTestFlags)

bool FFoundationFootstepsInsideTest::RunTest(const FString& Parameters)
{
    FFootstepsLookupWorld Lookup(*this);
    const double Cm = UFoundationProfile::BlockCm;
    FoundationFootsteps::FStepBlocks Found;
    // A lily pad (1.5/16 tall) on water: the feet on its top, the water 0.2 blocks under them.
    Lookup.Set({0, 0, 0}, TEXT("water"));
    Lookup.Set({0, 0, 1}, TEXT("lily_pad"));
    if (TestTrue(TEXT("lily pad: found"),
            FoundationFootsteps::FindStepBlocks(Lookup.World, FVector(.5 * Cm, .5 * Cm, (1 + 1.5 / 16) * Cm), Found)))
        TestTrue(
            TEXT("lily pad: the lily pad alone"), Lookup.Id(Found.Primary) == TEXT("lily_pad") && !Found.bCombination);
    // Pink petals on grass: the feet on the grass, inside the petals' cell.
    Lookup.Set({1, 0, 0}, TEXT("grass_block"));
    Lookup.Set({1, 0, 1}, TEXT("pink_petals"));
    if (TestTrue(TEXT("petals: found"),
            FoundationFootsteps::FindStepBlocks(Lookup.World, FVector(1.5 * Cm, .5 * Cm, Cm), Found)))
        TestTrue(TEXT("petals: the petals, not the grass"),
            Lookup.Id(Found.Primary) == TEXT("pink_petals") && !Found.bCombination);
    return true;
}

#endif
