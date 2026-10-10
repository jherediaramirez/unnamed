#pragma once

#include "CoreMinimal.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelRuleTests.h"

// What a plant needs to stay where it is, from 26.3's support tags (the block tags the game's code reads by name; the
// worldgen reference's CODE_TAGS) and minecraft.wiki's placement rules for each plant. Generation places a plant only
// where it survives (scout I's vanilla probes: grass and flowers on grass, cane beside water, cactus with free sides,
// lily pads on water); S7 checks placement and pops unsupported plants with the same table (Jesus's answer 6).
enum class EVoxelSurvival : uint8
{
    // Not a plant: survives anywhere.
    Always,
    // The block below is in the rule's tag (supports_vegetation, supports_dry_vegetation).
    OnTag,
    // Below: sugar cane, or a supports_sugar_cane block with water or a supports_sugar_cane_adjacently block beside it.
    SugarCane,
    // Below: cactus or a supports_cactus block; no solid block or lava on any of the four sides.
    Cactus,
    // Below: a support_override_cactus_flower block or a sturdy top.
    CactusFlower,
    // The upper half of a two-cell plant: the same block below.
    UpperHalf,
    // In water, over a sturdy top that is not in the rule's tag (cannot_support_seagrass, cannot_support_kelp); kelp
    // also grows on kelp.
    Underwater,
    // Below: a full opaque block. Generation treats light as 0 (phase 5 facts), so the light condition is left out.
    Mushroom,
    // Below: a sturdy top (leaf litter, sea pickles).
    SturdyBelow,
    // Below: a water source or a supports_lily_pad block, with no fluid in the cell itself.
    LilyPad,
    // Below: anything but air (carpets, moss carpets).
    NotAirBelow,
    // S7's plants, from scout H's measured support table (T1):
    // The neighbour at Face shows a sturdy face toward the plant, or holds a block of Tag: amethyst and shelf mushroom
    // (behind their facing), corals and coral fans (below), the hanging plants (above: spore blossom, hanging roots;
    // cave vines also under cave vines, pale hanging moss also under leaves or itself).
    Attached,
    // The neighbour at Face holds a block of Tag: cocoa toward its facing (jungle logs), a hanging mangrove propagule
    // under mangrove leaves; below: azalea, the propagule, bamboo, big and small dripleaf.
    TagAt,
    // The faces the state holds (Faces) are held (HoldsFace): every one for glow lichen and sculk vein, any one for a
    // vine (scout H, T1).
    Multiface,
    // Pointed dripstone and sulfur spike: a sturdy face at Face, or the same block pointing the same way there.
    Pointed,
    // Big dripleaf stem: below a Tag block or a stem, and above a big dripleaf or a stem.
    DripleafStem
};

struct FVoxelSurvivalRule
{
    EVoxelSurvival Kind = EVoxelSurvival::Always;
    // The support set the rule reads (FVoxelPlantSurvival::GetSet), or INDEX_NONE.
    int32 Tag = INDEX_NONE;
    // Attached, TagAt and Pointed: the EVoxelFace toward the support.
    int8 Face = -1;
    // Multiface: the faces the state holds, one bit per EVoxelFace.
    uint8 Faces = 0;
};

// One survival rule per block state, built once and read-only after (any thread may ask).
class UNNAMEDVOXEL_API FVoxelPlantSurvival
{
public:
    // Rules for every state of Table from 26.3's support tags, which Tags expands against Table (S8 passes the
    // decorator's; S7 builds one from the graph VoxelWorldgen::ReadFeatures gives). DroppedTag, for the mutation check
    // only, leaves one support tag empty. Fails on a support tag the graph lacks.
    bool Initialize(
        const FVoxelBlockTable& Table, FVoxelBlockTags& Tags, FString* OutError = nullptr, FStringView DroppedTag = {});
    bool IsInitialized() const { return Table != nullptr; }

    const FVoxelSurvivalRule& GetRule(uint16 State) const
    {
        return State < Rules.Num() ? Rules.GetData()[State] : Rules.GetData()[0];
    }

    // 26.3's sturdy (full-support) faces of a state, one bit per EVoxelFace: all six for a full cube (glass included,
    // leaves not: scout H's measurement), the flat side of a slab, the bottom of snow two layers and up; none for
    // models and fluids. Which other shapes are sturdy is Jesus's open question (S7).
    uint8 GetSturdyFaces(uint16 State) const { return State < Sturdy.Num() ? Sturdy.GetData()[State] : 0; }

