#if WITH_DEV_AUTOMATION_TESTS
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationItemMesh.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationHandFitTest, "Unnamed.Foundation.HandFit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationHandFitTest::RunTest(const FString& Parameters)
{
    // The held item keeps P1's FOV (vanilla's frames were shot at 70) whatever Options > FOV says.
    TestEqual(TEXT("The held item's fixed vertical FOV is P1's"), FoundationHand::FixedVerticalFov, 70.);

    // A held block: block/block.json's firstperson_righthand (rotation 0 45 0, scale 0.4) on a cube a world block wide,
    // so the cube draws 0.4 model blocks wide, as a sprite item's display scales its model.
    const FTransform Block = FoundationHand::MakeHeldBlockDisplay();
    FFoundationItemDisplayAsset Display;
    Display.Rotation = FVector(0, 45, 0);
    Display.Scale = FVector(.4);
    const FTransform Expected =
        FoundationItemMesh::MakeDisplayTransform(Display, UFoundationInteractionView::HeldModelBlockCm);
    TestTrue(TEXT("The block turns as its display says"), Block.GetRotation().Equals(Expected.GetRotation(), 1e-6));
    TestTrue(TEXT("... about its centre"), Block.GetTranslation().IsNearlyZero(1e-6));
    TestTrue(TEXT("A world-block cube drawn 0.4 model blocks wide (0.2 of the mesh at 50 cm per model block)"),
        Block.GetScale3D().Equals(FVector(.2), 1e-6));
    // The display's 45 degrees about Mojang's Y turn about Unreal's Z: an edge of the cube ends up straight ahead.
    const FVector Corner = Block.TransformPosition(FVector(50, 50, 50));
    TestTrue(TEXT("A top corner turns onto the forward axis"), FMath::Abs(Corner.Y) < 1e-3 && Corner.X > 14);
    return true;
}

#endif
