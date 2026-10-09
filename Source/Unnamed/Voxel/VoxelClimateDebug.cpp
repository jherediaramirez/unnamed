#include "Voxel/VoxelClimateDebug.h"
#include "Algo/Find.h"
#include "Async/ParallelFor.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Random/VoxelSeed.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelWorldSettings.h"
#include "Voxel/VoxelWorldgenReader.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"

namespace
{
    struct FClimateDebugCacheEntry
    {
        FString Preset;
        uint64 Seed = 0;
        TSharedPtr<const FVoxelClimateSampler> Sampler;
        // Why Sampler is null.
        FString Error;
    };
    TArray<FClimateDebugCacheEntry> ClimateDebugCache;

    struct FClimateDebugViewsEntry
    {
        FString Preset;
        uint64 Seed = 0;
        TSharedPtr<const VoxelClimateDebug::FViews> Views;
        FString Error;
    };
    TArray<FClimateDebugViewsEntry> ClimateDebugViewsCache;
    const TCHAR* const ClimateDebugPresets[] = {TEXT("overworld"), TEXT("large_biomes"), TEXT("amplified")};

    FLinearColor ClimateDebugLerp(const FLinearColor& A, const FLinearColor& B, double Alpha)
    {
        return FLinearColor::LerpUsingHSV(A, B, float(FMath::Clamp(Alpha, 0., 1.)));
    }

    // Offset as terrain: water darkening with depth below sea level, then grass, hills, rock and snow.
    FColor ClimateDebugHeightColor(double Height)
    {
        const double Sea = VoxelConstants::SeaLevel;
        if (Height < Sea)
            return ClimateDebugLerp(
                FLinearColor(.02f, .05f, .25f), FLinearColor(.15f, .4f, .8f), 1 - (Sea - Height) / 64)
                .ToFColor(true);
        if (Height < Sea + 3) return FLinearColor(.85f, .8f, .55f).ToFColor(true);
        if (Height < 110)
            return ClimateDebugLerp(FLinearColor(.25f, .55f, .2f), FLinearColor(.55f, .6f, .3f), (Height - Sea) / 47)
                .ToFColor(true);
        if (Height < 170)
            return ClimateDebugLerp(FLinearColor(.5f, .45f, .35f), FLinearColor(.55f, .55f, .55f), (Height - 110) / 60)
                .ToFColor(true);
        return ClimateDebugLerp(FLinearColor(.7f, .7f, .7f), FLinearColor::White, (Height - 170) / 60).ToFColor(true);
    }

    // The usual range of each parameter, for the ramp. Factor and jaggedness are never negative, so they get a
    // one-sided ramp.
    void ClimateDebugRange(EVoxelClimate Parameter, double& OutMin, double& OutMax)
    {
        switch (Parameter)
        {
        case EVoxelClimate::Continents:
            OutMin = -1.2, OutMax = 1.2;
            return;
        case EVoxelClimate::Factor:
            OutMin = 0, OutMax = 8;
            return;
        case EVoxelClimate::Jaggedness:
            OutMin = 0, OutMax = .7;
            return;
        default:
            OutMin = -1, OutMax = 1;
        }
    }

    FColor ClimateDebugColor(EVoxelClimate Parameter, float Value)
    {
        if (Parameter == EVoxelClimate::Offset)
            return ClimateDebugHeightColor(VoxelClimateDebug::EstimateSurface(Value));
        double Min, Max;
        ClimateDebugRange(Parameter, Min, Max);
        const double Alpha = (Value - Min) / (Max - Min);
        if (Min >= 0)
            return ClimateDebugLerp(FLinearColor(.05f, .02f, .15f), FLinearColor(1.f, .85f, .2f), Alpha).ToFColor(true);
        // Diverging: blue below the middle, red above, white at it.
        return (Alpha < .5 ? ClimateDebugLerp(FLinearColor(.05f, .15f, .6f), FLinearColor::White, Alpha * 2)
                           : ClimateDebugLerp(FLinearColor::White, FLinearColor(.65f, .08f, .05f), Alpha * 2 - 1))
            .ToFColor(true);
    }

    // The voxel world's seed, or the dev seed outside one.
    uint64 ClimateDebugSeed(UWorld* World)
    {
        if (const AVoxelWorld* Voxels = World ? AVoxelWorld::Find(World) : nullptr) return Voxels->GetSeed().Value;
        return FVoxelSeed::Parse(UVoxelWorldSettings::Get().DevSeed).Value;
    }

