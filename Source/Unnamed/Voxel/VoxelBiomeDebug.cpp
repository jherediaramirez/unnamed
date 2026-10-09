#include "Voxel/VoxelBiomeDebug.h"
#include "Algo/Find.h"
#include "Async/ParallelFor.h"
#include "Biome/VoxelBiomeSource.h"
#include "Debug/DebugDrawService.h"
#include "Dom/JsonObject.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Random/VoxelSeed.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tasks/Task.h"
#include "Terrain/VoxelTerrain.h"
#include "UObject/StrongObjectPtr.h"
#include "Voxel/VoxelClimateDebug.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelWorldSettings.h"
#include "Voxel/VoxelWorldgenReader.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"

namespace
{
    const TCHAR* const BiomeDebugPresets[] = {TEXT("overworld"), TEXT("large_biomes"), TEXT("amplified")};

    struct FBiomeDebugCacheEntry
    {
        FString Preset;
        uint64 Seed = 0;
        TSharedPtr<const FVoxelBiomeSource> Source;
        // Why Source is null.
        FString Error;
    };
    TArray<FBiomeDebugCacheEntry> BiomeDebugCache;

    // Each biome's map colour, by table index.
    TArray<FColor> BiomeDebugColors(const FVoxelBiomeTable& Table, TArray<FString>* OutUnlisted = nullptr)
    {
        TArray<FColor> Colors;
        for (const FVoxelBiome& Biome : Table.Biomes)
        {
            bool bListed = true;
            Colors.Add(VoxelBiomeDebug::GetColor(Biome.Name, &bListed));
            if (!bListed && OutUnlisted) OutUnlisted->Add(Biome.Name);
        }
        return Colors;
    }

    // The biome index at each of Count points (X[I], Y[I]), at height Z[I] or, with Z null, at depth 0.
    void BiomeDebugSample(const FVoxelBiomeSource& Source, const double* X, const double* Y, const double* Z,
        int32 Count, FVoxelDensityScratch& Scratch, uint8* Out)
    {
        if (Source.IsFixed())
        {
            FMemory::Memset(Out, uint8(Source.GetBiome(FVoxelBiomeMatch())), Count);
            return;
        }
        TArray<FVoxelBiomeTarget> Targets;
        Targets.SetNumUninitialized(Count);
        Source.SampleTargets(X, Y, Z, Count, Scratch, Targets);
        for (int32 Index = 0; Index < Count; ++Index)
            Out[Index] = uint8(Source.GetBiome(Source.Find(Targets[Index])));
    }

