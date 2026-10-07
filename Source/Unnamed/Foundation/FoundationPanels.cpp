#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationPawn.h"
#include "Input/UnnamedInputSettings.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Menu/MenuBrowserHost.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
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
    Super::EndPlay(EndPlayReason);
}

void UFoundationPanels::Show(TSharedRef<SWidget> Content)
{
    if (bOpen) Close();
    bOpen = true;
    AFoundationController* Controller = GetFoundationController();
    Controller->CancelActions();
    if (UUnnamedInputSubsystem* InputSubsystem = Controller->GetInputSubsystem())
        InputSubsystem->SetGameplayInputEnabled(false);
    if (AFoundationPawn* Pawn = Controller->GetFoundationPawn()) Pawn->SetSimulationEnabled(false);
    // clang-format off
    Root = SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
    [
        SNew(SBorder).Padding(20).BorderBackgroundColor(FLinearColor(.02f, .03f, .04f, 1.f))
        [
            Content
        ]
    ];
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
    bOpen = bTextOpen = bInventoryOpen = false;
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
    // clang-format on
    Show(List);
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
                // Recorded locally only: no chat network and no command execution.
                WeakThis->GetFoundationController()->GetHud()->LogEvent(
                    TEXT("local text (no network/command execution): ") + Text.ToString().Left(256));
                WeakThis->Close();
            })
    ];
    // clang-format on
    Show(List);
    bTextOpen = true;
    FSlateApplication::Get().SetKeyboardFocus(Edit);
}

void UFoundationPanels::OpenInventory()
{
    FFoundationActionRequest Request;
    Request.Action = EFoundationAction::Inventory;
    GetFoundationController()->Actions.Dispatch(Request);
    const TWeakObjectPtr<UFoundationPanels> WeakThis = this;
    // clang-format off
    const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    List->AddSlot().AutoHeight()
    [
        Label(TEXT("INVENTORY INPUT FIXTURE\nNo items, crafting or survival inventory.\n"
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
