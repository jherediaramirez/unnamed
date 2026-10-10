#include "Foundation/FoundationBreakEffects.h"
#include "Foundation/FoundationBreakAssets.h"
#include "Blocks/VoxelBlockTable.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2DArray.h"
#include "Engine/World.h"
#include "Foundation/FoundationBlockMeshes.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionView.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Menu/MenuPreferences.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundWave.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"

namespace
{
    TAutoConsoleVariable<int32> CVarBreakOutline(TEXT("foundation.Break.Outline"), 2,
        TEXT("Target outline: 0 the view's own, 1 vanilla (black 40%, 3 px), 2 thinner (black 30%, 1 px), 3 none."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarBreakCracks(TEXT("foundation.Break.Cracks"), 2,
        TEXT("Crack overlay: 0 the view's lines, 1 vanilla's stages, 2 carved stages."), ECVF_Default);
    TAutoConsoleVariable<bool> CVarBreakCrackSmooth(TEXT("foundation.Break.CrackSmooth"), true,
        TEXT("1 blends the crack toward the next stage as the progress grows."), ECVF_Default);
    TAutoConsoleVariable<int32> CVarBreakParticles(TEXT("foundation.Break.Particles"), 1,
        TEXT("Break particles and hit chips: 0 the view's cubes, 1 textured."), ECVF_Default);
    TAutoConsoleVariable<bool> CVarBreakParticleCollision(TEXT("foundation.Break.ParticleCollision"), true,
        TEXT("1 makes particles land on solid blocks."), ECVF_Default);
    TAutoConsoleVariable<bool> CVarBreakParticleSpray(TEXT("foundation.Break.ParticleSpray"), true,
        TEXT("1 sprays break particles along the mined face and away from the camera."), ECVF_Default);
    TAutoConsoleVariable<int32> CVarBreakSounds(TEXT("foundation.Break.Sounds"), 2,
        TEXT("Break and hit sounds: 0 off, 1 vanilla, 2 vanilla with hit pitch variety and two hit voices."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarBreakSwing(TEXT("foundation.Break.Swing"), 2,
        TEXT("Swing: 0 the view's own, 1 a 6-tick swing with hits every 4 steps, 2 hits on each swing's contact tick."),
        ECVF_Default);
    TAutoConsoleVariable<bool> CVarBreakPop(
        TEXT("foundation.Break.Pop"), true, TEXT("1 shrinks a broken block away over 80 ms."), ECVF_Default);

    // The box of Boxes holding a point, or null.
    const FBox* BreakFindBox(const TArray<FBox>& Boxes, const FVector& Point)
    {
        for (const FBox& Box : Boxes)
            if (Box.IsInsideOrOn(Point)) return &Box;
        return nullptr;
    }
}

namespace FoundationBreak
{
    bool FSwitches::AnyOn() const
    {
        return Outline != 0 || Cracks != 0 || bCrackSmooth || Particles != 0 || bParticleCollision || bParticleSpray ||
            Sounds != 0 || Swing != 0 || bPop;
    }

    FSwitches ReadSwitches()
    {
        FSwitches Switches;
        Switches.Outline = CVarBreakOutline.GetValueOnGameThread();
        Switches.Cracks = CVarBreakCracks.GetValueOnGameThread();
        Switches.bCrackSmooth = CVarBreakCrackSmooth.GetValueOnGameThread();
        Switches.Particles = CVarBreakParticles.GetValueOnGameThread();
        Switches.bParticleCollision = CVarBreakParticleCollision.GetValueOnGameThread();
        Switches.bParticleSpray = CVarBreakParticleSpray.GetValueOnGameThread();
        Switches.Sounds = CVarBreakSounds.GetValueOnGameThread();
        Switches.Swing = CVarBreakSwing.GetValueOnGameThread();
        Switches.bPop = CVarBreakPop.GetValueOnGameThread();
        return Switches;
    }

    int32 CrackStage(double Progress)
    {
        return FMath::Clamp(int32(FMath::FloorToDouble(Progress * Stages)), 0, Stages - 1);
    }

    double CrackFade(double Progress)
    {
        if (CrackStage(Progress) == Stages - 1) return 0;
        const double Scaled = FMath::Max(Progress, 0.) * Stages;
        return FMath::Clamp(Scaled - FMath::FloorToDouble(Scaled), 0., 1. - UE_DOUBLE_SMALL_NUMBER);
    }

    void CarveStage(TConstArrayView<bool> Crack, TArrayView<uint8> OutGrey)
    {
        const int32 Size = int32(TexelsPerFace);
        check(Crack.Num() == Size * Size && OutGrey.Num() == Size * Size);
        for (int32 Y = 0; Y < Size; ++Y)
            for (int32 X = 0; X < Size; ++X)
            {
                uint8 Grey = NeutralGrey;
                if (Crack[Y * Size + X])
                    Grey = CrackGrey;
                else if ((X + 1 < Size && Crack[Y * Size + X + 1]) || (Y + 1 < Size && Crack[(Y + 1) * Size + X]))
                    Grey = ShadowGrey;
                else if ((X > 0 && Crack[Y * Size + X - 1]) || (Y > 0 && Crack[(Y - 1) * Size + X]))
                    Grey = LitGrey;
                OutGrey[Y * Size + X] = Grey;
            }
    }

    void PlainStage(TConstArrayView<bool> Crack, TConstArrayView<uint8> Grey, TArrayView<uint8> OutGrey)
    {
        const int32 Count = int32(TexelsPerFace * TexelsPerFace);
        check(Crack.Num() == Count && Grey.Num() == Count && OutGrey.Num() == Count);
        for (int32 Index = 0; Index < Count; ++Index)
            OutGrey[Index] = Crack[Index] ? Grey[Index] : NeutralGrey;
    }

    double ParticleAmount(const FString& Pref)
    {
        if (Pref.Equals(TEXT("Decreased"), ESearchCase::IgnoreCase)) return 0.67;
        if (Pref.Equals(TEXT("Minimal"), ESearchCase::IgnoreCase)) return 0.33;
        return 1;
    }

    int32 BreakParticlesPerBox(double Amount)
    {
        return FMath::Clamp(FMath::RoundToInt32(64 * Amount), 0, 64);
    }

    bool HitChipsOn(double Amount)
    {
        return Amount > 0.5;
    }

    bool IsHitStep(int32 SwingMode, int64 MiningStart, int64 SwingStart, int64 Step)
    {
        if (SwingMode == 2) return Step >= SwingStart && (Step - SwingStart) % SwingSteps == ContactStep;
        return Step >= MiningStart && (Step - MiningStart) % HitSteps == 0;
    }

    void CollectHitSteps(int32 SwingMode, int64 MiningStart, int64 SwingStart, int64 AfterStep, int64 UpToStep,
        TArray<int64, TInlineAllocator<4>>& Out)
    {
        Out.Reset();
        // Hits come at least every 6 steps, so the latest MaxCatchUp lie within this many steps of UpToStep.
        const int64 First = FMath::Max(AfterStep + 1, UpToStep - int64(MaxCatchUp) * SwingSteps + 1);
        for (int64 Step = First; Step <= UpToStep; ++Step)
            if (IsHitStep(SwingMode, MiningStart, SwingStart, Step)) Out.Add(Step);
        if (Out.Num() > MaxCatchUp) Out.RemoveAt(0, Out.Num() - MaxCatchUp);
    }

    double FSwingClock::GetPhase(int64 Step, double Alpha) const
    {
        if (DurationSteps <= 0) return -1;
        const double T = double(Step - StartStep + Alpha) / DurationSteps;
        if (T < 0) return -1;
        if (bRepeating) return T - FMath::FloorToDouble(T);
        return T < 1 ? T : -1;
    }

    double PopScale(double Seconds)
    {
        const double T = FMath::Clamp(Seconds / PopSeconds, 0., 1.);
        return 1 - T * T;
    }

    double PixelsToWorld(double Pixels, double DepthCm, double HorizontalFovDegrees, double ViewportWidth)
    {
        if (ViewportWidth <= 0 || DepthCm <= 0) return 0;
        return Pixels * 2 * DepthCm * FMath::Tan(FMath::DegreesToRadians(HorizontalFovDegrees * 0.5)) / ViewportWidth;
    }

    FSoundPlay PickSound(const FFoundationBreakBlock& Block, const FFoundationBreakSoundEvent& Event, bool bHit,
        int32 SoundsMode, FRandomStream& Random)
    {
        FSoundPlay Play;
        if (Event.Files.IsEmpty()) return Play;
        // The file first, then the pitch spread: tests predict the draws.
        Play.File = Random.RandRange(0, Event.Files.Num() - 1);
        const FFoundationBreakSoundFile& File = Event.Files[Play.File];
        const double BaseVolume = bHit ? 0.25 : 1.0;
        double Pitch = (bHit ? 0.5 : 0.8) * Block.Pitch * File.Pitch;
        if (bHit && SoundsMode == 2) Pitch *= Random.FRandRange(0.97, 1.03);
        Play.Volume = float(BaseVolume * Block.Volume * File.Volume);
        Play.Pitch = float(Pitch);
        return Play;
    }

    void SpawnBreakParticles(
        const FBox& Box, double Amount, const FVector& Push, FRandomStream& Random, TArray<FParticleSpawn>& Out)
    {
        constexpr int32 Divisions = 4;
        constexpr int32 Points = Divisions * Divisions * Divisions;
        int32 Order[Points];
        for (int32 Index = 0; Index < Points; ++Index)
            Order[Index] = Index;
        // Fisher-Yates: the first Count entries are the picked points, none repeated.
        for (int32 Index = Points - 1; Index > 0; --Index)
            Swap(Order[Index], Order[Random.RandRange(0, Index)]);

        const FVector Centre = Box.GetCenter();
        const FVector Extent = Box.GetSize();
        const int32 Count = BreakParticlesPerBox(Amount);
        for (int32 Pick = 0; Pick < Count; ++Pick)
        {
            const int32 Cell = Order[Pick];
            const FVector Point = Box.Min +
                Extent *
                    FVector((Cell % Divisions + 0.5) / Divisions, (Cell / Divisions % Divisions + 0.5) / Divisions,
                        (Cell / (Divisions * Divisions) + 0.5) / Divisions);
            FParticleSpawn& Spawn = Out.AddDefaulted_GetRef();
            Spawn.Location = Point;
            const FVector Jitter(Random.FRandRange(-JitterCm, JitterCm), Random.FRandRange(-JitterCm, JitterCm),
                Random.FRandRange(-JitterCm, JitterCm));
            Spawn.Velocity = (Point - Centre) * 1.5 + Jitter + FVector(0, 0, 100) + Push;
            Spawn.Life = Random.FRandRange(1.5, 2.3);
            Spawn.Size = BreakSizeCm;
            Spawn.PatchU = uint8(Random.RandRange(0, 12));
            Spawn.PatchV = uint8(Random.RandRange(0, 12));
        }
    }

    FParticleSpawn SpawnHitChip(const FVector& Point, const FVector& Normal, bool bSpray, FRandomStream& Random)
    {
        FParticleSpawn Spawn;
        Spawn.Location = Point + Normal;
        Spawn.Velocity = Normal * (bSpray ? 60 : 40);
        Spawn.Life = Random.FRandRange(1.5, 2.3);
        Spawn.Size = HitSizeCm;
        Spawn.PatchU = uint8(Random.RandRange(0, 12));
        Spawn.PatchV = uint8(Random.RandRange(0, 12));
        return Spawn;
    }

    FVector SprayPush(const FVector& FaceNormal, const FVector& AwayFromCamera)
    {
        return FaceNormal * 150 + AwayFromCamera.GetSafeNormal() * 50;
    }

    void StepParticle(FParticleState& Particle, double DeltaTime, bool bCollision,
        TFunctionRef<void(const FVector& Point, TArray<FBox>& OutBoxes)> Solid)
    {
        if (Particle.bResting) return;
        Particle.Velocity.Z -= GravityCm * DeltaTime;
        const FVector Next = Particle.Location + Particle.Velocity * DeltaTime;
        if (!bCollision)
        {
            Particle.Location = Next;
            return;
        }

        TArray<FBox> Boxes;
        Solid(Particle.Location, Boxes);
        // A particle already inside a box (spawned in one) flies free, or it would never leave.
        const bool bInside = BreakFindBox(Boxes, Particle.Location) != nullptr;
        Boxes.Reset();
        Solid(Next, Boxes);
        const FBox* Hit = bInside ? nullptr : BreakFindBox(Boxes, Next);
        if (!Hit)
        {
            Particle.Location = Next;
            return;
        }
        if (Particle.Velocity.Z < 0 && Particle.Location.Z >= Hit->Max.Z)
        {
            Particle.Location = FVector(Next.X, Next.Y, Hit->Max.Z + 0.01);
            Particle.Velocity = FVector::ZeroVector;
            Particle.bResting = true;
            return;
        }
        // A side hit: stop sideways and keep falling, unless that still ends in a box (then stay put).
        const FVector Slid(Particle.Location.X, Particle.Location.Y, Next.Z);
        Boxes.Reset();
        Solid(Slid, Boxes);
        if (!BreakFindBox(Boxes, Slid)) Particle.Location = Slid;
        Particle.Velocity = FVector(0, 0, FMath::Min(Particle.Velocity.Z, 0.));
    }
}

// FFoundationBreakEffects

namespace
{
    constexpr double BreakEffectsBlockCm = UFoundationProfile::BlockCm;
    // Probes compare runs: every play draws the same particles and sound files.
    constexpr int32 BreakEffectsSeed = 0x0B5EA4;
    // The crack overlay sits just outside what it cracks, scaled about the drawn box's centre.
    constexpr double CrackInflate = 1.002;
    // As today's outline: 0.3 cm past the box, each edge drawn as four strips so each keeps its own depth's width.
    constexpr double OutlineExpandCm = .3;
    constexpr int32 OutlineSegments = 4;
    constexpr double OutlineHaloPixels = 9;
    constexpr uint32 BreakUntinted = 0xFFFFFF;

    template <typename TAsset> TAsset* LoadBreakAsset(const TCHAR* Name)
    {
        // Under UUnnamedProjectSettings::BreakRoot (/Game/Breaking).
        const FString Path = UUnnamedProjectSettings::GetBreakAssetPath(Name).ToString();
        return LoadObject<TAsset>(nullptr, *Path, {}, LOAD_NoWarn | LOAD_Quiet);
    }

    // The effects' primitives: movable, shadowless, no collision, hidden, and registered now (never mid-play).
    void RegisterBreakComponent(UStaticMeshComponent& Component, AActor& Owner, UStaticMesh* Mesh, int32 SortPriority)
    {
        Component.SetStaticMesh(Mesh);
        Component.SetMobility(EComponentMobility::Movable);
        Component.SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component.SetCanEverAffectNavigation(false);
        Component.SetCastShadow(false);
        Component.SetTranslucentSortPriority(SortPriority);
        Component.SetVisibility(false);
        Component.SetupAttachment(Owner.GetRootComponent());
        Component.RegisterComponent();
    }

    // LoadAssets sets an effect's own material only when it loaded; without one the effect draws nothing.
    bool HasBreakMaterial(const UMeshComponent* Component)
    {
        return Component && Component->GetNumOverrideMaterials() > 0;
    }

    // Particles need the break data (their texture and tint) and M_BreakParticle.
    bool CanDrawBreakParticles(const UFoundationBreakAssets* BreakAssets, const UInstancedStaticMeshComponent* Mesh)
    {
        return BreakAssets && HasBreakMaterial(Mesh);
    }
}

FFoundationBreakEffects::FFoundationBreakEffects(UFoundationInteractionView& InView) : View(InView) {}

UWorld* FFoundationBreakEffects::GetWorld() const
{
    return View.GetWorld();
}

void FFoundationBreakEffects::Initialize(const FFoundationAuthority& Authority)
{
    Random.Initialize(BreakEffectsSeed);
    AActor* Owner = View.GetOwner();
    if (!Owner || !Owner->GetRootComponent()) return;
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));

    // Outline strips: halo under core, both over the cracks.
    auto MakeStrips = [Owner, CubeMesh](int32 SortPriority)
    {
        auto* Strips = NewObject<UInstancedStaticMeshComponent>(Owner);
        Strips->SetNumCustomDataFloats(4);
        RegisterBreakComponent(*Strips, *Owner, CubeMesh, SortPriority);
        return Strips;
    };
    OutlineHalo = MakeStrips(10);
    OutlineCore = MakeStrips(11);
    View.Owned.Add(OutlineHalo);
    View.Owned.Add(OutlineCore);

    // As many crack overlays and pop parts as the held block's pool: the model default state with the most parts.
    int32 MostParts = 1;
    const FFoundationBlockRegistry& Registry = Authority.World.GetRegistry();
    TArray<TConstArrayView<int32>> ModelParts;
    for (int32 Block = 1; Block < Registry.GetBlockCount(); ++Block)
        if (Registry.GetModelParts(Registry.GetBlockAt(Block).GetDefaultState(), ModelParts))
            MostParts = FMath::Max(MostParts, ModelParts.Num());
    for (int32 Index = 0; Index < MostParts; ++Index)
    {
        auto* Part = NewObject<UStaticMeshComponent>(Owner);
        RegisterBreakComponent(*Part, *Owner, nullptr, 5);
        View.Owned.Add(Part);
        CrackParts.Add(Part);
    }

    // Particles: one component, every instance made up front at zero scale and reused by id.
    ParticleMesh = NewObject<UInstancedStaticMeshComponent>(Owner);
    ParticleMesh->SetNumCustomDataFloats(3);
    RegisterBreakComponent(*ParticleMesh, *Owner, PlaneMesh, 0);
    View.Owned.Add(ParticleMesh);
    TArray<FTransform> Hidden;
    Hidden.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), MaxParticles);
    ParticleIds = ParticleMesh->AddInstancesById(Hidden, false, false);
    Particles.SetNum(MaxParticles);

