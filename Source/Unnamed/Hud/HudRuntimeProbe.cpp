#include "Foundation/FoundationRuntimeProbe.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationInputRouter.h"
#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationPawn.h"
#include "Hud/HudCreativeScreen.h"
#include "Hud/HudModel.h"
#include "Hud/HudPresenter.h"
#include "Menu/MenuBrowserHost.h"
#include "Menu/MenuPreferences.h"
#include "Menu/MenuVideoSettings.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// Extra cases in the existing Foundation harness, opt-in with -HudCaptures and run after the selected
// -FoundationSuite scenarios. Course positions are local to the lab's course origin (ResetCoursePlayer); the
// attack cases stand at the Playground's training target in world space.
// All preference writes are to that harness's isolated, session-only profile.
void UFoundationRuntimeProbe::AddHudCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    auto* Presenter = Controller->GetHud()->GetPresenter();
    if (!Presenter)
    {
        Record(TEXT("HUD presenter exists"), false, TEXT("missing presenter"));
        return;
    }
    auto Capture = [this, Presenter](
                       const FString& Name, TFunction<void()> Setup, TFunction<bool()> Check, double Duration = .6)
    {
        AddCase(TEXT("hud-") + Name, Duration, MoveTemp(Setup),
            [this, Presenter, Name, Check]
            {
                int32 W = 0, H = 0;
                Controller->GetViewportSize(W, H);
                const auto& S = Presenter->GetSnapshot();
                Record(TEXT("HUD ") + Name,
                    Check() && Presenter->GetMissingTextures() == 0 && Presenter->GetLoadedTextureCount() == 85 &&
                        Presenter->GetItemSpriteCount() == 63,
                    FString::Printf(
                        TEXT("viewport=%dx%d scale=%d slot=%d left=%d visible=%d draws=%d textures=%d item sprites=%d; "
                             "%s resources; application input"),
                        W, H, Presenter->GetGuiScale(), S.SelectedSlot + 1, S.bMainHandLeft, S.bVisible,
                        Presenter->GetDrawCount(), Presenter->GetLoadedTextureCount(), Presenter->GetItemSpriteCount(),
                        S.Resources == EHudDataSource::Preview ? TEXT("preview") : TEXT("player vitals")));
                FScreenshotRequest::RequestScreenshot(
                    FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-hud-") + Name + TEXT(".png"), true, false);
            });
        AddCase(TEXT("hud-flush-") + Name, .3, [] {}, [] {});
    };
    for (bool Left : {false, true})
        for (int32 Slot : {0, 8})
        {
            const FString Name = FString::Printf(TEXT("slot%d-%s"), Slot + 1, Left ? TEXT("left") : TEXT("right"));
            Capture(
                Name,
                [this, Presenter, Left, Slot]
                {
                    ResetCoursePlayer({-100, -850, 0});
                    Controller->SetControlRotation(FRotator(-10, 35, 0));
                    Controller->GetHud()->bOverlay = Controller->GetHud()->bHideHud =
                        Controller->GetHud()->bShowCollision = false;
                    // Milestone 1 coverage: the fixture's states, which have no gameplay owner yet.
                    Presenter->SetPreviewEnabled(true);
                    Presenter->ResetPreview();
                    Controller->GetGameInstance()->GetSubsystem<UMenuPreferences>()->ApplyChanges(Left
                            ? TEXT("{\"settings\":{\"mainHand\":\"Left\",\"attackIndicator\":\"Hotbar\"}}")
                            : TEXT("{\"settings\":{\"mainHand\":\"Right\",\"attackIndicator\":\"Crosshair\"}}"));
                    Controller->GetMenuHost()->GetVideoSettings()->ApplySetting(
                        TEXT("guiScale"), TEXT("{\"value\":0}"));
                    PressKey(Slot == 0 ? EKeys::One : EKeys::Nine, true);
                    PressKey(Slot == 0 ? EKeys::One : EKeys::Nine, false);
                },
                [Presenter, Left, Slot]
                {
                    return Presenter->GetSnapshot().SelectedSlot == Slot &&
                        Presenter->GetSnapshot().bMainHandLeft == Left && Presenter->GetDrawCount() > 0;
                });
        }
    Capture(
        TEXT("f1-hidden"),
        [this]
        {
            PressKey(EKeys::F1, true);
            PressKey(EKeys::F1, false);
        },
        [this, Presenter] { return Controller->GetHud()->bHideHud && Presenter->GetDrawCount() == 0; });
    Capture(
        TEXT("f1-f3-independent"),
        [this]
        {
            PressKey(EKeys::F3, true);
            PressKey(EKeys::F3, false);
        },
        [this, Presenter]
        { return Controller->GetHud()->bHideHud && Controller->GetHud()->bOverlay && Presenter->GetDrawCount() == 0; });
    Capture(
        TEXT("f3"),
        [this]
        {
            PressKey(EKeys::F1, true);
            PressKey(EKeys::F1, false);
        },
        [this, Presenter] { return Controller->GetHud()->bOverlay && Presenter->GetDrawCount() > 0; });
    Capture(
        TEXT("pause"),
        [this]
        {
            Controller->GetHud()->bOverlay = false;
            PressKey(EKeys::Escape, true);
            PressKey(EKeys::Escape, false);
        },
        [this, Presenter]
        {
            return Controller->GetMenuHost()->IsOpen() && Controller->bShowMouseCursor &&
                Presenter->GetDrawCount() == 0;
        },
        2);
    Capture(
        TEXT("preview-options"),
        [this]
        {
            Controller->ResumeFoundation();
            PressKey(EKeys::F3, true);
            PressKey(EKeys::F6, true);
            PressKey(EKeys::F6, false);
            PressKey(EKeys::F3, false);
        },
        [this, Presenter]
        {
            return Controller->GetPanels()->IsOpen() && Controller->GetHud()->bOverlay &&
                Presenter->GetDrawCount() == 0;
        });
    Capture(
        TEXT("hardcore-absorption"),
        [this, Presenter]
        {
            Controller->GetPanels()->Close();
            Controller->GetHud()->bOverlay = false;
            Presenter->ResetPreview();
            auto& V = Presenter->Preview.Values;
            V.Health = 19;
            V.MaxHealth = 40;
            V.Absorption = 5;
            V.bHardcore = true;
            V.Armor = 19;
        },
        [Presenter] { return Presenter->GetSnapshot().bHardcore && Presenter->GetSnapshot().Absorption == 5; });
    for (int32 Effect = 0; Effect < 4; ++Effect)
    {
        static const TCHAR* Names[] = {
            TEXT("poison-hunger"), TEXT("wither-absorption"), TEXT("frozen"), TEXT("low-health")};
        Capture(
            Names[Effect],
            [Presenter, Effect]
            {
                Presenter->ResetPreview();
                auto& V = Presenter->Preview.Values;
                V.Health = Effect == 3 ? 3 : 13;
                V.bPoison = Effect == 0;
                V.bWither = Effect <= 1;
                V.bFrozen = Effect <= 2;
                V.bHunger = Effect == 0;
                V.Saturation = 0;
                V.Food = 3;
                V.Absorption = Effect == 1 ? 3 : 0;
            },
            [Presenter] { return Presenter->GetDrawCount() > 0; });
    }
    Capture(
        TEXT("damage-heal-blink"),
        [Presenter]
        {
            Presenter->ResetPreview();
            Presenter->Preview.Start(EHudPreviewSequence::DamageHeal);
        },
        [Presenter] { return Presenter->GetPresentation().DisplayedHealth > Presenter->GetSnapshot().Health; }, .85);
    for (int32 Air : {272, 270, 0})
        Capture(
            FString::Printf(TEXT("air-%d"), Air),
            [Presenter, Air]
            {
                Presenter->ResetPreview();
                Presenter->Preview.Values.Air = Air;
                Presenter->Preview.Values.bUnderwater = true;
            },
            [Presenter, Air] { return Presenter->GetSnapshot().Air == Air; });
    Capture(
        TEXT("air-surface-refill"),
        [Presenter]
        {
            Presenter->Preview.Values.bUnderwater = false;
            Presenter->Preview.Values.Air = 152;
        },
        [Presenter] { return !Presenter->GetSnapshot().bUnderwater && Presenter->GetSnapshot().Air < 300; });
    Capture(
        TEXT("xp-gain"),
        [Presenter]
        {
            Presenter->ResetPreview();
            Presenter->Preview.Start(EHudPreviewSequence::ExperienceGain);
        },
        [Presenter] { return Presenter->GetSnapshot().Level >= 8; }, 1.8);
    Capture(
        TEXT("creative"),
        [Presenter]
        {
            Presenter->ResetPreview();
            Presenter->Preview.Values.Mode = EHudMode::Creative;
        },
        [Presenter] { return !Presenter->GetSnapshot().CanHurt() && Presenter->GetDrawCount() > 0; });
    Capture(
        TEXT("spectator"),
        [Presenter]
        {
            Presenter->Preview.Values.Mode = EHudMode::Spectator;
            Presenter->Preview.Values.bSpectatorTarget = true;
        },
        [Presenter] { return Presenter->GetSnapshot().Mode == EHudMode::Spectator && Presenter->GetDrawCount() > 0; });
    Capture(
        TEXT("third-person"),
        [this, Presenter]
        {
            Presenter->ResetPreview();
            Pawn->SetPerspective(1);
        },
        [Presenter] { return !Presenter->GetSnapshot().bFirstPerson; });
    Capture(
        TEXT("scale2"),
        [this]
        {
            Pawn->SetPerspective(0);
            Controller->GetMenuHost()->GetVideoSettings()->ApplySetting(TEXT("guiScale"), TEXT("{\"value\":2}"));
        },
        [Presenter] { return Presenter->GetGuiScale() == 2; });
    // The creative inventory follows the same setting as the HUD (vanilla: one GUI scale for the HUD and every GUI):
    // at 2, at 3 with the HUD at 3 under it, and at Auto (the viewport's). The pairs are the item 3 captures.
    auto Creative = [this] { return Controller->GetPanels()->GetCreativeScreen(); };
    // A requested scale, at most the viewport's Auto (a window under 960 x 720 cannot show 3).
    auto Expected = [this](int32 Requested)
    {
        FIntPoint Viewport;
        Controller->GetViewportSize(Viewport.X, Viewport.Y);
        return HudModel::GetScale(Viewport, Requested);
    };
    auto SetGuiScale = [this](int32 Scale)
    {
        Controller->GetMenuHost()->GetVideoSettings()->ApplySetting(
            TEXT("guiScale"), FString::Printf(TEXT("{\"value\":%d}"), Scale));
    };
    auto OpenCreative = [this]
    {
        if (!Controller->GetPanels()->IsInventoryOpen()) Controller->GetPanels()->OpenInventory();
    };
    Capture(TEXT("creative-scale2"), OpenCreative,
        [Creative, Expected] { return Creative().IsValid() && Creative()->GetLastFrameScale() == Expected(2); });
    Capture(
        TEXT("creative-scale3"), [SetGuiScale] { SetGuiScale(3); },
        [Creative, Expected] { return Creative().IsValid() && Creative()->GetLastFrameScale() == Expected(3); });
    Capture(
        TEXT("scale3"), [this] { Controller->GetPanels()->Close(); }, [this, Presenter, Expected]
        { return !Controller->GetPanels()->IsOpen() && Presenter->GetGuiScale() == Expected(3); });
    Capture(
        TEXT("creative-auto"),
        [SetGuiScale, OpenCreative]
        {
            SetGuiScale(0);
            OpenCreative();
        },
        [this, Creative]
        {
            FIntPoint Viewport;
            Controller->GetViewportSize(Viewport.X, Viewport.Y);
            return Creative().IsValid() && Creative()->GetLastFrameScale() == HudModel::GetScale(Viewport, 0);
        });
    AddCase(TEXT("hud-creative-close"), .3, [this] { Controller->GetPanels()->Close(); }, [] {});

    // Milestone 2: the same HUD over the player's real vitals, driven by movement, the course's water
    // column and a real attack on the Playground's training target. Player positions are course-local
    // (ResetCoursePlayer) except the attack cases, which stand at the target in world space. Open floor at
    // course y=-2000 keeps falls and walks clear of the course.
    auto Vitals = [this]() -> FFoundationVitals& { return Pawn->Vitals; };
    Capture(
        TEXT("vitals-fresh"),
        [this, Presenter]
        {
            Presenter->SetPreviewEnabled(false);
            Controller->GetMenuHost()->GetVideoSettings()->ApplySetting(TEXT("guiScale"), TEXT("{\"value\":0}"));
            Controller->GetGameInstance()->GetSubsystem<UMenuPreferences>()->ApplyChanges(
                TEXT("{\"settings\":{\"mainHand\":\"Right\",\"attackIndicator\":\"Crosshair\"}}"));
            ResetCoursePlayer({-100, -850, 0});
            Controller->SetControlRotation(FRotator(-10, 35, 0));
        },
        [Presenter]
        {
            const auto& S = Presenter->GetSnapshot();
            // The Playground always has an authority, so the items are its real inventory (never the token double).
            return S.Resources == EHudDataSource::Authoritative && S.Items == EHudDataSource::Authoritative &&
                S.Health == 20 && S.Food == 20 && S.Air == 300 && S.Level == 0;
        });
    Capture(
        TEXT("vitals-fall"), [this] { ResetCoursePlayer({0, -2000, 600}); },
        [Vitals, Presenter]
        {
            // Six blocks: floor(6 - 3) = 3 points, bypassing armor. Saturated regeneration may already be healing.
            return Vitals().LastDamage.StartsWith(TEXT("fall 3 -> 3 to health")) &&
                Presenter->GetSnapshot().Health < 20;
        },
        1.8);
    Capture(
        TEXT("vitals-water"), [this] { ResetCoursePlayer({1650, 450, 0}); },
        [Vitals, Presenter]
        {
            const auto& S = Presenter->GetSnapshot();
            return S.bUnderwater && Vitals().bEyesInWater && S.Air > 200 && S.Air < 285;
        },
        2);
    Capture(
        TEXT("vitals-drown"), [Vitals] { Vitals().Air = -18; },
        [Vitals, Presenter]
        {
            // Air -20 drowns for 2 points and resets to zero, which keeps falling underwater.
            return Vitals().LastDamage.StartsWith(TEXT("drown 2 -> 2 to health")) && Vitals().Air <= 0 &&
                Presenter->GetSnapshot().Air <= 0;
        },
        .6);
    Capture(
        TEXT("vitals-surface"),
        [this, Vitals]
        {
            ResetCoursePlayer({1650, 150, 0}, false);
            Vitals().Air = 100;
        },
        [Vitals, Presenter]
        {
            const auto& S = Presenter->GetSnapshot();
            return !S.bUnderwater && !Vitals().bEyesInWater && S.Air > 100 && S.Air < 300;
        },
        .3);
    Capture(
        TEXT("vitals-hungry-sprint"),
        [this, Vitals]
        {
            ResetCoursePlayer({0, -2000, 0});
            Vitals().Food = 6;
            PressKey(EKeys::W, true);
            PressKey(EKeys::LeftShift, true);
        },
        [this, Presenter]
        {
            return !Pawn->Simulation.bSprinting && Pawn->Simulation.Rejection.Contains(TEXT("food")) &&
                Presenter->GetSnapshot().Food == 6;
        });
    Capture(
        TEXT("vitals-armor-xp"),
        [this, Vitals]
        {
            ResetCoursePlayer({0, -2000, 0});
            Vitals().Armor = 20;
            // Normal difficulty: 10 through 20 armor is 4.
            Vitals().Hurt(EFoundationDamage::MobAttack, 10);
            // 7 + 9 + 11 points reach level 3, plus 3 of its 13.
            Vitals().AddExperience(30);
        },
        [Vitals, Presenter]
        {
            const auto& S = Presenter->GetSnapshot();
            return Vitals().LastDamage.StartsWith(TEXT("mob 10 -> 4 to health")) && S.Armor == 20 && S.Level == 3 &&
                FMath::IsNearlyEqual(S.Experience, 3. / 13., 1e-4);
        });
    Capture(
        TEXT("vitals-creative"),
        [this, Vitals]
        {
            Vitals().Armor = 0;
            ResetCoursePlayer({0, -2000, 0});
            Vitals().Mode = EFoundationPlayerMode::Creative;
            Vitals().Hurt(EFoundationDamage::Generic, 5);
        },
        [Vitals, Presenter]
        {
            return Vitals().Health == 20 && Presenter->GetSnapshot().Mode == EHudMode::Creative &&
                Vitals().HasEnoughFood();
        });
    Capture(
        TEXT("vitals-dead"),
        [this, Vitals]
        {
            Vitals().Mode = EFoundationPlayerMode::Survival;
            ResetCoursePlayer({0, -2000, 0});
            Vitals().Hurt(EFoundationDamage::Kill, TNumericLimits<float>::Max());
        },
        [Vitals, Presenter] { return Vitals().IsDead() && Presenter->GetSnapshot().Health == 0; }, .5);
    Capture(
        TEXT("vitals-respawned"), [] {},
        [this, Vitals]
        {
            return !Vitals().IsDead() && Vitals().Health == 20 && Pawn->Simulation.Feet.Size2D() < 1 &&
                Vitals().Deaths > 0;
        },
        1);
    for (bool bReady : {false, true})
        Capture(
            bReady ? TEXT("vitals-attack-ready") : TEXT("vitals-attack-recharge"),
            [this, bReady]
            {
                if (bReady) return;
                // World position: two blocks north of the Playground's "Training target" (world (0, 1000)),
                // facing it, holding the iron sword (slot 3, 1.6 attacks per second, so a 12.5-tick
                // recovery: the held item sets the attack speed). The click is a real authority attack.
                ResetPlayer({0, 800, 0});
                Controller->SetControlRotation(FRotator(0, 90, 0));
                Pawn->SelectInteractionSlot(2);
                PressKey(EKeys::LeftMouseButton, true);
                PressKey(EKeys::LeftMouseButton, false);
            },
            [Presenter, bReady]
            {
                const auto& S = Presenter->GetSnapshot();
                return S.bLivingTarget && S.bTargetInRange && S.AttackDelay > 5 &&
                    (bReady ? HudModel::IsReadyMarkerVisible(S) : S.AttackStrength < 1);
            },
            bReady ? 1. : .3);
    AddCase(
        TEXT("hud-performance-warm"), 4,
        [this, Presenter]
        {
            // Measured over the milestone-1 fixture so the cost stays comparable.
            Pawn->Vitals.AttackSpeed = 4;
            Presenter->SetPreviewEnabled(true);
            Presenter->ResetPreview();
            Pawn->SelectInteractionSlot(0);
            Controller->GetMenuHost()->GetVideoSettings()->ApplySetting(TEXT("guiScale"), TEXT("{\"value\":0}"));
            Controller->GetHud()->bOverlay = Controller->GetHud()->bShowCollision = Controller->GetHud()->bHideHud =
                false;
        },
        [] {});
    for (int32 Pass = 0; Pass < 4; ++Pass)
        AddCase(
            FString::Printf(TEXT("hud-performance-%s-%d"), Pass % 2 ? TEXT("on") : TEXT("off"), Pass / 2 + 1), 5,
            [this, Pass] { Controller->GetHud()->bHideHud = Pass % 2 == 0; }, [] {});
#endif
}