    // The first local player's position in blocks.
    bool ClimateDebugPlayer(UWorld* World, FVector& OutBlocks)
    {
        const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
        if (!Controller) return false;
        FVector Location;
        FRotator Rotation;
        Controller->GetPlayerViewPoint(Location, Rotation);
        OutBlocks = Location / 100.;
        return true;
    }

    constexpr int32 ClimateDebugMessageKey = 0x5C11A7E;

    TAutoConsoleVariable<FString> CVarDebugClimate(TEXT("voxel.Debug.Climate"), TEXT("off"),
        TEXT(
            "Shows the terrain climate at the player on screen, from the overworld preset: C, E, W, PV, T, V, O, F, J, "
            "D (depth at the player's height), N (final density at the player) and PS (preliminary surface), "
            "highlighting the one named (C|E|W|PV|T|V|O|F|J|D|N|PS; off hides it). Debug only, not a setting."));

    bool TickClimateDebug(float)
    {
        const FString Label = CVarDebugClimate.GetValueOnGameThread();
        if (Label.Equals(TEXT("off"), ESearchCase::IgnoreCase) || !GEngine) return true;
        UWorld* World = nullptr;
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
            if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) World = Context.World();
        FVector Blocks;
        if (!ClimateDebugPlayer(World, Blocks)) return true;
        FString Error;
        const TSharedPtr<const FVoxelClimateSampler> Sampler =
            VoxelClimateDebug::GetSampler(TEXT("overworld"), ClimateDebugSeed(World), Error);
        if (!Sampler)
        {
            GEngine->AddOnScreenDebugMessage(ClimateDebugMessageKey, 0, FColor::Red, Error);
            return true;
        }
        FVoxelDensityScratch Scratch;
        FVoxelClimateSample Sample;
        Sampler->Sample(&Blocks.X, &Blocks.Y, 1, Scratch, MakeArrayView(&Sample, 1));
        const float Depth = Sampler->SampleDepth(Blocks.X, Blocks.Y, Blocks.Z, Scratch);
        FString Line = FString::Printf(TEXT("Climate at %.0f %.0f %.0f:"), Blocks.X, Blocks.Y, Blocks.Z);
        for (int32 Index = 0; Index < int32(EVoxelClimate::Count); ++Index)
        {
            const TCHAR* Name = FVoxelClimateSampler::GetLabel(EVoxelClimate(Index));
            const bool bPicked = Label.Equals(Name, ESearchCase::IgnoreCase);
            Line += FString::Printf(TEXT("  %s%s %.3f%s"), bPicked ? TEXT("[") : TEXT(""), Name, Sample.Values[Index],
                bPicked ? TEXT("]") : TEXT(""));
        }
        const bool bDepth = Label.Equals(TEXT("D"), ESearchCase::IgnoreCase);
        Line +=
            FString::Printf(TEXT("  %sD %.3f%s"), bDepth ? TEXT("[") : TEXT(""), Depth, bDepth ? TEXT("]") : TEXT(""));
        Line +=
            FString::Printf(TEXT("  surface ~%.0f"), VoxelClimateDebug::EstimateSurface(Sample[EVoxelClimate::Offset]));
        // N at the player and PS under them, through the reference axes (x, y up, z).
        if (const TSharedPtr<const VoxelClimateDebug::FViews> Views =
                VoxelClimateDebug::GetViews(TEXT("overworld"), ClimateDebugSeed(World), Error))
        {
            const double PX = Blocks.X, PY = Blocks.Z, PZ = Blocks.Y;
            Views->Evaluator.Evaluate(&PX, &PY, &PZ, 1, Scratch);
            const bool bN = Label.Equals(TEXT("N"), ESearchCase::IgnoreCase);
            const bool bPS = Label.Equals(TEXT("PS"), ESearchCase::IgnoreCase);
            Line += FString::Printf(TEXT("  %sN %.3f%s  %sPS %.0f%s"), bN ? TEXT("[") : TEXT(""),
                Views->Evaluator.GetValues(Scratch, Views->Density)[0], bN ? TEXT("]") : TEXT(""),
                bPS ? TEXT("[") : TEXT(""), Views->Evaluator.GetValues(Scratch, Views->Surface)[0],
                bPS ? TEXT("]") : TEXT(""));
        }
        GEngine->AddOnScreenDebugMessage(ClimateDebugMessageKey, 0, FColor::White, Line);
        return true;
    }

    FTSTicker::FDelegateHandle ClimateDebugTicker;
    FDelayedAutoRegisterHelper ClimateDebugRegister(EDelayedRegisterRunPhase::EndOfEngineInit,
        []
        {
            ClimateDebugTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickClimateDebug));
        });

    FAutoConsoleCommandWithWorldArgsAndOutputDevice ClimateMapCommand(TEXT("voxel.ClimateMap"),
        TEXT("voxel.ClimateMap <C|E|W|PV|T|V|O|F|J|N|PS|all> [size px, default 1024] [blocks per px, default 8] "
             "[preset: "
             "overworld|large_biomes|amplified]: writes a top-down map centered on the player (or 0, 0) to "
             "Saved/VoxelClimate. O draws the surface height the offset implies, PS the preliminary surface, and N "
             "the final density at sea level (solid red, open blue)."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
            [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
            {
                // Climate parameters by index; the views as -1 (N) and -2 (PS).
                TArray<int32> Maps;
                EVoxelClimate Parameter;
                if (!Args.IsEmpty() && Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase))
                {
                    for (int32 Index = 0; Index < int32(EVoxelClimate::Count); ++Index)
                        Maps.Add(Index);
                    Maps.Append({-1, -2});
                }
                else if (!Args.IsEmpty() && FVoxelClimateSampler::ParseLabel(Args[0], Parameter))
                    Maps.Add(int32(Parameter));
                else if (!Args.IsEmpty() && Args[0].Equals(TEXT("N"), ESearchCase::IgnoreCase))
                    Maps.Add(-1);
                else if (!Args.IsEmpty() && Args[0].Equals(TEXT("PS"), ESearchCase::IgnoreCase))
                    Maps.Add(-2);
                else
                {
                    Output.Log(
                        TEXT("Usage: voxel.ClimateMap <C|E|W|PV|T|V|O|F|J|N|PS|all> [size] [blocks per px] [preset]"));
                    return;
                }
                VoxelClimateDebug::FMapOptions Options;
                if (Args.Num() > 1) Options.Size = FMath::Clamp(FCString::Atoi(*Args[1]), 16, 8192);
                if (Args.Num() > 2) Options.BlocksPerPixel = FMath::Clamp(FCString::Atod(*Args[2]), .25, 1024.);
                const FString Preset = Args.Num() > 3 ? Args[3] : TEXT("overworld");
                FVector Blocks;
                if (ClimateDebugPlayer(World, Blocks)) Options.CenterX = Blocks.X, Options.CenterY = Blocks.Y;

                const uint64 Seed = ClimateDebugSeed(World);
                FString Error;
                const TSharedPtr<const FVoxelClimateSampler> Sampler =
                    VoxelClimateDebug::GetSampler(Preset, Seed, Error);
                // The N and PS views compile only when asked for.
                const bool bViews = Maps.ContainsByPredicate([](int32 Map) { return Map < 0; });
                const TSharedPtr<const VoxelClimateDebug::FViews> Views =
                    Sampler && bViews ? VoxelClimateDebug::GetViews(Preset, Seed, Error) : nullptr;
                if (!Sampler || (bViews && !Views))
                {
                    Output.Log(ELogVerbosity::Error, Error);
                    return;
                }
                const FString Folder = FPaths::ProjectSavedDir() / TEXT("VoxelClimate");
                for (const int32 Map : Maps)
                {
                    const TCHAR* Label = Map >= 0 ? FVoxelClimateSampler::GetLabel(EVoxelClimate(Map))
                                                  : (Map == -1 ? TEXT("N") : TEXT("PS"));
                    const FString Path = Folder /
                        FString::Printf(TEXT("%s-%lld-%s-%d-%g.png"), *Preset, int64(Seed), Label, Options.Size,
                            Options.BlocksPerPixel);
                    FString Summary;
                    bool bWritten;
                    if (Map >= 0)
                    {
                        Options.Parameter = EVoxelClimate(Map);
                        bWritten = VoxelClimateDebug::WriteMap(*Sampler, Options, Path, Summary, Error);
                    }
                    else
                        bWritten = VoxelClimateDebug::WriteViewMap(
                            *Views, Map == -2, VoxelConstants::SeaLevel, Options, Path, Summary, Error);
                    if (bWritten)
                    {
                        Output.Log(Summary);
                        UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Summary);
                    }
                    else
                        Output.Log(ELogVerbosity::Error, Error);
                }
            }));

    // Fills Size x Size values across every task worker, row by row (row 0 at the top, the map's north), colors them
    // and writes the PNG; OutSummary gets the timing and spread. SampleRow(X, Y, Count, Out) samples one row.
    bool ClimateDebugWriteValues(const VoxelClimateDebug::FMapOptions& Options, const TCHAR* Label,
        TFunctionRef<void(const double*, const double*, int32, float*)> SampleRow, TFunctionRef<FColor(float)> Color,
        bool bHeights, const FString& Path, FString& OutSummary, FString& OutError)
    {
        const int32 Size = Options.Size;
        TArray<FColor> Pixels;
        Pixels.SetNumUninitialized(Size * Size);
        TArray<float> Values;
        Values.SetNumUninitialized(Size * Size);
        const double Start = FPlatformTime::Seconds();
        ParallelFor(Size,
            [&](int32 Row)
            {
                TArray<double> X, Y;
                X.SetNumUninitialized(Size);
                Y.SetNumUninitialized(Size);
                // Y points south, so rows run north to south.
                const double BlockY = Options.CenterY + (Row - Size / 2) * Options.BlocksPerPixel;
                for (int32 Column = 0; Column < Size; ++Column)
                {
                    X[Column] = Options.CenterX + (Column - Size / 2) * Options.BlocksPerPixel;
                    Y[Column] = BlockY;
                }
                float* Out = Values.GetData() + Row * Size;
                SampleRow(X.GetData(), Y.GetData(), Size, Out);
                for (int32 Column = 0; Column < Size; ++Column)
                    Pixels[Row * Size + Column] = Color(Out[Column]);
            });
        const double Seconds = FPlatformTime::Seconds() - Start;

        if (!VoxelClimateDebug::WritePng(Pixels, Size, Path, OutError)) return false;

        // Only finite values are ranked: a NaN would break the sort's ordering.
        Values.RemoveAll([](float Value) { return !FMath::IsFinite(Value); });
        if (Values.IsEmpty()) Values.Add(0);
        Values.Sort();
        auto At = [&Values](double Fraction) { return Values[FMath::FloorToInt(Fraction * (Values.Num() - 1))]; };
        int32 Below = 0;
        if (bHeights)
            for (const float Value : Values)
                Below += Value < VoxelConstants::SeaLevel;
        OutSummary =
            FString::Printf(TEXT("voxel.ClimateMap %s: %d x %d px at %g blocks per px (%.0f blocks across) in %.0f "
                                 "ms (%.2f us per point); p1 %.3f, p50 %.3f, p99 %.3f%s -> %s"),
                Label, Size, Size, Options.BlocksPerPixel, Size * Options.BlocksPerPixel, Seconds * 1000,
                Seconds * 1e6 / Values.Num(), At(.01), At(.5), At(.99),
                bHeights ? *FString::Printf(TEXT("; below sea level %.0f%%"), 100. * Below / Values.Num()) : TEXT(""),
                *FPaths::ConvertRelativePathToFull(Path));
        return true;
    }
} // namespace

