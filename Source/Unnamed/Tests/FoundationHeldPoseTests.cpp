#include "Misc/AutomationTest.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationItemMesh.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationHeldPoseTest, "Unnamed.Foundation.Items.HeldPose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationHeldPoseTest::RunTest(const FString&)
{
    for (const bool bFit : {false, true})
    {
        const FoundationHeldPose::FPose Main = FoundationHeldPose::Get(EFoundationHand::Main, bFit);
        const FoundationHeldPose::FPose Off = FoundationHeldPose::Get(EFoundationHand::Off, bFit);
        TestEqual(TEXT("main hand: the right-hand display"), Main.Context, FName(TEXT("firstperson_righthand")));
        TestEqual(TEXT("offhand: the left-hand display"), Off.Context, FName(TEXT("firstperson_lefthand")));
        TestTrue(TEXT("main hand is on the +Y side"), Main.Mirror == 1. && Main.Pivot.Y > 0);
        TestTrue(TEXT("offhand mirrors x of the view (Y negated), the rest equal"),
            Off.Mirror == -1. && Off.Pivot.Equals(FVector(Main.Pivot.X, -Main.Pivot.Y, Main.Pivot.Z)));
    }
    TestTrue(TEXT("fitted main pivot"),
        FoundationHeldPose::Get(EFoundationHand::Main, true).Pivot.Equals(FVector(71.9, 56.2, -52.2)));

    // The shield's left-hand displays from DA_Items.
    const UFoundationItemAssets* Assets = UFoundationItemAssets::LoadDefault();
    if (!TestNotNull(TEXT("DA_Items"), Assets)) return false;
    FFoundationItemModelView Idle, Raised;
    const FName Left = FoundationHeldPose::Get(EFoundationHand::Off, false).Context;
    const bool bIdle = Assets->FindSpecialView(TEXT("shield"), Left, Idle, false);
    const bool bRaised = Assets->FindSpecialView(TEXT("shield"), Left, Raised, true);
    TestTrue(TEXT("shield left, idle: rotation (0, 180, 5), translation (10, 0, -10), scale 1.25"),
        bIdle && Idle.Model && Idle.Model->Name == TEXT("item/shield") && Idle.Display &&
            Idle.Display->Rotation.Equals(FVector(0, 180, 5)) &&
            Idle.Display->Translation.Equals(FVector(10, 0, -10)) && Idle.Display->Scale.Equals(FVector(1.25)));
    TestTrue(TEXT("shield left, in use: rotation (0, 180, -5), translation (5, 5, -11), scale 1.25"),
        bRaised && Raised.Model && Raised.Model->Name == TEXT("item/shield_blocking") && Raised.Display &&
            Raised.Display->Rotation.Equals(FVector(0, 180, -5)) &&
            Raised.Display->Translation.Equals(FVector(5, 5, -11)) && Raised.Display->Scale.Equals(FVector(1.25)));
    return true;
}
#endif
