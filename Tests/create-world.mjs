import {chromium} from 'playwright';
import {evidencePath, testsRoot} from './evidence.mjs';
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

// Create New World end to end, through CEF and the native bridge, in four launches of the game:
// 1. create "Hello World" (Default terrain, seed "hello") and play it;
// 2. check the list (natives only, actions on saves only, Re-Create, Edit renaming natively), then create "Flat Test"
//    (Superflat, Creative, Peaceful) and play it;
// 3. play the renamed world again from the list: the same seed;
// 4. delete both worlds.
// World runs pass -VoxelShot, so the generated map saves its screenshots and build report and quits. Saves go to a
// throwaway -WorldsDir inside the evidence folder.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const argument = (name, fallback) => process.argv.find(a => a.startsWith(name + '='))?.slice(name.length + 1) || fallback;
const packageName = argument('--package', '');
const output = path.resolve(argument('--output', evidencePath('Worlds', 'Create-' + Date.now())));
const editor = process.argv.includes('--editor') || !packageName;
const exe = editor ? 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'
  : path.join(root, `Packages/${packageName}/Windows/Unnamed/Binaries/Win64/Unnamed.exe`);
const worlds = path.join(output, 'worlds');
// The editor's per-run user folder is a cache, not evidence: it goes to the ignored Tests/Runtime/.
const profile = path.join(testsRoot, 'Runtime', 'create-world-' + Date.now(), 'profile');
fs.mkdirSync(output, {recursive: true});
const checks = [], errors = [], delay = ms => new Promise(r => setTimeout(r, ms));
const check = (name, pass, detail = '') => { checks.push({name, pass, detail}); console.log(`${pass ? 'PASS' : 'FAIL'} ${name}`); if (!pass) throw Error(name); };
let browser, page, game, exit;

async function waitFor(fn, limit = 90000) {
  const start = Date.now();
  while (!await fn()) { if (Date.now() - start > limit || game?.exitCode !== null) throw Error('Runtime timeout'); await delay(150); }
}
async function attach() {
  await waitFor(async () => { try { browser = await chromium.connectOverCDP('http://127.0.0.1:18946'); return true; } catch { return false; } });
  await waitFor(() => { page = browser.contexts().flatMap(c => c.pages()).find(p => !p.isClosed() && p.url().includes('/Menu/Web/index.html')); return !!page; });
  await page.waitForFunction(() => typeof MenuApplication !== 'undefined' && MenuApplication.getState()?.canOpenWorld);
  page.on('pageerror', e => errors.push(String(e)));
}
async function start(suffix, shots) {
  const args = editor ? [path.join(root, 'Unnamed.uproject'), '-game'] : [];
  args.push('-windowed', '-ResX=1280', '-ResY=720', '-RenderOffscreen', '-unattended', '-nosplash', '-nosound', '-cefdebug=18946',
    `-UserDir=${profile}/`, `-MenuVideoSettingsFile=${output}/video.ini`, `-MenuPreferencesFile=${output}/preferences.json`,
    `-WorldsDir=${worlds}`, `-abslog=${output}/${suffix}.log`);
  if (shots) args.push(`-VoxelShot=${shots}`);
  game = spawn(exe, args, {cwd: root, windowsHide: true, stdio: 'ignore'});
  exit = new Promise((resolve, reject) => { game.once('error', reject); game.once('exit', (code, signal) => resolve({code, signal})); });
  await attach();
}
// The world run ends by itself once its screenshots are saved.
async function finishWorld(name) {
  const status = await Promise.race([exit, delay(300000).then(() => null)]);
  check(name + ': the world ran and closed normally', status?.code === 0, JSON.stringify(status));
  await browser.close().catch(() => {}); browser = null;
}
async function quit() {
  try { await page.evaluate(() => MenuApplication.quit()); } catch (e) { if (!/closed/i.test(String(e))) throw e; }
  const status = await Promise.race([exit, delay(20000).then(() => null)]);
  check('normal exit', status?.code === 0, JSON.stringify(status));
  await browser.close(); browser = null;
}

