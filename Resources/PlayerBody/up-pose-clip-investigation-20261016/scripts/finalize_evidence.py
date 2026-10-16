from pathlib import Path
import hashlib,json,re
OUT=Path(r'C:\dev\output\up-pose-clip-investigation-20261016')
SB=Path(r'C:\dev\sandbox\up-posefix')
def read(n):return json.loads((OUT/'evidence'/n).read_text())
c=read('ue_clean_comparison.json');u=read('ue_clean.json');logs=read('log_scan.json')
assert u['complete']
checks={}
for v in ('clean_three_keys','clean_set_range_0_2'):
    data=c['variants'][v]; actual=u['variants'][v]
    assert data['summary']['source_pass']==20 and data['summary']['additive_pass']==18
    assert all(e['keys']==3 and abs(e['seconds']-2/60)<1e-7 and abs(e['sample_dt']-1/60)<1e-7 for e in data['clips'].values())
    assert all(len(e['track_names'])==22 for e in actual['clips'].values())
    maxscale=max(e['max']['scale_dev'] for e in data['clips'].values())
    assert maxscale<1e-4
    assert all(e['max']['seam_cm']<0.001 and e['max']['seam_deg']<0.01 for e in data['clips'].values())
    checks[v]={'clips':20,'additive_looks':18,'keys':3,'fps':60,'tracks_per_clip':22,'max_scale_deviation':maxscale,'max_source_cm':data['summary']['max_cm'],'max_source_deg':data['summary']['max_deg']}
for v in ('reload_three_keys','reload_set_range_0_2'): assert c['variants'][v]['summary']['source_pass']==20
cleanlog=next(e for n,e in logs.items() if n.startswith('ue_2_'))
assert cleanlog['completion'] and not cleanlog['all_issues']
assert read('editor_run_2.json')['exit_code']==0
assert not read('input_hashes_after.json')['changed']
report=(OUT/'REPORT.md').read_text()
links=re.findall(r'\]\(<([A-Z]:/[^>]+)>\)',report)
for link in links: assert Path(re.sub(r':\d+$','',link)).is_file(),link
checks['clean_log_warning_error_ensure_lines']=0
checks['protected_files_unchanged']=read('input_hashes_after.json')['checked']
checks['local_report_source_links_checked']=len(links)
checks['editor_runs']=len(list((OUT/'evidence').glob('editor_run_*.json')))
(OUT/'evidence/acceptance_summary.json').write_text(json.dumps(checks,indent=2))
paths=[p for p in (SB/'Content').rglob('*') if p.is_file()]
assets={str(p.relative_to(SB)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}
(OUT/'evidence/sandbox_asset_hashes.json').write_text(json.dumps(assets,indent=2))
files=[p for p in OUT.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.name!='deliverable_hashes.json']
manifest={str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)}
(OUT/'evidence/deliverable_hashes.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(checks,indent=2))
print('Hashed',len(assets),'sandbox assets and',len(manifest),'delivery files')
