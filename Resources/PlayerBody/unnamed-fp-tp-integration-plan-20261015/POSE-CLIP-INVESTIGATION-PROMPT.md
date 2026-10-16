Curiosity investigation, sandbox only: find out why Unreal 5.8.3's Interchange FBX importer mangles our single-pose animation clips, and whether a clean workaround exists. Jesus asked for this out of curiosity; nothing here changes the game. You were started by the planning chat session_01Se4YftFDXKqfe2uM3AkET9; report back to it.

BACKGROUND
- Unnamed (a UE 5.8.3 game) is getting a new 22-bone player character ("UP"), built in Blender 5.2.2 (C:\dev\tools\blender-5.2.2\blender.exe).
- Plan: C:\dev\output\unnamed-fp-tp-integration-plan-20261015\PLAN.md. Read section 4, section 7, and the "M0 status" note under "Open chats".
- Export: one FBX per clip from a Blender export script. Settings:
  - in-memory centimetre copy, FBX_SCALE_NONE, axes -Z/Y
  - no leaf bones, deform bones only
  - armature exported as a Null named "Armature"
  - meshes included as bind-pose carriers
  - take = clip name
  - forced start/end keys, bake step 1, no simplify
- Import: Interchange (the default FBX path in 5.8.3) with explicit pipeline settings, into a blank content-only sandbox project.
- The 46 multi-frame clips import correctly. For example Idle (120 f), Walk_F (36 f), Crouch_FR (54 f) and StrikeA (18 f) each give exactly N+1 keys, exactly N/60 s, 60 Hz, and compressed = raw (0.0 cm).

THE ISSUE
- 20 single-pose clips are affected:
  - 18 look poses: UP_Look_* and UP_SneakLook_* (imported as UP_CrouchLook_*)
  - UP_Pickaxe_Hold
  - UP_Low_Pose
- The game only samples them at t = 0. The looks are mesh-space additives on frame 0 of UP_Look_C or UP_CrouchLook_C.

WHAT WAS TRIED AND WHAT HAPPENED

1. v002b (C:\dev\output\unnamed-player-export-v002b-20261015): pose clips exported as 1 key (frames 0..0).
   - Interchange output:
     - an ensure at FbxAnimation.cpp:1049 (RangeStop > RangeStart)
     - "Cannot fetch FBX animation bake transforms payload because the bake range is invalid"
     - "unexpected number of Baked Transforms"
   - Every pose clip imported as 3 keys, 0.0333 s long.
   - Look_D's pose was 5.385 cm off the Blender round trip. Look_C matched (≤0.00004 cm), but its payload also failed.

2. v002c (C:\dev\output\unnamed-player-export-v002c-20261016): pose clips exported as 2 identical keys (frames 0..1; the two keys are bit-identical in Blender).
   - The "bake range invalid" error is gone.
   - Every pose clip still imports as 3 keys, 0.0333 s long.
   - Each logs "Animation Payload ... has unexpected number of Baked Transforms".
   - An ensure fires at InterchangeAnimSequenceFactory.cpp:579 (once per session).
   - Pose at t=0 against the Blender round trip:
     - C, L and R looks, Low_Pose, Pickaxe_Hold: ≤0.00008 cm
     - every D look: 5.385 cm off
     - every U look: 4.031 cm off
   - A read-only probe found that the SOURCE .blend's own pose differs from the Blender round trip by the same 5.385 / 4.031 cm at the head joint, in exactly the ±90° pitch clips.
     - So Blender's FBX re-import is probably the distorted reference (Euler/gimbal?), and UE probably matches the source.
     - That is inferred from the identical magnitudes, NOT measured: UE's per-bone positions weren't saved.
     - The earlier rotation check was also flawed: quaternion angles weren't normalized (350° vs 10°).

Evidence:
- REPORT.md in each export folder.
- In v002c\evidence\: ue_proofC_logscan.json, pose_source_vs_roundtrip.json, ue_proof_20261016_011906.json.
- The scripts\ folders: export_up.py, the proof importer, the launchers.
- Engine source:
  - C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\Interchange\Runtime\Source\Parsers\Fbx\Private\FbxAnimation.cpp
  - C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\Interchange\Runtime\Source\Import\Private\Animation\InterchangeAnimSequenceFactory.cpp

QUESTIONS (look broadly; pick your own order)

A. Is UE's imported pose actually wrong?
   - For all 20 pose clips, measure UE's t=0 pose per bone against the SOURCE .blend's evaluated pose: component space, cm, shortest-arc rotation angles.
   - If UE matches the source, say so plainly. Then the remaining problem is only the 3-key length and the log noise.

