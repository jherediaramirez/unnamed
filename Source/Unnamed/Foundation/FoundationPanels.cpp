#include "Foundation/FoundationPanels.h"
#include "UnnamedLog.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationPlayground.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Hud/HudAssets.h"
#include "Hud/HudCreativeScreen.h"
#include "Hud/HudPresenter.h"
#include "UnnamedProjectSettings.h"
#include "Foundation/FoundationPawn.h"
#include "Input/UnnamedInputSettings.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Menu/MenuBrowserHost.h"
#include "Menu/MenuVideoSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    TSharedRef<STextBlock> Label(const TCHAR* Text)
    {
        return SNew(STextBlock).Text(FText::FromString(Text));
    }

    // Saves debug-option edits immediately; editor and session-only runs keep them in memory.
    void SaveDebugOptions(UUnnamedInputSettings* Settings)
    {
        FString Error;
        Settings->SaveChecked(Error);
    }

    // The Playground's session controls; the same three exist as in-world controls west of spawn.
    TSharedRef<SWidget> MakeInteractionControls(AFoundationPlayground& Lab)
    {
        const TWeakObjectPtr<AFoundationPlayground> WeakLab = &Lab;
        const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
        List->AddSlot().AutoHeight()[SNew(STextBlock)
                .AutoWrapText(true)
                .Text(FText::FromString(
                    TEXT("PLAYGROUND LAB CONTROLS - session only, nothing is saved. Placement timing preset, "
                         "supply rule and reset (also in-world controls west of spawn).")))];
        auto Button = [&List](TFunction<FString()> Describe, TFunction<void()> Apply)
        {
            List->AddSlot().AutoHeight().Padding(0, 2)[SNew(SButton)
                    .Text_Lambda([Describe] { return FText::FromString(Describe()); })
                    .OnClicked_Lambda(
                        [Apply]
                        {
                            Apply();
                            return FReply::Handled();
                        })];
        };
        Button(
            [WeakLab]
            {
                return WeakLab.IsValid() ? TEXT("Placement preset: ") + WeakLab->GetPlacementPreset().ToString()
                                         : FString();
            },
            [WeakLab]
            {
                if (!WeakLab.IsValid()) return;
                const TArray<FFoundationPlacementPreset>& Presets = UFoundationInteractionSettings::Get().Presets;
                const int32 Current = Presets.IndexOfByPredicate([&WeakLab](const FFoundationPlacementPreset& Preset)
                    { return Preset.Name == WeakLab->GetPlacementPreset(); });
                if (Presets.Num()) WeakLab->SetPlacementPreset(Presets[(Current + 1) % Presets.Num()].Name);
            });
        Button(
            [WeakLab]
            {
                return WeakLab.IsValid() && WeakLab->IsFiniteSupply()
                    ? FString(TEXT("Supply: finite fixture (64 blocks, tool wear) - resets the lab"))
                    : FString(TEXT("Supply: infinite, no tool wear - resets the lab"));
            },
            [WeakLab]
            {
                if (WeakLab.IsValid()) WeakLab->SetFiniteSupply(!WeakLab->IsFiniteSupply());
            });
        Button([] { return FString(TEXT("Reset lab: seed blocks, loadout, targets; back to spawn")); },
            [WeakLab]
            {
                if (WeakLab.IsValid()) WeakLab->ResetLab();
            });
        return List;
    }

    // Lab controls for the player's real, session-only vitals. Values apply at once; the buttons go
    // through the same rules as play (armor, cooldown, absorption, death). Timed rules run once play resumes.
    TSharedRef<SWidget> MakeVitalsControls(AFoundationPawn& Pawn)
    {
        const TWeakObjectPtr<AFoundationPawn> WeakPawn = &Pawn;
        const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
        List->AddSlot().AutoHeight()[SNew(STextBlock)
                .AutoWrapText(true)
                .Text(FText::FromString(TEXT("PLAYER VITALS - session only; Java 26.3 rules for damage, food, air, "
                                             "falls, XP and attack recovery. "
                                             "Armor, toughness and attack speed stand in for equipment.")))];
        auto Cycle = [&List, WeakPawn](TFunction<FString(const FFoundationVitals&)> Describe,
                         TFunction<void(FFoundationVitals&)> Next)
        {
            List->AddSlot().AutoHeight().Padding(0, 2)[SNew(SButton)
                    .Text_Lambda([WeakPawn, Describe]
                        { return FText::FromString(WeakPawn.IsValid() ? Describe(WeakPawn->Vitals) : FString()); })
                    .OnClicked_Lambda(
                        [WeakPawn, Next]
                        {
                            if (WeakPawn.IsValid()) Next(WeakPawn->Vitals);
                            return FReply::Handled();
                        })];
        };
        Cycle(
            [](const FFoundationVitals& V)
            {
                static const TCHAR* Modes[] = {
                    TEXT("Survival"), TEXT("Creative"), TEXT("Adventure"), TEXT("Spectator")};
                return FString(TEXT("Player mode: ")) + Modes[int32(V.Mode)];
            },
            [](FFoundationVitals& V) { V.Mode = EFoundationPlayerMode((int32(V.Mode) + 1) % 4); });
        Cycle(
            [](const FFoundationVitals& V)
            {
                static const TCHAR* Difficulties[] = {TEXT("Peaceful"), TEXT("Easy"), TEXT("Normal"), TEXT("Hard")};
                return FString(TEXT("Difficulty: ")) + Difficulties[int32(V.Difficulty)];
            },
            [](FFoundationVitals& V) { V.Difficulty = EFoundationDifficulty((int32(V.Difficulty) + 1) % 4); });
        List->AddSlot().AutoHeight().Padding(0, 2)[SNew(SCheckBox)
                .IsChecked_Lambda(
                    [WeakPawn]
                    {
                        return WeakPawn.IsValid() && WeakPawn->Vitals.bNaturalRegeneration ? ECheckBoxState::Checked
                                                                                           : ECheckBoxState::Unchecked;
                    })
                .OnCheckStateChanged_Lambda(
                    [WeakPawn](ECheckBoxState State)
                    {
                        if (WeakPawn.IsValid())
                            WeakPawn->Vitals.bNaturalRegeneration = State == ECheckBoxState::Checked;
                    })[Label(TEXT("Natural health regeneration (game rule)"))]];
        auto Number = [&List, WeakPawn](const TCHAR* Name, float Min, float Max,
                          TFunction<float(const FFoundationVitals&)> Get,
                          TFunction<void(FFoundationVitals&, float)> Set)
        {
            List->AddSlot().AutoHeight().Padding(
                0, 2)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1)[Label(Name)] +
                SHorizontalBox::Slot().AutoWidth()[SNew(SSpinBox<float>)
                        .MinDesiredWidth(105)
                        .MinValue(Min)
                        .MaxValue(Max)
                        .Value_Lambda([WeakPawn, Get] { return WeakPawn.IsValid() ? Get(WeakPawn->Vitals) : 0.f; })
                        .OnValueChanged_Lambda(
                            [WeakPawn, Set](float Value)
                            {
                                if (WeakPawn.IsValid() && !WeakPawn->Vitals.IsDead()) Set(WeakPawn->Vitals, Value);
                            })]];
        };
        Number(
            TEXT("Health (points)"), .5f, 1024, [](const FFoundationVitals& V) { return V.Health; },
            [](FFoundationVitals& V, float Value) { V.Health = FMath::Clamp(Value, .5f, V.MaxHealth); });
        Number(
            TEXT("Maximum health"), 1, 200, [](const FFoundationVitals& V) { return V.MaxHealth; },
            [](FFoundationVitals& V, float Value)
            {
                V.MaxHealth = Value;
                V.Health = FMath::Min(V.Health, V.MaxHealth);
            });
        Number(
            TEXT("Absorption"), 0, 40, [](const FFoundationVitals& V) { return V.Absorption; },
            [](FFoundationVitals& V, float Value) { V.Absorption = Value; });
        Number(
            TEXT("Armor points (equipment stand-in)"), 0, 30, [](const FFoundationVitals& V) { return float(V.Armor); },
            [](FFoundationVitals& V, float Value) { V.Armor = FMath::RoundToInt(Value); });
        Number(
            TEXT("Armor toughness"), 0, 20, [](const FFoundationVitals& V) { return V.ArmorToughness; },
            [](FFoundationVitals& V, float Value) { V.ArmorToughness = Value; });
        Number(
            TEXT("Food"), 0, 20, [](const FFoundationVitals& V) { return float(V.Food); },
            [](FFoundationVitals& V, float Value) { V.Food = FMath::RoundToInt(Value); });
        Number(
            TEXT("Saturation (no visible meter)"), 0, 20, [](const FFoundationVitals& V) { return V.Saturation; },
            [](FFoundationVitals& V, float Value) { V.Saturation = Value; });
        Number(
            TEXT("Exhaustion"), 0, 40, [](const FFoundationVitals& V) { return V.Exhaustion; },
            [](FFoundationVitals& V, float Value) { V.Exhaustion = Value; });
        Number(
            TEXT("Air"), 0, 300, [](const FFoundationVitals& V) { return float(V.Air); },
            [](FFoundationVitals& V, float Value) { V.Air = FMath::RoundToInt(Value); });
        Number(
            TEXT("XP level"), 0, 999, [](const FFoundationVitals& V) { return float(V.Level); },
            [](FFoundationVitals& V, float Value) { V.Level = FMath::RoundToInt(Value); });
        Number(
            Pawn.HasInteraction() ? TEXT("Attack speed (the held item sets it here)")
                                  : TEXT("Attack speed (4 = empty hand)"),
            .25f, 16, [](const FFoundationVitals& V) { return V.AttackSpeed; },
            [](FFoundationVitals& V, float Value) { V.AttackSpeed = Value; });
        auto Actions = [&List, WeakPawn](const TCHAR* LeftName, TFunction<void(FFoundationVitals&)> Left,
                           const TCHAR* RightName, TFunction<void(FFoundationVitals&)> Right)
        {
            auto Button = [WeakPawn](const TCHAR* Name, TFunction<void(FFoundationVitals&)> Apply)
            {
                return SNew(SButton)
                    .Text(FText::FromString(Name))
                    .OnClicked_Lambda(
                        [WeakPawn, Apply]
                        {
                            if (WeakPawn.IsValid()) Apply(WeakPawn->Vitals);
                            return FReply::Handled();
                        });
            };
            List->AddSlot().AutoHeight().Padding(0, 2)[SNew(SHorizontalBox) +
                SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 2, 0)[Button(LeftName, Left)] +
                SHorizontalBox::Slot().FillWidth(1).Padding(2, 0, 0, 0)[Button(RightName, Right)]];
        };
        Actions(
            TEXT("Mob hit 4 (armor, difficulty)"),
            [](FFoundationVitals& V) { V.Hurt(EFoundationDamage::MobAttack, 4); }, TEXT("Damage 2 (bypasses armor)"),
            [](FFoundationVitals& V) { V.Hurt(EFoundationDamage::Generic, 2); });
        Actions(
            TEXT("Heal 4"), [](FFoundationVitals& V) { V.Heal(4); }, TEXT("Eat cooked beef (8, 12.8)"),
            [](FFoundationVitals& V) { V.Eat(8, 12.8f); });
        Actions(
            TEXT("Exhaustion +4"), [](FFoundationVitals& V) { V.AddExhaustion(4); }, TEXT("XP +10 points"),
            [](FFoundationVitals& V) { V.AddExperience(10); });
        Actions(
            TEXT("Absorption 4"), [](FFoundationVitals& V) { V.GrantAbsorption(4); }, TEXT("Kill (respawn after 1 s)"),
            [](FFoundationVitals& V) { V.Hurt(EFoundationDamage::Kill, TNumericLimits<float>::Max()); });
        List->AddSlot().AutoHeight().Padding(0, 2)[SNew(SButton)
                .Text(FText::FromString(TEXT("Restore fresh vitals (keeps position and lab settings)")))
                .OnClicked_Lambda(
                    [WeakPawn]
                    {
                        if (WeakPawn.IsValid()) WeakPawn->Vitals.Respawn();
                        return FReply::Handled();
                    })];
        return List;
    }
}

