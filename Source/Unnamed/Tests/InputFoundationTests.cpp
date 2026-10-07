#if WITH_DEV_AUTOMATION_TESTS
#include "Input/UnnamedInputIntent.h"
#include "Input/UnnamedInputSettings.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Menu/MenuInputBridge.h"
#include "Foundation/FoundationActions.h"
#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    struct FInputFixture
    {
        UGameInstance* Instance;
        UWorld* World;
        ULocalPlayer* Player;
        APlayerController* Controller;
        UUnnamedInputSubsystem* Input;
        UEnhancedPlayerInput* PlayerInput;
        UEnhancedInputComponent* Component;
        UMenuInputBridge* Bridge;

        FInputFixture()
        {
            Instance = NewObject<UGameInstance>(GEngine);
            Instance->AddToRoot();
            Instance->InitializeStandalone();
            World = Instance->GetWorld();
            Instance->GetWorldContext()->GameViewport = NewObject<UGameViewportClient>(GEngine);
            FString Error;
            Player = Instance->CreateLocalPlayer(0, Error, false);
            Controller = World->SpawnActor<APlayerController>();
            PlayerInput = NewObject<UEnhancedPlayerInput>(Controller);
            Controller->PlayerInput = PlayerInput;
            Component = NewObject<UEnhancedInputComponent>(Controller);
            Controller->InputComponent = Component;
            Controller->SetPlayer(Player);
            Input = Player->GetSubsystem<UUnnamedInputSubsystem>();
            Bridge = NewObject<UMenuInputBridge>(Controller);
            Bridge->Initialize(Input);
        }

        ~FInputFixture()
        {
            Instance->RemoveLocalPlayer(Player);
            Instance->Shutdown();
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
            Instance->RemoveFromRoot();
        }

        void Tick() { PlayerInput->ProcessInputStack({Component}, 1.f / 60.f, false); }

        void Key(FKey Key, EInputEvent Event)
        {
            PlayerInput->InputKey(
                FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key, Event, 1.f, false, 0));
            Tick();
        }
    };

    TSharedPtr<FJsonObject> Read(const FString& Json)
    {
        TSharedPtr<FJsonObject> Value;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Value);
        return Value;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputIntentTest, "Unnamed.Input.IntentRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInputIntentTest::RunTest(const FString& Parameters)
{
    FUnnamedInputIntentModel Model;
    TestEqual(TEXT("No movement is idle"), Model.Intent.RequestedGait, EUnnamedRequestedGait::Idle);
    Model.Sprint(true);
    TestEqual(TEXT("Sprint while still remains idle"), Model.Intent.RequestedGait, EUnnamedRequestedGait::Idle);
    Model.Move({0, 1});
    TestEqual(TEXT("Move plus sprint requests sprint"), Model.Intent.RequestedGait, EUnnamedRequestedGait::Sprint);
    Model.Crouch(true);
    TestEqual(TEXT("Crouch wins over sprint"), Model.Intent.RequestedGait, EUnnamedRequestedGait::Walk);
    Model.Crouch(false);
    Model.Sprint(false);
    TestEqual(TEXT("Released sprint becomes walk"), Model.Intent.RequestedGait, EUnnamedRequestedGait::Walk);
    Model.Move({1, 1});
    TestTrue(TEXT("Diagonal request is normalized"), FMath::IsNearlyEqual(Model.Intent.Move.Size(), 1.0));
    Model.Jump(true);
    Model.Intent.bJumpPressed = false;
    Model.Jump(true);
    TestFalse(TEXT("Held jump does not manufacture another press"), Model.Intent.bJumpPressed);
    Model.SetPolicies(true, true);
    Model.Move({0, 1});
    Model.Sprint(true);
    Model.Sprint(true);
    Model.Sprint(false);
    TestTrue(TEXT("Toggle survives release, ignores repeats"), Model.Intent.bSprintRequested);
    Model.Sprint(true);
    TestFalse(TEXT("Second press clears toggle"), Model.Intent.bSprintRequested);
    Model.Crouch(true);
    Model.Crouch(false);
    TestTrue(TEXT("Crouch toggle survives release"), Model.Intent.bCrouchRequested);
    Model.Reset();
    TestFalse(TEXT("Reset clears crouch toggle"), Model.Intent.bCrouchRequested);
    TestTrue(TEXT("Reset clears movement"), Model.Intent.Move.IsNearlyZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputBindingTest, "Unnamed.Input.BindingsAndSerialization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInputBindingTest::RunTest(const FString& Parameters)
{
    FInputFixture F;
    auto State = Read(F.Bridge->GetState());
    if (!TestTrue(TEXT("Native input assets and settings ready"), State->GetBoolField(TEXT("ready")))) return false;
    State = Read(F.Bridge->ResetBindings(true));
    TestEqual(TEXT("35 foundation bindings"), State->GetArrayField(TEXT("supported")).Num(), 35);
    TestEqual(TEXT("Project sprint default Left Shift"),
        State->GetObjectField(TEXT("nativeKeys"))->GetStringField(TEXT("key.sprint")), FString(TEXT("LeftShift")));
    TestEqual(TEXT("Project crouch default C"),
        State->GetObjectField(TEXT("nativeKeys"))->GetStringField(TEXT("key.sneak")), FString(TEXT("C")));
    TestTrue(TEXT("Remap forward to Up"),
        Read(F.Bridge->ApplyBinding(TEXT("key.forward"), TEXT("Up")))->GetBoolField(TEXT("ok")));
    for (const TCHAR* Key : {TEXT("Up"), TEXT("Escape"), TEXT("F11"), TEXT("Tilde"), TEXT("MouseX"),
             TEXT("Gamepad_LeftX"), TEXT("BogusKey")})
        TestFalse(FString(TEXT("Reject conflict/reserved/invalid: ")) + Key,
            Read(F.Bridge->ApplyBinding(TEXT("key.back"), Key))->GetBoolField(TEXT("ok")));
    TestFalse(TEXT("Deferred action rejected"),
        Read(F.Bridge->ApplyBinding(TEXT("key.advancements"), TEXT("J")))->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("Unbind is supported"),
        Read(F.Bridge->ApplyBinding(TEXT("key.jump"), TEXT("None")))->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("Mouse button mapping supported"),
        Read(F.Bridge->ApplyBinding(TEXT("key.jump"), TEXT("ThumbMouseButton")))->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("Wheel pulse can be rebound"),
        Read(F.Bridge->ApplyBinding(TEXT("key.jump"), TEXT("MouseScrollUp")))->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("Debug chord can share a gameplay key"),
        Read(F.Bridge->ApplyBinding(TEXT("key.debug.showHitboxes"), TEXT("W")))->GetBoolField(TEXT("ok")));
    TestFalse(TEXT("Same debug chord collision rejected"),
        Read(F.Bridge->ApplyBinding(TEXT("key.debug.clearChat"), TEXT("W")))->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("Toggle sprint policy accepted"),
        Read(F.Bridge->ApplyPolicy(TEXT("toggleSprint"), true))->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("Custom sprint binding accepted"),
        Read(F.Bridge->ApplyBinding(TEXT("key.sprint"), TEXT("RightControl")))->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("Custom crouch binding accepted"),
        Read(F.Bridge->ApplyBinding(TEXT("key.sneak"), TEXT("RightShift")))->GetBoolField(TEXT("ok")));
    TArray<uint8> Bytes;
    TestTrue(TEXT("Serialize using Unreal SaveGame"), UGameplayStatics::SaveGameToMemory(F.Input->Settings(), Bytes));
    auto* Restored = Cast<UUnnamedInputSettings>(UGameplayStatics::LoadGameFromMemory(Bytes));
    TestNotNull(TEXT("Settings class restores"), Restored);
    if (Restored)
    {
        Restored->Initialize(F.Player);
        Restored->RegisterInputMappingContext(F.Input->Context());
        FMapPlayerKeyArgs Query;
        Query.MappingName = TEXT("key.forward");
        Query.Slot = EPlayerMappableKeySlot::First;
        const auto* Mapping = Restored->GetActiveKeyProfile()->FindKeyMapping(Query);
        TestTrue(
            TEXT("Remap survives complete serializer round trip"), Mapping && Mapping->GetCurrentKey() == EKeys::Up);
        Query.MappingName = TEXT("key.sprint");
        Mapping = Restored->GetActiveKeyProfile()->FindKeyMapping(Query);
        TestTrue(TEXT("Custom sprint survives registration with project default"),
            Mapping && Mapping->GetCurrentKey() == EKeys::RightControl && Mapping->GetDefaultKey() == EKeys::LeftShift);
        Query.MappingName = TEXT("key.sneak");
        Mapping = Restored->GetActiveKeyProfile()->FindKeyMapping(Query);
        TestTrue(TEXT("Custom crouch survives registration with project default"),
            Mapping && Mapping->GetCurrentKey() == EKeys::RightShift && Mapping->GetDefaultKey() == EKeys::C);
        TestTrue(TEXT("Hold/toggle policy survives serializer"), Restored->bToggleSprint);
    }
    State = Read(F.Bridge->ResetBinding(TEXT("key.sneak")));
    TestEqual(TEXT("Reset one restores C crouch"),
        State->GetObjectField(TEXT("nativeKeys"))->GetStringField(TEXT("key.sneak")), FString(TEXT("C")));
    TestEqual(TEXT("Reset crouch preserves custom sprint"),
        State->GetObjectField(TEXT("nativeKeys"))->GetStringField(TEXT("key.sprint")), FString(TEXT("RightControl")));
    State = Read(F.Bridge->ResetBinding(TEXT("key.sprint")));
    TestEqual(TEXT("Reset one restores Left Shift sprint"),
        State->GetObjectField(TEXT("nativeKeys"))->GetStringField(TEXT("key.sprint")), FString(TEXT("LeftShift")));
    TestTrue(TEXT("Reset all movement and policies"), Read(F.Bridge->ResetBindings(true))->GetBoolField(TEXT("ok")));
    TestFalse(TEXT("Reset returns sprint policy to Hold"), F.Input->Settings()->bToggleSprint);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputPipelineTest, "Unnamed.Input.NativeKeyPipeline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInputPipelineTest::RunTest(const FString& Parameters)
{
    FInputFixture F;
    if (!TestTrue(TEXT("Bind actual Enhanced Input component"), F.Input->BindGameplayInput(F.Component))) return false;
    F.Input->SetGameplayInputEnabled(true);
    F.Key(EKeys::W, IE_Pressed);
    TestEqual(TEXT("W reaches native Move action"), F.Input->GetIntent().Move, FVector2D(0, 1));
    F.Key(EKeys::S, IE_Pressed);
    TestTrue(TEXT("W and S cancel"), F.Input->GetIntent().Move.IsNearlyZero());
    F.Key(EKeys::S, IE_Released);
    F.Key(EKeys::D, IE_Pressed);
    TestTrue(TEXT("Native diagonal is normalized"), FMath::IsNearlyEqual(F.Input->GetIntent().Move.Size(), 1.0));
    TestTrue(TEXT("Both diagonal axes survive debug-key overlap"),
        F.Input->GetIntent().Move.X > .7 && F.Input->GetIntent().Move.Y > .7);
    F.Key(EKeys::LeftShift, IE_Pressed);
    TestEqual(TEXT("Native Shift requests sprint"), F.Input->GetIntent().RequestedGait, EUnnamedRequestedGait::Sprint);
    TestFalse(TEXT("Shift no longer crouches"), F.Input->GetIntent().bCrouchRequested);
    F.Key(EKeys::C, IE_Pressed);
    TestTrue(TEXT("Native C requests crouch despite debug C overlap"), F.Input->GetIntent().bCrouchRequested);
    TestEqual(TEXT("Native crouch takes precedence"), F.Input->GetIntent().RequestedGait, EUnnamedRequestedGait::Walk);
    F.Key(EKeys::SpaceBar, IE_Pressed);
    TestTrue(TEXT("Jump first simulation update sees edge"), F.Input->ConsumeIntent().bJumpPressed);
    TestFalse(TEXT("Next simulation update does not repeat edge"), F.Input->ConsumeIntent().bJumpPressed);
    TestTrue(TEXT("Jump remains held"), F.Input->GetIntent().bJumpHeld);
    F.Input->ClearIntent();
    F.Tick();
    TestTrue(TEXT("Focus/reset clears movement"), F.Input->GetIntent().Move.IsNearlyZero());
    TestFalse(TEXT("Focus/reset clears jump"), F.Input->GetIntent().bJumpHeld);
    F.Input->SetGameplayInputEnabled(false);
    F.Key(EKeys::W, IE_Released);
    F.Key(EKeys::W, IE_Pressed);
    TestTrue(TEXT("UI-only mode cannot create movement"), F.Input->GetIntent().Move.IsNearlyZero());
    F.Input->SetGameplayInputEnabled(true);
    F.Tick();
    TestTrue(TEXT("Held key on enabling waits for release"), F.Input->GetIntent().Move.IsNearlyZero());
    F.Key(EKeys::W, IE_Released);
    F.Key(EKeys::W, IE_Pressed);
    TestEqual(TEXT("Fresh press after enabling works"), F.Input->GetIntent().Move, FVector2D(0, 1));
    F.Key(EKeys::W, IE_Released);
    Read(F.Bridge->ApplyBinding(TEXT("key.forward"), TEXT("Up")));
    F.Input->SetGameplayInputEnabled(true);
    F.Key(EKeys::W, IE_Pressed);
    TestTrue(TEXT("Old key stops moving after remap"), F.Input->GetIntent().Move.IsNearlyZero());
    F.Key(EKeys::W, IE_Released);
    F.Key(EKeys::Up, IE_Pressed);
    TestEqual(TEXT("Remapped physical key reaches movement"), F.Input->GetIntent().Move, FVector2D(0, 1));
    Read(F.Bridge->ResetBindings(true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationActionsTest, "Unnamed.Foundation.ActionContracts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationActionsTest::RunTest(const FString&)
{
    FInputFixture F;
    auto* Lab = F.World->SpawnActor<AFoundationLab>();
    const auto& P = *GetDefault<UFoundationProfile>();
    Lab->Primitives = {
        {FBox(FVector(449, -50, 0), FVector(549, 50, 200)), TEXT("block"), EFoundationTargetKind::Block, true}};
    auto Target = Lab->Target({0, 0, 162}, {1, 0, 0}, P);
    TestTrue(TEXT("block 4.49m reachable"), Target.bInRange);
    Target = Lab->Target({-1, 0, 162}, {1, 0, 0}, P);
    TestFalse(TEXT("block exact4.5m rejected"), Target.bInRange);
    Lab->Primitives[0].Kind = EFoundationTargetKind::Entity;
    TestFalse(TEXT("entity same4.49m rejected"), Lab->Target({0, 0, 162}, {1, 0, 0}, P).bInRange);
    Target = Lab->Target({150, 0, 162}, {1, 0, 0}, P);
    TestTrue(TEXT("entity2.99m reachable"), Target.bInRange);
    Lab->Primitives.Add(
        {FBox(FVector(300, -50, 0), FVector(350, 50, 200)), TEXT("occluder"), EFoundationTargetKind::Block, false});
    TestEqual(
        TEXT("solid block occludes entity"), Lab->Target({150, 0, 162}, {1, 0, 0}, P).Name, FString(TEXT("occluder")));
    FFoundationActions A;
    FFoundationActionRequest R;
    R.Target = Target;
    TestTrue(TEXT("entity attack press accepted"), A.Dispatch(R).bAccepted);
    TestFalse(TEXT("entity attack does not mine"), A.bBreaking);
    R.Phase = EFoundationPhase::Hold;
    TestFalse(TEXT("held entity attack is not extra clicks"), A.Dispatch(R).bAccepted);
    R.Phase = EFoundationPhase::Press;
    R.Target.Kind = EFoundationTargetKind::Block;
    A.Dispatch(R);
    TestTrue(TEXT("block break fixture active"), A.bBreaking);
    R.Action = EFoundationAction::Use;
    TestFalse(TEXT("active break prevents use"), A.Dispatch(R).bAccepted);
    R.Action = EFoundationAction::Attack;
    R.Phase = EFoundationPhase::Release;
    A.Dispatch(R);
    TestFalse(TEXT("release cancels break"), A.bBreaking);
    R.Action = EFoundationAction::Use;
    R.Phase = EFoundationPhase::Press;
    TestTrue(TEXT("interactive target accepted before item"), A.Dispatch(R).bAccepted);
    TestFalse(TEXT("interaction is not ongoing item use"), A.bUsing);
    R.Target.bRejectInteraction = true;
    TestFalse(TEXT("block failure prevents item/offhand fallback"), A.Dispatch(R).bAccepted);
    TestFalse(TEXT("failed target did not begin item use"), A.bUsing);
    R.Target.bRejectInteraction = false;
    R.Target.Kind = EFoundationTargetKind::Entity;
    R.bSneak = true;
    A.Dispatch(R);
    TestFalse(TEXT("sneaking does not generically bypass entity interaction"), A.bUsing);
    R.Target.Kind = EFoundationTargetKind::Block;
    R.bSneak = true;
    A.Dispatch(R);
    TestTrue(TEXT("sneak with hand bypasses target to item"), A.bUsing);
    R.Action = EFoundationAction::Attack;
    TestFalse(TEXT("using item consumes attack"), A.Dispatch(R).bAccepted);
    R.Action = EFoundationAction::Pick;
    TestFalse(TEXT("using item consumes pick"), A.Dispatch(R).bAccepted);
    A.Cancel();
    A.Select(8);
    R.Action = EFoundationAction::Use;
    auto Used = A.Dispatch(R);
    TestEqual(TEXT("main empty tries offhand"), Used.Hand, EFoundationHand::Off);
    A.Cancel();
    A.Offhand = 0;
    R.Target.bInteractive = false;
    TestFalse(TEXT("empty hands pass"), A.Dispatch(R).bAccepted);
    A.Scroll(-1);
    TestEqual(TEXT("wheel wraps slot9 to1"), A.Slot, 0);
    R.Action = EFoundationAction::Drop;
    R.bControl = false;
    A.Dispatch(R);
    TestEqual(TEXT("drop one"), A.Slots[0], 63);
    R.bControl = true;
    A.Dispatch(R);
    TestEqual(TEXT("fixed control drops stack"), A.Slots[0], 0);
    A.Offhand = 7;
    R.Action = EFoundationAction::Swap;
    A.Dispatch(R);
    TestEqual(TEXT("offhand swaps tokens"), A.Slots[0], 7);
    R.Action = EFoundationAction::Inventory;
    A.Dispatch(R);
    TestFalse(TEXT("inventory cancels actions"), A.bUsing || A.bBreaking);
    return true;
}
#endif
