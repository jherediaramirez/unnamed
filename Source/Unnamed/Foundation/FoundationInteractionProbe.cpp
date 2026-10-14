#include "Foundation/FoundationRuntimeProbe.h"
#include "Algo/AllOf.h"
#include "Foundation/FoundationBreakEffects.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationInputRouter.h"
#include "Foundation/FoundationPlayground.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationExperienceOrbView.h"
#include "Foundation/FoundationItemEntityView.h"
#include "Foundation/FoundationItemMesh.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationTags.h"
#include "Hud/HudPresenter.h"
#include "Menu/MenuBrowserHost.h"
#include "Menu/MenuPreferences.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2DArray.h"
#include "StaticMeshResources.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformProcess.h"
#include "InputKeyEventArgs.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
    // The block instances the view should hold, counted from the world and the registry rather than the view: one per
    // cube, slab, layer or fluid cell; one per part of a model state (a vine with no face has none), plus one for the
    // water a model state holds.
    int32 CountExpectedBlockInstances(const FFoundationBlockWorld& World)
    {
        const FFoundationBlockRegistry& Registry = World.GetRegistry();
        TArray<TConstArrayView<int32>> Parts;
        int32 Count = 0;
        World.GetStorage().ForEachSolid(World.BoundsMin, World.BoundsMax,
            [&Registry, &Parts, &Count](const FIntVector&, const FFoundationCell& Value)
            {
                if (Registry.GetShape(Value.State) != EFoundationBlockShape::Model)
                {
                    ++Count;
                    return;
                }
                Registry.GetModelParts(Value.State, Parts);
                for (const TConstArrayView<int32> Alternatives : Parts)
                    Count += !Alternatives.IsEmpty();
                Count += Registry.GetFluid(Value.State).Amount > 0;
            });
        return Count;
    }
}

