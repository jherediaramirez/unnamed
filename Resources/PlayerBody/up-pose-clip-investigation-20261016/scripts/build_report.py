"""Build the final report from recorded measurements, never from inferred success."""
from pathlib import Path
import json
OUT=Path(r'C:\dev\output\up-pose-clip-investigation-20261016')
def read(n): return json.loads((OUT/'evidence'/n).read_text())
one=read('ue_imports_comparison.json'); clean=read('ue_clean_comparison.json')
ue=read('ue_imports.json'); uc=read('ue_clean.json'); scan=read('log_scan.json')
fbx=read('fbx_decoded.json'); rt=read('blender_roundtrip.json'); protection=read('input_hashes_after.json')
assert ue.get('complete') and uc.get('complete')
for v,clips in fbx.items():
    assert len(clips)==20
    timing=clips['UP_Look_D']['key_times']
    for n,e in clips.items():
        assert e['key_times']==timing,(v,n)
        for bone in e['curves'].values():
            for prop in bone.values():
                for channel in prop.values():
                    assert len(set(channel['values']))==1,(v,n)
def fmt(v): return f'{v:.8f}' if isinstance(v,(int,float)) else str(v)
def engine(rel,line): return f'<C:/Program Files/Epic Games/UE_5.8/Engine/{rel}:{line}>'
parser=engine('Plugins/Interchange/Runtime/Source/Parsers/Fbx/Private/FbxAnimation.cpp',1044)
factory=engine('Plugins/Interchange/Runtime/Source/Import/Private/Animation/InterchangeAnimSequenceFactory.cpp',531)
fallback=engine('Plugins/Interchange/Runtime/Source/Import/Private/Animation/InterchangeAnimSequenceFactory.cpp',553)
bind=engine('Plugins/Interchange/Runtime/Source/Import/Private/Animation/InterchangeAnimSequenceFactory.cpp',664)
types=engine('Source/Runtime/Engine/Public/Animation/AnimTypes.h',27)
pipeline=engine('Plugins/Interchange/Runtime/Source/Pipelines/Private/InterchangeGenericAnimationPipeline.cpp',745)
scene=engine('Plugins/Interchange/Runtime/Source/Parsers/Fbx/Private/FbxScene.cpp',1102)
bl='<C:/dev/tools/blender-5.2.2/5.2/scripts/addons_core/io_scene_fbx/import_fbx.py:2620>'
good=clean['variants']['clean_three_keys']['summary']; imp=clean['variants']['clean_set_range_0_2']['summary']
base=one['variants']['two_keys']['summary']
lines=[
'# UP pose-clip investigation — 2026-10-16', '',
f"Engine: `{ue['engine']}`. Blender: `5.2.2 LTS d13f752e3b9c`. Fresh content-only project: `C:\\dev\\sandbox\\up-posefix\\PoseFix.uproject`.", '',
'## Result', '',
f"The 20 v002c poses match the source: maximum {fmt(base['max_cm'])} cm and {fmt(base['max_deg'])} degrees in this reproduction. The three-key export passes {good['source_pass']}/20 source comparisons; importing v002c with a per-pose range of 0..2 passes {imp['source_pass']}/20. See the clean-session log table for warnings and ensures.", '',
'The two problems are independent: Unreal has inconsistent minimum bake lengths; Blender reconnects the head bone and suppresses its translation. The FBX rotations are faithful. No authored pose needs changing.', '',
'## A. Does Unreal match the source?', '',
'Reference: the approved source blend, SHA-256 `26E585D5676D5A8A18A6E6A2FAE5D682DF3470FA7B83A01972CFAE138D4D22C7`. Evaluation follows the exporter: apply each UP action to the deform skeleton, remove its rig-copy constraints and the leftover control-rig action/NLA in memory, never save the blend. Source frames 0, 1 and 2 were bit-identical for every pose.', '',
'Every clip was evaluated at every imported key, in raw and compressed form, with retargeting and root-motion extraction disabled. Positions use component space in centimetres, with Blender `(x,y,z)` mapped to Unreal `(x,-y,z)*100`. Rotation comparison accounts for the fixed imported rest-bone basis: `R_expected = C R_source_pose inverse(R_source_rest) C R_UE_rest`, where `C = diag(1,-1,1)`. Quaternion norms are normalized, and the shortest arc treats q and -q as equal. The actual UE rest, raw, compressed and per-bone errors are saved, not inferred from error magnitudes.', '',
f"Rest-joint position calibration error: {fmt(one['reference_max_cm'])} cm. Gates: source error <=0.001 cm and <=0.01 degrees. The per-clip table below reports maxima across **all bones, all keys, raw and compressed**, so it also bounds t=0.", '',
'| Source clip | v002b cm / deg | v002c cm / deg | Fresh three-key cm / deg | Fresh range 0..2 cm / deg |',
'|---|---:|---:|---:|---:|',
]
for n in sorted(ue['variants']['two_keys']['clips']):
    cells=[]
    for data,v in [(one,'one_key'),(one,'two_keys'),(clean,'clean_three_keys'),(clean,'clean_set_range_0_2')]:
        c=data['variants'][v]['clips'][n]
        cells.append(f"{fmt(c['max']['cm'])} / {fmt(c['max']['deg'])}" if 'max' in c else 'ERROR')
    lines.append('| '+n+' | '+' | '.join(cells)+' |')
