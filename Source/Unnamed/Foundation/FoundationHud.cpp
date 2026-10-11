#include "Foundation/FoundationHud.h"
#include "Hud/HudPresenter.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationCoordinates.h"
#include "Foundation/FoundationInputRouter.h"
#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "Input/UnnamedInputSettings.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Menu/MenuBrowserHost.h"
#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "RenderTimer.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    // Bits of UUnnamedInputSettings::DebugSections (Debug Options panel order).
    enum EDebugSection : int32
    {
        PoseSection = 1,
        MovementSection = 2,
        InputSection = 4,
        ActionSection = 8,
        PerformanceSection = 16,
        AllSections = 31,
    };
}

AFoundationController* UFoundationHud::GetFoundationController() const
{
    return Cast<AFoundationController>(GetOwner());
}

void UFoundationHud::BeginPlay()
{
    Super::BeginPlay();
    AFoundationController* Controller = GetFoundationController();
    if (!Controller || !Controller->IsLocalController() || !GEngine || !GEngine->GameViewport ||
        !FSlateApplication::IsInitialized())
        return;
    const FLinearColor White = FLinearColor::White;
    Presenter = NewObject<UHudPresenter>(this);
    // clang-format off: Slate's declarative layout reads best indented by hand.
    OverlayRoot = SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
        + SOverlay::Slot()
        [ Presenter->CreateView() ]
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(12)
        [
            SNew(SBorder).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, .75f))
            [
                SAssignNew(DebugText, STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Mono", 11)).ColorAndOpacity(White)
            ]
        ];
    // clang-format on
    GEngine->GameViewport->AddViewportWidgetContent(OverlayRoot.ToSharedRef(), 200);
    LastFrameTime = FPlatformTime::Seconds();
    LogEvent(TEXT("diagnostics ready; reference Java26.3 / UE5.8.3"));
}

void UFoundationHud::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GEngine && GEngine->GameViewport && OverlayRoot)
        GEngine->GameViewport->RemoveViewportWidgetContent(OverlayRoot.ToSharedRef());
    Super::EndPlay(EndPlayReason);
}

void UFoundationHud::LogEvent(const FString& Text)
{
    Events.Add(FString::Printf(TEXT("%.2f %s"), GetWorld() ? GetWorld()->GetTimeSeconds() : 0, *Text));
    if (Events.Num() > 24) Events.RemoveAt(0);
}

void UFoundationHud::ToggleOverlay()
{
    bOverlay = !bOverlay;
    LogEvent(TEXT("debug overlay toggled"));
}

void UFoundationHud::CopySnapshot()
{
    FPlatformApplicationMisc::ClipboardCopy(*BuildSnapshot());
    LogEvent(TEXT("diagnostic snapshot copied"));
}

void UFoundationHud::BeginFrame()
{
    Now = FPlatformTime::Seconds();
    FrameMs = FrameMs == 0 ? (Now - LastFrameTime) * 1000 : FrameMs * .9 + (Now - LastFrameTime) * 100.;
    LastFrameTime = Now;
}

void UFoundationHud::NoteMotion(const AFoundationPawn& Pawn)
{
    const FFoundationSimulation& Simulation = Pawn.Simulation;
    const FString Motion = FString::Printf(TEXT("ground=%d crouch=%d sprint=%d headroom=%d fly=%d"),
        Simulation.bGrounded, Simulation.bCrouched, Simulation.bSprinting, Simulation.bHeadroom, Simulation.bFlying);
    if (Motion == LastMotionState) return;
    LastMotionState = Motion;
    LogEvent(Motion);
}

void UFoundationHud::NoteVitals(const FFoundationVitals& Vitals)
{
    if (Vitals.DamageEvents != LoggedDamageEvents)
    {
        LoggedDamageEvents = Vitals.DamageEvents;
        LogEvent(TEXT("damage: ") + Vitals.LastDamage);
    }
    if (Vitals.IsDead() != bLoggedDead)
    {
        bLoggedDead = Vitals.IsDead();
        LogEvent(bLoggedDead ? Vitals.DeathMessage : FString(TEXT("respawned with fresh vitals")));
    }
}

