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
    // 20e1: the farm extras: a pink, a black and a shorn red sheep, then a temperate, a cold and a warm cow (compared
    // to the jar textures); the control is the same six with foundation.Mobs.Farm 0 (white, woolly sheep).
    auto SetFarm = [](int32 Value)
    {
        if (IConsoleVariable* Farm = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Mobs.Farm")))
            Farm->Set(Value, ECVF_SetByCode);
    };
    AddCase(
        TEXT("mobs: farm extras"), 1.,
        [this, SetMobs, SetFarm, Authority, Farm, LookAt]
        {
            SetMobs(1);
            SetFarm(1);
            ResetPlayer(Spot);
            FFoundationAuthority* A = Authority();
            if (!A) return;
            A->ClearMobs();
            Farm->Reset();
            struct FFarmSubject
            {
                EFoundationMobType Type;
                uint8 Color;
                bool bSheared;
                EFoundationMobVariant Variant;
            };
            const FFarmSubject Subjects[] = {{EFoundationMobType::Sheep, 6, false, EFoundationMobVariant::Temperate},
                {EFoundationMobType::Sheep, 15, false, EFoundationMobVariant::Temperate},
                {EFoundationMobType::Sheep, 14, true, EFoundationMobVariant::Temperate},
                {EFoundationMobType::Cow, 0, false, EFoundationMobVariant::Temperate},
                {EFoundationMobType::Cow, 0, false, EFoundationMobVariant::Cold},
                {EFoundationMobType::Cow, 0, false, EFoundationMobVariant::Warm}};
            for (int32 Index = 0; Index < 6; ++Index)
            {
                const FVector At = Spot + FVector(Index < 3 ? 450 : 700, 170 * (Index % 3 - 1), 0);
                const int32 Id = A->SpawnMob(Subjects[Index].Type, At - A->World.OriginCm, 150);
                if (FFoundationMob* Mob = A->FindMob(Id))
                {
                    Mob->Color = Subjects[Index].Color;
                    Mob->bSheared = Subjects[Index].bSheared;
                    Mob->Variant = Subjects[Index].Variant;
                }
                Farm->Add(Id);
            }
            LookAt(Spot + FVector(575, 0, 60));
        },
        [this, Shot, Drawn, FindFarm]
        {
            for (int32 Index = 0; Index < 6; ++Index)
                if (FFoundationMob* Mob = FindFarm(Index)) Mob->bMoving = false;
            Record(TEXT("mobs: the six farm subjects drawn"), Drawn() == 6, FString::FromInt(Drawn()));
            Shot(TEXT("14a-farm-extras"));
        });
    Flush();
    AddCase(
        TEXT("mobs: farm extras off"), .4, [SetFarm] { SetFarm(0); },
        [this, Shot, Drawn]
        {
            Record(TEXT("mobs: farm 0 still draws six"), Drawn() == 6, FString::FromInt(Drawn()));
            Shot(TEXT("14b-farm-off"));
        });
    Flush();
    AddCase(
        TEXT("mobs: farm restore"), .4, [SetFarm] { SetFarm(1); },
        [this, Shot, Drawn, FindFarm]
        {
            const FFoundationMob* Cold = FindFarm(4);
            const FFoundationMob* Warm = FindFarm(5);
            Record(TEXT("mobs: farm 1 restores the same variant animals"),
                Drawn() == 6 && Cold && Warm && Cold->Variant == EFoundationMobVariant::Cold &&
                    Warm->Variant == EFoundationMobVariant::Warm,
                FString::Printf(
                    TEXT("drawn %d; cold mob %d; warm mob %d"), Drawn(), Cold ? Cold->Id : 0, Warm ? Warm->Id : 0));
            Shot(TEXT("14c-farm-restored"));
        });
    Flush();
    // 20c1: a zombie 1.3 blocks east of a survival player (in reach: the Playground's collision gives A* no path on its
    // floor, a known issue) hits it (3 a hit on Normal, every 20 steps) through the pawn's vitals, and knocks it back
    // west.
    auto Zombie = MakeShared<int32>(0);
    auto Before = MakeShared<TPair<EFoundationPlayerMode, EFoundationDifficulty>>();
    AddCase(
        TEXT("mobs: a zombie attacks"), 2.5,
        [this, SetMobs, Authority, Zombie, Before, LookAt]
        {
            SetMobs(1);
            *Before = {Pawn->Vitals.Mode, Pawn->Vitals.Difficulty};
            Pawn->Vitals.Mode = EFoundationPlayerMode::Survival;
            Pawn->Vitals.Difficulty = EFoundationDifficulty::Normal;
            ResetPlayer(Spot);
            FFoundationAuthority* A = Authority();
            if (!A) return;
            A->ClearMobs();
            A->Options.bMobs = true;
            *Zombie = A->SpawnMob(EFoundationMobType::Zombie, Spot + FVector(130, 0, 0) - A->World.OriginCm, 180);
            LookAt(Spot + FVector(130, 0, 150));
        },
        [this, Shot, Authority, Zombie]
        {
            const FFoundationAuthority* A = Authority();
            const FFoundationMob* Mob = A ? const_cast<FFoundationAuthority*>(A)->FindMob(*Zombie) : nullptr;
            Record(TEXT("mobs: the zombie targets the player"), Mob && Mob->Target == Pawn->Interaction.PlayerId,
                Mob ? FString::Printf(TEXT("target %d at %s (player %s), moving %d, path %d/%d, cooldown %d"),
                          Mob->Target, *(A->World.OriginCm + Mob->LocalCm).ToCompactString(),
                          *Pawn->GetActorLocation().ToCompactString(), Mob->bMoving, Mob->PathIndex, Mob->Path.Num(),
                          Mob->AttackCooldown)
                    : TEXT("no zombie"));
            Record(TEXT("mobs: its hits hurt the player 3 each"),
                A && A->MobMeleeHits >= 1 && Pawn->Vitals.Health < 20.f &&
                    Pawn->Vitals.LastDamage.Contains(TEXT(" 3 -> 3 to health")),
                FString::Printf(TEXT("hits %d, health %.1f, %s"), A ? A->MobMeleeHits : -1, Pawn->Vitals.Health,
                    *Pawn->Vitals.LastDamage));
            Record(TEXT("mobs: knocked back west"), Pawn->GetActorLocation().X < Spot.X - 30,
                FString::Printf(TEXT("x %.0f from %.0f"), Pawn->GetActorLocation().X, Spot.X));
            Shot(TEXT("12-zombie-attacks"));
        });
    Flush();
    AddCase(
        TEXT("mobs: zombie restore"), .2, [] {},
        [this, Authority, Before]
        {
            if (FFoundationAuthority* A = Authority()) A->ClearMobs();
            Pawn->Vitals.Mode = Before->Key;
            Pawn->Vitals.Difficulty = Before->Value;
            ResetPlayer(Spot);
        });
    // 20c2: the spider close up at rest, then walking (its eight legs; the look call against the model data), and
    // Peaceful removing a monster. The Playground has no sky hook: its full daylight keeps the spider neutral.
    auto Spider = MakeShared<int32>(0), Despawned = MakeShared<int32>(0);
    const auto FindSpider = [Authority, Spider] { return Authority() ? Authority()->FindMob(*Spider) : nullptr; };
    AddCase(
        TEXT("mobs: a spider stands"), 1.,
        [this, SetMobs, Authority, Spider, LookAt]
        {
            SetMobs(1);
            ResetPlayer(Spot);
            FFoundationAuthority* A = Authority();
            if (!A) return;
            A->ClearMobs();
            A->Options.bMobs = true;
            *Spider = A->SpawnMob(EFoundationMobType::Spider, Spot + FVector(260, 0, 0) - A->World.OriginCm, 180);
            LookAt(Spot + FVector(260, 0, 40));
        },
        [this, Shot, Drawn, FindSpider]
        {
            const FFoundationMob* Mob = FindSpider();
            Record(TEXT("mobs: the spider stands, neutral in daylight"),
                Mob && Mob->bOnGround && Mob->IsAlive() && Mob->Target == 0,
                Mob ? FString::Printf(TEXT("at %s, target %d"), *Mob->LocalCm.ToCompactString(), Mob->Target)
                    : TEXT("no spider"));
            Record(TEXT("mobs: one spider drawn"), Drawn() == 1, FString::FromInt(Drawn()));
            Shot(TEXT("13a-spider-rest"));
        });
    Flush();
    AddCase(
        TEXT("mobs: spider side view"), .4,
        [FindSpider]
        {
            if (FFoundationMob* Mob = FindSpider())
                Mob->Yaw = Mob->PreviousYaw = Mob->HeadYaw = Mob->PreviousHeadYaw = 90;
        },
        [Shot] { Shot(TEXT("13c-spider-side")); });
    Flush();
    AddCase(
        TEXT("mobs: the spider walks"), .8,
        [this, FindSpider, LookAt]
        {
            if (FFoundationMob* Mob = FindSpider())
            {
                Mob->bMoving = true;
                Mob->SpeedModifier = 1;
                Mob->MoveTicks = Mob->StuckTicks = 0;
                Mob->TargetCm = Mob->LocalCm + FVector(0, 900, 0);
            }
            LookAt(Spot + FVector(260, 250, 40));
        },
        [this, Shot, FindSpider]
        {
            const FFoundationMob* Mob = FindSpider();
            Record(TEXT("mobs: the spider walking (legs swing)"), Mob && Mob->WalkSpeed > .1f,
                Mob ? FString::Printf(TEXT("walk speed %.2f"), Mob->WalkSpeed) : TEXT("no spider"));
            Shot(TEXT("13b-spider-walks"));
        });
    Flush();
    AddCase(
        TEXT("mobs: Peaceful removes monsters"), .4,
        [this, Authority, Zombie, Despawned, Before]
        {
            *Before = {Pawn->Vitals.Mode, Pawn->Vitals.Difficulty};
            Pawn->Vitals.Difficulty = EFoundationDifficulty::Peaceful;
            if (FFoundationAuthority* A = Authority())
            {
                *Despawned = A->MobsDespawned;
                *Zombie = A->SpawnMob(EFoundationMobType::Zombie, Spot + FVector(300, 0, 0) - A->World.OriginCm, 180);
            }
        },
        [this, Authority, FindSpider, Zombie, Despawned, Before]
        {
            FFoundationAuthority* A = Authority();
            Record(TEXT("mobs: Peaceful removed the spider and the zombie"),
                A && !FindSpider() && !A->FindMob(*Zombie) && A->MobsDespawned - *Despawned == 2,
                FString::Printf(TEXT("despawned %d"), A ? A->MobsDespawned - *Despawned : -1));
            Pawn->Vitals.Difficulty = Before->Value;
            if (FFoundationAuthority* Mutable = Authority()) Mutable->ClearMobs();
        });
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