    // Pops draw with the block materials, so they need the view's block assets.
    if (!View.BlockMeshes.IsValidIndex(FoundationBlockMeshes::Cube) || View.LayerMaterials.IsEmpty()) return;
    for (int32 Slot = 0; Slot < PopSlots; ++Slot)
    {
        FPop& Pop = Pops.AddDefaulted_GetRef();
        for (int32 Index = 0; Index < MostParts; ++Index)
        {
            UInstancedStaticMeshComponent* Part =
                View.MakeBlockComponent(nullptr, View.BlockMeshes[FoundationBlockMeshes::Cube], 0);
            Part->SetCastShadow(false);
            Part->SetVisibility(false);
            View.Owned.Add(Part);
            Pop.Parts.Add(Part);
        }
    }
}

void FFoundationBreakEffects::LoadAssets(const FFoundationAuthority& Authority)
{
    bTriedAssets = true;
    UFoundationBreakAssets* Loaded = LoadBreakAsset<UFoundationBreakAssets>(TEXT("DA_BreakAssets"));
    UMaterialInterface* CracksBase = LoadBreakAsset<UMaterialInterface>(TEXT("M_BreakCracks"));
    UMaterialInterface* ParticleBase = LoadBreakAsset<UMaterialInterface>(TEXT("M_BreakParticle"));
    UMaterialInterface* OutlineBase = LoadBreakAsset<UMaterialInterface>(TEXT("M_BreakOutline"));
    TArray<FString> Missing;
    if (!Loaded) Missing.Add(TEXT("DA_BreakAssets"));
    if (!CracksBase) Missing.Add(TEXT("M_BreakCracks"));
    if (!ParticleBase) Missing.Add(TEXT("M_BreakParticle"));
    if (!OutlineBase) Missing.Add(TEXT("M_BreakOutline"));
    if (!Missing.IsEmpty())
        UE_LOG(LogUnnamedFoundation, Warning,
            TEXT("Breaking effects: %s missing under %s; the effects needing them stay off"),
            *FString::Join(Missing, TEXT(", ")), *UUnnamedProjectSettings::Get().BreakRoot);

    Assets = Loaded;
    if (Loaded) View.Owned.Add(Loaded);
    // The outline needs only its material.
    if (OutlineBase && OutlineHalo && OutlineCore)
    {
        View.Owned.Add(OutlineBase);
        OutlineHalo->SetMaterial(0, OutlineBase);
        OutlineCore->SetMaterial(0, OutlineBase);
    }
    if (ParticleBase && ParticleMesh)
    {
        View.Owned.Add(ParticleBase);
        ParticleMesh->SetMaterial(0, ParticleBase);
    }
    if (CracksBase && Loaded)
    {
        View.Owned.Add(CracksBase);
        CrackMaterial = UMaterialInstanceDynamic::Create(CracksBase, &View);
        CrackMaterial->SetTextureParameterValue(TEXT("Cracks"), Loaded->Cracks.Get());
        View.Owned.Add(CrackMaterial);
        for (UStaticMeshComponent* Part : CrackParts)
            Part->SetMaterial(0, CrackMaterial);
    }

    // DA_Blocks' texture names -> their first layer in T_BlockTextures, and the break blocks by id.
    TextureLayers.Reset();
    BlockIndex.Reset();
    const UFoundationBlockAssets* BlockAssets = Authority.World.GetRegistry().GetAssets();
    if (BlockAssets)
        for (const FFoundationBlockTextureAsset& Texture : BlockAssets->Textures)
            TextureLayers.Add(Texture.Name, Texture.FirstLayer);
    if (!Loaded) return;
    for (int32 Index = 0; Index < Loaded->Blocks.Num(); ++Index)
        BlockIndex.Add(Loaded->Blocks[Index].Id, Index);
    if (!BlockAssets)
    {
        UE_LOG(LogUnnamedFoundation, Warning, TEXT("Breaking effects: no block textures; particles draw layer 0"));
        return;
    }
    // A particle texture DA_Blocks lacks draws layer 0: one warning per name, here, so the lookups stay const.
    auto Resolve = [this](FName Name)
    {
        if (Name.IsNone() || TextureLayers.Contains(Name)) return;
        UE_LOG(LogUnnamedFoundation, Warning, TEXT("Breaking effects: particle texture %s is not in DA_Blocks"),
            *Name.ToString());
        TextureLayers.Add(Name, 0);
    };
    for (const FFoundationBreakBlock& Block : Loaded->Blocks)
    {
        Resolve(Block.Particle);
        for (const FName Name : Block.OverrideParticles)
            Resolve(Name);
    }
}

