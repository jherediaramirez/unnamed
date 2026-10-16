"""Build previews/gallery.html: every review image, GIF and video, grouped by stage, newest build only.

    uv run --no-project python scripts/tools/make_gallery.py <work root>
"""
import html
import os
import sys

root = os.path.abspath(sys.argv[1])
pv = os.path.join(root, "previews")
SECTIONS = [
    ("Source audit", ["s00_source_sheet.png", "s00_texture_64grid_x8.png"]),
    ("Character, guides, proportions", ["s01_sheet_views.png", "s01_front_compare.png", "s07_presets_front.png",
                                        "s01_sheet_close.png"]),
    ("Hands: box vs pixel fingers (box chosen)", ["s10_hands_compare.png"]),
    ("Rig tests", ["s02_sheet_A.png", "s02_sheet_B.png", "s02_sheet_hands.png"]),
    ("Locomotion (8 directions x 4 phases)", ["s03_sheet_Walk.png", "s03_sheet_Sprint.png", "s03_sheet_Sneak.png"]),
    ("Locomotion videos (travel on the 1 m grid)", ["loco_Walk_F.mp4", "loco_Walk_FR.mp4", "loco_Walk_R.mp4",
                                                    "loco_Walk_B.mp4", "loco_Walk_BL.mp4", "loco_Sprint_F.mp4",
                                                    "loco_Sprint_R.mp4", "loco_Sneak_F.mp4", "loco_Sneak_FR.mp4",
                                                    "loco_Sneak_B.mp4"]),
    ("Air, turns, stand/sneak", ["s04_sheet_air_turns.png", "air_standing_jump.mp4", "air_edge_fall_firm_landing.mp4",
                                 "turn_Turn_L90.mp4", "turn_Turn_R45.mp4", "turn_SneakTurn_L90.mp4",
                                 "transition_stand_sneak.mp4"]),
    ("Look grid and true first person", ["s05_sheet_look_grids.png", "s05_sheet_first_person.png"]),
    ("Pickaxe strikes", ["s06_strips.png", "s06_compare_AAAA_vs_ABAC_3p.gif", "s06_compare_AAAA_vs_ABAC_1p.gif",
                         "s06_compare_keyframes_3p.png", "s06_compare_keyframes_1p.png",
                         "s06_strikes_over_locomotion.png"]),
    ("Earlier strike drafts (history)", ["iterations/s06_strips_draft1.png", "iterations/s06_strips_draft2.png",
                                         "iterations/s06_strips_draft3.png"]),
]
parts = ["<!doctype html><html><head><meta charset='utf-8'><title>Unnamed Player v001 previews</title><style>",
         "body{background:#16181c;color:#ddd;font:15px Segoe UI,Arial;margin:24px}h1{color:#fff}h2{color:#ffd77a;"
         "border-bottom:1px solid #444;padding-bottom:4px;margin-top:36px}figure{margin:12px 0 24px}img,video{max-width:"
         "100%;border:1px solid #333;background:#000}figcaption{color:#aaa;font-size:13px;margin-top:4px}"
         "nav a{color:#9cf;margin-right:14px}</style></head><body>",
         "<h1>Unnamed Player v001 &mdash; previews</h1><p>Box hands, head preset B, 22 bones, 66 clips. Videos: click "
         "to play (60 fps). Files live in this folder.</p><nav>"]
for i, (title, _) in enumerate(SECTIONS):
    parts.append(f"<a href='#s{i}'>{html.escape(title)}</a>")
parts.append("</nav>")
count = 0
for i, (title, files) in enumerate(SECTIONS):
    parts.append(f"<h2 id='s{i}'>{html.escape(title)}</h2>")
    for f in files:
        p = os.path.join(pv, f)
        if not os.path.exists(p):
            continue
        count += 1
        cap = html.escape(f)
        if f.endswith(".mp4"):
            parts.append(f"<figure><video src='{f}' controls loop muted preload='metadata' width='960'></video>"
                         f"<figcaption>{cap}</figcaption></figure>")
        else:
            parts.append(f"<figure><a href='{f}'><img src='{f}' loading='lazy'></a><figcaption>{cap}</figcaption></figure>")
parts.append("</body></html>")
out = os.path.join(pv, "gallery.html")
open(out, "w", encoding="utf-8").write("\n".join(parts))
print("[gallery]", out, count, "items")
