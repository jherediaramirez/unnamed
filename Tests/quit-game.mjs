import {chromium} from 'playwright';
import {evidencePath} from './evidence.mjs';
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const directory=path.join(root,'Tests/Runtime/quit-game-'+Date.now());
fs.mkdirSync(directory,{recursive:true});
const argument=(key,fallback)=>process.argv.find(x=>x.startsWith(`--${key}=`))?.slice(key.length+3)||fallback;
const packageName=argument('package','MenuReviewFixesDevelopment');
const reportName=argument('report','ReviewFixesFinalQuitResults.json');
if(!/^[A-Za-z0-9_-]+$/.test(packageName)||!/^[A-Za-z0-9_-]+\.json$/.test(reportName))throw Error('Invalid package or report name');
if(fs.existsSync(evidencePath(reportName)))throw Error('Report exists; choose a fresh name to preserve evidence.');
const exe=path.join(root,'Packages',packageName,'Windows/Unnamed/Binaries/Win64/Unnamed.exe');
const checks=[];
let game,browser,page;
const check=(name,pass,detail)=>{checks.push({name,pass:!!pass,detail});console.log((pass?'PASS ':'FAIL ')+name);assert.ok(pass,name);};
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function launch(width,height,settingsFile,logFile){
  game=spawn(exe,['-windowed',`-ResX=${width}`,`-ResY=${height}`,'-NoSplash','-WinX=0','-WinY=0','-cefdebug=18893',`-MenuVideoSettingsFile=${settingsFile}`,`-abslog=${logFile}`],{cwd:root,windowsHide:true,stdio:'ignore'});
  const deadline=Date.now()+45000;
  while(!browser){try{browser=await chromium.connectOverCDP('http://127.0.0.1:18893');}catch(error){if(Date.now()>deadline||game.exitCode!==null)throw error;await delay(500);}}
  while(!page){page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/Menu/Web/index.html'));if(!page){if(Date.now()>deadline)throw Error('No menu target');await delay(100);}}
  await page.waitForFunction(()=>window.MenuApplication?.enabled()&&window.MenuVideo?.getState()?.capabilities.fullscreen,null,{timeout:30000});
}
async function closeByButton(keyboard=false){
  const exited=new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(Error('Quit did not terminate the game normally')),30000);
    game.once('exit',(code,signal)=>{clearTimeout(timer);resolve({code,signal});});
  });
  const quit=page.getByRole('button',{name:'Quit Game',exact:true});
  if(keyboard){await quit.focus();await page.keyboard.press('Enter');}else await quit.click();
  const status=await exited;
  await browser.close();browser=null;page=null;game=null;
  return status;
}
async function release(){
  if(browser){await browser.close().catch(()=>{});browser=null;page=null;}
  if(game&&game.exitCode===null){game.kill();await new Promise(resolve=>game.once('exit',resolve));}
  game=null;
}
try{
  for(const [width,height,fullscreen,keyboard] of [[1280,720,false,true],[1920,1080,true,false]]){
    const profile=width+'x'+height;
    const folder=path.join(directory,profile);fs.mkdirSync(folder);
    const settingsFile=path.join(folder,'MenuVideoSettings.ini'),logFile=path.join(folder,'quit.log');
    const sentinel=path.join(folder,'protected-world.save');fs.writeFileSync(sentinel,'Protected world data');
    await launch(width,height,settingsFile,logFile);
    check(profile+' removes the mock quit screen',await page.evaluate(()=>!MenuPrototype.getState().routes.includes('quit')));
    await page.evaluate(async fullscreen=>{
      for(const [id,value] of [['guiScale',2],['menuBackgroundBlurriness',8],['enableVsync',false],['fullscreen',fullscreen]])await MenuPrototype.setSetting(id,value);
      await MenuVideo.settled();
      void MenuPrototype.setSetting('framerateLimit',30);
      void MenuPrototype.setSetting('framerateLimit',60);
      MenuPrototype.prepare('main');
    },fullscreen);
    const exit=await closeByButton(keyboard);
    check(profile+' Quit Game exits normally without process termination',exit.code===0&&exit.signal===null,exit);
    const log=fs.readFileSync(logFile,'utf8');
    check(profile+' records saved settings and normal engine shutdown',log.includes('Settings flushed; requesting normal game shutdown')&&log.includes('LogExit: Exiting.'),log.slice(-1500));
    const saved=fs.readFileSync(settingsFile,'utf8');
    check(profile+' queued settings finish before exit',saved.includes('FrameLimit=60')&&saved.includes('GuiScale=2')&&saved.includes('BackgroundBlur=8')&&saved.includes('VSync=False'),saved);
    await launch(width,height,settingsFile,path.join(folder,'relaunch.log'));
    const restored=await page.evaluate(()=>MenuVideo.getState());
    check(profile+' saved settings restore after Quit Game',restored.settings.fullscreen===fullscreen&&restored.settings.framerateLimit===60&&restored.settings.guiScale===2&&restored.settings.menuBackgroundBlurriness===8&&!restored.settings.enableVsync,restored);
    check(profile+' unrelated save survives quit and relaunch',fs.readFileSync(sentinel,'utf8')==='Protected world data');
    check(profile+' second Quit Game also exits normally',(await closeByButton()).code===0);
  }
  const folder=path.join(directory,'save-failure');fs.mkdirSync(folder);
  const blocked=path.join(folder,'MenuVideoSettings.ini');fs.mkdirSync(blocked);
  await launch(1280,720,blocked,path.join(folder,'quit.log'));
  await page.getByRole('button',{name:'Quit Game',exact:true}).click();
  await page.waitForFunction(()=>MenuApplication.getState()?.error&&!MenuApplication.getState().pendingQuit&&MenuPrototype.getState().screen==='message');
  check('Failed save keeps game running and cancels exit',game.exitCode===null&&await page.evaluate(()=>!MenuApplication.getState().quitIssued));
  fs.rmdirSync(blocked);
  await page.getByRole('button',{name:'Back',exact:true}).click();
  check('Quit can be retried after correcting save failure',(await closeByButton()).code===0);
}catch(error){checks.push({name:'Runtime suite completed',pass:false,detail:String(error)});throw error;}
finally{
  await release();
  fs.mkdirSync(evidencePath(),{recursive:true});fs.writeFileSync(evidencePath(reportName),JSON.stringify({packageName,directory,finished:new Date().toISOString(),checks},null,2)+'\n');
}
