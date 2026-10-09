"""Print a material rule file as a compact indented tree (references shown, not inlined)."""
import json, sys, os
root = sys.argv[1]  # .../worldgen
def cond(c):
    if isinstance(c, str): return "@" + c.replace("minecraft:", "")
    t = c["type"].replace("minecraft:", "")
    a = {k: v for k, v in c.items() if k != "type"}
    if t == "biome":
        b = a["biome_is"]; b = [b] if isinstance(b, str) else b
        return "biome(" + ",".join(x.replace("minecraft:", "") for x in b) + ")"
    if t == "not": return "NOT " + cond(a["invert"])
    if t == "noise_threshold": return f"noise({a['noise'].replace('minecraft:','')},{a['min_threshold']},{a['max_threshold']})"
    return t + "(" + json.dumps(a, separators=(",", ":")) + ")"
def rule(r, d):
    p = "  " * d
    if isinstance(r, str): print(p + "@" + r.replace("minecraft:", "")); return
    t = r["type"].replace("minecraft:", "")
    if t == "sequence":
        print(p + "seq"); [rule(x, d + 1) for x in r["sequence"]]
    elif t == "condition":
        print(p + "if " + cond(r["if_true"])); rule(r["then_run"], d + 1)
    elif t == "block":
        s = r["result_state"]; n = s["Name"].replace("minecraft:", "") if isinstance(s, dict) else s
        props = s.get("Properties") if isinstance(s, dict) else None
        print(p + "=" + n + (str(props) if props else ""))
    else:
        print(p + t + " " + json.dumps({k: v for k, v in r.items() if k != "type"}, separators=(",", ":")))
for f in sys.argv[2:]:
    print("##", f)
    rule(json.load(open(os.path.join(root, f))), 0)
