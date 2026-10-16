#include "Hud/HudPresenter.h"
#include "Hud/HudAssets.h"
#include "Hud/HudCreative.h"
#include "Hud/HudDrawList.h"
#include "Hud/HudView.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationDeath.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationPawn.h"
#include "Menu/MenuBrowserHost.h"
#include "Menu/MenuPreferences.h"
#include "Menu/MenuVideoSettings.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"
#include "Algo/Compare.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

static TAutoConsoleVariable<int32> CVarHurtScreenTint(TEXT("foundation.Hurt.ScreenTint"), 0,
    TEXT("1: a full-screen 0xB2FF0000 tint while hurt or dead (a stand-in; vanilla tints only the hurt body). 0: "
         "none."));

TSharedRef<SWidget> UHudPresenter::CreateView()
{
    Assets = LoadObject<UHudAssets>(nullptr, *UUnnamedProjectSettings::GetHudRegistryPath());
    View = SNew(SHudView);
    if (!Assets)
    {
        ++MissingTextures;
        UE_LOG(LogUnnamedHud, Error, TEXT("Missing HUD registry; run ImportHud.py and cook the HUD directory"));
        return View.ToSharedRef();
    }
    TMap<FName, FSlateBrush> Brushes;
    for (const FHudSpriteAsset& Entry : Assets->Sprites)
    {
        if (!Entry.Id.ToString().StartsWith(TEXT("gui/sprites/hud/")) && Entry.Id != TEXT("font/ascii") &&
            Entry.Id != TEXT("block/fire_1"))
            continue;
        UTexture2D* Texture = Entry.Texture.LoadSynchronous();
        FIntPoint Size = Texture ? FIntPoint(Texture->GetSizeX(), Texture->GetSizeY()) : FIntPoint::ZeroValue;
#if WITH_EDITORONLY_DATA
        if (Texture) Size = {int32(Texture->Source.GetSizeX()), int32(Texture->Source.GetSizeY())};
#endif
        if (!Texture || Size != Entry.NativeSize)
        {
            ++MissingTextures;
            UE_LOG(LogUnnamedHud, Error, TEXT("Missing or resized HUD texture: %s"), *Entry.Id.ToString());
            continue;
        }
        Textures.Add(Texture);
        FSlateBrush Brush;
        Brush.SetResourceObject(Texture);
        Brush.ImageSize = FVector2D(Entry.NativeSize);
        Brush.DrawAs = ESlateBrushDrawType::Image;
        Brushes.Add(Entry.Id, Brush);
    }
    // The items' GUI sprites (ImportItems.py, ItemRoot/Gui), by sprite id ("item/iron_pickaxe").
    if (const UFoundationItemAssets* Items = UFoundationItemAssets::LoadDefault())
        for (const FFoundationItemAsset& Item : Items->Items)
            // The first texture, and the second (the leather pieces' overlay layer).
            for (int32 Layer = 0; Layer < FMath::Min(Item.Textures.Num(), 2); ++Layer)
            {
                const FName Sprite = Item.Textures[Layer];
                if (Brushes.Contains(Sprite)) continue;
                UTexture2D* Texture = Cast<UTexture2D>(
                    UUnnamedProjectSettings::GetItemAssetPath(TEXT("Gui/") + Sprite.ToString()).TryLoad());
                if (!Texture)
                {
                    // Only the first layer is drawn for every item; a missing overlay just draws nothing.
                    if (Layer > 0) continue;
                    ++MissingTextures;
                    UE_LOG(LogUnnamedHud, Error, TEXT("Missing item sprite: %s"), *Sprite.ToString());
                    continue;
                }
                ItemSprites.Add(Texture);
                FSlateBrush Brush;
                Brush.SetResourceObject(Texture);
                Brush.ImageSize = FVector2D(16, 16);
                Brush.DrawAs = ESlateBrushDrawType::Image;
                Brushes.Add(Sprite, Brush);
            }
    // The camera overlay of a worn head piece (ImportContainerGui.py, GuiRoot/Container/misc/pumpkinblur).
    {
        const FSoftObjectPath Path =
            UUnnamedProjectSettings::GetGuiAssetPath(TEXT("Container/") + FString(TEXT("misc/pumpkinblur")));
        UTexture2D* Texture =
            FPackageName::DoesPackageExist(Path.GetLongPackageName()) ? Cast<UTexture2D>(Path.TryLoad()) : nullptr;
        if (Texture)
        {
            CameraOverlays.Add(Texture);
            FSlateBrush Brush;
            Brush.SetResourceObject(Texture);
            Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
            Brush.DrawAs = ESlateBrushDrawType::Image;
            Brushes.Add(TEXT("misc/pumpkinblur"), Brush);
        }
        else
            UE_LOG(LogUnnamedHud, Warning, TEXT("Missing GUI sprite misc/pumpkinblur; run ImportContainerGui.py"));
    }
    // The death screen's vanilla buttons (step 11a), from ImportContainerGui.py (GuiRoot/Container).
    for (const TCHAR* Id : {TEXT("gui/sprites/widget/button"), TEXT("gui/sprites/widget/button_highlighted"),
             TEXT("gui/sprites/widget/button_disabled")})
    {
        const FSoftObjectPath Path = UUnnamedProjectSettings::GetGuiAssetPath(FString(TEXT("Container/")) + Id);
        UTexture2D* Texture =
            FPackageName::DoesPackageExist(Path.GetLongPackageName()) ? Cast<UTexture2D>(Path.TryLoad()) : nullptr;
        if (!Texture)
        {
            UE_LOG(LogUnnamedHud, Warning, TEXT("Missing GUI sprite %s; run ImportContainerGui.py"), Id);
            continue;
        }
        Textures.Add(Texture);
        FSlateBrush Brush;
        Brush.SetResourceObject(Texture);
        Brush.ImageSize = FVector2D(200, 20);
        Brush.DrawAs = ESlateBrushDrawType::Image;
        Brushes.Add(Id, Brush);
    }
    View->SetBrushes(MoveTemp(Brushes));
    UE_LOG(LogUnnamedHud, Display,
        TEXT("HUD_READY registry=%d textures=%d missing=%d glyphs=%d item_sprites=%d; resources from player vitals, "
             "items PREVIEW"),
        Assets->Sprites.Num(), Textures.Num(), MissingTextures, Assets->Glyphs.Num(), ItemSprites.Num());
    return View.ToSharedRef();
}

