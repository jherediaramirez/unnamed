import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const html = fs.readFileSync(new URL('../Content/Menu/Web/index.html', import.meta.url), 'utf8');
const renderer = html.split('\n').find(line => line.startsWith('video(){'));
function controls(debugMenuToolsEnabled) {
  const buttons = [];
  const node = () => ({dataset:{},setAttribute(){}});
  const context = {
    debugMenuToolsEnabled,W:480,
    title(){},text:node,tr:key=>key,pos:n=>n,el:node,tip(){},setting(){},
    scroll:()=>({c:node(),y:0}),heading:(_label,_parent,y)=>y+25,
    listSettings:(ids,_parent,y)=>y+Math.ceil(ids.length/2)*25,
    finishList(){},done(){},
    button:(label,x,y,width)=>{const b={...node(),label,x,y,width};buttons.push(b);return b;}
  };
  vm.runInNewContext(`({${renderer}}).video()`,context);
  return buttons;
}
test('temporary reset uses a centered main-menu-sized button', () => {
  const buttons = controls(true);
  assert.equal(buttons.length,1);
  assert.equal(buttons[0].label,'Hard Reset');
  assert.equal(buttons[0].x,55);
  assert.equal(buttons[0].width,200);
  assert.equal(buttons[0].dataset.debugControl,'hard-reset');
});
test('excluding debug tools removes the reset from video settings', () => {
  assert.equal(controls(false).length,0);
});
