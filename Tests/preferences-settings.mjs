import {chromium} from 'playwright';
import {evidencePath} from './evidence.mjs';
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const stamp=new Date().toISOString().replaceAll(/[:.]/g,'-');
const runtime=path.join(root,'Tests/Runtime/preferences-'+stamp);
const output=evidencePath('Captures','preferences-'+stamp);
fs.mkdirSync(runtime,{recursive:true});fs.mkdirSync(output,{recursive:true});
const packageName=process.argv.find(a=>a.startsWith('--package='))?.slice(10)||'CodeReviewDevelopment20261007';
if(!/^[A-Za-z0-9_-]+$/.test(packageName))throw Error('Invalid package name');
const packageRoot=path.join(root,'Packages',packageName,'Windows/Unnamed');
const exe=path.join(packageRoot,'Binaries/Win64/Unnamed.exe');
const schema=JSON.parse(fs.readFileSync(path.join(packageRoot,'Content/Menu/Web/preferences-schema.menuprefs')));
const preferencesFile=path.join(runtime,'preferences.json');
const checks=[],errors=[],delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const check=(name,pass,detail)=>{checks.push({name,pass:!!pass,detail});console.log((pass?'PASS ':'FAIL ')+name);if(!pass)throw Error(name);};
let expected;
for(const [index,profile] of ['1280x720','1920x1080'].entries()){
  if(index===1&&!expected)break;
  const [width,height]=profile.split('x');
  const game=spawn(exe,['-windowed','-ResX='+width,'-ResY='+height,'-NoSplash','-cefdebug=18924',
    '-UserDir='+runtime+'/','-MenuPreferencesFile='+preferencesFile,'-MenuVideoSettingsFile='+runtime+'/video-'+profile+'.ini','-abslog='+runtime+'/'+profile+'.log'],
    {cwd:root,windowsHide:true,stdio:'ignore'});
  const exit=new Promise((resolve,reject)=>{game.once('error',reject);game.once('exit',(code,signal)=>resolve({code,signal}));});
  let browser,page;
  try{
    const deadline=Date.now()+45000;
    while(!browser){try{browser=await chromium.connectOverCDP('http://127.0.0.1:18924');}catch(error){if(Date.now()>deadline||game.exitCode!==null)throw error;await delay(300);}}
    while(!page){page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/Menu/Web/index.html'));if(!page){if(Date.now()>deadline)throw Error('No menu target');await delay(100);}}
    await page.waitForFunction(()=>window.MenuPreferences?.available()&&window.MenuApplication?.enabled()&&window.MenuInput?.getState()?.ready&&window.MenuVideo?.getState());
    page.on('pageerror',error=>errors.push(String(error)));
    const state=()=>page.evaluate(()=>MenuPreferences.getState());
    const prepare=route=>page.evaluate(route=>MenuPrototype.prepare(route),route);
    const settle=async()=>{await page.evaluate(()=>MenuPreferences.settled());await page.waitForFunction(()=>!MenuPreferences.isCommitting());};
    const click=async label=>{await page.getByRole('button',{name:label,exact:true}).click();await settle();};
    const packAction=async label=>{
      const control=page.locator('button[aria-label="'+label+'"]');
      // Pack arrows appear only while the parent row has hover or keyboard focus.
      await control.locator('..').focus();await control.click();
    };
    check(profile+' persistent native service ready',(await state()).persistent&&Object.keys((await state()).settings).length===101);
    if(index===0){
      const videoBefore=await page.evaluate(()=>MenuVideo.getState().settings);
      const inputBefore=await page.evaluate(()=>MenuInput.getState().nativeKeys);
      const outcomes=await page.evaluate(async schema=>{
        const outcomes=[];
        for(const [id,d] of Object.entries(schema.settings)){
          const value=d.values?d.values.find(v=>v!==d.default):d.min===d.default?d.max:d.min;
          await MenuPreferences.set(id,value);
          outcomes.push({id,value,native:MenuPreferences.getState().settings[id],display:MenuPrototype.getState().settings[id]});
        }
        return outcomes;
      },schema);
      for(const o of outcomes)check('Native/browser round trip '+o.id,o.value===o.native&&o.value===o.display,o);
      check('101 preferences leave live video unchanged',JSON.stringify(await page.evaluate(()=>MenuVideo.getState().settings))===JSON.stringify(videoBefore));
      check('101 preferences leave movement keys unchanged',JSON.stringify(await page.evaluate(()=>MenuInput.getState().nativeKeys))===JSON.stringify(inputBefore));
      await prepare('sounds');
      check('Saved-only tooltip is visible in control metadata',(await page.locator('[data-setting="sound_master"]').getAttribute('data-tooltip')).includes('Saved for later'));
      await page.evaluate(()=>MenuPreferences.set('showSubtitles',false));
      await page.locator('[data-setting="showSubtitles"]').click();await settle();
      await prepare('accessibility');
      check('Repeated Closed Captions control uses shared value',(await state()).settings.showSubtitles&&(await page.locator('[data-setting="showSubtitles"]').textContent()).includes('ON'));
      await prepare('language');await page.locator('[data-language="fr_fr"]').click();await page.keyboard.press('Escape');
      check('Language Escape discards the draft',(await state()).language==='en_us');
      await prepare('language');await page.locator('[data-language="fr_fr"]').click();await click('Done');
      check('Language Done saves only the chosen locale',(await state()).language==='fr_fr'&&await page.evaluate(()=>document.documentElement.lang==='en-US'));
      await page.evaluate(()=>MenuPreferences.set('highContrast',false));
      const packsBefore=(await state()).packs;
      await prepare('packs');await packAction('Select Programmer Art');await page.keyboard.press('Escape');
      check('Resource Pack Escape discards draft',JSON.stringify((await state()).packs)===JSON.stringify(packsBefore));
      await prepare('packs');await packAction('Select Programmer Art');
      await packAction('Select High Contrast');
      await packAction('Move High Contrast up');await click('Done');
      check('Pack order and shared High Contrast save together',JSON.stringify((await state()).packs)==='["high_contrast","programmer_art"]'&&(await state()).settings.highContrast);
      await prepare('safety');await page.locator('[data-preference-flag="skipWarning"]').click();await settle();
      await prepare('telemetry');await page.locator('[data-preference-flag="telemetryOptInExtra"]').click();await settle();
      check('Warning and telemetry choices persist',(await state()).skipWarning&&(await state()).telemetryOptInExtra);
      check('Saving appearance choices leaves current presentation stable',await page.evaluate(()=>!document.querySelector('#gui').classList.contains('high-contrast')&&!document.querySelector('#gui').classList.contains('uniform-font')));
      expected=await state();
      await page.reload();await page.waitForFunction(()=>window.MenuPreferences?.available()&&window.MenuApplication?.enabled());
      const reloaded=await state();
      check('Document reload restores all preferences',JSON.stringify(reloaded.settings)===JSON.stringify(expected.settings)&&reloaded.language===expected.language&&JSON.stringify(reloaded.packs)===JSON.stringify(expected.packs)&&reloaded.skipWarning&&reloaded.telemetryOptInExtra);
      await prepare('video');await page.evaluate(()=>MenuPrototype.freezeFrame());
      await page.screenshot({path:path.join(output,profile+'-preferences.png')});
      await page.evaluate(()=>{void MenuPreferences.set('fov',91);MenuPrototype.prepare('main');});
      await page.getByRole('button',{name:'Quit Game',exact:true}).click();
      expected.settings.fov=91;
    }else{
      const relaunched=await state();
      check('New process restores all 101 scalar choices',JSON.stringify(relaunched.settings)===JSON.stringify(expected.settings));
      check('New process restores locale, ordered packs and flags',relaunched.language===expected.language&&JSON.stringify(relaunched.packs)===JSON.stringify(expected.packs)&&relaunched.skipWarning&&relaunched.telemetryOptInExtra);
      const document=JSON.parse(fs.readFileSync(preferencesFile));
      check('Versioned native document matches accepted choices',document.version===1&&JSON.stringify(document.settings)===JSON.stringify(relaunched.settings));
      check('Native document contains no world, keybinding or live-video fields',!('worlds' in document)&&!('bindings' in document)&&!('fullscreen' in document.settings));
      const sentinel=path.join(runtime,'Saved/SaveGames/UnrelatedProgress.sav');
      fs.mkdirSync(path.dirname(sentinel),{recursive:true});fs.writeFileSync(sentinel,'preserve-world-progress');
      await prepare('video');await page.locator('#hard-reset').scrollIntoViewIfNeeded();await click('Hard Reset');await click('Cancel');
      check('Hard Reset Cancel preserves saved choices',(await state()).settings.fov===91);
      await click('Hard Reset');await page.getByRole('button',{name:'Hard Reset',exact:true}).click();
      await page.waitForFunction(()=>window.MenuPreferences?.available()&&MenuPrototype.getState().screen==='main'&&MenuPreferences.getState().settings.fov===110&&window.MenuApplication?.enabled());
      const defaults=await state();
      check('Confirmed Reset restores all 101 defaults',Object.entries(schema.settings).every(([id,d])=>defaults.settings[id]===d.default));
      check('Confirmed Reset restores locale, packs and flags',defaults.language==='en_us'&&defaults.packs.length===0&&!defaults.skipWarning&&!defaults.telemetryOptInExtra);
      check('Reset persists defaults',JSON.parse(fs.readFileSync(preferencesFile)).settings.fov===110);
      check('Reset preserves unrelated progress',fs.readFileSync(sentinel,'utf8')==='preserve-world-progress');
      await prepare('sounds');await page.evaluate(()=>MenuPrototype.freezeFrame());
      await page.screenshot({path:path.join(output,profile+'-preferences.png')});
      await prepare('main');await page.getByRole('button',{name:'Quit Game',exact:true}).click();
    }
    let timer;
    const status=await Promise.race([exit,new Promise(resolve=>timer=setTimeout(()=>resolve(null),30000))]);clearTimeout(timer);
    check(profile+' normal Quit flushes and exits',status?.code===0&&status.signal===null,status);
  }catch(error){
    checks.push({name:profile+' suite completed',pass:false,detail:String(error)});console.error(error);
    if(page&&!page.isClosed()){
      fs.writeFileSync(path.join(output,profile+'-failure.json'),JSON.stringify(await page.evaluate(()=>({state:MenuPrototype.getState(),native:MenuPreferences.getState(),controls:MenuPrototype.controls()})),null,2));
      await page.screenshot({path:path.join(output,profile+'-failure.png')});
    }
  }
  finally{if(browser)await browser.close();if(game.exitCode===null){game.kill();await exit;}}
}
checks.push({name:'No uncaught browser errors',pass:errors.length===0,detail:errors});
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
fs.writeFileSync(path.join(output,'results.json'),JSON.stringify({
  finished:new Date().toISOString(),exe,exeSHA256:hash(exe),
  htmlSHA256:hash(path.join(packageRoot,'Content/Menu/Web/index.html')),runtime,checks
},null,2)+'\n');
console.log('Report: '+path.join(output,'results.json'));
if(checks.some(c=>!c.pass))process.exitCode=1;
