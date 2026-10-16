#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationBlockPlacement.h"
#include "Foundation/FoundationBlockRegistry.h"

struct FFoundationBlockWorld;
struct FFoundationHit;
struct FFoundationPlayer;
struct FFoundationResult;
class FFoundationAuthority;

// The block families with placement and shape rules of their own (survival track T5). B2: the connecting blocks and
// stairs; B3a: doors and trapdoors; B3b: buttons and the lever.
enum class EFoundationBlockFamily : uint8
{
    None,
    // #minecraft:stairs: facing, half, shape.
    Stairs,
    // #minecraft:fences: a side per horizontal direction.
    Fence,
    // #minecraft:walls: none, low or tall per side, and the post (up).
    Wall,
    // Glass panes and every bars block (the IronBarsBlock class): the fences' property set without the tag.
    Pane,
    // #minecraft:fence_gates: facing, in_wall; open (B3a; powered needs redstone).
    FenceGate,
    // #minecraft:doors (B3a): two halves, facing, hinge, open.
    Door,
    // #minecraft:trapdoors (B3a): facing, half, open, waterlogged.
    Trapdoor,
    // #minecraft:buttons (B3b): face, facing, powered; pressed for GetButtonTicks steps.
    Button,
    // The lever (B3b): face, facing, powered.
    Lever,
    // The ladder (B4b): facing, waterlogged.
    Ladder,
};

// A family block's survival where it stands (FoundationBlockFamilies::GetSurvival).
enum class EFoundationFamilySurvival : uint8
{
    // No family rule: the plant rules decide.
    NoRule,
    Survives,
    Fails,
    // The rule reads a cell nobody knows.
    Unknown,
};

// Survival B2b's rules for those families, behind foundation.Blocks.Shapes (FoundationBlockFamilies::AreShapesEnabled,
// FoundationBlockRegistry.h): placement takes facing, half and connections, and an edit updates its neighbours' shapes.
// Behaviour is BlockState.updateShape as measured in Reference/minecraft-blocks-26.3/facts/shape_updates.json (the
// tests compare every row), and MINECRAFT-26.3-VALUES.md "Stair corners" and "Fence gates".
namespace FoundationBlockFamilies
{
    // Reads the state at a cell; cells nobody knows read as air.
    using FGetState = TFunctionRef<FFoundationBlockState(const FIntVector& Cell)>;

    UNNAMED_API EFoundationBlockFamily GetFamily(const FFoundationBlockType& Block);
    // The faces of a state that are sturdy (isFaceSturdy, full support), a bit per EFoundationFace: the faces its
    // collision boxes cover whole, clipped to the cell, apart from the measured exceptions (shape_updates.json).
    UNNAMED_API uint8 GetSturdyFaces(const FFoundationBlockRegistry& Registry, FFoundationBlockState State);
    // One face of GetSturdyFaces (Block.isFaceSturdy, full support).
    UNNAMED_API bool IsFaceSturdy(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState State, EFoundationFace Face);
    // BlockState.updateShape: State at Cell, told that its neighbour toward Toward is now Neighbour. Other cells (the
    // wall's above, the stair's sides, the gate's other side) come from Get. Non-family states come back unchanged.
    UNNAMED_API FFoundationBlockState UpdateShape(const FFoundationBlockRegistry& Registry, FFoundationBlockState State,
        const FIntVector& Cell, EFoundationFace Toward, FFoundationBlockState Neighbour, FGetState Get);
    // UpdateShape toward down, up, north, south, west, east with the states Get finds there, until a pass changes
    // nothing (at most 3 passes): the oracle's settle rule, and how placement takes its connections.
    UNNAMED_API FFoundationBlockState SettleShape(
        const FFoundationBlockRegistry& Registry, FFoundationBlockState State, const FIntVector& Cell, FGetState Get);
    // getStateForPlacement for a family block at Context.Destination: stairs face the player's horizontal look with
    // the half from the click (bTopHalf: the slab rule), gates face the look (VALUES "Fence gates",
    // blocks.fence_gate_placement), waterlogged in a water source, then SettleShape. Others: the default state.
    // B3a: a door's lower half faces the look with its hinge (VALUES "Door hinges"; Tests/Probes/door-hinge-26.3.json),
    // and the authority writes its upper half (the plant rules' pairing); a trapdoor takes bTopHalf and faces the
    // clicked side face, or away from the player on a top or bottom face.
    // B3b: a button or lever takes the first of the click's looks (FoundationBlockPlacement::GetClickLooks) where it
    // survives (GetSurvival): up hangs it from the ceiling, down stands it on the floor, both facing the horizontal
    // look; a side puts it on that wall, facing away from it (Tests/Probes/switch-placement-26.3.json). None survives:
    // the floor state, which the authority's survival check refuses.
    // B4b: a ladder takes the first side look where it survives, facing away from that wall, waterlogged in a water
    // source; a ladder clicked on its own front face places nothing (Tests/Probes/ladder-placement-26.3.json).
    UNNAMED_API FFoundationBlockState GetStateForPlacement(const FFoundationBlockWorld& World,
        const FFoundationBlockType& Block, const FFoundationPlacementContext& Context, bool bTopHalf);
    // After Cell changed: each family block among its 4 horizontal neighbours, and a wall below it, takes one
    // UpdateShape toward Cell. One ring, no cascade; unknown cells are skipped. Nothing while the switch is 0.
    UNNAMED_API void UpdateNeighbourShapes(FFoundationBlockWorld& World, const FIntVector& Cell);

