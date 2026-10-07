// Owned, isolated game sessions. Windows input is supplied through Computer Use;
// CDP observes its DOM/native result and verifies relaunch persistence.
import {chromium} from 'playwright';
import {evidencePath} from './evidence.mjs';
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const editor=process.argv.includes('--editor'),mode=editor?'editor-game':'packaged';
const stamp=new Date().toISOString().replaceAll(/[:.]/g,'-');
const output=evidencePath('Captures','native-input-'+mode+'-'+stamp);
const runtime=path.join(root,'Tests/Runtime','native-input-'+mode+'-'+stamp);
fs.mkdirSync(output,{recursive:true});fs.mkdirSync(runtime,{recursive:true});
const checks=[],events=[],delay=ms=>new Promise(r=>setTimeout(r,ms));
function check(name,pass,detail){checks.push({name,pass:!!pass,detail});console.log((pass?'PASS ':'FAIL ')+name);if(!pass)throw Error(name);}
const exe=editor?'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe':
  path.join(root,'Packages/MenuInputCrouchCDevelopment/Windows/Unnamed/Binaries/Win64/Unnamed.exe');
let game,browser,page,exit;
async function launch(){
  game=spawn(exe,[...(editor?[path.join(root,'Unnamed.uproject'),'-game']:[]),'-windowed','-ResX=1280','-ResY=720',
    '-NoSplash','-cefdebug=18924',`-UserDir=${runtime}/`,`-MenuVideoSettingsFile=${runtime}/video.ini`,
    `-abslog=${runtime}/game-${checks.length}.log`],{cwd:root,windowsHide:false,stdio:'ignore'});
  exit=new Promise((resolve,reject)=>{game.once('error',reject);game.once('exit',(code,signal)=>resolve({code,signal}));});
  console.log('Owned game PID '+game.pid);
  const deadline=Date.now()+45000;
  while(!browser){try{browser=await chromium.connectOverCDP('http://127.0.0.1:18924');}catch(e){if(Date.now()>deadline||game.exitCode!==null)throw e;await delay(200);}}
  while(!page){page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/Menu/Web/index.html'));if(!page){if(Date.now()>deadline)throw Error('No menu target');await delay(100);}}
  await page.waitForFunction(()=>window.MenuInput?.getState()?.ready&&window.MenuApplication?.enabled()&&window.MenuVideo?.getState());
}
async function quit(){
  await page.evaluate(()=>MenuPrototype.prepare('main'));
  await page.getByRole('button',{name:'Quit Game',exact:true}).click();
  let timer;const result=await Promise.race([exit,new Promise(resolve=>timer=setTimeout(()=>resolve(null),15000))]);clearTimeout(timer);
  check('normal owned process exit',result?.code===0&&result?.signal===null,result);
  await browser.close();browser=null;page=null;
}
try{
  await launch();
  const state=await page.evaluate(()=>MenuInput.getState());
  check('current native build and fresh C/Shift defaults',state.build==='Input v2'&&state.nativeKeys['key.sneak']==='C'&&state.nativeKeys['key.sprint']==='LeftShift',state);
  check('standalone preferences persist',state.persistent);
  await page.evaluate(()=>{window.nativeInputEvents=[];document.addEventListener('keydown',e=>nativeInputEvents.push({key:e.key,code:e.code,keyCode:e.keyCode,location:e.location,isTrusted:e.isTrusted}),true);MenuPrototype.prepare('keybinds');});
  await page.screenshot({path:path.join(output,'defaults.png')});
  console.log('WAIT WINDOWS INPUT: Click Walk Forward and press Up.');
  await page.waitForFunction(()=>MenuInput.getState().nativeKeys['key.forward']==='Up',null,{timeout:180000});
  await page.evaluate(()=>MenuInput.settled());
  events.push(...await page.evaluate(()=>nativeInputEvents.splice(0)));
  check('Windows Up key reaches native remapping',events.some(e=>e.keyCode===38&&e.isTrusted),events);
  await page.screenshot({path:path.join(output,'windows-forward-up.png')});
  console.log('WAIT WINDOWS INPUT: Click Sprint and press Right Shift.');
  await page.waitForFunction(()=>MenuInput.getState().nativeKeys['key.sprint']==='RightShift',null,{timeout:180000});
  await page.evaluate(()=>MenuInput.settled());
  const modifierEvents=await page.evaluate(()=>nativeInputEvents.splice(0));events.push(...modifierEvents);
  check('Windows right Shift location reaches native remapping',modifierEvents.some(e=>e.keyCode===16&&e.location===2&&e.isTrusted),modifierEvents);
  await quit();await delay(500);await launch();
  const restored=await page.evaluate(()=>MenuInput.getState());
  check('Windows remaps survive full process relaunch',restored.nativeKeys['key.forward']==='Up'&&restored.nativeKeys['key.sprint']==='RightShift',restored);
  await page.evaluate(()=>MenuInput.resetAll());
  const reset=await page.evaluate(()=>MenuInput.settled());
  check('reset restores W/C/Left Shift',reset.nativeKeys['key.forward']==='W'&&reset.nativeKeys['key.sneak']==='C'&&reset.nativeKeys['key.sprint']==='LeftShift',reset);
  await page.evaluate(()=>MenuPrototype.prepare('video'));
  const api=await page.locator('[data-setting="preferredGraphicsBackend"]').getAttribute('data-tooltip');
  check('native configured and active graphics APIs are DirectX 12',api.includes('Default is DirectX 12.')&&api.includes('Currently running: DirectX 12.'),api);
  const apiPoint=await page.locator('[data-setting="preferredGraphicsBackend"]').evaluate(n=>{const r=n.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}});
  await page.mouse.move(apiPoint.x,apiPoint.y);await delay(1200);
  await page.screenshot({path:path.join(output,'graphics-api-tooltip.png')});
  await quit();
}catch(e){checks.push({name:'native Windows input suite',pass:false,detail:String(e)});console.error(e);}
finally{if(browser)await browser.close();if(game&&game.exitCode===null){game.kill();await exit;}}
fs.writeFileSync(path.join(output,'results.json'),JSON.stringify({finished:new Date().toISOString(),mode,exe,runtime,checks,events},null,2)+'\n');
console.log('Report: '+path.join(output,'results.json'));
if(checks.some(c=>!c.pass))process.exitCode=1;
