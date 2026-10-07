import {chromium} from 'playwright';
import {evidencePath} from './evidence.mjs';
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import crypto from 'node:crypto';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const arg=(key,fallback)=>process.argv.find(a=>a.startsWith(`--${key}=`))?.slice(key.length+3)||fallback;
const packageName=arg('package','MenuReviewFixesDevelopment');
const profile=arg('profile','1280x720');
const capture=arg('capture','review-regression-'+profile);
if(!/^[A-Za-z0-9_-]+$/.test(packageName)||!/^[A-Za-z0-9_-]+$/.test(capture)||!/^\d+x\d+$/.test(profile))throw Error('Invalid suite arguments');
const [width,height]=profile.split('x').map(Number);
const out=evidencePath('Captures',capture);
if(fs.existsSync(out))throw Error('Capture exists; select a fresh name to preserve evidence');
fs.mkdirSync(out,{recursive:true});
const userDir=path.join(root,'Tests/Runtime',capture+'-'+Date.now());fs.mkdirSync(userDir,{recursive:true});
const checks=[];
let htmlSHA256;
const check=(name,pass,detail)=>{checks.push({name,pass:!!pass,detail});console.log((pass?'PASS ':'FAIL ')+name);};
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const game=spawn(path.join(root,'Packages',packageName,'Windows/Unnamed/Binaries/Win64/Unnamed.exe'),
  ['-windowed',`-ResX=${width}`,`-ResY=${height}`,'-NoSplash','-cefdebug=18911','-MenuSettingsSessionOnly',`-UserDir=${userDir}/`,`-abslog=${userDir}/review.log`],
  {cwd:root,windowsHide:true,stdio:'ignore'});