    // Survival B3a, behind foundation.Blocks.Interact (default 1): doors, trapdoors and fence gates place by their own
    // rules and open by hand. 0: no family use or door survival, and doors and trapdoors place as before B3a.
    UNNAMED_API bool IsInteractOn();
    // Whether Block places through GetStateForPlacement: a family block, doors and trapdoors only while IsInteractOn.
    UNNAMED_API bool UsesFamilyPlacement(const FFoundationBlockType& Block);
    // Whether a hand opens Block (FFoundationAuthority::FindBlockUse's fallback while IsInteractOn): every door,
    // trapdoor and fence gate but iron_door and iron_trapdoor, which have no use, so a click on them passes
    // (survival.r9.switches: no hand use; the held block places against them).
    UNNAMED_API bool IsUsable(const FFoundationBlockType& Block);
    // The use (an FFoundationAuthority::FBlockUse): flips open, a door's two halves at once, a gate first turning to
    // the player's look when it faces the player; FFoundationAuthority::CommitBlockUse writes it with its sound.
    // B3b: a lever flips powered (volume 0.3, pitch 0.6 on and 0.5 off); a released button is pressed and its release
    // scheduled (FFoundationAuthority::ScheduleBlockTick, GetButtonTicks steps); a pressed one consumes the click and
    // changes nothing, its release unmoved (survival.r9.buttons; the probe's part C).
    UNNAMED_API bool Use(FFoundationAuthority& Authority, FFoundationPlayer& Player, const FFoundationHit& Hit,
        FFoundationResult& Result);
    // The sound event a use plays (Reference/minecraft-blocks-26.3/facts/block_use_sounds.csv: by set type); bOpen is
    // powered for buttons and the lever.
    UNNAMED_API FName GetUseSound(const FFoundationBlockType& Block, bool bOpen);
    // How long a pressed button stays pressed, in steps: 30 for #minecraft:wooden_buttons, else 20 (stone and
    // polished blackstone; F 5053-5069, survival.r9.buttons). 0 for other blocks.
    UNNAMED_API int32 GetButtonTicks(const FFoundationBlockType& Block);
    // A scheduled button release at Cell (FFoundationAuthority's block ticks, kind ButtonRelease): unpowers it with its
    // click_off. The authority only calls it while the cell still holds the pressed state it scheduled.
    UNNAMED_API void ReleaseButton(FFoundationAuthority& Authority, const FIntVector& Cell);
    // A door half's survival at Cell (family_survival.json): the lower half needs a sturdy top below it, the upper half
    // the same door below; a button, lever or ladder the sturdy face of the block it hangs on. Other blocks: NoRule.
    // Nothing while IsInteractOn is off, apart from the B4b vines: twisting vines need below them, weeping vines above
    // them, the same vine (head or body) or a sturdy face toward them (family_survival.json signatures 88 and 89).
    UNNAMED_API EFoundationFamilySurvival GetSurvival(
        const FFoundationBlockWorld& World, const FIntVector& Cell, FFoundationBlockState State);

    // Survival B4b, behind foundation.Blocks.Climb (default 1): the player climbs in a climbable block.
    UNNAMED_API bool IsClimbOn();
    // Whether the block at Cell is climbed (minecraft.wiki "Ladder", "Trapdoor"): #minecraft:climbable
    // (Block.Climbable), or an open trapdoor over a ladder facing the same way. Unknown cells are not. The cell is the
    // one holding the player's feet (provisional, provisional-values.md).
    UNNAMED_API bool IsClimbable(const FFoundationBlockWorld& World, const FIntVector& Cell);
}