void FFoundationBreakEffects::BeginFrame(const FFoundationAuthority& Authority)
{
    Switches = FoundationBreak::ReadSwitches();
    bOutlineSubmitted = false;
    OutlineBoxes.Reset();
    // Every switch at 0 reads nothing else.
    if (!Switches.AnyOn()) return;
    if (!bTriedAssets) LoadAssets(Authority);

    const UWorld* World = GetWorld();
    const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
    if (const UMenuPreferences* Preferences = Instance ? Instance->GetSubsystem<UMenuPreferences>() : nullptr)
    {
        const TSharedPtr<const FJsonValue> ParticlePref = Preferences->FindPreference(TEXT("particles"));
        Amount = FoundationBreak::ParticleAmount(
            ParticlePref.IsValid() && ParticlePref->Type == EJson::String ? ParticlePref->AsString() : FString());
        const TSharedPtr<const FJsonValue> Contrast = Preferences->FindPreference(TEXT("highContrastBlockOutline"));
        bHighContrast = Contrast.IsValid() && Contrast->Type == EJson::Boolean && Contrast->AsBool();
    }
    const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
    if (const APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager.Get() : nullptr)
    {
        CameraLocation = Camera->GetCameraLocation();
        CameraForward = Camera->GetCameraRotation().Vector();
        CameraFov = Camera->GetFOVAngle();
    }
    ViewportWidth = 1280;
    if (GEngine && GEngine->GameViewport)
    {
        FVector2D Size = FVector2D::ZeroVector;
        GEngine->GameViewport->GetViewportSize(Size);
        if (Size.X > 0) ViewportWidth = Size.X;
    }
}