    // Whether State stays at (X east, Y south, Z up), reading its neighbours through GetState(X, Y, Z) -> uint16.
    template <typename FGetState> bool Survives(uint16 State, int32 X, int32 Y, int32 Z, FGetState&& GetState) const
    {
        const FVoxelSurvivalRule& Rule = GetRule(State);
        if (Rule.Kind == EVoxelSurvival::Always) return true;
        if (!SurvivesOn(State, GetState(X, Y, Z - 1), GetState(X, Y, Z))) return false;
        static constexpr int32 Sides[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        switch (Rule.Kind)
        {
        case EVoxelSurvival::SugarCane:
        {
            const uint16 Below = GetState(X, Y, Z - 1);
            if (Table->Get(Below).Block == SugarCaneBlock) return true;
            for (const auto& Side : Sides)
            {
                const uint16 Beside = GetState(X + Side[0], Y + Side[1], Z - 1);
                if (IsWater(Beside) || Sets[SugarCaneAdjacentSet].Contains(Table->Get(Beside).Block)) return true;
            }
            return false;
        }
        case EVoxelSurvival::Cactus:
            for (const auto& Side : Sides)
            {
                const FVoxelStateInfo& Beside = Table->Get(GetState(X + Side[0], Y + Side[1], Z));
                if (Beside.bSolid || (Beside.IsFluid() && Beside.Block == LavaBlock)) return false;
            }
            return true;
        case EVoxelSurvival::Attached:
        case EVoxelSurvival::TagAt:
        case EVoxelSurvival::Pointed:
        {
            const int8* Step = FaceSteps[Rule.Face];
            return HoldsAt(State, Rule, GetState(X + Step[0], Y + Step[1], Z + Step[2]));
        }
        case EVoxelSurvival::Multiface:
        {
            // A vine stays while any face holds; glow lichen and sculk vein need every face.
            const bool bAnyFace = Table->Get(State).Block == VineBlock;
            for (int32 Face = 0; Face < 6; ++Face)
                if ((Rule.Faces >> Face & 1) && HoldsFace(State, Face, X, Y, Z, GetState) == bAnyFace) return bAnyFace;
            return Rule.Faces != 0 && !bAnyFace;
        }
        case EVoxelSurvival::DripleafStem:
        {
            const int32 Above = Table->Get(GetState(X, Y, Z + 1)).Block;
            return Above == DripleafBlock || Above == DripleafStemBlock;
        }
        default:
            return true;
        }
    }

    // Whether one face (an EVoxelFace) of a multiface state is held at (X, Y, Z): the neighbour there has a full
    // collision face or a sturdy face toward it, or, for a vine's side face, the vine above holds the same face
    // (scout H, T1). A vine's up face reads the upper face of the block above instead, as measured: a top slab holds
    // it, a bottom slab does not (Jesus's answer to Q8). S7 trims the faces that are no longer held after a neighbour
    // changes.
    template <typename FGetState>
    bool HoldsFace(uint16 State, int32 Face, int32 X, int32 Y, int32 Z, FGetState&& GetState) const
    {
        const int8* Step = FaceSteps[Face];
        const uint16 Beside = GetState(X + Step[0], Y + Step[1], Z + Step[2]);
        const bool bVine = Table->Get(State).Block == VineBlock;
        const int32 Read = bVine && Face == int32(EVoxelFace::Up) ? Face : Face ^ 1;
        if (Table->Get(Beside).Shape == EVoxelShape::Full || (GetSturdyFaces(Beside) >> Read & 1)) return true;
        if (Face >= int32(EVoxelFace::Down) || !bVine) return false;
        const uint16 Above = GetState(X, Y, Z + 1);
        return Table->Get(Above).Block == VineBlock && (GetRule(Above).Faces >> Face & 1) != 0;
    }

    // The vertical part of the rule alone: Below is the state under the cell and Cell what the cell holds before
    // State goes in (Finalize re-checks this against the merged column, where both lie in the target column).
    bool SurvivesOn(uint16 State, uint16 Below, uint16 Cell) const;

    // A support set by the rule's Tag.
    const FVoxelBlockSet& GetSet(int32 Tag) const { return Sets[Tag]; }
    // Water: a water fluid state (source or flowing) or a state holding water.
    bool IsWater(uint16 State) const
    {
        const FVoxelStateInfo& Info = Table->Get(State);
        return Info.HeldFluid != 0 || (Info.IsFluid() && Info.Block == WaterBlock);
    }
    bool IsWaterSource(uint16 State) const
    {
        const FVoxelStateInfo& Info = Table->Get(State);
        return Info.HeldFluid != 0 || (Info.IsFluid() && Info.Block == WaterBlock && Info.IsFluidSource());
    }

private:
    const FVoxelBlockTable* Table = nullptr;
    TArray<FVoxelSurvivalRule> Rules;
    TArray<uint8> Sturdy;
    TArray<FVoxelBlockSet> Sets;
    int32 SugarCaneSet = INDEX_NONE, SugarCaneAdjacentSet = INDEX_NONE, CactusSet = INDEX_NONE,
          CactusFlowerSet = INDEX_NONE, LilyPadSet = INDEX_NONE;
    int32 WaterBlock = INDEX_NONE, LavaBlock = INDEX_NONE, SugarCaneBlock = INDEX_NONE, CactusBlock = INDEX_NONE,
          KelpBlock = INDEX_NONE, KelpPlantBlock = INDEX_NONE;
    int32 VineBlock = INDEX_NONE, DripleafBlock = INDEX_NONE, DripleafStemBlock = INDEX_NONE;
    // One cell toward each EVoxelFace (X east, Y south, Z up).
    static constexpr int8 FaceSteps[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};

    // Attached, TagAt and Pointed: whether Support (the state at the rule's Face) holds State.
    bool HoldsAt(uint16 State, const FVoxelSurvivalRule& Rule, uint16 Support) const;
};