lines += ['',
'The existing M0 assets were not loaded or rewritten. These are fresh reproductions from the original v002b/v002c FBX files in the new sandbox.', '',
'## B. Why does Unreal produce three keys?', '',
f"1. [`AnimTypes.h:27-28`]({types}) defines the minimum animation duration as `1/30` second.",
f"2. The [`FBX parser:1044-1062`]({parser}) rejects `stop <= start`. For a valid range, its count is `round(max(stop-start, 1/sample_rate)*sample_rate)+1`. At 60 Hz, the 0..1 take therefore produces **two** transforms.",
f"3. The [`animation factory:531-537, 571-586`]({factory}) instead uses `max(duration, MINIMUM_ANIMATION_LENGTH)` both for the sequence and the expected payload count. At 60 Hz, it requires **three** transforms. This causes the count ensure and warning on two-key clips. A zero-length take also triggers the parser's invalid-range ensure/error.",
f"4. The [`mismatch branch:553-586`]({fallback}) removes a node from the non-animated set, then breaks the animated-track loop on the mismatch. The [`fallback:664-710`]({bind}) can create constant tracks from the FBX node's local transform for other nodes. These exports carry their pose in the static node transforms, explaining why the reproduced poses survive. This is an error path; it is not a reliable import contract for arbitrary files or animated roots.", '',
'### Decoded timing', '',
'FBX tick rate is 46,186,158,000 ticks/second. All 20 files in each variant share these timing values; all keys of each pose curve are constant.', '',
'| Export | Take LocalTime / ReferenceTime | Key ticks | TimeMode / custom fps | Global TimeSpanStart / Stop |',
'|---|---|---|---|---|',
]
for v in ('one_key','two_keys','three_keys'):
    e=fbx[v]['UP_Look_D'];g=e['global'];take=e['takes'][0]
    lines.append(f"| {v} | {take['LocalTime']} / {take['ReferenceTime']} | {e['key_times']} | {g['TimeMode'][0]} / {g['CustomFrameRate'][0]} | {g['TimeSpanStart'][0]} / {g['TimeSpanStop'][0]} |")
lines += ['',
f"The global span is one second even for the short takes. It is not causing the three-key output: [`FbxScene.cpp:1102-1114`]({scene}) reads the animation stack's local span for Source Timeline. [`InterchangeGenericAnimationPipeline.cpp:745-768`]({pipeline}) uses that span, or the requested frame range divided by the sample rate. Snap-to-frame validation sees the already-clamped 1/30-second duration, so it does not extend the actual 0..1 bake request.", '',
'## C. Why does Blender distort the up/down looks?', '',
f"Blender's [`import_fbx.py:2620-2633`]({bl}) automatically sets `child_bone.use_connect = True` when a child's rest head coincides with its parent's tail, even when `force_connect_children=False`. The imported `head` becomes connected to `neck_01`; Blender then ignores the head bone's authored pose translation. The rotation remains correct.", '',
'For Look_D the suppressed local translation is approximately `(0, 2, 5)` cm: its magnitude is `sqrt(29) = 5.385165` cm. For Look_U it is approximately `(0, 3.5, -2)` cm: `sqrt(16.25) = 4.031129` cm. The same translations occur in the crouched and diagonal variants.', '',
f"Controlled intervention: after each default FBX import, change **only the imported head bone's** `use_connect` to false in Edit Mode, reevaluate the same action, and never save the source blend. Across all 20 clips, the maximum position error drops to **{fmt(max(e['head_only_unconnected_max_cm'] for e in rt.values()))} cm**. Default round-trip rotation error is already <= **{fmt(max(e['max_deg'] for e in rt.values()))} degrees**.", '',
f"Independent binary-FBX decoding reconstructs each bone's local translation, XYZ Euler rotation and scale, then composes the hierarchy and exporter axis conversion. Across 60 files (20 each of v002b, v002c and three-key), maximum source error is **{fmt(max(e['max_curve_cm'] for es in fbx.values() for e in es.values()))} cm / {fmt(max(e['max_curve_deg'] for es in fbx.values() for e in es.values()))} degrees**. This directly verifies the exported data, independently of both importers.", '',
'| Clip | Exported head Lcl Rotation XYZ, degrees | Per-clip max FBX/source rotation error |',
'|---|---|---:|',
]
for n in ('UP_Look_D','UP_Look_U'):
    e=fbx['two_keys'][n]
    rot=[e['curves']['head']['Lcl Rotation'][a]['values'][0] for a in 'XYZ']
    lines.append(f"| {n} | {', '.join(f'{x:.8f}' for x in rot)} | {fmt(e['max_curve_deg'])} degrees |")