bool FFoundationBreakEffects::SubmitOutline(TConstArrayView<FBox> Boxes)
{
    if (Switches.Outline == 0) return false;
    // Outline 3 hides the view's lines and draws nothing; 1 and 2 need M_BreakOutline, else the view's lines stay.
    if (Switches.Outline == 3) return true;
    if (!HasBreakMaterial(OutlineCore) || !OutlineHalo) return false;
    OutlineBoxes.Append(Boxes.GetData(), Boxes.Num());
    bOutlineSubmitted = true;
    return true;
}

bool FFoundationBreakEffects::UpdateMining(
    const FFoundationAuthority& Authority, const FFoundationInteraction* Interaction, bool bVisible, double DeltaTime)
{
    // The clocks are a few compares, kept with every switch at 0 so turning one on never replays a stale press.
    if (Interaction && Interaction->Step != LastStep)
    {
        LastStep = Interaction->Step;
        StepAlphaSeconds = 0;
    }
    else
        StepAlphaSeconds += DeltaTime;
    if (Interaction)
    {
        if (Interaction->PressSerial != LastPressSerial)
        {
            LastPressSerial = Interaction->PressSerial;
            Swing.StartStep = Interaction->Step;
        }
        // Held on a block (mining, or the delay between breaks), the swing repeats with no rest.
        Swing.bRepeating = Interaction->bAttackHeld && (Interaction->bMining || Interaction->MiningDelay > 0);
    }

    const bool bMining = Interaction && Interaction->bMining;
    if (bMining)
    {
        if (!bWasMining || Interaction->MiningCell != MiningCell)
        {
            bWasMining = true;
            MiningCell = Interaction->MiningCell;
            MiningStart = Interaction->Step;
            LastHitStep = Interaction->Step - 1;
            MinedFace = EFoundationFace::None;
        }
        if (Interaction->Target.Cell == MiningCell) MinedFace = Interaction->Target.Face;
        if (Switches.Sounds != 0 || Switches.Particles == 1)
        {
            TArray<int64, TInlineAllocator<4>> Hits;
            FoundationBreak::CollectHitSteps(
                Switches.Swing, MiningStart, Swing.StartStep, LastHitStep, Interaction->Step, Hits);
            const FFoundationBlockState State = Authority.World.Get(MiningCell).State;
            const bool bChips = Switches.Particles == 1 && FoundationBreak::HitChipsOn(Amount) &&
                CanDrawBreakParticles(Assets, ParticleMesh);
            if (!Hits.IsEmpty() && !State.IsAir())
            {
                const FVector Centre = FoundationGrid::CellBox(MiningCell, Authority.World.OriginCm).GetCenter();
                float Layer = 0, Tint = 0;
                if (bChips) GetParticleLook(Authority, State, Layer, Tint);
                // The chip leaves the aimed point only while the target is the mined cell; otherwise the mined face's
                // centre (none when no face was seen).
                const bool bAimed = Interaction->Target.Cell == MiningCell;
                const EFoundationFace ChipFace = bAimed ? Interaction->Target.Face : MinedFace;
                const FVector ChipNormal = FoundationGrid::FaceNormal(ChipFace);
                const FVector ChipPoint =
                    bAimed ? Interaction->Target.Point : Centre + ChipNormal * (BreakEffectsBlockCm / 2);
                const bool bChip = bChips && ChipFace != EFoundationFace::None;
                for (int32 Hit = 0; Hit < Hits.Num(); ++Hit)
                {
                    if (Switches.Sounds != 0) PlaySound(Authority, State, Centre, true);
                    if (bChip)
                        Spawn(FoundationBreak::SpawnHitChip(ChipPoint, ChipNormal, Switches.bParticleSpray, Random),
                            Layer, Tint);
                }
            }
        }
        // Each step's hit is due once, whichever switch is on as it passes (no burst when one turns on).
        LastHitStep = Interaction->Step;
    }
    else
        bWasMining = false;

    if (Switches.Cracks != 0 && bMining && bVisible && Assets && CrackMaterial)
        UpdateCracks(Authority, *Interaction);
    else
        HideCracks();
    return Switches.Cracks != 0 && Assets && CrackMaterial;
}