B. Why does Interchange bake 3 keys for a 1- or 2-key take?
   - Read the two engine files around the cited lines.
   - Inspect the FBX itself with a small parser: the take's LocalTime/ReferenceTime span, GlobalSettings TimeSpanStart/Stop, TimeMode, and key times.
   - The FBX SDK isn't installed. Use Blender's io_scene_fbx parse_fbx module or a minimal binary-FBX reader.

C. Why does Blender's own round trip distort the ±90° looks?
   - It could be gimbal lock at exactly 90° in the exported Euler curves, or Blender's importer.
   - Decode the exported Lcl Rotation curves for Look_D and Look_U, and compare them with the source quaternions.
   - This tells us whether the FBX file itself is faithful.

D. Try workarounds and measure each one: keys, length, warnings/ensures, and max per-bone error vs the source. These are ideas, not orders:
   - 3 identical keys over frames 0..2.
   - Interchange animation-range settings, read from the installed pipeline classes: e.g. a set range of 0..1 or 0..0, snap to frame boundary, sample-rate options.
   - The legacy FBX importer for the pose clips only, e.g. Interchange.FeatureFlags.Import.FBX=False in your own sandbox project's config.
   - One multi-frame "pose atlas" clip (one pose per frame) that imports like the healthy clips, then per-pose sequences built from it inside UE.
   - Building the 20 pose sequences directly in UE, with Python and the animation data controller, from the source's bone transforms. No FBX at all.
   - Anything better you find.

   Don't change the authored poses. If a pose would need a tiny change (e.g. 89.9°), propose it but don't apply it.

WHERE AND HOW (hard rules)
- Inputs are read-only: the v002b/v002c export folders, the Blender sources (never save a .blend), and the planning folder.
- Leave C:\dev\sandbox\fpbody-m0 untouched; the M0 work is on hold there.
- Make your own folders:
  - C:\dev\output\up-pose-clip-investigation-<yyyymmdd>\ for scripts, variant exports, evidence and REPORT.md.
  - A fresh blank content-only UE 5.8.3 project in C:\dev\sandbox\up-posefix\. Copy FpBodyM0.uproject and its Config\ if useful. No C++ modules, no build.
- Before EVERY UnrealEditor-Cmd run, check:
  pwsh -NoProfile -File C:\dev\tools\shift-managers\scripts\quiet_window.ps1 -Status
  Never start while a window is clearing or open.
- Editor runs:
  - One editor at a time.
  - Launch through PowerShell: Start-Process -PassThru -WindowStyle Hidden, then $p.PriorityClass='BelowNormal' and $p.WaitForExit(). Not -Wait, and not from the Bash tool.
  - Use absolute .uproject and script paths: -run=pythonscript "-script=<abs path>".
  - Log with -abslog into your evidence folder.
  - Never touch other UnrealEditor processes.
- Never write in any C:\dev\projects\Unnamed* folder or in C:\dev\tools\shift-managers.
- No commits, no pushes.
- Don't message the M0 chat (session_0114WGY52DwNTBxfxvswhMGR) or the survival managers.
- If the auto-mode classifier refuses an action, stop and report. Never retry it reworded.
- Budget: this is curiosity, so stay lean: about 200k tokens and at most 6 editor runs. Stop early once the cause is understood and a workaround is proven, or once it's clear there isn't a clean one.

TIPS
- Python isn't on PATH. Use: uv run --no-project --python 3.13 python -I <script>
- Write scripts with the Write tool; Bash heredocs mangle Windows paths.
- Blender 5 actions have no action.fcurves (layers > strips > channelbags). The export scripts' up_anim.action_fcurves helper handles it.
- Blender headless: blender.exe -b --factory-startup --python <script> -- <args>
- UE Python: Quat has no inverse(); use Transform.inverse().rotation. Find API names in the sandbox's Intermediate\PythonStub\unreal.py after the first run.
- Summarize logs with scripts; don't print them raw.

DELIVERABLE
- REPORT.md in your output folder, containing:
  1. answers to A-C, with engine-source citations;
  2. a table of every variant: keys, length, warnings/ensures, max per-bone error vs the source;
  3. the recommended workaround, and the exact change M0's export/import would need;
  4. open doubts.
- Then send a short summary with send_message (Claude Code Remote MCP) to session_01Se4YftFDXKqfe2uM3AkET9, and stop.
