#include "Foundation/FoundationRuntimeProbe.h"
#include "Foundation/FoundationBlockTextureTable.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationPlayground.h"
#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationPawn.h"
#include "Hud/HudCreativeScreen.h"
#include "Foundation/FoundationProfile.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// -FoundationSuite=tour (never part of "all"): a screenshot tour of the Playground with the HUD visible, taken with
// the probe's ordinary screenshot request. Each shot is a case that sets the scene, waits for the frame to settle and
// requests the capture; a short flush case lets the file reach the disk. Named <report>-tour-<name>.png.
void UFoundationRuntimeProbe::AddTourCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    auto* Lab = Cast<AFoundationPlayground>(Pawn->GetLab());
    if (!Lab)
    {
        AddCase(
            TEXT("playground present"), .1, [] {},
            [this] { Record(TEXT("playground present"), false, TEXT("the map has no AFoundationPlayground")); });
        return;
    }
    auto Authority = [Lab]() -> FFoundationAuthority& { return *Lab->GetAuthority(); };
    auto Click = [this](FKey Key)
    {
        PressKey(Key, true);
        PressKey(Key, false);
    };
    auto Shot = [this](const TCHAR* Name)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-tour-") + Name + TEXT(".png"), true, false);
    };
    // The camera for an aerial view: the player is held with its eyes at Eye looking at Target (no falling).
    auto Hover = [this](FVector Eye, FVector Target)
    {
        FrameHook = [this, Eye, Target]
        {
            const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
            const FVector Feet = Eye - FVector(0, 0, Profile.StandingEyesCm);
            Pawn->Simulation.Reset(Feet, Profile);
            Pawn->Simulation.bGrounded = true;
            Pawn->Vitals.ResetFall();
            Pawn->SetActorLocation(Feet);
            Controller->SetControlRotation((Target - Eye).Rotation());
        };
    };
    auto Look = [this](FVector Point) { Controller->SetControlRotation((Point - Pawn->GetEyePosition()).Rotation()); };
    auto Flush = [this](const TCHAR* Name) { AddCase(FString(TEXT("tour flush ")) + Name, .4, [] {}, [] {}); };
    const FVector Course = GetCourseOrigin();

    // A moment for shaders to compile and textures to stream before the first picture.
    AddCase(TEXT("tour warm-up"), 4., [this] { ResetPlayer(); }, [] {});

    // a. Spawn overview: from above and southwest, over the stations, signs and training targets.
    AddCase(
        TEXT("tour spawn overview"), 1.,
        [this, Hover]
        {
            ResetPlayer();
            Controller->GetHud()->bHideHud = Controller->GetHud()->bOverlay = Controller->GetHud()->bShowCollision =
                false;
            Hover(FVector(-450, -600, 380), FVector(50, 200, 40));
        },
        [Shot] { Shot(TEXT("01-spawn")); });
    Flush(TEXT("spawn"));

    // b. The dev grid at eye height: 1 m tiles, labels and the red (east) and green (south) axis lines.
    AddCase(
        TEXT("tour dev grid"), .8,
        [this, Look]
        {
            ResetPlayer({-250, -350, 0});
            Look(FVector(250, 0, 0));
        },
        [Shot] { Shot(TEXT("02-grid")); });
    Flush(TEXT("grid"));

    // c. Building: a three-step staircase, three blocks wide, placed through authority commands (a second player
    // aiming straight down onto each column, as the stress scenario does), then seen from its side.
    AddCase(
        TEXT("tour build"), .8,
        [this, Authority, Look]
        {
            FFoundationAuthority& A = Authority();
            const int32 Before = A.World.GetSolidCount();
            FFoundationVitals Builder;
            const int32 Id = A.AddPlayer();
            uint32 Sequence = 1;
            int32 Placed = 0;
            for (int32 Z = 0; Z < 3; ++Z)
                for (int32 X = -3; X <= -1; ++X)
                    for (int32 Y = -8; Y <= -6; ++Y)
                    {
                        // Column X + 4 blocks high: one, two and three steps.
                        if (Z > X + 3) continue;
                        FFoundationPlayerFacts Facts;
                        Facts.EyeCm = FVector(X * 100 + 50, Y * 100 + 50, Z * 100 + 120);
                        Facts.FeetCm = Facts.EyeCm + FVector(0, 0, 5000);
                        Facts.Aim = FVector(0, 0, -1);
                        Facts.Step = Pawn->Simulation.Steps * 10000 + Sequence;
                        A.SetFacts(Id, Facts);
                        FFoundationCommand Command;
                        Command.SessionId = A.GetSessionId();
                        Command.Sequence = Sequence++;
                        Command.Type = EFoundationCommandType::UseOn;
                        Command.Item = A.FindPlayer(Id)->Inventory.GetHand(EFoundationHand::Main).GetRef();
                        Command.Target = A.Resolve(Facts);
                        FIntVector Destination;
                        EFoundationReason Ignored;
                        A.GetDestination(Command.Target, Destination, Ignored);
                        Command.Destination = Destination;
                        Command.DestinationRevision = A.World.Get(Destination).Revision;
                        Placed += A.Execute(Id, Command, Builder).bPlaced ? 1 : 0;
                    }
            Record(TEXT("tour staircase placed"), Placed == 18 && A.World.GetSolidCount() == Before + 18,
                FString::Printf(TEXT("%d placed, %d solid cells"), Placed, A.World.GetSolidCount()));
            // From the north-west corner, so the risers and treads are lit.
            ResetPlayer({-550, -250, 0});
            Look(FVector(-150, -650, 100));
        },
        [Shot] { Shot(TEXT("03-building")); });
    Flush(TEXT("build"));

    // d. Mining in progress on a stone seed block, bare hand (150 reference steps), captured past a third.
    AddCase(
        TEXT("tour mining"), 3.,
        [this, Click, Look]
        {
            ResetPlayer({400, 350, 0});
            Look(FVector(600, 350, 50));
            Click(EKeys::Four);
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this, Shot]
        {
            Record(TEXT("tour mining in progress"), Pawn->Interaction.bMining && Pawn->Interaction.MiningProgress > .3,
                FString::Printf(TEXT("%.0f%%"), Pawn->Interaction.MiningProgress * 100));
            Shot(TEXT("04-mining"));
        });
    AddCase(TEXT("tour mining release"), .4, [this] { PressKey(EKeys::LeftMouseButton, false); }, [] {});

    // e. Combat: the sword facing the Training target, captured on the frame its hit lands (red body flash).
    AddCase(
        TEXT("tour sword charge"), .9,
        [this, Click, Look]
        {
            ResetPlayer({0, 760, 0});
            Click(EKeys::Three);
            Look(FVector(0, 1000, 95));
        },
        [] {});
    AddCase(
        TEXT("tour sword hit"), .8,
        [this, Click, Authority, Shot]
        {
            Click(EKeys::LeftMouseButton);
            FrameHook = [this, Authority, Shot, bShot = false]() mutable
            {
                if (bShot || Authority().Targets[0].Health.Health >= 20) return;
                bShot = true;
                Shot(TEXT("05-combat"));
            };
        },
        [this, Authority]
        {
            Record(TEXT("tour sword hit landed"), Authority().Targets[0].Health.Health < 20,
                FString::Printf(TEXT("target health %.1f"), Authority().Targets[0].Health.Health));
        });
    Flush(TEXT("combat"));

    // f. The movement course from the south and above: steps, ceiling, gaps, ledge, stairs, drop and water.
    AddCase(
        TEXT("tour movement course"), .8,
        [this, Hover, Click, Course]
        {
            ResetPlayer(Course);
            Click(EKeys::One);
            Hover(Course + FVector(200, -2300, 1250), Course + FVector(200, -150, 0));
        },
        [Shot] { Shot(TEXT("06-course")); });
    Flush(TEXT("course"));

    // g. Inside the water column: the eyes are underwater and the air bubbles show.
    AddCase(
        TEXT("tour water"), 1.5,
        [this, Look, Click, Course]
        {
            ResetPlayer(Course + FVector(1650, 450, 0));
            Click(EKeys::One);
            // Out of the column toward the stairs and the drop, with the bubbles over the hotbar.
            Look(Course + FVector(1650, -900, 150));
        },
        [this, Shot]
        {
            Record(TEXT("tour eyes underwater"), Pawn->Vitals.bEyesInWater && Pawn->Vitals.Air < 300,
                FString::Printf(TEXT("air %d"), int32(Pawn->Vitals.Air)));
            Shot(TEXT("07-water"));
        });
    Flush(TEXT("water"));

    // h. The F3 debug overlay, pressed through the router like the keyboard, over the lab controls.
    AddCase(
        TEXT("tour debug overlay"), .8,
        [this, Look, Click]
        {
            ResetPlayer({150, 0, 0});
            Click(EKeys::One);
            Look(FVector(-350, 50, 80));
            PressKey(EKeys::F3, true);
            PressKey(EKeys::F3, false);
        },
        [this, Shot]
        {
            Record(TEXT("tour F3 overlay open"), Controller->GetHud()->bOverlay, TEXT("F3 toggles the overlay"));
            Shot(TEXT("08-debug-f3"));
        });
    Flush(TEXT("debug"));

    // i. The block gallery (Docs/Blocks.md): one shot per showcase row from above and to the south-east, the
    // facing row again from the north-west (the other two furnace fronts), the animated row on two frames, and the
    // registry and every-state grids from above.
    auto RowCenter = [](int32 Row, int32 Blocks)
    {
        const FVector First = FVector(AFoundationPlayground::GetShowcaseCell(Row, 0)) * UFoundationProfile::BlockCm;
        return First + FVector((Blocks - 1) * 100 + 50, 50, 40);
    };
    auto Gallery = [this, Hover, Click](const TCHAR* Name, FVector Eye, FVector Target)
    {
        AddCase(
            FString(TEXT("tour gallery ")) + Name, 1.,
            [this, Hover, Click, Eye, Target]
            {
                ResetPlayer();
                // An empty hand: no placement preview in the picture.
                Click(EKeys::Four);
                Controller->GetHud()->bOverlay = false;
                Hover(Eye, Target);
            },
            [this, Name]
            {
                FScreenshotRequest::RequestScreenshot(
                    FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-tour-") + Name + TEXT(".png"), true, false);
            });
        AddCase(FString(TEXT("tour flush ")) + Name, .4, [] {}, [] {});
    };
    const FVector Showcase = RowCenter(3, 4);
    Gallery(TEXT("09-showcase"), Showcase + FVector(-200, 1500, 900), Showcase + FVector(-200, 0, 0));
    const TCHAR* RowShots[] = {TEXT("10-grass"), TEXT("11-leaves"), TEXT("12-translucent"), TEXT("13-axis-facing-se"),
        TEXT("14-slabs"), TEXT("15-water")};
    const int32 RowBlocks[] = {6, 10, 8, 8, 8, 9};
    for (int32 Row = 0; Row < 6; ++Row)
    {
        const FVector Center = RowCenter(Row, RowBlocks[Row]);
        const double Back = RowBlocks[Row] * 60.;
        Gallery(RowShots[Row], Center + FVector(Back * .6, 260, 230), Center);
    }
    const FVector Facing = RowCenter(3, 8);
    Gallery(TEXT("13-axis-facing-nw"), Facing + FVector(-500, -230, 230), Facing);
    // Two frames of the animated row: the sea lantern changes frame every 5 ticks (a quarter second).
    const FVector Animated = RowCenter(6, 6);
    const FVector AnimatedEye = Animated + FVector(300, 230, 180);
    auto LanternLayer = [Lab]()
    {
        const UFoundationBlockTextureTable* Table = Lab->GetView() ? Lab->GetView()->GetTextureTable() : nullptr;
        const UFoundationBlockAssets* Assets = Lab->GetAuthorityState().World.GetRegistry().GetAssets();
        const int32 Index = Assets ? Assets->Textures.IndexOfByPredicate([](const FFoundationBlockTextureAsset& T)
                                         { return T.Name == TEXT("sea_lantern"); })
                                   : INDEX_NONE;
        return Table && Index != INDEX_NONE ? Table->GetCurrentLayer(Index) : -1;
    };
    TSharedRef<int32> FirstLayer = MakeShared<int32>(-1);
    AddCase(
        TEXT("tour gallery animated a"), 1.,
        [this, Hover, Click, AnimatedEye, Animated]
        {
            ResetPlayer();
            Click(EKeys::Four);
            Hover(AnimatedEye, Animated);
        },
        [Shot, LanternLayer, FirstLayer]
        {
            *FirstLayer = LanternLayer();
            Shot(TEXT("16-animated-a"));
        });
    AddCase(
        TEXT("tour gallery animated b"), .3, [] {},
        [this, Shot, LanternLayer, FirstLayer]
        {
            const int32 A = *FirstLayer, B = LanternLayer();
            Record(TEXT("tour animated frames differ"), A >= 0 && B >= 0 && A != B,
                FString::Printf(TEXT("sea lantern layer %d then %d"), A, B));
            Shot(TEXT("17-animated-b"));
        });
    Flush(TEXT("animated"));
    const FVector Registry = (FVector(AFoundationPlayground::RegistryGalleryOrigin) +
                                 FVector(AFoundationPlayground::RegistryGalleryColumns, 22, 0)) *
        UFoundationProfile::BlockCm;
    Gallery(TEXT("18-gallery-registry"), Registry + FVector(0, 3200, 2600), Registry);
    const FVector States = (FVector(AFoundationPlayground::StateGalleryOrigin) +
                               FVector(AFoundationPlayground::StateGalleryColumns, 58, 0)) *
        UFoundationProfile::BlockCm;
    Gallery(TEXT("19-gallery-states"), States + FVector(0, 7000, 5500), States);

    // j. The creative inventory (E): a category tab with a hovered item and its tooltip, the search tab with a
    // query, and the survival inventory tab, each over the darkened world.
    auto Creative = [this](const TCHAR* Name, TFunction<void(SHudCreativeScreen&)> Arrange)
    {
        AddCase(
            FString(TEXT("tour creative ")) + Name, .8,
            [this, Arrange]
            {
                ResetPlayer({0, 0, 0});
                if (!Controller->GetPanels()->IsInventoryOpen()) Controller->GetPanels()->OpenInventory();
                if (TSharedPtr<SHudCreativeScreen> Screen = Controller->GetPanels()->GetCreativeScreen())
                    Arrange(*Screen);
            },
            [this, Name]
            {
                Record(FString(TEXT("tour creative open: ")) + Name,
                    Controller->GetPanels()->GetCreativeScreen().IsValid(),
                    TEXT("E opens the creative inventory in the Playground"));
                FScreenshotRequest::RequestScreenshot(
                    FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-tour-") + Name + TEXT(".png"), true, false);
            });
        AddCase(FString(TEXT("tour flush ")) + Name, .4, [] {}, [] {});
    };
    auto TabIndex = [](SHudCreativeScreen& Screen, const TCHAR* Id)
    {
        return Screen.GetModel().GetTabs().IndexOfByPredicate(
            [Id](const FHudCreativeModel::FTab& Tab) { return Tab.Id == Id; });
    };
    Creative(TEXT("20-creative-building"),
        [TabIndex](SHudCreativeScreen& Screen)
        {
            Screen.GetModel().SelectTab(TabIndex(Screen, TEXT("building_blocks")));
            Screen.SetMouse(FHudCreativeModel::GetSlotPosition(EHudCreativeHit::Grid, 13) + FIntPoint(8, 8));
        });
    Creative(TEXT("21-creative-search"),
        [TabIndex](SHudCreativeScreen& Screen)
        {
            Screen.GetModel().SelectTab(TabIndex(Screen, TEXT("search")));
            Screen.GetModel().SetSearch(TEXT("glass"));
            Screen.SetMouse({-200, -200});
        });
    Creative(TEXT("22-creative-natural-carried"),
        [TabIndex](SHudCreativeScreen& Screen)
        {
            Screen.GetModel().SelectTab(TabIndex(Screen, TEXT("natural_blocks")));
            Screen.GetModel().Carried = {TEXT("grass_block"), 64};
            Screen.SetMouse({120, 70});
        });
    Creative(TEXT("23-creative-inventory"),
        [TabIndex](SHudCreativeScreen& Screen)
        {
            Screen.GetModel().Carried = {};
            Screen.GetModel().SelectTab(TabIndex(Screen, TEXT("inventory")));
            Screen.SetMouse(FHudCreativeModel::GetSlotPosition(EHudCreativeHit::Hotbar, 0) + FIntPoint(8, 8));
        });
    AddCase(TEXT("tour creative close"), .3, [this] { Controller->GetPanels()->Close(); }, [] {});
#endif
}