void FFoundationBreakEffects::UpdateCracks(const FFoundationAuthority& Authority, const FFoundationInteraction& Mining)
{
    const FFoundationBlockState State = Authority.World.Get(Mining.MiningCell).State;
    if (!bCrackVisible || CrackCell != Mining.MiningCell || CrackState != State)
    {
        CrackCell = Mining.MiningCell;
        CrackState = State;
        TArray<UStaticMesh*, TInlineAllocator<4>> Meshes;
        TArray<FTransform, TInlineAllocator<4>> Places;
        GetStateParts(Authority, CrackCell, State, Meshes, Places);
        // M_BreakCracks projects from the mesh's local position, so mesh space stays cell space: the inflation is a
        // uniform scale about the drawn box's centre, folded into the translation.
        FBox Drawn(ForceInit);
        for (const UStaticMesh* Mesh : Meshes)
            Drawn += Mesh->GetBoundingBox();
        const FVector Centre = Drawn.IsValid ? Drawn.GetCenter() : FVector::ZeroVector;
        for (int32 Index = 0; Index < CrackParts.Num(); ++Index)
        {
            UStaticMeshComponent* Part = CrackParts[Index];
            const bool bUsed = Meshes.IsValidIndex(Index);
            if (bUsed)
            {
                FTransform Place = Places[Index];
                Place.SetScale3D(FVector(CrackInflate));
                Place.AddToTranslation(Centre * (1 - CrackInflate));
                Part->SetStaticMesh(Meshes[Index]);
                Part->SetWorldTransform(Place);
            }
            Part->SetVisibility(bUsed);
        }
    }
    CrackMaterial->SetScalarParameterValue(TEXT("Stage"), float(FoundationBreak::CrackStage(Mining.MiningProgress)));
    CrackMaterial->SetScalarParameterValue(
        TEXT("Fade"), Switches.bCrackSmooth ? float(FoundationBreak::CrackFade(Mining.MiningProgress)) : 0.f);
    CrackMaterial->SetScalarParameterValue(
        TEXT("LayerBase"), Switches.Cracks == 1 ? float(FoundationBreak::PlainLayer) : 0.f);
    bCrackVisible = true;
}

void FFoundationBreakEffects::HideCracks()
{
    if (!bCrackVisible) return;
    for (UStaticMeshComponent* Part : CrackParts)
        Part->SetVisibility(false);
    bCrackVisible = false;
    CrackCell = FIntVector(MAX_int32);
}

