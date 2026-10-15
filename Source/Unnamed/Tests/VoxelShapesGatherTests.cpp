#if WITH_DEV_AUTOMATION_TESTS
#include "Blocks/VoxelBlockTable.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Streaming/VoxelStreamingPlanner.h"
#include "Terrain/VoxelFlatTerrain.h"
#include "Voxel/VoxelBlockStorage.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelWorld.h"
#include "VoxelConstants.h"

// Survival B2a's outer-layer gather on the voxel world's own path (AVoxelWorld::GatherColumns, B2a known issue 2): a
// generated column with a fence and a pitcher crop, foundation.Blocks.Shapes 0 against 1, regions above and below.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelShapesGatherTest, "Unnamed.Voxel.World.ShapesGather",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelShapesGatherTest::RunTest(const FString& Parameters)
{
    IConsoleVariable* Shapes = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Blocks.Shapes"));
    if (!TestNotNull(TEXT("foundation.Blocks.Shapes exists"), Shapes)) return false;
    const int32 Saved = Shapes->GetInt();
    // A flat stone floor up to z 63, streamed around column (0, 0), the block world over its columns.
    constexpr int32 FloorTop = 63;
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    const FVoxelFlatLayer Layer{Table->FindState(TEXT("stone")), FloorTop - VoxelConstants::MinZ + 1};
    const TSharedRef<const IVoxelTerrain> Terrain = MakeShared<FVoxelFlatTerrain>(MakeArrayView(&Layer, 1), *Table);
    FVoxelColumnPipeline::FSettings Settings;
    Settings.MaxTasksInFlight = 1024;
    Settings.bManualJobs = true;
    FVoxelColumnPipeline Pipeline(Table, Terrain, Settings);
    Pipeline.SetFocus(FVoxelStreamingRings(FIntPoint(0, 0), 1, 1), 0);
    for (int32 Round = 0; Round < 4000 && !Pipeline.IsIdle(); ++Round)
    {
        Pipeline.Update();
        FVoxelColumnPipeline::FReady Ready;
        while (Pipeline.PopReady(Ready))
        {
        }
        if (!Pipeline.IsIdle()) Pipeline.WaitForJobs();
    }
    if (!TestTrue(TEXT("the pipeline streams the columns"), Pipeline.IsIdle())) return false;
    FFoundationBlockWorld World;
    World.SetRegistry(FFoundationBlockRegistry::GetDefault());
    World.BoundsMin = FIntVector(-1000, -1000, VoxelConstants::MinZ);
    World.BoundsMax = FIntVector(1000, 1000, VoxelConstants::MaxZ);
    FVoxelColumnPipeline* Raw = &Pipeline;
    World.SetStorage(MakeUnique<FVoxelBlockStorage>([Raw]() { return Raw; }));
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    auto Put = [&](const FIntVector& Cell, const TCHAR* Text)
    {
        FFoundationBlockState State;
        ensure(Registry.Parse(Text, State));
        World.Set(Cell, State);
    };
    const FIntVector Fence(2, 2, FloorTop + 1), Pitcher(6, 2, FloorTop + 2);
    Put(Fence, TEXT("oak_fence"));
    Put(Pitcher - FIntVector(0, 0, 1), TEXT("farmland[moisture=0]"));
    Put(Pitcher, TEXT("pitcher_crop[age=0,half=lower]"));
    // AVoxelWorld's ModelCollides.
    TArray<bool> ModelCollides;
    ModelCollides.Init(false, Registry.GetStateLimit());
    TArray<FBox> Boxes;
    for (int32 Id = 0; Id < Registry.GetStateLimit(); ++Id)
        if (Registry.GetShape({uint16(Id)}) == EFoundationBlockShape::Model)
        {
            Registry.GetCollisionBoxes({uint16(Id)}, Boxes);
            ModelCollides[Id] = !Boxes.IsEmpty();
            Boxes.Reset();
        }
    const double Cm = UFoundationProfile::BlockCm;
    auto Gather = [&](int32 Value, const FBox& Region)
    {
        Shapes->Set(Value, ECVF_SetByCode);
        TArray<FBox> Out;
        AVoxelWorld::GatherColumns(Region, Pipeline, *Table, ModelCollides, World, FVector::ZeroVector, Out);
        return Out;
    };
    auto Above = [&](const FIntVector& Cell, double Bottom, double Top)
    {
        const FVector Min = FoundationGrid::CellMin(Cell, FVector::ZeroVector);
        return FBox(Min + FVector(1, 1, Bottom * Cm), Min + FVector(Cm - 1, Cm - 1, Top * Cm));
    };
    struct FRegion
    {
        const TCHAR* Name;
        FBox Box;
        int32 Added;
        double Top, Bottom;
    };
    const FRegion Regions[] = {{TEXT("above the fence, low"), Above(Fence + FIntVector(0, 0, 1), 0, .4), 1, 1.5, 0},
        {TEXT("above the fence, over its post"), Above(Fence + FIntVector(0, 0, 1), .55, .9), 0, 0, 0},
        {TEXT("the farmland's top under the pitcher"), Above(Pitcher - FIntVector(0, 0, 1), .95, .99), 1, 0, -1 / 16.},
        {TEXT("the farmland's middle under the pitcher"), Above(Pitcher - FIntVector(0, 0, 1), .5, .9), 0, 0, 0}};
    for (const FRegion& Region : Regions)
    {
        const TArray<FBox> Off = Gather(0, Region.Box), On = Gather(1, Region.Box);
        TestEqual(FString::Printf(TEXT("%s: switch 1 adds"), Region.Name), On.Num() - Off.Num(), Region.Added);
        for (const FBox& Box : Off)
            TestTrue(FString::Printf(TEXT("%s: switch 1 keeps switch 0's boxes"), Region.Name),
                On.ContainsByPredicate([&Box](const FBox& Other) { return Other.Equals(Box, 1e-3); }));
        // The farmland's own cell is gathered at either setting.
        if (Region.Bottom == 0 && Region.Top == 0 &&
            Region.Box.Min.Z < FoundationGrid::CellMin(Pitcher, FVector::ZeroVector).Z)
            TestTrue(FString::Printf(TEXT("%s: switch 0 gathers the farmland"), Region.Name), Off.Num() > 0);
        const FBox* Added = On.FindByPredicate([&Off](const FBox& Box)
            { return !Off.ContainsByPredicate([&Box](const FBox& Other) { return Other.Equals(Box, 1e-3); }); });
        if (Region.Added == 1 && Added)
        {
            const FBox& New = *Added;
            // The fence post tops out 1.5 blocks over its cell; the pitcher's lower half reaches 1/16 under its own.
            if (Region.Top > 0)
                TestEqual(FString::Printf(TEXT("%s: the post's top"), Region.Name), New.Max.Z,
                    FoundationGrid::CellMin(Fence, FVector::ZeroVector).Z + Region.Top * Cm, 1e-3);
            if (Region.Bottom < 0)
                TestEqual(FString::Printf(TEXT("%s: the crop's bottom"), Region.Name), New.Min.Z,
                    FoundationGrid::CellMin(Pitcher, FVector::ZeroVector).Z + Region.Bottom * Cm, 1e-3);
        }
    }
    Shapes->Set(Saved, ECVF_SetByCode);
    return true;
}
#endif
