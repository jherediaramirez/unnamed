import {chromium} from 'playwright';
import {evidencePath} from './evidence.mjs';
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

// CEF + native bridge integration. This does not replace hardware/OS input tests.
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const argument=(name,fallback)=>process.argv.find(a=>a.startsWith(name+'='))?.slice(name.length+1)||fallback;
const packageName=argument('--package','CodeReviewDevelopment20261007');
const output=path.resolve(argument('--output',evidencePath('Foundation','Menu-'+Date.now())));
const editor=process.argv.includes('--editor');
const exe=editor?'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe':path.join(root,`Packages/${packageName}/Windows/Unnamed/Binaries/Win64/Unnamed.exe`);
fs.mkdirSync(output,{recursive:true});const checks=[],errors=[],delay=ms=>new Promise(r=>setTimeout(r,ms));
const check=(name,pass,detail='')=>{checks.push({name,pass,detail});console.log(`${pass?'PASS':'FAIL'} ${name}`);if(!pass)throw Error(name);};
let browser,page,game,exit;
async function waitFor(fn,limit=60000){const start=Date.now();while(!await fn()){if(Date.now()-start>limit||game?.exitCode!==null)throw Error('Runtime timeout');await delay(150);}}
async function attach(){await waitFor(async()=>{try{browser=await chromium.connectOverCDP('http://127.0.0.1:18944');return true;}catch{return false;}});await waitFor(()=>{page=browser.contexts().flatMap(c=>c.pages()).find(p=>!p.isClosed()&&p.url().includes('/Menu/Web/index.html'));return !!page;});await page.waitForFunction(()=>typeof MenuInput!=='undefined'&&typeof MenuApplication!=='undefined'&&typeof MenuPreferences!=='undefined'&&MenuInput.getState()?.ready&&MenuApplication.enabled()&&MenuPreferences.available());page.on('pageerror',e=>errors.push(String(e)));}
async function start(suffix){const args=editor?[path.join(root,'Unnamed.uproject'),'-game']:[];args.push('-windowed','-ResX=1280','-ResY=720','-RenderOffscreen','-unattended','-nosplash','-nosound','-cefdebug=18944',`-UserDir=${output}/profile/`,`-MenuVideoSettingsFile=${output}/video.ini`,`-MenuPreferencesFile=${output}/preferences.json`,`-abslog=${output}/${suffix}.log`,`-FoundationProbe=${output}/travel.json`,'-FoundationMenuCycle');game=spawn(exe,args,{cwd:root,windowsHide:true,stdio:'ignore'});exit=new Promise((resolve,reject)=>{game.once('error',reject);game.once('exit',(code,signal)=>resolve({code,signal}));});await attach();}
async function quit(){try{await page.evaluate(()=>MenuApplication.quit());}catch(e){if(!/closed/i.test(String(e)))throw e;}const status=await Promise.race([exit,delay(20000).then(()=>null)]);check('normal exit',status?.code===0,JSON.stringify(status));await browser.close();browser=null;}
const state=()=>page.evaluate(()=>MenuInput.getState());
async function capture(id,key){await page.locator(`[data-binding="${id}"]`).click();await page.waitForFunction(id=>bindingCapture===id,id);await page.keyboard.press(key);await page.evaluate(()=>MenuInput.settled().catch(()=>{}));}
try{
    await start('write');let s=await state();check('35 native configurable foundation actions',s.supported.length===35&&s.persistent);
    check('fresh profile uses Shift sprint and C crouch',s.nativeKeys['key.sprint']==='LeftShift'&&s.nativeKeys['key.sneak']==='C');
    await page.evaluate(()=>MenuPrototype.prepare('keybinds'));
    check('Controls labels match native personal defaults',(await page.locator('[data-binding="key.sprint"]').innerText())===s.defaults['key.sprint']&&(await page.locator('[data-binding="key.sneak"]').innerText())===s.defaults['key.sneak']);
    await capture('key.forward','k');check('CEF capture changes native forward', (await state()).nativeKeys['key.forward']==='K');
    await capture('key.jump','Enter');await capture('key.sprint','ControlRight');await capture('key.sneak','ShiftRight');
    await page.locator('[data-binding-reset="key.sneak"]').click();await page.evaluate(()=>MenuInput.settled());s=await state();check('reset crouch restores C and preserves custom sprint',s.nativeKeys['key.sneak']==='C'&&s.nativeKeys['key.sprint']==='RightControl');
    await page.locator('[data-binding-reset="key.sprint"]').click();await page.evaluate(()=>MenuInput.settled());s=await state();check('reset sprint restores Shift and preserves custom forward',s.nativeKeys['key.sprint']==='LeftShift'&&s.nativeKeys['key.forward']==='K');
    await capture('key.sprint','ControlRight');await capture('key.sneak','ShiftRight');
    await capture('key.back','k');check('context conflict preserves old back binding',(await state()).nativeKeys['key.back']==='S');await page.keyboard.press('Escape');
    await capture('key.forward','Escape');check('capture cancellation preserves K',(await state()).nativeKeys['key.forward']==='K');
    await capture('key.jump','Backspace');check('explicit unbind',(await state()).nativeKeys['key.jump']==='None');
    await page.locator('[data-binding-reset="key.jump"]').click();await page.evaluate(()=>MenuInput.settled());check('reset one uses vanilla Space',(await state()).nativeKeys['key.jump']==='SpaceBar');
    await page.locator('[data-binding="key.attack"]').click();await page.waitForFunction(()=>bindingCapture==='key.attack');
    const cdp=await page.context().newCDPSession(page);await cdp.send('Input.dispatchMouseEvent',{type:'mousePressed',x:20,y:20,button:'back',buttons:8,clickCount:1});await cdp.send('Input.dispatchMouseEvent',{type:'mouseReleased',x:20,y:20,button:'back',buttons:0,clickCount:1});await cdp.detach();
    await page.evaluate(()=>MenuInput.settled());check('extra mouse button capture through CEF',(await state()).nativeKeys['key.attack']==='ThumbMouseButton');
    await page.locator('[data-binding="key.jump"]').click();await page.waitForFunction(()=>bindingCapture==='key.jump');await page.mouse.wheel(0,-100);await page.evaluate(()=>MenuInput.settled());check('wheel capture',(await state()).nativeKeys['key.jump']==='MouseScrollUp');
    await page.evaluate(()=>MenuInput.setPolicy('toggleCrouch',true));await page.evaluate(()=>MenuInput.settled());
    await page.screenshot({path:path.join(output,'controls-native-cef.png')});
    const saved=(await state()).nativeKeys;await quit();
    await start('reload');s=await state();check('bindings survive process relaunch',JSON.stringify(s.nativeKeys)===JSON.stringify(saved));check('toggle mode survives process relaunch',s.policies.toggleCrouch===true);
    await page.evaluate(()=>MenuInput.resetAll(true));check('reset-all restores Shift sprint and C crouch',(await state()).nativeKeys['key.sprint']==='LeftShift'&&(await state()).nativeKeys['key.sneak']==='C');
    for(const id of ['fictional-forest','fictional-build','/Game/Maps/L_Playground','playground?listen']){const reply=await page.evaluate(async id=>JSON.parse(await window.ue.menuapplication.requestopenworld(id)),id);check('Native rejects noncatalog travel: '+id,!reply.ok&&!reply.pendingWorld&&!reply.travelIssued,reply);}
    for(const [index,action] of ['button','double click','Enter'].entries()){
        await page.getByRole('button',{name:'Singleplayer',exact:true}).click();
        await page.waitForFunction(()=>MenuPrototype.getState().screen==='worlds');
        if(!index){
            check('Singleplayer lists Playground and exactly two design references',await page.locator('[data-world]').count()===3&&await page.locator('[data-world="playground"]').count()===1);
            for(const id of ['fictional-forest','fictional-build']){
                const row=page.locator(`[data-world="${id}"]`);await row.click();
                check(id+' labeled as reference with Play disabled',(await row.textContent()).includes('Design reference')&&await page.getByRole('button',{name:'Play Selected World',exact:true}).isDisabled());
                await row.dblclick();
                check(id+' double click remains a reference explanation',await page.evaluate(()=>MenuPrototype.getState().screen==='message'));
                await page.keyboard.press('Escape');
            }
        }
        await page.locator('[data-world="playground"]').click();
        if(!index){
            await page.screenshot({path:path.join(output,'singleplayer-native-cef.png')});
            for(const label of ['Edit','Delete','Re-Create'])check('Built-in world protected from mock '+label,await page.getByRole('button',{name:label,exact:true}).isDisabled());
        }
        if(action==='button')await page.getByRole('button',{name:'Play Selected World',exact:true}).click();
        else if(action==='double click')await page.locator('[data-world="playground"]').dblclick();
        else{await page.locator('[data-world="playground"]').focus();await page.keyboard.press('Enter');}
        const report=path.join(output,`travel.${index+1}.json`);await waitFor(()=>fs.existsSync(report));const result=JSON.parse(fs.readFileSync(report));check(`${action}: menu to possessed moving pawn`,result.failures===0,result.checks);
        await browser.close();browser=null;await delay(500);await attach();check(`${action}: returned usable menu`,await page.evaluate(()=>MenuPrototype.getState().screen==='main'));check('one browser after travel',browser.contexts().flatMap(c=>c.pages()).filter(p=>!p.isClosed()&&p.url().includes('/Menu/Web/index.html')).length===1);
    }
    check('no uncaught JavaScript errors',errors.length===0,errors);await quit();
}catch(e){checks.push({name:'harness completed',pass:false,detail:String(e)});console.error(e);if(page&&!page.isClosed()){await page.screenshot({path:path.join(output,'failure.png')}).catch(()=>{});fs.writeFileSync(path.join(output,'failure-state.json'),JSON.stringify(await page.evaluate(()=>MenuPrototype.getState()).catch(()=>({})),null,2));}}
finally{if(browser)await browser.close();if(game&&game.exitCode===null){game.kill();await exit;}fs.writeFileSync(path.join(output,'results.json'),JSON.stringify({evidence:'CEF and native bridge, not OS hardware input',checks},null,2));}
console.log(output);if(checks.some(c=>!c.pass))process.exitCode=1;
