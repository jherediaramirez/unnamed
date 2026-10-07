import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const debugFollowup = process.argv.includes('--debug-reset');
const quitFollowup = process.argv.includes('--quit');
const noRealmsFollowup = process.argv.includes('--no-realms');
const nativeQuit = quitFollowup || noRealmsFollowup;
const packageRoot = path.join(root, noRealmsFollowup ? 'Packages/MenuNoRealmsDevelopment/Windows' : quitFollowup ? 'Packages/MenuQuitDevelopment/Windows' : debugFollowup ? 'Packages/VideoSettingsDebugDevelopment/Windows' : 'Packages/VideoSettingsDevelopment/Windows');
const backup = 'C:/dev/backups/Unnamed/video-settings-20261007-081817';
const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const readJSON = file => JSON.parse(fs.readFileSync(file, 'utf8').trimStart());
const walk = dir => fs.readdirSync(dir, {withFileTypes: true}).flatMap(entry => {
  const file = path.join(dir, entry.name);
  return entry.isDirectory() ? (entry.name === 'Saved' ? [] : walk(file)) : [file];
});
const record = (file, base = root) => ({
  path: path.relative(base, file).replaceAll('\\', '/'),
  bytes: fs.statSync(file).size,
  sha256: digest(file)
});
const baseline = readJSON(path.join(backup, 'PreservationManifest.json'));
const indexEntries = index => execFileSync('git', ['ls-files', '--stage', '-z'], {
  cwd: root,
  env: {...process.env, GIT_OPTIONAL_LOCKS: '0', GIT_INDEX_FILE: index}
});
const originalEntries = indexEntries(path.join(backup, '.git/index'));
const currentEntries = indexEntries(path.join(root, '.git/index'));
if (!originalEntries.equals(currentEntries)) throw Error('Git index entries changed');
const preserved = baseline.files.filter(file =>
  file.path === 'Unnamed.uproject' ||
  file.path.startsWith('Config\\') || file.path === 'Content\\Menu\\L_Menu.umap' ||
  (file.path.startsWith('Packages\\MenuDevelopment\\') && !file.path.includes('\\Saved\\'))
).map(file => {
  const current = record(path.join(root, file.path));
  if (current.sha256 !== file.sha256.toLowerCase()) throw Error(`Preservation mismatch: ${file.path}`);
  return current;
});
const working = path.join(root, 'Content/Menu/Web/index.html');
if (digest(working) !== digest(path.join(packageRoot, 'Unnamed/Content/Menu/Web/index.html')))
  throw Error('Package HTML differs from source');
const reference = path.join(root, 'Reference/minecraft-menu/index.html');
if (digest(reference) !== readJSON(path.join(root, 'Reference/OriginalHashes.json')).index)
  throw Error('Reference HTML changed');
const logFile = path.join(root, nativeQuit ? 'Tests/Evidence/2026-10-07/QuitBuild.log' : 'Tests/Evidence/2026-10-07/VideoBuild.log');
const log = fs.readFileSync(logFile, 'utf8');
if (!log.includes('BUILD SUCCESSFUL') || !log.includes('ExitCode=0 (Success)'))
  throw Error('Successful packaging evidence missing');
const automationFile = path.join(root, nativeQuit ? 'Tests/Evidence/2026-10-07/MenuAutomation-20261007-091836/index.json' : 'Tests/Evidence/2026-10-07/VideoAutomation-20261007-083946/index.json');
const automation = readJSON(automationFile);
if (automation.succeeded !== (nativeQuit ? 2 : 1) || automation.failed !== 0 || automation.notRun !== 0)
  throw Error('Native automation did not pass');
