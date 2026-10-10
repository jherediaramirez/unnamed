"""Independent checker: reproduces every torch-placement matrix entry from the look-order table and the support config.

Rule under test (derived from the matrix):
  order  = Direction.orderedByNearest(player) (table 'nearest');
           for a plain (non-replaceable) click, the direction INTO the clicked block (opposite of the clicked face)
           is moved to the front, the rest keep their order; for a replaceable click the order is unchanged.
  Wall candidate W = the first horizontal direction h in that same order whose neighbour (destination+h) supports;
  the wall torch then faces opposite(h). None if no side supports.
  Walk the order: skip U; D -> floor torch if the cell below the destination supports, else continue;
  any horizontal direction -> W if it exists, else continue. The first hit wins; if none, nothing is placed.
  Destination = clicked cell if replaceable, else clicked + face.
Supporting blocks here: stone, grass_block (full cubes). Air does not support.
Usage: python check_rule.py <torch-placement-26.3.json>
"""
import json
import sys

OPP = {"D": "U", "U": "D", "N": "S", "S": "N", "W": "E", "E": "W"}
WALL = {"N": "s", "S": "n", "W": "e", "E": "w"}  # look direction -> wall_torch facing code
CFG_POS = {"D": 0, "N": 1, "S": 2, "W": 3, "E": 4}  # cfg chars B,N,S,W,E


def ctx_order(nearest, case, face):
    if case == "B":
        return nearest
    first = OPP[face]
    return first + nearest.replace(first, "")


def predict(order, cfg):
    # The wall candidate is the same for every horizontal step: the first horizontal direction, in the same
    # order, whose neighbour supports; None if no side supports.
    wall = next((WALL[h] for h in order if h in WALL and cfg[CFG_POS[h]] != "-"), None)
    for d in order:
        if d == "U":
            continue
        if d == "D":
            if cfg[CFG_POS["D"]] != "-":
                return "F"
        elif wall is not None:
            return wall
    return "-"


def predict_closed_form(nearest, case, face, cfg):
    # Same rule, closed form: plain side click -> wall on the clicked block; plain top click -> floor torch;
    # otherwise (underside or replaceable): floor torch if the floor supports and (nearest[0] == D or no side
    # supports); else wall on the first supported side in nearest's horizontal order; else nothing.
    if case == "A" and face in WALL:
        return WALL[OPP[face]]
    if case == "A" and face == "U":
        return "F"
    sides = [h for h in nearest if h in WALL and cfg[CFG_POS[h]] != "-"]
    floor = cfg[CFG_POS["D"]] != "-"
    if floor and (nearest[0] == "D" or not sides):
        return "F"
    return WALL[sides[0]] if sides else "-"


def main():
    data = json.load(open(sys.argv[1], encoding="utf-8"))
    looks = data["looks"]
    bad_ctx = 0
    for lk in looks:
        for key, val in lk["ctx"].items():
            case, face = key[0], key[1]
            want = ctx_order(lk["nearest"], case, face) + ("r" if case == "B" else "p") + "@D"
            if val != want:
                bad_ctx += 1
                if bad_ctx <= 5:
                    print("ctx mismatch", lk["i"], key, val, want)
    total = mism = 0
    for row in data["results"]:
        case, face, cfg, r = row["case"], row["face"], row["cfg"], row["r"]
        assert len(r) == len(looks)
        for i, lk in enumerate(looks):
            total += 1
            p = predict(ctx_order(lk["nearest"], case, face), cfg)
            if predict_closed_form(lk["nearest"], case, face, cfg) != p:
                mism += 1
                print("closed form disagrees", case, face, cfg, i)
            if p != r[i]:
                mism += 1
                if mism <= 10:
                    print("MISMATCH", case, face, cfg, "look", i, "yaw", lk["yaw"], "pitch", lk["pitch"], "got", r[i], "want", p)
    print(f"ctx orders checked: {len(looks) * 12}, mismatches: {bad_ctx}")
    print(f"matrix entries checked: {total}, mismatches: {mism}")
    print(f"others: {len(data['others'])}, anomalies: {len(data['anomalies'])}")
    sys.exit(1 if (mism or bad_ctx) else 0)


if __name__ == "__main__":
    main()