const label = key => page.evaluate(key => tr(key), key);
const draft = () => page.evaluate(() => MenuPrototype.getState().draft);
const rowIds = () => page.locator('[data-world]').evaluateAll(rows => rows.map(r => r.dataset.world));
const report = shots => fs.readFileSync(path.join(shots, 'build.txt'), 'utf8');
const world = id => JSON.parse(fs.readFileSync(path.join(worlds, id, 'world.json'), 'utf8'));
async function singleplayer() {
  await page.getByRole('button', {name: 'Singleplayer', exact: true}).click();
  await page.waitForFunction(() => MenuPrototype.getState().screen === 'worlds');
}
async function action(key) { return page.getByRole('button', {name: await label(key), exact: true}); }
async function select(id) { await page.locator(`[data-world="${id}"]`).click(); }
async function createWorld({name, seed, type, mode, difficulty}) {
  await (await action('selectWorld.create')).click();
  await page.waitForFunction(() => MenuPrototype.getState().screen === 'create');
  await page.locator(`input[aria-label="${await label('selectWorld.enterName')}"]`).fill(name);
  for (let turns = 0; mode && (await draft()).gameMode !== mode && turns < 4; turns++) await page.locator('[data-local="draft.gameMode"]').click();
  for (let turns = 0; difficulty && (await draft()).difficulty !== difficulty && turns < 5; turns++) await page.locator('[data-local="draft.difficulty"]').click();
  await page.getByRole('tab', {name: await label('createWorld.tab.world.title')}).click();
  for (let turns = 0; type && (await draft()).type !== type && turns < 6; turns++) await page.locator('[data-local="draft.type"]').click();
  if (seed !== undefined) await page.locator(`input[aria-label="${await label('selectWorld.enterSeed')}"]`).fill(seed);
  const settings = await draft();
  check(`${name}: the draft holds the choices`, settings.name === name && (!mode || settings.gameMode === mode) &&
    (!difficulty || settings.difficulty === difficulty) && (!type || settings.type === type) && (seed === undefined || settings.seed === seed), settings);
  await page.screenshot({path: path.join(output, `create-${name.toLowerCase().replace(/\W+/g, '-')}.png`)});
  await page.locator('[data-create-submit]:visible').click();
}

