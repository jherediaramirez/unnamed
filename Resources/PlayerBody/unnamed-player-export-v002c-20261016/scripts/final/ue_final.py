"""M0 final library: mesh (fresh skeleton) + T_UP_Skin + all 66 clips (pose clips via Set Range 0..2 @ 60 Hz) +
SM_UP_Pickaxe into /Game/Characters/UnnamedPlayer; gates 1-8 (8 scoped to the folder), source-pose and additive checks.
No deletion here: ue_cleanup.py runs only after the log scan is clean."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue_m0_lib as L  # noqa: E402

out = os.path.join(L.FINAL, "ue_final_" + time.strftime("%Y%m%d_%H%M%S") + ".json")
try:
    L.run("/Game/Characters/UnnamedPlayer", sorted(L.CLIPS), out, with_pickaxe=True)
except Exception as ex:  # noqa: BLE001
    import json
    import traceback
    L.REPORT["fatal"] = traceback.format_exc()
    json.dump(L.REPORT, open(out, "w"), indent=1, default=str)
    L.log("FATAL", ex)
