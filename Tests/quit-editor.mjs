import {chromium} from 'playwright';
import fs from 'node:fs';
import assert from 'node:assert/strict';

const port=Number(process.argv.find(arg=>arg.startsWith('--port='))?.split('=')[1]||18890);
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${port}`);
const checks=[];
const check=(name,pass,detail)=>{checks.push({name,pass:!!pass,detail});console.log((pass?'PASS ':'FAIL ')+name);assert.ok(pass,name);};
try{
  const page=browser.contexts().flatMap(context=>context.pages()).find(target=>target.url().includes('/Menu/Web/index.html'));
  assert.ok(page,'Start the editor Play window before running this safety check.');
  await page.waitForFunction(()=>window.MenuApplication?.getState());
  check('Editor Play has no quit capability',!(await page.evaluate(()=>MenuApplication.getState())).canQuit);
  check('Editor Play Quit Game is disabled',await page.getByRole('button',{name:'Quit Game',exact:true}).isDisabled());
  const reply=await page.evaluate(async()=>JSON.parse(await window.ue.menuapp.requestquit()));
  check('Direct native quit request is rejected in editor Play',!reply.ok&&!reply.pendingQuit&&!reply.quitIssued,reply);
  await page.waitForTimeout(1000);
  check('Editor Play remains responsive after rejected quit',await page.evaluate(()=>MenuPrototype.getState().screen==='main'));
}finally{
  fs.writeFileSync(new URL('./QuitEditorSafetyResults.json',import.meta.url),JSON.stringify({finished:new Date().toISOString(),checks},null,2)+'\n');
  await browser.close();
}