    // Each biome's share among the jar's lookup samples at depth 0, by biome id; empty when the facts cannot be read.
    TMap<FString, double> BiomeDebugJarShares()
    {
        TMap<FString, double> Shares;
        const FString Facts = FPaths::Combine(VoxelWorldgen::GetReferenceRoot(), TEXT("facts"));
        FString SamplesText, ParametersText;
        TSharedPtr<FJsonObject> Samples, Parameters;
        const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Biomes = nullptr;
        if (!FFileHelper::LoadFileToString(SamplesText, *(Facts / TEXT("biome_lookup_samples.json"))) ||
            !FFileHelper::LoadFileToString(ParametersText, *(Facts / TEXT("biome_parameters.json"))) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SamplesText), Samples) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ParametersText), Parameters) ||
            !Samples->TryGetArrayField(TEXT("samples"), Rows) || !Parameters->TryGetArrayField(TEXT("biomes"), Biomes))
            return Shares;
        int32 Surface = 0;
        for (const TSharedPtr<FJsonValue>& Row : *Rows)
        {
            const TArray<TSharedPtr<FJsonValue>>& Values = Row->AsArray();
            if (Values.Num() < 7 || Values[4]->AsNumber() != 0) continue;
            const int32 Biome = int32(Values[6]->AsNumber());
            if (!Biomes->IsValidIndex(Biome)) continue;
            Shares.FindOrAdd((*Biomes)[Biome]->AsString()) += 1;
            ++Surface;
        }
        for (TPair<FString, double>& Share : Shares)
            Share.Value /= FMath::Max(Surface, 1);
        return Shares;
    }

    // The voxel world's seed, or the dev seed outside one.
    uint64 BiomeDebugSeed(UWorld* World)
    {
        if (const AVoxelWorld* Voxels = World ? AVoxelWorld::Find(World) : nullptr) return Voxels->GetSeed().Value;
        return FVoxelSeed::Parse(UVoxelWorldSettings::Get().DevSeed).Value;
    }

    // The first local player's view point in blocks: its cell is the floor of each axis.
    bool BiomeDebugViewPoint(const APlayerController* Controller, FVector& OutBlocks, FRotator& OutRotation)
    {
        if (!Controller) return false;
        FVector Location;
        Controller->GetPlayerViewPoint(Location, OutRotation);
        const AVoxelWorld* Voxels = AVoxelWorld::Find(Controller->GetWorld());
        OutBlocks = (Location - (Voxels ? Voxels->OriginCm : FVector::ZeroVector)) / 100.;
        return true;
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice BiomeMapCommand(TEXT("voxel.BiomeMap"),
        TEXT("voxel.BiomeMap [size px, default 1024] [blocks per px, default 8] [preset: "
             "overworld|large_biomes|amplified] "
             "[surface|<y>]: writes a top-down biome map centered on the player (or 0, 0) to Saved/VoxelClimate, with "
             "a CSV of each biome's share beside it. Surface reads each point at depth 0, the climate's own surface; a "
             "number reads that height. Colours: UVoxelWorldSettings::BiomeMapColors."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
            [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
            {
                VoxelBiomeDebug::FMapOptions Options;
                if (Args.Num() > 0) Options.Size = FMath::Clamp(FCString::Atoi(*Args[0]), 16, 8192);
                if (Args.Num() > 1) Options.BlocksPerPixel = FMath::Clamp(FCString::Atod(*Args[1]), .25, 1024.);
                const FString Preset = Args.Num() > 2 ? Args[2] : TEXT("overworld");
                if (Args.Num() > 3 && !Args[3].Equals(TEXT("surface"), ESearchCase::IgnoreCase))
                {
                    if (!Args[3].IsNumeric())
                    {
                        Output.Log(ELogVerbosity::Error, TEXT("voxel.BiomeMap: the height is surface or a number"));
                        return;
                    }
                    Options.Height = FCString::Atod(*Args[3]);
                }
                FVector Blocks;
                FRotator Rotation;
                if (BiomeDebugViewPoint(World ? World->GetFirstPlayerController() : nullptr, Blocks, Rotation))
                    Options.CenterX = Blocks.X, Options.CenterY = Blocks.Y;

                const uint64 Seed = BiomeDebugSeed(World);
                FString Error;
                const TSharedPtr<const FVoxelBiomeSource> Source = VoxelBiomeDebug::GetSource(Preset, Seed, Error);
                if (!Source)
                {
                    Output.Log(ELogVerbosity::Error, Error);
                    return;
                }
                const FString Path = FPaths::ProjectSavedDir() / TEXT("VoxelClimate") /
                    FString::Printf(TEXT("%s-%lld-biomes-%d-%g-%s.png"), *Preset, int64(Seed), Options.Size,
                        Options.BlocksPerPixel,
                        Options.Height ? *FString::Printf(TEXT("y%g"), *Options.Height) : TEXT("surface"));
                FString Summary;
                if (VoxelBiomeDebug::WriteMap(*Source, Options, Path, Summary, Error))
                {
                    Output.Log(Summary);
                    UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Summary);
                }
                else
                    Output.Log(ELogVerbosity::Error, Error);
            }));

    TAutoConsoleVariable<bool> CVarDebugBiomes(TEXT("voxel.Debug.Biomes"), false,
        TEXT(
            "Shows the biome at the player on screen: the one its loaded column stores for the player's block, and the "
            "six climate targets there with the biome they pick. Debug only, not a setting."));
    TAutoConsoleVariable<FString> CVarDebugMinimap(TEXT("voxel.Debug.Minimap"), TEXT("off"),
        TEXT("Shows a 256-pixel map around the player in the top right corner, north (-Y) up: biomes (at depth 0, the "
             "surface), or one climate parameter (C|E|W|PV|T|V|O|F|J, as voxel.ClimateMap draws them); off hides it. "
             "It reads the world's own terrain. Debug only, not a setting."));
    TAutoConsoleVariable<float> CVarDebugMinimapScale(
        TEXT("voxel.Debug.MinimapScale"), 4.f, TEXT("Blocks per pixel of voxel.Debug.Minimap (256 pixels across)."));

    constexpr int32 BiomeMinimapSize = 256;
    constexpr float BiomeOverlayMargin = 16;

    // What a minimap image shows.
    struct FBiomeMinimapView
    {
        FString Mode;
        double CenterX = 0, CenterY = 0, Scale = 0;
        // Weak, so a later terrain at a freed one's address still counts as another terrain.
        TWeakPtr<const IVoxelTerrain> Terrain;
    };

    // The on-screen views' state, alive only while one is on.
    struct FBiomeOverlay
    {
        FDelegateHandle DrawHandle;
        TStrongObjectPtr<UTexture2D> Texture;
        FBiomeMinimapView Shown;
        bool bShown = false;
        FBiomeMinimapView Pending;
        UE::Tasks::TTask<TArray<FColor>> Task;
        bool bTask = false;
    };
    TUniquePtr<FBiomeOverlay> BiomeOverlay;

    // Starts the minimap image for View on the workers.
    UE::Tasks::TTask<TArray<FColor>> BiomeMinimapLaunch(
        TSharedPtr<const IVoxelTerrain> Terrain, const FBiomeMinimapView& View, TArray<FColor> Colors)
    {
        EVoxelClimate Parameter = EVoxelClimate::Continents;
        const bool bBiomes = !FVoxelClimateSampler::ParseLabel(View.Mode, Parameter);
        return UE::Tasks::Launch(UE_SOURCE_LOCATION,
            [Terrain, View, Colors = MoveTemp(Colors), bBiomes, Parameter]()
            {
                const FVoxelBiomeSource& Source = *Terrain->GetBiomeSource();
                TArray<FColor> Pixels;
                Pixels.SetNumUninitialized(BiomeMinimapSize * BiomeMinimapSize);
                ParallelFor(BiomeMinimapSize,
                    [&](int32 Row)
                    {
                        double X[BiomeMinimapSize], Y[BiomeMinimapSize];
                        for (int32 Column = 0; Column < BiomeMinimapSize; ++Column)
                        {
                            X[Column] = View.CenterX + (Column - BiomeMinimapSize / 2) * View.Scale;
                            Y[Column] = View.CenterY + (Row - BiomeMinimapSize / 2) * View.Scale;
                        }
                        FVoxelDensityScratch Scratch;
                        FColor* Out = Pixels.GetData() + Row * BiomeMinimapSize;
                        if (bBiomes)
                        {
                            uint8 Biomes[BiomeMinimapSize];
                            BiomeDebugSample(Source, X, Y, nullptr, BiomeMinimapSize, Scratch, Biomes);
                            for (int32 Column = 0; Column < BiomeMinimapSize; ++Column)
                                Out[Column] =
                                    Colors.IsValidIndex(Biomes[Column]) ? Colors[Biomes[Column]] : FColor::Black;
                            return;
                        }
                        TArray<FVoxelClimateSample> Samples;
                        Samples.SetNumUninitialized(BiomeMinimapSize);
                        Source.GetClimate().Sample(X, Y, BiomeMinimapSize, Scratch, Samples);
                        for (int32 Column = 0; Column < BiomeMinimapSize; ++Column)
                            Out[Column] = VoxelClimateDebug::GetColor(Parameter, Samples[Column][Parameter]);
                    });
                return Pixels;
            });
    }

    void BiomeMinimapUpload(FBiomeOverlay& Overlay, const TArray<FColor>& Pixels)
    {
        if (!Overlay.Texture)
        {
            UTexture2D* Texture = UTexture2D::CreateTransient(BiomeMinimapSize, BiomeMinimapSize, PF_B8G8R8A8);
            Texture->Filter = TF_Nearest;
            Texture->SRGB = true;
            Overlay.Texture.Reset(Texture);
        }
        FTexture2DMipMap& Mip = Overlay.Texture->GetPlatformData()->Mips[0];
        void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
        Mip.BulkData.Unlock();
        Overlay.Texture->UpdateResource();
    }

    void BiomeOverlayText(UCanvas* Canvas, float X, float& Y, const FString& Line, const FColor& Color = FColor::White)
    {
        FFontRenderInfo Info;
        Info.bEnableShadow = true;
        Canvas->SetDrawColor(Color);
        Canvas->DrawText(GEngine->GetSmallFont(), Line, X, Y, 1, 1, Info);
        Y += 14;
    }

    // Updates and draws the minimap in the top right corner; returns the y below it.
    float BiomeMinimapDraw(UCanvas* Canvas, FBiomeOverlay& Overlay, TSharedPtr<const IVoxelTerrain> Terrain,
        const FVoxelBiomeSource& Source, const FVector& Blocks, const FRotator& Rotation)
    {
        const float Left = Canvas->ClipX - BiomeMinimapSize - BiomeOverlayMargin, Top = BiomeOverlayMargin;
        float Y = Top + BiomeMinimapSize + 4;
        FString Mode = CVarDebugMinimap.GetValueOnGameThread();
        EVoxelClimate Parameter;
        const bool bClimate = FVoxelClimateSampler::ParseLabel(Mode, Parameter);
        if (!bClimate && !Mode.Equals(TEXT("biomes"), ESearchCase::IgnoreCase))
        {
            BiomeOverlayText(
                Canvas, Left, Y, TEXT("voxel.Debug.Minimap: biomes, C, E, W, PV, T, V, O, F, J or off"), FColor::Red);
            return Y;
        }
        if (bClimate && Source.IsFixed())
        {
            BiomeOverlayText(Canvas, Left, Y, TEXT("Minimap: a fixed biome has no climate"), FColor::Red);
            return Y;
        }
        Mode = bClimate ? FString(FVoxelClimateSampler::GetLabel(Parameter)) : TEXT("biomes");

        if (Overlay.bTask && Overlay.Task.IsCompleted())
        {
            BiomeMinimapUpload(Overlay, Overlay.Task.GetResult());
            Overlay.Shown = Overlay.Pending;
            Overlay.bShown = true;
            Overlay.bTask = false;
        }
        const double Scale = FMath::Clamp(double(CVarDebugMinimapScale.GetValueOnGameThread()), .25, 256.);
        const double Span = BiomeMinimapSize * Scale;
        const FBiomeMinimapView& Shown = Overlay.Shown;
        const bool bStale = !Overlay.bShown || Shown.Mode != Mode || Shown.Scale != Scale ||
            Shown.Terrain.Pin() != Terrain || FMath::Abs(Blocks.X - Shown.CenterX) > Span / 8 ||
            FMath::Abs(Blocks.Y - Shown.CenterY) > Span / 8;
        if (bStale && !Overlay.bTask)
        {
            // Centers snap to whole pixels, so a redraw never shifts the image by part of one.
            Overlay.Pending = {Mode, FMath::RoundToDouble(Blocks.X / Scale) * Scale,
                FMath::RoundToDouble(Blocks.Y / Scale) * Scale, Scale, Terrain};
            Overlay.Task = BiomeMinimapLaunch(Terrain, Overlay.Pending, BiomeDebugColors(Source.GetTable()));
            Overlay.bTask = true;
        }
        if (!Overlay.bShown || !Overlay.Texture)
        {
            BiomeOverlayText(Canvas, Left, Y, TEXT("Minimap: drawing..."));
            return Y;
        }

        Canvas->SetDrawColor(FColor::White);
        Canvas->DrawTile(Overlay.Texture.Get(), Left, Top, BiomeMinimapSize, BiomeMinimapSize, 0, 0, BiomeMinimapSize,
            BiomeMinimapSize, BLEND_Opaque);
        Canvas->K2_DrawBox(FVector2D(Left, Top), FVector2D(double(BiomeMinimapSize)), 1, FLinearColor::Black);
        // The player, where it stands on the shown image, and its heading. Y points south: down, as on screen.
        const FVector2D Center(Left + BiomeMinimapSize / 2., Top + BiomeMinimapSize / 2.);
        const FVector2D Player(FMath::Clamp(Center.X + (Blocks.X - Shown.CenterX) / Shown.Scale, double(Left),
                                   double(Left + BiomeMinimapSize)),
            FMath::Clamp(
                Center.Y + (Blocks.Y - Shown.CenterY) / Shown.Scale, double(Top), double(Top + BiomeMinimapSize)));
        const double Yaw = FMath::DegreesToRadians(Rotation.Yaw);
        Canvas->K2_DrawLine(Player, Player + FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw)) * 12, 2, FLinearColor::Red);
        Canvas->K2_DrawBox(Player - FVector2D(3.), FVector2D(6.), 2, FLinearColor::White);
        BiomeOverlayText(Canvas, Left, Y,
            FString::Printf(TEXT("Minimap: %s, %g blocks/px, %.0f blocks across"), *Shown.Mode, Shown.Scale,
                BiomeMinimapSize * Shown.Scale));
        return Y;
    }

    // The readout lines at the player.
    void BiomeReadoutDraw(
        UCanvas* Canvas, float Y, const AVoxelWorld& Voxels, const FVoxelBiomeSource& Source, const FVector& Blocks)
    {
        const float X = BiomeOverlayMargin;
        const FVoxelBiomeTable& Table = Source.GetTable();
        const FIntVector Cell(
            FMath::FloorToInt32(Blocks.X), FMath::FloorToInt32(Blocks.Y), FMath::FloorToInt32(Blocks.Z));
        const int32 Stored = Voxels.GetBiome(Cell);
        BiomeOverlayText(Canvas, X, Y,
            FString::Printf(TEXT("Biome at %d %d %d: %s"), Cell.X, Cell.Y, Cell.Z,
                Table.Biomes.IsValidIndex(Stored) ? *Table.Biomes[Stored].Name
                    : Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ
                    ? TEXT("outside the world's height")
                    : TEXT("column not loaded")));
        if (Source.IsFixed())
        {
            BiomeOverlayText(Canvas, X, Y, TEXT("Fixed biome: no climate"));
            return;
        }
        FVoxelDensityScratch Scratch;
        FVoxelBiomeTarget Target;
        const double PX = Blocks.X, PY = Blocks.Y, PZ = Blocks.Z;
        Source.SampleTargets(&PX, &PY, &PZ, 1, Scratch, MakeArrayView(&Target, 1));
        const FVoxelBiomeMatch Match = Source.Find(Target);
        const int32 Picked = Source.GetBiome(Match);
        const auto Value = [&Target](EVoxelClimateAxis Axis)
        { return double(Target[Axis]) / FVoxelBiomeTable::Quantization; };
        BiomeOverlayText(Canvas, X, Y,
            FString::Printf(TEXT("Climate here: T %.4f  H %.4f  C %.4f  E %.4f  D %.4f  W %.4f -> %s (fitness %lld)"),
                Value(EVoxelClimateAxis::Temperature), Value(EVoxelClimateAxis::Humidity),
                Value(EVoxelClimateAxis::Continentalness), Value(EVoxelClimateAxis::Erosion),
                Value(EVoxelClimateAxis::Depth), Value(EVoxelClimateAxis::Weirdness),
                Table.Biomes.IsValidIndex(Picked) ? *Table.Biomes[Picked].Name : TEXT("none"), Match.Fitness));
    }

    void DrawBiomeOverlay(UCanvas* Canvas, APlayerController* Controller)
    {
        if (!Canvas || !BiomeOverlay || !GEngine) return;
        FVector Blocks;
        FRotator Rotation;
        if (!BiomeDebugViewPoint(Controller, Blocks, Rotation)) return;
        const AVoxelWorld* Voxels = AVoxelWorld::Find(Controller->GetWorld());
        const TSharedPtr<const IVoxelTerrain> Terrain = Voxels ? Voxels->GetTerrain() : nullptr;
        const FVoxelBiomeSource* Source = Terrain ? Terrain->GetBiomeSource() : nullptr;
        float Y = BiomeOverlayMargin;
        if (!Source)
        {
            BiomeOverlayText(Canvas, BiomeOverlayMargin, Y,
                Voxels ? TEXT("Biome views: this world's terrain has no biomes")
                       : TEXT("Biome views: not a voxel world"),
                FColor::Red);
            return;
        }
        if (!CVarDebugMinimap.GetValueOnGameThread().Equals(TEXT("off"), ESearchCase::IgnoreCase))
            BiomeMinimapDraw(Canvas, *BiomeOverlay, Terrain, *Source, Blocks, Rotation);
        // Below the on-screen debug messages (voxel.Debug.Climate's line among them).
        if (CVarDebugBiomes.GetValueOnGameThread())
            BiomeReadoutDraw(Canvas, Canvas->ClipY * .25f, *Voxels, *Source, Blocks);
    }

    // Set once the engine starts exiting: the views stay down from then on.
    bool bBiomeDebugExiting = false;

    void BiomeOverlayRelease()
    {
        if (!BiomeOverlay) return;
        UDebugDrawService::Unregister(BiomeOverlay->DrawHandle);
        // A task in flight keeps its own copies and finishes alone; its image is dropped.
        BiomeOverlay.Reset();
    }

    // Registers the draw hook while a view is on and drops everything when none is.
    void BiomeOverlayRefresh(IConsoleVariable*)
    {
        const bool bOn = VoxelBiomeDebug::IsOverlayOn() && !bBiomeDebugExiting;
        if (bOn && !BiomeOverlay)
        {
            BiomeOverlay = MakeUnique<FBiomeOverlay>();
            BiomeOverlay->DrawHandle =
                UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&DrawBiomeOverlay));
        }
        else if (!bOn)
            BiomeOverlayRelease();
    }

    FDelayedAutoRegisterHelper BiomeDebugRegister(EDelayedRegisterRunPhase::EndOfEngineInit,
        []
        {
            for (IConsoleVariable* Variable : {CVarDebugBiomes.AsVariable(), CVarDebugMinimap.AsVariable()})
                Variable->SetOnChangedCallback(FConsoleVariableDelegate::CreateStatic(&BiomeOverlayRefresh));
            BiomeOverlayRefresh(nullptr);
            // The texture is a UObject: let it go before the object system shuts down.
            FCoreDelegates::OnEnginePreExit.AddLambda(
                []
                {
                    bBiomeDebugExiting = true;
                    BiomeOverlayRelease();
                });
        });
} // namespace

