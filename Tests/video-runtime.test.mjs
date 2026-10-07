import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const script = fs.readFileSync(new URL('../Scripts/MenuVideoRuntime.js', import.meta.url), 'utf8');
const bridgeScript = fs.readFileSync(new URL('../Scripts/MenuBridgeRuntime.js', import.meta.url), 'utf8');
function fixture({native = true} = {}) {
  const calls = [], notices = [], intervals = [];
  const saved = {fullscreen:false, framerateLimit:120, enableVsync:true, guiScale:0, menuBackgroundBlurriness:5};
  const snapshot = () => ({ok:true, settings:{...saved}, capabilities:Object.fromEntries(Object.keys(saved).map(id => [id, native || ['guiScale','menuBackgroundBlurriness'].includes(id)])), observed:{}, pendingFullscreen:false});
  const context = vm.createContext({
    window:{ue:{menuvideo:{
      getstate:async () => JSON.stringify(snapshot()),
      applysetting:async (id, json) => {
        await new Promise(resolve => setTimeout(resolve, 2));
        calls.push([id, JSON.parse(json).value]);
        saved[id] = JSON.parse(json).value;
        return JSON.stringify(snapshot());
      }
    }}},
    state:{settings:{...saved}}, D:{
      fullscreen:{values:[false,true]}, enableVsync:{values:[false,true]},
      framerateLimit:{min:10,max:260,step:10}, guiScale:{min:0,max:4,step:1},
      menuBackgroundBlurriness:{min:0,max:10,step:1}
    },
    liveVideoSettings:undefined, quality:new Set(['menuBackgroundBlurriness']),
    clone:structuredClone, maximumScale:() => 4,
    applyAppearance:() => {}, resize:() => {}, refreshSettings:() => {},
    message:(...args) => notices.push(args), console,
    setTimeout:(fn,ms)=>setTimeout(fn,ms>=5000?40:ms), clearTimeout,
    setInterval:fn => intervals.push(fn), Date, Map, Set, Promise
  });
  vm.runInContext(bridgeScript, context);
  vm.runInContext(script, context);
  return {api:context.window.MenuVideo, context, calls, notices, saved, intervals};
}
test('five settings use one ordered, coalescing native path', async () => {
  const f = fixture();
  await f.api.initialize();
  f.api.set('framerateLimit', 30);
  f.api.set('framerateLimit', 60);
  f.api.set('framerateLimit', 90);
  f.api.set('guiScale', 2);
  f.api.set('menuBackgroundBlurriness', 0);
  await f.api.settled();
  assert.deepEqual(f.calls, [['framerateLimit',30],['framerateLimit',90],['guiScale',2],['menuBackgroundBlurriness',0]]);
  assert.equal(f.context.state.settings.framerateLimit, 90);
  assert.equal(f.context.state.settings.guiScale, 2);
  assert.equal(f.saved.menuBackgroundBlurriness, 0);
  assert.equal(f.notices.length, 0);
});
test('failed earlier edit retains a newer queued choice', async () => {
  const f=fixture();await f.api.initialize();
  let rejectFirst;
  f.context.window.ue.menuvideo.applysetting=(id,json)=>{
    const value=JSON.parse(json).value;f.calls.push([id,value]);
    if(value===30)return new Promise((_,reject)=>rejectFirst=reject);
    f.saved[id]=value;return Promise.resolve(JSON.stringify({...f.api.getState(),settings:{...f.saved}}));
  };
  f.api.set('framerateLimit',30);await Promise.resolve();
  f.api.set('framerateLimit',90);rejectFirst(Error('First edit failed'));
  await f.api.settled();
  assert.deepEqual(f.calls,[['framerateLimit',30],['framerateLimit',90]]);
  assert.equal(f.saved.framerateLimit,90);
  assert.equal(f.context.state.settings.framerateLimit,90);
});
test('unanswered settings request times out and later edits still complete',async()=>{
  const f=fixture();await f.api.initialize();
  const apply=f.context.window.ue.menuvideo.applysetting;
  let lateReply;
  f.context.window.ue.menuvideo.applysetting=()=>new Promise(resolve=>lateReply=resolve);
  await f.api.set('framerateLimit',30);
  assert.match(f.notices.at(-1)[1],/did not respond/);
  assert.equal(f.context.state.settings.framerateLimit,120);
  f.context.window.ue.menuvideo.applysetting=apply;
  await f.api.set('framerateLimit',90);
  lateReply(JSON.stringify({...f.api.getState(),settings:{...f.saved,framerateLimit:30}}));
  await Promise.resolve();await Promise.resolve();
  assert.equal(f.context.state.settings.framerateLimit,90);
});
test('poll started before an edit cannot overwrite its completed result',async()=>{
  const f=fixture();await f.api.initialize();
  const old=f.api.getState();let finishPoll;
  f.context.window.ue.menuvideo.getstate=()=>new Promise(resolve=>finishPoll=resolve);
  const polling=f.intervals[0]();await Promise.resolve();
  await f.api.set('framerateLimit',90);
  finishPoll(JSON.stringify(old));await polling;
  assert.equal(f.context.state.settings.framerateLimit,90);
  assert.equal(f.api.getState().settings.framerateLimit,90);
});
test('malformed and unsupported values never cross the bridge', async () => {
  const f = fixture(); await f.api.initialize();
  for (const [id,value] of [['fullscreen',1],['enableVsync','yes'],['framerateLimit',25],['framerateLimit',NaN],['guiScale',5],['menuBackgroundBlurriness',11],['renderDistance',12]])
    assert.throws(() => f.api.set(id,value));
  assert.equal(f.calls.length, 0);
});
test('editor capabilities protect global engine settings', async () => {
  const f = fixture({native:false}); await f.api.initialize();
  await f.api.set('fullscreen',true); await f.api.set('framerateLimit',30);
  await f.api.set('guiScale',1);
  assert.deepEqual(f.calls,[['guiScale',1]]);
  assert.equal(f.context.state.settings.fullscreen,false);
});
test('rejected changes restore actual values and surface the failure', async () => {
  const f = fixture(); await f.api.initialize();
  f.context.window.ue.menuvideo.applysetting = async () => JSON.stringify({...f.api.getState(),ok:false,error:'Overridden by configuration'});
  await f.api.set('enableVsync',false);
  assert.equal(f.context.state.settings.enableVsync,true);
  assert.equal(f.notices[0][1],'Overridden by configuration');
});
test('exit locks new edits while completing already queued settings', async () => {
  const f=fixture();await f.api.initialize();
  f.api.set('framerateLimit',30);f.api.set('framerateLimit',60);
  const exiting=f.api.prepareForExit();
  await f.api.set('framerateLimit',90);await exiting;
  assert.equal(f.saved.framerateLimit,60);
  assert.equal(f.api.enabled('framerateLimit'),false);
  f.api.cancelExit();await f.api.set('framerateLimit',120);
  assert.equal(f.saved.framerateLimit,120);
});