let browser,page;
try{
  const end=Date.now()+45000;
  while(!browser){try{browser=await chromium.connectOverCDP('http://127.0.0.1:18911');}catch(error){if(Date.now()>end||game.exitCode!==null)throw error;await delay(300);}}
  while(!page){page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/Menu/Web/index.html'));if(!page){if(Date.now()>end)throw Error('No menu target');await delay(100);}}
  await page.waitForFunction(()=>window.MenuApplication?.enabled()&&window.MenuVideo?.getState()?.capabilities.fullscreen);
  htmlSHA256=crypto.createHash('sha256').update(fs.readFileSync(fileURLToPath(page.url()))).digest('hex');
  const prepare=name=>page.evaluate(name=>MenuPrototype.prepare(name),name);
  const toggle=async()=>{await page.keyboard.press('F11');await page.evaluate(()=>MenuVideo.settled());};
  await prepare('editworld');
  const worldName=page.getByRole('textbox').first();
  const original=await worldName.inputValue();
  await worldName.fill('Preserved draft');
  await page.getByRole('button',{name:'Open World Folder',exact:true}).click();
  await page.getByRole('button',{name:'Back',exact:true}).click();
  check('World-name draft survives child return',await worldName.inputValue()==='Preserved draft');
  await worldName.fill('Fullscreen draft');await toggle();
  check('World-name draft survives fullscreen redraw',await worldName.inputValue()==='Fullscreen draft');
  await page.getByRole('button',{name:'Save',exact:true}).click();
  check('World Save commits the displayed draft',await page.evaluate(()=>MenuPrototype.getState().worlds[0].name==='Fullscreen draft'));
  await toggle();await prepare('editworld');await worldName.fill('');await toggle();
  check('Blank world name remains blank after redraw',await worldName.inputValue()==='');
  check('Blank world name keeps Save disabled',await page.locator('#world-save').isDisabled());
  await page.getByRole('button',{name:'Cancel',exact:true}).click();await toggle();
  await prepare('editworld');await worldName.fill('Discarded draft');await page.getByRole('button',{name:'Cancel',exact:true}).click();
  check('World Cancel leaves committed name unchanged',await page.evaluate(()=>MenuPrototype.getState().worlds[0].name!=='Discarded draft'));

  await prepare('presets');const preset=page.getByRole('textbox');
  await preset.fill('minecraft:stone;minecraft:plains');await toggle();
  check('Preset text survives fullscreen redraw',await preset.inputValue()==='minecraft:stone;minecraft:plains');
  await page.getByRole('button',{name:'Use Preset',exact:true}).click();
  check('Preset submits the same displayed layers',await page.evaluate(()=>flatEdit.length===1&&flatEdit[0].block==='minecraft:stone'));
  await toggle();await prepare('presets');await preset.fill('');await toggle();
  check('Empty preset remains empty after redraw',await preset.inputValue()==='');
  await page.getByRole('button',{name:'Use Preset',exact:true}).click();
  check('Empty preset is rejected instead of falling back',await page.evaluate(()=>MenuPrototype.getState().screen==='message'));
  if(await page.evaluate(()=>MenuPrototype.getState().screen==='message')){
    await page.getByRole('button',{name:'Back',exact:true}).click();
    check('Preset error return retains empty draft',await preset.inputValue()==='');
  }else check('Preset error return retains empty draft',false);
  await toggle();

  await prepare('rules');const numeric=page.locator('#rule-list input').first();
  const ruleLabel=await numeric.getAttribute('aria-label');
  await numeric.fill('invalid');await toggle();
  check('Invalid rule text survives redraw',await page.getByRole('textbox',{name:ruleLabel,exact:true}).inputValue()==='invalid');
  check('Invalid rule still blocks Done after redraw',await page.locator('#rules-done').isDisabled());
  await page.getByRole('textbox',{name:'Search...',exact:true}).fill('no-match-for-this-search');
  check('Filtering does not discard invalid rule validation',await page.locator('#rules-done').isDisabled());
  await page.getByRole('textbox',{name:'Search...',exact:true}).fill('');
  check('Filtered rule retains raw invalid text',await page.getByRole('textbox',{name:ruleLabel,exact:true}).inputValue()==='invalid');
  await page.getByRole('textbox',{name:ruleLabel,exact:true}).fill('1');
  check('Correcting invalid rule restores Done',await page.locator('#rules-done').isEnabled());
  await page.getByRole('button',{name:'Cancel',exact:true}).click();await toggle();await prepare('rules');
  check('Rule Cancel discards invalid/raw drafts',await page.locator('#rules-done').isEnabled()&&await page.getByRole('textbox',{name:ruleLabel,exact:true}).inputValue()!=='invalid');

  await prepare('serveredit');const name=page.getByRole('textbox',{name:'Server Name',exact:true});
  const address=page.getByRole('textbox',{name:'Server Address',exact:true});
  await address.fill('localhost');await name.fill('   ');
  check('Whitespace-only server name blocks Done',await page.locator('#server-save').isDisabled());
  await toggle();check('Blank server remains invalid after redraw',await page.locator('#server-save').isDisabled());
  await name.fill('Local sample');await address.fill('invalid/address');
  check('Invalid server address also blocks Done',await page.locator('#server-save').isDisabled());
  await address.fill('localhost');check('Valid name and address enable Done',await page.locator('#server-save').isEnabled());
  await page.getByRole('button',{name:'Done',exact:true}).click();
  check('Server Save commits the valid displayed name',await page.evaluate(()=>MenuPrototype.getState().servers[0].name==='Local sample'));
  await toggle();
  check('No browser runtime errors',await page.evaluate(()=>MenuPrototype.getState().errors.length===0));
  await prepare('main');await page.screenshot({path:path.join(out,'main.png')});
}catch(error){check('Suite completed',false,String(error));}
finally{
  if(page&&!page.isClosed()){
    try{
      await page.evaluate(()=>MenuPrototype.prepare('main'));
      const exited=new Promise(resolve=>game.once('exit',(code,signal)=>resolve({code,signal})));
      await page.getByRole('button',{name:'Quit Game',exact:true}).click();
      let timer;
      const exit=await Promise.race([exited,new Promise(resolve=>timer=setTimeout(()=>resolve(null),30000))]);
      clearTimeout(timer);
      check('Owned diagnostic quits normally',exit?.code===0&&exit.signal===null,exit);
    }catch(error){check('Normal diagnostic cleanup',false,String(error));}
  }
  if(browser)await browser.close();
  if(game.exitCode===null){game.kill();await new Promise(resolve=>game.once('exit',resolve));}
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({packageName,profile,htmlSHA256,userDir,finished:new Date().toISOString(),checks},null,2)+'\n');
}
if(checks.some(c=>!c.pass))process.exitCode=1;
