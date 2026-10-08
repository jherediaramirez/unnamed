#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Hud/HudPreview.h"
#include "HudPresenter.generated.h"

class AFoundationController;
class FFoundationBlockRegistry;
class SHudView;
class SWidget;
class UHudAssets;
class UTexture2D;
struct FFoundationInventory;
struct FFoundationRules;
struct FFoundationTarget;
struct FFoundationVitals;

// The Foundation adapter: samples the player's vitals (or, on request, the preview fixture), the
// hotbar (real items in the Playground, the token test double elsewhere) and settings into one
// FHudSnapshot, and hands draw lists to the passive view.
UCLASS()
class UNNAMED_API UHudPresenter : public UObject
{
    GENERATED_BODY()
public:
    TSharedRef<SWidget> CreateView();
    void Refresh(AFoundationController& Controller, double Now);
    TSharedRef<SWidget> CreatePreviewControls();
    void ResetPreview();
    // Resources come from the player's vitals unless the preview fixture is shown instead.
    void SetPreviewEnabled(bool bEnabled) { bPreviewEnabled = bEnabled; }
    bool IsPreviewEnabled() const { return bPreviewEnabled; }
    // Authoritative resources, mode and attack facts. Effects and Hardcore have no owner yet and stay off.
    static void ApplyVitals(FHudSnapshot& Snapshot, const FFoundationVitals& Vitals, const FFoundationTarget& Target);
    // Authoritative items (the Playground): selection, counts, icons, durability and the held name.
    static void ApplyInventory(FHudSnapshot& Snapshot, const FFoundationInventory& Inventory,
        const FFoundationRules& Rules, const FFoundationBlockRegistry* Blocks = nullptr);
    FHudPreview Preview;
    const FHudSnapshot& GetSnapshot() const { return Snapshot; }
    const FHudPresentation& GetPresentation() const { return Presentation; }
    int32 GetLoadedTextureCount() const { return Textures.Num(); }
    int32 GetMissingTextures() const { return MissingTextures; }
    int32 GetDrawCount() const;
    int32 GetGuiScale() const { return GuiScale; }
    int64 GetTick() const { return HudTick; }

private:
    UPROPERTY(Transient)
    TObjectPtr<UHudAssets> Assets;
    // Slate brushes do not retain UObjects against GC.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTexture2D>> Textures;
    TSharedPtr<SHudView> View;
    FHudSnapshot Snapshot;
    FHudSnapshot LastDrawSnapshot;
    FHudPresentation Presentation;
    FIntPoint LastViewport = FIntPoint::ZeroValue;
    int64 HudTick = 0;
    int64 LastSampleTick = -1;
    int64 LastDrawTick = -1;
    FIntPoint LastDrawViewport = FIntPoint::ZeroValue;
    int32 LastDrawScale = 0;
    int32 LastDrawHealth = -1;
    bool bLastDrawBlink = false;
    float LastDrawNameAlpha = -1;
    int32 GuiScale = 1, MissingTextures = 0;
    double LastTime = 0, Accumulator = 0;
    bool bPreviewEnabled = false;
};
