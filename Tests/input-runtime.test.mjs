import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const script=fs.readFileSync(new URL('../MenuWeb/Runtime/MenuInputRuntime.js',import.meta.url),'utf8');
const bridgeScript=fs.readFileSync(new URL('../MenuWeb/Runtime/MenuBridgeRuntime.js',import.meta.url),'utf8');
function fixture() {
  let native={ok:true,ready:true,persistent:true,error:'',bindings:{'key.forward':'W','key.jump':'Space'},
    nativeKeys:{'key.forward':'W','key.jump':'SpaceBar'},defaults:{'key.forward':'W','key.jump':'Space'},
    supported:['key.forward','key.jump'],policies:{toggleSprint:false,toggleCrouch:false}};
  const calls=[],notices=[],window={addEventListener(){},ue:{menuinput:{
    getstate:async()=>structuredClone(native),
    applybinding:async(id,key)=>{calls.push([id,key]);if(key==='SpaceBar')return {...native,ok:false,error:'Conflict'};
      native.bindings[id]=key==='None'?'Not Bound':key;native.nativeKeys[id]=key;return structuredClone(native);},
    applypolicy:async(id,value)=>{native.policies[id]=value;return structuredClone(native);},
    resetbindings:async()=>{native.bindings={...native.defaults};return structuredClone(native);},
    resetbinding:async id=>{native.bindings[id]=native.defaults[id];return structuredClone(native);},
  }}};
  window.ue.menuinput.applycapturedkey=async (...args)=>window.ue.menuinput.applybinding(...args);
  const context=vm.createContext({window,clone:structuredClone,state:{bindings:{},settings:{}},
    DATA:{bindings:[{id:'key.forward',default:'W'},{id:'key.jump',default:'Space'}]},current:{name:'keybinds'},
    bindingCapture:null,render:()=>{},refreshSettings:()=>{},message:(...args)=>notices.push(args),console,Date,
    document:{addEventListener(){}},
    setTimeout:(fn,ms)=>setTimeout(fn,ms>=5000?25:ms),clearTimeout});
  vm.runInContext(bridgeScript,context);vm.runInContext(script,context);
  return {api:window.MenuInput,bridge:window.ue.menuinput,context,calls,notices};
}
const keyEvent=(key,code,repeat=false)=>({key,code,repeat,preventDefault(){},stopImmediatePropagation(){}});
test('physical key codes distinguish modifier sides and keyboard punctuation',async()=>{
  const f=fixture();await f.api.initialize();
  assert.equal(f.api.keyFromEvent(keyEvent('Control','ControlRight')),'RightControl');
  assert.equal(f.api.keyFromEvent(keyEvent('Shift','ShiftLeft')),'LeftShift');
  assert.equal(f.api.keyFromEvent(keyEvent('z','KeyW')),'W');
  assert.equal(f.api.keyFromEvent(keyEvent('?','Slash')),'Slash');
  assert.equal(f.api.keyFromEvent(keyEvent('1','Digit1')),'One');
});
test('Unreal Windows CEF events without scan codes still map native key identities',async()=>{
  const f=fixture();await f.api.initialize();
  const cef=(key,vk,location=0)=>({...keyEvent(key,''),keyCode:vk,which:vk,location});
  assert.equal(f.api.keyFromEvent(cef('c',67)),'C');
  assert.equal(f.api.keyFromEvent(cef('z',87)),'W');
  assert.equal(f.api.keyFromEvent(cef('ArrowUp',38)),'Up');
  assert.equal(f.api.keyFromEvent(cef('Shift',16,1)),'LeftShift');
  assert.equal(f.api.keyFromEvent(cef('Shift',16,2)),'RightShift');
  assert.equal(f.api.keyFromEvent(cef('Control',17,2)),'RightControl');
  assert.equal(f.api.keyFromEvent(cef('Unidentified',0)),null);
  f.api.startCapture('key.forward');f.api.captureKey(cef('k',75));await f.api.settled();
  assert.deepEqual(f.calls,[['key.forward','K']]);
});
test('Esc cancels, repeats do not toggle, and Backspace explicitly unbinds',async()=>{
  const f=fixture();await f.api.initialize();f.api.startCapture('key.forward');
  f.api.captureKey(keyEvent('Escape','Escape'));assert.deepEqual(f.calls,[]);
  f.api.startCapture('key.forward');f.api.captureKey(keyEvent('K','KeyK',true));assert.deepEqual(f.calls,[]);
  f.api.captureKey(keyEvent('Backspace','Backspace'));await f.api.settled();
  assert.deepEqual(f.calls,[['key.forward','None']]);assert.equal(f.context.state.bindings['key.forward'],'Not Bound');
});
test('native conflict leaves the previous binding authoritative',async()=>{
  const f=fixture();await f.api.initialize();f.api.startCapture('key.forward');
  f.api.captureKey(keyEvent(' ','Space'));await f.api.settled().catch(()=>{});
  assert.equal(f.context.state.bindings['key.forward'],'W');assert.equal(f.notices.at(-1)[1],'Conflict');
});
test('unanswered request unlocks for a fresh read and ignores the late reply',async()=>{
  const f=fixture();await f.api.initialize();let finish;
  f.bridge.applybinding=()=>new Promise(resolve=>finish=resolve);
  f.api.startCapture('key.forward');f.api.captureKey(keyEvent('K','KeyK'));
  await f.api.settled().catch(()=>{});
  assert.equal(f.api.enabled('key.forward'),true);assert.equal(f.context.state.bindings['key.forward'],'W');
  finish({...f.api.getState(),bindings:{'key.forward':'K'}});await Promise.resolve();await Promise.resolve();
  assert.equal(f.context.state.bindings['key.forward'],'W');
});
test('quit drains an in-flight edit and locks capture until cancelled',async()=>{
  const f=fixture();await f.api.initialize();let finish;
  const apply=f.bridge.applybinding;f.bridge.applybinding=(...args)=>new Promise(resolve=>finish=()=>apply(...args).then(resolve));
  f.api.startCapture('key.forward');f.api.captureKey(keyEvent('K','KeyK'));await Promise.resolve();
  const draining=f.api.prepareForExit();assert.equal(f.api.enabled('key.forward'),false);
  f.api.startCapture('key.jump');assert.equal(f.context.bindingCapture,null);
  finish();await draining;f.api.cancelExit();assert.equal(f.api.enabled('key.forward'),true);
  assert.equal(f.context.state.bindings['key.forward'],'K');
});
test('capture hooks: the native side hears capture start before keys and every way it ends',async()=>{
  const f=fixture();await f.api.initialize();const events=[];let open;
  f.api.setCaptureHooks({beforeStart:id=>new Promise(resolve=>{events.push('start:'+id);open=resolve;}),afterEnd:()=>events.push('end')});
  const starting=f.api.startCapture('key.forward');
  assert.equal(f.context.bindingCapture,null,'capture waits for the native side');
  open();await starting;assert.equal(f.context.bindingCapture,'key.forward');
  f.api.captureKey(keyEvent('K','KeyK',true));assert.deepEqual(events,['start:key.forward'],'a repeat keeps capturing');
  f.api.captureKey(keyEvent('Escape','Escape'));assert.deepEqual(events,['start:key.forward','end']);
  f.context.bindingCapture='key.jump';f.api.capturePointer({button:4,preventDefault(){},stopImmediatePropagation(){}});
  await f.api.settled();assert.deepEqual(events,['start:key.forward','end','end']);
  assert.deepEqual(f.calls.at(-1),['key.jump','ThumbMouseButton2']);
});