void UHudPresenter::ApplyVitals(FHudSnapshot& S, const FFoundationVitals& V, const FFoundationTarget& Target)
{
    S.Resources = EHudDataSource::Authoritative;
    switch (V.Mode)
    {
    case EFoundationPlayerMode::Creative:
        S.Mode = EHudMode::Creative;
        break;
    case EFoundationPlayerMode::Adventure:
        S.Mode = EHudMode::Adventure;
        break;
    case EFoundationPlayerMode::Spectator:
        S.Mode = EHudMode::Spectator;
        break;
    default:
        S.Mode = EHudMode::Survival;
        break;
    }
    S.Health = V.Health;
    S.MaxHealth = V.MaxHealth;
    S.Absorption = V.Absorption;
    // The armor attribute: the lab's base plus the worn pieces (survival step 19a).
    S.Armor = V.GetArmor();
    S.Food = V.Food;
    S.Saturation = V.Saturation;
    S.Air = V.Air;
    S.MaxAir = V.MaxAir;
    S.bUnderwater = V.bEyesInWater;
    S.Level = V.Level;
    S.Experience = V.Progress;
    S.AttackStrength = V.GetAttackStrength();
    S.AttackDelay = V.GetAttackDelay();
    S.bDamageCooldown = V.DamageCooldown > 0;
    // Step 11b feel (foundation.Death.Screen 1): the red tint while hurtTime > 0 (DamageCooldown above 10) or dead, the
    // fire overlay while burning outside Creative/Spectator.
    const bool bFeel = FoundationDeath::IsScreenEnabled();
    // The 0xB2FF0000 overlay [survival.r6.hud_rules] is vanilla's entity damage overlay, drawn on the hurt body; in
    // first person without hands vanilla shows no red on screen. foundation.Hurt.ScreenTint 1 keeps the full-screen
    // stand-in one switch away (a look call; default off = vanilla).
    S.bDamageTint =
        bFeel && CVarHurtScreenTint.GetValueOnGameThread() != 0 && (V.IsInHurtWindow() || V.DeathTicks >= 0);
    S.bOnFire = bFeel && V.IsOnFire() && S.CanHurt();
    // The eye ray's nearest fixture: an entity in reach is the crosshair entity; the lab's entities are alive.
    // An interactive target in reach stands in for a block or entity with a menu (spectator crosshair).
    const bool bEntity = Target.Kind == EFoundationTargetKind::Entity && Target.bInRange;
    S.bLivingTarget = S.bTargetInRange = bEntity;
    S.bTargetAlive = bEntity && Target.bAlive;
    S.bSpectatorTarget = Target.bInRange && Target.bInteractive;
    S.bHardcore = S.bPoison = S.bWither = S.bFrozen = S.bHunger = S.bRegeneration = false;
}

