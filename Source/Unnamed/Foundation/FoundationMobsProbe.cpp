#include "Foundation/FoundationRuntimeProbe.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationMobView.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationPlayground.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// The mobs' captures (survival T6 20a) on the Playground's open ground: a summoned cow standing, walking across the
// view, hit (the red overlay and the knockback), dying (the roll) and its drops; 20b: the overlay close up (unhurt,
// hurt), the pig, sheep and chicken, the chicken falling and the sheep grazing; then the foundation.Mobs control pair
// (1 drawn, 0 hidden). Shots are named <output>-mobs-<name>.png.
void UFoundationRuntimeProbe::AddMobCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    // Open ground 20 m east and 25 m north of spawn.
    static const FVector Spot(2000, -2500, 0);
    auto Shot = [this](const FString& Name)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-mobs-") + Name + TEXT(".png"), true, false);
    };
    auto SetMobs = [](int32 Value)
    {
        if (IConsoleVariable* Mobs = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Mobs")))
            Mobs->Set(Value, ECVF_SetByCode);
    };
    auto Authority = [this] { return Pawn->GetLab() ? Pawn->GetLab()->GetAuthority() : nullptr; };
    auto MobId = MakeShared<int32>(0);
    auto FindCow = [Authority, MobId]() -> FFoundationMob*
    { return Authority() ? Authority()->FindMob(*MobId) : nullptr; };
    auto Drawn = [this]
    {
        const AFoundationPlayground* Lab = Cast<AFoundationPlayground>(Pawn->GetLab());
        const UFoundationInteractionView* View = Lab ? Lab->GetView() : nullptr;
        const FFoundationMobView* Mobs = View ? View->GetMobView() : nullptr;
        return Mobs ? Mobs->GetDrawnCount() : -1;
    };
    // Looks from the spot at a point (world cm).
    auto LookAt = [this](const FVector& Target)
    { Controller->SetControlRotation((Target - Pawn->GetActorLocation() - FVector(0, 0, 162)).Rotation()); };
    // A fully charged attack on what the player aims at, through the authority's command path.
    auto Sequence = MakeShared<uint32>(900000);
    auto Attack = [this, Authority, Sequence]
    {
        FFoundationAuthority* A = Authority();
        const FFoundationPlayer* Player = A ? A->FindPlayer(Pawn->Interaction.PlayerId) : nullptr;
        if (!Player) return FFoundationResult();
        Pawn->Vitals.AttackTicker = 100;
        FFoundationCommand Command;
        Command.SessionId = A->GetSessionId();
        Command.Type = EFoundationCommandType::Attack;
        Command.Hand = EFoundationHand::Main;
        Command.Item = Player->Inventory.GetHand(EFoundationHand::Main).GetRef();
        Command.Target = A->Resolve(Player->Facts);
        Command.Sequence = ++*Sequence;
        return A->Execute(Pawn->Interaction.PlayerId, Command, Pawn->Vitals);
    };
    // A screenshot lands on the next frame: an idle case after each shot keeps the next case's changes out of it.
    auto Flush = [this] { AddCase(TEXT("mobs: flush"), .2, [] {}, [] {}); };
    auto Summon = [this, Authority, MobId](const FVector& WorldCm, float Yaw)
    {
        FFoundationAuthority* A = Authority();
        if (!A) return;
        A->ClearMobs();
        A->Options.bMobs = true;
        *MobId = A->SpawnMob(EFoundationMobType::Cow, WorldCm - A->World.OriginCm, Yaw);
    };

    AddCase(
        TEXT("mobs: summon a cow"), 1.5,
        [this, SetMobs, Summon, LookAt]
        {
            SetMobs(1);
            ResetPlayer(Spot);
            Summon(Spot + FVector(400, 0, 0), 150);
            LookAt(Spot + FVector(400, 0, 70));
        },
        [this, Shot, FindCow, Drawn]
        {
            const FFoundationMob* Cow = FindCow();
            Record(TEXT("mobs: the cow stands"), Cow && Cow->bOnGround && Cow->IsAlive(),
                Cow ? Cow->LocalCm.ToCompactString() : TEXT("no cow"));
            Record(TEXT("mobs: one cow drawn"), Drawn() == 1, FString::FromInt(Drawn()));
            Shot(TEXT("1-standing"));
        });
    Flush();
    for (const TPair<float, const TCHAR*>& View : {TPair<float, const TCHAR*>(180, TEXT("1b-front")),
             TPair<float, const TCHAR*>(0, TEXT("1c-back")), TPair<float, const TCHAR*>(90, TEXT("1d-side"))})
    {
        AddCase(
            TEXT("mobs: view ") + FString(View.Value), .4,
            [FindCow, View]
            {
                if (FFoundationMob* Cow = FindCow())
                    Cow->Yaw = Cow->PreviousYaw = Cow->HeadYaw = Cow->PreviousHeadYaw = View.Key;
            },
            [Shot, View] { Shot(View.Value); });
        Flush();
    }
    AddCase(
        TEXT("mobs: the cow walks across"), 1.4,
        [FindCow]
        {
            if (FFoundationMob* Cow = FindCow())
            {
                Cow->bMoving = true;
                Cow->SpeedModifier = 1;
                Cow->MoveTicks = Cow->StuckTicks = 0;
                Cow->TargetCm = Cow->LocalCm + FVector(0, 900, 0);
            }
        },
        [this, Shot, FindCow]
        {
            const FFoundationMob* Cow = FindCow();
            Record(TEXT("mobs: walking (legs swing)"), Cow && Cow->WalkSpeed > .1f,
                Cow ? FString::Printf(TEXT("walk speed %.2f"), Cow->WalkSpeed) : TEXT("no cow"));
            Shot(TEXT("2-walking"));
        });
    Flush();
    AddCase(
        TEXT("mobs: hit"), .1,
        [FindCow, LookAt, Authority]
        {
            if (FFoundationMob* Cow = FindCow())
            {
                Cow->bMoving = false;
                Cow->VelocityCm = FVector::ZeroVector;
                Cow->LocalCm = Cow->PreviousLocalCm = Spot + FVector(300, 0, 0) - Authority()->World.OriginCm;
                Cow->Yaw = Cow->PreviousYaw = 90;
                LookAt(Spot + FVector(300, 0, 80));
            }
        },
        [this, Attack, FindCow]
        {
            const FFoundationResult Result = Attack();
            const FFoundationMob* Cow = FindCow();
            Record(TEXT("mobs: the hit landed with knockback"), Result.bKnockback && Cow && Cow->HurtTime == 10,
                FString::Printf(TEXT("damage %.1f, reason %d"), Result.DamageApplied, int32(Result.Reason)));
        });
    AddCase(
        TEXT("mobs: red overlay"), .12, [] {},
        [this, Shot, FindCow]
        {
            const FFoundationMob* Cow = FindCow();
            Record(TEXT("mobs: hurt, knocked away"), Cow && Cow->HurtTime > 0 && Cow->VelocityCm.X > 0,
                Cow ? Cow->VelocityCm.ToCompactString() : TEXT("no cow"));
            Shot(TEXT("3-hit"));
        });
    Flush();
    AddCase(
        TEXT("mobs: kill"), 1.2,
        [FindCow, LookAt, Authority]
        {
            if (FFoundationMob* Cow = FindCow())
            {
                Cow->LocalCm = Cow->PreviousLocalCm = Spot + FVector(300, 0, 0) - Authority()->World.OriginCm;
                Cow->VelocityCm = FVector::ZeroVector;
                Cow->bMoving = Cow->bPanicking = false;
                Cow->Health.Health = 1;
            }
            LookAt(Spot + FVector(300, 0, 80));
        },
        [this, Attack, FindCow]
        {
            const FFoundationResult Result = Attack();
            Record(TEXT("mobs: the killing hit"), Result.bTargetDied,
                FString::Printf(TEXT("reason %d"), int32(Result.Reason)));
        });
    AddCase(
        TEXT("mobs: dying"), .5, [] {},
        [this, Shot, FindCow]
        {
            const FFoundationMob* Cow = FindCow();
            Record(TEXT("mobs: rolling over, red"), Cow && Cow->DeathTime > 5,
                Cow ? FString::Printf(TEXT("death time %d"), Cow->DeathTime) : TEXT("removed early"));
            Shot(TEXT("4-dying"));
        });
    Flush();
    AddCase(
        TEXT("mobs: drops"), 1.2, [this, LookAt] { LookAt(Spot + FVector(300, 0, 0)); },
        [this, Shot, FindCow, Authority]
        {
            int32 Beef = 0, Leather = 0;
            if (const FFoundationAuthority* A = Authority())
                for (const FFoundationItemEntity& Entity : A->GetItemEntities())
                {
                    const FName Id = A->Rules.GetItem(Entity.Item).Id;
                    Beef += Id == TEXT("beef") ? Entity.Count : 0;
                    Leather += Id == TEXT("leather") ? Entity.Count : 0;
                }
            Record(TEXT("mobs: the corpse is gone and beef dropped"), !FindCow() && Beef >= 1,
                FString::Printf(TEXT("beef %d, leather %d"), Beef, Leather));
            Shot(TEXT("5-drops"));
        });
    Flush();
    // 20b: the hurt overlay close up (the look call against vanilla's Cow_Death.png), unhurt then hurt.
    AddCase(
        TEXT("mobs: close-up unhurt"), .6,
        [this, SetMobs, Summon, LookAt]
        {
            SetMobs(1);
            ResetPlayer(Spot);
            Summon(Spot + FVector(180, 0, 0), 120);
            LookAt(Spot + FVector(180, 0, 80));
        },
        [Shot] { Shot(TEXT("8a-closeup-unhurt")); });
    Flush();
    AddCase(
        TEXT("mobs: close-up hurt"), .12,
        [this, Attack, FindCow]
        {
            Attack();
            if (FFoundationMob* Cow = FindCow())
            {
                Cow->VelocityCm = FVector::ZeroVector;
                Cow->bMoving = false;
            }
        },
        [this, Shot, FindCow]
        {
            const FFoundationMob* Cow = FindCow();
            Record(TEXT("mobs: close-up hurt"), Cow && Cow->HurtTime > 0,
                Cow ? FString::Printf(TEXT("hurt %d"), Cow->HurtTime) : TEXT("no cow"));
            Shot(TEXT("8b-closeup-hurt"));
        });
    Flush();
    // 20b: the pig, the sheep (its wool layer) and the chicken beside each other, then the chicken falling (its wings)
    // and the sheep grazing (its head down).
    auto Farm = MakeShared<TArray<int32>>();
    auto FindFarm = [Authority, Farm](int32 Index) -> FFoundationMob*
    { return Authority() && Farm->IsValidIndex(Index) ? Authority()->FindMob((*Farm)[Index]) : nullptr; };
    AddCase(
        TEXT("mobs: pig, sheep, chicken"), 1.,
        [this, SetMobs, Authority, Farm, LookAt]
        {
            SetMobs(1);
            ResetPlayer(Spot);
            FFoundationAuthority* A = Authority();
            if (!A) return;
            A->ClearMobs();
            A->Options.bMobs = true;
            Farm->Reset();
            const EFoundationMobType Types[] = {
                EFoundationMobType::Pig, EFoundationMobType::Sheep, EFoundationMobType::Chicken};
            for (int32 Index = 0; Index < 3; ++Index)
                Farm->Add(
                    A->SpawnMob(Types[Index], Spot + FVector(400, 180 * (Index - 1), 0) - A->World.OriginCm, 150));
            LookAt(Spot + FVector(400, 0, 50));
        },
        [this, Shot, Drawn, FindFarm]
        {
            for (int32 Index = 0; Index < 3; ++Index)
                if (FFoundationMob* Mob = FindFarm(Index)) Mob->bMoving = false;
            Record(TEXT("mobs: pig, sheep and chicken drawn"), Drawn() == 3, FString::FromInt(Drawn()));
            Shot(TEXT("9-farm"));
        });
    Flush();
    AddCase(
        TEXT("mobs: chicken falls"), .35,
        [FindFarm]
        {
            if (FFoundationMob* Chicken = FindFarm(2))
            {
                Chicken->LocalCm.Z += 300;
                Chicken->PreviousLocalCm = Chicken->LocalCm;
                Chicken->bOnGround = false;
            }
        },
        [this, Shot, FindFarm]
        {
            const FFoundationMob* Chicken = FindFarm(2);
            Record(TEXT("mobs: the chicken flaps and falls slowly"),
                Chicken && !Chicken->bOnGround && Chicken->FlapSpeed > .5f && Chicken->VelocityCm.Z > -19.5,
                Chicken ? FString::Printf(TEXT("flap speed %.2f, vy %.1f"), Chicken->FlapSpeed, Chicken->VelocityCm.Z)
                        : TEXT("no chicken"));
            Shot(TEXT("10-chicken-falls"));
        });
    Flush();
    AddCase(
        TEXT("mobs: sheep grazes"), 1.,
        [FindFarm]
        {
            if (FFoundationMob* Sheep = FindFarm(1)) Sheep->GrazeTicks = 40;
        },
        [this, Shot, FindFarm]
        {
            const FFoundationMob* Sheep = FindFarm(1);
            Record(TEXT("mobs: the sheep's head is down"), Sheep && Sheep->GrazeTicks > 4 && Sheep->GrazeTicks < 36,
                Sheep ? FString::Printf(TEXT("graze %d"), Sheep->GrazeTicks) : TEXT("no sheep"));
            Shot(TEXT("11-sheep-grazes"));
        });
    Flush();
    // The control pair: the same cow with foundation.Mobs 1, then 0.
    AddCase(
        TEXT("mobs: control on"), .8,
        [this, SetMobs, Summon, LookAt]
        {
            SetMobs(1);
            ResetPlayer(Spot);
            Summon(Spot + FVector(400, 0, 0), 150);
            LookAt(Spot + FVector(400, 0, 70));
        },
        [this, Shot, Drawn]
        {
            Record(TEXT("mobs 1: drawn"), Drawn() == 1, FString::FromInt(Drawn()));
            Shot(TEXT("6-control-on"));
        });
    Flush();
    AddCase(
        TEXT("mobs: control off"), .8, [SetMobs] { SetMobs(0); },
        [this, Shot, Drawn]
        {
            Record(TEXT("mobs 0: hidden"), Drawn() == 0, FString::FromInt(Drawn()));
            Shot(TEXT("7-control-off"));
        });
    Flush();
    AddCase(
        TEXT("mobs: restore"), .2, [] {},
        [SetMobs, Authority]
        {
            SetMobs(1);
            if (FFoundationAuthority* A = Authority()) A->ClearMobs();
        });
#endif
}
