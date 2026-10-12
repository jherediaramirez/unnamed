#include "Misc/AutomationTest.h"
#include "Engine/Texture2DArray.h"
#include "Foundation/FoundationBlockImport.h"
#include "Foundation/FoundationBreakAssets.h"
#include "Foundation/FoundationBreakEffects.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Sound/SoundWave.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags BreakTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr double BreakTolerance = 1e-6;
    constexpr int32 BreakTexels = 256;

    bool BreakNear(double A, double B, double Tol = BreakTolerance)
    {
        return FMath::Abs(A - B) <= Tol;
    }

    bool BreakCheckNear(
        FAutomationTestBase& Test, const TCHAR* What, double Actual, double Expected, double Tol = BreakTolerance)
    {
        return Test.TestTrue(
            FString::Printf(TEXT("%s: %.9g, expected %.9g"), What, Actual, Expected), BreakNear(Actual, Expected, Tol));
    }

    bool BreakCheckInt(FAutomationTestBase& Test, const TCHAR* What, int64 Actual, int64 Expected)
    {
        return Test.TestTrue(
            FString::Printf(TEXT("%s: %lld, expected %lld"), What, Actual, Expected), Actual == Expected);
    }

    struct FBreakCarveMask
    {
        TArray<bool> Crack;
        TArray<uint8> Grey;

        FBreakCarveMask()
        {
            Crack.Init(false, BreakTexels);
            Grey.SetNumZeroed(BreakTexels);
        }
        void Set(int32 X, int32 Y) { Crack[Y * 16 + X] = true; }
        void Carve() { FoundationBreak::CarveStage(Crack, Grey); }
        int32 At(int32 X, int32 Y) const { return Grey[Y * 16 + X]; }
    };

    // One destroy_stage PNG: its crack mask (alpha > 127) and greys.
    bool LoadBreakStage(FAutomationTestBase& Test, int32 Stage, TArray<bool>& OutCrack, TArray<uint8>& OutGrey)
    {
        const FString Path = FPaths::ProjectDir() /
            FString::Printf(TEXT("Reference/minecraft-blocks-26.3/assets/minecraft/textures/block/"
                                 "destroy_stage_%d.png"),
                Stage);
        TArray<uint8> Pixels;
        int32 Width = 0, Height = 0;
        if (!UFoundationBlockImport::DecodePngFile(Path, Pixels, Width, Height) || Width != 16 || Height != 16)
        {
            Test.AddError(FString::Printf(TEXT("destroy_stage_%d.png did not decode as 16 x 16"), Stage));
            return false;
        }
        OutCrack.SetNum(BreakTexels);
        OutGrey.SetNum(BreakTexels);
        for (int32 Texel = 0; Texel < BreakTexels; ++Texel)
        {
            OutCrack[Texel] = Pixels[Texel * 4 + 3] > 127;
            OutGrey[Texel] = Pixels[Texel * 4];
        }
        return true;
    }

    // One simulated frame sequence: the hit steps collected over the frames (Times in seconds).
    TArray<int64> BreakCollectOverFrames(int32 Mode, const TArray<double>& Times, int32& OutMostInFrame)
    {
        TArray<int64> Hits;
        TArray<int64, TInlineAllocator<4>> Frame;
        int64 Previous = -1;
        OutMostInFrame = 0;
        for (const double Time : Times)
        {
            const int64 Step = int64(FMath::FloorToDouble(Time * 20));
            FoundationBreak::CollectHitSteps(Mode, 0, 0, Previous, Step, Frame);
            OutMostInFrame = FMath::Max(OutMostInFrame, Frame.Num());
            Hits.Append(Frame);
            Previous = Step;
        }
        return Hits;
    }

    TArray<int64> BreakExpectedHits(int32 Mode, int64 LastStep)
    {
        TArray<int64> Hits;
        for (int64 Step = Mode == 2 ? 3 : 0; Step <= LastStep; Step += Mode == 2 ? 6 : 4)
            Hits.Add(Step);
        return Hits;
    }

    FFoundationBreakSoundEvent BreakMakeEvent()
    {
        FFoundationBreakSoundEvent Event;
        const float Volumes[] = {1.f, 0.5f, 0.8f};
        const float Pitches[] = {1.f, 0.9f, 1.2f};
        for (int32 Index = 0; Index < 3; ++Index)
        {
            FFoundationBreakSoundFile& File = Event.Files.AddDefaulted_GetRef();
            File.Volume = Volumes[Index];
            File.Pitch = Pitches[Index];
        }
        return Event;
    }

    const FFoundationBreakBlock* BreakFindAsset(const UFoundationBreakAssets& Assets, FName Id)
    {
        return Assets.Blocks.FindByPredicate([Id](const FFoundationBreakBlock& Block) { return Block.Id == Id; });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBreakStagesTest, "Unnamed.Foundation.Break.Stages", BreakTestFlags)

bool FFoundationBreakStagesTest::RunTest(const FString&)
{
    const double Progress[] = {0, 0.05, 0.0999, 0.1, 0.35, 0.95, 1.0, 1.5, -0.1};
    const int32 Stage[] = {0, 0, 0, 1, 3, 9, 9, 9, 0};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Progress); ++Index)
        BreakCheckInt(*this, *FString::Printf(TEXT("CrackStage(%g)"), Progress[Index]),
            FoundationBreak::CrackStage(Progress[Index]), Stage[Index]);
    BreakCheckNear(*this, TEXT("CrackFade(0.35)"), FoundationBreak::CrackFade(0.35), 0.5);
    BreakCheckNear(*this, TEXT("CrackFade(0.95)"), FoundationBreak::CrackFade(0.95), 0);
    BreakCheckNear(*this, TEXT("CrackFade(0.1)"), FoundationBreak::CrackFade(0.1), 0);
    TestTrue(TEXT("CrackFade stays below 1"), FoundationBreak::CrackFade(0.0999999999) < 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBreakCarveTest, "Unnamed.Foundation.Break.Carve", BreakTestFlags)

