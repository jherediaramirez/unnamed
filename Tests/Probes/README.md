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
