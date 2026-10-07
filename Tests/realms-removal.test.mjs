import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const html=fs.readFileSync(new URL('../Content/Menu/Web/index.html',import.meta.url),'utf8');
const lines=html.split('\n');
const renderer=name=>lines.find(line=>line.startsWith(`${name}(){`));
const data=JSON.parse(lines.find(line=>line.startsWith('const DATA = ')).slice(13).replace(/;$/,''));

test('Realms renderers, entry points, join form and owned CSS are removed',()=>{
  assert.equal(renderer('realms'),undefined);
  assert.equal(renderer('realmjoin'),undefined);
  assert.doesNotMatch(html,/go\('realms'\)|go\('realmjoin'\)|realm-join|\.realm-feature/);
});

test('retired Realms preference is absent while supported settings remain',()=>{
  assert.equal(Object.hasOwn(data.definitions,'realmsNotifications'),false);
  for(const id of ['fullscreen','framerateLimit','enableVsync','guiScale','menuBackgroundBlurriness'])
    assert.ok(Object.hasOwn(data.definitions,id),id);
});

test('main menu closes the removed row and retains real Quit activation',()=>{
  const buttons=[],icons=[];
  let quitRequests=0;
  const node=()=>({dataset:{},style:{}});
  const context={C:240,H:270,A:'',state:{settings:{skipWarning:false}},
    el:node,pos:n=>n,text(){},tr:k=>k,
    button:(label,x,y,width,action,disabled)=>{const n={...node(),label,x,y,width,action,disabled};buttons.push(n);return n;},
    icon:(name,label,x,y)=>icons.push({name,label,x,y}),
    MenuApplication:{enabled:()=>true,quit:()=>quitRequests++}};
  vm.runInNewContext(`({${renderer('main')}}).main()`,context);
  assert.deepEqual(buttons.map(b=>b.label),['menu.singleplayer','menu.multiplayer','menu.options','menu.quit']);
  const y=Math.floor(context.H/4)+48;
  assert.deepEqual(buttons.map(b=>b.y),[y,y+24,y+72,y+72]);
  assert.ok(icons.length===3&&icons.every(i=>i.y===y+48));
  assert.equal(buttons.at(-1).disabled,false);
  buttons.at(-1).action();
  assert.equal(quitRequests,1);
});

test('Online Settings retains friends and server controls without Realms',()=>{
  const settings=[],headings=[];
  const context={tr:k=>k,title(){},scroll:()=>({c:{},y:0}),
    heading:(key,_parent,y)=>{headings.push(key);return y+25;},
    listSettings:(ids,_parent,y)=>{settings.push(...ids);return y+100;},
    setting:id=>settings.push(id),button(){},finishList(){},done(){}};
  vm.runInNewContext(`({${renderer('online')}}).online()`,context);
  assert.deepEqual(settings,['friendsList','allowFriendRequests','inGameNotification','sharePresence','allowServerListing']);
  assert.deepEqual(headings,['options.online.friends.header','options.online.servers.header']);
});