FColor VoxelClimateDebug::GetColor(EVoxelClimate Parameter, float Value)
{
    return ClimateDebugColor(Parameter, Value);
}

bool VoxelClimateDebug::WritePng(TConstArrayView<FColor> Pixels, int32 Size, const FString& Path, FString& OutError)
{
    IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    const TSharedPtr<IImageWrapper> Png = Images.CreateImageWrapper(EImageFormat::PNG);
    if (Pixels.Num() != Size * Size ||
        !Png->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Size, Size, ERGBFormat::BGRA, 8) ||
        !FFileHelper::SaveArrayToFile(Png->GetCompressed(), *Path))
    {
        OutError = TEXT("could not write ") + Path;
        return false;
    }
    return true;
}

double VoxelClimateDebug::EstimateSurface(float Offset)
{
    // The overworld's depth gradient falls from 1.5 at y -64 to -1.5 at y 320: 1/128 per block.
    return VoxelConstants::MinZ + (double(Offset) + 1.5) * 128;
}

TSharedPtr<const FVoxelClimateSampler> VoxelClimateDebug::GetSampler(
    const FString& Preset, uint64 Seed, FString& OutError)
{
    check(IsInGameThread());
    for (const FClimateDebugCacheEntry& Entry : ClimateDebugCache)
        if (Entry.Preset == Preset && Entry.Seed == Seed)
        {
            OutError = Entry.Error;
            return Entry.Sampler;
        }

    // A failure is cached too, so a per-frame caller never re-reads the reference.
    if (ClimateDebugCache.Num() >= 4) ClimateDebugCache.RemoveAt(0);
    FClimateDebugCacheEntry& Entry = ClimateDebugCache.AddDefaulted_GetRef();
    Entry.Preset = Preset;
    Entry.Seed = Seed;
    FVoxelTerrainPreset Data;
    const TSharedRef<FVoxelClimateSampler> Sampler = MakeShared<FVoxelClimateSampler>();
    // Only the reference's presets: this also keeps a typed name out of the map's file path.
    if (!Algo::Find(ClimateDebugPresets, Preset))
        OutError = FString::Printf(TEXT("unknown preset %s (overworld, large_biomes or amplified)"), *Preset);
    else if (VoxelWorldgen::ReadPreset(VoxelWorldgen::GetReferenceRoot(), Preset, Data, OutError) &&
        Sampler->Initialize(Data, Seed, &OutError))
        Entry.Sampler = Sampler;
    Entry.Error = OutError;
    return Entry.Sampler;
}

