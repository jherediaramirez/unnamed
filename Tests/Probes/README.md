# Probes

Black-box measurements of vanilla Minecraft 26.3 behaviour that the game copies. Each probe runs the 26.3 server's own
code through its public API only (`javap -public` signatures; no bytecode bodies, no reflection into private
internals) and writes a deterministic matrix that a native test checks row by row.

## Torch placement (torch session T3)

- `TorchPlacementProbe.java`: starts an in-process 26.3 server (`MinecraftServer.spin` with `GameTestServer.create`,
  no tests selected), and a `ServerPlayer` without a connection. The player clicks with a torch (`ItemStack.useOn`) at
  every look on a grid of yaw 0-345 and pitch -90-90 in 15° steps.
  - Case A: a solid click on every face of a stone block, including the underside, for each on/off set of stone
    supports (floor and four walls) around the destination.
  - Case B: a click on each face of short grass (a replaceable block) on a grass block, for each on/off set of walls.
- `torch-placement-26.3.json`: the accepted matrix. Its header documents the format: the look grid, vanilla's yaw
  and pitch convention, the codes and the config legend. `looks[]` holds `Direction.orderedByNearest` and each
  click's `getNearestLookingDirections()`; `results[]` holds one character per look for every case, face and
  config. SHA-256 `5B45941BAD1B3828392B025502C6586208B6D05FD7474394C3ECF91BF7EAD5E2`.
- `check_rule.py`: an independent checker that rebuilds every entry from the look table and the config, using the
  rule in its docstring.
- `run-torch-placement-probe.ps1`: compiles and runs the probe into a work folder, runs the checker and compares the
  output with the accepted matrix (it must be byte-identical; it never overwrites it). Needs JDK 25 and the 26.3
  server jar with its libraries (`C:\dev\sandbox\mc-26.3-blocks\datagen`).

The native test `Unnamed.Foundation.Blocks.TorchProbe` reads the matrix and checks the game's placement rule against
every row. The record is `Docs/Records/2026-10-10-Torch-T3.md`.

## Door hinge placement (blocks session B3a)

- `DoorHingeProbe.java`: the torch probe's in-process 26.3 server and connection-less `ServerPlayer`. The player places
  an `oak_door` (`ItemStack.useOn` on the top face of a stone floor) for 4 facings x 2 lateral clicks (0.25, 0.75)
  against the cells left and right of the door (lower and upper cell each):
  - Part A: all 6^4 combinations of air, stone, glass, oak_leaves, a bottom oak_slab and an oak_fence;
  - Part B: 8 x 8 door neighbours (oak, spruce and iron, the opposite facing, either hinge, a lower or an upper half
    alone);
  - Part C: a door on one side and stone on the other;
  - Part D: a door's lower half alone with stone above it on one side, against air or stone on the other (clicks 0.25,
    0.5, 0.75): which wins when the door and the full-block count disagree;
  - Part E: nothing beside the door, clicks 0.5, 0.4999 and 0.5001 (the centre line). 11,100 cases.
- `door-hinge-26.3.json`: the measured matrix, one case per line; its header documents left, right and the click
  coordinates. SHA-256 `AE1CC663E4A81A5DB3C58BD8D49B8AA4C83238A50ACE4FB1706BACBA15121F0C`.
- `run-door-hinge-probe.ps1`: compiles and runs the probe into a work folder (default
  `C:\dev\sandbox\b3a-hinge-probe`) and writes the JSON next to the script (about 30 s). It does not compare with an
  accepted copy: check the file's hash against the one above after a run.

The native test `Unnamed.Foundation.Interact.Hinge` places every case through the game's rule and compares the lower
half's state. The record is `Docs/Records/2026-10-15-Blocks-B3a.md`.
