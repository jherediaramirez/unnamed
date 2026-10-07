import {chromium} from 'playwright';
import {evidencePath} from './evidence.mjs';
import {PNG} from 'pngjs';
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';

const port = Number(process.argv.find(a => a.startsWith('--port='))?.split('=')[1] || 18891);
const profile = process.argv.find(a => a.startsWith('--profile='))?.split('=')[1] || '1280x720';
const capture = process.argv.find(a => a.startsWith('--capture='))?.split('=')[1] || 'video-settings-' + profile;
if (!/^[a-zA-Z0-9_-]+$/.test(capture)) throw Error('Invalid capture name');
const out = evidencePath('Captures', capture);
if (fs.existsSync(out)) throw Error('Capture exists; choose a fresh name to preserve evidence.');
fs.mkdirSync(out, {recursive:true});
const checks = [];
let browser;
const deadline = Date.now()+45000;
for (;;) {
  try { browser=await chromium.connectOverCDP(`http://127.0.0.1:${port}`); break; }
  catch (error) { if(Date.now()>deadline) throw error; await new Promise(resolve=>setTimeout(resolve,500)); }
}
const page = browser.contexts().flatMap(c => c.pages()).find(p => p.url().includes('/Menu/Web/index.html'));
const htmlSHA256 = crypto.createHash('sha256').update(fs.readFileSync(fileURLToPath(page.url()))).digest('hex');
const check = (name, pass, detail) => { checks.push({name,pass:!!pass,detail}); console.log((pass?'PASS ':'FAIL ') + name); };
const live = () => page.evaluate(() => MenuVideo.getState());
const settled = () => page.evaluate(() => MenuVideo.settled());
const set = (id, value) => page.evaluate(([id,value]) => MenuPrototype.setSetting(id,value), [id,value]);
const prepare = name => page.evaluate(name => MenuPrototype.prepare(name), name);
try {
  await page.waitForFunction(() => window.MenuVideo?.getState()?.capabilities.fullscreen && MenuPrototype.getState().assets.panoramaLoaded, null, {timeout:30000});
  if ((await live()).persistent) throw Error('Use a -SessionOnly diagnostic launch for this destructive preference-reset test.');
  await page.evaluate(() => MenuVideo.hardReset()).catch(error => { if(!/context.*destroyed/i.test(String(error))) throw error; });
  await page.waitForFunction(() => window.MenuVideo?.getState()?.settings.guiScale===0 && MenuPrototype.getState().screen==='main');
  await settled();
  const initial = await live();
  const dimensions = await page.evaluate(() => [innerWidth,innerHeight]);
  check('Native bridge exposes exactly five settings', Object.keys(initial.settings).length===5 && Object.values(initial.capabilities).every(Boolean), initial);
  await prepare('main');
  const mainStyle = await page.getByRole('button',{name:'Singleplayer',exact:true}).evaluate(node => {
    const css=getComputedStyle(node);
    return {width:css.width,height:css.height,font:css.fontFamily,border:css.borderImageSource};
  });
  check('Debug reset is not added to the main menu', await page.locator('#hard-reset').count()===0);
  await prepare('video');
  const resetStyle = await page.locator('#hard-reset').evaluate(node => {
    const css=getComputedStyle(node), parent=node.parentElement;
    return {width:css.width,height:css.height,font:css.fontFamily,border:css.borderImageSource,
      centered:node.offsetLeft*2+node.offsetWidth===parent.clientWidth,
      temporary:node.dataset.debugControl==='hard-reset',last:parent.lastElementChild===node};
  });
  check('Debug reset matches the main-menu button style and is centered last',
    resetStyle.width===mainStyle.width && resetStyle.height===mainStyle.height &&
    resetStyle.font===mainStyle.font && resetStyle.border===mainStyle.border &&
    resetStyle.centered && resetStyle.temporary && resetStyle.last,resetStyle);
  await page.locator('#hard-reset').scrollIntoViewIfNeeded();
  const resetBox = await page.locator('#hard-reset').boundingBox();
  check('Debug reset fits within the video settings viewport', resetBox.x>=0 &&
    resetBox.x+resetBox.width<=dimensions[0] && resetBox.y>=0 && resetBox.y+resetBox.height<=dimensions[1],resetBox);
  await page.screenshot({path:path.join(out,'debug-reset-button.png')});
  await page.evaluate(() => MenuPrototype.freezeFrame());
  const full = page.locator('[data-setting=fullscreen]');
  await full.scrollIntoViewIfNeeded(); await full.click(); await settled();
  const borderless = await live();
  const fullDimensions = await page.evaluate(() => [innerWidth,innerHeight]);
  check('Fullscreen button changes the real native window', borderless.settings.fullscreen && borderless.observed.windowMode==='Borderless' && borderless.observed.clientWidth===fullDimensions[0] && borderless.observed.clientHeight===fullDimensions[1], {borderless,fullDimensions});
  check('No DOM fullscreen workaround', await page.evaluate(() => !document.fullscreenElement));
  await page.screenshot({path:path.join(out,'fullscreen.png')});
  await page.keyboard.press('F11'); await settled();
  check('F11 restores the exact windowed viewport', (await live()).observed.windowMode==='Windowed' && JSON.stringify(dimensions)===JSON.stringify(await page.evaluate(() => [innerWidth,innerHeight])), await live());
  await page.keyboard.press('Alt+Enter'); await settled();
  check('Alt+Enter enters borderless using the same adapter', (await live()).observed.windowMode==='Borderless');
  await page.keyboard.press('Alt+Enter'); await settled();
  check('Alt+Enter returns to windowed', (await live()).observed.windowMode==='Windowed');
  await set('fullscreen',true); await set('fullscreen',false); await settled();
  check('Repeated fullscreen transitions settle correctly', !(await live()).pendingFullscreen && !(await live()).settings.fullscreen);
  await set('enableVsync',false);
  check('VSync OFF changes the real engine variable', (await live()).observed.vsync===0);
  for (const cap of [30,60]) {
    await set('framerateLimit',cap); await page.waitForTimeout(2600);
    const observed = (await live()).observed;
    check('Actual engine cadence at ' + cap + ' FPS', observed.maxFPS===cap && observed.measuredFPS>cap*0.70 && observed.measuredFPS<cap*1.15, observed);
  }
  await set('framerateLimit',260);
  check('Unlimited disables the actual engine cap', (await live()).observed.maxFPS===0);
  await set('framerateLimit',120);
  await set('enableVsync',true);
  check('VSync ON changes the real engine variable', (await live()).observed.vsync===1);
  await page.evaluate(() => { MenuPrototype.setSetting('framerateLimit',30); MenuPrototype.setSetting('framerateLimit',90); MenuPrototype.setSetting('framerateLimit',60); });
  await settled();
  check('Rapid frame-limit edits finish at the latest choice', (await live()).observed.maxFPS===60 && (await live()).settings.framerateLimit===60);
  await set('framerateLimit',120);
  await prepare('video');
  await page.locator('[data-setting=guiScale]').focus();
  const focusID = await page.locator('[data-setting=guiScale]').getAttribute('data-control-id');
  await set('guiScale',1);
  check('GUI scale changes actual layout', await page.evaluate(() => MenuPrototype.getState().gui.scale===1 && document.querySelector('#gui').style.transform==='scale(1)'));
  await set('guiScale',2);
  check('GUI scale preserves its keyboard focus', await page.evaluate(id => MenuPrototype.getState().gui.scale===2 && document.activeElement?.dataset.controlId===id,focusID));
  await set('guiScale',0);
  check('Auto restores integer scaling', await page.evaluate(() => MenuPrototype.getState().gui.scale===maximumScale()));
  const max = await page.evaluate(() => maximumScale());
  await set('guiScale',max);
  const footer = await page.getByRole('button',{name:'Done',exact:true}).boundingBox();
  check('Largest supported scale fits the video toolbar', footer.x>=0 && footer.x+footer.width<=dimensions[0] && footer.y>=0 && footer.y+footer.height<=dimensions[1], footer);
  await set('guiScale',0);
  await set('menuBackgroundBlurriness',0);
  await page.mouse.move(0,0); await page.waitForTimeout(100);
  const clear = PNG.sync.read(await page.screenshot({path:path.join(out,'blur-0.png')}));
  check('Zero blur changes the rendered filter', await page.evaluate(() => getComputedStyle(document.querySelector('#panorama')).filter==='blur(0px)'));
  await set('menuBackgroundBlurriness',10); await page.waitForTimeout(100);
  const blurred = PNG.sync.read(await page.screenshot({path:path.join(out,'blur-10.png')}));
  let changed = 0;
  for (let i=0;i<clear.data.length;i+=4) if (Math.abs(clear.data[i]-blurred.data[i])+Math.abs(clear.data[i+1]-blurred.data[i+1])+Math.abs(clear.data[i+2]-blurred.data[i+2])>15) changed++;
  check('Blur slider changes real screenshot pixels', changed>clear.width*clear.height*0.05 && await page.evaluate(() => getComputedStyle(document.querySelector('#panorama')).filter==='blur(10px)'), {changed,total:clear.width*clear.height});
  await set('graphicsPreset','Fast');
  check('Mock graphics preset cannot overwrite functional blur', (await live()).settings.menuBackgroundBlurriness===10 && await page.evaluate(() => MenuPrototype.getState().settings.menuBackgroundBlurriness===10));
  await prepare('accessibility');
  check('Accessibility shares the same functional blur', await page.locator('[data-setting=menuBackgroundBlurriness] input').inputValue()==='10');
  await set('menuBackgroundBlurriness',2); await prepare('video');
  check('Video sees changes from shared controls', await page.locator('[data-setting=menuBackgroundBlurriness] input').inputValue()==='2');
  for (const [id,json] of [['renderDistance','{"value":24}'],['fullscreen','{"value":1}'],['framerateLimit','{"value":25}'],['guiScale','{"value":-1}'],['menuBackgroundBlurriness','{"value":11}'],['enableVsync','{"value":false,"extra":true}']]) {
    const reply = await page.evaluate(async ([id,json]) => JSON.parse(await window.ue.menuvideo.applysetting(id,json)), [id,json]);
    check('Native validation rejects ' + id + ' ' + json, !reply.ok);
  }
  await set('guiScale',0); await set('menuBackgroundBlurriness',5); await set('enableVsync',true); await set('framerateLimit',120);
  await prepare('main');
  check('Main menu keeps its unblurred background', await page.evaluate(() => getComputedStyle(document.querySelector('#panorama')).filter==='blur(0px)'));
  await page.reload();
  await page.waitForFunction(() => window.MenuVideo?.getState()?.capabilities.fullscreen);
  check('Reload rehydrates real native state', (await live()).settings.framerateLimit===120 && (await live()).settings.menuBackgroundBlurriness===5 && (await live()).settings.guiScale===0);
  check('No browser runtime errors', await page.evaluate(() => MenuPrototype.getState().errors.length===0));
  await set('guiScale',1); await set('menuBackgroundBlurriness',9); await set('framerateLimit',60);
  await set('fov',110); await set('sound_master',0); await set('highContrast',true);
  await prepare('video');
  await page.getByRole('button',{name:'Hard Reset',exact:true}).click();
  fs.writeFileSync(path.join(out,'VideoResetControls.json'),JSON.stringify(await page.evaluate(()=>MenuPrototype.controls()),null,2)+'\n');
  await page.getByRole('button',{name:'Cancel',exact:true}).click();
  check('Hard Reset cancellation preserves settings', (await live()).settings.guiScale===1 && (await live()).settings.framerateLimit===60 && await page.evaluate(() => MenuPrototype.getState().settings.fov===110));
  await page.getByRole('button',{name:'Hard Reset',exact:true}).click();
  await page.screenshot({path:path.join(out,'hard-reset-confirmation.png')});
  await page.getByRole('button',{name:'Hard Reset',exact:true}).click();
  await page.waitForFunction(() => window.MenuVideo?.getState()?.settings.guiScale===0 && MenuPrototype.getState().screen==='main');
  check('Hard Reset restores all five functional settings', (await live()).settings.fullscreen===false && (await live()).settings.framerateLimit===120 && (await live()).settings.enableVsync===true && (await live()).settings.guiScale===0 && (await live()).settings.menuBackgroundBlurriness===5);
  check('Hard Reset recreates first-run mock preferences', await page.evaluate(() => {const s=MenuPrototype.getState(),d=MenuPrototype.definitions();return s.settings.fov===d.settings.fov.default && s.settings.sound_master===d.settings.sound_master.default && !s.settings.highContrast && d.bindings.every(b=>s.bindings[b.id]===b.default)}));
  await page.evaluate(() => MenuPrototype.unfreeze());
} finally {
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({profile,htmlSHA256,checks},null,2)+'\n');
  await browser.close();
}
assert.ok(checks.every(c=>c.pass), 'One or more video settings checks failed.');
