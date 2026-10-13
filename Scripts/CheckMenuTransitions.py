"""Check the menu-transition probe's run (MenuTransitionProbe.cpp, dev-only): classify the frames, time the transitions, pass/fail.

Usage: uv run --no-project --python 3.13 --with pillow python -I CheckMenuTransitions.py <run dir> [--out <dir>]
Reads (only) <run dir>\\tr-pause-frames.csv (one row per frame, 'event' rows in between; optional columns browser_id, cover,
title_ack), the optional shots tr-pause-<SEQ>-f<+/-NNN>.png (shot runs only) and run.log (the 'Switch <name> = <value>' lines,
'LogWebBrowser: Error'). A sequence is the transition letter plus an optional loop number (R1, R2, ...).
Writes (only) into --out, default <run dir>\\check: classes.csv, summary.csv, sheet-<SEQ>.png (shot runs), timing.txt,
checks.txt and table.md. Everything is also printed.
Frame classes (frame >= 0 is after the act; frame < 0 is prefixed 'pre:'):
  cover   the CSV row says a native launch cover is on screen (cover == 1); overrides black, dark and other
  black   more than half the pixels near-black (luma < 12)
  dark    mean luma < 25 but not black (an unexposed world frame)
  title   the title logo where the destination is not the title
  source  still the pre-press frame (frame -1)
  target  the settled destination (the sequence's last frame)
  other   none of these (an interstitial, the loading screen, a partial paint or a wrong screen)
Checks (PASS|FAIL|SKIP <seq> <criterion>: <measured> (want <rule>); SKIP = the data is absent; exit 1 on any FAIL). A
transition with its switch >= 1 gets the 'fixed' criteria, 0 or absent the 'today' criteria (the baseline in facts.md):
  R, F, S  menu.Title.Keep          N  menu.World.WaitForPaint
  P        foundation.Load.Cover    I  foundation.Panels.Preload
  guards K E O V W C D Y J: no black frames and settled by frame 2 (shots) / within 60 ms (log-only); every run: no
  'LogWebBrowser: Error' in run.log.
"""
import argparse
import csv
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageStat

PATTERN = re.compile(r"-pause-(?P<seq>[A-Za-z]+\d*)-f(?P<idx>[+-]?\d+)\.png$")
SWITCH = re.compile(r"Switch (?P<name>[A-Za-z0-9_.]+) = (?P<value>-?\d+)")
SWITCH_NAMES = ("menu.Title.Keep", "menu.World.WaitForPaint", "foundation.Load.Cover", "foundation.Panels.Preload")
LOGO_BOX = (0.25, 0.03, 0.75, 0.24)
CENTRE_BOX = (0.36, 0.30, 0.64, 0.70)
# Inside the title's Singleplayer button (opaque, so the turning panorama never shows): a title frame matches the
# settled title here within BUTTON_SAME whatever the panorama's angle; other screens differ by 28 or more.
BUTTON_BOX = (0.30, 0.455, 0.70, 0.525)
BUTTON_SAME = 5.0
# N's last menu frame: its centre against frame -1's (the Create screen). The Create screen against itself stays under
# about 2; the preparing screen differs by 18 or more at 1920x1080 and 2560x1440 (the whole frame barely changes at some
# GUI scales, so the full-frame classes cannot tell).
PREPARING_CENTRE = 10.0
CENTRE_PRE = {}  # (seq, frame) -> centre difference from frame -1
SAME = 15.0  # mean abs luma difference (160x90 or crop) below which two frames show the same screen
GUARDS = "KEOVWCDYJ"
COLOURS = {"black": (255, 80, 80), "dark": (255, 140, 80), "title": (255, 0, 255), "source": (200, 200, 200),
           "target": (120, 220, 120), "other": (255, 220, 90), "cover": (90, 170, 255)}


def base_of(seq):
    return seq.rstrip("0123456789")


def num(text, default=None):
    try:
        return float(text)
    except (TypeError, ValueError):
        return default


def crop(luma, box, size):
    w, h = luma.size
    return luma.crop(tuple(int(v * s) for v, s in zip(box, (w, h, w, h)))).resize(size, Image.BILINEAR)