try {
  // 1. Create "Hello World" and play it.
  const helloShots = path.join(output, 'hello-world-created');
  await start('create-hello', helloShots);
  await singleplayer();
  check('A fresh profile lists only the built-in Playground', JSON.stringify(await rowIds()) === '["playground"]', await rowIds());
  await createWorld({name: 'Hello World', seed: 'hello'});
  await finishWorld('Hello World');
  const hello = world('hello-world');
  check('world.json: the draft as created', hello.version === 1 && hello.generator === 2 && hello.name === 'Hello World' &&
    hello.seed === 'hello' && hello.gameMode === 'Survival' && hello.difficulty === 'Normal' && hello.type === 'Default' &&
    Object.keys(hello.rules).length > 40, hello);
  const helloReport = report(helloShots);
  const seedLine = helloReport.split('\n').find(line => line.startsWith('Seed '));
  check('The world was built from its saved seed', seedLine === `Seed ${hello.seedValue} (text)`, seedLine);
  check('The world report names the saved world', helloReport.includes('World: hello-world "Hello World", Default, Survival, Normal'), helloReport);
  check('The player plays it in Survival on Normal, on the ground', /Player: .*grounded; Survival Normal;/.test(helloReport), helloReport);

  // 2. The list, the actions on saves, then "Flat Test".
  const flatShots = path.join(output, 'flat-test-created');
  await start('list', flatShots);
  await singleplayer();
  check('The list is the Playground, then the save', JSON.stringify(await rowIds()) === '["playground","hello-world"]', await rowIds());
  const row = await page.locator('[data-world="hello-world"]').innerText();
  check('The save shows its folder, date and mode', row.includes('Hello World') && row.includes('hello-world (') && row.includes('Survival Mode'), row);
  check('The last played save is selected', await page.evaluate(() => chosenWorld) === 'hello-world');
  await page.screenshot({path: path.join(output, 'singleplayer-with-save.png')});
  await select('playground');
  for (const key of ['selectWorld.edit', 'selectWorld.delete', 'selectWorld.recreate'])
    check('Built-in world: no ' + key, await (await action(key)).isDisabled());
  await select('hello-world');
  for (const key of ['selectWorld.edit', 'selectWorld.delete', 'selectWorld.recreate'])
    check('Saved world: ' + key, !await (await action(key)).isDisabled());
  await (await action('selectWorld.recreate')).click();
  await page.waitForFunction(() => MenuPrototype.getState().screen === 'create');
  const again = await draft();
  check('Re-Create starts from the saved settings and seed', again.name === 'Hello World' && again.seed === 'hello' &&
    again.type === 'Default' && again.gameMode === 'Survival', again);
  await page.keyboard.press('Escape');
  await page.waitForFunction(() => MenuPrototype.getState().screen === 'worlds');
  await select('hello-world');
  await (await action('selectWorld.edit')).click();
  await page.waitForFunction(() => MenuPrototype.getState().screen === 'editworld');
  await page.locator(`input[aria-label="${await label('selectWorld.enterName')}"]`).fill('Hello Again');
  await page.locator('#world-save').click();
  await page.waitForFunction(() => MenuPrototype.getState().screen === 'worlds');
  await waitFor(() => world('hello-world').name === 'Hello Again', 10000);
  check('Edit renames the save natively; its id stays', world('hello-world').name === 'Hello Again' &&
    JSON.stringify(await rowIds()) === '["playground","hello-world"]');
  await createWorld({name: 'Flat Test', type: 'Superflat', mode: 'Creative', difficulty: 'Peaceful'});
  await finishWorld('Flat Test');
  const flat = world('flat-test');
  check('world.json: Superflat with its layers', flat.type === 'Superflat' && flat.gameMode === 'Creative' &&
    flat.difficulty === 'Peaceful' && flat.commands === true && flat.flat.length === 3 && flat.flat[0].block === 'minecraft:grass_block', flat);
  const flatReport = report(flatShots);
  check('The flat world opened as saved', flatReport.includes('World: flat-test "Flat Test", Superflat, Creative, Peaceful'), flatReport);
  check('Its player stands on the grass four blocks above the floor, in Creative on Peaceful',
    /Player: feet at V\(X=50\.00, Y=50\.00, Z=-6000\.00\), grounded; Creative Peaceful;/.test(flatReport), flatReport);

  // 3. Play the renamed world from the list: same seed.
  const playShots = path.join(output, 'hello-world-played');
  await start('play', playShots);
  await singleplayer();
  check('Saves follow the catalog, most recently played first', JSON.stringify(await rowIds()) === '["playground","flat-test","hello-world"]', await rowIds());
  await select('hello-world');
  await (await action('selectWorld.select')).click();
  await finishWorld('Hello Again');
  const playReport = report(playShots);
  check('Playing it again builds the same seed', playReport.split('\n').find(line => line.startsWith('Seed ')) === seedLine, playReport);
  check('...under its new name', playReport.includes('World: hello-world "Hello Again", Default, Survival, Normal'), playReport);
  const played = world('hello-world');
  check('Playing updates last played', Date.parse(played.lastPlayed) > Date.parse(played.created), played);

  // 4. Delete both.
  await start('delete');
  await singleplayer();
  for (const [id, left] of [['flat-test', '["playground","hello-world"]'], ['hello-world', '["playground"]']]) {
    await select(id);
    await (await action('selectWorld.delete')).click();
    await page.waitForFunction(() => MenuPrototype.getState().screen === 'confirm');
    await page.getByRole('button', {name: await label('selectWorld.deleteButton'), exact: true}).click();
    await waitFor(async () => JSON.stringify(await rowIds()) === left, 10000);
    check(`Delete removes ${id} from the list and the disk`, !fs.existsSync(path.join(worlds, id)));
  }
  check('no uncaught JavaScript errors', errors.length === 0, errors);
  await quit();
} catch (e) {
  checks.push({name: 'harness completed', pass: false, detail: String(e)}); console.error(e);
  if (page && !page.isClosed()) {
    await page.screenshot({path: path.join(output, 'failure.png')}).catch(() => {});
    fs.writeFileSync(path.join(output, 'failure-state.json'), JSON.stringify(await page.evaluate(() => MenuPrototype.getState()).catch(() => ({})), null, 2));
  }
} finally {
  if (browser) await browser.close().catch(() => {});
  if (game && game.exitCode === null) { game.kill(); await exit; }
  fs.writeFileSync(path.join(output, 'results.json'), JSON.stringify({evidence: 'Create New World through CEF and the native bridge', checks}, null, 2));
}
console.log(output);
if (checks.some(c => !c.pass)) process.exitCode = 1;
