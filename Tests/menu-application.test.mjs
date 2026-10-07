import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const script = fs.readFileSync(new URL('../MenuWeb/Runtime/MenuApplicationRuntime.js', import.meta.url), 'utf8');
const bridgeScript = fs.readFileSync(new URL('../MenuWeb/Runtime/MenuBridgeRuntime.js', import.meta.url), 'utf8');
function fixture({canQuit=true, reject=false, saveFailure=false}={}) {
  const calls=[], notices=[], intervals=[], button={disabled:true};
  let requested=false;
  const context=vm.createContext({
    window:{ue:{menuapplication:{
      getstate:async()=>JSON.stringify({ok:!requested||!saveFailure,canQuit,pendingQuit:false,
        quitIssued:requested&&!saveFailure,error:requested&&saveFailure?'Settings could not be saved.':''}),
      requestquit:async()=>{calls.push('quit');requested=!reject;return JSON.stringify({
        ok:!reject,canQuit,pendingQuit:!reject&&saveFailure,quitIssued:!reject&&!saveFailure,
        error:reject?'Standalone only.':''});}
    }}},
    screen:{querySelectorAll:()=>[button]},clone:structuredClone,state:{worlds:[]},current:{name:'main'},drawWorlds:()=>{},
    MenuVideo:{prepareForExit:async()=>{calls.push('drain');await new Promise(resolve=>setTimeout(resolve,5));calls.push('saved');},cancelExit:()=>calls.push('unlock')},
    message:(...args)=>notices.push(args),console,Date,clearTimeout,
    setTimeout:(fn,ms)=>setTimeout(fn,ms>=20000?60:ms>=5000?40:ms),
    setInterval:fn=>intervals.push(fn),
  });
  vm.runInContext(bridgeScript,context);
  vm.runInContext('MenuStores.register(MenuVideo)',context);
  vm.runInContext(script,context);
  return {api:context.window.MenuApplication,calls,notices,button,context,intervals};
}
test('quit drains settings once and prevents repeated submissions',async()=>{
  const f=fixture();await f.api.initialize();
  const closing=f.api.quit();await f.api.quit();await closing;
  assert.deepEqual(f.calls,['drain','saved','quit']);
  assert.equal(f.button.disabled,true);
  assert.equal(f.notices.length,0);
});
test('overall quit deadline includes an unanswered settings drain',async()=>{
  const f=fixture();await f.api.initialize();let finishDrain;
  f.context.MenuVideo.prepareForExit=()=>new Promise(resolve=>finishDrain=resolve);
  await f.api.quit();
  assert.match(f.notices.at(-1)[1],/Settings did not finish/);
  assert.equal(f.button.disabled,false);
  assert.deepEqual(f.calls,['unlock']);
  finishDrain();await Promise.resolve();await Promise.resolve();
  assert.deepEqual(f.calls,['unlock']);
});
test('unanswered quit request unlocks and ignores its late reply after retry',async()=>{
  const f=fixture();await f.api.initialize();let finishRequest;
  const request=f.context.window.ue.menuapplication.requestquit;
  f.context.window.ue.menuapplication.requestquit=()=>new Promise(resolve=>finishRequest=resolve);
  await f.api.quit();
  assert.match(f.notices.at(-1)[1],/did not respond/);
  assert.equal(f.button.disabled,false);
  f.context.window.ue.menuapplication.requestquit=request;
  await f.api.quit();
  assert.equal(f.api.getState().quitIssued,true);
  finishRequest(JSON.stringify({ok:true,canQuit:true,pendingQuit:false,quitIssued:false}));
  await Promise.resolve();await Promise.resolve();
  assert.equal(f.api.getState().quitIssued,true);
  assert.equal(f.button.disabled,true);
});
test('late pre-quit poll cannot replace the newer shutdown state',async()=>{
  const f=fixture();await f.api.initialize();const old=f.api.getState();let finishPoll;
  f.context.window.ue.menuapplication.getstate=()=>new Promise(resolve=>finishPoll=resolve);
  const polling=f.intervals[0]();await Promise.resolve();
  await f.api.quit();finishPoll(JSON.stringify(old));await polling;
  assert.equal(f.api.getState().quitIssued,true);
});
test('accepted but unanswered shutdown completion still permits an explicit retry',async()=>{
  const f=fixture();await f.api.initialize();
  f.context.window.ue.menuapplication.requestquit=async()=>JSON.stringify({ok:true,canQuit:true,pendingQuit:true,quitIssued:false});
  f.context.window.ue.menuapplication.getstate=()=>new Promise(()=>{});
  await f.api.quit();
  assert.match(f.notices.at(-1)[1],/did not respond/);
  assert.equal(f.api.getState().pendingQuit,true);
  assert.equal(f.button.disabled,false);
  assert.equal(f.calls.at(-1),'unlock');
  f.context.window.ue.menuapplication.requestquit=async()=>JSON.stringify({ok:true,canQuit:true,pendingQuit:false,quitIssued:true});
  await f.api.quit();
  assert.equal(f.api.getState().quitIssued,true);
  assert.equal(f.button.disabled,true);
});
test('editor quit remains disabled and never reaches native shutdown',async()=>{
  const f=fixture({canQuit:false});await f.api.initialize();await f.api.quit();
  assert.equal(f.button.disabled,true);
  assert.equal(f.calls.length,0);
});
test('native rejection unlocks settings and reports the failure',async()=>{
  const f=fixture({reject:true});await f.api.initialize();await f.api.quit();
  assert.deepEqual(f.calls,['drain','saved','quit','unlock']);
  assert.equal(f.notices[0][1],'Standalone only.');
  assert.equal(f.button.disabled,false);
});
test('failed final save cancels quit and keeps the interface recoverable',async()=>{
  const f=fixture({saveFailure:true});await f.api.initialize();await f.api.quit();
  assert.equal(f.calls.at(-1),'unlock');
  assert.equal(f.notices[0][1],'Settings could not be saved.');
  assert.equal(f.button.disabled,false);
});
