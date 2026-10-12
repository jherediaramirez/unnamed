import {chromium} from 'playwright';
import {evidencePath} from './evidence.mjs';
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const directory = path.join(root, 'Tests/Runtime/video-persistence-' + Date.now());
fs.mkdirSync(directory, {recursive:true});
const settingsFile = path.join(directory, 'MenuVideoSettings.ini');
const sentinel = path.join(directory, 'protected-world-sentinel.save');
fs.writeFileSync(sentinel, 'This unrelated save must survive Hard Reset.');
const exe = path.join(root,'Packages/VideoSettingsDevelopment/Windows/Unnamed/Binaries/Win64/Unnamed.exe');
const checks = [];
let game, browser, page;
const check = (name, pass, detail) => { checks.push({name,pass:!!pass,detail}); console.log((pass?'PASS ':'FAIL ') + name); };
const live = () => page.evaluate(() => MenuVideo.getState());
async function launch() {
  game = spawn(exe, ['-windowed','-ResX=1280','-ResY=720','-NoSplash','-WinX=0','-WinY=0','-cefdebug=18893',`-MenuVideoSettingsFile=${settingsFile}`], {cwd:root,windowsHide:true,stdio:'ignore'});
  const deadline = Date.now()+45000;
  for (;;) {
    try { browser = await chromium.connectOverCDP('http://127.0.0.1:18893'); break; }
    catch (error) { if (Date.now()>deadline || game.exitCode!==null) throw error; await new Promise(resolve=>setTimeout(resolve,500)); }
  }
  page = browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/Menu/Web/index.html'));
  if (!page) { await new Promise(resolve=>setTimeout(resolve,1000)); page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/Menu/Web/index.html')); }
  await page.waitForFunction(()=>window.MenuVideo?.getState()?.capabilities.fullscreen,null,{timeout:30000});
  await page.evaluate(()=>MenuVideo.settled());
}
async function stop() {
  if (browser) { await browser.close(); browser=null; }
  if (game && game.exitCode===null) {
    const exited = new Promise(resolve=>game.once('exit',resolve));
    game.kill();
    await exited;
  }
  game=null;
  await new Promise(resolve=>setTimeout(resolve,600));
}
const set = (id,value) => page.evaluate(([id,value])=>MenuPrototype.setSetting(id,value),[id,value]);
try {
  await launch();
  check('Persistence enabled on ordinary launch', (await live()).persistent);
  await set('framerateLimit',60); await set('enableVsync',false); await set('guiScale',2); await set('menuBackgroundBlurriness',8); await set('fullscreen',true);
  await set('fov',110); await set('sound_master',0);
  const saved = await live();
  check('Only implemented settings reach disk', fs.existsSync(settingsFile) && !/fov|sound_master|renderDistance/i.test(fs.readFileSync(settingsFile,'utf8')), fs.readFileSync(settingsFile,'utf8'));
  await stop(); // Forced process exit proves saving does not depend on a future Quit implementation.
  await launch();
  const restored = await live();
  check('All five settings survive process exit and relaunch', Object.entries(saved.settings).every(([id,value])=>restored.settings[id]===value), restored);
  check('Restored fullscreen is an actual borderless window', restored.observed.windowMode==='Borderless');
  check('Restored frame limit and VSync affect the engine', restored.observed.maxFPS===60 && restored.observed.vsync===0);
  check('Restored GUI and blur affect presentation', await page.evaluate(()=>MenuPrototype.getState().gui.scale===2));
  await page.evaluate(()=>MenuPrototype.prepare('video'));
  check('Restored blur reaches rendered video background', await page.evaluate(()=>getComputedStyle(document.querySelector('#panorama')).filter==='blur(8px)'));
  check('Unfinished settings remain session-only', await page.evaluate(()=>MenuPrototype.getState().settings.fov===110 && MenuPrototype.getState().settings.sound_master===1));
  await page.getByRole('button',{name:'Hard Reset',exact:true}).click();
  await page.getByRole('button',{name:'Hard Reset',exact:true}).click();
  await page.waitForFunction(()=>window.MenuVideo?.getState()?.settings.guiScale===0 && MenuPrototype.getState().screen==='main',null,{timeout:15000});
  check('Reset exits fullscreen and restores prior window dimensions', (await live()).observed.windowMode==='Windowed' && (await live()).observed.clientWidth===1280 && (await live()).observed.clientHeight===720, await live());
  check('Hard Reset leaves unrelated save data untouched', fs.readFileSync(sentinel,'utf8')==='This unrelated save must survive Hard Reset.');
  await stop(); await launch();
  const reset = await live();
  check('Hard Reset defaults survive another launch', reset.settings.fullscreen===false && reset.settings.framerateLimit===120 && reset.settings.enableVsync===true && reset.settings.guiScale===0 && reset.settings.menuBackgroundBlurriness===5, reset);
  await stop();
} finally {
  await stop();
  fs.mkdirSync(evidencePath(),{recursive:true});fs.writeFileSync(evidencePath('VideoPersistenceResults.json'),JSON.stringify({directory,checks},null,2)+'\n');
}
assert.ok(checks.every(c=>c.pass),'One or more persistence checks failed.');