TSharedPtr<const VoxelClimateDebug::FViews> VoxelClimateDebug::GetViews(
    const FString& Preset, uint64 Seed, FString& OutError)
{
    check(IsInGameThread());
    for (const FClimateDebugViewsEntry& Entry : ClimateDebugViewsCache)
        if (Entry.Preset == Preset && Entry.Seed == Seed)
        {
            OutError = Entry.Error;
            return Entry.Views;
        }

    // A failure is cached too, so a per-frame caller never re-reads the reference.
    if (ClimateDebugViewsCache.Num() >= 4) ClimateDebugViewsCache.RemoveAt(0);
    FClimateDebugViewsEntry& Entry = ClimateDebugViewsCache.AddDefaulted_GetRef();
    Entry.Preset = Preset;
    Entry.Seed = Seed;
    FVoxelTerrainPreset Data;
    const TSharedRef<FViews> Views = MakeShared<FViews>();
    if (!Algo::Find(ClimateDebugPresets, Preset))
        OutError = FString::Printf(TEXT("unknown preset %s (overworld, large_biomes or amplified)"), *Preset);
    else if (VoxelWorldgen::ReadPreset(VoxelWorldgen::GetReferenceRoot(), Preset, Data, OutError))
    {
        for (const FVoxelTerrainPreset::FDebugView& View : Data.DebugViews)
        {
            if (View.Label == TEXT("N")) Views->Density = View.Node;
            if (View.Label == TEXT("PS")) Views->Surface = View.Node;
        }
        if (Views->Density == INDEX_NONE || Views->Surface == INDEX_NONE)
            OutError = Preset + TEXT(" has no N or PS view");
        else if (Views->Evaluator.Compile(Data.Graph, Seed, {Views->Density, Views->Surface}, &OutError))
            Entry.Views = Views;
    }
    Entry.Error = OutError;
    return Entry.Views;
}

