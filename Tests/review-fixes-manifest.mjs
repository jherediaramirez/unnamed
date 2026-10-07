import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const backup='C:/dev/backups/Unnamed/review-fixes-20261007-104750';
const packageRoot=path.join(root,'Packages/MenuReviewFixesDevelopment/Windows');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').trimStart());
const digest=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const record=(file,base=root)=>({path:path.relative(base,file).replaceAll('\\','/'),bytes:fs.statSync(file).size,sha256:digest(file)});
const walk=dir=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(entry=>{
  const file=path.join(dir,entry.name);return entry.isDirectory()?(entry.name==='Saved'?[]:walk(file)):[file];
});
const baseline=read(path.join(backup,'PreservationManifest.json'));
if(execFileSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim()!==baseline.head)throw Error('HEAD changed');
const indexEntries=index=>execFileSync('git',['ls-files','--stage','-z'],{cwd:root,env:{...process.env,GIT_OPTIONAL_LOCKS:'0',GIT_INDEX_FILE:index}});
if(!indexEntries(path.join(backup,'.git/index')).equals(indexEntries(path.join(root,'.git/index'))))throw Error('Git index entries changed');
const preserved=baseline.files.filter(file=>{
  const name=file.path.replaceAll('\\','/');
  return name.startsWith('Source/')||name.startsWith('Config/')||name==='Content/Menu/L_Menu.umap'||name==='Unnamed.uproject'||
    name.startsWith('Saved/Config/')||name.startsWith('Saved/SaveGames/')||
    name.startsWith('Tests/Captures/')||/^Tests\/(?:.*Results\.json|.*ArtifactManifest\.json|.*UnitTests\.tap|AdditionalControls\.json|VideoResetControls\.json)$/.test(name)||
    /^Docs\/Independent/.test(name);
}).map(file=>{
  const current=record(path.join(root,file.path));
  if(current.sha256!==file.sha256.toLowerCase())throw Error('Preservation mismatch: '+file.path);
  return current;
});
const previous=read(path.join(root,'Tests/NoRealmsArtifactManifest.json'));
const inheritedNative=Object.fromEntries(['packaging','nativeAutomation','editorModule'].map(key=>{
  const evidence=previous[key],file=path.join(root,evidence.path);
  if(digest(file)!==evidence.sha256)throw Error('Inherited native evidence changed: '+key);
  return [key,record(file)];
}));
for(const file of previous.files){
  if(digest(path.join(previous.packageRoot,file.path))!==file.sha256)throw Error('Previous package changed: '+file.path);
  if(file.path!=='Unnamed/Content/Menu/Web/index.html'&&digest(path.join(packageRoot,file.path))!==file.sha256)
    throw Error('Unexpected non-HTML candidate change: '+file.path);
}
const html=path.join(root,'Content/Menu/Web/index.html');
if(digest(html)!==digest(path.join(packageRoot,'Unnamed/Content/Menu/Web/index.html')))throw Error('Source/package HTML mismatch');
const reference=path.join(root,'Reference/minecraft-menu/index.html');
if(digest(reference)!==read(path.join(root,'Reference/OriginalHashes.json')).index)throw Error('Reference changed');
const unitFile=path.join(root,'Tests/ReviewFixesUnitTests.tap');
if(!/# pass 25\b/.test(fs.readFileSync(unitFile,'utf8'))||!/# fail 0\b/.test(fs.readFileSync(unitFile,'utf8')))throw Error('Unit evidence missing');
const resultNames=[
  'Tests/Captures/review-accepted-drafts-1280x720/results.json','Tests/Captures/review-accepted-drafts-1920x1080/results.json',
  ...['1280x720','1920x1080'].flatMap(profile=>[
    `Tests/Captures/review-accepted-video-${profile}/results.json`,`Tests/Captures/review-accepted-packaged-${profile}/results.json`
  ]),'Tests/ReviewFixesFinalQuitResults.json'
];
const runs=resultNames.map(name=>{
  const file=path.join(root,name),result=read(file);
  if(!result.checks.length||result.checks.some(check=>!check.pass))throw Error('Runtime evidence failed: '+name);
  if(result.htmlSHA256&&result.htmlSHA256!==digest(html))throw Error('Capture HTML identity mismatch: '+name);
  return {...record(file),passed:result.checks.length,total:result.checks.length};
});
const cleanup=read(path.join(root,'Tests/review-accepted-ValidationResults.json'));
if(cleanup.checks.length!==6||cleanup.checks.some(check=>!check.pass))throw Error('Diagnostic ownership/cleanup failed');
const registry=read(path.join(root,'Docs/ControlRegistry.json'));
if(registry.htmlSHA256!==digest(html)||registry.captures.join(',')!=='review-accepted-packaged-1920x1080,review-accepted-video-1920x1080')
  throw Error('Registry is not from the current selected captures');
const report={
  generated:new Date().toISOString(),stage:'implemented_verified_with_remaining_human_gates',configuration:'Win64 Development',
  startingHead:baseline.head,indexEntriesMatch:true,backup,snapshotFiles:baseline.files.length,packageRoot,
  implementation:'Five independent-review fixes. Presentation/runtime-JavaScript only; native build, binaries and editor safety evidence inherited unchanged.',
  previousPackage:{...record(path.join(root,'Tests/NoRealmsArtifactManifest.json')),filesPreserved:previous.files.length,nonHTMLCandidateFilesMatch:true},
  inheritedNative,
  ui:record(html),reference:record(reference),unitTests:{...record(unitFile),passed:25,total:25},
  runtimeChecks:runs.reduce((sum,run)=>sum+run.passed,0),runs,diagnosticCleanup:record(path.join(root,'Tests/review-accepted-ValidationResults.json')),
  graphicsAPIReadback:record(path.join(root,'Tests/ReviewFixesGraphicsAPI.json')),
  controlRegistry:{...record(path.join(root,'Docs/ControlRegistry.json')),controls:registry.controls.length,routes:33},
  sourceFiles:['Source','Scripts','Config'].flatMap(dir=>walk(path.join(root,dir))).map(file=>record(file)),
  testSources:fs.readdirSync(path.join(root,'Tests')).filter(name=>/\.(mjs|json)$/.test(name)&&(!name.endsWith('.json')||name.startsWith('package'))).map(name=>record(path.join(root,'Tests',name))),
  documentation:walk(path.join(root,'Docs/Documented Actions')).map(file=>record(file)),
  preserved,files:walk(packageRoot).map(file=>record(file,packageRoot)),
  remainingGates:['Physical OS input/focus','Driver-level VSync/tearing','Multi-monitor/DPI','Real transport-stall injection (VM simulation tested)','Shipping/other platforms','Future gameplay save/quit lifecycle']
};
const out=path.join(root,'Tests/ReviewFixesArtifactManifest.json');
if(fs.existsSync(out))throw Error('Manifest exists; preserve the accepted record before selecting a new output.');
fs.writeFileSync(out,JSON.stringify(report,null,2)+'\n');
console.log(`Verified ${report.files.length} package files, ${preserved.length} protected records, 25 units and ${report.runtimeChecks} runtime checks. Native code unchanged; source/package HTML matches.`);
