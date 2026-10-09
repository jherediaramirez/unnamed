#pragma once

#include "CoreMinimal.h"
#include "Density/VoxelDensityEvaluator.h"

// The terrain's 2D inputs: the noise router's climate entries, peaks-and-valleys, and the splines' outputs. None of
// them depends on height, so one pass per column covers every block in it.
enum class EVoxelClimate : uint8
{
    Continents, // C
    Erosion, // E
    Ridges, // W (weirdness)
    PeaksAndValleys, // PV, folded from ridges
    Temperature, // T
    Vegetation, // V
    Offset, // base height, from the offset spline
    Factor, // vertical stretch; low means tall terrain
    Jaggedness, // sharp peak detail
    Count
};

struct FVoxelClimateSample
{
    float Values[int32(EVoxelClimate::Count)] = {};

    float operator[](EVoxelClimate Parameter) const { return Values[int32(Parameter)]; }
};

// A column's climate on its cell corners: 5 x 5 points 4 blocks apart, X fastest, from the column's minimum corner.
struct FVoxelColumnClimate
{
    static constexpr int32 CellWidth = 4;
    static constexpr int32 Corners = 5;

    FVoxelClimateSample Samples[Corners * Corners];
    double Seconds = 0;

    const FVoxelClimateSample& At(int32 X, int32 Y) const { return Samples[Y * Corners + X]; }
};

// Samples a preset's climate for one world seed. Our axes: X and Y across, Z up; the reference's x is our X and its z
// our Y. Immutable after Initialize; any thread may sample with its own scratch.
class UNNAMEDVOXEL_API FVoxelClimateSampler
{
public:
    // Fails, with a reason, when the preset lacks an entry or the graph cannot be evaluated.
    bool Initialize(const FVoxelTerrainPreset& Preset, uint64 WorldSeed, FString* OutError = nullptr);

    bool IsInitialized() const { return Evaluator.IsCompiled() && DepthEvaluator.IsCompiled(); }

    // Count points at block positions (X[I], Y[I]). Out receives Count samples.
    void Sample(const double* X, const double* Y, int32 Count, FVoxelDensityScratch& Scratch,
        TArrayView<FVoxelClimateSample> Out) const;

    // The column at column coordinates (ColumnX, ColumnY), timed.
    void SampleColumn(int32 ColumnX, int32 ColumnY, FVoxelDensityScratch& Scratch, FVoxelColumnClimate& Out) const;

    // The router's depth at one block position (height matters here): offset plus the height gradient.
    float SampleDepth(double X, double Y, double Z, FVoxelDensityScratch& Scratch) const;

    // The short labels of 26.3's debug views, plus O, F and J for the splines: "C", "PV", "O"...
    static const TCHAR* GetLabel(EVoxelClimate Parameter);
    static bool ParseLabel(FStringView Label, EVoxelClimate& OutParameter);

private:
    FVoxelDensityEvaluator Evaluator;
    FVoxelDensityEvaluator DepthEvaluator;
    int32 Nodes[int32(EVoxelClimate::Count)] = {};
    int32 DepthNode = INDEX_NONE;
};
