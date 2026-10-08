import {chromium} from 'playwright';
import {evidencePath} from './evidence.mjs';
import {PNG} from 'pngjs';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import crypto from 'node:crypto';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);
const port=Number(args.find(x=>x.startsWith('--port='))?.split('=')[1]||18888);
const mode=args.find(x=>x.startsWith('--mode='))?.split('=')[1]||'packaged';
const profile=args.find(x=>x.startsWith('--profile='))?.split('=')[1]||'1920x1080';
const slice=args.includes('--slice');
const skipActions=args.includes('--skip-actions');
if(!/^[A-Za-z0-9_-]+$/.test(mode)||!/^\d+x\d+$/.test(profile))throw Error('Invalid capture arguments');
const [width,height]=profile.split('x').map(Number);
const out=evidencePath('Captures',mode+'-'+profile+(slice?'-slice':skipActions?'-targeted':''));
if(fs.existsSync(out))throw Error('Capture exists; choose a fresh mode to preserve evidence.');
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
  result.htmlSHA256=crypto.createHash('sha256').update(fs.readFileSync(fileURLToPath(page.url()))).digest('hex');
  page.on('console',m=>result.console.push({type:m.type(),text:m.text()}));
  page.on('pageerror',e=>result.console.push({type:'error',text:String(e)}));
  page.on('request',r=>result.requests.push(r.url()));
  await page.reload();
  await page.waitForFunction(()=>window.MenuPrototype?.getState().assets.panoramaLoaded||window.MenuPrototype?.getState().assets.panoramaFallback,null,{timeout:30000});
  await page.waitForFunction(()=>window.MenuVideo?.getState()&&window.MenuPreferences?.getState()?.ready&&window.MenuInput?.getState()?.ready,null,{timeout:15000});
  const functionalQuit=await page.evaluate(()=>!!window.MenuApplication);
  const functionalWorld=await page.evaluate(()=>typeof window.MenuApplication?.worldEnabled==='function');
  if(functionalQuit)await page.waitForFunction(()=>window.MenuApplication?.enabled(),null,{timeout:15000});
  if(functionalQuit){
    if(await page.evaluate(()=>MenuVideo.getState().persistent))throw Error('Use a -SessionOnly diagnostic launch for menu regression tests.');
    await page.evaluate(()=>MenuVideo.hardReset()).catch(error=>{if(!/context.*destroyed/i.test(String(error)))throw error;});
    await page.waitForFunction(()=>MenuApplication?.enabled()&&MenuVideo?.getState()?.settings.guiScale===0&&MenuPreferences?.getState()?.ready&&MenuInput?.getState()?.ready&&MenuPrototype.getState().screen==='main');
  }
  await page.evaluate(()=>MenuPrototype.freezeFrame(0,0));
  const state=()=>page.evaluate(()=>MenuPrototype.getState());
  const prepare=(name,variant='')=>page.evaluate(([n,v])=>MenuPrototype.prepare(n,v),[name,variant]);
  const settle=async()=>{
    await page.evaluate(()=>Promise.all([MenuPreferences.settled(),MenuVideo.settled(),MenuInput.settled()]));
    await page.waitForFunction(()=>!MenuPreferences.isCommitting());
  };
  const mouseButton=async(label)=>{await page.getByRole('button',{name:label,exact:true}).click();await settle();};
  const value=async(id)=>page.evaluate(id=>MenuPrototype.getState().settings[id],id);
  const set=async(id,v)=>page.evaluate(([id,v])=>MenuPrototype.setSetting(id,v),[id,v]);
  const boot=await state();
  if(mode==='no-realms-packaged'||mode.startsWith('review-')){
    check('Realms and Join Realm routes are removed',!boot.routes.includes('realms')&&!boot.routes.includes('realmjoin')&&boot.routes.length===33,boot.routes);
    check('No main-menu Realms button remains',await page.getByRole('button',{name:/Realms/i}).count()===0);
    check('No retired Realms preference remains',await page.evaluate(()=>!('realmsNotifications' in MenuPrototype.definitions().settings)&&!('realmsNotifications' in MenuPrototype.getState().settings)));
  }
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
  await page.evaluate(()=>MenuVideo.settled());
  check('Fullscreen changes native window only',await value('fullscreen')===!defaultSettings.fullscreen&&await page.evaluate(()=>!document.fullscreenElement&&MenuVideo.getState().observed.windowMode==='Borderless'));
  await page.keyboard.press('F11');await page.evaluate(()=>MenuVideo.settled());check('F11 restores native window',await value('fullscreen')===defaultSettings.fullscreen);
  await prepare('options');
  const slider=page.locator('[data-setting="fov"] input');await slider.focus();await page.keyboard.press('End');
  await settle();check('Keyboard slider saves desired FOV',await value('fov')===110);
  await set('guiScale',1);await set('forceUnicodeFont',true);await set('highContrast',true);await set('menuBackgroundBlurriness',0);await set('panoramaSpeed',0);await set('framerateLimit',10);await set('sound_master',0);await set('narrator','Narrates All');
  check('Presentation baseline is immutable',JSON.stringify((await state()).presentation)===JSON.stringify(baseline));
  check('GUI scale functional; mock font/contrast/locale unchanged',await page.evaluate(()=>!document.querySelector('#gui').classList.contains('uniform-font')&&!document.querySelector('#gui').classList.contains('high-contrast')&&document.documentElement.lang==='en-US')&&(await state()).gui.scale===1);
  if(!slice){
    const owned=await page.evaluate(()=>[...Object.keys(MenuPreferences.getState().settings),...Object.keys(MenuVideo.getState().settings),...Object.keys(MenuInput.getState().policies)]);
    for(const [id,v] of Object.entries(defaultSettings)){if(owned.includes(id))await set(id,v)}
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
          if(functionalQuit&&control.label==='Quit Game'){
            check('Quit is a real application action (verified separately)',await page.evaluate(()=>MenuApplication.enabled()&&!MenuPrototype.getState().routes.includes('quit')));
            continue;
          }
          if(functionalWorld&&route==='worlds'&&control.label==='Play Selected World'){
            check('Playground is a real map action (verified separately)',await page.evaluate(()=>MenuApplication.worldEnabled('playground')));
            continue;
          }
          await buttons.nth(i).evaluate(n=>n.click());
          await settle();
          const next=await state();
          result.controls.push(...await page.evaluate(context=>MenuPrototype.controls().map(c=>({...c,context})),route+' action '+control.label));
          if(next.screen!==route)result.edges.push({from:route,variant,control:control.id,label:control.label,to:next.screen});
          check('Action '+control.id,!next.errors.length,next.errors);
        }
        await prepare(route,variant);
        if(route!=='main'){await page.keyboard.press('Escape');check('Escape return '+route,(await state()).screen==='main')}
      }
    }
    // Contextual controls are owned by this capture, never shared legacy observations.
    const recordControls=async context=>result.controls.push(...await page.evaluate(
      context=>MenuPrototype.controls().map(c=>({...c,context})),context));
    const friendsBefore=await value('friendsList');
    await prepare('online');await set('friendsList',false);
    await page.locator('[data-setting=friendsList]').click();
    await recordControls('Friends enablement confirmation');
    await mouseButton('Turn OFF');
    check('Friends declined enablement keeps OFF',await value('friendsList')===false);
    await page.locator('[data-setting=friendsList]').click();await mouseButton('Turn ON');
    const friendsEnabled=await value('friendsList');
    await page.locator('[data-setting=friendsList]').click();await settle();
    check('Friends enable then disable returns OFF',friendsEnabled&&await value('friendsList')===false);
    await set('friendsList',friendsBefore);
    for(const [route,collection] of [['worlds','worlds'],['multiplayer','servers']]){
      await prepare(route);const count=(await state())[collection].length;
      await mouseButton('Delete');await recordControls(route+' delete confirmation');
      await mouseButton('Delete');await recordControls(route+' restore entry');
      check(route+' sample deletion is session-only',(await state())[collection].length===count-1);
      await mouseButton('Restore Entry');
      check(route+' sample restore restores count',(await state())[collection].length===count);
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
    await mouseButton('Resource Packs...');await page.getByRole('option').first().click();
    result.controls.push(...await page.evaluate(()=>MenuPrototype.controls().map(c=>({...c,context:'selected resource pack'}))));
    await page.getByRole('option').first().click();
    result.controls.push(...await page.evaluate(()=>MenuPrototype.controls().map(c=>({...c,context:'ordered resource packs'}))));
    await mouseButton('Done');
    check('Pack selection Done commits',JSON.stringify((await state()).packs)!==JSON.stringify(originalPacks));
    await prepare('language');await page.locator('[data-language="fr_fr"]').click();await mouseButton('Done');
    check('Language choice saves without applying translation',(await state()).language==='fr_fr'&&await page.evaluate(()=>document.documentElement.lang==='en-US'));
    await prepare('video');await set('textureFiltering','None');check('Anisotropy disabled dependency',await page.locator('[data-setting="maxAnisotropyBit"] input').isDisabled());await set('textureFiltering','Anisotropic');check('Anisotropy enabled dependency',await page.locator('[data-setting="maxAnisotropyBit"] input').isEnabled());
    await prepare('online');await set('friendsList',false);check('Friends dependency',await page.locator('[data-setting="allowFriendRequests"]').isDisabled());
    await prepare('keybinds');const binding=page.locator('[data-binding]').first();const bindID=await binding.getAttribute('data-binding');await binding.click();await page.keyboard.press('K');await page.evaluate(()=>MenuInput.settled());check('Native key capture',(await state()).bindings[bindID]==='K');await page.evaluate(()=>MenuInput.resetAll());
    await set('guiScale',0);await page.evaluate(()=>MenuVideo.settled());await prepare('video');const scroll=page.locator('.scroll').first();const scrollPoint=await scroll.evaluate(n=>{const r=n.getBoundingClientRect();return {x:r.x+20,y:r.y+20}});await page.mouse.move(scrollPoint.x,scrollPoint.y);await page.mouse.wheel(0,500);await page.waitForTimeout(150);check('Wheel scrolls settings',await scroll.evaluate(n=>n.scrollTop)>0);
    const scaleBefore=(await state()).gui;await page.keyboard.down('Control');await page.mouse.wheel(0,-100);await page.keyboard.up('Control');await page.evaluate(()=>MenuVideo.settled());check('Ctrl-wheel changes actual GUI scale without changing window dimensions',(await state()).gui.scale===Math.min(scaleBefore.scale+1,Math.floor(scaleBefore.viewportHeight/240))&&(await state()).gui.viewportWidth===scaleBefore.viewportWidth);
    await prepare('create','more');await mouseButton('Game Rules');const numeric=page.locator('#rule-list input').first();await numeric.fill('bad');check('Invalid rule blocks Done',await page.locator('#rules-done').isDisabled());await page.keyboard.press('Escape');await mouseButton('Game Rules');check('Canceled invalid rule clears error',await page.locator('#rules-done').isEnabled());await page.keyboard.press('Escape');
    await prepare('create','world');await page.locator('[data-local="draft.type"]').click();await mouseButton('Customize');check('Superflat expected route',(await state()).screen==='flat');const layers=(await state()).draft.flat.length;await mouseButton('Remove Layer');await mouseButton('Cancel');check('Superflat Cancel discards',(await state()).draft.flat.length===layers);await mouseButton('Customize');await mouseButton('Remove Layer');await mouseButton('Done');check('Superflat Done commits',(await state()).draft.flat.length===layers-1);
    await prepare('multiplayer');await mouseButton('Direct Connection');const address=page.locator('input').first();await address.fill('not valid/address');check('Invalid server address blocks Join',await page.locator('#direct-connect').isDisabled());await address.fill('play.example.invalid:25565');await mouseButton('Join Server');check('Server connection stops at mock endpoint',(await state()).screen==='message');await page.keyboard.press('Escape');await mouseButton('Cancel');
    await prepare('editworld');const referenceWorld=()=>state().then(s=>s.worlds.find(w=>w.id==='fictional-forest'));const worldName=(await referenceWorld()).name;await page.locator('input').first().fill('Session Edit');await mouseButton('Cancel');check('World name Cancel discards',(await referenceWorld()).name===worldName);await prepare('editworld');await page.locator('input').first().fill('Session Edit');await mouseButton('Save');check('World name Save is session-only',(await referenceWorld()).name==='Session Edit');
    await prepare('language');await page.locator('input').first().fill('English');check('Language search filters sample list',await page.locator('[data-language]').count()<143&&await page.locator('[data-language]').count()>0);
    if(functionalQuit)check('Mock quit route replaced by normal shutdown',!(await state()).routes.includes('quit'));
    else{await prepare('quit');await mouseButton('Return to Title');check('Quit returns, process remains alive',(await state()).screen==='main');}
    await prepare('linkconfirm');await mouseButton('Open in Browser');check('External action is reversible endpoint',(await state()).screen==='message'&&browser.contexts().flatMap(c=>c.pages()).length===1);
    await prepare('creditstext');await page.waitForTimeout(200);check('Credits bundled and loaded offline',await page.locator('#credits-content .text').count()>100);
    const errors=(await state()).errors;check('No uncaught errors in all route actions',errors.length===0,errors);
    check('No remote resource requests',result.requests.every(url=>url.startsWith('file:')||url.startsWith('data:')),result.requests.filter(url=>!url.startsWith('file:')&&!url.startsWith('data:')));
  }
  const nativeBeforeReload=await page.evaluate(()=>MenuVideo.getState().settings);
  const inputBeforeReload=await page.evaluate(()=>MenuInput.getState().policies);
  await settle();
  const preferencesBeforeReload=await page.evaluate(()=>MenuPreferences.getState());
  await page.reload();await page.waitForFunction(()=>window.MenuPrototype?.getState().assets.panoramaLoaded&&window.MenuVideo?.getState()&&window.MenuInput?.getState()?.ready&&window.MenuPreferences?.getState()?.ready,null,{timeout:30000});
  const reset=await state();
  check('Reload preserves all desired preferences',Object.entries(preferencesBeforeReload.settings).every(([id,value])=>reset.settings[id]===value));
  check('Reload preserves the five functional settings',Object.keys(nativeBeforeReload).every(id=>reset.settings[id]===nativeBeforeReload[id]));
  check('Reload preserves native Hold/Toggle input policies',Object.keys(inputBeforeReload).every(id=>reset.settings[id]===inputBeforeReload[id]));
  check('Reload preserves locale and pack preferences',reset.language===preferencesBeforeReload.language&&JSON.stringify(reset.packs)===JSON.stringify(preferencesBeforeReload.packs));
  check('Reload retains warning and telemetry choices',reset.settings.skipWarning===preferencesBeforeReload.skipWarning&&reset.settings.telemetryOptInExtra===preferencesBeforeReload.telemetryOptInExtra);
  check('Reload restores the built-in world catalog and transient reference entries',reset.worlds.length===(functionalWorld?3:2)&&(!functionalWorld||reset.worlds[0].id==='playground'&&reset.worlds[0].name==='Playground')&&reset.servers.length===1);
  result.final=reset;result.finished=new Date().toISOString();
}catch(e){check('Harness completed',false,String(e));result.failure=String(e);}
finally{
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(result,null,2)+'\n');
  await browser.close();
}
const failed=result.checks.filter(x=>!x.pass);console.log(`${result.checks.length-failed.length}/${result.checks.length} passed; evidence ${out}`);
if(failed.length)process.exitCode=1;
