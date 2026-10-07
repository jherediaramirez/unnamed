import {chromium} from 'playwright';
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const stamp = new Date().toISOString().replaceAll(/[:.]/g, '-');
const runtime = path.join(root, 'Tests/Runtime/super-flat-' + stamp);
const output = path.join(root, 'Tests/Captures/super-flat-' + stamp);
const packageName = process.argv.find(a => a.startsWith('--package='))?.slice(10) || 'SuperFlatDevelopment';
if (!/^[A-Za-z0-9_-]+$/.test(packageName)) throw Error('Invalid package name');
fs.mkdirSync(runtime, {recursive: true}); fs.mkdirSync(output, {recursive: true});
const exe = path.join(root, 'Packages', packageName, 'Windows/Unnamed/Binaries/Win64/Unnamed.exe');
const log = path.join(runtime, 'game.log');
const checks = [], errors = [], delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const check = (name, pass, detail) => {
  checks.push({name, pass: !!pass, detail}); console.log((pass ? 'PASS ' : 'FAIL ') + name);
  if (!pass) throw Error(name);
};
const game = spawn(exe, ['-windowed', '-ResX=1280', '-ResY=720', '-NoSplash', '-cefdebug=18928', '-SuperFlatSmokeTest',
  '-UserDir=' + runtime + '/', '-MenuPreferencesFile=' + runtime + '/preferences.json',
  '-MenuVideoSettingsFile=' + runtime + '/video.ini', '-abslog=' + log], {cwd: root, windowsHide: true, stdio: 'ignore'});