void UHudPresenter::ApplyInventory(FHudSnapshot& S, const FFoundationInventory& Inventory,
    const FFoundationRules& Rules, const FFoundationBlockRegistry* Blocks)
{
    S.Items = EHudDataSource::Authoritative;
    S.SelectedSlot = Inventory.Selected;
    auto View = [&Inventory, &Rules, Blocks](const FFoundationItemInstance& Stack, int32& OutCount)
    {
        FHudItemView Out;
        OutCount = Stack.IsEmpty() ? 0 : Stack.Count;
        if (Stack.IsEmpty()) return Out;
        const FFoundationItemDefinition& Item = Rules.GetItem(Stack.Item);
        // As vanilla's GUI: blocks as their 3D icon, items as their flat sprite.
        Out.Icon = Item.Icon == TEXT("block") ? EHudItemIcon::Block
            : !Item.Sprite.IsNone()           ? EHudItemIcon::Sprite
                                              : EHudItemIcon::Token;
        Out.Sprite = Out.Icon == EHudItemIcon::Sprite ? Item.Sprite : NAME_None;
        if (Out.Icon == EHudItemIcon::Sprite)
        {
            Out.SpriteOverlay = Item.SpriteOverlay;
            Out.SpriteTint = FLinearColor(Item.SpriteTint);
        }
        Out.Tint = Item.IconColor;
        if (Out.Icon == EHudItemIcon::Block && Blocks && Blocks->FindBlock(Item.PlacesBlock))
        {
            Out.Block = Item.PlacesBlock;
            Out.bAnimatedBlock = HudBlockIcon::IsAnimated(*Blocks, Item.PlacesBlock);
        }
        Out.Damage = Stack.Damage;
        Out.MaxDamage = Item.MaxDurability;
        // Unlimited supply shows a sign instead of a count that never changes.
        Out.bInfinite = Inventory.bInfiniteSupply && Item.CanPlace();
        return Out;
    };
    S.Counts.SetNum(FFoundationInventory::HotbarSlots);
    S.ItemViews.SetNum(FFoundationInventory::HotbarSlots);
    for (int32 Slot = 0; Slot < FFoundationInventory::HotbarSlots; ++Slot)
        S.ItemViews[Slot] = View(Inventory.Hotbar[Slot], S.Counts[Slot]);
    S.OffhandView = View(Inventory.Offhand, S.OffhandCount);
    const FFoundationItemInstance& Held = Inventory.GetHand(EFoundationHand::Main);
    S.ItemName = Held.IsEmpty() ? FString() : Rules.GetItem(Held.Item).DisplayName;
    // The head slot's camera overlay (the pumpkin blur); only a piece made for the head, in the head slot, shows it.
    S.CameraOverlay = NAME_None;
    constexpr int32 HeadIndex = 3;
    // With foundation.Inventory.Full 0 the armor slots are off, and so is their overlay (Codex review 31, finding 2).
    if (Inventory.bFull && Inventory.Armor.IsValidIndex(HeadIndex) && !Inventory.Armor[HeadIndex].IsEmpty())
        if (const FFoundationItemDefinition& Worn = Rules.GetItem(Inventory.Armor[HeadIndex].Item);
            Worn.Equipment == EFoundationEquipmentSlot::Head)
            S.CameraOverlay = Worn.CameraOverlay;
}