bool VoxelClimateDebug::WriteMap(const FVoxelClimateSampler& Sampler, const FMapOptions& Options, const FString& Path,
    FString& OutSummary, FString& OutError)
{
    const EVoxelClimate Parameter = Options.Parameter;
    const bool bOffset = Parameter == EVoxelClimate::Offset;
    return ClimateDebugWriteValues(
        Options, FVoxelClimateSampler::GetLabel(Parameter),
        [&Sampler, Parameter, bOffset](const double* X, const double* Y, int32 Count, float* Out)
        {
            FVoxelDensityScratch Scratch;
            TArray<FVoxelClimateSample> Samples;
            Samples.SetNum(Count);
            Sampler.Sample(X, Y, Count, Scratch, Samples);
            // Offset is kept as the surface height it implies, for the colors and the share below sea level.
            for (int32 Index = 0; Index < Count; ++Index)
                Out[Index] = bOffset ? float(EstimateSurface(Samples[Index][Parameter])) : Samples[Index][Parameter];
        },
        [Parameter, bOffset](float Value)
        { return bOffset ? ClimateDebugHeightColor(Value) : ClimateDebugColor(Parameter, Value); }, bOffset, Path,
        OutSummary, OutError);
}

bool VoxelClimateDebug::WriteViewMap(const FViews& Views, bool bSurface, double Height, const FMapOptions& Options,
    const FString& Path, FString& OutSummary, FString& OutError)
{
    const int32 Node = bSurface ? Views.Surface : Views.Density;
    return ClimateDebugWriteValues(
        Options, bSurface ? TEXT("PS") : TEXT("N"),
        [&Views, Node, Height](const double* X, const double* Y, int32 Count, float* Out)
        {
            // Our X and Y are the reference's x and z; its y is the height.
            FVoxelDensityScratch Scratch;
            TArray<double> Heights;
            Heights.Init(Height, Count);
            Views.Evaluator.Evaluate(X, Heights.GetData(), Y, Count, Scratch);
            FMemory::Memcpy(Out, Views.Evaluator.GetValues(Scratch, Node).GetData(), Count * sizeof(float));
        },
        [bSurface](float Value)
        { return bSurface ? ClimateDebugHeightColor(Value) : ClimateDebugColor(EVoxelClimate::Erosion, Value); },
        bSurface, Path, OutSummary, OutError);
}