AFoundationController* UFoundationPanels::GetFoundationController() const
{
    return Cast<AFoundationController>(GetOwner());
}

void UFoundationPanels::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Root && GEngine && GEngine->GameViewport)
        GEngine->GameViewport->RemoveViewportWidgetContent(Root.ToSharedRef());
    Root.Reset();
    PreloadedCreative.Reset();
    Super::EndPlay(EndPlayReason);
}

void UFoundationPanels::Show(TSharedRef<SWidget> Content, bool bBare)
{
    if (bOpen) Close();
    bOpen = true;
    AFoundationController* Controller = GetFoundationController();
    Controller->CancelActions();
    if (UUnnamedInputSubsystem* InputSubsystem = Controller->GetInputSubsystem())
        InputSubsystem->SetGameplayInputEnabled(false);
    if (AFoundationPawn* Pawn = Controller->GetFoundationPawn()) Pawn->SetSimulationEnabled(false);
    // clang-format off
    if (bBare)
    {
        Root = SNew(SBox).HAlign(HAlign_Fill).VAlign(VAlign_Bottom)[Content];
    }
    else
    {
        Root = SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [
            SNew(SBorder).Padding(20).BorderBackgroundColor(FLinearColor(.02f, .03f, .04f, 1.f))
            [
                Content
            ]
        ];
    }
    // clang-format on
    GEngine->GameViewport->AddViewportWidgetContent(Root.ToSharedRef(), 300);
    Controller->bShowMouseCursor = true;
    Controller->SetInputMode(FInputModeUIOnly());
    Controller->GetHud()->LogEvent(TEXT("blocking fixture/options context"));
}