def load(path):
    luma = Image.open(path).convert("L")
    hist = luma.histogram()
    return {
        "mean": ImageStat.Stat(luma).mean[0],
        "black_share": sum(hist[:12]) / max(1, sum(hist)),
        "small": luma.resize((160, 90), Image.BILINEAR),
        "logo": crop(luma, LOGO_BOX, (160, 60)),
        "centre": crop(luma, CENTRE_BOX, (120, 120)),
        "button": crop(luma, BUTTON_BOX, (200, 35)),
    }


def diff(a, b):
    return ImageStat.Stat(ImageChops.difference(a, b)).mean[0]


def classify(run, out, timing):
    """Frame classes per sequence from the PNGs. Returns (labels {seq: {frame: label}}, summary {seq: dict})."""
    sequences = {}
    for path in sorted(run.glob("*.png")):
        m = PATTERN.search(path.name)
        if m:
            sequences.setdefault(m["seq"], []).append((int(m["idx"]), path))
    labels, summaries, rows = {}, {}, []
    if not sequences:
        return labels, summaries
    title = None
    s_keys = sorted(k for k in sequences if base_of(k) == "S")
    if s_keys:
        title = load(sorted(sequences[s_keys[0]])[-1][1])
    def is_title(d):
        return title is not None and diff(d["button"], title["button"]) < BUTTON_SAME

    for seq, frames in sequences.items():
        frames.sort()
        data = [(i, p, load(p)) for i, p in frames]
        pre = next((d for i, p, d in reversed(data) if i < 0), None)
        for i, p, d in data:
            CENTRE_PRE[(seq, i)] = diff(d["centre"], pre["centre"]) if pre else None
        last = data[-1][2]
        dest_title = title is not None and (diff(last["logo"], title["logo"]) < SAME or is_title(last))
        seq_rows = []
        for i, p, d in data:
            t = timing.get((seq, i), {})
            if t.get("cover") == "1":
                label = "cover"
            elif d["black_share"] > 0.5:
                label = "black"
            elif d["mean"] < 25:
                label = "dark"
            elif pre is not None and i >= 0 and diff(d["small"], pre["small"]) < SAME and diff(pre["small"], last["small"]) >= SAME:
                label = "source"
            elif diff(d["small"], last["small"]) < SAME or (dest_title and (diff(d["centre"], last["centre"]) < SAME or
                                                                          is_title(d))):
                label = "target"
            elif title is not None and not dest_title and (diff(d["logo"], title["logo"]) < SAME or is_title(d)):
                label = "title"
            else:
                label = "other"
            if i < 0:
                label = "pre:" + label
            seq_rows.append({"seq": seq, "frame": i, "class": label, "mean_luma": round(d["mean"], 1),
                             "black_share": round(d["black_share"], 3),
                             "diff_pre": round(diff(d["small"], pre["small"]), 1) if pre else "",
                             "diff_last": round(diff(d["small"], last["small"]), 1),
                             "ms": t.get("ms", ""), "map": t.get("map", ""), "page": t.get("page", ""), "file": p.name})
        rows += seq_rows
        labels[seq] = {r["frame"]: r["class"] for r in seq_rows}
        after = [r for r in seq_rows if r["frame"] >= 0]
        settled = next((r["frame"] for k, r in enumerate(after) if all(x["class"] == "target" for x in after[k:])), None)
        pick = lambda c: [r["frame"] for r in after if r["class"] == c]
        summaries[seq] = {"seq": seq, "frames_after_press": len(after), "black": pick("black"), "dark": pick("dark"),
                          "cover": pick("cover"), "title": pick("title"),
                          "source_after_press": [f for f in pick("source") if f > 0], "other": pick("other"),
                          "first_settled_frame": settled,
                          "settled_ms_in_shot_run": next((r["ms"] for r in after if r["frame"] == settled), "")}
        thumbs = [(r, Image.open(run / r["file"]).convert("RGB").resize((320, 180))) for r in seq_rows]
        columns = 8
        sheet = Image.new("RGB", (columns * 324, ((len(thumbs) + columns - 1) // columns) * 204), (40, 40, 40))
        draw = ImageDraw.Draw(sheet)
        for n, (r, thumb) in enumerate(thumbs):
            x, y = (n % columns) * 324 + 2, (n // columns) * 204 + 2
            sheet.paste(thumb, (x, y))
            draw.text((x + 2, y + 184), f"{seq} f{r['frame']:+d} {r['class']} L{r['mean_luma']:.0f} {r['ms']}ms",
                      fill=COLOURS.get(r["class"].split(":")[-1], (160, 160, 160)))
        sheet.save(out / f"sheet-{seq}.png")
    with open(out / "classes.csv", "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    with open(out / "summary.csv", "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(next(iter(summaries.values()))))
        writer.writeheader()
        writer.writerows(summaries.values())
    return labels, summaries


def milestones(rows):
    """timing.py's milestones. Returns (per base {key: [values]}, per seq settled ms, seq -> its rows)."""
    steps = defaultdict(list)  # seq -> rows (frames and the events that follow its act)
    current = None
    for r in rows:
        if r["seq"] != "event":
            current = r["seq"]
        if current:
            steps[current].append(r)
    ms_by_base = defaultdict(lambda: defaultdict(list))
    settled_ms = {}
    for seq, rs in steps.items():
        base = base_of(seq)
        frames = [r for r in rs if r["seq"] != "event" and r["frame"] != "" and int(r["frame"]) >= 0]
        events = [r for r in rs if r["seq"] == "event"]
        if not frames:
            continue
        first = frames[0]
        m = ms_by_base[base]
        for e in events:
            for key in ("preload", "postload", "pre-exit"):
                if e["note"].startswith(key):
                    m[key].append(float(e["ms"]))
        new_map = next((r for r in frames if r["map"] != first["map"]), None)
        if new_map:
            m["first frame on new map"].append(float(new_map["ms"]))
            m["frames on old map after act"].append(sum(1 for r in frames if r["map"] == first["map"]))
            m["longest frame gap"].append(max(float(r["dt_ms"]) for r in frames))
        page0 = first["page"]
        on_new = [r for r in frames if (not new_map or r["map"] == new_map["map"])]
        if new_map:
            report = next((r for r in on_new if r["page"]), None)
            if report:
                m["first page report (new page)"].append(float(report["ms"]))
        done, label = None, None
        if base in ("S", "R", "F"):
            done = next((r for r in on_new if r["page"].startswith("main true")), None)
            title = next((r for r in on_new if r["page"].startswith("main")), None)
            if title:
                m["title page rendered"].append(float(title["ms"]))
            label = "title + panorama (settled)"
            loaded = next((r for r in on_new if r["loaded"] == "1"), None)
            if loaded:
                m["document loaded"].append(float(loaded["ms"]))
        elif base in ("P", "N"):
            shown = [r for r in on_new if r["loading_screen"] == "1"]
            if shown:
                m["loading screen frames"].append(len(shown))
                m["loading screen first"].append(float(shown[0]["ms"]))
                m["loading screen last"].append(float(shown[-1]["ms"]))
            done = next((r for r in on_new if r["gameplay"] == "1"), None)
            label = "gameplay (settled)"
        elif base in ("K", "E"):
            done = next((r for r in frames if r["page"].startswith("foundationPause") and r["page"].endswith("false")), None)
            label = "page shown (settled)"
        elif base in ("I", "J"):
            target = "1" if base == "I" else "0"
            done = next((r for r in frames if r["panels_open"] == target), None)
            label = "panel state (settled)"
        elif base != "Q":
            done = next((r for r in frames if r["page"] and r["page"].split(" ")[0] != page0.split(" ")[0]), None)
            label = "page screen changed (settled)"
        if done:
            m[label].append(float(done["ms"]))
            m["frames to settled"].append(int(done["frame"]))
            settled_ms[seq] = float(done["ms"])
    return ms_by_base, settled_ms


def span(values):
    return f"{statistics.median(values):.0f} ({min(values):.0f}-{max(values):.0f}, n={len(values)})"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    run = args.run_dir
    out = args.out or run / "check"
    # Outputs live beside the run's evidence: never overwrite an earlier check (pass a new --out instead).
    if out.exists() and any(out.iterdir()):
        raise SystemExit(f"{out} exists and is not empty; pass a new --out")
    out.mkdir(parents=True, exist_ok=True)
    lines = []

    def emit(text=""):
        print(text)
        lines.append(text)

    # switches
    log_path = run / "run.log"
    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    switches = {n: 0 for n in SWITCH_NAMES}
    for m in SWITCH.finditer(log_text):
        switches[m["name"]] = int(m["value"])
    emit("Switches: " + "; ".join(f"{n}={v}" for n, v in switches.items()))
    # -UseFixedTimeStep decouples the game's frames from CEF's: there the page acts on a click and its paint reaches the
    # game several frames late, so N's picture is not judged (its page report and the real-time runs are).
    fixed_step = "-UseFixedTimeStep" in log_text

    csv_path = run / "tr-pause-frames.csv"
    if not csv_path.exists():
        emit(f"FAIL - csv: {csv_path} missing (want the probe's frame CSV)")
        emit("RESULT FAIL (1 checks, 1 failed, 0 skipped)")
        sys.exit(1)
    with open(csv_path, newline="") as handle:
        rows = list(csv.DictReader(handle))
    for r in rows:  # tolerate older CSVs without the later columns
        for col in ("browser_id", "cover", "title_ack"):
            r.setdefault(col, "")
    has_browser_id = any(r["browser_id"] != "" for r in rows)
    timing = {(r["seq"], int(r["frame"])): r for r in rows if r["seq"] != "event" and r["frame"] != ""}

    labels, summaries = classify(run, out, timing)
    for s in summaries.values():
        emit(str(s))
    ms_by_base, settled_ms = milestones(rows)
    timing_lines = []
    for base, m in ms_by_base.items():
        timing_lines.append(f"{base}: " + "; ".join(f"{k} {span(v)}" for k, v in m.items()))
    (out / "timing.txt").write_text("\n".join(timing_lines) + "\n", newline="\n")
    emit()
    for t in timing_lines:
        emit(t)
    emit()

    # per-sequence ordered frame rows and events
    seq_frames = defaultdict(list)  # seq -> frame rows (all frames, in order)
    seq_events = defaultdict(list)  # seq -> [(index into seq_frames at event time, event row)]
    current = None
    for r in rows:
        if r["seq"] == "event":
            if current:
                seq_events[current].append((len(seq_frames[current]), r))
        else:
            current = r["seq"]
            if r["frame"] != "":
                seq_frames[current].append(r)
    seqs_of = defaultdict(list)
    for seq in seq_frames:
        seqs_of[base_of(seq)].append(seq)

    checks = []  # (status, seq, criterion, measured, rule)

    def check(ok, seq, criterion, measured, rule):
        checks.append(("PASS" if ok else "FAIL", seq, criterion, measured, rule))

    def skip(seq, criterion, why):
        checks.append(("SKIP", seq, criterion, why, None))

    def counts(seq, frames_ok):
        lab = labels[seq]
        return {c: [f for f, l in lab.items() if f in frames_ok and l == c] for c in ("black", "dark", "other", "cover")}

    def after(seq):
        return {f for f in labels.get(seq, {}) if f >= 0}

    # The probe's own failures: a step skipped before it started, a step whose destination never came (cap reached;
    # Z records a fixed count and Q ends with the process by design), or the run's watchdog.
    for r in rows:
        note = r["seq"] == "event" and r.get("note") or ""
        for prefix in ("ready timeout", "cap reached in ", "watchdog exit"):
            if note.startswith(prefix):
                seq = note.split()[-1] if prefix != "watchdog exit" else "-"
                if prefix == "cap reached in " and base_of(seq) in ("Z", "Q"):
                    continue
                check(False, seq, "probe step completed", note, "no ready timeout, cap or watchdog")

    def count_text(c):
        return f"black={len(c['black'])} dark={len(c['dark'])} other={len(c['other'])} cover={len(c['cover'])}"

    def instances(letter, criterion):
        found = sorted(seqs_of.get(letter, []))
        if not found:
            skip(letter, criterion, "sequence not in this run")
        return found

    keep = switches["menu.Title.Keep"] >= 1
    mode = "fixed" if keep else "today"
    # R, F: the new map after the act
    for letter in "RF":
        crit = f"{letter} {mode}"
        for seq in instances(letter, crit):
            fr = seq_frames[seq]
            old = next((r["map"] for r in reversed(fr) if int(r["frame"]) < 0), None)
            if old is None:
                skip(seq, crit, "no frame -1 row")
                continue
            new_rows = [r for r in fr if int(r["frame"]) >= 0 and r["map"] != old]
            if seq in labels:
                c = counts(seq, {int(r["frame"]) for r in new_rows})
                if keep:
                    bad = len(c["black"]) + len(c["dark"]) + len(c["other"])
                    check(bool(new_rows) and bad == 0, seq, "no black/dark/other frames on the new map",
                          count_text(c) + f" in {len(new_rows)} new-map frames", "0, and the map changed (cover allowed)")
                else:
                    ca = counts(seq, after(seq))
                    check(len(ca["black"]) >= 3, seq, "black frames after the act", count_text(ca), "black >= 3")
            else:
                skip(seq, "new-map frame classes" if keep else "black frames", "no PNGs in this run")
            if keep:
                if has_browser_id:
                    s_keys = sorted(seqs_of.get("S", []))
                    if not s_keys:
                        skip(seq, "new-map browser_id is the first menu visit's", "no sequence S")
                    else:
                        want = seq_frames[s_keys[0]][-1]["browser_id"]
                        ids = sorted({r["browser_id"] for r in new_rows} - {"0"})
                        check(bool(new_rows) and ids == [want], seq, "new-map browser_id is the first menu visit's",
                              f"ids {ids}", f"all == {want!r} (last row of {s_keys[0]})")
                else:
                    skip(seq, "new-map browser_id is the first menu visit's", "no browser_id column")
                if seq not in labels:
                    first = new_rows[0] if new_rows else None
                    done = next((r for r in new_rows if r["page"].startswith("main true")), None)
                    if first and done:
                        d = float(done["ms"]) - float(first["ms"])
                        check(d <= 100, seq, "ms from first new-map row to 'main true'", f"{d:.0f} ms", "<= 100 ms")
                    else:
                        check(False, seq, "ms from first new-map row to 'main true'", "no new-map row or no 'main true' page",
                              "<= 100 ms")
                else:
                    skip(seq, "ms from first new-map row to 'main true'", "shot run (frames are slow)")
    # S
    for seq in instances("S", f"S {mode}"):
        if seq in labels:
            c = counts(seq, after(seq))
            if keep:
                check(len(c["black"]) == 0, seq, "black frames after the act", count_text(c), "black == 0")
            else:
                check(len(c["black"]) >= 3, seq, "black frames after the act", count_text(c), "black >= 3")
        else:
            skip(seq, "black frames after the act", "no PNGs in this run")
    # N
    keep_n = switches["menu.World.WaitForPaint"] >= 1
    for seq in instances("N", "N " + ("fixed" if keep_n else "today")):
        evs = [i for i, e in seq_events[seq] if e["note"].startswith("preload")]
        if not evs or evs[0] == 0:
            skip(seq, "last menu frame before the preload event", "no preload event (or no frame before it)")
            continue
        lastf = seq_frames[seq][evs[0] - 1]
        page = lastf["page"]
        # A shot run gives CEF a long wall-clock frame, so its page report runs ahead of the picture: there the frame's
        # class decides (the Create screen still shown = 'source'); in a log-only run the page report does.
        f = int(lastf["frame"])
        if seq in labels and fixed_step:
            skip(seq, "last menu frame's class", "fixed-step run: CEF's paint lags the game's frames")
        elif seq in labels:
            if f in labels[seq]:
                cls = labels[seq][f].replace("pre:", "")
                centre = CENTRE_PRE.get((seq, f))
                shown = f"f{f:+d} {cls}, centre {centre:.1f} from f-1" if centre is not None else f"f{f:+d} {cls}"
                if keep_n:
                    check(cls not in ("black", "dark") and centre is not None and centre >= PREPARING_CENTRE, seq,
                          "last menu frame shows the preparing screen", shown,
                          f"centre >= {PREPARING_CENTRE:g} from the Create screen, not black or dark")
                else:
                    check(centre is not None and centre < PREPARING_CENTRE, seq, "last menu frame still the Create screen",
                          shown, f"centre < {PREPARING_CENTRE:g} from the Create screen")
            else:
                skip(seq, "last menu frame's class", "no PNG for that frame")
        elif keep_n:
            check(page.startswith("generic"), seq, "last menu frame's page", repr(page), "starts with 'generic'")
        else:
            check(not page.startswith("generic"), seq, "last menu frame's page", repr(page), "does not start with 'generic'")
    # P
    keep_p = switches["foundation.Load.Cover"] >= 1
    for seq in instances("P", "P " + ("fixed" if keep_p else "today")):
        if seq in labels:
            c = counts(seq, after(seq))
            if keep_p:
                check(len(c["dark"]) + len(c["black"]) == 0, seq, "dark/black frames after the act", count_text(c),
                      "dark == 0 and black == 0")
            else:
                # facts.md calls the unexposed frame 'dark' (mean luma 15), but 71% of its pixels are below luma 12, so the
                # classifier (black tested first, as in classify.py) labels it black: count either.
                old = next((r["map"] for r in reversed(seq_frames[seq]) if int(r["frame"]) < 0), None)
                new_f = {int(r["frame"]) for r in seq_frames[seq] if int(r["frame"]) >= 0 and old is not None and r["map"] != old}
                cn = counts(seq, new_f)
                check(len(cn["dark"]) + len(cn["black"]) >= 1, seq, "dark or black frames on the new map after the act",
                      count_text(cn), "dark + black >= 1")
        else:
            skip(seq, "dark frames after the act", "no PNGs in this run")
    # I
    keep_i = switches["foundation.Panels.Preload"] >= 1
    for seq in instances("I", "I " + ("fixed" if keep_i else "today")):
        if seq in labels:
            if keep_i:
                c = counts(seq, after(seq))
                check(len(c["other"]) == 0, seq, "'other' frames at frame >= 0", count_text(c), "other == 0")
            else:
                cls = labels[seq].get(0)
                check(cls == "other", seq, "frame 0 class", str(cls), "other")
        else:
            f1 = next((r for r in seq_frames[seq] if int(r["frame"]) == 1), None)
            dt = num(f1["dt_ms"]) if f1 else None
            if dt is None:
                skip(seq, "frame 1 dt_ms", "no frame 1 row")
            elif keep_i:
                # The runs cap at 60 fps, whose own pacing alternates about 17 and 29 ms frames in a world, so the bound
                # is two cap slots: no hitch beyond the cap's jitter (today's open frame takes 55-101 ms).
                check(dt < 35, seq, "frame 1 dt_ms", f"{dt:.0f} ms", "< 35 ms")
            else:
                check(dt >= 40, seq, "frame 1 dt_ms", f"{dt:.0f} ms", ">= 40 ms")
    # guards
    for letter in GUARDS:
        for seq in instances(letter, "guard"):
            if seq in labels:
                c = counts(seq, after(seq))
                check(len(c["black"]) == 0, seq, "black frames", count_text(c), "black == 0")
                sf = summaries[seq]["first_settled_frame"]
                check(sf is not None and sf <= 2, seq, "first settled frame", str(sf), "<= 2")
            else:
                ms = settled_ms.get(seq)
                if ms is None:
                    check(False, seq, "ms to settled", "no settled milestone", "<= 60 ms")
                else:
                    check(ms <= 60, seq, "ms to settled", f"{ms:.0f} ms", "<= 60 ms")
    # web errors
    errors = log_text.count("LogWebBrowser: Error")
    if log_path.exists():
        check(errors == 0, "-", "LogWebBrowser errors in run.log", str(errors), "0")
    else:
        skip("-", "LogWebBrowser errors in run.log", "no run.log")

    result = []
    for status, seq, crit, measured, rule in checks:
        result.append(f"SKIP {seq} {crit}: {measured}" if status == "SKIP" else f"{status} {seq} {crit}: {measured} (want {rule})")
    failed = sum(1 for c in checks if c[0] == "FAIL")
    skipped = sum(1 for c in checks if c[0] == "SKIP")
    result.append(f"RESULT {'FAIL' if failed else 'PASS'} ({len(checks)} checks, {failed} failed, {skipped} skipped)")
    for line in result:
        emit(line)
    (out / "checks.txt").write_text("\n".join(result) + "\n", newline="\n")

    # table
    header = "| Transition | black | dark | other | cover | first settled frame | real ms to settled median (min-max, n) |"
    table = [header, "|---|---|---|---|---|---|---|"]
    for base in sorted(seqs_of):
        shot = [s for s in seqs_of[base] if s in summaries]
        if shot:
            tot = {k: sum(len(summaries[s][k]) for s in shot) for k in ("black", "dark", "other", "cover")}
            cells = [str(tot[k]) for k in ("black", "dark", "other", "cover")]
            sf = [summaries[s]["first_settled_frame"] for s in shot if summaries[s]["first_settled_frame"] is not None]
            frame_cell = f"f{min(sf)}" if sf and min(sf) == max(sf) else (f"f{min(sf)}-f{max(sf)}" if sf else "none")
        else:
            cells, frame_cell = ["-"] * 4, "-"
        vals = [settled_ms[s] for s in seqs_of[base] if s in settled_ms]
        table.append(f"| {base} | " + " | ".join(cells) + f" | {frame_cell} | {span(vals) if vals else '-'} |")
    emit()
    for line in table:
        emit(line)
    (out / "table.md").write_text("\n".join(table) + "\n", newline="\n")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
