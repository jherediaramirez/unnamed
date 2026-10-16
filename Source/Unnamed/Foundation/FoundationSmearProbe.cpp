#include "Foundation/FoundationRuntimeProbe.h"
#include "Components/StaticMeshComponent.h"
#include "Foundation/FoundationArrowView.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationItemEntityView.h"
#include "Foundation/FoundationMobView.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationPlayground.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// K-PLAYER-7 (the smear fix): what moves in front of the camera, rendered with the game's own anti-aliasing (run at
// full cost: TestFoundationRuntime.ps1 -Editor -Suite smear -FullCostRendering). On the Playground's open ground a pig
// and a cow walk across a still view at 2 m/s (driven: the floor gives A* no path), then again with the camera
// following the pig; a porkchop and a crafting table fly across a still view, then lie bobbing; the held sword swings;
// the player walks; an arrow flies across a still view. A moving subject is shot the first frame it is drawn past a
// fixed point, so two builds' shots frame the same moment. Shots are named <output>-smear-<name>.png, without the HUD.
void UFoundationRuntimeProbe::AddSmearCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    // The mobs probe's open ground, 20 m east and 25 m north of spawn; the view looks east (+Y to its right).
    static const FVector Spot(2000, -2500, 0);
    // The mobs' walk, cm per reference step (2 m/s).
    static constexpr double WalkCm = 10;
    struct FSubjects
    {
        int32 Pig = 0, Cow = 0, Porkchop = 0, Table = 0, Arrow = 0;
    };
    auto Subjects = MakeShared<FSubjects>();
    auto LookAt = [this](const FVector& Target)
    { Controller->SetControlRotation((Target - Pawn->GetActorLocation() - FVector(0, 0, 162)).Rotation()); };
    // Where a moving thing is drawn this frame (world cm): between its last two reference steps, as the views draw it.
    auto Drawn = [this](const FVector& Previous, const FVector& Current)
    {
        const double Alpha =
            FMath::Clamp(Pawn->Simulation.Accumulator / GetDefault<UFoundationProfile>()->TickSeconds, 0., 1.);
        return Pawn->GetAuthority()->World.OriginCm + FMath::Lerp(Previous, Current, Alpha);
    };
    auto MobAt = [this, Drawn](int32 Id, FVector& OutCm)
    {
        const FFoundationMob* Mob = Pawn->GetAuthority() ? Pawn->GetAuthority()->FindMob(Id) : nullptr;
        if (Mob) OutCm = Drawn(Mob->PreviousLocalCm, Mob->LocalCm);
        return Mob != nullptr;
    };
    auto ItemAt = [this, Drawn](int32 Id, FVector& OutCm, int32& OutAge)
    {
        const FFoundationAuthority* A = Pawn->GetAuthority();
        const FFoundationItemEntity* Item = A
            ? A->GetItemEntities().FindByPredicate(
                  [Id](const FFoundationItemEntity& Entity) { return Entity.Id == Id; })
            : nullptr;
        if (Item)
        {
            OutCm = Drawn(Item->PreviousLocalCm, Item->LocalCm);
            OutAge = Item->Age;
        }
        return Item != nullptr;
    };
    auto GetView = [this]() -> const UFoundationInteractionView*
    {
        const AFoundationPlayground* Lab = Cast<AFoundationPlayground>(Pawn->GetLab());
        return Lab ? Lab->GetView() : nullptr;
    };
    auto Counts = [GetView]
    {
        const UFoundationInteractionView* View = GetView();
        const FFoundationMobView* Mobs = View ? View->GetMobView() : nullptr;
        const FFoundationItemEntityView* Drops = View ? View->GetItemEntityView() : nullptr;
        return FString::Printf(TEXT("mobs drawn %d, drops drawn %d"), Mobs ? Mobs->GetDrawnCount() : -1,
            Drops ? Drops->GetDrawnCount() : -1);
    };
    // Clears the mobs, drops and arrows, the player standing at the spot.
    auto Clear = [this]
    {
        ResetPlayer(Spot);
        if (FFoundationAuthority* A = Pawn->GetAuthority())
        {
            A->ClearMobs();
            A->RemoveItemEntities();
            A->RemoveArrows();
        }
    };
    // A case that runs Each every frame, takes the shot the first frame Ready holds (Frame counts from the case's
    // start), keeps running Each while the screenshot lands (the next frame), and records Detail as it was at the shot.
    // It passes only if Shown held at the shot: its subjects were drawn (Codex review 41, R41-4).
    auto ShotWhen = [this](const FString& Name, TFunction<void()> Start, TFunction<void()> Each,
                        TFunction<bool(int32 Frame)> Ready, TFunction<FString()> Detail, TFunction<bool()> Shown)
    {
        struct FShot
        {
            int32 StartFrame = 0, ShotFrame = -1;
            bool bShown = false;
            FString Note;
        };
        auto Shot = MakeShared<FShot>();
        AddCase(
            TEXT("smear: ") + Name, .1,
            [this, Name, Start, Each, Ready, Detail, Shown, Shot]
            {
                Shot->StartFrame = Frames;
                Shot->ShotFrame = -1;
                Shot->bShown = false;
                if (Start) Start();
                bHoldCase = true;
                FrameHook = [this, Name, Each, Ready, Detail, Shown, Shot]
                {
                    if (Each) Each();
                    if (Shot->ShotFrame < 0 && Ready(Frames - Shot->StartFrame))
                    {
                        FScreenshotRequest::RequestScreenshot(
                            FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-smear-") + Name + TEXT(".png"), false,
                            false);
                        Shot->ShotFrame = Frames;
                        Shot->bShown = Shown();
                        Shot->Note = Detail();
                    }
                    else if (Shot->ShotFrame >= 0 && Frames > Shot->ShotFrame + 1)
                        bHoldCase = false;
                };
            },
            [this, Name, Shot]
            {
                Record(TEXT("smear: ") + Name + TEXT(" shot, its subjects drawn"), Shot->ShotFrame >= 0 && Shot->bShown,
                    Shot->Note + (Shot->bShown ? TEXT("") : TEXT("; not drawn at the shot")));
            });
    };

    // 1-3: the pig walks across the view, left to right 4 m away, the cow behind it the other way, facing where they
    // go.
    auto Place = [this, Subjects, Clear]
    {
        if (IConsoleVariable* Mobs = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Mobs")))
            Mobs->Set(1, ECVF_SetByCode);
        Clear();
        FFoundationAuthority* A = Pawn->GetAuthority();
        if (!A) return;
        A->Options.bMobs = true;
        Subjects->Pig = A->SpawnMob(EFoundationMobType::Pig, Spot + FVector(400, -380, 0) - A->World.OriginCm, 90);
        Subjects->Cow = A->SpawnMob(EFoundationMobType::Cow, Spot + FVector(650, 420, 0) - A->World.OriginCm, -90);
    };
    auto Walk = [this, Subjects]
    {
        FFoundationAuthority* A = Pawn->GetAuthority();
        for (const TPair<int32, float>& Each :
            {TPair<int32, float>(Subjects->Pig, 90.f), TPair<int32, float>(Subjects->Cow, -90.f)})
            if (FFoundationMob* Mob = A ? A->FindMob(Each.Key) : nullptr)
            {
                Mob->bMoving = false;
                Mob->Yaw = Mob->PreviousYaw = Each.Value;
                Mob->VelocityCm.X = 0;
                Mob->VelocityCm.Y = Each.Value > 0 ? WalkCm : -WalkCm;
            }
    };
    // Ready once the pig is drawn past Y cm from the spot's line.
    auto PigPast = [MobAt, Subjects](double Y)
    {
        return [MobAt, Subjects, Y](int32)
        {
            FVector At;
            return MobAt(Subjects->Pig, At) && At.Y >= Spot.Y + Y;
        };
    };
    auto MobsDetail = [MobAt, Subjects, Counts]
    {
        FVector Pig(0), Cow(0);
        MobAt(Subjects->Pig, Pig);
        MobAt(Subjects->Cow, Cow);
        return FString::Printf(
            TEXT("pig drawn at %s, cow at %s; %s"), *Pig.ToCompactString(), *Cow.ToCompactString(), *Counts());
    };
    auto MobsShown = [GetView]
    {
        const UFoundationInteractionView* View = GetView();
        // Both drawn with every model part's mesh: a model loaded without geometry does not count (R46-1).
        return View && View->GetMobView() && View->GetMobView()->GetDrawnCount() == 2 &&
            View->GetMobView()->GetWholeCount() == 2;
    };
    ShotWhen(
        TEXT("1-mobs-still-camera"),
        [Place, LookAt]
        {
            Place();
            LookAt(Spot + FVector(450, 0, 60));
        },
        Walk, PigPast(-100), MobsDetail, MobsShown);
    ShotWhen(TEXT("2-mobs-still-camera"), nullptr, Walk, PigPast(60), MobsDetail, MobsShown);
    // The same walk with the camera turning to follow the pig: the ground and the cow sweep past it.
    ShotWhen(
        TEXT("3-mobs-camera-follows-pig"), Place,
        [Walk, MobAt, Subjects, LookAt]
        {
            Walk();
            FVector Pig;
            if (MobAt(Subjects->Pig, Pig)) LookAt(Pig + FVector(50, 0, 60));
        },
        PigPast(-100), MobsDetail, MobsShown);

    // 4-5: a porkchop (an extruded sprite) and a crafting table (a block) thrown across a still view, out of the
    // player's reach; shot as the porkchop passes 1 m left of the view's centre line, then lying (bobbing and
    // turning) 2.5 s on.
    AddCase(
        TEXT("smear: drops setup"), .6,
        [Clear, LookAt]
        {
            Clear();
            LookAt(Spot + FVector(400, 0, 60));
        },
        [] {});
    auto DropsDetail = [ItemAt, Subjects, Counts]
    {
        FVector Porkchop(0), Table(0);
        int32 Age = 0, TableAge = 0;
        ItemAt(Subjects->Porkchop, Porkchop, Age);
        ItemAt(Subjects->Table, Table, TableAge);
        return FString::Printf(TEXT("porkchop drawn at %s (age %d), crafting table at %s; %s"),
            *Porkchop.ToCompactString(), Age, *Table.ToCompactString(), *Counts());
    };
    auto DropsShown = [GetView]
    {
        const UFoundationInteractionView* View = GetView();
        return View && View->GetItemEntityView() && View->GetItemEntityView()->GetDrawnCount() == 2;
    };
    ShotWhen(
        TEXT("4-drops-fly"),
        [this, Subjects]
        {
            FFoundationAuthority* A = Pawn->GetAuthority();
            if (!A) return;
            // A player's throw speed (ThrowSpeedCm 30), sideways.
            Subjects->Porkchop = A->SpawnItem(A->Rules.FindItem(TEXT("porkchop")), 1,
                Spot + FVector(330, -260, 150) - A->World.OriginCm, FVector(0, 30, 12), 40);
            Subjects->Table = A->SpawnItem(A->Rules.FindItem(TEXT("crafting_table")), 1,
                Spot + FVector(430, -300, 110) - A->World.OriginCm, FVector(0, 30, 14), 40);
        },
        nullptr,
        [ItemAt, Subjects](int32)
        {
            FVector At;
            int32 Age = 0;
            return ItemAt(Subjects->Porkchop, At, Age) && At.Y >= Spot.Y - 100;
        },
        DropsDetail, DropsShown);
    ShotWhen(
        TEXT("5-drops-lie"), nullptr, nullptr,
        [ItemAt, Subjects](int32)
        {
            FVector At;
            int32 Age = 0;
            return ItemAt(Subjects->Porkchop, At, Age) && Age >= 50;
        },
        DropsDetail, DropsShown);

    // 6-7: the held iron sword (slot 3) mid-swing 4 frames after an attack into the air, the view still; then walking
    // forward with it for 0.75 s.
    AddCase(
        TEXT("smear: sword setup"), .6,
        [this, Clear, LookAt]
        {
            Clear();
            LookAt(Spot + FVector(1000, 0, 162));
            PressKey(EKeys::Three, true);
            PressKey(EKeys::Three, false);
        },
        [] {});
    auto HeldDetail = [this]
    {
        const FFoundationAuthority* A = Pawn->GetAuthority();
        const FFoundationPlayer* Player = A ? A->FindPlayer(Pawn->Interaction.PlayerId) : nullptr;
        return FString::Printf(TEXT("holding %s at %s, view %s"),
            Player ? *A->GetHandItem(*Player, EFoundationHand::Main).Id.ToString() : TEXT("?"),
            *Pawn->GetActorLocation().ToCompactString(), *Controller->GetControlRotation().ToCompactString());
    };
    // The iron sword held and its mesh drawn.
    auto SwordShown = [this, GetView]
    {
        const UFoundationInteractionView* View = GetView();
        const UStaticMeshComponent* Held = View ? View->GetHeldItem() : nullptr;
        const FFoundationAuthority* A = Pawn->GetAuthority();
        const FFoundationPlayer* Player = A ? A->FindPlayer(Pawn->Interaction.PlayerId) : nullptr;
        return Held && Held->IsVisible() && Held->GetStaticMesh() && Player &&
            A->GetHandItem(*Player, EFoundationHand::Main).Id == TEXT("iron_sword");
    };
    ShotWhen(
        TEXT("6-sword-swing"),
        [this]
        {
            PressKey(EKeys::LeftMouseButton, true);
            PressKey(EKeys::LeftMouseButton, false);
        },
        nullptr, [](int32 Frame) { return Frame >= 4; }, HeldDetail, SwordShown);
    ShotWhen(
        TEXT("7-walking"), [this] { PressKey(EKeys::W, true); }, nullptr, [](int32 Frame) { return Frame >= 45; },
        HeldDetail, SwordShown);

    // 8-9: an arrow shot across a still view 4.5 m away, left to right, at a weak draw's speed (0.6 blocks per tick)
    // and without spread; shot as it passes 1.2 m left of the view's centre line, then 0.8 m right of it.
    AddCase(
        TEXT("smear: arrow setup"), .6,
        [Clear, LookAt]
        {
            Clear();
            LookAt(Spot + FVector(450, 0, 120));
        },
        [] {});
    auto ArrowAt = [this, Drawn, Subjects](FVector& OutCm)
    {
        const FFoundationAuthority* A = Pawn->GetAuthority();
        const FFoundationArrow* Arrow = A ? A->GetArrows().FindByPredicate([Subjects](const FFoundationArrow& Each)
                                                { return Each.Id == Subjects->Arrow; })
                                          : nullptr;
        if (Arrow) OutCm = Drawn(Arrow->PreviousLocalCm, Arrow->LocalCm);
        return Arrow != nullptr;
    };
    auto ArrowPast = [ArrowAt](double Y)
    {
        return [ArrowAt, Y](int32)
        {
            FVector At;
            return ArrowAt(At) && At.Y >= Spot.Y + Y;
        };
    };
    auto ArrowDetail = [GetView, ArrowAt]
    {
        FVector At(0);
        const bool bFound = ArrowAt(At);
        const UFoundationInteractionView* View = GetView();
        const FFoundationArrowView* Arrows = View ? View->GetArrowView() : nullptr;
        return FString::Printf(TEXT("arrow %s at %s; arrows drawn %d"), bFound ? TEXT("drawn") : TEXT("gone"),
            *At.ToCompactString(), Arrows ? Arrows->GetDrawnCount() : -1);
    };
    auto ArrowShown = [GetView]
    {
        const UFoundationInteractionView* View = GetView();
        return View && View->GetArrowView() && View->GetArrowView()->GetDrawnCount() == 1;
    };
    ShotWhen(
        TEXT("8-arrow-flies"),
        [this, Subjects]
        {
            FFoundationAuthority* A = Pawn->GetAuthority();
            if (!A) return;
            FFoundationArrowShot Shot;
            Shot.FromCm = Spot + FVector(450, -520, 170);
            Shot.Direction = FVector(0, 1, .3).GetSafeNormal();
            Shot.SpeedBlocks = .6f;
            Shot.Inaccuracy = 0;
            Subjects->Arrow = A->ShootArrow(Shot);
        },
        nullptr, ArrowPast(-120), ArrowDetail, ArrowShown);
    ShotWhen(TEXT("9-arrow-flies-on"), nullptr, nullptr, ArrowPast(80), ArrowDetail, ArrowShown);
    AddCase(
        TEXT("smear: restore"), .2,
        [this, Clear]
        {
            // ResetPlayer releases W.
            Clear();
            PressKey(EKeys::One, true);
            PressKey(EKeys::One, false);
        },
        [] {});
#endif
}
