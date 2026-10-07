import {chromium} from 'playwright';
import {PNG} from 'pngjs';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);
const port=Number(args.find(x=>x.startsWith('--port='))?.split('=')[1]||18888);
const mode=args.find(x=>x.startsWith('--mode='))?.split('=')[1]||'packaged';
const profile=args.find(x=>x.startsWith('--profile='))?.split('=')[1]||'1920x1080';
const slice=args.includes('--slice');
const skipActions=args.includes('--skip-actions');
const [width,height]=profile.split('x').map(Number);
const out=path.join(root,'Tests/Captures',mode+'-'+profile+(slice?'-slice':skipActions?'-targeted':''));
fs.mkdirSync(out,{recursive:true});
const result={mode,profile,started:new Date().toISOString(),checks:[],console:[],requests:[],routes:[],controls:[],edges:[]};
const check=(name,actual,detail)=>{result.checks.push({name,pass:!!actual,detail});if(!actual)console.error('FAIL '+name,detail);else console.log('PASS '+name)};
async function connect(){
  const until=Date.now()+30000;
  for(;;){try{return await chromium.connectOverCDP(`http://127.0.0.1:${port}`)}catch(e){if(Date.now()>until)throw e;await new Promise(resolve=>setTimeout(resolve,1000))}}
}
const browser=await connect();
try{
  const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('/Menu/Web/index.html'));
  if(!page)throw Error('No Unreal menu browser target');
  page.on('console',m=>result.console.push({type:m.type(),text:m.text()}));
  page.on('pageerror',e=>result.console.push({type:'error',text:String(e)}));
  page.on('request',r=>result.requests.push(r.url()));
  await page.reload();
  await page.waitForFunction(()=>window.MenuPrototype?.getState().assets.panoramaLoaded||window.MenuPrototype?.getState().assets.panoramaFallback,null,{timeout:30000});
  await page.evaluate(()=>MenuPrototype.freezeFrame(0,0));
  const state=()=>page.evaluate(()=>MenuPrototype.getState());
  const prepare=(name,variant='')=>page.evaluate(([n,v])=>MenuPrototype.prepare(n,v),[name,variant]);
  const mouseButton=async(label)=>page.getByRole('button',{name:label,exact:true}).click();
  const value=async(id)=>page.evaluate(id=>MenuPrototype.getState().settings[id],id);
  const set=async(id,v)=>page.evaluate(([id,v])=>MenuPrototype.setSetting(id,v),[id,v]);
  const boot=await state();
  check('Actual rendered viewport',boot.gui.viewportWidth===width&&boot.gui.viewportHeight===height,boot.gui);
  check('Integer Auto scaling',boot.gui.scale===(width===1920?4:3)&&boot.gui.width===(width===1920?480:427)&&boot.gui.height===(height===1080?270:240),boot.gui);
  check('Real cubemap (not fallback)',boot.assets.panoramaLoaded&&!boot.assets.panoramaFallback,boot.assets);
  await page.evaluate(()=>document.fonts.ready);
  check('Bitmap font loaded',await page.evaluate(()=>[...document.fonts].some(f=>f.family==='MC'&&f.status==='loaded')));
  for(const name of ['main','options','video']){
    await prepare(name);
    await page.mouse.move(width-2,height-2);
    await page.waitForTimeout(350);
    const pixels=await page.screenshot({path:path.join(out,name+'.png')});
    const png=PNG.sync.read(pixels);const colors=new Set();
    for(let y=0;y<png.height;y+=19)for(let x=0;x<png.width;x+=19){const i=(y*png.width+x)*4;colors.add(png.data.subarray(i,i+3).toString('hex'))}
    check(name+' nonblank rendered pixels',colors.size>60,{width:png.width,height:png.height,sampledColors:colors.size});
  }
  await prepare('main');
  await mouseButton('Options...');check('Mouse: Main -> Options',(await state()).screen==='options');
  await mouseButton('Video Settings...');check('Mouse: Options -> Video',(await state()).screen==='video');
  await page.keyboard.press('Escape');check('Escape: Video -> Options',(await state()).screen==='options');
  await page.keyboard.press('Escape');check('Escape: Options -> Main',(await state()).screen==='main');
  const baseline=(await state()).presentation;
  const defaultSettings=(await state()).settings;
  await prepare('video');
  const full=page.locator('[data-setting="fullscreen"]');
  await full.scrollIntoViewIfNeeded();await full.click();
  check('Fullscreen changes mock only',await value('fullscreen')===!defaultSettings.fullscreen&&await page.evaluate(()=>!document.fullscreenElement));
  await page.keyboard.press('F11');check('F11 changes mock only',await value('fullscreen')===defaultSettings.fullscreen);
  await prepare('options');
  const slider=page.locator('[data-setting="fov"] input');await slider.focus();await page.keyboard.press('End');
  check('Keyboard slider changes FOV mock',await value('fov')===110);
  await set('guiScale',1);await set('forceUnicodeFont',true);await set('highContrast',true);await set('menuBackgroundBlurriness',0);await set('panoramaSpeed',0);await set('framerateLimit',10);await set('sound_master',0);await set('narrator','Narrates All');
  check('Presentation baseline is immutable',JSON.stringify((await state()).presentation)===JSON.stringify(baseline));
  check('GUI/font/contrast/locale remain baseline',await page.evaluate(()=>!document.querySelector('#gui').classList.contains('uniform-font')&&!document.querySelector('#gui').classList.contains('high-contrast')&&document.documentElement.lang==='en-US')&&(await state()).gui.scale===boot.gui.scale);
  if(!slice){
    for(const [id,v] of Object.entries(defaultSettings)){if(id in (await page.evaluate(()=>MenuPrototype.definitions().settings)))await set(id,v)}
    const routes=(await state()).routes;result.routes=routes;
    const definitions=await page.evaluate(()=>MenuPrototype.definitions());
    fs.writeFileSync(path.join(out,'definitions.json'),JSON.stringify(definitions,null,2));
    for(const route of routes){
      for(const variant of route==='create'?['game','world','more']:['']){
        await prepare(route,variant);await page.waitForTimeout(100);
        const s=await state();check('Render '+route+(variant?'/'+variant:''),s.screen===route&&!s.errors.length,s.errors);
        let controls=await page.evaluate(()=>MenuPrototype.controls());
        result.controls.push(...controls);
        const indices=await page.locator('#screen button:not(:disabled)').count();
        for(let i=0;i<(skipActions?0:indices);i++){
          await prepare(route,variant);
          const buttons=page.locator('#screen button:not(:disabled)');
          if(i>=await buttons.count())continue;
          const control=await buttons.nth(i).evaluate(n=>({id:n.dataset.controlId,label:n.textContent||n.getAttribute('aria-label')}));
          await buttons.nth(i).evaluate(n=>n.click());
          await page.waitForTimeout(15);
          const next=await state();
          if(next.screen!==route)result.edges.push({from:route,variant,control:control.id,label:control.label,to:next.screen});
          check('Action '+control.id,!next.errors.length,next.errors);
        }
        await prepare(route,variant);
        if(route!=='main'){await page.keyboard.press('Escape');check('Escape return '+route,(await state()).screen==='main')}
      }
    }
    // Nested transactional edits through source buttons, rather than diagnostic setters.
    await prepare('create','more');await mouseButton('Game Rules');
    const boolRule=page.locator('[data-local^="rule."]').first();
    const ruleID=(await boolRule.getAttribute('data-local')).slice(5);
    const initialRule=(await state()).draft.rules[ruleID];
    await boolRule.click();await mouseButton('Cancel');
    check('Game Rules Cancel discards', (await state()).draft.rules[ruleID]===initialRule);
    await mouseButton('Game Rules');await page.locator('[data-local^="rule."]').first().click();await mouseButton('Done');
    check('Game Rules Done commits', (await state()).draft.rules[ruleID]!==initialRule);
    await mouseButton('Experiments');await page.locator('[data-local^="switch."]').first().click();await page.keyboard.press('Escape');
    check('Experiments Escape discards',Object.keys((await state()).draft.experiments).length===0);
    await mouseButton('Experiments');await page.locator('[data-local^="switch."]').first().click();await mouseButton('Done');
    check('Experiments Done commits',Object.values((await state()).draft.experiments).includes(true));
    await prepare('create','game');
    await page.locator('[data-local="draft.gameMode"]').click();
    check('Hardcore constraints',await page.locator('[data-local="draft.difficulty"]').isDisabled()&&await page.locator('[data-local="draft.commands"]').isDisabled()&&(await state()).draft.difficulty==='Hard'&&!(await state()).draft.commands);
    await prepare('options');const originalPacks=(await state()).packs;await mouseButton('Resource Packs...');
    await page.getByRole('option').first().click();await page.keyboard.press('Escape');
    check('Pack selection Escape discards',JSON.stringify((await state()).packs)===JSON.stringify(originalPacks));
    await mouseButton('Resource Packs...');await page.getByRole('option').first().click();await mouseButton('Done');
    check('Pack selection Done commits',JSON.stringify((await state()).packs)!==JSON.stringify(originalPacks));
    await prepare('language');await page.locator('[data-language="fr_fr"]').click();await mouseButton('Done');
    check('Language commits mock only',(await state()).language==='fr_fr'&&await page.evaluate(()=>document.documentElement.lang==='en-US'));
    await prepare('video');await set('textureFiltering','None');check('Anisotropy disabled dependency',await page.locator('[data-setting="maxAnisotropyBit"] input').isDisabled());await set('textureFiltering','Anisotropic');check('Anisotropy enabled dependency',await page.locator('[data-setting="maxAnisotropyBit"] input').isEnabled());
    await prepare('online');await set('friendsList',false);check('Friends dependency',await page.locator('[data-setting="allowFriendRequests"]').isDisabled());
    await prepare('keybinds');const binding=page.locator('[data-binding]').first();const bindID=await binding.getAttribute('data-binding');await binding.click();await page.keyboard.press('K');check('Mock key capture',(await state()).bindings[bindID]==='K');
    await prepare('video');const scroll=page.locator('.scroll').first();await scroll.hover();await page.mouse.wheel(0,500);await page.waitForTimeout(150);check('Wheel scrolls settings',await scroll.evaluate(n=>n.scrollTop)>0);
    const scaleBefore=(await state()).gui;await page.keyboard.down('Control');await page.mouse.wheel(0,-100);await page.keyboard.up('Control');check('Ctrl-wheel changes mock, not viewport scale',(await state()).gui.scale===scaleBefore.scale);
    await prepare('create','more');await mouseButton('Game Rules');const numeric=page.locator('#rule-list input').first();await numeric.fill('bad');check('Invalid rule blocks Done',await page.locator('#rules-done').isDisabled());await page.keyboard.press('Escape');await mouseButton('Game Rules');check('Canceled invalid rule clears error',await page.locator('#rules-done').isEnabled());await page.keyboard.press('Escape');
    await prepare('create','world');await page.locator('[data-local="draft.type"]').click();await mouseButton('Customize');check('Superflat expected route',(await state()).screen==='flat');const layers=(await state()).draft.flat.length;await mouseButton('Remove Layer');await mouseButton('Cancel');check('Superflat Cancel discards',(await state()).draft.flat.length===layers);await mouseButton('Customize');await mouseButton('Remove Layer');await mouseButton('Done');check('Superflat Done commits',(await state()).draft.flat.length===layers-1);
    await prepare('multiplayer');await mouseButton('Direct Connection');const address=page.locator('input').first();await address.fill('not valid/address');check('Invalid server address blocks Join',await page.locator('#direct-connect').isDisabled());await address.fill('play.example.invalid:25565');await mouseButton('Join Server');check('Server connection stops at mock endpoint',(await state()).screen==='message');await page.keyboard.press('Escape');await mouseButton('Cancel');
    await prepare('editworld');const worldName=(await state()).worlds[0].name;await page.locator('input').first().fill('Session Edit');await mouseButton('Cancel');check('World name Cancel discards',(await state()).worlds[0].name===worldName);await prepare('editworld');await page.locator('input').first().fill('Session Edit');await mouseButton('Save');check('World name Save is session-only',(await state()).worlds[0].name==='Session Edit');
    await prepare('language');await page.locator('input').first().fill('English');check('Language search filters sample list',await page.locator('[data-language]').count()<143&&await page.locator('[data-language]').count()>0);
    await prepare('quit');await mouseButton('Return to Title');check('Quit returns, process remains alive',(await state()).screen==='main');
    await prepare('linkconfirm');await mouseButton('Open in Browser');check('External action is reversible endpoint',(await state()).screen==='message'&&browser.contexts().flatMap(c=>c.pages()).length===1);
    await prepare('creditstext');await page.waitForTimeout(200);check('Credits bundled and loaded offline',await page.locator('#credits-content .text').count()>100);
    const errors=(await state()).errors;check('No uncaught errors in all route actions',errors.length===0,errors);
    check('No remote resource requests',result.requests.every(url=>url.startsWith('file:')||url.startsWith('data:')),result.requests.filter(url=>!url.startsWith('file:')&&!url.startsWith('data:')));
  }
  await page.reload();await page.waitForFunction(()=>window.MenuPrototype?.getState().assets.panoramaLoaded,null,{timeout:30000});
  const reset=await state();
  check('Reload discards all mock settings',JSON.stringify(reset.settings)===JSON.stringify(defaultSettings),reset.settings);
  check('Reload resets locale/sample state',reset.language==='en_us'&&reset.packs.length===0&&reset.worlds.length===2&&reset.servers.length===1);
  result.final=reset;result.finished=new Date().toISOString();
}catch(e){check('Harness completed',false,String(e));result.failure=String(e);}
finally{
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(result,null,2)+'\n');
  await browser.close();
}
const failed=result.checks.filter(x=>!x.pass);console.log(`${result.checks.length-failed.length}/${result.checks.length} passed; evidence ${out}`);
if(failed.length)process.exitCode=1;