void FFoundationBreakEffects::GetStateParts(const FFoundationAuthority& Authority, const FIntVector& Cell,
    FFoundationBlockState State, TArray<UStaticMesh*, TInlineAllocator<4>>& OutMeshes,
    TArray<FTransform, TInlineAllocator<4>>& OutPlaces)
{
    OutMeshes.Reset();
    OutPlaces.Reset();
    const FFoundationBlockRegistry& Registry = Authority.World.GetRegistry();
    const FFoundationBlockType& Type = Registry.GetBlock(State);
    // Water's cell is never minable and never pops.
    if (State.IsAir() || Type.IsWater()) return;
    // The mesh pivot is the cell's minimum corner, as AddInstance places it.
    const FVector CellMin = FoundationGrid::CellMin(Cell, Authority.World.OriginCm);
    if (Registry.GetShape(State) == EFoundationBlockShape::Model)
    {
        // Each part's alternative picked at the cell and the whole state moved by its offset, as the view draws it.
        const FTransform Moved(CellMin + FoundationBlockOffsets::GetOffset(Type, Cell) * BreakEffectsBlockCm);
        TArray<TConstArrayView<int32>> Parts;
        Registry.GetModelParts(State, Parts);
        for (int32 Part = 0; Part < Parts.Num(); ++Part)
        {
            const TConstArrayView<int32> Alternatives = Parts[Part];
            if (Alternatives.IsEmpty()) continue;
            const int32 Model =
                Alternatives[VoxelModels::PickAlternative(Cell.X, Cell.Y, Cell.Z, Part, Alternatives.Num())];
            if (UStaticMesh* Mesh = View.GetBlockMesh(Registry, FoundationBlockMeshes::Count + Model))
            {
                OutMeshes.Add(Mesh);
                OutPlaces.Add(Moved);
            }
        }
        return;
    }
    if (UStaticMesh* Mesh = View.GetBlockMesh(Registry, FoundationBlockMeshes::GetKind(Registry, State, false)))
    {
        OutMeshes.Add(Mesh);
        OutPlaces.Add(FTransform(CellMin));
    }
}

bool FFoundationBreakEffects::OnBlockRemoved(const FFoundationAuthority& Authority, const FFoundationEvent& Event)
{
    LastBreakSpawnCount = 0;
    const bool bParticles = Switches.Particles == 1;
    if (!bParticles && Switches.Sounds == 0 && !Switches.bPop) return false;
    const FFoundationBlockState State = Event.State;
    const FBox CellBox = FoundationGrid::CellBox(Event.Cell, Authority.World.OriginCm);
    const FVector Centre = CellBox.GetCenter();

    const bool bCanDraw = bParticles && CanDrawBreakParticles(Assets, ParticleMesh);
    if (bCanDraw)
    {
        // The cell is already air: the boxes come from the removed state.
        TArray<FBox> Boxes;
        if (!Authority.World.GetOutlineBoxes(Event.Cell, State, Boxes)) Boxes.Add(CellBox);
        // Spray: out of the face the local player was mining (none for a pop), and away from the camera.
        const bool bMined = Event.Kind == EFoundationEventKind::Broken && bWasMining && Event.Cell == MiningCell;
        const FVector Normal = bMined ? FoundationGrid::FaceNormal(MinedFace) : FVector::ZeroVector;
        const FVector Push =
            Switches.bParticleSpray ? FoundationBreak::SprayPush(Normal, Centre - CameraLocation) : FVector::ZeroVector;
        float Layer = 0, Tint = 0;
        GetParticleLook(Authority, State, Layer, Tint);
        TArray<FoundationBreak::FParticleSpawn> Spawns;
        for (const FBox& Box : Boxes)
        {
            Spawns.Reset();
            FoundationBreak::SpawnBreakParticles(Box, Amount, Push, Random, Spawns);
            for (const FoundationBreak::FParticleSpawn& New : Spawns)
                Spawn(New, Layer, Tint);
            LastBreakSpawnCount += Spawns.Num();
        }
    }

    // Pops play the break sound too.
    if (Switches.Sounds != 0) PlaySound(Authority, State, Centre, false);

    if (Switches.bPop && !Pops.IsEmpty())
    {
        FPop* Slot = Pops.FindByPredicate([](const FPop& Pop) { return !Pop.bLive; });
        if (!Slot)
        {
            Slot = &Pops[0];
            for (FPop& Pop : Pops)
                if (Pop.Age > Slot->Age) Slot = &Pop;
        }
        TArray<UStaticMesh*, TInlineAllocator<4>> Meshes;
        TArray<FTransform, TInlineAllocator<4>> Places;
        GetStateParts(Authority, Event.Cell, State, Meshes, Places);
        const FFoundationBlockRegistry& Registry = Authority.World.GetRegistry();
        // The block's own layer material (GetStateParts never returns water, so never the water layer).
        const int32 RenderLayer = int32(Registry.GetBlock(State).Layer);
        UMaterialInterface* Material =
            View.LayerMaterials.IsValidIndex(RenderLayer) ? View.LayerMaterials[RenderLayer].Get() : nullptr;
        float Data[UFoundationInteractionView::CustomDataFloats];
        UFoundationInteractionView::MakeCustomData(Registry, State, Data);
        FBox Drawn(ForceInit);
        for (int32 Index = 0; Index < Meshes.Num(); ++Index)
            Drawn += Meshes[Index]->GetBoundingBox().ShiftBy(Places[Index].GetTranslation());
        Slot->Pivot = Drawn.IsValid ? Drawn.GetCenter() : Centre;
        Slot->Age = 0;
        Slot->bLive = false;
        // Each part's component sits on the pivot and its instance is placed relative to it, so UpdatePops scales
        // the whole state about the pivot by moving only the components.
        for (int32 Index = 0; Index < Slot->Parts.Num(); ++Index)
        {
            UInstancedStaticMeshComponent* Part = Slot->Parts[Index];
            Part->ClearInstances();
            const bool bUsed = Material && Meshes.IsValidIndex(Index);
            if (bUsed)
            {
                Part->SetStaticMesh(Meshes[Index]);
                Part->SetMaterial(0, Material);
                Part->SetWorldTransform(FTransform(Slot->Pivot));
                Part->SetCustomDataById(
                    Part->AddInstanceById(FTransform(Places[Index].GetTranslation() - Slot->Pivot), false), Data);
                Slot->bLive = true;
            }
            Part->SetVisibility(bUsed);
        }
    }
    return bCanDraw;
}

const FFoundationBreakBlock* FFoundationBreakEffects::FindBreakBlock(
    const FFoundationAuthority& Authority, FFoundationBlockState State) const
{
    if (!Assets) return nullptr;
    const int32* Index = BlockIndex.Find(Authority.World.GetBlock(State).Id);
    return Index && Assets->Blocks.IsValidIndex(*Index) ? &Assets->Blocks[*Index] : nullptr;
}