void UHudPresenter::ResetPreview()
{
    Preview.Reset();
    Presentation = FHudPresentation{};
    LastSampleTick = -1;
}
int32 UHudPresenter::GetDrawCount() const
{
    return View ? View->GetDrawCount() : 0;
}

void UHudPresenter::Refresh(AFoundationController& Controller, double Now)
{
    if (!View || !Assets) return;
    const double Delta = LastTime > 0 ? FMath::Clamp(Now - LastTime, 0., .25) : 0;
    LastTime = Now;
    const bool bGameplay = Controller.IsGameplay();
    if (bGameplay) Accumulator += Delta;
    while (Accumulator >= .05)
    {
        Accumulator -= .05;
        ++HudTick;
        Preview.Step();
    }
    FIntPoint Viewport;
    Controller.GetViewportSize(Viewport.X, Viewport.Y);
    // The chat (step 15): the HUD stays up under the open chat, which shows every message.
    const bool bChatOn = FoundationChat::AreCommandsEnabled();
    const bool bChatOpen = bChatOn && Controller.GetPanels()->IsTextOpen();
    // The death screen (step 11a): the HUD stays drawn under it, its buttons enable 20 ticks after death and the
    // hovered one follows the cursor (GUI pixels from the controller). The HUD's tick stops outside gameplay, so a
    // change here redraws by itself.
    const AFoundationPawn* DeathPawn = Controller.GetFoundationPawn();
    const bool bDeath = DeathPawn && DeathPawn->IsDeathScreenActive();
    const bool bDeathEnabled = bDeath && FoundationDeath::AreButtonsEnabled(DeathPawn->GetDeathTicks());
    const EHudDeathButton DeathHover = bDeath ? Controller.GetDeathHover() : EHudDeathButton::None;
    const bool bDeathChanged = bDeath != Snapshot.bDeathScreen || bDeathEnabled != Snapshot.bDeathButtonsEnabled ||
        DeathHover != Snapshot.DeathHover;
    // F1 hides the HUD, but an open chat or the death screen (screens) still draws: the chat's input line takes the
    // typing; only the chat draws then.
    const bool bHideHud = Controller.GetHud()->bHideHud;
    const bool bVisible = bChatOpen || bDeath || (bGameplay && !bHideHud);
    const bool bVisibilityChanged = Snapshot.bVisible != bVisible || Snapshot.bHideHud != bHideHud;
    // The lines are rebuilt only when the chat changes, opens or closes, or a closed chat's line fades (per 1/20 s).
    const FFoundationChat& Chat = Controller.GetChat();
    const int64 ChatStep = bChatOn && bVisible && !bChatOpen && (Chat.HasRecent(Now) || !ChatLines.IsEmpty())
        ? FMath::FloorToInt64(Now * 20)
        : -1;
    const FString ChatInput = bChatOpen ? Controller.GetPanels()->GetTypedText() : FString();
    // The caret [survival.r4.caret]: blinks 300 ms on / 300 ms off, counted from the chat opening (PROVISIONAL: vanilla
    // may restart it on an edit; unconfirmed). The panels' caret index; a change of either redraws.
    if (bChatOpen && !Snapshot.bChatOpen) ChatOpenedAt = Now;
    const int32 CaretIndex = bChatOpen ? FMath::Clamp(Controller.GetPanels()->GetTypedCaret(), 0, ChatInput.Len()) : 0;
    const bool bCaretVisible =
        !bChatOpen || FMath::FloorToInt64((Now - ChatOpenedAt) * 1000 / HudDrawList::CaretBlinkMs) % 2 == 0;
    // The command suggestions over the input bar (vanilla's CommandSuggestions).
    const UFoundationPanels::FChatSuggestions Suggestions =
        bChatOpen ? Controller.GetPanels()->GetChatSuggestions() : UFoundationPanels::FChatSuggestions();
    const bool bSuggestionsChanged = Suggestions.Selected != Snapshot.ChatSuggestionSelected ||
        Suggestions.Start != Snapshot.ChatSuggestionStart ||
        !Suggestions.Usage.Equals(Snapshot.ChatUsage, ESearchCase::CaseSensitive) ||
        !Suggestions.Inline.Equals(Snapshot.ChatInlineSuggestion, ESearchCase::CaseSensitive) ||
        Suggestions.Items.Num() != Snapshot.ChatSuggestions.Num() ||
        !Algo::Compare(Suggestions.Items, Snapshot.ChatSuggestions,
            [](const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); });
    const bool bChatChanged = bChatOpen != Snapshot.bChatOpen || Chat.GetRevision() != ChatRevision ||
        ChatStep != LastChatStep || !ChatInput.Equals(Snapshot.ChatInput, ESearchCase::CaseSensitive) ||
        CaretIndex != Snapshot.CaretIndex || bCaretVisible != Snapshot.bCaretVisible || bSuggestionsChanged;
    if (bChatChanged)
    {
        ChatLines.Reset();
        if (bChatOn && bVisible) ChatLines = Chat.GetVisible(Now, bChatOpen);
        ChatRevision = Chat.GetRevision();
        LastChatStep = ChatStep;
    }
    if (HudTick == LastSampleTick && Viewport == LastViewport && !bVisibilityChanged && !bChatChanged && !bDeathChanged)
        return;
    LastSampleTick = HudTick;
    LastViewport = Viewport;

    const AFoundationPawn* Pawn = Controller.GetFoundationPawn();
    if (bPreviewEnabled || !Pawn)
        Snapshot = Preview.Values;
    else
    {
        Snapshot = FHudSnapshot{};
        ApplyVitals(Snapshot, Pawn->Vitals, Pawn->TraceTarget());
    }
    // The fire overlay and the screen tint are camera effects, not HUD: they still draw under F1 (Build gates the
    // HUD itself on bHideHud).
    Snapshot.bVisible = bVisible || (bGameplay && (Snapshot.bOnFire || Snapshot.bDamageTint));
    Snapshot.bHideHud = bHideHud;
    Snapshot.bDeathScreen = bDeath;
    Snapshot.bDeathButtonsEnabled = bDeathEnabled;
    Snapshot.DeathHover = DeathHover;
    Snapshot.DeathMessage = bDeath ? DeathPawn->GetDeathMessage() : FString();
    Snapshot.DeathScore = bDeath ? DeathPawn->GetDeathScore() : 0;
    Snapshot.Chat = ChatLines;
    Snapshot.ChatInput = ChatInput;
    Snapshot.ChatSuggestions = Suggestions.Items;
    Snapshot.ChatSuggestionSelected = Suggestions.Selected;
    Snapshot.ChatSuggestionStart = Suggestions.Start;
    Snapshot.ChatUsage = Suggestions.Usage;
    Snapshot.ChatInlineSuggestion = Suggestions.Inline;
    Snapshot.CaretIndex = CaretIndex;
    Snapshot.bCaretVisible = bCaretVisible;
    Snapshot.FireFrame = Snapshot.bOnFire ? int32(HudTick % HudDrawList::FireFrames) : 0;
    Snapshot.bChatOpen = bChatOpen;
    Snapshot.bFirstPerson = Pawn && Pawn->GetPerspective() == 0;
    const FFoundationAuthority* Authority = Pawn ? Pawn->GetAuthority() : nullptr;
    const FFoundationPlayer* Player = Authority ? Authority->FindPlayer(Pawn->Interaction.PlayerId) : nullptr;
    if (Player)
        ApplyInventory(Snapshot, Player->Inventory, Authority->Rules, &Authority->World.GetRegistry());
    else
    {
        Snapshot.SelectedSlot = Controller.Actions.Slot;
        Snapshot.Counts = Controller.Actions.Slots;
        Snapshot.OffhandCount = Controller.Actions.Offhand;
        Snapshot.Items =
            EHudDataSource::Preview; // selection is real; token counts are the existing action test double.
        Snapshot.ItemName =
            Snapshot.Counts.IsValidIndex(Snapshot.SelectedSlot) && Snapshot.Counts[Snapshot.SelectedSlot] > 0
            ? TEXT("Preview Token")
            : TEXT("");
    }
    if (View && View->GetBlockIcons() != (Authority ? &Authority->World.GetRegistry() : nullptr))
        View->SetBlockIcons(Authority ? Authority->World.GetRegistryRef().ToSharedPtr() : nullptr);
    const UMenuPreferences* Preferences = Controller.GetGameInstance()->GetSubsystem<UMenuPreferences>();
    if (Preferences)
    {
        const auto Hand = Preferences->FindPreference(TEXT("mainHand"));
        Snapshot.bMainHandLeft = Hand.IsValid() && Hand->AsString() == TEXT("Left");
        const auto Attack = Preferences->FindPreference(TEXT("attackIndicator"));
        const FString Indicator = Attack.IsValid() ? Attack->AsString() : TEXT("Crosshair");
        Snapshot.AttackIndicator = Indicator == TEXT("Hotbar") ? EHudAttack::Hotbar
            : Indicator == TEXT("Crosshair")                   ? EHudAttack::Crosshair
                                                               : EHudAttack::Off;
        const auto Duration = Preferences->FindPreference(TEXT("notificationDisplayTime"));
        Snapshot.NotificationTime = Duration.IsValid() ? Duration->AsNumber() : 1;
    }
    GuiScale = HudModel::GetScale(Viewport, Controller.GetMenuHost()->GetVideoSettings()->GetGuiScale());
    Presentation.Update(Snapshot, HudTick, HudTick * .05);
    const bool bBlink = Presentation.IsBlinking(HudTick);
    const float NameAlpha = Presentation.GetNameAlpha(HudTick);
    const bool bTickGeometry = Snapshot.bRegeneration ||
        FMath::CeilToInt(Snapshot.Health) + FMath::CeilToInt(Snapshot.Absorption) <= 4 || Snapshot.Saturation <= 0 ||
        Snapshot.Air <= 0 || Snapshot.OffhandView.bAnimatedBlock ||
        Snapshot.ItemViews.ContainsByPredicate([](const FHudItemView& Item) { return Item.bAnimatedBlock; });
    if (Snapshot == LastDrawSnapshot && LastDrawViewport == Viewport && LastDrawScale == GuiScale &&
        LastDrawHealth == Presentation.DisplayedHealth && bLastDrawBlink == bBlink && LastDrawNameAlpha == NameAlpha &&
        (!bTickGeometry || LastDrawTick == HudTick))
        return;
    LastDrawSnapshot = Snapshot;
    LastDrawViewport = Viewport;
    LastDrawScale = GuiScale;
    LastDrawHealth = Presentation.DisplayedHealth;
    bLastDrawBlink = bBlink;
    LastDrawNameAlpha = NameAlpha;
    LastDrawTick = HudTick;
    View->SetFrame(HudDrawList::Build(Snapshot, Presentation,
                       HudModel::GetLayout(
                           HudModel::GetLogicalSize(Viewport, GuiScale), Snapshot.SelectedSlot, Snapshot.bMainHandLeft),
                       HudTick, Assets->Glyphs),
        GuiScale);
}

