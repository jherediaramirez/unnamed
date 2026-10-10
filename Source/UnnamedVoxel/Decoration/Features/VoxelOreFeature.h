#pragma once

#include "CoreMinimal.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelRuleTests.h"
#include "Random/VoxelRandom.h"

class FVoxelFeatureGraph;

// One of an ore's targets: where its rule test matches, the ore places State.
struct FVoxelOreTarget
{
    FVoxelRuleTest Rule;
    uint16 State = 0;
    // The block's full id ("minecraft:iron_ore").
    FString Id;
};

struct FVoxelOreConfig
{
    int32 Size = 0;
    float DiscardOnAir = 0;
    // Index of the decorator's target list.
    int32 Targets = INDEX_NONE;
};

// The ore feature, our own blob matched to S1b's 24 probes (facts/feature_probes/) as distributions. The shape is the
// widely published ore blob, overlapping spheres along a random segment; its constants were checked against the probes
// and needed no fitting (Docs/VoxelWorld.md, "Decoration"):
// - a horizontal segment of half-length Size / 8 through the origin block's lower corner, at a random angle;
// - its ends 0 to 2 blocks below the origin, independently;
// - Size spheres at t = i / Size along it, of radius ((s Size / 16) (sin(pi t) + 1) + 1) / 2 with s uniform in [0, 1);
// - every cell whose centre lies inside a sphere is a candidate, once.
// A candidate takes the first target whose rule test matches its block; a candidate with an air face neighbour is
// dropped with the discard chance.
namespace VoxelOreFeature
{
    // How far a blob of Size can reach from its origin block: X and Y within Horizontal, Z from Down below to Up above.
    struct FReach
    {
        int32 Horizontal = 0;
        int32 Down = 0;
        int32 Up = 0;
    };
    UNNAMEDVOXEL_API FReach GetReach(int32 Size);

    // An ore feature node ("minecraft:ore") compiled: its size, discard chance and targets (Targets left unset; the
    // caller keeps the list). A target whose block the table lacks is left out; OutSkip says why when none is left.
    UNNAMEDVOXEL_API bool Compile(const FVoxelFeatureGraph& Graph, int32 FeatureNode, FVoxelBlockTags& Tags,
        const FVoxelBlockTable& Table, FVoxelOreConfig& OutConfig, TArray<FVoxelOreTarget>& OutTargets,
        FString& OutSkip, FString* OutError);

    // The blob's candidate cells for one placement at Origin, each once, in a fixed order; draws from Random.
    UNNAMEDVOXEL_API void GetBlobCells(int32 Size, FIntVector Origin, FVoxelXoroshiro& Random, TArray<FIntVector>& Out);

    // Places one blob in the working copy and calls Emit(X, Y, Z, State) for every block it placed. Returns the cells
    // that fell outside the context's 3x3 (dropped).
    template <typename FEmit>
    int32 Place(const FVoxelOreConfig& Config, TConstArrayView<FVoxelOreTarget> Targets, FIntVector Origin,
        FVoxelXoroshiro& Random, FVoxelDecorationContext& Context, const FVoxelBlockTable& Table, bool bDiscard,
        TArray<FIntVector>& Scratch, FEmit&& Emit)
    {
        GetBlobCells(Config.Size, Origin, Random, Scratch);
        int32 Dropped = 0;
        const float Discard = bDiscard ? Config.DiscardOnAir : 0.f;
        const auto IsAir = [&Context, &Table](int32 X, int32 Y, int32 Z)
        { return Table.Get(Context.GetState(X, Y, Z)).IsAir(); };
        for (const FIntVector& Cell : Scratch)
        {
            if (!Context.Contains(Cell.X, Cell.Y))
            {
                ++Dropped;
                continue;
            }
            const int32 Block = Table.Get(Context.GetState(Cell.X, Cell.Y, Cell.Z)).Block;
            const FVoxelOreTarget* Target = nullptr;
            for (const FVoxelOreTarget& Candidate : Targets)
                if (Candidate.Rule.Test(Block, Cell.Z))
                {
                    Target = &Candidate;
                    break;
                }
            if (!Target) continue;
            if (Discard > 0 &&
                (IsAir(Cell.X - 1, Cell.Y, Cell.Z) || IsAir(Cell.X + 1, Cell.Y, Cell.Z) ||
                    IsAir(Cell.X, Cell.Y - 1, Cell.Z) || IsAir(Cell.X, Cell.Y + 1, Cell.Z) ||
                    IsAir(Cell.X, Cell.Y, Cell.Z - 1) || IsAir(Cell.X, Cell.Y, Cell.Z + 1)) &&
                (Discard >= 1 || Random.NextFloat() < Discard))
                continue;
            Context.SetState(Cell.X, Cell.Y, Cell.Z, Target->State);
            Emit(Cell.X, Cell.Y, Cell.Z, Target->State);
        }
        return Dropped;
    }
}
