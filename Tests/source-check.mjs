import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const ref=path.join(root,'Reference/minecraft-menu');
const assets=JSON.parse(fs.readFileSync(path.join(ref,'assets/asset-provenance.json'),'utf8')).assets;
let checked=0;
for(const a of assets){
  const p=path.join(ref,a.path), working=path.join(root,'Content/Menu/Web',a.path);
  const b=fs.readFileSync(p),c=fs.readFileSync(working);
  if(b.length!==a.bytes||crypto.createHash('sha1').update(b).digest('hex')!==a.sha1||!b.equals(c))throw Error(`Asset mismatch: ${a.path}`);
  checked++;
}
const hashes=JSON.parse(fs.readFileSync(path.join(root,'Reference/OriginalHashes.json')));
const sha256=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
if(sha256(path.join(ref,'index.html'))!==hashes.index)throw Error('Reference hash changed');
if(sha256(path.join(root,'Docs/Unreal_Menu_Interface_Prompt.txt'))!==hashes.handoff)throw Error('Supplied menu prompt changed');
const html=fs.readFileSync(path.join(root,'Content/Menu/Web/index.html'),'utf8');
new vm.Script(html.match(/<script>([\s\S]*?)<\/script>/)[1]);
// Ray tracing on needs every geometry resident: the pinned RealtimeMesh asserts on an evicted one (scout S's audit,
// C:\dev\sandbox\scout-rtcrash\facts.md; upstream PR #304; the far LOD record of 2026-10-11).
const engineIni=fs.readFileSync(path.join(root,'Config/DefaultEngine.ini'),'utf8');
if(/^\s*r\.RayTracing\s*=\s*True\s*$/im.test(engineIni)&&!/^\s*r\.RayTracing\.UseReferenceBasedResidency\s*=\s*False\s*$/im.test(engineIni))throw Error('Config/DefaultEngine.ini turns ray tracing on without r.RayTracing.UseReferenceBasedResidency=False: RealtimeMesh asserts on evicted ray tracing geometry (RealtimeMesh PR #304)');
// A test whose block assets are missing (DA_Blocks failed to load, e.g. LFS pointer files) checks nothing: its guard must
// fail the test, not pass it with a warning (review F7, 2026-10-12).
const testsDir=path.join(root,'Source/Unnamed/Tests');
let guards=0;
for(const name of fs.readdirSync(testsDir).filter(n=>n.endsWith('.cpp'))){
  const text=fs.readFileSync(path.join(testsDir,name),'utf8');
  if(/AddWarning\(\s*TEXT\(\s*"DA_Blocks is missing/.test(text))throw Error(`${name} passes a test whose blocks are missing (AddWarning); use AddError`);
  if(/AddError\(\s*TEXT\(\s*"DA_Blocks is missing[^;]*;(?:\s|\/\/[^\n]*)*return\s+true\s*;/.test(text))throw Error(`${name} returns true after a missing-asset error`);
  guards+=(text.match(/AddError\(\s*TEXT\(\s*"DA_Blocks is missing/g)||[]).length;
}
if(!guards)throw Error('No missing-asset guards found in Source/Unnamed/Tests');
// Packages only hold the Reference/ files Unnamed.Build.cs stages: every reference root the game module reads at runtime
// from the project folder needs a RuntimeDependencies line, and the files it reads must be covered (QC R35-3: the sky
// reference wasn't staged, so a package's monsters never burned and its night sky light stayed 15).
const buildCs=fs.readFileSync(path.join(root,'Source/Unnamed/Unnamed.Build.cs'),'utf8');
const staged=[...buildCs.matchAll(/RuntimeDependencies\.Add\(\s*"\$\(ProjectDir\)\/(Reference\/[^"]+)"/g)].map(m=>m[1]);
const covers=file=>staged.some(s=>s.endsWith('/...')?file.startsWith(s.slice(0,-3)):file===s);
const readRoots=new Map();
const walk=dir=>{for(const e of fs.readdirSync(dir,{withFileTypes:true})){
  const p=path.join(dir,e.name);
  if(e.isDirectory()){if(p!==testsDir)walk(p);continue;}
  if(!/\.(cpp|h)$/.test(e.name))continue;
  for(const m of fs.readFileSync(p,'utf8').matchAll(/TEXT\("(Reference\/[^/"]+)/g))readRoots.set(m[1],path.relative(root,p));
}};
walk(path.join(root,'Source/Unnamed'));
if(!readRoots.size)throw Error('No runtime Reference/ reads found in Source/Unnamed');
for(const [refRoot,file] of readRoots)if(!staged.some(s=>s.startsWith(refRoot+'/')))throw Error(`${file} reads ${refRoot} at runtime, but Unnamed.Build.cs doesn't stage it (RuntimeDependencies)`);
// The files those roots' readers open (VoxelWorldgen, VoxelTerrainFactory::ReadInputs, VoxelSkyReference).
const runtimeReads=['Reference/minecraft-worldgen-26.3/data/minecraft/worldgen/noise_settings/overworld.json',
  'Reference/minecraft-worldgen-26.3/facts/biome_parameters.json',
  ...['grass','foliage','dry_foliage'].map(n=>`Reference/minecraft-blocks-26.3/assets/minecraft/textures/colormap/${n}.png`),
  'Reference/minecraft-sky-26.3/data/minecraft/timeline/day.json','Reference/minecraft-sky-26.3/attributes.json'];
for(const f of runtimeReads){
  if(!fs.existsSync(path.join(root,f)))throw Error(`${f} is missing`);
  if(!covers(f))throw Error(`${f} is read at runtime, but Unnamed.Build.cs doesn't stage it (RuntimeDependencies)`);
}
console.log(`PASS: ${checked} original assets and working copies, unchanged reference hash, adapted JavaScript syntax, ray tracing residency off, ${guards} missing-asset guards fail their test, ${readRoots.size} runtime reference roots and ${runtimeReads.length} runtime reference files staged.`);
