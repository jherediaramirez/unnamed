#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Spline/VoxelSpline.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSplineHermiteTest, "Unnamed.Voxel.Spline.Hermite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSplineHermiteTest::RunTest(const FString& Parameters)
{
    FVoxelSplineBuilder Builder;
    const int32 Node = Builder.AddNode(0);
    Builder.AddPoint(Node, -1, -.5f, .2f);
    Builder.AddPoint(Node, 0, .25f, 1.5f);
    Builder.AddPoint(Node, .5f, .25f, 0);
    Builder.AddPoint(Node, 2, 1, -.4f);
    FVoxelSpline Spline;
    TestTrue(TEXT("Builds"), Builder.Build(Node, 1, Spline));
    auto At = [&Spline](float X) { return Spline.Evaluate(TConstArrayView<float>(&X, 1)); };

    // Through every point, with the given slope there (central differences).
    const float Locations[] = {-1, 0, .5f, 2}, Values[] = {-.5f, .25f, .25f, 1}, Slopes[] = {.2f, 1.5f, 0, -.4f};
    for (int32 Index = 0; Index < 4; ++Index)
    {
        TestEqual(FString::Printf(TEXT("Value at point %d"), Index), At(Locations[Index]), Values[Index], 1e-6f);
        // The curve is only C1 at its points (the second derivative jumps), so a central difference across a point is
        // off by about H / 4 times that jump: keep H small.
        const float H = 1e-4f;
        const float Slope = (At(Locations[Index] + H) - At(Locations[Index] - H)) / (2 * H);
        TestEqual(FString::Printf(TEXT("Slope at point %d"), Index), Slope, Slopes[Index], 2e-3f);
    }
    // Beyond the ends: straight lines along the end slopes.
    TestEqual(TEXT("Left extrapolation"), At(-3), -.5f + .2f * -2, 1e-6f);
    TestEqual(TEXT("Right extrapolation"), At(5), 1 - .4f * 3, 1e-6f);
    // The cubic in the middle of a segment: the Hermite basis at t = 1/2 gives (v0 + v1) / 2 + width (d0 - d1) / 8.
    TestEqual(TEXT("Segment midpoint"), At(1.25f), (.25f + 1) / 2 + 1.5f * (0 - -.4f) / 8, 1e-6f);

    FVoxelSplineBuilder Single;
    const int32 Lone = Single.AddNode(0);
    Single.AddPoint(Lone, 3, 7, .5f);
    FVoxelSpline Line;
    TestTrue(TEXT("A single point builds"), Single.Build(Lone, 1, Line));
    float X = 1;
    TestEqual(TEXT("A single point is a line through it"), Line.Evaluate(TConstArrayView<float>(&X, 1)), 6.f, 1e-6f);
    TestEqual(TEXT("An empty spline is 0"), FVoxelSpline().Evaluate({}), 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSplineNestedTest, "Unnamed.Voxel.Spline.Nested",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSplineNestedTest::RunTest(const FString& Parameters)
{
    // Root over coordinate 0 whose middle point is a spline over coordinate 1: the terrain-spline shape.
    FVoxelSplineBuilder Builder;
    const int32 Root = Builder.AddNode(0);
    const int32 Inner = Builder.AddNode(1);
    Builder.AddPoint(Inner, -1, 2, 0);
    Builder.AddPoint(Inner, 1, 4, 1);
    Builder.AddPoint(Root, -1, 0, 0);
    Builder.AddChildPoint(Root, 0, Inner, .5f);
    Builder.AddPoint(Root, 1, 1, 0);
    FVoxelSpline Spline;
    FString Error;
    TestTrue(TEXT("Builds: ") + Error, Builder.Build(Root, 2, Spline, &Error));
    TestEqual(TEXT("Root first, then the child"), Spline.GetNodeCount(), 2);

    // The same composition by hand: the inner spline alone, then the outer one with its value plugged in.
    FVoxelSplineBuilder InnerOnly;
    const int32 InnerNode = InnerOnly.AddNode(0);
    InnerOnly.AddPoint(InnerNode, -1, 2, 0);
    InnerOnly.AddPoint(InnerNode, 1, 4, 1);
    FVoxelSpline InnerSpline;
    InnerOnly.Build(InnerNode, 1, InnerSpline);
    double Worst = 0;
    for (float A = -1.5f; A <= 1.5f; A += .125f)
        for (float B = -1.5f; B <= 1.5f; B += .125f)
        {
            const float InnerValue = InnerSpline.Evaluate(TConstArrayView<float>(&B, 1));
            FVoxelSplineBuilder Outer;
            const int32 OuterNode = Outer.AddNode(0);
            Outer.AddPoint(OuterNode, -1, 0, 0);
            Outer.AddPoint(OuterNode, 0, InnerValue, .5f);
            Outer.AddPoint(OuterNode, 1, 1, 0);
            FVoxelSpline OuterSpline;
            Outer.Build(OuterNode, 1, OuterSpline);
            const float Coordinates[2] = {A, B};
            Worst = FMath::Max(Worst,
                double(FMath::Abs(Spline.Evaluate(Coordinates) - OuterSpline.Evaluate(TConstArrayView<float>(&A, 1)))));
        }
    TestTrue(FString::Printf(TEXT("Nested evaluation equals the composition (worst %.2g)"), Worst), Worst < 1e-6);

    // A shared child (a DAG) is fine; bad trees are refused with a reason.
    FVoxelSplineBuilder Shared;
    const int32 SharedRoot = Shared.AddNode(0), Leaf = Shared.AddNode(1);
    Shared.AddPoint(Leaf, 0, 1);
    Shared.AddChildPoint(SharedRoot, 0, Leaf);
    Shared.AddChildPoint(SharedRoot, 1, Leaf);
    FVoxelSpline SharedSpline;
    TestTrue(TEXT("A child used twice builds"), Shared.Build(SharedRoot, 2, SharedSpline));

    auto Refuses = [this](const TCHAR* What, const FVoxelSplineBuilder& Bad, int32 BadRoot, int32 Coordinates)
    {
        FVoxelSpline Ignored;
        FString Reason;
        TestFalse(FString(What) + TEXT(" is refused"), Bad.Build(BadRoot, Coordinates, Ignored, &Reason));
        TestFalse(FString(What) + TEXT(" gives a reason"), Reason.IsEmpty());
    };
    FVoxelSplineBuilder Unsorted;
    const int32 U = Unsorted.AddNode(0);
    Unsorted.AddPoint(U, 1, 0);
    Unsorted.AddPoint(U, 1, 1);
    Refuses(TEXT("Repeated location"), Unsorted, U, 1);
    FVoxelSplineBuilder Empty;
    const int32 E = Empty.AddNode(0);
    Refuses(TEXT("A node without points"), Empty, E, 1);
    FVoxelSplineBuilder Cycle;
    const int32 C0 = Cycle.AddNode(0), C1 = Cycle.AddNode(0);
    Cycle.AddChildPoint(C0, 0, C1);
    Cycle.AddChildPoint(C1, 0, C0);
    Refuses(TEXT("A cycle"), Cycle, C0, 1);
    FVoxelSplineBuilder Coordinate;
    const int32 K = Coordinate.AddNode(3);
    Coordinate.AddPoint(K, 0, 0);
    Refuses(TEXT("A coordinate out of range"), Coordinate, K, 2);
    FVoxelSplineBuilder Dangling;
    const int32 D = Dangling.AddNode(0);
    Dangling.AddChildPoint(D, 0, 42);
    Refuses(TEXT("An unknown child"), Dangling, D, 1);
    return true;
}
#endif