void UFoundationHud::DrawDebug(const AFoundationPawn& Pawn) const
{
    const FFoundationSimulation& Simulation = Pawn.Simulation;
    if (Pawn.GetPerspective())
    {
        const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
        DrawDebugBox(GetWorld(), Simulation.Feet + FVector(0, 0, Simulation.HeightCm / 2),
            FVector(Profile.WidthCm / 2, Profile.WidthCm / 2, Simulation.HeightCm / 2), FColor::Cyan, false, -1, 0,
            1.5);
    }
    if (bShowCollision && Pawn.GetLab())
    {
        // What the movement solver sees near the player: the lab's cuboids and any committed blocks.
        TArray<FBox> Boxes;
        Pawn.GetLab()->GatherCollision(FFoundationSimulation::Bounds(Simulation.Feet, 180, 60).ExpandBy(800), Boxes);
        for (const FBox& Box : Boxes)
            DrawDebugBox(GetWorld(), Box.GetCenter(), Box.GetExtent(), FColor::Green, false, -1, 0, .3f);
    }
}

void UFoundationHud::Refresh()
{
    if (Presenter) Presenter->Refresh(*GetFoundationController(), Now);
    if (Now < NextRefreshTime || !DebugText) return;
    NextRefreshTime = Now + .1;
    AFoundationController* Controller = GetFoundationController();
    DebugText->SetVisibility(bOverlay ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
    if (bOverlay)
    {
        DebugText->SetText(FText::FromString(BuildSnapshot()));
        const UUnnamedInputSubsystem* InputSubsystem = Controller->GetInputSubsystem();
        if (const UUnnamedInputSettings* Settings = InputSubsystem ? InputSubsystem->GetSettings() : nullptr)
            DebugText->SetFont(
                FCoreStyle::GetDefaultFontStyle("Mono", FMath::RoundToInt(11 * Settings->DebugTextScale)));
    }
}

FString UFoundationHud::BuildInteractionText(const AFoundationPawn& Pawn, const FFoundationAuthority& Authority)
{
    const FFoundationInteraction& Interaction = Pawn.Interaction;
    const FFoundationPlayer* Player = Authority.FindPlayer(Interaction.PlayerId);
    if (!Player) return TEXT("Interaction: not registered\n");
    auto Describe = [&Authority, Player](const FFoundationItemInstance& Stack)
    {
        if (Stack.IsEmpty()) return FString(TEXT("empty hand"));
        const FFoundationItemDefinition& Item = Authority.Rules.GetItem(Stack.Item);
        FString Text = FString::Printf(TEXT("%s #%d r%d"), *Item.Id.ToString(), Stack.InstanceId, Stack.Revision);
        if (Item.CanPlace())
            Text += Player->Inventory.bInfiniteSupply ? TEXT(" x inf") : *FString::Printf(TEXT(" x%d"), Stack.Count);
        if (Item.MaxDurability > 0)
            Text += FString::Printf(TEXT(" %d/%d"), Item.MaxDurability - Stack.Damage, Item.MaxDurability);
        return Text;
    };
    const FFoundationPreview& Preview = Interaction.Preview;
    FString Out =
        FString::Printf(TEXT("PLAYGROUND (session-only) | supply %s | blocks %d rev %lld | placed %d broke %d hits %d "
                             "rejected %d duplicates %d resets %d\n"),
            Player->Inventory.bInfiniteSupply ? TEXT("infinite") : TEXT("finite, tool wear"),
            Authority.World.GetSolidCount(), Authority.World.Revision, Authority.Placements, Authority.Breaks,
            Authority.Hits, Authority.Rejections, Authority.Duplicates, Authority.Resets);
    Out += FString::Printf(TEXT("Hand slot %d: %s | off: %s\n"), Player->Inventory.Selected + 1,
        *Describe(Player->Inventory.GetHand(EFoundationHand::Main)), *Describe(Player->Inventory.Offhand));
    Out += TEXT("Target ") + FoundationTargeting::Describe(Preview.Target);
    if (Preview.bInteraction)
        Out += TEXT(" | use: interact");
    else if (Preview.bPlacement)
        Out += FString::Printf(TEXT(" | place %s %s"), *FoundationGrid::DescribeCell(Preview.Cell),
            Preview.bValid ? TEXT("ok") : LexToString(Preview.Reason));
    Out += TEXT("\n") + Interaction.Describe() + TEXT("\n");
    for (const FFoundationTrainingTarget& Target : Authority.Targets)
        Out += FString::Printf(TEXT("%s #%d: %.2f/%.0f cooldown %d hits %d%s | "), *Target.Label, Target.Id,
            Target.Health.Health, Target.Health.MaxHealth, Target.Health.DamageCooldown, Target.Hits,
            Target.Health.IsDead() ? TEXT(" DOWN") : TEXT(""));
    if (Pawn.InteractionMs.Num())
    {
        TArray<float> Recent(Pawn.InteractionMs.GetData() + FMath::Max(0, Pawn.InteractionMs.Num() - 600),
            FMath::Min(600, Pawn.InteractionMs.Num()));
        Recent.Sort();
        Out += FString::Printf(TEXT("\nInteraction cost p50 %.3f p95 %.3f ms/update (%d updates)"),
            Recent[Recent.Num() / 2], Recent[FMath::Min(Recent.Num() - 1, FMath::FloorToInt32(Recent.Num() * .95f))],
            Recent.Num());
    }
    return Out + TEXT("\n");
}

FString UFoundationHud::BuildSnapshot() const
{
    const AFoundationController* Controller = GetFoundationController();
    const AFoundationPawn* Pawn = Controller ? Controller->GetFoundationPawn() : nullptr;
    UUnnamedInputSubsystem* InputSubsystem = Controller ? Controller->GetInputSubsystem() : nullptr;
    const UUnnamedInputSettings* Settings = InputSubsystem ? InputSubsystem->GetSettings() : nullptr;
    const int32 Sections = Settings ? Settings->DebugSections : AllSections;
    FString Out = TEXT("FOUNDATION | Java 26.3 / UE 5.8.3 | 1 block = 100 cm\n");
    if (!Pawn) Out += TEXT("PLAYER: no player possessed (menu)\n");
    if (Pawn)
    {
        const FFoundationSimulation& Simulation = Pawn->Simulation;
        const FFoundationIntent& Intent = Pawn->LastIntent;
        const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
        const FVector Blocks =
            FoundationCoordinates::ToMinecraft(Simulation.Feet, Profile.MinecraftOriginCm, GetWorld()->OriginLocation);
        const FInt64Vector3 Block = FoundationCoordinates::Blocks(Blocks);
        const FRotator View = Controller->GetControlRotation();
        if (Sections & PoseSection)
        {
            Out += FString::Printf(
                TEXT("Feet XYZ blocks: %.3f / %.3f / %.3f | block: %lld / %lld / %lld\nFacing %s | MC yaw %.2f pitch "
                     "%.2f | view %d\nPose %s | box %.1f x %.1f x %.1f cm | eyes %.2f cm (smoothed %.2f)\n"),
                Blocks.X, Blocks.Y, Blocks.Z, Block.X, Block.Y, Block.Z, *FoundationCoordinates::Facing(View.Yaw),
                FoundationCoordinates::Yaw(View.Yaw), FoundationCoordinates::Pitch(View.Pitch), Pawn->GetPerspective(),
                Simulation.bCrouched ? TEXT("crouched") : TEXT("standing"), Profile.WidthCm, Profile.WidthCm,
                Simulation.HeightCm, Simulation.bCrouched ? Profile.CrouchingEyesCm : Profile.StandingEyesCm,
                Simulation.EyeCm);
            if (Settings && Settings->bDebugUnrealCoordinates)
                Out += FString::Printf(TEXT("UE feet/actor cm %s | camera cm %s\n"), *Simulation.Feet.ToCompactString(),
                    *Pawn->GetCamera()->GetComponentLocation().ToCompactString());
            if (const AFoundationLab* Lab = Pawn->GetLab())
                if (const FString WorldLine = Lab->DescribeDebug(); !WorldLine.IsEmpty()) Out += WorldLine + TEXT("\n");
        }
        if (Sections & MovementSection)
            Out += FString::Printf(
                TEXT("%s | sprint intent=%d resolved=%d | crouch intent=%d pose=%d | headroom=%d\nVelocity post-drag "
                     "cm/ref.tick %s | measured H %.4f blocks/s V %.4f blocks/s\nDisplacement %.5f blocks/reference "
                     "tick (20 Hz) | step %lld | dropped %.3fs\nEligibility: %s\n"),
                Simulation.bGrounded ? TEXT("grounded") : TEXT("airborne"), Intent.bSprint, Simulation.bSprinting,
                Intent.bCrouch, Simulation.bCrouched, Simulation.bHeadroom,
                *Simulation.VelocityPerTick.ToCompactString(), Simulation.LastDisplacement.Size2D() / 5.,
                Simulation.LastDisplacement.Z / 5., Simulation.LastDisplacement.Size() / 100., Simulation.Steps,
                Simulation.DroppedSeconds, *Simulation.Rejection);
        if (Sections & MovementSection)
            Out += TEXT("Vitals: ") + Pawn->Vitals.Describe() + TEXT("\nLast damage: ") + Pawn->Vitals.LastDamage +
                FString::Printf(
                    TEXT(" | deaths %d | fall %.2f blocks\n"), Pawn->Vitals.Deaths, Pawn->Vitals.FallDistanceCm / 100.);
        const FFoundationAuthority* Authority = Pawn->GetAuthority();
        if ((Sections & ActionSection) && Authority)
            Out += BuildInteractionText(*Pawn, *Authority);
        else if (Sections & ActionSection)
        {
            const FFoundationActions& Actions = Controller->Actions;
            const FFoundationTarget Target = Pawn->TraceTarget();
            Out += FString::Printf(TEXT("Hotbar %d | main=%d off=%d | hand=%s | break=%d use=%d\nTarget %s | distance "
                                        "%.3f reach %.2f blocks | allowed=%d\nHit cm %s normal %s\nResult: %s\n"),
                Actions.Slot + 1, Actions.Slots[Actions.Slot], Actions.Offhand, *Actions.LastHand, Actions.bBreaking,
                Actions.bUsing, *Target.Name, Target.DistanceCm / 100., Target.ReachCm / 100., Target.bInRange,
                *Target.Hit.ToCompactString(), *Target.Normal.ToCompactString(), *Actions.LastResult);
        }
    }
    if (Sections & InputSection)
    {
        const UFoundationPanels* Panels = Controller ? Controller->GetPanels() : nullptr;
        const FString Contexts =
            InputSubsystem ? InputSubsystem->DescribeAppliedContexts() : TEXT("no input subsystem");
        Out += FString::Printf(TEXT("Contexts: %s + diagnostics preprocessor | capture=%d text=%d panel=%d | "
                                    "cursor=%s\nPhysical %s -> logical %s\n"),
            *Contexts, Controller && Controller->GetInputRouter()->IsCapturing(), Panels && Panels->IsTextOpen(),
            Panels && Panels->IsOpen(),
            Controller && Controller->bShowMouseCursor ? TEXT("visible") : TEXT("game capture requested"),
            Controller ? *Controller->GetInputRouter()->GetLastPhysical() : TEXT("none"),
            Controller ? *Controller->GetLastLogical() : TEXT("none"));
        if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
            Out += FString::Printf(TEXT("Viewport actual mouse capture=%d focus=%d\n"),
                GEngine->GameViewport->Viewport->HasMouseCapture(), GEngine->GameViewport->Viewport->HasFocus());
        else
            Out += TEXT("Viewport capture/focus: unavailable\n");
        if (Settings)
            Out += TEXT("Profile: ") + Settings->GetActiveKeyProfileId() + TEXT(" | focus ") +
                (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive() ? TEXT("active")
                                                                                           : TEXT("inactive")) +
                TEXT("\n");
        if (InputSubsystem)
        {
            int32 Count = 0;
            for (FName Id : InputSubsystem->GetSupportedIds())
            {
                Out += Id.ToString().Replace(TEXT("key."), TEXT("")) + TEXT("=") +
                    InputSubsystem->GetCurrentKey(Id).ToString() + TEXT("  ");
                if (++Count % 5 == 0) Out += TEXT("\n");
            }
            Out += TEXT("\n");
        }
    }
    if (Sections & PerformanceSection)
    {
        Out += FString::Printf(TEXT("Frame %.2f ms | %.1f FPS | "), FrameMs, FrameMs > 0 ? 1000. / FrameMs : 0.);
        const uint32 GPUCycles = GDynamicRHI ? RHIGetGPUFrameCycles() : 0;
        Out += GPUCycles ? FString::Printf(TEXT("GPU %.3f ms\n"), FPlatformTime::ToMilliseconds(GPUCycles))
                         : TEXT("GPU timing: unavailable\n");
        if (GGameThreadTime && GRenderThreadTime)
            Out += FString::Printf(TEXT("Engine CPU game %.3f ms / render %.3f ms (excludes idle)\n"),
                FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime));
        else
            Out += TEXT("Engine CPU timings: unavailable\n");
        for (int32 Index = FMath::Max(0, Events.Num() - 6); Index < Events.Num(); ++Index)
            Out += Events[Index] + TEXT("\n");
    }
    return Out;
}