const exit = new Promise((resolve, reject) => {game.once('error', reject); game.once('exit', (code, signal) => resolve({code, signal}));});
let browser, page;
const waitUntil = async (predicate, timeout = 45000) => {
  const deadline = Date.now() + timeout;
  while (!await predicate()) {if (Date.now() > deadline || game.exitCode !== null) throw Error('Runtime condition timed out'); await delay(150);}
};
const logText = () => fs.existsSync(log) ? fs.readFileSync(log, 'utf8') : '';
const findMenu = async old => {
  let found;
  await waitUntil(() => {found = browser.contexts().flatMap(c => c.pages()).find(p => p !== old && !p.isClosed() && p.url().includes('/Menu/Web/index.html')); return found;});
  await found.waitForFunction(() => window.MenuApplication?.worldEnabled('super-flat') && window.MenuPreferences?.available() && window.MenuInput?.getState()?.ready);
  found.on('pageerror', error => errors.push(String(error)));
  return found;
};
try {
  await waitUntil(async () => {try {browser = await chromium.connectOverCDP('http://127.0.0.1:18928'); return true;} catch {return false;}});
  page = await findMenu();
  for (const id of ['fictional-forest', 'fictional-build', '/Game/Maps/L_SuperFlat', 'super-flat?listen']) {
    const reply = await page.evaluate(async id => JSON.parse(await window.ue.menuapp.requestopenworld(id)), id);
    check('Native rejects noncatalog travel: ' + id, !reply.ok && !reply.pendingWorld && !reply.travelIssued, reply);
  }
  await page.getByRole('button', {name: 'Singleplayer', exact: true}).click();
  check('Singleplayer keeps exactly Super Flat and two design references', await page.locator('[data-world]').count() === 3);
  check('Super Flat selected with Play available', await page.locator('[data-world="super-flat"]').getAttribute('aria-selected') === 'true' && !await page.getByRole('button', {name: 'Play Selected World', exact: true}).isDisabled());
  for (const label of ['Edit', 'Delete', 'Re-Create']) check('Built-in map protected from mock ' + label, await page.getByRole('button', {name: label, exact: true}).isDisabled());
  for (const id of ['fictional-forest', 'fictional-build']) {
    const row = page.locator('[data-world="' + id + '"]'); await row.click();
    check(id + ' labeled as reference with Play disabled', (await row.textContent()).includes('Design reference') && await page.getByRole('button', {name: 'Play Selected World', exact: true}).isDisabled());
    await row.dblclick();
    check(id + ' double click remains a reference explanation', await page.evaluate(() => MenuPrototype.getState().screen === 'message') && !logText().includes('Super Flat ready:'));
    await page.keyboard.press('Escape');
  }
  await page.locator('[data-world="super-flat"]').click();
  await page.screenshot({path: path.join(output, 'singleplayer-1280x720.png')});
  const inputBefore = await page.evaluate(() => MenuInput.getState().nativeKeys);
  for (const [index, action] of ['Play button', 'double click', 'Enter key'].entries()) {
    if (index > 0) await page.getByRole('button', {name: 'Singleplayer', exact: true}).click();
    await page.locator('[data-world="super-flat"]').click();
    await page.evaluate(index => {void MenuPreferences.set('fov', 85 + index);}, index);
    if (action === 'Play button') await page.getByRole('button', {name: 'Play Selected World', exact: true}).click();
    else if (action === 'double click') await page.locator('[data-world="super-flat"]').dblclick();
    else {await page.locator('[data-world="super-flat"]').focus(); await page.keyboard.press('Enter');}
    await waitUntil(() => (logText().match(/SUPER_FLAT_TEST result=PASS/g) || []).length === index + 1);
    check(action + ' loads the native level with ground collision and clear spawn', !logText().includes('SUPER_FLAT_TEST result=FAIL'));
    await waitUntil(() => (logText().match(/LogUnnamedMenu: Offline menu:/g) || []).length === index + 2);
    // CEF replaces its browser when the menu controller is recreated. Reattach
    // after travel rather than retaining a stale Playwright target connection.
    await browser.close();
    browser = await chromium.connectOverCDP('http://127.0.0.1:18928');
    page = await findMenu();
    check(action + ' returns to a usable main menu', await page.evaluate(() => MenuPrototype.getState().screen === 'main' && MenuApplication.enabled()));
    check(action + ' drains and retains pending preference changes', await page.evaluate(() => MenuPreferences.getState().settings.fov) === 85 + index);
    check(action + ' retains native bindings', JSON.stringify(await page.evaluate(() => MenuInput.getState().nativeKeys)) === JSON.stringify(inputBefore));
    check(action + ' restored only one browser document', browser.contexts().flatMap(c => c.pages()).filter(p => !p.isClosed() && p.url().includes('/Menu/Web/index.html')).length === 1);
  }
  check('No JavaScript errors during map transitions', errors.length === 0, errors);
  check('No native map load, material compilation, or runtime errors', !/TravelFailure|Failed to load.*L_SuperFlat|Failed to compile Material|Fatal error|Assertion failed/i.test(logText()));
  await page.getByRole('button', {name: 'Quit Game', exact: true}).click();
  let timer;
  const status = await Promise.race([exit, new Promise(resolve => timer = setTimeout(() => resolve(null), 20000))]); clearTimeout(timer);
  check('Normal exit after repeated world transitions', status?.code === 0 && status?.signal === null, status);
  const shots = fs.readdirSync(runtime, {recursive: true}).filter(p => p.endsWith('SuperFlat.png'));
  check('Native rendered world screenshot saved', shots.length === 1, shots);
  fs.copyFileSync(path.join(runtime, shots[0]), path.join(output, 'super-flat-world.png'));
} catch (error) {checks.push({name: 'Harness completed', pass: false, detail: String(error)}); console.error(error);}
finally {
  if (browser) await browser.close();
  if (game.exitCode === null) {game.kill(); await exit;}
  if (fs.existsSync(log)) fs.copyFileSync(log, path.join(output, 'game.log'));
  fs.writeFileSync(path.join(output, 'results.json'), JSON.stringify({finished: new Date().toISOString(), packageName,
    exeSHA256: crypto.createHash('sha256').update(fs.readFileSync(exe)).digest('hex'), checks, runtime}, null, 2) + '\n');
}
console.log(`${checks.filter(c => c.pass).length}/${checks.length} passed; ${output}`);
if (checks.some(c => !c.pass)) process.exitCode = 1;
