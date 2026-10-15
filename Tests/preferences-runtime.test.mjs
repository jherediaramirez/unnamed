import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const schema = JSON.parse(fs.readFileSync(new URL('../Content/Menu/Web/preferences-schema.menuprefs',import.meta.url)));
const script = fs.readFileSync(new URL('../MenuWeb/Runtime/MenuPreferencesRuntime.js',import.meta.url),'utf8');
const bridgeScript = fs.readFileSync(new URL('../MenuWeb/Runtime/MenuBridgeRuntime.js',import.meta.url),'utf8');
function fixture() {
  const defaults = () => ({settings:Object.fromEntries(Object.entries(schema.settings).map(([id,d])=>[id,d.default])),
    language:'en_us',packs:[],skipWarning:false,telemetryOptInExtra:false});
  let saved = defaults();
  const snapshot = () => ({...structuredClone(saved),ok:true,ready:true,editable:true,persistent:true,error:'',revision:0});
  const calls=[],notices=[],intervals=[],listeners={};
  const native = {
    getstate:async()=>JSON.stringify(snapshot()),
    applychanges:async json=>{
      const patch=JSON.parse(json);calls.push(patch);
      await new Promise(resolve=>setTimeout(resolve,2));
      if(patch.settings)Object.assign(saved.settings,patch.settings);
      for(const key of ['language','packs','skipWarning','telemetryOptInExtra'])if(Object.hasOwn(patch,key))saved[key]=patch[key];
      return JSON.stringify(snapshot());
    },
    resetpreferences:async()=>{saved=defaults();return JSON.stringify(snapshot());}
  };
  const context=vm.createContext({
    PREFERENCE_SCHEMA:schema,window:{ue:{menupreferences:native}},state:{...defaults(),settings:{...saved.settings,guiScale:3,toggleSprint:true}},
    chosenLanguage:'en_us',screen:{querySelectorAll:()=>[]},
    document:{addEventListener:(type,fn)=>listeners[type]=fn},
    refreshSettings:()=>{},message:(...args)=>notices.push(args),console,
    setTimeout:(fn,ms)=>setTimeout(fn,ms>=5000?40:ms),clearTimeout,setInterval:fn=>intervals.push(fn)
  });
  vm.runInContext(bridgeScript,context);
  vm.runInContext(script,context);
  return {api:context.window.MenuPreferences,context,native,calls,notices,intervals,listeners,snapshot};
}

test('102 preference definitions exclude settings with other native owners',()=>{
  assert.equal(Object.keys(schema.settings).length,102);
  for(const id of ['fullscreen','framerateLimit','enableVsync','guiScale','menuBackgroundBlurriness','toggleSprint','toggleCrouch','preferredGraphicsBackend','soundDevice','rotateWithMinecart'])
    assert.equal(id in schema.settings,false,id);
  for(const preset of Object.values(schema.presets))for(const id of Object.keys(preset))assert.ok(id in schema.settings);
});

test('ordered coalescing saves latest slider values and preserves other native owners',async()=>{
  const f=fixture();await f.api.initialize();
  f.api.set('fov',80);f.api.set('fov',90);f.api.set('fov',100);f.api.set('sound_master',0);
  await f.api.settled();
  assert.deepEqual(f.calls,[{settings:{fov:80}},{settings:{fov:100}},{settings:{sound_master:0}}]);
  assert.equal(f.context.state.settings.fov,100);
  assert.equal(f.context.state.settings.sound_master,0);
  assert.equal(f.context.state.settings.guiScale,3);
  assert.equal(f.context.state.settings.toggleSprint,true);
});

test('failed earlier save rolls back while retaining a newer queued choice',async()=>{
  const f=fixture();await f.api.initialize();
  const apply=f.native.applychanges;let reject;
  f.native.applychanges=json=>JSON.parse(json).settings.fov===80?new Promise((_,no)=>reject=no):apply(json);
  const first=f.api.set('fov',80);await Promise.resolve();const latest=f.api.set('fov',90);
  reject(Error('Disk unavailable'));await assert.rejects(first,/Disk unavailable/);await latest;
  assert.equal(f.context.state.settings.fov,90);
  assert.equal(f.notices.length,1);
});

