import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import {readCaptureEvidence} from './capture-evidence.mjs';

const dialogScript=fs.readFileSync(new URL('../MenuWeb/Runtime/MenuDialogRuntime.js',import.meta.url),'utf8');
test('numeric validation preserves invalid raw drafts including hidden rules',()=>{
  const rule={id:'limit',type:'Integer',min:0,max:100};
  const context=vm.createContext({DATA:{rules:[rule]},current:{data:{ruleText:{limit:'bad'}}},rulesEdit:{limit:10}});
  vm.runInContext(dialogScript,context);
  assert.equal(context.ruleDraftsValid(),false);
  for(const text of ['','1.5','1e2','101','-1','9007199254740992'])assert.equal(context.validRuleText(rule,text),false,text);
  context.current.data.ruleText.limit='25';
  assert.equal(context.ruleDraftsValid(),true);
  assert.equal(context.rulesEdit.limit,10);
});
test('server validation requires both trimmed name and valid address',()=>{
  const context=vm.createContext({editServer:{name:' ',address:'localhost'},validAddress:v=>v==='localhost'});
  vm.runInContext(dialogScript,context);
  assert.equal(context.validServerDraft(),false);
  context.editServer.name='Local';assert.equal(context.validServerDraft(),true);
  context.editServer.address='bad/address';assert.equal(context.validServerDraft(),false);
});
test('registry only reads explicitly selected matching capture evidence',()=>{
  const temp=fs.mkdtempSync(path.join(os.tmpdir(),'unnamed-evidence-'));
  try {
    for(const name of ['smoke','video','other'])fs.mkdirSync(path.join(temp,name));
    const write=(name,file,data)=>fs.writeFileSync(path.join(temp,name,file),JSON.stringify(data));
    const result={htmlSHA256:'same',profile:'1280x720',checks:[{pass:true}]};
    write('smoke','results.json',result);write('smoke','definitions.json',{});
    write('video','results.json',result);write('video','VideoResetControls.json',[{id:'selected'}]);
    write('other','results.json',{...result,htmlSHA256:'old'});
    fs.writeFileSync(path.join(temp,'VideoResetControls.json'),'invalid shared legacy file');
    assert.deepEqual(readCaptureEvidence(temp,'smoke','video').extras,[{id:'selected'}]);
    assert.throws(()=>readCaptureEvidence(temp,'smoke','other'),/matching/);
    assert.throws(()=>readCaptureEvidence(temp,'../smoke','video'),/Invalid/);
    write('video','results.json',{...result,checks:[{pass:false}]});
    assert.throws(()=>readCaptureEvidence(temp,'smoke','video'),/matching/);
  } finally {fs.rmSync(temp,{recursive:true,force:true});}
});
