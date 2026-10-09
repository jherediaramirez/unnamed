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
        TEXT("Shows the terrain climate at the player on screen, from the overworld preset: C, E, W, PV, T, V, O, F, J "
             "and D (depth at the player's height), highlighting the parameter named (C|E|W|PV|T|V|O|F|J|D; off hides "
             "it). N and PS come with the 3D density in phase 4 session 3. Debug only, not a setting."));

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
        TEXT("voxel.ClimateMap <C|E|W|PV|T|V|O|F|J|all> [size px, default 1024] [blocks per px, default 8] [preset: "
             "overworld|large_biomes|amplified]: writes a top-down map of the climate centered on the player (or 0, 0) "
             "to Saved/VoxelClimate. O draws the surface height the offset implies."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
            [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
            {
                TArray<EVoxelClimate> Parameters;
                EVoxelClimate Parameter;
                if (!Args.IsEmpty() && Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase))
                    for (int32 Index = 0; Index < int32(EVoxelClimate::Count); ++Index)
                        Parameters.Add(EVoxelClimate(Index));
                else if (!Args.IsEmpty() && FVoxelClimateSampler::ParseLabel(Args[0], Parameter))
                    Parameters.Add(Parameter);
                else
                {
                    Output.Log(
                        TEXT("Usage: voxel.ClimateMap <C|E|W|PV|T|V|O|F|J|all> [size] [blocks per px] [preset]"));
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
                if (!Sampler)
                {
                    Output.Log(ELogVerbosity::Error, Error);
                    return;
                }
                const FString Folder = FPaths::ProjectSavedDir() / TEXT("VoxelClimate");
                for (const EVoxelClimate Each : Parameters)
                {
                    Options.Parameter = Each;
                    const FString Path = Folder /
                        FString::Printf(TEXT("%s-%lld-%s-%d-%g.png"), *Preset, int64(Seed),
                            FVoxelClimateSampler::GetLabel(Each), Options.Size, Options.BlocksPerPixel);
                    FString Summary;
                    if (VoxelClimateDebug::WriteMap(*Sampler, Options, Path, Summary, Error))
                    {
                        Output.Log(Summary);
                        UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Summary);
                    }
                    else
                        Output.Log(ELogVerbosity::Error, Error);
                }
            }));
} // namespace

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

bool VoxelClimateDebug::WriteMap(const FVoxelClimateSampler& Sampler, const FMapOptions& Options, const FString& Path,
    FString& OutSummary, FString& OutError)
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
            FVoxelDensityScratch Scratch;
            TArray<double> X, Y;
            TArray<FVoxelClimateSample> Samples;
            X.SetNumUninitialized(Size);
            Y.SetNumUninitialized(Size);
            Samples.SetNum(Size);
            // Row 0 is the top of the image, the map's north (+Y).
            const double BlockY = Options.CenterY + (Size / 2 - Row) * Options.BlocksPerPixel;
            for (int32 Column = 0; Column < Size; ++Column)
            {
                X[Column] = Options.CenterX + (Column - Size / 2) * Options.BlocksPerPixel;
                Y[Column] = BlockY;
            }
            Sampler.Sample(X.GetData(), Y.GetData(), Size, Scratch, Samples);
            for (int32 Column = 0; Column < Size; ++Column)
            {
                const float Value = Samples[Column][Options.Parameter];
                Values[Row * Size + Column] = Value;
                Pixels[Row * Size + Column] = ClimateDebugColor(Options.Parameter, Value);
            }
        });
    const double Seconds = FPlatformTime::Seconds() - Start;

    IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    const TSharedPtr<IImageWrapper> Png = Images.CreateImageWrapper(EImageFormat::PNG);
    if (!Png->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Size, Size, ERGBFormat::BGRA, 8) ||
        !FFileHelper::SaveArrayToFile(Png->GetCompressed(), *Path))
    {
        OutError = TEXT("could not write ") + Path;
        return false;
    }

    // Only finite values are ranked: a NaN would break the sort's ordering.
    Values.RemoveAll([](float Value) { return !FMath::IsFinite(Value); });
    if (Values.IsEmpty()) Values.Add(0);
    Values.Sort();
    auto At = [&Values](double Fraction) { return Values[FMath::FloorToInt(Fraction * (Values.Num() - 1))]; };
    int32 Below = 0;
    if (Options.Parameter == EVoxelClimate::Offset)
        for (const float Value : Values)
            Below += EstimateSurface(Value) < VoxelConstants::SeaLevel;
    OutSummary =
        FString::Printf(TEXT("voxel.ClimateMap %s: %d x %d px at %g blocks per px (%.0f blocks across) in %.0f "
                             "ms (%.2f us per point); p1 %.3f, p50 %.3f, p99 %.3f%s -> %s"),
            FVoxelClimateSampler::GetLabel(Options.Parameter), Size, Size, Options.BlocksPerPixel,
            Size * Options.BlocksPerPixel, Seconds * 1000, Seconds * 1e6 / Values.Num(), At(.01), At(.5), At(.99),
            Options.Parameter == EVoxelClimate::Offset
                ? *FString::Printf(TEXT("; below sea level %.0f%%"), 100. * Below / Values.Num())
                : TEXT(""),
            *FPaths::ConvertRelativePathToFull(Path));
    return true;
}
