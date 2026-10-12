#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Voxel/VoxelEdgeFog.h"

// Far LOD step 2: which edge the view gets, and where the far meshes fade out.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelEdgeFogModeTest, "Unnamed.Voxel.EdgeFog.Mode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelEdgeFogModeTest::RunTest(const FString&)
{
    // The haze only while the switch is on and far meshes form the edge; voxel.FarRadius 0 always keeps the wall.
    TestTrue(TEXT("far ring 24 past view 12, switch on"), VoxelEdgeFog::UsesHaze(true, 12, 24));
    TestTrue(TEXT("one column past the view"), VoxelEdgeFog::UsesHaze(true, 12, 13));
    TestFalse(TEXT("switch off"), VoxelEdgeFog::UsesHaze(false, 12, 24));
    TestFalse(TEXT("voxel.FarRadius 0"), VoxelEdgeFog::UsesHaze(true, 12, 0));
    TestFalse(TEXT("far radius equal to the view radius"), VoxelEdgeFog::UsesHaze(true, 12, 12));
    TestFalse(TEXT("far radius inside the view radius"), VoxelEdgeFog::UsesHaze(true, 16, 12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelEdgeFogFadeTest, "Unnamed.Voxel.EdgeFog.Fade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelEdgeFogFadeTest::RunTest(const FString&)
{
    // The drawn radius's last two columns, in centimetres: 352 to 384 blocks at radius 24.
    const VoxelEdgeFog::FFade Fade24 = VoxelEdgeFog::GetFarFade(24);
    TestEqual(TEXT("radius 24 start"), Fade24.StartCm, 35200.);
    TestEqual(TEXT("radius 24 end"), Fade24.EndCm, 38400.);
    const VoxelEdgeFog::FFade Fade32 = VoxelEdgeFog::GetFarFade(32);
    TestEqual(TEXT("radius 32 start"), Fade32.StartCm, 48000.);
    TestEqual(TEXT("radius 32 end"), Fade32.EndCm, 51200.);
    // Never before the camera.
    const VoxelEdgeFog::FFade Fade1 = VoxelEdgeFog::GetFarFade(1);
    TestEqual(TEXT("radius 1 start"), Fade1.StartCm, 0.);
    TestEqual(TEXT("radius 1 end"), Fade1.EndCm, 1600.);
    return true;
}
#endif
