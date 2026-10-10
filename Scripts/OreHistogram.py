"""Draws voxel.OreHistogram's CSV: blocks the ore features placed per Z, one panel per ore on one sheet, the stone and
deepslate variants as two series, plus one PNG per ore with --each.

usage: uv run --no-project --python 3.13 --with matplotlib python -I Scripts/OreHistogram.py <csv> [--out <png>] [--each]
"""
import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

# The reference palette's first two categorical slots (validated: CVD dE 24.7, contrast >= 3:1 on the surface).
STONE, DEEPSLATE = "#2a78d6", "#eb6834"
SURFACE, INK, MUTED, GRID = "#fcfcfb", "#1f1f1e", "#6b6a63", "#e6e5df"
ORES = ["coal", "iron", "copper", "gold", "redstone", "lapis", "diamond", "emerald", "infested"]
ROCKS = ["granite", "diorite", "andesite", "tuff", "dirt", "gravel", "clay"]
# The ore veins (phase 6, S4), counted on their own: ore with raw ore, and filler.
VEINS = [("copper vein", ["copper_ore", "raw_copper_block"], "granite"),
         ("iron vein", ["deepslate_iron_ore", "raw_iron_block"], "tuff")]


def read(path):
    with open(path, newline="", encoding="utf-8") as f:
        rows = list(csv.reader(f))
    names = [n.replace("minecraft:", "") for n in rows[0][1:]]
    z = [int(r[0]) for r in rows[1:]]
    columns = {n: [int(r[i + 1]) for r in rows[1:]] for i, n in enumerate(names)}
    return z, columns


def panels(columns):
    """Each ore with its stone and deepslate states, then each rock blob alone."""
    out = []
    for ore in ORES:
        if ore == "infested":
            stone, deep = "infested_stone", "infested_deepslate"
        else:
            stone, deep = f"{ore}_ore", f"deepslate_{ore}_ore"
        out.append((ore, [(stone, columns.get(stone), STONE), (deep, columns.get(deep), DEEPSLATE)]))
    for rock in ROCKS:
        out.append((rock, [(rock, columns.get(rock), STONE)]))
    for title, ores, filler in VEINS:
        found = [columns[f"vein {o}"] for o in ores if f"vein {o}" in columns]
        if found:
            total = [sum(values) for values in zip(*found)]
            out.append((title, [("ore and raw ore", total, STONE), (filler, columns.get(f"vein {filler}"), DEEPSLATE)]))
    return out


def draw(ax, title, series, z, per_column):
    ax.set_facecolor(SURFACE)
    total = 0
    for name, values, color in series:
        if values is None:
            continue
        scaled = [v / per_column for v in values]
        total += sum(values)
        ax.step(scaled, z, where="mid", color=color, linewidth=2, label=name)
    ax.set_title(title, color=INK, fontsize=10, loc="left")
    ax.axhline(0, color=GRID, linewidth=1, zorder=0)
    ax.axhline(63, color=GRID, linewidth=1, linestyle="--", zorder=0)
    ax.grid(axis="x", color=GRID, linewidth=0.6)
    ax.tick_params(colors=MUTED, labelsize=7)
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.set_ylim(-64, 320)
    if total == 0:
        ax.text(0.5, 0.5, "none in this area", transform=ax.transAxes, ha="center", va="center", color=MUTED, fontsize=8)
    elif len([s for s in series if s[1] is not None]) > 1:
        ax.legend(fontsize=7, frameon=False, labelcolor=INK, loc="upper right")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv")
    parser.add_argument("--out")
    parser.add_argument("--columns", type=int, default=0, help="columns in the area (default: from the file name)")
    parser.add_argument("--each", action="store_true")
    args = parser.parse_args()
    path = Path(args.csv)
    z, columns = read(path)
    radius = next((int(p[1:]) for p in path.stem.split("-") if p.startswith("r") and p[1:].isdigit()), 0)
    if not args.columns and not radius:
        parser.error("the file name has no -r<radius>: give --columns")
    per_column = args.columns or (2 * radius + 1) ** 2
    out = Path(args.out) if args.out else path.with_suffix(".png")
    rows = panels(columns)

    across = (len(rows) + 1) // 2
    fig, axes = plt.subplots(2, across, figsize=(2.5 * across, 8.5), sharey=True, facecolor=SURFACE)
    for ax, (title, series) in zip(axes.flat, rows):
        draw(ax, title, series, z, per_column)
    for ax in axes.flat[len(rows):]:
        ax.set_visible(False)
    for ax in axes[:, 0]:
        ax.set_ylabel("Z (the reference's y)", color=MUTED, fontsize=8)
    fig.suptitle(f"Blocks the ore features (and the ore veins) placed, per Z and per column ({per_column} columns): "
                 f"{path.stem}. "
                 "Solid line Z 0 (deepslate below), dashed line sea level.", color=INK, fontsize=11, x=0.01, ha="left")
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    fig.savefig(out, dpi=110, facecolor=SURFACE)
    print(out)
    if args.each:
        for title, series in rows:
            fig, ax = plt.subplots(figsize=(4, 6), facecolor=SURFACE)
            draw(ax, f"{title}: blocks per Z per column", series, z, per_column)
            ax.set_ylabel("Z (the reference's y)", color=MUTED, fontsize=8)
            fig.tight_layout()
            each = out.with_name(f"{out.stem}-{title}.png")
            fig.savefig(each, dpi=110, facecolor=SURFACE)
            plt.close(fig)
            print(each)


if __name__ == "__main__":
    main()