bool VoxelBiomeDebug::IsOverlayOn()
{
    return CVarDebugBiomes.GetValueOnGameThread() ||
        !CVarDebugMinimap.GetValueOnGameThread().Equals(TEXT("off"), ESearchCase::IgnoreCase);
}

FColor VoxelBiomeDebug::GetColor(const FString& Biome, bool* bOutListed)
{
    for (const FVoxelBiomeMapColor& Entry : UVoxelWorldSettings::Get().BiomeMapColors)
        if (Entry.Biome.Equals(Biome, ESearchCase::CaseSensitive))
        {
            if (bOutListed) *bOutListed = true;
            return Entry.Color;
        }
    if (bOutListed) *bOutListed = false;
    const uint32 Hash = GetTypeHash(Biome);
    return FColor(uint8(Hash), uint8(Hash >> 8), uint8(Hash >> 16));
}

TSharedPtr<const FVoxelBiomeSource> VoxelBiomeDebug::GetSource(const FString& Preset, uint64 Seed, FString& OutError)
{
    check(IsInGameThread());
    for (const FBiomeDebugCacheEntry& Entry : BiomeDebugCache)
        if (Entry.Preset == Preset && Entry.Seed == Seed)
        {
            OutError = Entry.Error;
            return Entry.Source;
        }
    FBiomeDebugCacheEntry& Entry = BiomeDebugCache.AddDefaulted_GetRef();
    Entry.Preset = Preset;
    Entry.Seed = Seed;
    const FString Root = VoxelWorldgen::GetReferenceRoot();
    FVoxelTerrainPreset Data;
    FVoxelBiomeTable Table;
    if (!Algo::FindByPredicate(BiomeDebugPresets,
            [&Preset](const TCHAR* Name) { return Preset.Equals(Name, ESearchCase::CaseSensitive); }))
        Entry.Error = FString::Printf(TEXT("unknown preset %s: overworld, large_biomes or amplified"), *Preset);
    else if (VoxelWorldgen::ReadPreset(Root, Preset, Data, Entry.Error) &&
        VoxelWorldgen::ReadBiomeParameters(Root, TEXT("overworld"), Table, Entry.Error))
    {
        const TSharedRef<FVoxelBiomeSource> Source = MakeShared<FVoxelBiomeSource>();
        if (Source->Initialize(Table, Data, Seed, &Entry.Error)) Entry.Source = Source;
    }
    OutError = Entry.Error;
    return Entry.Source;
}