lines += ['',
'The +/-90-degree look label is a total authored look angle distributed across bones; the decoded head Euler triplets above are not a singular +/-90-degree middle-axis rotation. No 89.9-degree pose edit is needed.', '',
'## D. Measured variants', '',
'Counts below are actual log lines: payload-count warnings, invalid-range errors, and ensure emissions. Ensures are normally emitted once per source location per process, so zero later in the mixed session is not independent evidence. The second session imports only the workarounds and supplies the clean-session proof.', '',
'| Variant | Clips | Keys | Length (s) | Payload warnings / range errors / ensures | Max source cm / deg | Source pose checks passing |',
'|---|---:|---|---|---|---:|---:|',
]
groups={}
for log,r in scan.items():
    for v,g in r['groups'].items(): groups[v]=g['by_kind']
for data in (one,clean):
    for v,e in data['variants'].items():
        s=e['summary'];g=groups.get(v,{})
        counts='n/a (reload)' if v.startswith('reload_') else f"{g.get('payload_count',0)} / {g.get('invalid_range',0)} / {g.get('ensure',0)}"
        lines.append(f"| {v} | {s['clips']} | {s['keys']} | {', '.join(fmt(x) for x in s['lengths'])} | {counts} | {fmt(s['max_cm'])} / {fmt(s['max_deg'])} | {s['source_pass']}/{s['clips']} |")
lines += ['',
'The 30 Hz trial changes the pose sampling rate and is an alternative only; the recommended routes retain 60 Hz. Set Range 0..0, 0..1 and snap-only were diagnostic controls. Legacy import, pose atlas and direct controller construction were not pursued once the small changes were proven.', '',
'Passing source-pose comparisons do not make the baseline imports acceptable: their payload warnings/errors still fail clean-import acceptance.', '',
'Run 1 completed every measurement and intentionally exited with code 1 because it reproduced the invalid-range errors. The one-key baseline emitted four distinct ensures (invalid range, missing payload file, missing payload collection and transform-count mismatch). Later two-key failures reused the already-triggered count ensure. Shutdown log summaries repeat earlier messages; the scanner separates those replays from actual emissions. Winning imports have all 22 bone tracks; the failing Look_D baselines contain only 19 fallback tracks, including no root track.', '',
'### Compression, additive settings and restart', '',
]
for v in ('clean_three_keys','clean_set_range_0_2'):
    e=clean['variants'][v];cs=e['clips'].values()
    pos=max(c['max']['compressed_vs_raw_cm'] for c in cs);rot=max(c['max']['compressed_vs_raw_deg'] for c in cs)
    seam=max(c['max']['seam_cm'] for c in cs)
    lines.append(f"- `{v}`: raw/compressed maximum {fmt(pos)} cm / {fmt(rot)} degrees; maximum across-key position difference {fmt(seam)} cm. All {e['summary']['additive_pass']}/{e['summary']['additive_count']} looks also match after mesh-space additive configuration and full-pose reconstruction.")
lines += ['',
'Both look grids use mesh-space rotation additive, AnimFrame base, frame 0, and their own central look as reference. Previously saved winning assets were loaded and remeasured in the second editor process (`reload_*` rows). This checks serialization and compressed pose reconstruction, not gameplay or rendering acceptance.', '',
'### Session log accounting', '',
]
for n,r in scan.items():
    counts={}
    for issue in r['all_issues']: counts[issue['kind']]=counts.get(issue['kind'],0)+1
    lines.append(f"- `{n}`: completion marker `{r['completion']}`; log issues by category `{counts}`. Full line-level records: `evidence/log_scan.json`.")
