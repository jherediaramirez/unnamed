"""Count actual warning/error lines, separately from suppressed once-per-process ensures."""
from pathlib import Path
import re,json
OUT=Path(r'C:\dev\output\up-pose-clip-investigation-20261016')
reports={}
for p in (OUT/'evidence').glob('ue_*.log'):
    text=p.read_text(errors='replace')
    variant=None;clip=None;groups={};all_issues=[];summary_replays=[]
    for i,line in enumerate(text.splitlines(),1):
        b=re.search(r'\[POSEFIX_BEGIN\] (\S+) (\S+)',line)
        if b: variant,clip=b.groups()
        if re.search(r': (Warning|Error):|Ensure condition failed:|Fatal error:|Assertion failed:',line):
            if 'LogInit: Display:' in line:
                summary_replays.append({'line':i,'text':line})
                continue
            kind='other'
            if 'unexpected number of Baked Transforms' in line: kind='payload_count'
            elif 'bake range is invalid' in line: kind='invalid_range'
            elif 'Ensure condition failed:' in line: kind='ensure'
            elif 'LogPython: Error:' in line: kind='python_error'
            elif '[Callstack]' in line or ': Error: Stack:' in line or ': Error: === Handled ensure:' in line or re.search(r': Error:\s*$',line): kind='ensure_diagnostic'
            issue={'line':i,'kind':kind,'text':line,'variant':variant,'clip':clip}
            all_issues.append(issue)
            if variant:
                g=groups.setdefault(variant,{'by_kind':{},'by_clip':{}})
                g['by_kind'][kind]=g['by_kind'].get(kind,0)+1
                c=g['by_clip'].setdefault(clip,{})
                c[kind]=c.get(kind,0)+1
        if '[POSEFIX_END]' in line: variant=clip=None
    reports[p.name]={'groups':groups,'all_issues':all_issues,'summary_replays':summary_replays,'completion':'[POSEFIX_COMPLETE]' in text}
(OUT/'evidence/log_scan.json').write_text(json.dumps(reports,indent=2))
print(json.dumps({p:{'groups':{v:g['by_kind'] for v,g in r['groups'].items()},'issues':len(r['all_issues']),'completion':r['completion']} for p,r in reports.items()},indent=2))
