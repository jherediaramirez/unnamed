import {chromium} from 'playwright';
import {evidencePath} from './evidence.mjs';
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';

const tests=path.dirname(fileURLToPath(import.meta.url)),root=path.dirname(tests);
const stamp=new Date().toISOString().replaceAll(/[:.]/g,'-');
const runtime=path.join(tests,'Runtime','input-foundation-'+stamp);
const output=evidencePath('Captures','input-foundation-'+stamp);
fs.mkdirSync(runtime,{recursive:true});fs.mkdirSync(output,{recursive:true});
const checks=[],delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const packageName=process.argv.find(a=>a.startsWith('--package='))?.slice(10)||'CodeReviewDevelopment20261007';
if(!/^[A-Za-z0-9_-]+$/.test(packageName))throw Error('Invalid package name');
const exe=path.join(root,'Packages',packageName,'Windows/Unnamed/Binaries/Win64/Unnamed.exe');
const htmlSHA256=crypto.createHash('sha256').update(fs.readFileSync(path.join(root,'Content/Menu/Web/index.html'))).digest('hex');
function check(name,pass,detail){checks.push({name,pass:!!pass,detail});console.log((pass?'PASS ':'FAIL ')+name);if(!pass)throw Error(name);}
for(const [index,profile] of ['1280x720','1920x1080'].entries()){
  const [width,height]=profile.split('x');
  const game=spawn(exe,['-windowed',`-ResX=${width}`,`-ResY=${height}`,'-NoSplash','-cefdebug=18920',`-UserDir=${runtime}/`,`-MenuVideoSettingsFile=${runtime}/video-${profile}.ini`,`-abslog=${runtime}/${profile}.log`],{cwd:root,windowsHide:true,stdio:'ignore'});
  const exit=new Promise((resolve,reject)=>{game.once('error',reject);game.once('exit',(code,signal)=>resolve({code,signal}));});
  let browser,page;
  try{
    const deadline=Date.now()+45000;
    while(!browser){try{browser=await chromium.connectOverCDP('http://127.0.0.1:18920');}catch(error){if(Date.now()>deadline||game.exitCode!==null)throw error;await delay(300);}}
    while(!page){page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/Menu/Web/index.html'));if(!page){if(Date.now()>deadline)throw Error('No CEF menu target');await delay(100);}}
    await page.waitForFunction(()=>window.MenuInput?.getState()?.ready&&window.MenuApplication?.enabled()&&window.MenuVideo?.getState());
    const state=()=>page.evaluate(()=>MenuInput.getState());
    const settle=()=>page.evaluate(()=>MenuInput.settled().catch(()=>{}));
    const capture=async(id,key)=>{await page.locator(`[data-binding="${id}"]`).click();await page.keyboard.press(key);await settle();};
    const prepared=route=>page.evaluate(route=>MenuPrototype.prepare(route),route);
    const s=await state();
    check(profile+' seven native movement rows',s.supported.length===7&&s.persistent);
    check(profile+' saved/default forward binding',s.nativeKeys['key.forward']===(index===0?'W':'Up'),s.nativeKeys);
    check(profile+' saved/default Hold policy',s.policies.toggleSprint===(index!==0),s.policies);
    check(profile+' Shift sprint and C crouch defaults',s.defaults['key.sprint']==='Left Shift'&&s.defaults['key.sneak']==='C');
    check(profile+' native input build is v2',s.build==='Input v2');
    await prepared('video');
    const graphicsAPI=page.locator('[data-setting="preferredGraphicsBackend"]');
    check(profile+' graphics API reports real DirectX 12 default',await graphicsAPI.textContent()==='Graphics API: DirectX 12'&&await graphicsAPI.isDisabled());
    check(profile+' graphics API label fits its button',await graphicsAPI.evaluate(n=>n.scrollWidth<=n.clientWidth));
    check(profile+' graphics tooltip reports actual running DirectX 12',(await graphicsAPI.getAttribute('data-tooltip')).includes('Default is DirectX 12.')&&(await graphicsAPI.getAttribute('data-tooltip')).includes('Currently running: DirectX 12.'));
    // Exercise the actual hover handler without the OS cursor's background Slate
    // events racing CDP hover. Physical cursor acceptance is a separate window test.
    await page.locator('.graphics-api-info').dispatchEvent('pointerenter');await page.waitForTimeout(900);
    check(profile+' tooltip handler renders the native graphics API',await page.locator('#tooltip').isVisible()&&(await page.locator('#tooltip').textContent()).includes('Default is DirectX 12.'));
    await page.screenshot({path:path.join(output,profile+'-graphics-api-tooltip.png')});
    await prepared('keybinds');
    check(profile+' visible input save status',(await page.locator('[data-input-status]').textContent()).includes('Input v2 - Changes save automatically'));
    check(profile+' deferred bindings disabled',await page.locator('[data-binding="key.attack"]').isDisabled());
    await capture('key.forward','Escape');
    check(profile+' Escape cancels capture',(await state()).nativeKeys['key.forward']===(index===0?'W':'Up'));
    await capture('key.forward','ArrowUp');
    check(profile+' real remap stores Unreal key',(await state()).nativeKeys['key.forward']==='Up');
    await capture('key.back','ArrowUp');
    check(profile+' conflicting key rejected',(await state()).nativeKeys['key.back']==='S');
    await page.getByRole('button',{name:'Back',exact:true}).click();
    const mode=await page.evaluate(()=>MenuVideo.getState().observed.windowMode);
    await capture('key.jump','F11');
    check(profile+' reserved F11 does not toggle window',await page.evaluate(()=>MenuVideo.getState().observed.windowMode)===mode);
    await page.getByRole('button',{name:'Back',exact:true}).click();
    await capture('key.sprint','ShiftRight');
    check(profile+' right modifier side preserved',(await state()).nativeKeys['key.sprint']==='RightShift');
    await page.locator('[data-binding-reset="key.sprint"]').click();await settle();
    await capture('key.jump','Backspace');
    check(profile+' explicit unbind',(await state()).nativeKeys['key.jump']==='None');
    await page.locator('[data-binding-reset="key.jump"]').click();await settle();
    await page.locator('[data-binding="key.jump"]').click();await page.mouse.click(20,20,{button:'middle'});await settle();
    check(profile+' native mouse button capture',(await state()).nativeKeys['key.jump']==='MiddleMouseButton');
    await page.locator('[data-binding-reset="key.jump"]').click();await settle();
    await page.evaluate(()=>MenuPrototype.freezeFrame());
    await page.screenshot({path:path.join(output,profile+'-keybinds.png')});
    check(profile+' exact browser viewport',await page.evaluate(([w,h])=>innerWidth===w&&innerHeight===h,[Number(width),Number(height)]));
    await prepared('controls');
    if(!(await state()).policies.toggleSprint){await page.locator('[data-setting="toggleSprint"]').click();await settle();}
    check(profile+' Toggle policy updates native profile',(await state()).policies.toggleSprint);
    await page.reload();await page.waitForFunction(()=>window.MenuInput?.getState()?.ready&&window.MenuApplication?.enabled());
    check(profile+' page reload rehydrates native input',(await state()).nativeKeys['key.forward']==='Up'&&(await state()).policies.toggleSprint);
    if(index===1){
      await prepared('keybinds');await page.getByRole('button',{name:'Reset Keys',exact:true}).click();await settle();
      check(profile+' Reset Keys restores bindings and preserves policies',(await state()).nativeKeys['key.forward']==='W'&&(await state()).policies.toggleSprint);
      await capture('key.forward','ArrowUp');
      const sentinel=path.join(runtime,'Saved','SaveGames','UnrelatedProgress.sav');fs.mkdirSync(path.dirname(sentinel),{recursive:true});fs.writeFileSync(sentinel,'preserve-input-test-progress');
      await prepared('video');await page.locator('#hard-reset').scrollIntoViewIfNeeded();await page.getByRole('button',{name:'Hard Reset',exact:true}).click();
      await page.getByRole('button',{name:'Cancel',exact:true}).click();
      check(profile+' reset confirmation Cancel preserves input',(await state()).nativeKeys['key.forward']==='Up');
      await page.getByRole('button',{name:'Hard Reset',exact:true}).click();await page.getByRole('button',{name:'Hard Reset',exact:true}).click();
      await page.waitForFunction(()=>window.MenuInput?.getState()?.ready&&MenuPrototype.getState().screen==='main'&&MenuInput.getState().nativeKeys['key.forward']==='W');
      check(profile+' confirmed reset restores input and Hold policies',!(await state()).policies.toggleSprint&&!(await state()).policies.toggleCrouch);
      check(profile+' unrelated progress survives reset',fs.readFileSync(sentinel,'utf8')==='preserve-input-test-progress');
    }
    check(profile+' no uncaught browser errors',await page.evaluate(()=>MenuPrototype.getState().errors.length===0));
    await prepared('main');await page.getByRole('button',{name:'Quit Game',exact:true}).click();
    let timer;const result=await Promise.race([exit,new Promise(resolve=>timer=setTimeout(()=>resolve(null),30000))]);clearTimeout(timer);
    check(profile+' normal owned-process quit',result?.code===0&&result.signal===null,result);
  }catch(error){checks.push({name:profile+' runtime suite',pass:false,detail:String(error)});console.error(error);}
  finally{if(browser)await browser.close();if(game.exitCode===null){game.kill();await exit;}}
}
fs.writeFileSync(path.join(output,'results.json'),JSON.stringify({finished:new Date().toISOString(),htmlSHA256,runtime,exe,checks},null,2)+'\n');
console.log('Report: '+path.join(output,'results.json'));
if(checks.some(c=>!c.pass))process.exitCode=1;