const resultFiles = noRealmsFollowup ? [
  'Tests/Evidence/2026-10-07/Captures/no-realms-video-1280x720/results.json',
  'Tests/Evidence/2026-10-07/Captures/no-realms-video-1920x1080/results.json',
  'Tests/Evidence/2026-10-07/Captures/no-realms-packaged-1280x720/results.json',
  'Tests/Evidence/2026-10-07/Captures/no-realms-packaged-1920x1080/results.json',
  'Tests/Evidence/2026-10-07/NoRealmsQuitResults.json'
] : quitFollowup ? [
  'Tests/Evidence/2026-10-07/Captures/quit-video-1280x720/results.json',
  'Tests/Evidence/2026-10-07/Captures/quit-video-1920x1080/results.json',
  'Tests/Evidence/2026-10-07/Captures/quit-packaged-1280x720/results.json',
  'Tests/Evidence/2026-10-07/Captures/quit-packaged-1920x1080/results.json',
  'Tests/Evidence/2026-10-07/QuitGameResults.json',
  'Tests/Evidence/2026-10-07/QuitEditorSafetyResults.json'
] : debugFollowup ? [
  'Tests/Evidence/2026-10-07/Captures/debug-reset-1280x720/results.json',
  'Tests/Evidence/2026-10-07/Captures/debug-reset-1920x1080/results.json'
] : [
  'Tests/Evidence/2026-10-07/Captures/video-settings-1280x720/results.json',
  'Tests/Evidence/2026-10-07/Captures/video-settings-1920x1080/results.json',
  'Tests/Evidence/2026-10-07/Captures/video-packaged-1280x720/results.json',
  'Tests/Evidence/2026-10-07/Captures/video-packaged-1920x1080/results.json',
  'Tests/Evidence/2026-10-07/VideoPersistenceResults.json'
];
const results = resultFiles.map(file => {
  const result = readJSON(path.join(root, file));
  const passed = result.checks.filter(check => check.pass).length;
  if (passed !== result.checks.length) throw Error(`Failed runtime checks: ${file}`);
  return {...record(path.join(root, file)), passed, total: result.checks.length, finished: result.finished || null};
});
const sourceFiles = ['Source', 'Scripts', 'Config'].flatMap(dir => walk(path.join(root, dir))).map(file => record(file));
let previousPackage = null;
if (debugFollowup || nativeQuit) {
  const previousManifest = noRealmsFollowup ? 'Tests/Evidence/2026-10-07/QuitArtifactManifest.json' : quitFollowup ? 'Tests/Evidence/2026-10-07/DebugResetArtifactManifest.json' : 'Tests/Evidence/2026-10-07/VideoArtifactManifest.json';
  const previous = readJSON(path.join(root, previousManifest));
  for (const file of previous.files) {
    if (digest(path.join(previous.packageRoot, file.path)) !== file.sha256)
      throw Error(`Previous video package changed: ${file.path}`);
    if (!quitFollowup && file.path !== 'Unnamed/Content/Menu/Web/index.html' && digest(path.join(packageRoot, file.path)) !== file.sha256)
      throw Error(`Unexpected non-HTML candidate change: ${file.path}`);
    if (quitFollowup && file.path.startsWith('Unnamed/Content/Menu/Web/assets/') && digest(path.join(packageRoot, file.path)) !== file.sha256)
      throw Error(`Unexpected asset change: ${file.path}`);
  }
  previousPackage = {...record(path.join(root, previousManifest)), previousFilesPreserved:true, candidateNativeFilesChanged:quitFollowup};
  if(noRealmsFollowup)for(const file of previous.sourceFiles.filter(file=>file.path.startsWith('Source/')))
    if(digest(path.join(root,file.path))!==file.sha256)throw Error(`Native source changed during presentation-only follow-up: ${file.path}`);
}
const report = {
  generated: new Date().toISOString(),
  stage: 'implemented_verified_with_remaining_human_gates',
  engine: '5.8.3 CL 58210709',
  configuration: 'Win64 Development',
  packageRoot,
  backup,
  startingHead: baseline.head,
  index: {
    baseline: record(path.join(backup, '.git/index'), backup),
    current: record(path.join(root, '.git/index')),
    entriesMatch: true,
    entriesSHA256: crypto.createHash('sha256').update(currentEntries).digest('hex'),
    note: 'Git refreshed index stat metadata; all paths, stages, modes and blob IDs remain unchanged.'
  },
  packaging: record(logFile),
  presentationRefresh: noRealmsFollowup ? 'Realms removal only. All native source and non-HTML package files match the previous Quit candidate. Current rendered suites and normal-exit persistence checks rerun; native build/automation evidence is inherited, not newly executed.' : quitFollowup ? 'Full editor/runtime build and BuildCookRun for normal Quit Game. Source and staged HTML match; prior debug-reset candidate files remain unchanged.' : debugFollowup ? 'Temporary debug-only reset layout follow-up. Native/package files match the prior verified candidate except refreshed physical HTML; no new native build claimed.' : 'Final NonUFS HTML includes the 416-logical-pixel scale guard staged after native packaging; both full runtime sweeps used this final HTML.',
  followupBackup: noRealmsFollowup ? 'C:/dev/backups/Unnamed/no-realms-20261007-093729' : quitFollowup ? 'C:/dev/backups/Unnamed/quit-game-20261007-091347' : null,
  previousPackage,
  ui: record(working),
  reference: record(reference),
  editorModule: record(path.join(root, 'Binaries/Win64/UnrealEditor-Unnamed.dll')),
  nativeAutomation: {...record(automationFile), succeeded: automation.succeeded, failed: automation.failed},
  currentUnitTests: noRealmsFollowup ? record(path.join(root,'Tests/Evidence/2026-10-07/NoRealmsUnitTests.tap')) : null,
  graphicsAPIReadback: noRealmsFollowup ? record(path.join(root,'Tests/Evidence/2026-10-07/NoRealmsGraphicsAPI.json')) : null,
  runs: results,
  sourceFiles,
  preserved,
  files: walk(packageRoot).map(file => record(file, packageRoot)),
  remainingGates: ['Physical OS keyboard/focus', 'Driver VSync/tearing', 'Multi-monitor/DPI', 'Unsupported-window failure injection', 'Shipping and other platforms']
};
fs.writeFileSync(path.join(root, noRealmsFollowup ? 'Tests/Evidence/2026-10-07/NoRealmsArtifactManifest.json' : quitFollowup ? 'Tests/Evidence/2026-10-07/QuitArtifactManifest.json' : debugFollowup ? 'Tests/Evidence/2026-10-07/DebugResetArtifactManifest.json' : 'Tests/Evidence/2026-10-07/VideoArtifactManifest.json'), JSON.stringify(report, null, 2) + '\n');
console.log(`Verified ${report.files.length} package files, ${preserved.length} preserved files and ${results.reduce((sum, run) => sum + run.passed, 0)} passing runtime checks. ${noRealmsFollowup?'Native evidence inherited; native source and binaries unchanged.':'Native automation passed.'} Source/package HTML matches.`);
