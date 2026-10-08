"""Verify delivered asset bytes, PNG decoding, metadata, links and bounded coverage."""
from pathlib import Path
import hashlib, io, json, re, zipfile
from collections import Counter
from urllib.parse import unquote
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
m=json.loads((ROOT/'manifest.json').read_text(encoding='utf-8'))
errors=[];png_count=0;size=0;counts=Counter()
jar=Path(m['provenance']['local_jar'])
z=zipfile.ZipFile(jar) if jar.exists() else None
source_compared=0
if z and hashlib.sha1(jar.read_bytes()).hexdigest()!=m['provenance']['client']['sha1']:errors.append('Source client hash mismatch')
index=json.loads((ROOT/'evidence/official-asset-index.json').read_text())['objects']
for r in m['files']:
 p=ROOT/r['path']
 if not p.is_file():errors.append('Missing '+r['path']);continue
 b=p.read_bytes();size+=len(b);counts[r['type']]+=1
 if len(b)!=r['bytes'] or hashlib.sha256(b).hexdigest()!=r['sha256']:errors.append('Byte mismatch '+r['path'])
 if r['source']['kind']=='client.jar' and z:
  source_compared+=1
  if b!=z.read(r['source']['entry']):errors.append('JAR mismatch '+r['path'])
 elif r['source']['kind']=='asset-index-34':
  source_compared+=1; expected=index[r['source']['key']]['hash']
  if hashlib.sha1(b).hexdigest()!=expected:errors.append('Indexed object mismatch '+r['path'])
 if r['type']=='png':
  png_count+=1
  try:
   im=Image.open(io.BytesIO(b));im.load()
   if (im.width,im.height)!=(r['width'],r['height']):errors.append('Dimensions '+r['path'])
  except Exception as e:errors.append('PNG decode '+r['path']+': '+str(e))
  for key in ['purpose','placement','behavior','unreal','reference']:
   if not r.get(key):errors.append('Missing annotation '+key+' '+r['path'])
 if r['type'] in ['json','mcmeta']:
  try:json.loads(b)
  except Exception as e:errors.append('Metadata '+r['path']+': '+str(e))
 if r['type']=='zip':
  try:
   with zipfile.ZipFile(io.BytesIO(b)) as f:
    if f.testzip():errors.append('Font archive CRC '+r['path'])
  except Exception as e:errors.append('Font archive '+str(e))
# Validate local clickable artifacts; skip the verification report being written below.
broken=[]
for p in list(ROOT.glob('*.html'))+list(ROOT.glob('*.md')):
 s=p.read_text(encoding='utf-8')
 targets=re.findall(r'(?:href|src)="([^"]+)"',s) if p.suffix=='.html' else re.findall(r'\]\(([^\)]+)\)',s)
 for t in targets:
  if t.startswith(('http:','https:','data:','#')) or '${' in t:continue
  t=unquote(t.split('#')[0])
  if t=='evidence/verification.json':continue
  if not (p.parent/t).exists():broken.append(p.name+': '+t)
errors.extend('Broken link '+t for t in broken)
coverage=json.loads((ROOT/'evidence/reference-coverage.json').read_text())
errors.extend('Unresolved fixed reference '+str(x) for x in coverage['missing'])
core=sum(r['type']=='png' and '/textures/gui/sprites/hud/' in r['path'] for r in m['files'])
if png_count!=194 or core!=84:errors.append('Unexpected inventory totals')
result={'passed':not errors,'asset_files':len(m['files']),'pngs_decoded':png_count,'core_hud_pngs':core,'types':dict(counts),'asset_bytes':size,'source_files_compared':source_compared,'source_client_available':bool(z),'annotated_pngs':png_count,'fixed_texture_references':len(coverage['fixed_references_covered']),'broken_local_links':broken,'errors':errors,'acceptance':'Asset/reference validation only. Native Minecraft captures and Unreal implementation acceptance are not claimed.'}
(ROOT/'evidence/verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result,indent=2))
raise SystemExit(0 if result['passed'] else 1)
