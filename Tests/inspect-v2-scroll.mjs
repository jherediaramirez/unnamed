import {chromium} from 'playwright';
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
const root=path.resolve(import.meta.dirname,'..');
const runtime=path.join(root,'Tests/Runtime','v2-scroll-'+Date.now());fs.mkdirSync(runtime,{recursive:true});
const game=spawn(path.join(root,'Packages/MenuInputCrouchCDevelopment/Windows/Unnamed/Binaries/Win64/Unnamed.exe'),
  ['-windowed','-ResX=1920','-ResY=1080','-NoSplash','-cefdebug=18926','-MenuSettingsSessionOnly',`-UserDir=${runtime}/`],
  {cwd:root,windowsHide:true,stdio:'ignore'});
const exit=new Promise(resolve=>game.once('exit',resolve));let b,p;
try{
  for(let i=0;i<200&&!b;i++){try{b=await chromium.connectOverCDP('http://127.0.0.1:18926');}catch{await new Promise(r=>setTimeout(r,100));}}
  for(let i=0;i<100&&!p;i++){p=b.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('Menu/Web'));if(!p)await new Promise(r=>setTimeout(r,100));}
  await p.waitForFunction(()=>window.MenuInput?.getState()?.ready&&window.MenuApplication?.enabled()&&window.MenuVideo?.getState());
  await p.evaluate(async()=>{await MenuVideo.set('guiScale',0);MenuPrototype.prepare('video');});
  const inspect=()=>p.evaluate(()=>{const n=document.querySelector('.scroll'),r=n.getBoundingClientRect(),a=document.querySelector('[data-setting="preferredGraphicsBackend"]'),ar=a.getBoundingClientRect();return {state:MenuPrototype.getState().gui,rect:{x:r.x,y:r.y,width:r.width,height:r.height},hit:document.elementFromPoint(r.x+r.width/2,r.y+r.height/2)?.outerHTML.slice(0,200),styles:{pointer:getComputedStyle(n).pointerEvents,transform:getComputedStyle(document.querySelector('#gui')).transform},apiRect:{x:ar.x,y:ar.y,width:ar.width,height:ar.height}}});
  console.log(JSON.stringify(await inspect(),null,2));
  await p.evaluate(()=>{window.pointerEvidence=[];document.addEventListener('pointermove',e=>pointerEvidence.push({x:e.clientX,y:e.clientY,target:e.target.className,setting:e.target.dataset.setting,trusted:e.isTrusted}),true);document.addEventListener('pointerover',e=>pointerEvidence.push({over:true,x:e.clientX,y:e.clientY,target:e.target.className,setting:e.target.dataset.setting,trusted:e.isTrusted}),true);});
  const apiPoint=await p.locator('[data-setting="preferredGraphicsBackend"]').evaluate(n=>{const r=n.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}});
  await p.mouse.move(apiPoint.x,apiPoint.y);await p.waitForTimeout(900);
  console.log('Tooltip '+JSON.stringify(await p.locator('#tooltip').evaluate(n=>({text:n.textContent,display:getComputedStyle(n).display}))));
  console.log('Pointer events '+JSON.stringify(await p.evaluate(()=>pointerEvidence)));
  await p.screenshot({path:path.join(root,'Tests/Captures/v2-api-hover.png')});
  const box=await p.locator('.scroll').evaluate(n=>{const r=n.getBoundingClientRect();return {x:r.x,y:r.y}});await p.mouse.move(box.x+20,box.y+20);await p.mouse.wheel(0,500);await p.waitForTimeout(200);
  console.log('Wheel scrollTop '+await p.locator('.scroll').evaluate(n=>n.scrollTop));
  await p.evaluate(()=>MenuPrototype.prepare('main'));await p.getByRole('button',{name:'Quit Game',exact:true}).click();await exit;
}catch(e){console.error(e);process.exitCode=1;}finally{if(b)await b.close();if(game.exitCode===null){game.kill();await exit;}}