TSharedRef<SWidget> UHudPresenter::CreatePreviewControls()
{
    const TWeakObjectPtr<UHudPresenter> WeakThis = this;
    const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    auto Label = [](const TCHAR* Text) { return SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true); };
    List->AddSlot().AutoHeight()[Label(
        TEXT("HUD PREVIEW fixture - session only. It replaces the player's vitals on screen while shown; its "
             "edits and sequences never change the player."))];
    List->AddSlot().AutoHeight().Padding(0, 2)[SNew(SCheckBox)
            .IsChecked_Lambda(
                [WeakThis]
                {
                    return WeakThis.IsValid() && WeakThis->bPreviewEnabled ? ECheckBoxState::Checked
                                                                           : ECheckBoxState::Unchecked;
                })
            .OnCheckStateChanged_Lambda(
                [WeakThis](ECheckBoxState State)
                {
                    if (WeakThis.IsValid()) WeakThis->bPreviewEnabled = State == ECheckBoxState::Checked;
                })[Label(TEXT("Show the preview fixture instead of the player's vitals"))]];
    auto Number = [&List, WeakThis, Label](const TCHAR* Name, double Min, double Max,
                      TFunction<double(const FHudSnapshot&)> Get, TFunction<void(FHudSnapshot&, double)> Set)
    {
        List->AddSlot().AutoHeight().Padding(
            0, 2)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1)[Label(Name)] +
            SHorizontalBox::Slot().AutoWidth()[SNew(SSpinBox<double>)
                    .MinDesiredWidth(105)
                    .MinValue(Min)
                    .MaxValue(Max)
                    .Value_Lambda([WeakThis, Get] { return WeakThis.IsValid() ? Get(WeakThis->Preview.Values) : 0.; })
                    .OnValueChanged_Lambda(
                        [WeakThis, Set](double Value)
                        {
                            if (!WeakThis.IsValid()) return;
                            WeakThis->Preview.Sequence = EHudPreviewSequence::None;
                            Set(WeakThis->Preview.Values, Value);
                        })]];
    };
    auto Real = [&Number](const TCHAR* Name, double Min, double Max, double FHudSnapshot::* Member)
    {
        Number(
            Name, Min, Max, [Member](const FHudSnapshot& S) { return S.*Member; },
            [Member](FHudSnapshot& S, double V) { S.*Member = V; });
    };
    auto Integer = [&Number](const TCHAR* Name, int32 Min, int32 Max, int32 FHudSnapshot::* Member)
    {
        Number(
            Name, Min, Max, [Member](const FHudSnapshot& S) { return double(S.*Member); },
            [Member](FHudSnapshot& S, double V) { S.*Member = FMath::RoundToInt(V); });
    };
    auto Check = [&List, WeakThis, Label](const TCHAR* Name, bool FHudSnapshot::* Member)
    {
        List->AddSlot().AutoHeight().Padding(0, 2)[SNew(SCheckBox)
                .IsChecked_Lambda(
                    [WeakThis, Member]
                    {
                        return WeakThis.IsValid() && WeakThis->Preview.Values.*Member ? ECheckBoxState::Checked
                                                                                      : ECheckBoxState::Unchecked;
                    })
                .OnCheckStateChanged_Lambda(
                    [WeakThis, Member](ECheckBoxState State)
                    {
                        if (WeakThis.IsValid()) WeakThis->Preview.Values.*Member = State == ECheckBoxState::Checked;
                    })[Label(Name)]];
    };
    List->AddSlot().AutoHeight()[SNew(SButton)
            .Text_Lambda(
                [WeakThis]
                {
                    static const TCHAR* Modes[] = {
                        TEXT("Survival"), TEXT("Adventure"), TEXT("Creative"), TEXT("Spectator")};
                    return FText::FromString(FString(TEXT("Preview mode: ")) +
                        Modes[WeakThis.IsValid() ? int32(WeakThis->Preview.Values.Mode) : 0]);
                })
            .OnClicked_Lambda(
                [WeakThis]
                {
                    if (WeakThis.IsValid())
                        WeakThis->Preview.Values.Mode = EHudMode((int32(WeakThis->Preview.Values.Mode) + 1) % 4);
                    return FReply::Handled();
                })];
    Real(TEXT("Health (points)"), 0, 200, &FHudSnapshot::Health);
    Real(TEXT("Maximum health"), 1, 200, &FHudSnapshot::MaxHealth);
    Real(TEXT("Absorption"), 0, 200, &FHudSnapshot::Absorption);
    Integer(TEXT("Armor"), 0, 20, &FHudSnapshot::Armor);
    Integer(TEXT("Food"), 0, 20, &FHudSnapshot::Food);
    Real(TEXT("Saturation (no visible meter)"), 0, 20, &FHudSnapshot::Saturation);
    Integer(TEXT("Air"), -20, 1200, &FHudSnapshot::Air);
    Integer(TEXT("Maximum air (modifier input)"), 1, 1200, &FHudSnapshot::MaxAir);
    Integer(TEXT("XP level"), 0, 999, &FHudSnapshot::Level);
    Real(TEXT("XP progress"), 0, 1, &FHudSnapshot::Experience);
    Real(TEXT("Attack strength"), 0, 1, &FHudSnapshot::AttackStrength);
    Real(TEXT("Attack delay (ticks)"), 1, 40, &FHudSnapshot::AttackDelay);
    Check(TEXT("Eyes underwater"), &FHudSnapshot::bUnderwater);
    Check(TEXT("Hardcore art"), &FHudSnapshot::bHardcore);
    Check(TEXT("Poison"), &FHudSnapshot::bPoison);
    Check(TEXT("Wither"), &FHudSnapshot::bWither);
    Check(TEXT("Fully frozen"), &FHudSnapshot::bFrozen);
    Check(TEXT("Hunger effect"), &FHudSnapshot::bHunger);
    Check(TEXT("Regeneration lift"), &FHudSnapshot::bRegeneration);
    Check(TEXT("Damage cooldown (health edits blink)"), &FHudSnapshot::bDamageCooldown);
    Check(TEXT("Living target"), &FHudSnapshot::bLivingTarget);
    Check(TEXT("Target alive"), &FHudSnapshot::bTargetAlive);
    Check(TEXT("Target in attack range"), &FHudSnapshot::bTargetInRange);
    Check(TEXT("Spectator interaction target"), &FHudSnapshot::bSpectatorTarget);
    const TCHAR* Sequences[] = {TEXT("Damage + healing during blink"), TEXT("Absorption loss"),
        TEXT("Air depletion + surface refill"), TEXT("XP gain"), TEXT("Attack recharge + ready target")};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sequences); ++Index)
        List->AddSlot().AutoHeight().Padding(0, 2)[SNew(SButton)
                .Text(FText::FromString(Sequences[Index]))
                .OnClicked_Lambda(
                    [WeakThis, Index]
                    {
                        // A timed sequence is only useful on screen, so it shows the fixture.
                        if (WeakThis.IsValid())
                        {
                            WeakThis->Preview.Start(EHudPreviewSequence(Index + 1));
                            WeakThis->bPreviewEnabled = true;
                        }
                        return FReply::Handled();
                    })];
    List->AddSlot().AutoHeight()[Label(TEXT(
        "Close options to run a timed preview. Hand / attack style / GUI scale use the existing Options screens."))];
    List->AddSlot().AutoHeight()[SNew(SButton)
            .Text(FText::FromString(TEXT("Reset HUD preview")))
            .OnClicked_Lambda(
                [WeakThis]
                {
                    if (WeakThis.IsValid()) WeakThis->ResetPreview();
                    return FReply::Handled();
                })];
    return SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(.02f, .025f, .03f, 1.f))
        .Padding(10)[SNew(SBox).WidthOverride(500).HeightOverride(330)[SNew(SScrollBox) + SScrollBox::Slot()[List]]];
}