test('stale idle read cannot undo a completed edit',async()=>{
  const f=fixture();await f.api.initialize();
  const old=f.snapshot();let finish;
  f.native.getstate=()=>new Promise(resolve=>finish=resolve);
  const poll=f.intervals[0]();await Promise.resolve();
  await f.api.set('fov',95);finish(JSON.stringify(old));await poll;
  assert.equal(f.api.getState().settings.fov,95);
});

test('invalid and reserved values never reach native bridge',async()=>{
  const f=fixture();await f.api.initialize();
  for(const [id,value] of [['fov',29],['fov',70.5],['fov','90'],['fov',NaN],['highContrast',1],['fullscreen',true]])
    assert.throws(()=>f.api.set(id,value));
  assert.throws(()=>f.api.setFlag('skipWarning',1));
  assert.equal(f.calls.length,0);
});

test('Done waits for saving and freezes dialog input; failure keeps dialog open',async()=>{
  const f=fixture();await f.api.initialize();
  let reply,closed=false;
  f.native.applychanges=()=>new Promise(resolve=>reply=resolve);
  const commit=f.api.commit({language:'fr_fr'},()=>closed=true);await Promise.resolve();
  assert.equal(closed,false);assert.equal(f.api.isCommitting(),true);
  let prevented=false,stopped=false;
  f.listeners.click({preventDefault:()=>prevented=true,stopImmediatePropagation:()=>stopped=true});
  assert.ok(prevented&&stopped);
  reply(JSON.stringify({...f.snapshot(),ok:false,error:'Save failed'}));await commit;
  assert.equal(closed,false);assert.equal(f.api.available(),true);
  assert.equal(f.context.state.language,'en_us');assert.equal(f.notices.length,1);
  f.native.applychanges=async()=>JSON.stringify({...f.snapshot(),language:'fr_fr'});
  await f.api.commit({language:'fr_fr'},()=>closed=true);
  assert.equal(closed,true);assert.equal(f.context.state.language,'fr_fr');
});

test('Quit drains accepted choices and reset explicitly restores defaults',async()=>{
  const f=fixture();await f.api.initialize();
  f.api.set('fov',84);f.api.setFlag('skipWarning',true);
  const quit=f.api.prepareForExit();await f.api.set('fov',100);await quit;
  assert.equal(f.api.getState().settings.fov,84);assert.equal(f.api.getState().skipWarning,true);
  assert.equal(f.api.available(),false);f.api.cancelExit();
  await f.api.reset();
  assert.equal(f.api.getState().settings.fov,110);assert.equal(f.api.getState().skipWarning,false);
});

test('New players start at FOV 110, the slider maximum (Preferences patch)',()=>{
  assert.deepEqual(schema.settings.fov,{default:110,min:30,max:110,step:1});
  const page=fs.readFileSync(new URL('../Content/Menu/Web/index.html',import.meta.url),'utf8');
  assert.match(page,/"fov":\{"key":"options\.fov","label":"FOV","kind":"slider","min":30,"max":110,"step":1,"default":110,/);
});

test('timed out request does not stop later saves or accept a late reply',async()=>{
  const f=fixture();await f.api.initialize();
  const apply=f.native.applychanges;let late;
  f.native.applychanges=()=>new Promise(resolve=>late=resolve);
  await assert.rejects(f.api.set('fov',80),/did not respond/);
  f.native.applychanges=apply;await f.api.set('fov',100);
  late(JSON.stringify({...f.snapshot(),settings:{...f.snapshot().settings,fov:80}}));
  await Promise.resolve();await Promise.resolve();
  assert.equal(f.context.state.settings.fov,100);
});
