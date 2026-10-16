"""M0 batch (only after every proof gate passed): mesh + all 66 clips into /Game/Characters/UnnamedPlayer with the
proven settings, gates 1-8 on everything (6-8 per clip), plus SM_UP_Pickaxe, then delete both proof folders (ProofB and the v002a Proof)."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue_m0_lib as L  # noqa: E402

out = os.path.join(L.E, "evidence", "ue_batch_" + time.strftime("%Y%m%d_%H%M%S") + ".json")
try:
    L.run("/Game/Characters/UnnamedPlayer", sorted(L.CLIPS), out, delete_folders_after=("/Game/Characters/UnnamedPlayer_ProofC", "/Game/Characters/UnnamedPlayer_ProofB",
                                                           "/Game/Characters/UnnamedPlayer_Proof"), with_pickaxe=True)
except Exception as ex:  # noqa: BLE001
    import json
    import traceback
    L.REPORT["fatal"] = traceback.format_exc()
    json.dump(L.REPORT, open(out, "w"), indent=1, default=str)
    L.log("FATAL", ex)