bool FFoundationBreakCarveTest::RunTest(const FString&)
{
    {
        FBreakCarveMask Mask;
        Mask.Set(5, 5);
        Mask.Carve();
        BreakCheckInt(*this, TEXT("crack"), Mask.At(5, 5), 60);
        BreakCheckInt(*this, TEXT("right of the crack: lit"), Mask.At(6, 5), 170);
        BreakCheckInt(*this, TEXT("below the crack: lit"), Mask.At(5, 6), 170);
        BreakCheckInt(*this, TEXT("left of the crack: shadow"), Mask.At(4, 5), 90);
        BreakCheckInt(*this, TEXT("above the crack: shadow"), Mask.At(5, 4), 90);
        BreakCheckInt(*this, TEXT("diagonal"), Mask.At(6, 6), 128);
        BreakCheckInt(*this, TEXT("far texel"), Mask.At(12, 12), 128);
    }
    {
        // (4,5) is right of one crack and left of another: shadow wins.
        FBreakCarveMask Mask;
        Mask.Set(3, 5);
        Mask.Set(5, 5);
        Mask.Carve();
        BreakCheckInt(*this, TEXT("both sides"), Mask.At(4, 5), 90);
        BreakCheckInt(*this, TEXT("lit past the first crack"), Mask.At(3, 6), 170);
    }
    {
        FBreakCarveMask Mask;
        Mask.Set(0, 0);
        Mask.Set(15, 15);
        Mask.Carve();
        BreakCheckInt(*this, TEXT("(0,0)"), Mask.At(0, 0), 60);
        BreakCheckInt(*this, TEXT("(15,15)"), Mask.At(15, 15), 60);
        BreakCheckInt(*this, TEXT("(1,0) lit"), Mask.At(1, 0), 170);
        BreakCheckInt(*this, TEXT("(0,1) lit"), Mask.At(0, 1), 170);
        BreakCheckInt(*this, TEXT("(14,15) shadow"), Mask.At(14, 15), 90);
        BreakCheckInt(*this, TEXT("(15,14) shadow"), Mask.At(15, 14), 90);
        BreakCheckInt(*this, TEXT("(15,0) does not wrap"), Mask.At(15, 0), 128);
        BreakCheckInt(*this, TEXT("(0,15) does not wrap"), Mask.At(0, 15), 128);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationBreakCarveReferenceTest, "Unnamed.Foundation.Break.CarveReference", BreakTestFlags)

bool FFoundationBreakCarveReferenceTest::RunTest(const FString&)
{
    const int32 Counts[FoundationBreak::Stages] = {4, 10, 18, 28, 41, 55, 70, 100, 129, 145};
    for (int32 Stage = 0; Stage < FoundationBreak::Stages; ++Stage)
    {
        TArray<bool> Crack;
        TArray<uint8> Grey;
        if (!LoadBreakStage(*this, Stage, Crack, Grey)) return false;
        int32 Cracks = 0, OddGreys = 0;
        for (int32 Texel = 0; Texel < BreakTexels; ++Texel)
            if (Crack[Texel])
            {
                ++Cracks;
                OddGreys += Grey[Texel] != 61 && Grey[Texel] != 155;
            }
        BreakCheckInt(*this, *FString::Printf(TEXT("stage %d crack texels"), Stage), Cracks, Counts[Stage]);
        BreakCheckInt(*this, *FString::Printf(TEXT("stage %d greys outside {61, 155}"), Stage), OddGreys, 0);

        TArray<uint8> Carved, Plain;
        Carved.SetNumZeroed(BreakTexels);
        Plain.SetNumZeroed(BreakTexels);
        FoundationBreak::CarveStage(Crack, Carved);
        FoundationBreak::PlainStage(Crack, Grey, Plain);
        int32 CrackGreys = 0, Strange = 0, PlainWrong = 0;
        for (int32 Texel = 0; Texel < BreakTexels; ++Texel)
        {
            const uint8 Value = Carved[Texel];
            CrackGreys += Value == 60;
            Strange += Value != 60 && Value != 90 && Value != 128 && Value != 170;
            PlainWrong += Plain[Texel] != (Crack[Texel] ? Grey[Texel] : 128);
        }
        BreakCheckInt(*this, *FString::Printf(TEXT("stage %d carved cracks"), Stage), CrackGreys, Cracks);
        BreakCheckInt(*this, *FString::Printf(TEXT("stage %d carved odd greys"), Stage), Strange, 0);
        BreakCheckInt(*this, *FString::Printf(TEXT("stage %d plain texels"), Stage), PlainWrong, 0);
    }

    // Stage 0 read from the PNG by hand: cracks at (7,7)=61, (7,8)=155, (8,8)=61, (8,9)=155.
    TArray<bool> Crack;
    TArray<uint8> Grey;
    if (!LoadBreakStage(*this, 0, Crack, Grey)) return false;
    BreakCheckInt(*this, TEXT("stage 0 (7,7) grey"), Grey[7 * 16 + 7], 61);
    BreakCheckInt(*this, TEXT("stage 0 (7,8) grey"), Grey[8 * 16 + 7], 155);
    BreakCheckInt(*this, TEXT("stage 0 (8,9) grey"), Grey[9 * 16 + 8], 155);
    TestFalse(TEXT("stage 0 (8,7) is not a crack"), Crack[7 * 16 + 8]);
    TArray<uint8> Carved;
    Carved.SetNumZeroed(BreakTexels);
    FoundationBreak::CarveStage(Crack, Carved);
    BreakCheckInt(*this, TEXT("stage 0 carved (7,7)"), Carved[7 * 16 + 7], 60);
    // (8,7) is right of (7,7) but above (8,8): shadow wins.
    BreakCheckInt(*this, TEXT("stage 0 carved (8,7)"), Carved[7 * 16 + 8], 90);
    BreakCheckInt(*this, TEXT("stage 0 carved (9,8)"), Carved[8 * 16 + 9], 170);
    BreakCheckInt(*this, TEXT("stage 0 carved (6,6)"), Carved[6 * 16 + 6], 128);
    return true;
}

// The asset checks read the texture array's source data, which only editor builds keep.
#if WITH_EDITORONLY_DATA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationBreakCracksAssetTest, "Unnamed.Foundation.Break.CracksAsset", BreakTestFlags)

bool FFoundationBreakCracksAssetTest::RunTest(const FString&)
{
    UTexture2DArray* Array = LoadObject<UTexture2DArray>(nullptr, TEXT("/Game/Breaking/T_BreakCracks.T_BreakCracks"));
    if (!Array)
    {
        AddError(TEXT("/Game/Breaking/T_BreakCracks is missing: run Scripts/Editor/ImportBreakAssets.py"));
        return false;
    }
    BreakCheckInt(*this, TEXT("layers"), Array->Source.GetNumSlices(), 2 * FoundationBreak::Stages);
    BreakCheckInt(*this, TEXT("width"), Array->Source.GetSizeX(), 16);
    BreakCheckInt(*this, TEXT("height"), Array->Source.GetSizeY(), 16);
    TestFalse(TEXT("SRGB"), Array->SRGB);
    if (Array->Source.GetNumSlices() != 2 * FoundationBreak::Stages) return false;

    for (int32 Stage = 0; Stage < FoundationBreak::Stages; ++Stage)
    {
        TArray<bool> Crack;
        TArray<uint8> Grey;
        if (!LoadBreakStage(*this, Stage, Crack, Grey)) return false;
        TArray<uint8> Expected[2];
        Expected[0].SetNumZeroed(BreakTexels);
        Expected[1].SetNumZeroed(BreakTexels);
        FoundationBreak::CarveStage(Crack, Expected[0]);
        FoundationBreak::PlainStage(Crack, Grey, Expected[1]);
        for (int32 Kind = 0; Kind < 2; ++Kind)
        {
            const int32 Layer = Kind * FoundationBreak::PlainLayer + Stage;
            TArray<uint8> Pixels;
            if (!UFoundationBlockImport::ReadLayer(Array, Layer, Pixels) || Pixels.Num() != BreakTexels * 4)
            {
                AddError(FString::Printf(TEXT("layer %d is unreadable"), Layer));
                continue;
            }
            int32 Wrong = 0;
            for (int32 Texel = 0; Texel < BreakTexels; ++Texel)
                Wrong += Pixels[Texel * 4] != Expected[Kind][Texel] || Pixels[Texel * 4 + 1] != Expected[Kind][Texel] ||
                    Pixels[Texel * 4 + 2] != Expected[Kind][Texel] || Pixels[Texel * 4 + 3] != 255;
            BreakCheckInt(*this,
                *FString::Printf(
                    TEXT("layer %d (%s stage %d) wrong texels"), Layer, Kind ? TEXT("plain") : TEXT("carved"), Stage),
                Wrong, 0);
        }
    }
    return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBreakParticlesTest, "Unnamed.Foundation.Break.Particles", BreakTestFlags)

bool FFoundationBreakParticlesTest::RunTest(const FString&)
{
    using namespace FoundationBreak;
    BreakCheckInt(*this, TEXT("per box All"), BreakParticlesPerBox(1), 64);
    BreakCheckInt(*this, TEXT("per box Decreased"), BreakParticlesPerBox(0.67), 43);
    BreakCheckInt(*this, TEXT("per box Minimal"), BreakParticlesPerBox(0.33), 21);
    TestTrue(TEXT("chips All"), HitChipsOn(1));
    TestTrue(TEXT("chips Decreased"), HitChipsOn(0.67));
    TestFalse(TEXT("chips Minimal"), HitChipsOn(0.33));
    BreakCheckNear(*this, TEXT("All"), ParticleAmount(TEXT("All")), 1);
    BreakCheckNear(*this, TEXT("Decreased"), ParticleAmount(TEXT("Decreased")), 0.67);
    BreakCheckNear(*this, TEXT("Minimal"), ParticleAmount(TEXT("Minimal")), 0.33);
    BreakCheckNear(*this, TEXT("unknown"), ParticleAmount(TEXT("Everything")), 1);

    const FBox Boxes[] = {FBox(FVector(0), FVector(100)), FBox(FVector(0), FVector(100, 100, 50))};
    const double Amounts[] = {1, 0.67, 0.33};
    const int32 Counts[] = {64, 43, 21};
    const FVector Push(150, 0, 0);
    FRandomStream Random(1234);
    for (const FBox& Box : Boxes)
        for (int32 Index = 0; Index < 3; ++Index)
        {
            TArray<FParticleSpawn> Spawns;
            SpawnBreakParticles(Box, Amounts[Index], Push, Random, Spawns);
            const FString Name = FString::Printf(TEXT("box z %g, amount %g"), Box.Max.Z, Amounts[Index]);
            BreakCheckInt(*this, *(Name + TEXT(" count")), Spawns.Num(), Counts[Index]);
            TSet<FIntVector> Seen;
            const FVector Centre = Box.GetCenter(), Size = Box.GetSize();
            int32 Bad = 0;
            for (const FParticleSpawn& Spawn : Spawns)
            {
                // The grid cell of the point; its centre is ((i + 0.5) / 4) of the box.
                const FVector Local = (Spawn.Location - Box.Min) / Size * 4 - FVector(0.5);
                const FIntVector Cell(
                    FMath::RoundToInt32(Local.X), FMath::RoundToInt32(Local.Y), FMath::RoundToInt32(Local.Z));
                bool bOk = Box.IsInsideOrOn(Spawn.Location) && (Local - FVector(Cell)).GetAbsMax() < 1e-9 &&
                    Cell.GetMin() >= 0 && Cell.GetMax() <= 3 && !Seen.Contains(Cell);
                Seen.Add(Cell);
                const FVector Expected = (Spawn.Location - Centre) * 1.5 + FVector(0, 0, 100) + Push;
                bOk &= (Spawn.Velocity - Expected).GetAbsMax() <= FoundationBreak::JitterCm + 1e-9;
                bOk &= Spawn.Life >= 1.5 && Spawn.Life <= 2.3;
                bOk &= BreakNear(Spawn.Size, 10) && Spawn.PatchU <= 12 && Spawn.PatchV <= 12;
                Bad += !bOk;
            }
            BreakCheckInt(*this, *(Name + TEXT(" bad particles")), Bad, 0);
            BreakCheckInt(*this, *(Name + TEXT(" distinct points")), Seen.Num(), Counts[Index]);
        }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBreakChipsTest, "Unnamed.Foundation.Break.Chips", BreakTestFlags)

bool FFoundationBreakChipsTest::RunTest(const FString&)
{
    using namespace FoundationBreak;
    FRandomStream Random(7);
    for (const bool bSpray : {false, true})
    {
        const FParticleSpawn Chip = SpawnHitChip(FVector(10, 20, 30), FVector(0, 0, 1), bSpray, Random);
        const FString Name = bSpray ? TEXT("spray chip") : TEXT("chip");
        TestTrue(*(Name + TEXT(" location")), (Chip.Location - FVector(10, 20, 31)).IsNearlyZero(BreakTolerance));
        TestTrue(*(Name + TEXT(" velocity")),
            (Chip.Velocity - FVector(0, 0, bSpray ? 60 : 40)).IsNearlyZero(BreakTolerance));
        TestTrue(*(Name + TEXT(" life")), Chip.Life >= 1.5 && Chip.Life <= 2.3);
        BreakCheckNear(*this, *(Name + TEXT(" size")), Chip.Size, 6);
        TestTrue(*(Name + TEXT(" patch")), Chip.PatchU <= 12 && Chip.PatchV <= 12);
    }
    const FParticleSpawn Side = SpawnHitChip(FVector(0), FVector(-1, 0, 0), false, Random);
    TestTrue(TEXT("side chip"),
        (Side.Location - FVector(-1, 0, 0)).IsNearlyZero(BreakTolerance) &&
            (Side.Velocity - FVector(-40, 0, 0)).IsNearlyZero(BreakTolerance));

    TestTrue(TEXT("push, normal only"),
        (SprayPush(FVector(0, 0, 1), FVector::ZeroVector) - FVector(0, 0, 150)).IsNearlyZero(BreakTolerance));
    TestTrue(TEXT("push, camera only"),
        (SprayPush(FVector::ZeroVector, FVector(10, 0, 0)) - FVector(50, 0, 0)).IsNearlyZero(BreakTolerance));
    TestTrue(TEXT("push, both"),
        (SprayPush(FVector(1, 0, 0), FVector(0, 3, 0)) - FVector(150, 50, 0)).IsNearlyZero(BreakTolerance));
    TestTrue(TEXT("push, nothing"), SprayPush(FVector::ZeroVector, FVector::ZeroVector).IsNearlyZero(BreakTolerance));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBreakCadenceTest, "Unnamed.Foundation.Break.Cadence", BreakTestFlags)

bool FFoundationBreakCadenceTest::RunTest(const FString&)
{
    for (const int32 Mode : {0, 1, 2})
    {
        const TArray<int64> Expected = BreakExpectedHits(Mode, 199);
        BreakCheckInt(*this, TEXT("expected hit count"), Expected.Num(), Mode == 2 ? 33 : 50);
        for (const int32 Fps : {30, 60, 144})
        {
            // Integer steps: floor(n x 20 / fps) is floor(t x 20) without the float noise.
            TArray<double> Times;
            for (int32 Frame = 0; Frame < Fps * 10; ++Frame)
                Times.Add((Frame * 20 / Fps + 0.25) / 20);
            int32 Most = 0;
            const TArray<int64> Hits = BreakCollectOverFrames(Mode, Times, Most);
            const FString Name = FString::Printf(TEXT("swing %d at %d fps"), Mode, Fps);
            TestTrue(*(Name + TEXT(" hit steps")), Hits == Expected);
            TestTrue(*(Name + TEXT(" catch-up")), Most <= FoundationBreak::MaxCatchUp);
        }

        // Irregular frames, with a 0.3 s hitch every 100 frames.
        FRandomStream Random(99);
        TArray<double> Times;
        double Time = 0;
        for (int32 Frame = 0; Time < 10; ++Frame)
        {
            Times.Add(Time);
            Time += Frame % 100 == 99 ? 0.3 : Random.FRandRange(0.004, 0.05);
        }
        Times.RemoveAll([](double Value) { return Value >= 10; });
        int32 Most = 0;
        const TArray<int64> Hits = BreakCollectOverFrames(Mode, Times, Most);
        const int64 Last = int64(FMath::FloorToDouble(Times.Last() * 20));
        TestTrue(FString::Printf(TEXT("swing %d irregular frames"), Mode), Hits == BreakExpectedHits(Mode, Last));
        TestTrue(TEXT("irregular catch-up"), Most <= FoundationBreak::MaxCatchUp);
    }

    // A long hitch keeps only the latest hits, ascending.
    TArray<int64, TInlineAllocator<4>> Out;
    FoundationBreak::CollectHitSteps(1, 0, 0, 39, 79, Out);
    TestTrue(TEXT("hitch, swing 1"), Out == TArray<int64, TInlineAllocator<4>>({64, 68, 72, 76}));
    FoundationBreak::CollectHitSteps(2, 0, 0, 39, 79, Out);
    TestTrue(TEXT("hitch, swing 2"), Out == TArray<int64, TInlineAllocator<4>>({57, 63, 69, 75}));
    FoundationBreak::CollectHitSteps(1, 0, 0, 39, 1000000000, Out);
    TestTrue(TEXT("very long hitch"),
        Out == TArray<int64, TInlineAllocator<4>>({999999988, 999999992, 999999996, 1000000000}));
    FoundationBreak::CollectHitSteps(1, 0, 0, 10, 10, Out);
    TestTrue(TEXT("no new step"), Out.IsEmpty());
    FoundationBreak::CollectHitSteps(1, 8, 0, 2, 7, Out);
    TestTrue(TEXT("before the mining start"), Out.IsEmpty());
    TestTrue(TEXT("IsHitStep swing 1"),
        FoundationBreak::IsHitStep(1, 5, 0, 9) && !FoundationBreak::IsHitStep(1, 5, 0, 4) &&
            !FoundationBreak::IsHitStep(1, 5, 0, 10));
    TestTrue(TEXT("IsHitStep swing 2"),
        FoundationBreak::IsHitStep(2, 0, 10, 13) && FoundationBreak::IsHitStep(2, 0, 10, 19) &&
            !FoundationBreak::IsHitStep(2, 0, 10, 7));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBreakSwingClockTest, "Unnamed.Foundation.Break.SwingClock", BreakTestFlags)

bool FFoundationBreakSwingClockTest::RunTest(const FString&)
{
    FoundationBreak::FSwingClock Clock;
    BreakCheckNear(*this, TEXT("idle clock"), Clock.GetPhase(0, 0), -1);
    Clock.StartStep = 10;
    BreakCheckNear(*this, TEXT("before the start"), Clock.GetPhase(9, 0.5), -1);
    BreakCheckNear(*this, TEXT("start"), Clock.GetPhase(10, 0), 0);
    BreakCheckNear(*this, TEXT("middle"), Clock.GetPhase(13, 0.5), 3.5 / 6);
    BreakCheckNear(*this, TEXT("last tick"), Clock.GetPhase(15, 0.5), 5.5 / 6);
    BreakCheckNear(*this, TEXT("done"), Clock.GetPhase(16, 0), -1);
    BreakCheckNear(*this, TEXT("long done"), Clock.GetPhase(100, 0.5), -1);
    Clock.bRepeating = true;
    BreakCheckNear(*this, TEXT("repeating, before the start"), Clock.GetPhase(9, 0.99), -1);
    BreakCheckNear(*this, TEXT("repeating, start"), Clock.GetPhase(10, 0), 0);
    BreakCheckNear(*this, TEXT("repeating, wraps"), Clock.GetPhase(16, 0), 0);
    BreakCheckNear(*this, TEXT("repeating, second swing"), Clock.GetPhase(19, 0), 0.5);
    BreakCheckNear(*this, TEXT("repeating, third swing"), Clock.GetPhase(22, 0.5), 0.5 / 6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBreakPopTest, "Unnamed.Foundation.Break.Pop", BreakTestFlags)

bool FFoundationBreakPopTest::RunTest(const FString&)
{
    BreakCheckNear(*this, TEXT("0"), FoundationBreak::PopScale(0), 1);
    BreakCheckNear(*this, TEXT("0.04"), FoundationBreak::PopScale(0.04), 0.75);
    BreakCheckNear(*this, TEXT("0.08"), FoundationBreak::PopScale(0.08), 0);
    BreakCheckNear(*this, TEXT("0.2"), FoundationBreak::PopScale(0.2), 0);
    BreakCheckNear(*this, TEXT("-0.01"), FoundationBreak::PopScale(-0.01), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBreakPixelsTest, "Unnamed.Foundation.Break.Pixels", BreakTestFlags)

bool FFoundationBreakPixelsTest::RunTest(const FString&)
{
    BreakCheckNear(*this, TEXT("1 px at 640"), FoundationBreak::PixelsToWorld(1, 640, 90, 1280), 1);
    BreakCheckNear(*this, TEXT("3 px at 320"), FoundationBreak::PixelsToWorld(3, 320, 90, 1280), 1.5);
    BreakCheckNear(*this, TEXT("no viewport"), FoundationBreak::PixelsToWorld(1, 640, 90, 0), 0);
    BreakCheckNear(*this, TEXT("no depth"), FoundationBreak::PixelsToWorld(1, 0, 90, 1280), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationBreakOutlineGeometryTest, "Unnamed.Foundation.Break.OutlineGeometry", BreakTestFlags)

bool FFoundationBreakOutlineGeometryTest::RunTest(const FString&)
{
    using namespace FoundationBreak;
    // The width rule: 3 px at 1920 wide, scaled by width / 1920 above it, never below 3.
    BreakCheckNear(*this, TEXT("3 px at 1920"), OutlinePixels(3, 1920), 3);
    BreakCheckNear(*this, TEXT("3 px at 2560"), OutlinePixels(3, 2560), 4);
    BreakCheckNear(*this, TEXT("3 px at 1280"), OutlinePixels(3, 1280), 3);
    BreakCheckNear(*this, TEXT("3 px at 3840"), OutlinePixels(3, 3840), 6);

    // A strip's place and length; seen along its own axis, its square needs no widening (k = 1).
    const FTransform Upright =
        OutlineStrip(FVector(10, 20, 30), 2, 50, FVector(10, 20, -300), FVector::UpVector, 3, 90, 1920);
    BreakCheckNear(*this, TEXT("strip length"), Upright.GetScale3D().Z * OutlineStripMeshCm, 50);
    BreakCheckNear(*this, TEXT("strip side on its axis"), Upright.GetScale3D().X * OutlineStripMeshCm,
        PixelsToWorld(3, 330, 90, 1920));
    TestTrue(TEXT("strip centred, unrotated"),
        Upright.GetTranslation().Equals(FVector(10, 20, 30)) && Upright.GetRotation().Equals(FQuat::Identity));

    // The drawn width through a pinhole camera (proto strip.py's measure): the strip's square cross-section at its
    // centre, its four corners projected and measured across the projected edge. The edge runs along X through the
    // origin; the camera looks at it from Distance, at Theta to the edge, its ray turned by Phi about the edge.
    constexpr double Pixels = 3;
    for (const double Theta : {30., 45., 80.})
        for (const double Phi : {0., 30., 45., 80.})
            for (const double Distance : {100., 300.})
                for (const double Width : {1920., 2560.})
                    for (const double Fov : {102.4479, 137.0043})
                    {
                        const double ThetaRad = FMath::DegreesToRadians(Theta), PhiRad = FMath::DegreesToRadians(Phi);
                        const FVector Forward(FMath::Cos(ThetaRad), FMath::Sin(ThetaRad) * FMath::Cos(PhiRad),
                            FMath::Sin(ThetaRad) * FMath::Sin(PhiRad));
                        const FVector Camera = -Distance * Forward;
                        const FTransform Strip =
                            OutlineStrip(FVector::ZeroVector, 0, 100, Camera, Forward, Pixels, Fov, Width);
                        const double Side = Strip.GetScale3D().Y * OutlineStripMeshCm;
                        const FVector Right = FVector::CrossProduct(
                            Forward, FMath::Abs(Forward.Z) < .9 ? FVector::UpVector : FVector::ForwardVector)
                                                  .GetSafeNormal();
                        const FVector Up = FVector::CrossProduct(Right, Forward);
                        const double Focal = Width / (2 * FMath::Tan(FMath::DegreesToRadians(Fov / 2)));
                        auto Project = [&](const FVector& Point)
                        {
                            const FVector Ray = Point - Camera;
                            const double Depth = FVector::DotProduct(Ray, Forward);
                            return FVector2D(Focal * FVector::DotProduct(Ray, Right) / Depth,
                                Focal * FVector::DotProduct(Ray, Up) / Depth);
                        };
                        const FVector2D Centre = Project(FVector::ZeroVector);
                        const FVector2D Along = (Project(FVector(1, 0, 0)) - Centre).GetSafeNormal();
                        const FVector2D Across(-Along.Y, Along.X);
                        double Low = UE_DOUBLE_BIG_NUMBER, High = -UE_DOUBLE_BIG_NUMBER;
                        for (int32 Corner = 0; Corner < 4; ++Corner)
                        {
                            const FVector Point(0, ((Corner & 1) ? .5 : -.5) * Side, ((Corner & 2) ? .5 : -.5) * Side);
                            const double Offset = FVector2D::DotProduct(Project(Point) - Centre, Across);
                            Low = FMath::Min(Low, Offset);
                            High = FMath::Max(High, Offset);
                        }
                        BreakCheckNear(*this,
                            *FString::Printf(TEXT("drawn px at theta %g, phi %g, %g cm, %g wide, fov %g"), Theta, Phi,
                                Distance, Width, Fov),
                            High - Low, Pixels, .05);
                    }

    // The merged outer edges: a shape's own outline, without the seams between its boxes.
    auto Merge = [](std::initializer_list<FBox> Boxes, double ExpandCm = 0)
    {
        TArray<FBox> Grown;
        for (const FBox& Box : Boxes)
            Grown.Add(Box.ExpandBy(ExpandCm));
        return MergeOutlineEdges(Grown);
    };
    auto TotalLength = [](const TArray<FOutlineEdge>& Edges)
    {
        double Sum = 0;
        for (const FOutlineEdge& Edge : Edges)
            Sum += FVector::Dist(Edge.A, Edge.B);
        return Sum;
    };
    const FBox Cube(FVector(0.), FVector(100.));
    const FBox BottomSlab(FVector(0.), FVector(100, 100, 50));
    const FBox TopSlab(FVector(0, 0, 50), FVector(100.));
    const FBox StairBack(FVector(0, 50, 50), FVector(100.));
    const FBox Post(FVector(37.5, 37.5, 0), FVector(62.5, 62.5, 100));
    const FBox Arm(FVector(43.75, 62.5, 37.5), FVector(56.25, 100, 56.25));
    BreakCheckInt(*this, TEXT("cube edges"), Merge({Cube}).Num(), 12);
    BreakCheckNear(*this, TEXT("cube edge length"), TotalLength(Merge({Cube})), 1200);
    BreakCheckInt(*this, TEXT("bottom slab edges"), Merge({BottomSlab}).Num(), 12);
    BreakCheckInt(*this, TEXT("stacked slabs edges"), Merge({BottomSlab, TopSlab}).Num(), 12);
    BreakCheckNear(*this, TEXT("stacked slabs edge length"), TotalLength(Merge({BottomSlab, TopSlab})), 1200);
    BreakCheckInt(*this, TEXT("stair edges"), Merge({BottomSlab, StairBack}).Num(), 18);
    BreakCheckInt(*this, TEXT("stair edges, boxes expanded 0.3 cm"), Merge({BottomSlab, StairBack}, .3).Num(), 18);
    BreakCheckInt(*this, TEXT("fence post edges"), Merge({Post}).Num(), 12);
    BreakCheckInt(*this, TEXT("fence post and arm edges"), Merge({Post, Arm}).Num(), 24);
    BreakCheckInt(*this, TEXT("no boxes"), MergeOutlineEdges(TConstArrayView<FBox>()).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBreakSoundsTest, "Unnamed.Foundation.Break.Sounds", BreakTestFlags)

bool FFoundationBreakSoundsTest::RunTest(const FString&)
{
    using namespace FoundationBreak;
    FFoundationBreakBlock Block;
    // Not 1, so a missing type-volume factor fails the volume checks.
    Block.Volume = 0.6f;
    Block.Pitch = 1.5f;
    const FFoundationBreakSoundEvent Event = BreakMakeEvent();

    FRandomStream Random(42);
    TSet<int32> BreakFiles, HitFiles;
    bool bBelow = false, bAbove = false;
    for (int32 Play = 0; Play < 200; ++Play)
    {
        const FSoundPlay Break = PickSound(Block, Event, false, 1, Random);
        if (!Event.Files.IsValidIndex(Break.File)) return TestTrue(TEXT("break file in range"), false);
        BreakFiles.Add(Break.File);
        const FFoundationBreakSoundFile& BreakFile = Event.Files[Break.File];
        BreakCheckNear(*this, TEXT("break volume"), Break.Volume, 1.0 * 0.6 * BreakFile.Volume);
        BreakCheckNear(*this, TEXT("break pitch"), Break.Pitch, 0.8 * 1.5 * BreakFile.Pitch);

        const FSoundPlay Hit = PickSound(Block, Event, true, 1, Random);
        if (!Event.Files.IsValidIndex(Hit.File)) return TestTrue(TEXT("hit file in range"), false);
        const FFoundationBreakSoundFile& HitFile = Event.Files[Hit.File];
        BreakCheckNear(*this, TEXT("hit volume"), Hit.Volume, 0.25 * 0.6 * HitFile.Volume);
        BreakCheckNear(*this, TEXT("hit pitch, mode 1"), Hit.Pitch, 0.75 * HitFile.Pitch);

        const FSoundPlay Varied = PickSound(Block, Event, true, 2, Random);
        if (!Event.Files.IsValidIndex(Varied.File)) return TestTrue(TEXT("varied file in range"), false);
        HitFiles.Add(Varied.File);
        const double Base = 0.75 * Event.Files[Varied.File].Pitch;
        TestTrue(TEXT("mode 2 hit pitch within 3%"),
            Varied.Pitch >= Base * 0.97 - 1e-6 && Varied.Pitch <= Base * 1.03 + 1e-6);
        bBelow |= Varied.Pitch < Base;
        bAbove |= Varied.Pitch > Base;
        BreakCheckNear(*this, TEXT("mode 2 hit volume"), Varied.Volume, 0.25 * 0.6 * Event.Files[Varied.File].Volume);
    }
    BreakCheckInt(*this, TEXT("break files used"), BreakFiles.Num(), 3);
    BreakCheckInt(*this, TEXT("hit files used"), HitFiles.Num(), 3);
    TestTrue(TEXT("pitch goes below the base"), bBelow);
    TestTrue(TEXT("pitch goes above the base"), bAbove);

    // The draw order is fixed: the file, then the pitch spread.
    FRandomStream Played(5), Mirror(5);
    const FSoundPlay Play = PickSound(Block, Event, true, 2, Played);
    const int32 File = Mirror.RandRange(0, 2);
    const double Spread = Mirror.FRandRange(0.97, 1.03);
    BreakCheckInt(*this, TEXT("file draw"), Play.File, File);
    BreakCheckNear(*this, TEXT("pitch draw"), Play.Pitch, 0.75 * Event.Files[File].Pitch * Spread);

    const FSoundPlay None = PickSound(Block, FFoundationBreakSoundEvent(), false, 1, Random);
    BreakCheckInt(*this, TEXT("empty event"), None.File, INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationBreakSoundsAssetTest, "Unnamed.Foundation.Break.SoundsAsset", BreakTestFlags)

bool FFoundationBreakSoundsAssetTest::RunTest(const FString&)
{
    const UFoundationBreakAssets* Assets =
        LoadObject<UFoundationBreakAssets>(nullptr, TEXT("/Game/Breaking/DA_BreakAssets.DA_BreakAssets"));
    if (!Assets)
    {
        AddError(TEXT("/Game/Breaking/DA_BreakAssets is missing: run Scripts/Editor/ImportBreakAssets.py"));
        return false;
    }
    auto Event = [&](int32 Index) -> const FFoundationBreakSoundEvent*
    { return Assets->Events.IsValidIndex(Index) ? &Assets->Events[Index] : nullptr; };

    const FFoundationBreakBlock* Stone = BreakFindAsset(*Assets, TEXT("stone"));
    if (!TestNotNull(TEXT("stone"), Stone)) return false;
    const FFoundationBreakSoundEvent* StoneBreak = Event(Stone->BreakEvent);
    if (TestNotNull(TEXT("stone break event"), StoneBreak))
    {
        TestTrue(TEXT("stone break event name"), StoneBreak->Name.ToString() == TEXT("block.stone.break"));
        if (BreakCheckInt(*this, TEXT("stone break files"), StoneBreak->Files.Num(), 4))
            for (int32 Index = 0; Index < 4; ++Index)
                TestTrue(TEXT("stone break file name"),
                    StoneBreak->Files[Index].Name.ToString() == FString::Printf(TEXT("dig/stone%d"), Index + 1));
    }
    const FFoundationBreakSoundEvent* StoneHit = Event(Stone->HitEvent);
    if (TestNotNull(TEXT("stone hit event"), StoneHit))
        TestTrue(TEXT("stone hit event name"), StoneHit->Name.ToString() == TEXT("block.stone.hit"));
    // Place sounds (2026-10-11): the block's place event; sounds.json gives stone's the break's files.
    const FFoundationBreakSoundEvent* StonePlace = Event(Stone->PlaceEvent);
    if (TestNotNull(TEXT("stone place event"), StonePlace))
    {
        TestTrue(TEXT("stone place event name"), StonePlace->Name.ToString() == TEXT("block.stone.place"));
        if (BreakCheckInt(*this, TEXT("stone place files"), StonePlace->Files.Num(), 4))
            for (int32 Index = 0; Index < 4; ++Index)
                TestTrue(TEXT("stone place file name"),
                    StonePlace->Files[Index].Name.ToString() == FString::Printf(TEXT("dig/stone%d"), Index + 1));
    }
    // Glass places with stone's files under its own event.
    if (const FFoundationBreakBlock* Glass = BreakFindAsset(*Assets, TEXT("glass")))
    {
        const FFoundationBreakSoundEvent* GlassPlace = Event(Glass->PlaceEvent);
        if (TestNotNull(TEXT("glass place event"), GlassPlace))
            TestTrue(TEXT("glass place event name"), GlassPlace->Name.ToString() == TEXT("block.glass.place"));
    }
    else
        AddError(TEXT("glass is not in DA_BreakAssets"));
    // The block reference lacks deepslate's place files: its place plays its break event.
    if (const FFoundationBreakBlock* Deepslate = BreakFindAsset(*Assets, TEXT("deepslate")))
        BreakCheckInt(
            *this, TEXT("deepslate places with its break event"), Deepslate->PlaceEvent, Deepslate->BreakEvent);
    else
        AddError(TEXT("deepslate is not in DA_BreakAssets"));
    // Every block: its own place event (550), its break event for want of files (66), or silent (water, lava's 2).
    int32 OwnPlace = 0, BreakPlace = 0, SilentPlace = 0;
    for (const FFoundationBreakBlock& Block : Assets->Blocks)
    {
        const FFoundationBreakSoundEvent* Place = Event(Block.PlaceEvent);
        OwnPlace += Place && Place->Name.ToString().EndsWith(TEXT(".place"));
        BreakPlace += Place && Block.PlaceEvent == Block.BreakEvent;
        SilentPlace += Block.PlaceEvent == INDEX_NONE;
    }
    BreakCheckInt(*this, TEXT("blocks with their own place event"), OwnPlace, 550);
    BreakCheckInt(*this, TEXT("blocks placing with their break event"), BreakPlace, 66);
    BreakCheckInt(*this, TEXT("silent places"), SilentPlace, 2);
    // Vanilla plays a sound on every place: on by default (Jesus, 2026-10-11); 0 keeps the old silence.
    const IConsoleVariable* PlaceSound = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Place.Sound"));
    if (TestNotNull(TEXT("foundation.Place.Sound exists"), PlaceSound) &&
        (PlaceSound->GetFlags() & ECVF_SetByMask) == ECVF_SetByConstructor)
        BreakCheckInt(*this, TEXT("place sounds on by default"), PlaceSound->GetInt(), 1);

    if (const FFoundationBreakBlock* Gold = BreakFindAsset(*Assets, TEXT("gold_block")))
    {
        BreakCheckNear(*this, TEXT("gold pitch"), Gold->Pitch, 1.5);
        const FFoundationBreakSoundEvent* GoldBreak = Event(Gold->BreakEvent);
        if (TestNotNull(TEXT("gold break event"), GoldBreak))
            TestTrue(TEXT("gold break event name"), GoldBreak->Name.ToString() == TEXT("block.metal.break"));
    }
    else
        AddError(TEXT("gold_block is not in DA_BreakAssets"));

    if (const FFoundationBreakBlock* Water = BreakFindAsset(*Assets, TEXT("water")))
    {
        BreakCheckInt(*this, TEXT("water break event"), Water->BreakEvent, INDEX_NONE);
        BreakCheckInt(*this, TEXT("water hit event"), Water->HitEvent, INDEX_NONE);
        BreakCheckInt(*this, TEXT("water place event"), Water->PlaceEvent, INDEX_NONE);
    }
    else
        AddError(TEXT("water is not in DA_BreakAssets"));

    if (const FFoundationBreakBlock* Grass = BreakFindAsset(*Assets, TEXT("grass_block")))
    {
        TestTrue(TEXT("grass particle"), Grass->Particle.ToString() == TEXT("dirt"));
        TestFalse(TEXT("grass particle tinted"), Grass->bParticleTinted);
    }
    else
        AddError(TEXT("grass_block is not in DA_BreakAssets"));

    if (const FFoundationBreakBlock* Leaves = BreakFindAsset(*Assets, TEXT("oak_leaves")))
        TestTrue(TEXT("leaves particle tinted"), Leaves->bParticleTinted);
    else
        AddError(TEXT("oak_leaves is not in DA_BreakAssets"));

    TestTrue(TEXT("events exist"), Assets->Events.Num() > 0);
    int32 NoWave = 0;
    for (const FFoundationBreakSoundEvent& Entry : Assets->Events)
        for (const FFoundationBreakSoundFile& File : Entry.Files)
            NoWave += File.Wave == nullptr;
    BreakCheckInt(*this, TEXT("event files without a wave"), NoWave, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBreakCollisionTest, "Unnamed.Foundation.Break.Collision", BreakTestFlags)

bool FFoundationBreakCollisionTest::RunTest(const FString&)
{
    using namespace FoundationBreak;
    // One solid unit block at cell (0,0,0).
    const auto Solid = [](const FVector& Point, TArray<FBox>& OutBoxes)
    {
        if (Point.X >= 0 && Point.X < 100 && Point.Y >= 0 && Point.Y < 100 && Point.Z >= 0 && Point.Z < 100)
            OutBoxes.Add(FBox(FVector(0), FVector(100)));
    };
    constexpr double Dt = 1. / 60;

    FParticleState Landing;
    Landing.Location = FVector(50, 50, 150);
    int32 Steps = 0;
    for (; Steps < 120 && !Landing.bResting; ++Steps)
        StepParticle(Landing, Dt, true, Solid);
    TestTrue(TEXT("rests within 2 s"), Landing.bResting);
    BreakCheckNear(*this, TEXT("rest height"), Landing.Location.Z, 100.01);
    TestTrue(TEXT("rest position"), BreakNear(Landing.Location.X, 50) && BreakNear(Landing.Location.Y, 50));
    TestTrue(TEXT("rest velocity"), Landing.Velocity.IsNearlyZero());
    const FVector Resting = Landing.Location;
    StepParticle(Landing, Dt, true, Solid);
    TestTrue(TEXT("a resting particle stays"), Landing.Location == Resting);

    FParticleState Free;
    Free.Location = FVector(50, 50, 150);
    for (int32 Step = 0; Step < 120; ++Step)
        StepParticle(Free, Dt, false, Solid);
    TestTrue(TEXT("without collision it falls through"), Free.Location.Z < 0 && !Free.bResting);

    // A side hit stops the sideways motion and keeps falling, outside the block.
    FParticleState Side;
    Side.Location = FVector(-10, 50, 50);
    Side.Velocity = FVector(600, 0, 0);
    for (int32 Step = 0; Step < 120; ++Step)
    {
        StepParticle(Side, Dt, true, Solid);
        TArray<FBox> Boxes;
        Solid(Side.Location, Boxes);
        if (!Boxes.IsEmpty()) return TestTrue(TEXT("a particle never ends inside the block"), false);
    }
    TestTrue(TEXT("a side hit stays left of the block"), Side.Location.X < 0);
    TestTrue(TEXT("a side hit keeps falling"), Side.Location.Z < 50);
    return true;
}

#endif
