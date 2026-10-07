import {chromium} from 'playwright';
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const tests=path.dirname(fileURLToPath(import.meta.url)),root=path.dirname(tests);
const prefix=process.argv.find(a=>a.startsWith('--prefix='))?.slice(9)||'review-accepted';
if(!/^[A-Za-z0-9_-]+$/.test(prefix))throw Error('Invalid capture prefix');
const report=path.join(tests,prefix+'-ValidationResults.json');
if(fs.existsSync(report))throw Error('Validation report exists; preserve it before selecting a new run.');
const checks=[],delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const check=(name,pass,detail)=>{checks.push({name,pass:!!pass,detail});console.log((pass?'PASS ':'FAIL ')+name);};
const exited=child=>new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',(code,signal)=>resolve({code,signal}));});
for(const profile of ['1280x720','1920x1080']){
  const [width,height]=profile.split('x');
  const directory=path.join(tests,'Runtime','review-validation-'+profile+'-'+Date.now());fs.mkdirSync(directory,{recursive:true});
  const game=spawn(path.join(root,'Packages/MenuReviewFixesDevelopment/Windows/Unnamed/Binaries/Win64/Unnamed.exe'),
    ['-windowed',`-ResX=${width}`,`-ResY=${height}`,'-NoSplash','-cefdebug=18912','-MenuSettingsSessionOnly',`-UserDir=${directory}/`,`-abslog=${directory}/game.log`],
    {cwd:root,windowsHide:true,stdio:'ignore'});
  const gameExit=exited(game);
  let browser,page;
  try {
    const deadline=Date.now()+45000;
    while(!browser){try{browser=await chromium.connectOverCDP('http://127.0.0.1:18912');}catch(error){if(Date.now()>deadline||game.exitCode!==null)throw error;await delay(300);}}
    while(!page){page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/Menu/Web/index.html'));if(!page){if(Date.now()>deadline)throw Error('No menu target');await delay(100);}}
    await page.waitForFunction(()=>window.MenuApplication?.enabled()&&window.MenuVideo?.getState()?.capabilities.fullscreen);
    for(const [file,args] of [
      ['video-settings.mjs',[`--capture=${prefix}-video-${profile}`]],
      ['smoke.mjs',[`--mode=${prefix}-packaged`]]
    ]){
      const child=spawn(process.execPath,[file,'--port=18912',`--profile=${profile}`,...args],{cwd:tests,windowsHide:true,stdio:'inherit'});
      const status=await exited(child);check(profile+' '+file,status.code===0&&status.signal===null,status);
      if(status.code!==0)throw Error('Runtime suite failed');
    }
  }catch(error){check(profile+' validation completed',false,String(error));}
  finally {
    if(page&&!page.isClosed()&&game.exitCode===null){
      try {
        await page.evaluate(()=>MenuPrototype.prepare('main'));
        await page.getByRole('button',{name:'Quit Game',exact:true}).click();
        let timer;
        const status=await Promise.race([gameExit,new Promise(resolve=>timer=setTimeout(()=>resolve(null),30000))]);clearTimeout(timer);
        check(profile+' diagnostic normal exit',status?.code===0&&status.signal===null,status);
      }catch(error){check(profile+' normal cleanup',false,String(error));}
    }
    if(browser)await browser.close();
    if(game.exitCode===null){game.kill();await gameExit;}
  }
}
fs.writeFileSync(report,JSON.stringify({finished:new Date().toISOString(),checks},null,2)+'\n');
if(checks.some(c=>!c.pass))process.exitCode=1;