lines += ['',
'## Recommended M0 changes', '',
'For the current v002c exports, use **Set Range 0..2 at 60 Hz only for the 20 pose clips**. It reuses the original FBX hashes, requests three valid samples of their constant curves, and preserves the authored poses. Keep the normal Source Timeline path for the 46 moving clips. The [Epic import reference](https://dev.epicgames.com/documentation/unreal-engine/interchange-import-reference-in-unreal-engine?lang=en-US) documents the range and sample-rate controls; the installed source and measurements above establish their behavior in 5.8.3.', '',
'In M0, add the following immediately after creating each animation pipeline with `make_pipeline(False, skel, un)`, before `do_import`. Here `clip` is the manifest entry and `pipeline` is that newly created pipeline. Keep the existing explicit 30 Hz override disabled.', '',
'```python',
"ap = pipeline.get_editor_property('animation_pipeline')",
"if clip.get('pose_clip'):",
"    ap.set_editor_property('animation_range', unreal.InterchangeAnimationRange.SET_RANGE)",
"    interval = unreal.Int32Interval()",
"    interval.set_editor_property('min', 0)",
"    interval.set_editor_property('max', 2)",
"    ap.set_editor_property('frame_import_range', interval)",
"    ap.set_editor_property('custom_bone_animation_sample_rate', 60)",
'```', '',
'Record separate source and imported timing in the proof: source keys 2, source span 1/60 s; imported keys 3, imported span 2/60 s. Set the pose-specific expected frame count to 2 and verify all three keys against the source. Keep `pose_clip: true` and the runtime t=0 policy. Do not relabel the unchanged v002c FBX as a three-key source.', '',
'For the next export revision, the equally proven export-only solution is `scene.frame_end = 2` when the original action has frame count 0, with the existing bake step 1, simplify 0, forced endpoints and 60 fps. Record `source_frames: 0`, `frames: 2`, `keys: 3`, `seconds: 2/60`, `pose_clip: true`; import with Source Timeline. The actual tested exporter is `scripts/export_three_keys.py`, derived from v002c and restricted to these 20 clips. Both solutions are alternatives; applying both is unnecessary.', '',
'Replace the old round-trip pose oracle with direct source poses. If retaining Blender round-trip checks, disable the imported head bone connection in memory before evaluation. Replace the old rotation-angle subtraction with normalized shortest-arc quaternion comparison; scalar angle-to-rest differences do not prove orientation equality.', '',
'No M0 exporter/importer, planning file, original FBX, original blend or game asset was modified by this investigation.', '',
'## Evidence, preservation and limits', '',
f"- {len(list((OUT/'evidence').glob('editor_run_*.json')))} editor starts, below the six-run limit. Each launch recorded a clear quiet-window check, no other editor, BelowNormal priority, and an absolute script/project/log path. Other test processes caused deferral before the first run; none was touched.",
'- Concurrency incident: another task started editor PID 44256 at 01:47:41 EDT, after run 1 had started with a clear process check at 01:46:30. Our run exited at 01:47:53. That externally initiated overlap lasted about 12 seconds. No other process was touched; the second run waited for a clear launch slot again. This report does not claim exclusivity throughout run 1.',
f"- Preservation: {protection['checked']} original input files rehashed; changed files: {len(protection['changed'])}. Source unchanged: {protection['source_unchanged']}. See `input_hashes_before.json` / `input_hashes_after.json`.",
'- `source_poses.json`: evaluated source matrices and parents.',
'- `fbx_decoded.json`: per-file timing, node properties, curve keys, and per-bone source errors.',
'- `blender_roundtrip.json`: default round-trip poses, head connection state and head-only intervention.',
'- `ue_imports.json`, `ue_clean.json`: per-bone raw/compressed transforms, effective pipelines, asset paths and additive readback.',
'- `ue_*_comparison.json`: every measured per-bone/frame error and aggregate maxima.',
'- `source_excerpts.json`: installed engine/Blender source hashes and cited excerpts.',
'- `variants/three_keys/UP_manifest_posefix3.json`: the tested three-key variant and file hashes.',
'- Two setup issues were corrected before any editor run: bytes in the FBX JSON serializer, and an initial diagnostic hierarchy reader that treated bone-to-skin-cluster connections as parents. The corrected reader restricts parents to Model nodes/root; original diagnostic logs are retained. Neither issue changed input assets.', '',
'Open boundaries: the report proves these 20 poses and these importer settings on the installed engine. It does not certify a game integration, the remaining moving library, packaged runtime, visual playback, network behavior or comfort. M0 remains on hold; its final library/pickaxe/cleanup work was not resumed. No commits, pushes or game builds were made.', '',
'The specified Claude Code Remote messaging tool was not exposed in this session, and plugin discovery did not find a matching integration. No message was sent to the planning chat or other agents. A copy-ready summary is in `PLANNING-HANDOFF.txt`; other integrations may be available in the [plugin directory](https://chatgpt.com/plugins).', '',
]
(OUT/'REPORT.md').write_text('\n'.join(lines),encoding='utf-8')
print('Wrote REPORT.md')