bool VoxelBiomeDebug::WriteMap(const FVoxelBiomeSource& Source, const FMapOptions& Options, const FString& Path,
    FString& OutSummary, FString& OutError)
{
    const int32 Size = Options.Size;
    TArray<uint8> Biomes;
    Biomes.SetNumUninitialized(Size * Size);
    const double Start = FPlatformTime::Seconds();
    ParallelFor(Size,
        [&](int32 Row)
        {
            TArray<double> X, Y, Z;
            X.SetNumUninitialized(Size);
            Y.SetNumUninitialized(Size);
            Z.Init(Options.Height.Get(0), Size);
            // Y points south, so rows run north to south.
            const double BlockY = Options.CenterY + (Row - Size / 2) * Options.BlocksPerPixel;
            for (int32 Column = 0; Column < Size; ++Column)
            {
                X[Column] = Options.CenterX + (Column - Size / 2) * Options.BlocksPerPixel;
                Y[Column] = BlockY;
            }
            FVoxelDensityScratch Scratch;
            BiomeDebugSample(Source, X.GetData(), Y.GetData(), Options.Height ? Z.GetData() : nullptr, Size, Scratch,
                Biomes.GetData() + Row * Size);
        });
    const double Seconds = FPlatformTime::Seconds() - Start;

    const FVoxelBiomeTable& Table = Source.GetTable();
    TArray<FString> Unlisted;
    const TArray<FColor> Colors = BiomeDebugColors(Table, &Unlisted);
    TArray<FColor> Pixels;
    Pixels.SetNumUninitialized(Size * Size);
    TArray<int64> Counts;
    Counts.SetNumZeroed(Table.Biomes.Num());
    for (int32 Index = 0; Index < Biomes.Num(); ++Index)
    {
        const uint8 Biome = Biomes[Index];
        Pixels[Index] = Colors.IsValidIndex(Biome) ? Colors[Biome] : FColor::Black;
        if (Counts.IsValidIndex(Biome)) ++Counts[Biome];
    }
    if (!VoxelClimateDebug::WritePng(Pixels, Size, Path, OutError)) return false;

    // Shares, largest first, beside the jar's share of its depth-0 lookup samples: those are uniform climate targets,
    // so they tell which biomes the mapping reaches and roughly how much climate each holds, not how much area.
    const TMap<FString, double> JarShares = BiomeDebugJarShares();
    TArray<int32> Order;
    for (int32 Biome = 0; Biome < Counts.Num(); ++Biome)
        Order.Add(Biome);
    Order.StableSort([&Counts](int32 A, int32 B) { return Counts[A] > Counts[B]; });
    FString Csv = TEXT("biome,map_share,jar_depth0_sample_share\n");
    FString Largest;
    int32 Present = 0;
    for (const int32 Biome : Order)
    {
        const double Share = double(Counts[Biome]) / Biomes.Num();
        Present += Counts[Biome] > 0;
        Csv += FString::Printf(
            TEXT("%s,%.5f,%.5f\n"), *Table.Biomes[Biome].Name, Share, JarShares.FindRef(Table.Biomes[Biome].Name));
        if (Counts[Biome] > 0 && Largest.Len() < 160)
            Largest += FString::Printf(TEXT("%s%s %.1f%%"), Largest.IsEmpty() ? TEXT("") : TEXT(", "),
                *Table.Biomes[Biome].Name.RightChop(10), 100 * Share);
    }
    const FString CsvPath = FPaths::ChangeExtension(Path, TEXT("csv"));
    if (!FFileHelper::SaveStringToFile(Csv, *CsvPath))
    {
        OutError = TEXT("could not write ") + CsvPath;
        return false;
    }
    OutSummary =
        FString::Printf(TEXT("voxel.BiomeMap %s: %d x %d px at %g blocks per px (%.0f blocks across) in %.0f ms "
                             "(%.2f us per point); %d of %d biomes: %s%s -> %s"),
            Options.Height ? *FString::Printf(TEXT("y %g"), *Options.Height) : TEXT("surface"), Size, Size,
            Options.BlocksPerPixel, Size * Options.BlocksPerPixel, Seconds * 1000, Seconds * 1e6 / Biomes.Num(),
            Present, Table.Biomes.Num(), *Largest,
            Unlisted.IsEmpty() ? TEXT("") : *(TEXT("; no colour for ") + FString::Join(Unlisted, TEXT(", "))),
            *FPaths::ConvertRelativePathToFull(Path));
    return true;
}
