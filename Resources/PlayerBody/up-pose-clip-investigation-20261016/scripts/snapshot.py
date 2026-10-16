"""Hash read-only investigation inputs; use uv run --no-project --python 3.13 python -I."""
from pathlib import Path
import hashlib, json, sys, datetime

OUT = Path(r'C:\dev\output\up-pose-clip-investigation-20261016')
ROOTS = [Path(r'C:\dev\output') / n for n in (
    'unnamed-player-export-v002b-20261015', 'unnamed-player-export-v002c-20261016',
    'unnamed-fp-tp-integration-plan-20261015')]
SOURCE = Path(r'C:\dev\output\unnamed-player-motion-polish-v002-20261015\blend\UP_Player_arm_polish_v002.blend')
M0 = Path(r'C:\dev\sandbox\fpbody-m0')
def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()
if len(sys.argv) > 1 and sys.argv[1] == 'after':
    before = json.loads((OUT/'evidence/input_hashes_before.json').read_text())
    rows = {p: {'before': h, 'after': sha(Path(p)) if Path(p).is_file() else None} for p,h in before['files'].items()}
    changed = {p:v for p,v in rows.items() if v['before'] != v['after']}
    result = {'checked': len(rows), 'changed': changed, 'source_unchanged': str(SOURCE) not in changed}
    (OUT/'evidence/input_hashes_after.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result))
else:
    paths = [SOURCE, M0/'FpBodyM0.uproject']
    for root in ROOTS + [M0/'Content', M0/'Config']:
        paths.extend(p for p in root.rglob('*') if p.is_file())
    result = {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'files': {str(p):sha(p) for p in sorted(set(paths))}}
    (OUT/'evidence/input_hashes_before.json').write_text(json.dumps(result, indent=2))
    print('Snapshotted', len(result['files']), 'files; source', result['files'][str(SOURCE)])