void FFoundationBreakEffects::GetParticleLook(
    const FFoundationAuthority& Authority, FFoundationBlockState State, float& OutLayer, float& OutTint) const
{
    OutLayer = 0;
    OutTint = float(BreakUntinted);
    const FFoundationBreakBlock* Block = FindBreakBlock(Authority, State);
    if (!Block) return;
    FName Particle = Block->Particle;
    bool bTinted = Block->bParticleTinted;
    const int32 Override = Block->OverrideStates.IndexOfByKey(int32(State.Id));
    if (Block->OverrideParticles.IsValidIndex(Override))
    {
        Particle = Block->OverrideParticles[Override];
        bTinted = Block->OverrideTinted.IsValidIndex(Override) && Block->OverrideTinted[Override];
    }
    // LoadAssets resolved every listed name (a missing one to layer 0, with its warning).
    if (const int32* Layer = TextureLayers.Find(Particle)) OutLayer = float(*Layer);
    // 24-bit colors are exact in a float. Alpha 0 is a block with no tint.
    const FColor Color = Authority.World.GetBlock(State).Tint;
    if (bTinted && Color.A != 0) OutTint = float(uint32(Color.R) << 16 | uint32(Color.G) << 8 | Color.B);
}

void FFoundationBreakEffects::PlaySound(
    const FFoundationAuthority& Authority, FFoundationBlockState State, const FVector& Location, bool bHit)
{
    const FFoundationBreakBlock* Block = FindBreakBlock(Authority, State);
    const int32 EventIndex = Block ? (bHit ? Block->HitEvent : Block->BreakEvent) : INDEX_NONE;
    // INDEX_NONE is silent (water's intentionally_empty).
    if (!Block || !Assets->Events.IsValidIndex(EventIndex)) return;
    const FFoundationBreakSoundEvent& Event = Assets->Events[EventIndex];
    const FoundationBreak::FSoundPlay Play = FoundationBreak::PickSound(*Block, Event, bHit, Switches.Sounds, Random);
    if (!Event.Files.IsValidIndex(Play.File)) return;
    const FFoundationBreakSoundFile& File = Event.Files[Play.File];
    UWorld* World = GetWorld();
    // Logged even when nothing can play: the probes run with -nosound.
    FoundationBreak::FSoundLogEntry Entry;
    Entry.Time = World ? World->GetTimeSeconds() : 0;
    Entry.Event = Event.Name;
    Entry.File = File.Name;
    Entry.Volume = Play.Volume;
    Entry.Pitch = Play.Pitch;
    Entry.Location = Location;
    Entry.bHit = bHit;
    SoundLog.Add(Entry);
    // Past the cap the oldest half goes in one RemoveAt, so the shift is amortised over many sounds.
    if (SoundLog.Num() > MaxSoundLog) SoundLog.RemoveAt(0, SoundLog.Num() / 2);
    USoundWave* Wave = File.Wave.Get();
    if (!Wave || !World || !World->GetAudioDeviceRaw()) return;
    // Sounds 2 caps hits at two voices (HitConcurrency).
    UGameplayStatics::SpawnSoundAtLocation(World, Wave, Location, FRotator::ZeroRotator, Play.Volume, Play.Pitch, 0,
        Assets->Attenuation.Get(), bHit && Switches.Sounds == 2 ? Assets->HitConcurrency.Get() : nullptr, true);
}

void FFoundationBreakEffects::Spawn(const FoundationBreak::FParticleSpawn& New, float Layer, float Tint)
{
    if (!ParticleMesh || !ParticleIds.IsValidIndex(NextParticle) || !Particles.IsValidIndex(NextParticle)) return;
    // A ring: when the pool is full the oldest spawn gives way.
    const int32 Index = NextParticle;
    NextParticle = (NextParticle + 1) % Particles.Num();
    FParticle& Particle = Particles[Index];
    Particle.State.Location = New.Location;
    Particle.State.Velocity = New.Velocity;
    Particle.State.bResting = false;
    Particle.Age = 0;
    Particle.Life = New.Life;
    Particle.Size = New.Size;
    Particle.bLive = true;
    // Exact in a float: the layer, the 4 x 4 patch's texel offset (u | v << 4) and the packed tint.
    const float Data[3] = {Layer, float(uint32(New.PatchU) | uint32(New.PatchV) << 4), Tint};
    ParticleMesh->SetCustomDataById(ParticleIds[Index], Data);
    // UpdateParticles places it this frame; the component stays hidden while no particle lives.
    ParticleMesh->SetVisibility(true);
}

bool FFoundationBreakEffects::GetSwing(double& OutSwing) const
{
    if (Switches.Swing == 0) return false;
    const double Alpha = FMath::Min(StepAlphaSeconds / FoundationBreak::StepSeconds, .999);
    const double Phase = Swing.GetPhase(LastStep, Alpha);
    OutSwing = Phase < 0 ? 0 : FMath::Sin(Phase * UE_DOUBLE_PI);
    return true;
}

void FFoundationBreakEffects::EndFrame(const FFoundationAuthority& Authority, double DeltaTime)
{
    DrawOutlineStrips();
    UpdateParticles(Authority, DeltaTime);
    UpdatePops(DeltaTime);
}

