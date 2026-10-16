"""M0 proof import: mesh + 6 clips into /Game/Characters/UnnamedPlayer_ProofB, gates 1-8 (raw and compressed)."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue_m0_lib as L  # noqa: E402

PROOF = ["UP_Idle", "UP_Walk_F", "UP_Sneak_FR", "UP_Look_C", "UP_Look_D", "UP_Pickaxe_StrikeA"]
PROOF += sorted(n for n, c in L.CLIPS.items() if c.get("pose_clip") and n not in PROOF)   # all 20 pose clips
out = os.path.join(L.E, "evidence", "ue_proof_" + time.strftime("%Y%m%d_%H%M%S") + ".json")
try:
    L.run("/Game/Characters/UnnamedPlayer_ProofC", PROOF, out)
except Exception as ex:  # noqa: BLE001
    import json
    import traceback
    L.REPORT["fatal"] = traceback.format_exc()
    json.dump(L.REPORT, open(out, "w"), indent=1, default=str)
    L.log("FATAL", ex)