// The Playground's interaction scenarios (-FoundationSuite=interaction) in the existing Foundation harness.
// The stations stand around the world origin, clear of the movement course 60 m to the east; every position
// below is a world position. Input goes through the real router, controller and Enhanced Input (application-level
// events, not OS hardware input); aiming sets the control rotation as mouse look would. Expected values come
// from the definitions and the reference rules.
void UFoundationRuntimeProbe::AddInteractionCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    auto* Lab = Cast<AFoundationPlayground>(Pawn->GetLab());
    if (!Lab)
    {
        // A case, so the probe still finishes and reports the failure instead of waiting forever.
        AddCase(
            TEXT("playground present"), .1, [] {},
            [this] { Record(TEXT("playground present"), false, TEXT("the map has no AFoundationPlayground")); });
        return;
    }
    Extra = MakeShared<FJsonObject>();
    // The ordinary scenarios run with -InteractionPreset= when it is given, the reference otherwise; the A/B
    // cases (towers, low ceilings, cancel) always name their own preset.
    const FName StartupPreset = Lab->GetPlacementPreset();
    ScenarioPreset = TEXT("VanillaReference");
    FParse::Value(FCommandLine::Get(), TEXT("InteractionPreset="), ScenarioPreset);
    Extra->SetStringField(TEXT("scenario_preset"), ScenarioPreset);
    // Reference steps at 20 Hz: VanillaReference repeats every 0.2 s without retry; LucidFluid commits every
    // 0.1 s with one 0.1 s self-clearance retry.
    const bool bLucid = ScenarioPreset == TEXT("LucidFluid");
    const int32 ExpectedRepeat = bLucid ? 2 : 4, ExpectedRetry = bLucid ? 2 : 0;
    auto Authority = [Lab]() -> FFoundationAuthority& { return *Lab->GetAuthority(); };
    auto Player = [this, Authority]() { return Authority().FindPlayer(Pawn->Interaction.PlayerId); };
    auto Solid = [Authority](FIntVector Cell) { return !Authority().World.Get(Cell).State.IsAir(); };
    auto Look = [this](FVector Point) { Controller->SetControlRotation((Point - Pawn->GetEyePosition()).Rotation()); };
    // Fully down is the camera manager's limit, as mouse look reaches it.
    auto LookDown = [this] { Controller->SetControlRotation(FRotator(-89.9, 0, 0)); };
    auto Click = [this](FKey Key)
    {
        PressKey(Key, true);
        PressKey(Key, false);
    };
    auto Capture = [this](const FString& Name)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-interaction-") + Name + TEXT(".png"), true, false);
    };
    // The held item (items S4): an item's extruded sprite, as many triangles as its mask's quads make, placed by its
    // first-person display transform, with its sprite array on the material; nothing for None.
    auto RecordHeld = [this, Lab](const TCHAR* Name, FName Item)
    {
        const UFoundationInteractionView* View = Lab->GetView();
        const UStaticMeshComponent* Held = View->GetHeldItem();
        const UStaticMesh* Mesh = Held ? Held->GetStaticMesh() : nullptr;
        const int32 Triangles =
            Mesh && Mesh->GetRenderData() ? Mesh->GetRenderData()->LODResources[0].GetNumTriangles() : 0;
        const UFoundationItemAssets* Items = UFoundationItemAssets::LoadDefault();
        FFoundationItemModelView Model;
        if (Item.IsNone() || !Items || !Items->FindModelView(Item, TEXT("firstperson_righthand"), Model))
        {
            Record(Name, View->GetHeldSprite().IsNone() && Held && !Held->IsVisible(),
                FString::Printf(TEXT("sprite %s"), *View->GetHeldSprite().ToString()));
            return;
        }
        TArray<bool> Opaque;
        FoundationItemMesh::ReadMask(*Model.Texture, Opaque);
        const int32 Expected =
            2 * FoundationItemMesh::BuildQuads(Model.Texture->Width, Model.Texture->Height, Opaque).Num();
        const FTransform Display =
            FoundationItemMesh::MakeDisplayTransform(*Model.Display, UFoundationInteractionView::HeldModelBlockCm);
        UTexture* Sprites = nullptr;
        if (const UMaterialInterface* Material = Held ? Held->GetMaterial(0) : nullptr)
            Material->GetTextureParameterValue(TEXT("Sprites"), Sprites);
        const TSoftObjectPtr<UTexture2DArray>& Array =
            Model.Texture->Width == 32 ? Items->ItemTextures32 : Items->ItemTextures;
        Record(Name,
            View->GetHeldSprite() == Model.Texture->Name && Held && Held->IsVisible() && Triangles == Expected &&
                Held->GetRelativeTransform().Equals(Display, 1e-3) && Sprites &&
                FSoftObjectPath(Sprites) == Array.ToSoftObjectPath(),
            FString::Printf(TEXT("sprite %s, %d of %d triangles, sprites %s"), *View->GetHeldSprite().ToString(),
                Triangles, Expected, Sprites ? *Sprites->GetName() : TEXT("none")));
    };
    // foundation.Items.Entities (items S8a): the finite scenarios that count returns pin 0; the drop scenario sets 1.
    auto SetItemEntities = [](int32 Value)
    { IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Items.Entities"))->Set(Value, ECVF_SetByCode); };
    auto CountItem = [Player](int32 Item)
    {
        const FFoundationInventory& Inventory = Player()->Inventory;
        int32 Count = !Inventory.Offhand.IsEmpty() && Inventory.Offhand.Item == Item ? Inventory.Offhand.Count : 0;
        for (const FFoundationItemInstance& Stack : Inventory.Hotbar)
            Count += !Stack.IsEmpty() && Stack.Item == Item ? Stack.Count : 0;
        return Count;
    };
    // Preset is used only by the A/B cases; the others take the scenario preset.
    auto Prepare = [this, Lab](const TCHAR* Name, bool bFinite, const TCHAR* Preset = nullptr)
    {
        const FName Active = Preset ? FName(Preset) : FName(*ScenarioPreset);
        AddCase(
            FString(TEXT("prepare ")) + Name, .25,
            [this, Lab, bFinite, Active]
            {
                Controller->ResumeFoundation();
                // Also undoes the movement suite's closing screenshot, which draws the collision boxes.
                Controller->GetHud()->bHideHud = Controller->GetHud()->bOverlay = Controller->GetHud()->bShowCollision =
                    false;
                Lab->SetPlacementPreset(Active);
                if (Lab->IsFiniteSupply() != bFinite)
                    Lab->SetFiniteSupply(bFinite);
                else
                    Lab->ResetLab();
            },
            [] {});
    };

    // 0. Looking fully down must stay down: a large downward mouse move ends at the camera manager's limit
    // (-89.9), never at -90, which the engine would turn into straight up.
    AddCase(
        TEXT("mouse fully down"), .25,
        [this]
        {
            ResetPlayer();
            Controller->SetControlRotation(FRotator(-85, 0, 0));
            FrameHook = [this, bMoved = false]() mutable
            {
                if (bMoved || Frames < DeferKeysUntilFrame) return;
                bMoved = true;
                Controller->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::MouseY,
                    -5000.f, 1.f / RequestedFPS, 1, FPlatformTime::Cycles64()));
            };
        },
        [this]
        {
            const double Pitch = FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch);
            Record(TEXT("mouse look fully down stays down"),
                Pitch < -89.8 && Pawn->GetControlRotation().Vector().Z < -.9999,
                FString::Printf(TEXT("pitch %.4f"), Pitch));
        });

    // 1. The lab, its data and the HUD's authoritative items.
    Prepare(TEXT("startup"), false);
    AddCase(
        TEXT("interaction startup"), .4, [this] { ResetPlayer(); },
        [this, Lab, Authority, Player, StartupPreset, ExpectedRepeat, ExpectedRetry]
        {
            const FFoundationAuthority& A = Authority();
            const FFoundationBlockType* Stone = A.World.FindBlock(TEXT("stone"));
            Record(TEXT("playground, authority and registered player"),
                Pawn->HasInteraction() && Player() && Lab->GetView() != nullptr, Lab->GetName());
            // The whole ground is buildable: the bounds follow the ground's extent, and just beyond them is unknown
            // (never placeable). Edge cells come from the bounds themselves, not from literals.
            const int32 HalfCells = int32(AFoundationLab::GroundHalfExtentCm / UFoundationProfile::BlockCm);
            const FIntVector& Min = A.World.BoundsMin;
            const FIntVector& Max = A.World.BoundsMax;
            Record(TEXT("build area covers the whole ground"),
                Min == FIntVector(-HalfCells, -HalfCells, 0) &&
                    Max == FIntVector(HalfCells - 1, HalfCells - 1, AFoundationPlayground::BuildHeightCells - 1) &&
                    A.World.IsKnown(Min) && A.World.IsKnown(Max) && !A.World.IsKnown(Min - FIntVector(1, 0, 0)) &&
                    !A.World.IsKnown(Min - FIntVector(0, 1, 0)) && !A.World.IsKnown(Min - FIntVector(0, 0, 1)) &&
                    !A.World.IsKnown(Max + FIntVector(1, 0, 0)) && !A.World.IsKnown(Max + FIntVector(0, 1, 0)) &&
                    !A.World.IsKnown(Max + FIntVector(0, 0, 1)),
                FString::Printf(TEXT("cells %d,%d,%d .. %d,%d,%d"), Min.X, Min.Y, Min.Z, Max.X, Max.Y, Max.Z));
            Record(TEXT("stone definition"),
                Stone && Stone->ProtocolId == 1 && Stone->Hardness == 1.5f && !Stone->bReplaceable &&
                    Stone->bRequiresCorrectToolForDrops && Stone->Tags.HasTag(FoundationTags::BlockMineablePickaxe),
                TEXT("stone: protocol 1, hardness 1.5, not replaceable, drops only for a pickaxe"));
            // Without -InteractionPreset= the lab starts with the settings' default, VanillaReference.
            Record(TEXT("startup preset and infinite supply"),
                StartupPreset == FName(*ScenarioPreset) && Pawn->Interaction.Preset.Name == StartupPreset &&
                    Pawn->Interaction.HeldRepeatSteps == ExpectedRepeat &&
                    Pawn->Interaction.RetrySteps == ExpectedRetry && Player()->Inventory.bInfiniteSupply,
                Pawn->Interaction.Describe());
            const FFoundationInventory& Inventory = Player()->Inventory;
            auto Id = [&A](const FFoundationItemInstance& Stack)
            { return Stack.IsEmpty() ? FName(TEXT("empty")) : A.Rules.GetItem(Stack.Item).Id; };
            Record(TEXT("loadout"),
                Id(Inventory.Hotbar[0]) == TEXT("stone") && Id(Inventory.Hotbar[1]) == TEXT("iron_pickaxe") &&
                    Id(Inventory.Hotbar[2]) == TEXT("iron_sword") && Inventory.Hotbar[3].IsEmpty(),
                TEXT("slot 1 Stone, 2 iron pickaxe, 3 iron sword, 4-9 empty hand"));
            const int32 Expected = CountExpectedBlockInstances(A.World);
            Record(TEXT("seed blocks drawn"),
                A.World.GetSolidCount() == A.Seed.Num() && Lab->GetView()->GetDrawnCellCount() == A.Seed.Num() &&
                    Lab->GetView()->GetInstanceCount() == Expected &&
                    Lab->GetView()->GetDrawnRevision() == A.World.Revision,
                FString::Printf(TEXT("solid %d cells %d instances %d of %d sections %d"), A.World.GetSolidCount(),
                    Lab->GetView()->GetDrawnCellCount(), Lab->GetView()->GetInstanceCount(), Expected,
                    Lab->GetView()->GetSectionMeshCount()));
            const FHudSnapshot& S = Controller->GetHud()->GetPresenter()->GetSnapshot();
            Record(TEXT("HUD items authoritative"),
                S.Items == EHudDataSource::Authoritative && S.ItemViews.Num() == 9 &&
                    S.ItemViews[0].Icon == EHudItemIcon::Block && S.ItemViews[0].Block == TEXT("stone") &&
                    S.ItemViews[0].bInfinite && S.ItemViews[1].Icon == EHudItemIcon::Sprite &&
                    S.ItemViews[1].Sprite == TEXT("item/iron_pickaxe") && S.ItemViews[2].Icon == EHudItemIcon::Sprite &&
                    S.ItemViews[2].Sprite == TEXT("item/iron_sword"),
                S.ItemName);
        });
    AddCase(
        TEXT("overview capture"), .6,
        [this, Look, Capture]
        {
            ResetPlayer({-250, -150, 0});
            Look(FVector(350, 100, 60));
        },
        [Capture] { Capture(TEXT("overview")); });
    AddCase(TEXT("overview flush"), .3, [] {}, [] {});

    // 2. Item selection through the hotbar keys and wheel; the held item drives attack speed.
    AddCase(
        TEXT("select pickaxe"), .2, [this, Click] { Click(EKeys::Two); },
        [this, Player, RecordHeld]
        {
            RecordHeld(TEXT("the iron pickaxe is held as its extruded sprite"), TEXT("iron_pickaxe"));
            Record(TEXT("2 selects the iron pickaxe"),
                Player()->Inventory.Selected == 1 && Pawn->Vitals.AttackSpeed == 1.2f &&
                    Controller->GetHud()->GetPresenter()->GetSnapshot().ItemName == TEXT("Iron Pickaxe"),
                FString::Printf(
                    TEXT("slot %d speed %.2f"), Player()->Inventory.Selected + 1, Pawn->Vitals.AttackSpeed));
        });
    AddCase(
        TEXT("select sword"), .2, [this, Click] { Click(EKeys::Three); },
        [this, Player, RecordHeld]
        {
            RecordHeld(TEXT("the iron sword is held as its extruded sprite"), TEXT("iron_sword"));
            Record(TEXT("3 selects the iron sword"),
                Player()->Inventory.Selected == 2 && Pawn->Vitals.AttackSpeed == 1.6f,
                FString::Printf(
                    TEXT("speed %.2f delay %.2f ticks"), Pawn->Vitals.AttackSpeed, Pawn->Vitals.GetAttackDelay()));
        });
    AddCase(
        TEXT("wheel"), .2, [this] { Controller->GetInputRouter()->RouteWheel(-1); },
        [this, Player, RecordHeld]
        {
            RecordHeld(TEXT("the empty hand holds no item mesh"), NAME_None);
            Record(TEXT("wheel moves to the empty hand"),
                Player()->Inventory.Selected == 3 && Pawn->Vitals.AttackSpeed == 4.f,
                FString::Printf(TEXT("slot %d"), Player()->Inventory.Selected + 1));
        });

    // 3. One press places one block where the preview showed it; the press-to-commit time is measured. The placed
    // flash and the destination box show only with foundation.Place.Preview 1 (vanilla: the outline alone).
    const TSharedRef<int32> FlashesAtPress = MakeShared<int32>(0);
    // The place sound (foundation.Place.Sound): the world time of the press (the sound log drops its oldest half when
    // full, so entries are matched by time, not index).
    const TSharedRef<double> PressTime = MakeShared<double>(0);
    AddCase(
        TEXT("place press"), .45,
        [this, Click, Look, Lab, FlashesAtPress, PressTime]
        {
            *FlashesAtPress = Lab->GetView()->GetStartedFlashCount();
            *PressTime = GetWorld()->GetTimeSeconds();
            ResetPlayer({-650, 650, 0});
            Click(EKeys::One);
            Look(FVector(-450, 650, 0));
            Click(EKeys::RightMouseButton);
        },
        [this, Solid, Player, Lab, FlashesAtPress, PressTime]
        {
            Record(TEXT("single press places the previewed cell"),
                Solid({-5, 6, 0}) && Pawn->Interaction.Placed == 1 && Player()->Inventory.Hotbar[0].Count == 64,
                Pawn->Interaction.Describe());
            const int32 Mode = FoundationPlace::GetPreviewMode();
            const int32 Flashes = Lab->GetView()->GetStartedFlashCount() - *FlashesAtPress;
            Record(TEXT("placed flash follows foundation.Place.Preview"), Flashes == (Mode == 1 ? 1 : 0),
                FString::Printf(TEXT("foundation.Place.Preview %d, %d flashes started"), Mode, Flashes));
            // Stone's place event, at its break's volume and pitch (1.0 and 0.8 x the stone type's 1.0 and 1.0).
            TArray<FString> Places;
            bool bPlaceOk = true;
            const FFoundationBreakEffects* Effects = Lab->GetView()->GetBreakEffects();
            if (Effects)
                for (const FoundationBreak::FSoundLogEntry& Entry : Effects->GetSoundLog())
                    if (Entry.bPlace && Entry.Time >= *PressTime)
                    {
                        bPlaceOk &= Entry.Event == FName(TEXT("block.stone.place")) &&
                            FMath::IsNearlyEqual(Entry.Volume, 1.f, 1e-4f) &&
                            FMath::IsNearlyEqual(Entry.Pitch, .8f, 1e-4f);
                        Places.Add(FString::Printf(TEXT("%s %s volume %.3f pitch %.3f"), *Entry.Event.ToString(),
                            *Entry.File.ToString(), Entry.Volume, Entry.Pitch));
                    }
            const int32 PlaceSound = Effects ? Effects->GetSwitches().PlaceSound : -1;
            Record(TEXT("place sound follows foundation.Place.Sound"),
                Effects && bPlaceOk && Places.Num() == (PlaceSound == 1 ? 1 : 0),
                FString::Printf(TEXT("foundation.Place.Sound %d: %s"), PlaceSound,
                    Places.IsEmpty() ? TEXT("no place sound") : *FString::Join(Places, TEXT("; "))));
            const int32 Expected = CountExpectedBlockInstances(Lab->GetAuthorityState().World);
            Record(TEXT("solid block drawn after its commit"),
                Lab->GetView()->GetDrawnCellCount() == Lab->GetAuthorityState().Seed.Num() + 1 &&
                    Lab->GetView()->GetInstanceCount() == Expected,
                FString::Printf(TEXT("cells %d instances %d of %d"), Lab->GetView()->GetDrawnCellCount(),
                    Lab->GetView()->GetInstanceCount(), Expected));
        });
    AddCase(
        TEXT("preview"), .25, [] {},
        [this, Lab, Capture]
        {
            // The preview's data is the same in both modes; only mode 1 draws its box.
            const FFoundationPreview& Preview = Pawn->Interaction.Preview;
            const int32 Mode = FoundationPlace::GetPreviewMode();
            Record(TEXT("preview shows the next destination"),
                Preview.bPlacement && Preview.bValid && Preview.Cell == FIntVector(-6, 6, 0) &&
                    Lab->GetView()->IsPreviewVisible() == (Mode == 1) && Lab->GetView()->IsOutlineVisible(),
                FString::Printf(TEXT("foundation.Place.Preview %d, box %s; %s"), Mode,
                    Lab->GetView()->IsPreviewVisible() ? TEXT("drawn") : TEXT("hidden"),
                    *FoundationTargeting::Describe(Preview.Target)));
            Capture(TEXT("preview-valid"));
        });
    // 4. Held use repeats every 4 reference steps (VanillaReference; 2 with LucidFluid) until the body obstructs.
    AddCase(
        TEXT("held use"), 1.0,
        [this]
        {
            PressKey(EKeys::RightMouseButton, true);
            Extra->SetNumberField(TEXT("held_start_step"), double(Pawn->ReferenceSteps));
        },
        [this, Solid, ExpectedRepeat]
        {
            PressKey(EKeys::RightMouseButton, false);
            Record(TEXT("held use chains toward the player, then stops at its body"),
                Solid({-6, 6, 0}) && Solid({-6, 6, 1}) && !Solid({-7, 6, 1}), Pawn->Interaction.Describe());
            Record(FString::Printf(TEXT("held repeat cadence %d steps"), ExpectedRepeat),
                Pawn->Interaction.HeldRepeatSteps == ExpectedRepeat &&
                    Pawn->Interaction.LastResult.Reason == EFoundationReason::ObstructedBySelf,
                Pawn->Interaction.LastText);
        });
    AddCase(
        TEXT("obstructed preview"), .3, [] {},
        [this, Lab, Capture]
        {
            const FFoundationPreview& Preview = Pawn->Interaction.Preview;
            const int32 Mode = FoundationPlace::GetPreviewMode();
            Record(TEXT("own body shows as obstructed"),
                Preview.bPlacement && !Preview.bValid && Preview.Reason == EFoundationReason::ObstructedBySelf &&
                    Lab->GetView()->IsPreviewVisible() == (Mode == 1),
                FString::Printf(TEXT("%s; foundation.Place.Preview %d, face %s"), LexToString(Preview.Reason), Mode,
                    Lab->GetView()->IsPreviewVisible() ? TEXT("drawn") : TEXT("hidden")));
            Capture(TEXT("preview-self-obstructed"));
        });

    // 5. Jump-stacking, both presets: look down, hold jump and use. Never inside a block, one commit per update.
    for (const TCHAR* Run : {TEXT("VanillaReference"), TEXT("LucidFluid"), TEXT("VanillaReference stalls")})
    {
        const bool bStalls = FString(Run).EndsWith(TEXT("stalls"));
        const TCHAR* Preset = bStalls ? TEXT("VanillaReference") : Run;
        Prepare(*FString::Printf(TEXT("tower %s"), Run), false, Preset);
        AddCase(
            FString::Printf(TEXT("tower %s"), Run), 4.,
            [this, Click, LookDown, Solid, bStalls]
            {
                ResetPlayer({50, -550, 0});
                LookDown();
                Click(EKeys::One);
                PressKey(EKeys::SpaceBar, true);
                PressKey(EKeys::RightMouseButton, true);
                const int64 Start = Pawn->ReferenceSteps;
                const int32 Placed = Pawn->Interaction.Placed;
                FrameHook = [this, Solid, Start, Placed, bStalls, LastPlaced = Placed, Max = 0, Inside = false,
                                First = int64(-1), Frame = 0]() mutable
                {
                    // Real engine stalls: 150 ms every 30 frames while stacking.
                    if (bStalls && ++Frame % 30 == 0) FPlatformProcess::Sleep(.15f);
                    const int32 Now = Pawn->Interaction.Placed;
                    Max = FMath::Max(Max, Now - LastPlaced);
                    LastPlaced = Now;
                    if (First < 0 && Now > Placed) First = Pawn->ReferenceSteps - Start;
                    const FBox Body =
                        FFoundationSimulation::Bounds(Pawn->Simulation.Feet, Pawn->Simulation.HeightCm, 60)
                            .ExpandBy(-1e-3);
                    for (int32 Z = 0; Z < 16; ++Z)
                        if (Solid({0, -6, Z}) &&
                            Body.Intersect(FoundationGrid::CellBox({0, -6, Z}, FVector::ZeroVector)))
                            Inside = true;
                    Extra->SetNumberField(TEXT("tower_max_per_frame"), Max);
                    Extra->SetBoolField(TEXT("tower_inside"), Inside);
                    Extra->SetNumberField(TEXT("tower_first_steps"), double(First));
                };
            },
            [this, Solid, Preset = Run, Capture]
            {
                FrameHook = nullptr;
                int32 Height = 0;
                while (Solid({0, -6, Height}))
                    ++Height;
                const double First = Extra->GetNumberField(TEXT("tower_first_steps"));
                Extra->SetNumberField(FString(TEXT("tower_height_")) + Preset, Height);
                Extra->SetNumberField(FString(TEXT("tower_first_block_steps_")) + Preset, First);
                Record(FString(TEXT("jump-stack ")) + Preset,
                    Height >= 4 && !Extra->GetBoolField(TEXT("tower_inside")) &&
                        Extra->GetNumberField(TEXT("tower_max_per_frame")) <= 1,
                    FString::Printf(
                        TEXT("height %d in 4 s, first block after %.0f steps, max %0.f per frame, feet %.2f"), Height,
                        First, Extra->GetNumberField(TEXT("tower_max_per_frame")), Pawn->Simulation.Feet.Z));
                Capture(FString(TEXT("tower-")) + Preset);
                PressKey(EKeys::SpaceBar, false);
                PressKey(EKeys::RightMouseButton, false);
            });
        AddCase(
            FString::Printf(TEXT("tower settle %s"), Run), .9, [] {},
            [this, Solid, Preset = Run]
            {
                int32 Height = 0;
                while (Solid({0, -6, Height}))
                    ++Height;
                Record(FString(TEXT("stands on the tower top ")) + Preset,
                    Pawn->Simulation.bGrounded && FMath::IsNearlyEqual(Pawn->Simulation.Feet.Z, Height * 100., 1e-6),
                    FString::Printf(TEXT("feet %.4f top %d"), Pawn->Simulation.Feet.Z, Height * 100));
            });
    }

    // 6. Placing while falling, still clear of the destination, then landing on it. The hitch variant adds
    // 200 ms of catch-up right after the press is routed, so the next update runs several steps and the press
    // must act on the latest of them, not retroactively on the first.
    for (const bool bHitch : {false, true})
    {
        Prepare(bHitch ? TEXT("descent hitch") : TEXT("descent"), false);
        AddCase(
            bHitch ? TEXT("descent hitch") : TEXT("descent"), 1.2,
            [this, LookDown, Click, bHitch]
            {
                ResetPlayer({-1550, 1350, 286});
                Pawn->Simulation.bGrounded = false;
                LookDown();
                Click(EKeys::One);
                FrameHook = [this, bHitch, bPressed = false, MaxDue = 0]() mutable
                {
                    MaxDue = FMath::Max(MaxDue, Pawn->LastDueSteps);
                    Extra->SetNumberField(
                        bHitch ? TEXT("descent_hitch_max_due_steps") : TEXT("descent_max_due_steps"), MaxDue);
                    // After the first falling step, whatever the frame rate.
                    if (bPressed || Frames < DeferKeysUntilFrame || Pawn->Simulation.Steps < 1) return;
                    bPressed = true;
                    PressKey(EKeys::RightMouseButton, true);
                    PressKey(EKeys::RightMouseButton, false);
                    Extra->SetNumberField(TEXT("descent_press_step"), double(Pawn->ReferenceSteps));
                    // 200 ms of catch-up injected into the movement clock: the next update runs four or more
                    // steps, and the press must act on the latest one.
                    if (bHitch) Pawn->Simulation.Accumulator += .2;
                };
            },
            [this, Solid, bHitch]
            {
                const double Pressed = Extra->GetNumberField(TEXT("descent_press_step"));
                const double Placed = double(Pawn->Interaction.LastPlacementStep);
                Extra->SetNumberField(
                    bHitch ? TEXT("descent_hitch_steps_to_commit") : TEXT("descent_steps_to_commit"), Placed - Pressed);
                Record(bHitch ? TEXT("descending placement through a 200 ms hitch") : TEXT("descending placement"),
                    Solid({-16, 13, 0}) && Pawn->Simulation.bGrounded &&
                        FMath::IsNearlyEqual(Pawn->Simulation.Feet.Z, 100., 1e-6) && (!bHitch || Placed - Pressed >= 3),
                    FString::Printf(
                        TEXT("pressed at step %.0f, committed at %.0f; max %0.f steps in one update; feet %.4f "
                             "dropped %.3f s"),
                        Pressed, Placed,
                        Extra->GetNumberField(
                            bHitch ? TEXT("descent_hitch_max_due_steps") : TEXT("descent_max_due_steps")),
                        Pawn->Simulation.Feet.Z, Pawn->Simulation.DroppedSeconds));
            });
    }

    // 7. Crouch at a ledge edge, look back down at its side and build out.
    Prepare(TEXT("ledge"), false);
    AddCase(
        TEXT("crouch to the edge"), 1.5,
        [this, Click]
        {
            ResetPlayer({-1100, 50, 300});
            Click(EKeys::One);
            PressKey(EKeys::C, true);
            PressKey(EKeys::W, true);
        },
        [this]
        {
            PressKey(EKeys::W, false);
            Record(TEXT("crouch keeps the ledge"), Pawn->Simulation.Feet.Z == 300 && Pawn->Simulation.Feet.X < -970,
                Pawn->Simulation.Feet.ToCompactString());
        });
    AddCase(
        TEXT("edge build"), .4,
        [this, Click]
        {
            Controller->SetControlRotation(FVector(-1, 0, -6).Rotation());
            Click(EKeys::RightMouseButton);
        },
        [this, Solid, Capture]
        {
            Record(TEXT("built out from the edge while crouched"), Solid({-10, 0, 2}) && Pawn->Simulation.Feet.Z == 300,
                Pawn->Interaction.LastText);
            Capture(TEXT("edge-build"));
            PressKey(EKeys::C, false);
        });

    // 8. Low ceiling: the head stops with the feet exactly one block up; contact is not obstruction.
    for (const TCHAR* Preset : {TEXT("VanillaReference"), TEXT("LucidFluid")})
    {
        Prepare(*FString::Printf(TEXT("ceiling %s"), Preset), false, Preset);
        AddCase(
            FString::Printf(TEXT("low ceiling %s"), Preset), 1.2,
            [this, Click, LookDown]
            {
                ResetPlayer({650, -850, 0});
                LookDown();
                Click(EKeys::One);
                PressKey(EKeys::SpaceBar, true);
                PressKey(EKeys::RightMouseButton, true);
            },
            [this, Solid, Preset]
            {
                PressKey(EKeys::SpaceBar, false);
                PressKey(EKeys::RightMouseButton, false);
                Extra->SetBoolField(FString(TEXT("low_ceiling_placed_")) + Preset, Solid({6, -9, 0}));
                Record(FString(TEXT("low ceiling exact contact ")) + Preset, true,
                    FString::Printf(TEXT("placed=%d feet %.4f (LucidFluid's retry catches the 1-step window)"),
                        Solid({6, -9, 0}), Pawn->Simulation.Feet.Z));
            });
    }

    // 9. LucidFluid: releasing use before the body clears cancels the retry; nothing places later.
    Prepare(TEXT("cancel"), false, TEXT("LucidFluid"));
    AddCase(
        TEXT("cancel before clearance"), .06,
        [this, Click, LookDown]
        {
            ResetPlayer({-1250, -1550, 0});
            LookDown();
            Click(EKeys::One);
            PressKey(EKeys::SpaceBar, true);
            PressKey(EKeys::RightMouseButton, true);
        },
        [this] { PressKey(EKeys::RightMouseButton, false); });
    AddCase(
        TEXT("cancel settle"), 1., [] {},
        [this, Solid]
        {
            PressKey(EKeys::SpaceBar, false);
            Record(TEXT("release before clearance places nothing"), !Solid({-13, -16, 0}),
                FString::Printf(TEXT("retries %d canceled %d"), Pawn->Interaction.RetriesStarted,
                    Pawn->Interaction.RetriesCanceled));
        });

    // 10. Mining stone: pickaxe 8 steps, hand 150, and breaking the block underfoot drops the player.
    Prepare(TEXT("mining"), false);
    AddCase(
        TEXT("mine underfoot"), 1.2,
        [this, Click, LookDown]
        {
            ResetPlayer({650, 350, 100});
            LookDown();
            Click(EKeys::Two);
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this, Solid, Capture]
        {
            PressKey(EKeys::LeftMouseButton, false);
            Extra->SetNumberField(TEXT("pickaxe_steps"), Pawn->Interaction.LastBreakSteps);
            Record(TEXT("pickaxe breaks the block underfoot in 8 steps"),
                !Solid({6, 3, 0}) && Extra->GetNumberField(TEXT("pickaxe_steps")) == 8,
                FString::Printf(TEXT("%0.f steps"), Extra->GetNumberField(TEXT("pickaxe_steps"))));
            Record(TEXT("and the player falls to the floor"),
                Pawn->Simulation.bGrounded && FMath::IsNearlyEqual(Pawn->Simulation.Feet.Z, 0., 1e-6),
                Pawn->Simulation.Feet.ToCompactString());
        });
    AddCase(
        TEXT("mine by hand"), 8.2,
        [this, Click, Look]
        {
            ResetPlayer({550, 450, 0});
            Look(FVector(700, 450, 50));
            Click(EKeys::Four);
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this, Solid]
        {
            PressKey(EKeys::LeftMouseButton, false);
            Extra->SetNumberField(TEXT("hand_steps"), Pawn->Interaction.LastBreakSteps);
            // Stone needs a pickaxe to drop: by hand it mines at the ineligible rate, 1 / 1.5 / 100 per step.
            Record(TEXT("hand breaks stone in 150 steps"),
                !Solid({6, 4, 0}) && Extra->GetNumberField(TEXT("hand_steps")) == 150,
                FString::Printf(TEXT("%0.f steps"), Extra->GetNumberField(TEXT("hand_steps"))));
        });
    AddCase(
        TEXT("mining capture"), .5,
        [this, Click, Look]
        {
            ResetPlayer({550, 350, 0});
            Look(FVector(700, 350, 50));
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this, Capture]
        {
            Record(TEXT("mining in progress is shown"),
                Pawn->Interaction.bMining && Pawn->Interaction.MiningProgress > 0,
                FString::Printf(TEXT("%.0f%%"), Pawn->Interaction.MiningProgress * 100));
            Capture(TEXT("mining"));
        });
    AddCase(
        TEXT("mining release"), .2, [this] { PressKey(EKeys::LeftMouseButton, false); },
        [this, Solid]
        {
            Record(TEXT("release cancels the break and keeps the block"),
                !Pawn->Interaction.bMining && Solid({6, 3, 0}) == false && Solid({7, 3, 0}),
                Pawn->Interaction.LastText);
        });

    // 10b. Plants (phase 6, S7): a dandelion on grass pops when the grass is mined; a dandelion on stone is refused;
    // tall grass places both halves and goes as one unit. Cells and instances stay exact (one per model part).
    Prepare(TEXT("plants"), false);
    const FIntVector Grass(-6, 13, 0), Flower(-6, 13, 1), Rock(-6, 16, 0), Meadow(-6, 19, 0);
    auto Give = [Authority, Player](const TCHAR* Item)
    {
        FFoundationInventory& Inventory = Player()->Inventory;
        Inventory.SetSlot(4, Inventory.MakeStack(Authority().Rules.FindItem(Item), 64));
    };
    auto Seed = [Authority](FIntVector Cell, const TCHAR* Block)
    { Authority().World.Set(Cell, Authority().World.FindBlock(Block)->GetDefaultState()); };
    auto BlockAt = [Authority](FIntVector Cell)
    { return Authority().World.GetRegistry().ToString(Authority().World.Get(Cell).State); };
    auto Drawn = [this, Lab, Authority](const TCHAR* Name)
    {
        const int32 Expected = CountExpectedBlockInstances(Authority().World);
        Record(Name,
            Lab->GetView()->GetDrawnCellCount() == Authority().World.GetSolidCount() &&
                Lab->GetView()->GetInstanceCount() == Expected,
            FString::Printf(TEXT("cells %d of %d, instances %d of %d"), Lab->GetView()->GetDrawnCellCount(),
                Authority().World.GetSolidCount(), Lab->GetView()->GetInstanceCount(), Expected));
    };
    AddCase(
        TEXT("plant a flower"), .4,
        [this, Click, Look, Give, Seed, Grass]
        {
            ResetPlayer({-850, 1350, 0});
            Seed(Grass, TEXT("grass_block"));
            Give(TEXT("dandelion"));
            Click(EKeys::Five);
            Look(FVector(-550, 1350, 100));
            Click(EKeys::RightMouseButton);
        },
        [this, BlockAt, Drawn, Capture, Flower]
        {
            Record(TEXT("a dandelion places on grass"), BlockAt(Flower) == TEXT("dandelion"), BlockAt(Flower));
            Drawn(TEXT("the dandelion is drawn"));
            Capture(TEXT("plant-flower"));
        });
    AddCase(
        TEXT("mine under the flower"), 1.6,
        [this, Click, Look, Authority]
        {
            Extra->SetNumberField(TEXT("pops_before"), Authority().Pops);
            Click(EKeys::Four);
            Look(FVector(-600, 1350, 50));
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this, Solid, Authority, Drawn, Grass, Flower]
        {
            PressKey(EKeys::LeftMouseButton, false);
            const int32 Pops = Authority().Pops - int32(Extra->GetNumberField(TEXT("pops_before")));
            Record(TEXT("mining the grass pops the dandelion"), !Solid(Grass) && !Solid(Flower) && Pops == 1,
                FString::Printf(TEXT("grass %d flower %d pops %d"), Solid(Grass), Solid(Flower), Pops));
            Drawn(TEXT("both are gone from the view"));
        });
    AddCase(
        TEXT("flower on stone"), .4,
        [this, Click, Look, Seed, Rock]
        {
            ResetPlayer({-850, 1650, 0});
            Seed(Rock, TEXT("stone"));
            Click(EKeys::Five);
            Look(FVector(-550, 1650, 100));
            Click(EKeys::RightMouseButton);
        },
        [this, Solid, Rock]
        {
            Record(TEXT("a dandelion is refused on stone"),
                !Solid(Rock + FIntVector(0, 0, 1)) &&
                    Pawn->Interaction.LastResult.Reason == EFoundationReason::CannotSurvive,
                LexToString(Pawn->Interaction.LastResult.Reason));
        });
    AddCase(
        TEXT("plant tall grass"), .4,
        [this, Click, Look, Give, Seed, Meadow]
        {
            ResetPlayer({-850, 1950, 0});
            Seed(Meadow, TEXT("grass_block"));
            Give(TEXT("tall_grass"));
            Click(EKeys::Five);
            Look(FVector(-550, 1950, 100));
            Click(EKeys::RightMouseButton);
        },
        [this, BlockAt, Drawn, Meadow]
        {
            Record(TEXT("tall grass places both halves"),
                BlockAt(Meadow + FIntVector(0, 0, 1)) == TEXT("tall_grass[half=lower]") &&
                    BlockAt(Meadow + FIntVector(0, 0, 2)) == TEXT("tall_grass[half=upper]"),
                BlockAt(Meadow + FIntVector(0, 0, 2)));
            Drawn(TEXT("both halves are drawn"));
        });
    AddCase(
        TEXT("mine the upper half"), .4,
        [this, Click, Look]
        {
            Click(EKeys::Four);
            Look(FVector(-550, 1950, 250));
            Click(EKeys::LeftMouseButton);
        },
        [this, Solid, Drawn, Meadow]
        {
            Record(TEXT("mining the upper half takes the lower"),
                !Solid(Meadow + FIntVector(0, 0, 1)) && !Solid(Meadow + FIntVector(0, 0, 2)) && Solid(Meadow),
                Pawn->Interaction.LastText);
            Drawn(TEXT("the halves are gone from the view"));
        });

    // 10c. Torches (torch session T3): a torch on a wall's side hangs there as a wall torch, and mining the wall pops
    // it through S7's cascade. Cells and instances stay exact.
    {
        Prepare(TEXT("torches"), false);
        const FIntVector TorchWall(-5, 22, 1), WallTorch(-6, 22, 1), Footing(-5, 22, 0);
        AddCase(
            TEXT("hang a wall torch"), .4,
            [this, Click, Look, Give, Seed, TorchWall, Footing]
            {
                ResetPlayer({-850, 2250, 0});
                Seed(Footing, TEXT("stone"));
                Seed(TorchWall, TEXT("stone"));
                Give(TEXT("torch"));
                Click(EKeys::Five);
                Look(FVector(-500, 2250, 150));
                Click(EKeys::RightMouseButton);
            },
            [this, BlockAt, Drawn, Capture, WallTorch]
            {
                Record(TEXT("a torch on a wall's side hangs as a wall torch"),
                    BlockAt(WallTorch) == TEXT("wall_torch[facing=west]"), BlockAt(WallTorch));
                Drawn(TEXT("the wall torch is drawn"));
                Capture(TEXT("wall-torch"));
            });
        AddCase(
            TEXT("mine the torch's wall"), 1.,
            [this, Click, Look, Authority]
            {
                Extra->SetNumberField(TEXT("torch_pops_before"), Authority().Pops);
                Click(EKeys::Two);
                // Beside the torch, whose box covers only the middle of the wall's face.
                Look(FVector(-500, 2215, 150));
                PressKey(EKeys::LeftMouseButton, true);
            },
            [this, Solid, Authority, Drawn, TorchWall, WallTorch, Footing]
            {
                PressKey(EKeys::LeftMouseButton, false);
                const int32 Pops = Authority().Pops - int32(Extra->GetNumberField(TEXT("torch_pops_before")));
                Record(TEXT("mining the wall pops the wall torch"),
                    !Solid(TorchWall) && !Solid(WallTorch) && Solid(Footing) && Pops == 1,
                    FString::Printf(TEXT("wall %d torch %d pops %d"), Solid(TorchWall), Solid(WallTorch), Pops));
                Drawn(TEXT("the wall and its torch are gone from the view"));
            });
    }

    // 11. Combat: full sword hit, the target's cooldown, a falling critical, recharge and a wall.
    Prepare(TEXT("combat"), false);
    AddCase(
        TEXT("sword charge"), .8,
        [this, Click, Look]
        {
            ResetPlayer({0, 800, 0});
            Click(EKeys::Three);
            Look(FVector(0, 1000, 20));
        },
        [] {});
    // The hit and the weak repeat count reference steps, not wall time: until the repeat resolves, the pawn takes at
    // most one step per frame (the profile's catch-up limit, restored after), so the hook sees every step and a loaded
    // machine's catch-up steps cannot end the target's cooldown in between. Game time may fall behind wall time here.
    // The strong click waits for a full charge, the weak one comes 4 steps (0.2 s) after the hit; each press waits for
    // the next step, however many frames take none.
    struct FCombatSteps
    {
        int32 SavedCatchupSteps = 0;
        int64 HitStep = -1, AttackStep = -1;
        uint32 Sequence = 0;
        bool bHitClicked = false, bClicked = false;
        FFoundationResult Hit, Repeat;
        void RestoreCatchup()
        {
            if (SavedCatchupSteps > 0) GetMutableDefault<UFoundationProfile>()->MaxCatchupSteps = SavedCatchupSteps;
            SavedCatchupSteps = 0;
        }
        // Also when the probe is torn down inside the case, so a later session in the same editor keeps the limit.
        ~FCombatSteps() { RestoreCatchup(); }
    };
    const TSharedRef<FCombatSteps> CombatSteps = MakeShared<FCombatSteps>();
    AddCase(
        TEXT("sword hit"), .1,
        [this, Click, CombatSteps]
        {
            UFoundationProfile* Profile = GetMutableDefault<UFoundationProfile>();
            CombatSteps->SavedCatchupSteps = Profile->MaxCatchupSteps;
            Profile->MaxCatchupSteps = 1;
            CombatSteps->HitStep = CombatSteps->AttackStep = -1;
            CombatSteps->bHitClicked = CombatSteps->bClicked = false;
            CombatSteps->Sequence = Pawn->Interaction.LastResult.Sequence;
            bHoldCase = true;
            FrameHook = [this, Click, CombatSteps]
            {
                if (!bHoldCase) return;
                if (!CombatSteps->bHitClicked)
                {
                    if (Pawn->Vitals.GetAttackStrength() < 1) return;
                    CombatSteps->bHitClicked = true;
                    Click(EKeys::LeftMouseButton);
                }
                const FFoundationResult& R = Pawn->Interaction.LastResult;
                const int64 Step = Pawn->Simulation.Steps;
                if (R.Sequence != CombatSteps->Sequence && R.Type == EFoundationCommandType::Attack)
                {
                    CombatSteps->Sequence = R.Sequence;
                    if (CombatSteps->HitStep < 0)
                    {
                        CombatSteps->HitStep = Step;
                        CombatSteps->Hit = R;
                        return;
                    }
                    CombatSteps->AttackStep = Step;
                    CombatSteps->Repeat = R;
                    bHoldCase = false;
                }
                else if (CombatSteps->HitStep >= 0 && !CombatSteps->bClicked && Step - CombatSteps->HitStep == 4)
                {
                    CombatSteps->bClicked = true;
                    Click(EKeys::LeftMouseButton);
                }
            };
        },
        [this, CombatSteps, Capture]
        {
            CombatSteps->RestoreCatchup();
            const FFoundationResult& Hit = CombatSteps->Hit;
            Record(TEXT("full sword hit deals 6"),
                CombatSteps->HitStep >= 0 && Hit.TargetHealth == 14 && Hit.DamageApplied == 6,
                FString::Printf(TEXT("health %.2f strength %.3f"), Hit.TargetHealth, Hit.Strength));
            Capture(TEXT("hit"));
        });
    AddCase(
        TEXT("weak repeat"), .2, [] {},
        [this, Authority, CombatSteps]
        {
            const FFoundationResult& R = CombatSteps->Repeat;
            // -1: the hit or the repeat never happened.
            const int64 StepsAfterHit = CombatSteps->HitStep >= 0 && CombatSteps->AttackStep >= 0
                ? CombatSteps->AttackStep - CombatSteps->HitStep
                : -1;
            Record(TEXT("a weak hit inside the target's cooldown adds nothing"),
                StepsAfterHit == 5 && Authority().Targets[0].Health.Health == 14 &&
                    R.Reason == EFoundationReason::TargetCooldown && R.Strength < .5f,
                FString::Printf(
                    TEXT("%lld steps after the hit: strength %.3f damage %.3f"), StepsAfterHit, R.Strength, R.Damage));
        });
    AddCase(
        TEXT("recharge"), .9, [] {},
        [this]
        {
            Record(TEXT("recharge indicator full again"),
                Pawn->Vitals.GetAttackStrength() == 1 &&
                    HudModel::IsReadyMarkerVisible(Controller->GetHud()->GetPresenter()->GetSnapshot()),
                FString::Printf(TEXT("strength %.3f"), Pawn->Vitals.GetAttackStrength()));
        });
    AddCase(
        TEXT("jump for a critical"), .42, [this] { PressKey(EKeys::SpaceBar, true); },
        [this, Click]
        {
            PressKey(EKeys::SpaceBar, false);
            Record(TEXT("falling at the attack"), !Pawn->Simulation.bGrounded && Pawn->Simulation.FallDistanceCm > 0,
                FString::Printf(TEXT("fall %.2f cm"), Pawn->Simulation.FallDistanceCm));
            Click(EKeys::LeftMouseButton);
        });
    AddCase(
        TEXT("critical result"), .3, [] {},
        [this, Authority, Capture]
        {
            const FFoundationResult& R = Pawn->Interaction.LastResult;
            Record(TEXT("falling full-charge critical deals 9"),
                R.bCritical && R.Damage == 9 && Authority().Targets[0].Health.Health == 5,
                FString::Printf(TEXT("crit %d damage %.2f health %.2f"), R.bCritical, R.Damage,
                    Authority().Targets[0].Health.Health));
            Capture(TEXT("critical"));
        });
    AddCase(
        TEXT("rising is no critical"), .8,
        [this]
        {
            ResetPlayer({0, 800, 0});
            Controller->SetControlRotation((FVector(0, 1000, 20) - FVector(0, 800, 162)).Rotation());
        },
        [this] { PressKey(EKeys::SpaceBar, true); });
    AddCase(
        TEXT("rising attack"), .1, [] {},
        [this, Click]
        {
            Click(EKeys::LeftMouseButton);
            Extra->SetBoolField(
                TEXT("rising_at_click"), !Pawn->Simulation.bGrounded && Pawn->Simulation.LastDisplacement.Z > 0);
        });
    AddCase(
        TEXT("rising result"), .3, [this] { PressKey(EKeys::SpaceBar, false); },
        [this]
        {
            const FFoundationResult& R = Pawn->Interaction.LastResult;
            Record(TEXT("an attack while rising is not critical"),
                Extra->GetBoolField(TEXT("rising_at_click")) && R.Type == EFoundationCommandType::Attack &&
                    !R.bCritical && R.Strength > .9f,
                FString::Printf(TEXT("crit %d damage %.2f strength %.2f"), R.bCritical, R.Damage, R.Strength));
        });
    AddCase(
        TEXT("wall blocks the attack"), .4,
        [this, Click, Look]
        {
            ResetPlayer({500, 800, 0});
            Look(FVector(500, 1000, 120));
            Click(EKeys::LeftMouseButton);
        },
        [this, Authority]
        {
            Record(TEXT("target behind a wall is not hit"),
                Authority().Targets[1].Health.Health == 20 &&
                    Pawn->Interaction.Target.Kind == EFoundationHitKind::Static,
                FoundationTargeting::Describe(Pawn->Interaction.Target));
        });

    // 12. The in-world reset control: blocks, targets and session come back; crouch places on it instead.
    AddCase(
        TEXT("reset station"), .5,
        [this, Click, Look]
        {
            ResetPlayer({-150, 50, 0});
            Click(EKeys::One);
            Look(FVector(-300, 50, 60));
        },
        [this, Click, Authority]
        {
            Extra->SetNumberField(TEXT("session_before_reset"), Authority().GetSessionId());
            Click(EKeys::RightMouseButton);
        });
    AddCase(
        TEXT("reset result"), .4, [] {},
        [this, Authority]
        {
            const FFoundationAuthority& A = Authority();
            Record(TEXT("reset control resets the lab"),
                A.GetSessionId() != int32(Extra->GetNumberField(TEXT("session_before_reset"))) &&
                    A.World.GetSolidCount() == A.Seed.Num() && A.Targets[0].Health.Health == 20 &&
                    Pawn->Simulation.Feet.Size2D() < 1,
                FString::Printf(TEXT("session %d blocks %d"), A.GetSessionId(), A.World.GetSolidCount()));
        });

    // 13. Finite fixture: the same commit path consumes, returns and wears; the last point breaks the tool.
    Prepare(TEXT("finite"), true);
    AddCase(
        TEXT("finite place"), .45,
        [this, Click, Look, SetItemEntities]
        {
            // The break below returns straight to the inventory (foundation.Items.Entities 0, as before items S8a).
            SetItemEntities(0);
            ResetPlayer({-650, 650, 0});
            Click(EKeys::One);
            Look(FVector(-450, 650, 0));
            Click(EKeys::RightMouseButton);
        },
        [this, Player, Solid]
        {
            Record(TEXT("finite placement consumes one"),
                Solid({-5, 6, 0}) && Player()->Inventory.Hotbar[0].Count == 63,
                FString::Printf(TEXT("count %d"), Player()->Inventory.Hotbar[0].Count));
        });
    AddCase(
        TEXT("finite mine"), 1.,
        [this, Click, Look, Player]
        {
            Click(EKeys::Two);
            Player()->Inventory.Hotbar[1].Damage = 249;
            Look(FVector(-450, 650, 50));
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this, Authority, Player, Solid, Capture]
        {
            PressKey(EKeys::LeftMouseButton, false);
            const FFoundationInventory& Inventory = Player()->Inventory;
            // Stone's loot table returns cobblestone (loot step 2a; foundation.Loot.AllBlocks 0 returned stone).
            const int32 Cobblestone = Authority().Rules.FindItem(TEXT("cobblestone"));
            int32 Cobblestones = 0;
            for (const FFoundationItemInstance& Stack : Inventory.Hotbar)
                Cobblestones += !Stack.IsEmpty() && Stack.Item == Cobblestone ? Stack.Count : 0;
            Record(TEXT("break returns the block's loot once and the last durability point breaks the pickaxe"),
                !Solid({-5, 6, 0}) && Inventory.Hotbar[0].Count == 63 && Cobblestones == 1 &&
                    Inventory.Hotbar[1].IsEmpty(),
                FString::Printf(TEXT("stone %d cobblestone %d pickaxe empty %d"), Inventory.Hotbar[0].Count,
                    Cobblestones, Inventory.Hotbar[1].IsEmpty()));
            Capture(TEXT("finite-hud"));
        });

    // 13a. Item entities (items S8a): a mined block pops out as a drop that lands on the ground and is pulled into the
    // inventory when the player walks over it (foundation.Items.Entities 1). Then the capture pair: a block drop and an
    // item drop at rest before the camera, and the same view with the switch at 0 (the control: no drops).
    Prepare(TEXT("drops"), true);
    AddCase(
        TEXT("drop place"), .45,
        [this, Click, Look, SetItemEntities]
        {
            SetItemEntities(1);
            // Three blocks back, so the drop lands outside the pickup box (the body grown by a block).
            ResetPlayer({-750, 650, 0});
            Click(EKeys::One);
            Look(FVector(-450, 650, 0));
            Click(EKeys::RightMouseButton);
        },
        [this, Solid] { Record(TEXT("drop scenario: stone placed"), Solid({-5, 6, 0}), Pawn->Interaction.LastText); });
    AddCase(
        TEXT("drop mine"), 1.6,
        [this, Click, Look]
        {
            Click(EKeys::Two);
            Look(FVector(-450, 650, 50));
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this, Lab, Authority, Solid, CountItem]
        {
            PressKey(EKeys::LeftMouseButton, false);
            const TArray<FFoundationItemEntity>& Drops = Authority().GetItemEntities();
            const int32 Cobblestone = Authority().Rules.FindItem(TEXT("cobblestone"));
            const FFoundationItemEntityView* View = Lab->GetView()->GetItemEntityView();
            Record(TEXT("a mined block pops out as a drop resting on the ground, not in the inventory"),
                !Solid({-5, 6, 0}) && Drops.Num() == 1 && Drops[0].Item == Cobblestone && Drops[0].Count == 1 &&
                    Drops[0].bOnGround && FMath::Abs(Drops[0].LocalCm.Z) < .01 && CountItem(Cobblestone) == 0 && View &&
                    View->GetDrawnCount() == 1,
                Drops.IsEmpty() ? FString(TEXT("no drop"))
                                : FString::Printf(TEXT("drops %d at %s, cobblestone in inventory %d, drawn %d"),
                                      Drops.Num(), *Drops[0].LocalCm.ToCompactString(), CountItem(Cobblestone),
                                      View ? View->GetDrawnCount() : -1));
        });
    AddCase(
        TEXT("drop walk over"), 1.,
        [this, Lab, Authority]
        {
            if (FFoundationBreakEffects* Effects = Lab->GetView()->GetBreakEffects()) Effects->ClearSoundLog();
            const TArray<FFoundationItemEntity>& Drops = Authority().GetItemEntities();
            if (!Drops.IsEmpty())
            {
                const FVector Drop = Authority().World.OriginCm + Drops[0].LocalCm;
                Controller->SetControlRotation(FRotator(0, (Drop - Pawn->Simulation.Feet).Rotation().Yaw, 0));
            }
            PressKey(EKeys::W, true);
        },
        [this, Lab, Authority, CountItem]
        {
            PressKey(EKeys::W, false);
            const FFoundationAuthority& A = Authority();
            const FFoundationEvent* Picked = nullptr;
            for (const FFoundationEvent& Event : A.GetEvents())
                if (Event.Kind == EFoundationEventKind::PickedUp) Picked = &Event;
            const int32 Cobblestone = A.Rules.FindItem(TEXT("cobblestone"));
            Record(TEXT("walking over the drop pulls it into the inventory"),
                A.GetItemEntities().IsEmpty() && CountItem(Cobblestone) == 1 && Picked && Picked->bWholeStack &&
                    Picked->Count == 1 && Picked->Item == Cobblestone && Picked->Player == Pawn->Interaction.PlayerId,
                FString::Printf(TEXT("drops %d, cobblestone %d, picked up %d, feet %s"), A.GetItemEntities().Num(),
                    CountItem(Cobblestone), Picked ? Picked->Count : 0, *Pawn->Simulation.Feet.ToCompactString()));
            // The pickup sound (foundation.Sound.Events 1, the default): logged at the drop with vanilla's call values.
            const FFoundationBreakEffects* Effects = Lab->GetView()->GetBreakEffects();
            const FoundationBreak::FSoundLogEntry* Sound = nullptr;
            for (const FoundationBreak::FSoundLogEntry& Entry :
                Effects ? Effects->GetSoundLog() : TConstArrayView<FoundationBreak::FSoundLogEntry>())
                if (Entry.Event == TEXT("entity.item.pickup")) Sound = &Entry;
            Record(TEXT("the pickup plays entity.item.pickup at the drop"),
                Sound && Picked && Sound->Location.Equals(Picked->LocationCm, .01),
                Sound ? FString::Printf(TEXT("%s volume %.3f pitch %.3f at %s"), *Sound->File.ToString(), Sound->Volume,
                            Sound->Pitch, *Sound->Location.ToCompactString())
                      : FString(TEXT("no pickup sound logged")));
        });
    Prepare(TEXT("drops capture"), true);
    AddCase(
        TEXT("drop capture on"), 1.,
        [this, Authority, Look]
        {
            // A block drop (cobblestone, the block's ground display) and an item drop (coal, its extruded sprite), at
            // rest after their fall, the camera standing 2 m away.
            ResetPlayer({-750, 650, 0});
            FFoundationAuthority& A = Authority();
            A.SpawnItem(A.Rules.FindItem(TEXT("cobblestone")), 1, FVector(-560, 625, 0) - A.World.OriginCm,
                FVector::ZeroVector, FoundationItemEntities::BlockDropPickupDelay);
            A.SpawnItem(A.Rules.FindItem(TEXT("coal")), 1, FVector(-560, 685, 0) - A.World.OriginCm,
                FVector::ZeroVector, FoundationItemEntities::BlockDropPickupDelay);
            Look(FVector(-560, 655, 0));
        },
        [this, Lab, Authority, Capture]
        {
            const FFoundationItemEntityView* View = Lab->GetView()->GetItemEntityView();
            // Each drawn as one copy (items S8b: a block's instances or the sprites that draw).
            const bool bOneCopyEach = View &&
                Algo::AllOf(Authority().GetItemEntities(),
                    [View](const FFoundationItemEntity& Entity) { return View->GetDrawnCopies(Entity.Id) == 1; });
            Record(TEXT("a block drop and an item drop are drawn"), View && View->GetDrawnCount() == 2 && bOneCopyEach,
                FString::Printf(
                    TEXT("drawn %d %s"), View ? View->GetDrawnCount() : -1, View ? *View->Describe() : TEXT("")));
            Capture(TEXT("drops-on"));
        });
    // The next case resets the lab in this tick; the shot is taken at the frame's end.
    AddCase(TEXT("drop capture on hold"), .2, [] {}, [] {});
    Prepare(TEXT("drops control"), true);
    AddCase(
        TEXT("drop capture off"), .6,
        [this, Look, SetItemEntities]
        {
            SetItemEntities(0);
            ResetPlayer({-750, 650, 0});
            Look(FVector(-560, 655, 0));
        },
        [this, Lab, Capture, SetItemEntities]
        {
            const FFoundationItemEntityView* View = Lab->GetView()->GetItemEntityView();
            Record(TEXT("the control draws no drops"), View && View->GetDrawnCount() == 0,
                FString::Printf(TEXT("drawn %d"), View ? View->GetDrawnCount() : -1));
            Capture(TEXT("drops-off"));
        });
    AddCase(TEXT("drop capture off hold"), .2, [] {}, [SetItemEntities] { SetItemEntities(1); });

    // 13d. Experience orbs (survival step 12, foundation.Items.Xp 1): a mined diamond ore pays 3 to 7 points in orbs
    // that fly to the player and fill the XP bar. Shot as xp-on just after the break (the orbs popping out), and the
    // control xp-off: the same break with the switch at 0 pays no orbs.
    auto SetXp = [](int32 Value)
    { IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Items.Xp"))->Set(Value, ECVF_SetByCode); };
    struct FXpState
    {
        int32 Total = 0, Spawned = 0, Picked = 0, Sounds = 0;
    };
    TSharedRef<FXpState> Xp = MakeShared<FXpState>();
    auto MineOre = [this, Lab, Authority, Click, Look, Xp]
    {
        ResetPlayer({-750, 650, 0});
        FFoundationAuthority& A = Authority();
        A.World.Set({-5, 6, 0}, A.World.FindBlock(TEXT("diamond_ore"))->GetDefaultState());
        Xp->Total = Pawn->Vitals.TotalExperience;
        Xp->Spawned = A.OrbsSpawned;
        Xp->Picked = A.OrbsPickedUp;
        const FFoundationExperienceOrbView* OrbView = Lab->GetView()->GetExperienceOrbView();
        Xp->Sounds = OrbView ? OrbView->PickupSounds : 0;
        Click(EKeys::Two);
        Look(FVector(-450, 650, 50));
        PressKey(EKeys::LeftMouseButton, true);
    };
    Prepare(TEXT("xp"), true);
    AddCase(
        TEXT("xp mine"), .95,
        [SetXp, MineOre]
        {
            SetXp(1);
            MineOre();
        },
        [this, Lab, Authority, Solid, Capture, Xp]
        {
            PressKey(EKeys::LeftMouseButton, false);
            const FFoundationAuthority& A = Authority();
            const FFoundationExperienceOrbView* View = Lab->GetView()->GetExperienceOrbView();
            Record(TEXT("a mined diamond ore pays orbs, drawn"),
                !Solid({-5, 6, 0}) && A.OrbsSpawned > Xp->Spawned && View && View->GetDrawnCount() > 0,
                FString::Printf(TEXT("broken %d, orbs spawned %d, drawn %d"), !Solid({-5, 6, 0}),
                    A.OrbsSpawned - Xp->Spawned, View ? View->GetDrawnCount() : -1));
            Capture(TEXT("xp-on"));
        });
    AddCase(
        TEXT("xp fly in"), 2.,
        [this, Lab]
        {
            if (FFoundationBreakEffects* Effects = Lab->GetView()->GetBreakEffects()) Effects->ClearSoundLog();
        },
        [this, Lab, Authority, Xp]
        {
            const FFoundationAuthority& A = Authority();
            int32 Points = 0, Events = 0;
            for (const FFoundationEvent& Event : A.GetEvents())
                if (Event.Kind == EFoundationEventKind::ExperiencePickedUp &&
                    Event.Player == Pawn->Interaction.PlayerId)
                {
                    Points += Event.Count;
                    ++Events;
                }
            const FFoundationVitals& V = Pawn->Vitals;
            const FHudSnapshot& S = Controller->GetHud()->GetPresenter()->GetSnapshot();
            const int32 Gained = V.TotalExperience - Xp->Total;
            Record(TEXT("the orbs fly in and their 3 to 7 points reach the vitals"),
                A.GetExperienceOrbs().IsEmpty() && A.OrbsPickedUp - Xp->Picked == Events && Gained == Points &&
                    Gained >= 3 && Gained <= 7,
                FString::Printf(TEXT("orbs left %d, picked %d, events %d, points %d, gained %d"),
                    A.GetExperienceOrbs().Num(), A.OrbsPickedUp - Xp->Picked, Events, Points, Gained));
            Record(TEXT("the XP bar moves: the HUD shows the vitals' level and progress"),
                S.Level == V.Level && FMath::IsNearlyEqual(S.Experience, double(V.Progress)) &&
                    (S.Level > 0 || HudModel::GetExperienceWidth(S.Experience) > 0),
                FString::Printf(TEXT("level %d, progress %.4f, bar width %d"), S.Level, S.Experience,
                    HudModel::GetExperienceWidth(S.Experience)));
            const FFoundationExperienceOrbView* View = Lab->GetView()->GetExperienceOrbView();
            Record(TEXT("each pickup calls the pickup sound hook"),
                View && View->PickupSounds - Xp->Sounds == Events && Events > 0,
                FString::Printf(
                    TEXT("hook calls %d, pickups %d"), View ? View->PickupSounds - Xp->Sounds : -1, Events));
        });
    AddCase(
        TEXT("xp icons"), 1.,
        [this, Authority, Look]
        {
            // The eleven icons (values at each threshold) 9 blocks ahead, beyond the 8-block follow range, so they
            // rest.
            FFoundationAuthority& A = Authority();
            int32 Seed = 1;
            for (const int32 Value : FoundationExperienceOrbs::IconThresholds)
            {
                A.SpawnExperience(FVector(150, 400 + 50 * Seed, 50) - A.World.OriginCm, Value, Seed);
                ++Seed;
            }
            Look(FVector(150, 650, 0));
        },
        [this, Lab, Authority, Capture]
        {
            const FFoundationExperienceOrbView* View = Lab->GetView()->GetExperienceOrbView();
            TArray<int32> Icons;
            if (View)
                for (int32 Index = 0; Index < View->GetDrawnData().Num();
                    Index += FFoundationExperienceOrbView::DataFloats)
                    Icons.AddUnique(FMath::RoundToInt(View->GetDrawnData()[Index]));
            Record(TEXT("the eleven orb icons are drawn"),
                Authority().GetExperienceOrbs().Num() == 11 && View && View->GetDrawnCount() == 11 && Icons.Num() == 11,
                FString::Printf(TEXT("orbs %d, drawn %d, icons %d; %s"), Authority().GetExperienceOrbs().Num(),
                    View ? View->GetDrawnCount() : -1, Icons.Num(), View ? *View->Describe() : TEXT("")));
            Capture(TEXT("xp-icons"));
        });
    // The next case resets the lab in this tick; the shot is taken at the frame's end.
    AddCase(TEXT("xp icons hold"), .2, [] {}, [] {});
    Prepare(TEXT("xp control"), true);
    AddCase(
        TEXT("xp mine off"), 1.1,
        [SetXp, MineOre]
        {
            SetXp(0);
            MineOre();
        },
        [this, Lab, Authority, Solid, Capture, Xp]
        {
            PressKey(EKeys::LeftMouseButton, false);
            const FFoundationAuthority& A = Authority();
            const FFoundationExperienceOrbView* View = Lab->GetView()->GetExperienceOrbView();
            Record(TEXT("the control pays no orbs"),
                !Solid({-5, 6, 0}) && A.OrbsSpawned == Xp->Spawned && View && View->GetDrawnCount() == 0,
                FString::Printf(TEXT("broken %d, orbs spawned %d, drawn %d"), !Solid({-5, 6, 0}),
                    A.OrbsSpawned - Xp->Spawned, View ? View->GetDrawnCount() : -1));
            Capture(TEXT("xp-off"));
        });
    AddCase(TEXT("xp control hold"), .2, [] {}, [SetXp] { SetXp(1); });

    // 13c. Eating (step 10, foundation.Items.Use 1): holding use with apples at hunger 10 eats one in 1.6 s (hunger
    // 14, one apple fewer, the eat and burp sounds); letting go mid-way eats nothing. Then the capture pair: eating
    // mid-way, and the same hold with the switch at 0 (the control: no eating pose).
    auto SetItemUse = [](int32 Value)
    { IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Items.Use"))->Set(Value, ECVF_SetByCode); };
    auto StartEating = [this, Authority](int32 Food)
    {
        ResetPlayer({-750, 650, 0});
        Pawn->Interaction.SetCreativeSlot(Authority(), Pawn->Vitals, 0, TEXT("apple"), 3);
        PressKey(EKeys::One, true);
        PressKey(EKeys::One, false);
        Pawn->Vitals.Food = Food;
        Pawn->Vitals.Saturation = 0;
        // Up into the open sky: nothing in reach, so the use goes to the item.
        Controller->SetControlRotation(FRotator(30, 0, 0));
        PressKey(EKeys::RightMouseButton, true);
    };
    Prepare(TEXT("eating"), true);
    AddCase(
        TEXT("eat hold"), 2.,
        [Lab, SetItemUse, StartEating]
        {
            SetItemUse(1);
            if (FFoundationBreakEffects* Effects = Lab->GetView()->GetBreakEffects()) Effects->ClearSoundLog();
            StartEating(10);
        },
        [this, Lab, Authority, CountItem]
        {
            PressKey(EKeys::RightMouseButton, false);
            const FFoundationAuthority& A = Authority();
            const int32 Apple = A.Rules.FindItem(TEXT("apple"));
            int32 Ate = 0;
            for (const FFoundationEvent& Event : A.GetEvents())
                Ate += Event.Kind == EFoundationEventKind::Ate && Event.Item == Apple;
            Record(TEXT("holding use eats an apple: hunger 10 to 14, one apple fewer"),
                Ate == 1 && Pawn->Vitals.Food == 14 && FMath::IsNearlyEqual(Pawn->Vitals.Saturation, 2.4f) &&
                    CountItem(Apple) == 2,
                FString::Printf(TEXT("ate %d, hunger %d saturation %.2f, apples %d, eaten %d"), Ate, Pawn->Vitals.Food,
                    Pawn->Vitals.Saturation, CountItem(Apple), A.ItemsEaten));
            int32 Eats = 0, Burps = 0;
            const FFoundationBreakEffects* Effects = Lab->GetView()->GetBreakEffects();
            for (const FoundationBreak::FSoundLogEntry& Entry :
                Effects ? Effects->GetSoundLog() : TConstArrayView<FoundationBreak::FSoundLogEntry>())
            {
                Eats += Entry.Event == TEXT("entity.generic.eat");
                Burps += Entry.Event == TEXT("entity.player.burp");
            }
            Record(TEXT("eating plays entity.generic.eat, then one entity.player.burp"), Eats >= 7 && Burps == 1,
                FString::Printf(TEXT("eat sounds %d, burps %d"), Eats, Burps));
        });
    AddCase(
        TEXT("eat release"), .6, [StartEating] { StartEating(14); },
        [this, Authority, CountItem]
        {
            const bool bWasEating = Pawn->Interaction.bUsingItem;
            PressKey(EKeys::RightMouseButton, false);
            const int32 Apple = Authority().Rules.FindItem(TEXT("apple"));
            Record(TEXT("letting go mid-way eats nothing"),
                bWasEating && Pawn->Vitals.Food == 14 && CountItem(Apple) == 3,
                FString::Printf(
                    TEXT("was eating %d, hunger %d, apples %d"), bWasEating, Pawn->Vitals.Food, CountItem(Apple)));
        });
    AddCase(
        TEXT("eat capture on"), .8, [StartEating] { StartEating(10); },
        [this, Lab, Capture, Player]
        {
            const double Blend = Lab->GetView()->GetEatBlend();
            Record(TEXT("mid-way: the held apple is in its eating pose"),
                Pawn->Interaction.bUsingItem && Blend > .99 && Player() && Player()->Use.bActive,
                FString::Printf(TEXT("using %d, ticks %d of %d, blend %.2f"), Pawn->Interaction.bUsingItem,
                    Pawn->Interaction.UseTicks, Pawn->Interaction.UseDuration, Blend));
            Capture(TEXT("eating-on"));
        });
    AddCase(TEXT("eat capture on hold"), .2, [] {}, [this] { PressKey(EKeys::RightMouseButton, false); });
    Prepare(TEXT("eating control"), true);
    AddCase(
        TEXT("eat capture off"), .8,
        [SetItemUse, StartEating]
        {
            SetItemUse(0);
            StartEating(10);
        },
        [this, Lab, Capture]
        {
            const double Blend = Lab->GetView()->GetEatBlend();
            Record(TEXT("the control (switch 0) does not eat"),
                !Pawn->Interaction.bUsingItem && Blend == 0 && Pawn->Vitals.Food == 10,
                FString::Printf(
                    TEXT("using %d, blend %.2f, hunger %d"), Pawn->Interaction.bUsingItem, Blend, Pawn->Vitals.Food));
            Capture(TEXT("eating-off"));
        });
    AddCase(
        TEXT("eat capture off hold"), .2, [] {},
        [this, SetItemUse]
        {
            PressKey(EKeys::RightMouseButton, false);
            SetItemUse(1);
        });

    // 13b. Hands and items: pick block, drop (items S8b: throw, re-pickup, the stack, the switch-0 control), offhand
    // fallback, Toggle use, water.
    Prepare(TEXT("hands"), false);
    AddCase(
        TEXT("pick block"), .3,
        [this, Click, Look]
        {
            ResetPlayer({150, 50, 0});
            Click(EKeys::Three);
            Look(FVector(350, 50, 50));
            Click(EKeys::MiddleMouseButton);
        },
        [this, Player, Click]
        {
            Record(TEXT("middle click picks the Stone slot"), Player()->Inventory.Selected == 0,
                FString::Printf(TEXT("slot %d"), Player()->Inventory.Selected + 1));
            Click(EKeys::Q);
        });
    // Items S8b (foundation.Items.Entities 1): Q throws one stone from the stack (creative too), picked up again once
    // its 40-tick delay is over; Ctrl+Q throws the whole stack, drawn as 5 copies (the capture pair's "on" frame); with
    // the switch at 0, Q is refused and nothing is thrown (the control).
    const TSharedRef<FVector> ThrowLook = MakeShared<FVector>(FVector(350, 50, 0));
    auto Thrown = [Authority](int32 Index) -> const FFoundationItemEntity*
    {
        const TArray<FFoundationItemEntity>& Entities = Authority().GetItemEntities();
        return Entities.IsValidIndex(Index) ? &Entities[Index] : nullptr;
    };
    AddCase(
        TEXT("drop"), .2, [] {},
        [this, Player, Authority, Thrown]
        {
            const FFoundationItemEntity* Stone = Thrown(0);
            Record(TEXT("Q throws one stone with a 40-tick pickup delay"),
                Pawn->Interaction.LastResult.IsCommitted() && Player()->Inventory.Hotbar[0].Count == 63 &&
                    Authority().GetItemEntities().Num() == 1 && Stone &&
                    Stone->Item == Authority().Rules.FindItem(TEXT("stone")) && Stone->Count == 1 &&
                    Stone->PickupDelay > 0 && Stone->PickupDelay <= 40,
                FString::Printf(TEXT("%s; stone %d, thrown %d, delay %d"), *Pawn->Interaction.LastText,
                    Player()->Inventory.Hotbar[0].Count, Authority().GetItemEntities().Num(),
                    Stone ? Stone->PickupDelay : -1));
        });
    AddCase(TEXT("drop settle"), 1., [] {}, [] {});
    AddCase(
        TEXT("drop re-pickup"), 1.5,
        [this, Authority, Thrown]
        {
            // Standing just short of the thrown stone (it may rest against the block): it comes back once its delay is
            // over.
            if (const FFoundationItemEntity* Stone = Thrown(0))
                ResetPlayer(Authority().World.OriginCm + Stone->LocalCm - FVector(80, 0, 0));
        },
        [this, Player, Authority]
        {
            const FFoundationEvent* Picked = nullptr;
            for (const FFoundationEvent& Event : Authority().GetEvents())
                if (Event.Kind == EFoundationEventKind::PickedUp) Picked = &Event;
            Record(TEXT("the thrown stone is picked up again"),
                Authority().GetItemEntities().IsEmpty() && Player()->Inventory.Hotbar[0].Count == 64 && Picked &&
                    Picked->Item == Authority().Rules.FindItem(TEXT("stone")) && Picked->Count == 1,
                FString::Printf(TEXT("thrown %d, stone %d, picked up %d"), Authority().GetItemEntities().Num(),
                    Player()->Inventory.Hotbar[0].Count, Picked ? Picked->Count : 0));
        });
    AddCase(
        TEXT("drop stack ctrl"), .1,
        [this, Click, Look]
        {
            // Toward open floor (-Y), so the stack rests in the open for the capture.
            ResetPlayer({150, 50, 0});
            Look(FVector(150, -250, 50));
            PressKey(EKeys::LeftControl, true);
        },
        [Click] { Click(EKeys::Q); });
    AddCase(
        TEXT("drop stack"), 1.2, [] {},
        [this, Player, Lab, Thrown, Authority, ThrowLook]
        {
            PressKey(EKeys::LeftControl, false);
            const FFoundationItemEntity* Stack = Thrown(0);
            const FFoundationItemEntityView* View = Lab->GetView()->GetItemEntityView();
            const int32 Copies = View && Stack ? View->GetDrawnCopies(Stack->Id) : 0;
            Record(TEXT("Ctrl+Q throws the whole stack, drawn as 5 copies"),
                Player()->Inventory.Hotbar[0].IsEmpty() && Authority().GetItemEntities().Num() == 1 && Stack &&
                    Stack->Count == 64 && Stack->bOnGround && Copies == 5,
                FString::Printf(TEXT("slot 1 %d, thrown %d of %d at %s, copies %d"),
                    Player()->Inventory.Hotbar[0].Count, Authority().GetItemEntities().Num(), Stack ? Stack->Count : 0,
                    Stack ? *Stack->LocalCm.ToCompactString() : TEXT("-"), Copies));
            if (Stack) *ThrowLook = Authority().World.OriginCm + Stack->LocalCm;
        });
    AddCase(
        TEXT("throw capture on"), .3, [Look, ThrowLook] { Look(*ThrowLook); },
        [Capture] { Capture(TEXT("throws-on")); });
    AddCase(TEXT("throw capture on hold"), .2, [] {}, [] {});
    Prepare(TEXT("hands control"), false);
    AddCase(
        TEXT("throw capture off"), .6,
        [this, Look, SetItemEntities, ThrowLook]
        {
            SetItemEntities(0);
            ResetPlayer({150, 50, 0});
            Look(*ThrowLook);
        },
        // Q a frame after the switch, once the pawn has copied it into the authority's options.
        [Click] { Click(EKeys::Q); });
    AddCase(
        TEXT("throw capture off check"), .3, [] {},
        [this, Player, Lab, Authority, Capture]
        {
            const FFoundationItemEntityView* View = Lab->GetView()->GetItemEntityView();
            Record(TEXT("drop is refused without item entities"),
                Pawn->Interaction.LastResult.Reason == EFoundationReason::DropDeferred &&
                    Player()->Inventory.Hotbar[0].Count == 64 && Authority().GetItemEntities().IsEmpty() && View &&
                    View->GetDrawnCount() == 0,
                FString::Printf(TEXT("%s; drawn %d"), *Pawn->Interaction.LastText, View ? View->GetDrawnCount() : -1));
            Capture(TEXT("throws-off"));
        });
    AddCase(TEXT("throw capture off hold"), .2, [] {}, [SetItemEntities] { SetItemEntities(1); });
    // One input per frame: Enhanced Input fires same-frame actions in mapping order, not key order.
    AddCase(
        TEXT("offhand swap"), .2,
        [this, Click]
        {
            ResetPlayer({-650, 650, 0});
            Click(EKeys::One);
        },
        [this, Click] { Click(EKeys::F); });
    AddCase(TEXT("offhand select sword"), .2, [] {}, [this, Click] { Click(EKeys::Three); });
    AddCase(
        TEXT("offhand fallback"), .4,
        [this, Click, Look]
        {
            Look(FVector(-450, 650, 0));
            Click(EKeys::RightMouseButton);
        },
        [this, Player, Solid, Authority]
        {
            const FFoundationInventory& Inventory = Player()->Inventory;
            const FFoundationResult& R = Pawn->Interaction.LastResult;
            Record(TEXT("sword passes, the offhand places"),
                Solid({-5, 6, 0}) && R.bPlaced && R.Hand == EFoundationHand::Off && !Inventory.Offhand.IsEmpty() &&
                    Inventory.Selected == 2,
                FString::Printf(TEXT("%s; hand %s; offhand %s; slot %d"), *Pawn->Interaction.LastText,
                    R.Hand == EFoundationHand::Off ? TEXT("off") : TEXT("main"),
                    Inventory.Offhand.IsEmpty() ? TEXT("empty")
                                                : *Authority().Rules.GetItem(Inventory.Offhand.Item).Id.ToString(),
                    Inventory.Selected + 1));
        });
    AddCase(
        TEXT("toggle use on"), .7,
        [this, Click, Look]
        {
            Controller->GetGameInstance()->GetSubsystem<UMenuPreferences>()->ApplyChanges(
                TEXT("{\"settings\":{\"toggleUse\":true}}"));
            ResetPlayer({-1650, -650, 0});
            Look(FVector(-1450, -650, 0));
            Click(EKeys::RightMouseButton);
            Extra->SetNumberField(TEXT("toggle_before"), Pawn->Interaction.Placed);
        },
        [this, Click]
        {
            const int32 Placed = Pawn->Interaction.Placed - int32(Extra->GetNumberField(TEXT("toggle_before")));
            Record(TEXT("Toggle use keeps placing after a tap"), Pawn->Interaction.bUseHeld && Placed >= 2,
                FString::Printf(TEXT("placed %d while toggled on"), Placed));
            Click(EKeys::RightMouseButton);
        });
    AddCase(
        TEXT("toggle use off"), .5, [] {},
        [this]
        {
            Record(TEXT("a second tap toggles use off"), !Pawn->Interaction.bUseHeld,
                FString::Printf(TEXT("placed %d"), Pawn->Interaction.Placed));
            Controller->GetGameInstance()->GetSubsystem<UMenuPreferences>()->ApplyChanges(
                TEXT("{\"settings\":{\"toggleUse\":false}}"));
        });
    AddCase(
        TEXT("water mining"), 2.6,
        [this, Click, Look]
        {
            ResetPlayer({1150, 550, 0});
            Click(EKeys::Two);
            Look(FVector(1250, 650, 50));
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this, Solid]
        {
            PressKey(EKeys::LeftMouseButton, false);
            Record(TEXT("eyes underwater: pickaxe x0.2 is 38 steps"),
                Pawn->Simulation.bGrounded && !Solid({12, 6, 0}) && Pawn->Interaction.LastBreakSteps == 38,
                FString::Printf(
                    TEXT("%d steps, eyes in water %d"), Pawn->Interaction.LastBreakSteps, Pawn->Vitals.bEyesInWater));
        });
    // 13c. The player's own health: real damage and healing reach the hearts; death disables actions.
    AddCase(
        TEXT("player damage"), .3,
        [this]
        {
            ResetPlayer({-650, 650, 0});
            Pawn->Vitals.bNaturalRegeneration = false;
            Pawn->Vitals.Hurt(EFoundationDamage::Generic, 5);
        },
        [this]
        {
            Record(TEXT("damage reaches the hearts"),
                Controller->GetHud()->GetPresenter()->GetSnapshot().Health == 15 && Pawn->Vitals.Health == 15,
                Pawn->Vitals.LastDamage);
            Pawn->Vitals.Heal(3);
        });
    AddCase(
        TEXT("player heal"), .3, [] {},
        [this]
        {
            Record(TEXT("healing reaches the hearts"), Controller->GetHud()->GetPresenter()->GetSnapshot().Health == 18,
                FString::Printf(TEXT("health %.1f"), Pawn->Vitals.Health));
            Pawn->Vitals.bNaturalRegeneration = true;
        });
    AddCase(
        TEXT("dead player"), .4,
        [this, Click, Look]
        {
            ResetPlayer({-650, 1250, 0});
            Click(EKeys::One);
            Look(FVector(-450, 1250, 0));
            Pawn->Vitals.Hurt(EFoundationDamage::Kill, TNumericLimits<float>::Max());
            Click(EKeys::RightMouseButton);
        },
        [this, Solid]
        {
            Record(TEXT("a dead player cannot place"), Pawn->Vitals.IsDead() && !Solid({-5, 12, 0}),
                Pawn->Interaction.LastText);
        });
    AddCase(
        TEXT("respawned player"), 1.2, [] {},
        [this, Solid]
        {
            Record(TEXT("respawn restores play; the press made while dead never acted"),
                !Pawn->Vitals.IsDead() && !Solid({-5, 12, 0}), Pawn->Vitals.Describe());
        });

    // 13d. A respawn restarts movement's step count but never the interaction's clock: with LucidFluid's commit
    // cadence, a placement made late before death must not hold back the first use after the respawn.
    Prepare(TEXT("respawn clock"), false, TEXT("LucidFluid"));
    AddCase(
        TEXT("late placement"), 1.6,
        [this, Click, Look]
        {
            ResetPlayer({-650, 1250, 0});
            Click(EKeys::One);
            Look(FVector(-450, 1250, 0));
        },
        [this, Click] { Click(EKeys::RightMouseButton); });
    AddCase(
        TEXT("death after placing"), .3, [] {},
        [this, Solid]
        {
            Extra->SetNumberField(TEXT("respawn_placed_step"), double(Pawn->Interaction.LastPlacementStep));
            Record(TEXT("placed before dying"), Solid({-5, 12, 0}), Pawn->Interaction.LastText);
            Pawn->Vitals.Hurt(EFoundationDamage::Kill, TNumericLimits<float>::Max());
        });
    AddCase(
        TEXT("respawn"), 1.3, [] {},
        [this, Click, Look]
        {
            Extra->SetNumberField(TEXT("respawn_movement_steps"), double(Pawn->Simulation.Steps));
            Look(FVector(-150, 250, 0));
            Click(EKeys::RightMouseButton);
        });
    AddCase(
        TEXT("use after respawn"), .3, [] {},
        [this, Solid]
        {
            Record(TEXT("use places right after a respawn"),
                !Pawn->Vitals.IsDead() && Solid({-2, 2, 0}) &&
                    Pawn->Interaction.LastPlacementStep > int64(Extra->GetNumberField(TEXT("respawn_placed_step"))),
                FString::Printf(TEXT("placed at step %lld after %.0f; movement restarted at %.0f steps; %s"),
                    Pawn->Interaction.LastPlacementStep, Extra->GetNumberField(TEXT("respawn_placed_step")),
                    Extra->GetNumberField(TEXT("respawn_movement_steps")), *Pawn->Interaction.LastText));
        });

    // 14. Focus loss cancels a live break; nothing resumes on return.
    Prepare(TEXT("focus"), false);
    AddCase(
        TEXT("focus mining"), .3,
        [this, Click, Look]
        {
            ResetPlayer({550, 350, 0});
            Click(EKeys::Two);
            Look(FVector(700, 350, 50));
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this] { Controller->HandleFocusChanged(false); });
    AddCase(
        TEXT("focus return"), .4,
        [this]
        {
            Controller->HandleFocusChanged(true);
            Controller->ResumeFoundation();
        },
        [this, Solid]
        {
            PressKey(EKeys::LeftMouseButton, false);
            Record(TEXT("focus loss cancels mining; no automatic action on return"),
                !Pawn->Interaction.bMining && Solid({6, 3, 0}), Pawn->Interaction.LastText);
        });

    // 14b. Pausing (Escape) cancels a live break the same way: the context removal ends the held action, and
    // resuming starts nothing. By hand (45 steps), so the break is still live whatever the frame rate. This is the
    // authority-path replacement for the token fixture's "action cancel".
    Prepare(TEXT("pause"), false);
    AddCase(
        TEXT("pause mining"), .5,
        [this, Click, Look]
        {
            ResetPlayer({550, 450, 0});
            Look(FVector(700, 450, 50));
            Click(EKeys::Four);
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this, Solid]
        {
            Record(TEXT("mining is live before the pause"),
                Pawn->Interaction.bMining && Pawn->Interaction.MiningProgress > 0 && Solid({6, 4, 0}),
                FString::Printf(TEXT("%.0f%%"), Pawn->Interaction.MiningProgress * 100));
            PressKey(EKeys::Escape, true);
            PressKey(EKeys::Escape, false);
        });
    AddCase(
        TEXT("pause result"), .4, [] {},
        [this, Solid]
        {
            Record(TEXT("pause cancels mining and keeps the block"),
                Controller->GetMenuHost()->IsOpen() && !Pawn->Interaction.bMining && Solid({6, 4, 0}),
                Pawn->Interaction.LastText);
        });
    AddCase(
        TEXT("pause resume"), .4, [this] { Controller->ResumeFoundation(); },
        [this, Solid]
        {
            PressKey(EKeys::LeftMouseButton, false);
            Record(TEXT("nothing resumes automatically after the pause"),
                Controller->IsGameplay() && !Pawn->Interaction.bMining && Solid({6, 4, 0}), Pawn->Interaction.LastText);
        });

    // 15. Cost: idle, 4096 cubes (fixture setup through a stress player's commands) and 1000 place/break cycles.
    AddCase(TEXT("perf idle"), 3., [this] { ResetPlayer({0, 0, 0}); }, [] {});
    AddCase(
        TEXT("perf build 4096"), 1.,
        [this, Lab, Authority]
        {
            FFoundationAuthority& A = Authority();
            FFoundationVitals Stress;
            const int32 Id = A.AddPlayer();
            const uint64 MemoryBefore = FPlatformMemory::GetStats().UsedPhysical;
            const double Start = FPlatformTime::Seconds();
            int32 Placed = 0;
            uint32 Sequence = 1;
            for (int32 Z = 0; Z < 16; ++Z)
                for (int32 Y = 3; Y < 19; ++Y)
                    for (int32 X = -18; X < -2; ++X)
                    {
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
                        Placed += A.Execute(Id, Command, Stress).bPlaced ? 1 : 0;
                    }
            const double BuildMs = (FPlatformTime::Seconds() - Start) * 1000.;
            Extra->SetNumberField(TEXT("stress_build_cubes"), Placed);
            Extra->SetNumberField(TEXT("stress_build_ms"), BuildMs);
            Extra->SetNumberField(TEXT("stress_build_memory_delta_mb"),
                (double(FPlatformMemory::GetStats().UsedPhysical) - double(MemoryBefore)) / (1024. * 1024.));
            Extra->SetNumberField(TEXT("stress_player"), Id);
        },
        [this, Lab, Authority]
        {
            Record(TEXT("4096 cubes built and drawn"),
                Extra->GetNumberField(TEXT("stress_build_cubes")) == 4096 &&
                    Lab->GetView()->GetDrawnCellCount() == Authority().World.GetSolidCount() &&
                    Lab->GetView()->GetInstanceCount() == CountExpectedBlockInstances(Authority().World),
                FString::Printf(TEXT("%0.f cubes in %.2f ms; %d instances in %d section meshes; world %llu bytes"),
                    Extra->GetNumberField(TEXT("stress_build_cubes")), Extra->GetNumberField(TEXT("stress_build_ms")),
                    Lab->GetView()->GetInstanceCount(), Lab->GetView()->GetSectionMeshCount(),
                    uint64(Authority().World.GetAllocatedSize())));
        });
    AddCase(
        TEXT("perf idle 4096"), 3.,
        [this, Look]
        {
            ResetPlayer({-100, -100, 0});
            Look(FVector(-1000, 1000, 400));
        },
        [this, Capture] { Capture(TEXT("stress-4096")); });
    AddCase(
        TEXT("perf cycles 1000"), 2.,
        [this, Authority, Lab]
        {
            FFoundationAuthority& A = Authority();
            const int32 Id = int32(Extra->GetNumberField(TEXT("stress_player")));
            FFoundationItemDefinition Tool = A.Rules.Items[A.Rules.FindItem(TEXT("iron_pickaxe"))];
            Tool.Id = TEXT("probe_instant_tool");
            for (FFoundationMiningRule& Rule : Tool.MiningRules)
                if (Rule.Speed > 0) Rule.Speed = 1000;
            A.Rules.Items.Add(Tool);
            FFoundationPlayer* Stress = A.FindPlayer(Id);
            Stress->Inventory.SetSlot(5, Stress->Inventory.MakeStack(A.Rules.FindItem(TEXT("probe_instant_tool")), 1));
            FFoundationVitals Vitals;
            const uint64 MemoryBefore = FPlatformMemory::GetStats().UsedPhysical;
            uint32 Sequence = 100000;
            int32 Cycles = 0;
            const double Start = FPlatformTime::Seconds();
            for (int32 Index = 0; Index < 1000; ++Index)
            {
                FFoundationPlayerFacts Facts;
                Facts.EyeCm = FVector(1550, -1550, 120);
                Facts.FeetCm = Facts.EyeCm + FVector(0, 0, 5000);
                Facts.Aim = FVector(0, 0, -1);
                Facts.Step = Sequence;
                A.SetFacts(Id, Facts);
                auto Run = [&](EFoundationCommandType Type, int32 Slot)
                {
                    A.FindPlayer(Id)->Inventory.Select(Slot);
                    FFoundationCommand Command;
                    Command.SessionId = A.GetSessionId();
                    Command.Sequence = Sequence++;
                    Command.Type = Type;
                    Command.Item = A.FindPlayer(Id)->Inventory.GetHand(EFoundationHand::Main).GetRef();
                    Command.Target = A.Resolve(Facts);
                    FIntVector Destination;
                    EFoundationReason Ignored;
                    A.GetDestination(Command.Target, Destination, Ignored);
                    Command.Destination = Destination;
                    Command.DestinationRevision = A.World.Get(Destination).Revision;
                    return A.Execute(Id, Command, Vitals);
                };
                const bool bPlaced = Run(EFoundationCommandType::UseOn, 0).bPlaced;
                Facts.Step = Sequence;
                A.SetFacts(Id, Facts);
                const bool bBroken = Run(EFoundationCommandType::MineStart, 5).bBroken;
                Cycles += bPlaced && bBroken ? 1 : 0;
            }
            Extra->SetNumberField(TEXT("stress_cycles"), Cycles);
            Extra->SetNumberField(TEXT("stress_cycles_ms"), (FPlatformTime::Seconds() - Start) * 1000.);
            Extra->SetNumberField(TEXT("stress_cycles_memory_delta_mb"),
                (double(FPlatformMemory::GetStats().UsedPhysical) - double(MemoryBefore)) / (1024. * 1024.));
        },
        [this, Lab, Authority]
        {
            const int32 Id = int32(Extra->GetNumberField(TEXT("stress_player")));
            Record(TEXT("1000 place/break cycles, bounded"),
                Extra->GetNumberField(TEXT("stress_cycles")) == 1000 &&
                    Authority().FindPlayer(Id)->Results.Num() <= Authority().Rules.ResultCacheSize &&
                    Lab->GetView()->GetDrawnCellCount() == Authority().World.GetSolidCount() &&
                    Lab->GetView()->GetInstanceCount() == CountExpectedBlockInstances(Authority().World),
                FString::Printf(TEXT("%0.f cycles in %.2f ms; memory delta %.2f MB; cache %d"),
                    Extra->GetNumberField(TEXT("stress_cycles")), Extra->GetNumberField(TEXT("stress_cycles_ms")),
                    Extra->GetNumberField(TEXT("stress_cycles_memory_delta_mb")),
                    Authority().FindPlayer(Id)->Results.Num()));
            Authority().RemovePlayer(Id);
        });
    AddCase(
        TEXT("perf reset"), .4, [Lab] { Lab->ResetLab(); },
        [this, Authority, Lab]
        {
            Record(TEXT("reset after stress restores the seed"),
                Authority().World.GetSolidCount() == Authority().Seed.Num() &&
                    Lab->GetView()->GetDrawnCellCount() == Authority().Seed.Num() &&
                    Lab->GetView()->GetInstanceCount() == CountExpectedBlockInstances(Authority().World),
                FString::Printf(TEXT("%d blocks"), Authority().World.GetSolidCount()));
            // Press-to-commit latency and interaction cost samples.
            TArray<double> Latency = Pawn->Interaction.PressToCommitMs;
            Latency.Sort();
            if (Latency.Num())
            {
                Extra->SetNumberField(TEXT("press_commit_samples"), Latency.Num());
                Extra->SetNumberField(TEXT("press_commit_p50_ms"), Latency[Latency.Num() / 2]);
                Extra->SetNumberField(TEXT("press_commit_max_ms"), Latency.Last());
            }
            TArray<float> Cost = Pawn->InteractionMs;
            Cost.Sort();
            if (Cost.Num())
            {
                Extra->SetNumberField(TEXT("interaction_updates"), Cost.Num());
                Extra->SetNumberField(TEXT("interaction_p50_ms"), Cost[Cost.Num() / 2]);
                Extra->SetNumberField(
                    TEXT("interaction_p95_ms"), Cost[FMath::Min(Cost.Num() - 1, int32(Cost.Num() * .95))]);
                Extra->SetNumberField(
                    TEXT("interaction_p99_ms"), Cost[FMath::Min(Cost.Num() - 1, int32(Cost.Num() * .99))]);
                Extra->SetNumberField(TEXT("interaction_max_ms"), Cost.Last());
            }
            Extra->SetNumberField(TEXT("view_last_ms"), Lab->GetView()->GetLastViewMs());
            Extra->SetNumberField(
                TEXT("used_physical_mb"), double(FPlatformMemory::GetStats().UsedPhysical) / (1024. * 1024.));
            Record(TEXT("interaction logic p95 under the 0.5 ms budget"),
                Cost.Num() && Cost[FMath::Min(Cost.Num() - 1, int32(Cost.Num() * .95))] < .5f,
                FString::Printf(TEXT("p95 %.4f ms over %d updates"),
                    Cost.Num() ? Cost[FMath::Min(Cost.Num() - 1, int32(Cost.Num() * .95))] : -1.f, Cost.Num()));
        });
    AddCase(TEXT("capture flush"), .6, [] {}, [] {});
#endif
}