void UFoundationPanels::Close()
{
    if (Root && GEngine && GEngine->GameViewport)
        GEngine->GameViewport->RemoveViewportWidgetContent(Root.ToSharedRef());
    Root.Reset();
    Creative.Reset();
    bOpen = bTextOpen = bInventoryOpen = false;
    TextBox.Reset();
    AFoundationController* Controller = GetFoundationController();
    if (Controller && !Controller->GetMenuHost()->IsOpen()) Controller->ResumeFoundation();
}

void UFoundationPanels::OpenDebugOptions()
{
    if (bOpen)
    {
        Close();
        return;
    }
    AFoundationController* Controller = GetFoundationController();
    UUnnamedInputSubsystem* InputSubsystem = Controller->GetInputSubsystem();
    UUnnamedInputSettings* Settings = InputSubsystem ? InputSubsystem->GetSettings() : nullptr;
    if (!Settings) return;
    const TWeakObjectPtr<UUnnamedInputSettings> WeakSettings = Settings;
    const TWeakObjectPtr<UFoundationPanels> WeakThis = this;
    const TArray<FString> SectionLabels = {TEXT("Coordinates / camera / pose"), TEXT("Movement / eligibility"),
        TEXT("Input contexts / bindings"), TEXT("Hotbar / hands / targeting"), TEXT("Performance / recent events")};

    // clang-format off
    const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    List->AddSlot().AutoHeight()[Label(TEXT("Debug Options — independent of menu GUI scale"))];
    List->AddSlot().AutoHeight().Padding(0, 10)
    [
        SNew(SSpinBox<float>).MinValue(.5f).MaxValue(2.f).Delta(.1f).Value(Settings->DebugTextScale)
            .OnValueChanged_Lambda([WeakSettings](float Value)
            {
                if (!WeakSettings.IsValid()) return;
                WeakSettings->DebugTextScale = Value;
                SaveDebugOptions(WeakSettings.Get());
            })
    ];
    for (int32 Section = 0; Section < SectionLabels.Num(); ++Section)
    {
        const int32 Bit = 1 << Section;
        List->AddSlot().AutoHeight().Padding(0, 4)
        [
            SNew(SCheckBox)
                .IsChecked((Settings->DebugSections & Bit) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                .OnCheckStateChanged_Lambda([WeakSettings, Bit](ECheckBoxState State)
                {
                    if (!WeakSettings.IsValid()) return;
                    if (State == ECheckBoxState::Checked)
                        WeakSettings->DebugSections |= Bit;
                    else
                        WeakSettings->DebugSections &= ~Bit;
                    SaveDebugOptions(WeakSettings.Get());
                })
            [
                SNew(STextBlock).Text(FText::FromString(SectionLabels[Section]))
            ]
        ];
    }
    List->AddSlot().AutoHeight()
    [
        SNew(SCheckBox)
            .IsChecked(Settings->bDebugUnrealCoordinates ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([WeakSettings](ECheckBoxState State)
            {
                if (!WeakSettings.IsValid()) return;
                WeakSettings->bDebugUnrealCoordinates = State == ECheckBoxState::Checked;
                SaveDebugOptions(WeakSettings.Get());
            })
        [
            Label(TEXT("Show Unreal actor/camera coordinates (cm)"))
        ]
    ];
    List->AddSlot().AutoHeight().Padding(0, 8)
    [
        SNew(SButton).Text(FText::FromString(TEXT("Copy diagnostic snapshot"))).OnClicked_Lambda([WeakThis]
        {
            if (WeakThis.IsValid()) WeakThis->GetFoundationController()->GetHud()->CopySnapshot();
            return FReply::Handled();
        })
    ];
    List->AddSlot().AutoHeight()
    [
        SNew(SButton).Text(FText::FromString(TEXT("Done"))).OnClicked_Lambda([WeakThis]
        {
            if (WeakThis.IsValid()) WeakThis->Close();
            return FReply::Handled();
        })
    ];
    if (AFoundationPawn* Pawn = Controller->GetFoundationPawn())
    {
        if (auto* Playground = Cast<AFoundationPlayground>(Pawn->GetLab()))
            List->AddSlot().AutoHeight().Padding(0, 12)[MakeInteractionControls(*Playground)];
        List->AddSlot().AutoHeight().Padding(0, 12)[MakeVitalsControls(*Pawn)];
    }
    if (Controller->GetHud()->GetPresenter())
        List->AddSlot().AutoHeight().Padding(0, 12)[Controller->GetHud()->GetPresenter()->CreatePreviewControls()];
    // clang-format on
    Show(SNew(SBox).MaxDesiredHeight(600)[SNew(SScrollBox) + SScrollBox::Slot()[List]]);
    Controller->GetHud()->bOverlay = true;
}

void UFoundationPanels::OpenText(bool bCommand)
{
    const TWeakObjectPtr<UFoundationPanels> WeakThis = this;
    TSharedPtr<SEditableTextBox> Edit;
    // clang-format off
    const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    List->AddSlot().AutoHeight()[Label(TEXT("Local chat/command fixture — Enter records text, Escape cancels"))];
    List->AddSlot().AutoHeight().Padding(0, 10)
    [
        SAssignNew(Edit, SEditableTextBox).MinDesiredWidth(600)
            .Text(FText::FromString(bCommand ? TEXT("/") : TEXT("")))
            .OnTextCommitted_Lambda([WeakThis](const FText& Text, ETextCommit::Type Type)
            {
                if (Type != ETextCommit::OnEnter || !WeakThis.IsValid()) return;
                // foundation.Chat.Commands 1: the controller runs commands and shows the chat (step 15). 0: recorded
                // locally only, no chat network and no command execution.
                if (FoundationChat::AreCommandsEnabled())
                {
                    WeakThis->GetFoundationController()->SubmitChat(Text.ToString());
                }
                else
                {
                    WeakThis->GetFoundationController()->GetHud()->LogEvent(
                        TEXT("local text (no network/command execution): ") + Text.ToString().Left(256));
                }
                WeakThis->Close();
            })
    ];
    // clang-format on
    if (FoundationChat::AreCommandsEnabled())
    {
        // Vanilla's input bar is the HUD's (FHudSnapshot::ChatInput); the box only takes the keys, unseen.
        List->RemoveSlot(Edit.ToSharedRef());
        Edit->SetRenderOpacity(0);
        Show(Edit.ToSharedRef(), true);
    }
    else
        Show(List);
    bTextOpen = true;
    TextBox = Edit;
    FSlateApplication::Get().SetKeyboardFocus(Edit);
    // Typing goes after a prefilled "/", where the HUD draws its caret.
    Edit->GoTo(ETextLocation::EndOfDocument);
}

FString UFoundationPanels::GetTypedText() const
{
    return bTextOpen && TextBox ? TextBox->GetText().ToString() : FString();
}

bool UFoundationPanels::IsTypingInInventory() const
{
    return Creative && Creative->GetModel().IsSearch();
}

namespace
{
    // Block.Mineable.Pickaxe -> "mineable/pickaxe": the vanilla name BlockImportData.tag_name mapped from, for
    // the creative search's "#" queries.
    FString VanillaTagName(const FGameplayTag& Tag)
    {
        FString Path = Tag.ToString();
        if (!Path.RemoveFromStart(TEXT("Block."))) return FString();
        TArray<FString> Segments;
        Path.ParseIntoArray(Segments, TEXT("."));
        auto Snake = [](const FString& Pascal)
        {
            FString Out;
            for (int32 Index = 0; Index < Pascal.Len(); ++Index)
            {
                if (Index > 0 && FChar::IsUpper(Pascal[Index])) Out += TEXT('_');
                Out += FChar::ToLower(Pascal[Index]);
            }
            return Out;
        };
        if (Segments.Num() == 2 && (Segments[0] == TEXT("NeedsTool") || Segments[0] == TEXT("IncorrectForTool")))
            return (Segments[0] == TEXT("NeedsTool") ? TEXT("needs_") : TEXT("incorrect_for_")) + Snake(Segments[1]) +
                TEXT("_tool");
        for (FString& Segment : Segments)
            Segment = Snake(Segment);
        return FString::Join(Segments, TEXT("/"));
    }
}

bool UFoundationPanels::PreloadCreative()
{
    const AFoundationController* Controller = GetFoundationController();
    AFoundationPawn* Pawn = Controller ? Controller->GetFoundationPawn() : nullptr;
    AFoundationLab* Lab = Pawn ? Pawn->GetLab() : nullptr;
    // No game viewport (a headless run): nothing will ever open.
    if (!FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport) return true;
    if (!Lab || !Lab->GetAuthority()) return false;
    if (PreloadedCreative.IsValid() || !Lab->GetAuthority()->Options.bCreativeInventory) return true;
    const double Start = FPlatformTime::Seconds();
    PreloadedCreative = MakeCreative(*Lab);
    PreloadedPawn = Pawn;
    PreloadedAuthority = Lab->GetAuthority();
    UE_LOG(LogUnnamedFoundation, Display, TEXT("Creative inventory preloaded in %.1f ms"),
        (FPlatformTime::Seconds() - Start) * 1000);
    return true;
}

TSharedRef<SHudCreativeScreen> UFoundationPanels::MakeCreative(AFoundationLab& Lab)
{
    AFoundationPawn* Pawn = GetFoundationController()->GetFoundationPawn();
    FFoundationAuthority& Authority = *Lab.GetAuthority();
    const TSharedRef<const FFoundationBlockRegistry> Registry = Authority.World.GetRegistryRef();
    // Everything the player could hold: names, stack sizes, max damage, the 26.3 items' tooltip lines (DA_Items,
    // with the player's base values) and the placed block's tags for "#" search.
    const UFoundationItemAssets* ItemAssets = UFoundationItemAssets::LoadDefault();
    FHudCreativeItems Items;
    for (const FFoundationItemDefinition& Item : Authority.Rules.Items)
    {
        FHudCreativeItem& Entry = Items.Add(Item.Id);
        Entry.Name = Item.DisplayName;
        Entry.MaxStack = Item.MaxStack;
        Entry.MaxDamage = Item.MaxDurability;
        Entry.Equipment = Item.Equipment;
        if (const FFoundationItemAsset* Asset = ItemAssets ? ItemAssets->FindItem(Item.Id) : nullptr)
        {
            Entry.Details = HudTooltip::GetModifierLines(Asset->Modifiers, ItemAssets->Lang, true);
            if (!Asset->Textures.IsEmpty()) Entry.Sprite = Asset->Textures[0].ToString();
        }
        if (const FFoundationBlockType* Block = Registry->FindBlock(Item.PlacesBlock))
            for (const FGameplayTag& Tag : Block->Tags)
                Entry.Tags.Add(VanillaTagName(Tag));
    }
    const TWeakObjectPtr<AFoundationLab> WeakLab = &Lab;
    const TWeakObjectPtr<AFoundationPawn> WeakPawn = Pawn;
    const TWeakObjectPtr<UFoundationPanels> WeakThis = this;
    // clang-format off
    return SNew(SHudCreativeScreen)
        .Registry(Registry)
        .HudAssets(LoadObject<UHudAssets>(nullptr, *UUnnamedProjectSettings::GetHudRegistryPath()))
        // Options > GUI Scale, as the HUD reads it (HudPresenter): the screen follows a change at once.
        .RequestedGuiScale_Lambda([WeakController = TWeakObjectPtr<AFoundationController>(GetFoundationController())]
        {
            const AFoundationController* Controller = WeakController.Get();
            return Controller && Controller->GetMenuHost()
                ? Controller->GetMenuHost()->GetVideoSettings()->GetGuiScale()
                : 0;
        })
        .Items(MoveTemp(Items))
        .GetInventory([WeakLab, WeakPawn]
        {
            TArray<FHudCreativeStack> Stacks;
            Stacks.SetNum(FFoundationInventory::Slots);
            if (!WeakLab.IsValid() || !WeakPawn.IsValid() || !WeakLab->GetAuthority()) return Stacks;
            const FFoundationAuthority& A = *WeakLab->GetAuthority();
            const FFoundationPlayer* Player = A.FindPlayer(WeakPawn->Interaction.PlayerId);
            if (!Player) return Stacks;
            auto Stack = [&A](const FFoundationItemInstance& Item)
            {
                return Item.IsEmpty() ? FHudCreativeStack()
                                      : FHudCreativeStack{A.Rules.GetItem(Item.Item).Id, Item.Count, Item.Damage};
            };
            // Every slot by its Inventory number; with the lab inventory, GetSlot leaves main and armor empty.
            for (int32 Slot = 0; Slot < FFoundationInventory::Slots; ++Slot)
                if (const FFoundationItemInstance* Item = Player->Inventory.GetSlot(Slot)) Stacks[Slot] = Stack(*Item);
            return Stacks;
        })
        .OnWrite([WeakLab, WeakPawn](const FHudCreativeWrite& Write)
        {
            if (WeakLab.IsValid() && WeakPawn.IsValid() && WeakLab->GetAuthority())
                WeakPawn->Interaction.SetCreativeSlot(*WeakLab->GetAuthority(), WeakPawn->Vitals, Write.Slot,
                    Write.Stack.Item, Write.Stack.Count, Write.Stack.Damage);
        })
        .OnClose([WeakThis] { if (WeakThis.IsValid()) WeakThis->Close(); });
    // clang-format on
}

void UFoundationPanels::OpenCreative(AFoundationLab& Lab)
{
    AFoundationController* Controller = GetFoundationController();
    AFoundationPawn* Pawn = Controller->GetFoundationPawn();
    // The preloaded screen when it was built for this player (foundation.Panels.Preload), else a new one.
    Creative = PreloadedCreative.IsValid() && PreloadedPawn.Get() == Pawn && PreloadedAuthority == Lab.GetAuthority()
        ? PreloadedCreative.ToSharedRef()
        : MakeCreative(Lab);
    PreloadedCreative.Reset();
    bOpen = true;
    Controller->CancelActions();
    if (UUnnamedInputSubsystem* InputSubsystem = Controller->GetInputSubsystem())
        InputSubsystem->SetGameplayInputEnabled(false);
    if (Pawn) Pawn->SetSimulationEnabled(false);
    Root = Creative;
    GEngine->GameViewport->AddViewportWidgetContent(Root.ToSharedRef(), 300);
    Controller->bShowMouseCursor = true;
    FInputModeUIOnly Mode;
    Mode.SetWidgetToFocus(Creative);
    Controller->SetInputMode(Mode);
    FSlateApplication::Get().SetKeyboardFocus(Creative);
    Controller->GetHud()->LogEvent(TEXT("creative inventory"));
    bInventoryOpen = true;
}

void UFoundationPanels::OpenInventory()
{
    FFoundationActionRequest Request;
    Request.Action = EFoundationAction::Inventory;
    GetFoundationController()->Actions.Dispatch(Request);
    if (bOpen) Close();
    if (AFoundationPawn* Pawn = GetFoundationController()->GetFoundationPawn())
        // Any lab whose authority allows creative writes: the Playground, a Creative generated world.
        if (AFoundationLab* Lab = Pawn->GetLab();
            Lab && Lab->GetAuthority() && Lab->GetAuthority()->Options.bCreativeInventory)
        {
            OpenCreative(*Lab);
            return;
        }
    const TWeakObjectPtr<UFoundationPanels> WeakThis = this;
    // clang-format off
    const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    const AFoundationPawn* Pawn = GetFoundationController()->GetFoundationPawn();
    List->AddSlot().AutoHeight()
    [
        Label(Pawn && Pawn->HasInteraction()
            ? TEXT("LAB INVENTORY\nThe hotbar (1-9, wheel) and offhand (F) hold the practice items.\n"
                   "No inventory screen, crafting or dropped items in this lab.\nEscape returns to play.")
            : TEXT("INVENTORY INPUT FIXTURE\nNo items, crafting or survival inventory.\n"
                   "Token counts exercise drop/swap/use contracts.\nEscape returns to play."))
    ];
    List->AddSlot().AutoHeight()
    [
        SNew(SButton).Text(FText::FromString(TEXT("Close"))).OnClicked_Lambda([WeakThis]
        {
            if (WeakThis.IsValid()) WeakThis->Close();
            return FReply::Handled();
        })
    ];
    // clang-format on
    Show(List);
    bInventoryOpen = true;
}