void FFoundationBreakEffects::DrawOutlineStrips()
{
    const int32 Mode = Switches.Outline;
    // Outline 3 draws nothing; nor does a missing M_BreakOutline.
    const bool bDraw = (Mode == 1 || Mode == 2) && bOutlineSubmitted && HasBreakMaterial(OutlineCore) && OutlineHalo;
    if (!bDraw)
    {
        if (bOutlineVisible)
            for (UInstancedStaticMeshComponent* Strips : {OutlineHalo, OutlineCore})
            {
                Strips->ClearInstances();
                Strips->SetVisibility(false);
            }
        bOutlineVisible = false;
        return;
    }
    // Vanilla: black 40% at 3 px, or the thinner black 30% at 1 px. High contrast: a 3 px cyan core, opaque, on a
    // 9 px black halo (3 px each side).
    const double CorePixels = bHighContrast || Mode == 1 ? 3 : 1;
    const FLinearColor CoreColor = bHighContrast ? FLinearColor::FromSRGBColor(FColor(87, 255, 225))
                                                 : FLinearColor(0, 0, 0, Mode == 1 ? .4f : .3f);
    TArray<FTransform> Core, Halo;
    for (const FBox& Submitted : OutlineBoxes)
    {
        const FBox Box = Submitted.ExpandBy(OutlineExpandCm);
        const FVector Size = Box.GetSize();
        // The 12 edges: four along each axis, at the min/max corners of the other two.
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
            const double Segment = Size[Axis] / OutlineSegments;
            for (int32 Corner = 0; Corner < 4; ++Corner)
            {
                FVector Start = Box.Min;
                Start[U] = (Corner & 1) ? Box.Max[U] : Box.Min[U];
                Start[V] = (Corner & 2) ? Box.Max[V] : Box.Min[V];
                for (int32 Piece = 0; Piece < OutlineSegments; ++Piece)
                {
                    FVector Centre = Start;
                    Centre[Axis] += Segment * (Piece + .5);
                    // A constant screen width: the strip's world width at its own view depth. The extra width along
                    // the edge closes the corners.
                    const double Depth = FMath::Max(1., FVector::DotProduct(Centre - CameraLocation, CameraForward));
                    auto Strip = [&](double Pixels)
                    {
                        const double Width = FoundationBreak::PixelsToWorld(Pixels, Depth, CameraFov, ViewportWidth);
                        // The pieces tile the edge exactly; only the first and last reach Width/2 past its ends, so
                        // the translucent core overlaps at the corners alone.
                        const double Back = Piece == 0 ? Width / 2 : 0;
                        const double Forward = Piece == OutlineSegments - 1 ? Width / 2 : 0;
                        FVector Scale(Width);
                        Scale[Axis] = Segment + Back + Forward;
                        FVector Placed = Centre;
                        Placed[Axis] += (Forward - Back) / 2;
                        return FTransform(FQuat::Identity, Placed, Scale / BreakEffectsBlockCm);
                    };
                    Core.Add(Strip(CorePixels));
                    if (bHighContrast) Halo.Add(Strip(OutlineHaloPixels));
                }
            }
        }
    }
    // The same strip count moves the instances in place; a different one rebuilds them (under 200 per frame).
    // The colour is written only after a rebuild or a change, the last write marking the render state dirty.
    auto Apply = [](UInstancedStaticMeshComponent* Strips, const TArray<FTransform>& Transforms, FLinearColor Color,
                     FLinearColor& LastColor)
    {
        bool bRebuilt = false;
        if (Strips->GetInstanceCount() != Transforms.Num())
        {
            Strips->ClearInstances();
            if (!Transforms.IsEmpty()) Strips->AddInstances(Transforms, false, true, false);
            bRebuilt = true;
        }
        else if (!Transforms.IsEmpty())
            Strips->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
        if (!Transforms.IsEmpty() && (bRebuilt || !Color.Equals(LastColor, 0.f)))
        {
            const float Data[4] = {Color.R, Color.G, Color.B, Color.A};
            for (int32 Index = 0; Index < Transforms.Num(); ++Index)
                Strips->SetCustomData(Index, Data, Index == Transforms.Num() - 1);
            LastColor = Color;
        }
        Strips->SetVisibility(!Transforms.IsEmpty());
    };
    Apply(OutlineHalo, Halo, FLinearColor::Black, LastHaloColor);
    Apply(OutlineCore, Core, CoreColor, LastCoreColor);
    bOutlineVisible = !Core.IsEmpty();
    if (bOutlineVisible) bDrawnHighContrast = bHighContrast;
}

void FFoundationBreakEffects::UpdateParticles(const FFoundationAuthority& Authority, double DeltaTime)
{
    // Hidden means no particle lives (Spawn shows the component, the last death hides it): nothing to do.
    if (!ParticleMesh || !ParticleMesh->GetVisibleFlag()) return;
    // Both worlds keep their blocks in the authority's block world.
    const FFoundationBlockWorld& Blocks = Authority.World;
    auto Solid = [&Blocks](const FVector& Point, TArray<FBox>& OutBoxes)
    {
        const FIntVector Cell = FoundationGrid::CellAt(Point, Blocks.OriginCm);
        const FFoundationBlockState State = Blocks.Get(Cell).State;
        if (!State.IsAir()) Blocks.GetCollisionBoxes(Cell, State, OutBoxes);
    };
    // Camera-facing quads: the plane's +Z normal toward the camera.
    const FQuat Facing = FRotationMatrix::MakeFromZ(-CameraForward).ToQuat();
    int32 Live = 0;
    for (int32 Index = 0; Index < Particles.Num(); ++Index)
    {
        FParticle& Particle = Particles[Index];
        if (!Particle.bLive) continue;
        Particle.Age += DeltaTime;
        if (Particle.Age >= Particle.Life)
        {
            Particle.bLive = false;
            ParticleMesh->UpdateInstanceTransformById(
                ParticleIds[Index], FTransform(FQuat::Identity, Particle.State.Location, FVector::ZeroVector), true);
            continue;
        }
        FoundationBreak::StepParticle(Particle.State, DeltaTime, Switches.bParticleCollision, Solid);
        ParticleMesh->UpdateInstanceTransformById(ParticleIds[Index],
            FTransform(Facing, Particle.State.Location, FVector(Particle.Size / BreakEffectsBlockCm)), true);
        ++Live;
    }
    // The id updates mark only the changed instances: one incremental upload at the end of the frame.
    if (Live == 0) ParticleMesh->SetVisibility(false);
}

void FFoundationBreakEffects::UpdatePops(double DeltaTime)
{
    for (FPop& Pop : Pops)
    {
        if (!Pop.bLive) continue;
        Pop.Age += DeltaTime;
        if (Pop.Age >= FoundationBreak::PopSeconds)
        {
            Pop.bLive = false;
            for (UInstancedStaticMeshComponent* Part : Pop.Parts)
                Part->SetVisibility(false);
            continue;
        }
        // Uniform about the pivot: the components sit on it and carry the scale.
        const FTransform Scaled(FQuat::Identity, Pop.Pivot, FVector(FoundationBreak::PopScale(Pop.Age)));
        for (UInstancedStaticMeshComponent* Part : Pop.Parts)
            if (Part->GetVisibleFlag()) Part->SetWorldTransform(Scaled);
    }
}

int32 FFoundationBreakEffects::GetLiveParticleCount() const
{
    int32 Count = 0;
    for (const FParticle& Particle : Particles)
        Count += Particle.bLive ? 1 : 0;
    return Count;
}

bool FFoundationBreakEffects::IsPopVisible() const
{
    return Pops.ContainsByPredicate([](const FPop& Pop) { return Pop.bLive; });
}
