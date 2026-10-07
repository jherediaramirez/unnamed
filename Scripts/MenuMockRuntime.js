let frozenFrame=null;
const failures=[];
window.addEventListener('error',e=>failures.push({message:e.message,source:e.filename,line:e.lineno}));
window.addEventListener('unhandledrejection',e=>failures.push({message:String(e.reason)}));
const staging=new Set(['create','language','packs','datapacks','experiments','rules','flat','presets','biomes','serveredit','editworld','direct']);
function beginStage(name){
  if(!staging.has(name))return;
  current.transaction={draft:clone(draft),language:state.language,lastAddress:state.lastAddress,packs:clone(state.packs),worlds:clone(state.worlds),servers:clone(state.servers),flatEdit:clone(flatEdit),flatBiomeEdit,biomeEdit,experimentsEdit:clone(experimentsEdit),rulesEdit:clone(rulesEdit),packDraft:clone(packDraft)};
}
function commitStage(){current.committed=true}
function rollbackStage(){
  const t=current.transaction;
  if(!t||current.committed)return;
  // A child dialog rolls back only its own staged domain, not unrelated shared settings.
  const domains={create:['draft'],language:['language'],packs:['packs','packDraft'],datapacks:['packDraft'],experiments:['experimentsEdit'],rules:['rulesEdit'],flat:['flatEdit','flatBiomeEdit'],presets:['flatEdit','flatBiomeEdit'],biomes:['biomeEdit'],serveredit:['servers'],editworld:['worlds'],direct:['lastAddress']};
  for(const key of domains[current.name]||[]){
    if(key==='draft')draft=clone(t.draft);
    else if(key==='flatEdit')flatEdit=clone(t.flatEdit);
    else if(key==='flatBiomeEdit')flatBiomeEdit=t.flatBiomeEdit;
    else if(key==='biomeEdit')biomeEdit=t.biomeEdit;
    else if(key==='experimentsEdit')experimentsEdit=clone(t.experimentsEdit);
    else if(key==='rulesEdit')rulesEdit=clone(t.rulesEdit);
    else if(key==='packDraft')packDraft=clone(t.packDraft);
    else state[key]=clone(t[key]);
  }
}
function annotateControls(){
  let ordinal=0;
  const variant=current.name==='create'?(current.data.tab||'game'):'';
  const counts=new Map();
  for(const node of screen.querySelectorAll('button,input,[role=option],[role=scrollbar]')){
    const settingID=node.dataset.setting||node.closest('[data-setting]')?.dataset.setting;
    const rule=node.getAttribute('aria-label')&&DATA.rules.find(r=>r.label===node.getAttribute('aria-label'));
    const parentRow=node.closest('[role=option]');
    let key=settingID||node.dataset.binding||node.dataset.local||node.dataset.language||node.dataset.world||node.dataset.server||node.dataset.preset||node.id||rule?.id;
    if(!key&&node.getAttribute('role')==='option')key=node.textContent.trim().split('\n')[0];
    if(!key)key=(parentRow?.textContent.trim().split('\n')[0]||'')+'.'+(node.getAttribute('aria-label')||node.textContent.trim()||node.type||'control')+'.'+ordinal;
    key=String(key).replace(/[^a-zA-Z0-9_.-]/g,'_');
    const repeated=counts.get(key)||0;counts.set(key,repeated+1);
    node.dataset.controlId=[current.name,variant,key,repeated||''].filter(Boolean).join('.');
    ordinal++;
  }
}
new MutationObserver(annotateControls).observe(screen,{childList:true,subtree:true});
const MenuDiagnostics={
  getState:()=>({screen:current.name,variant:current.data.tab||'',settings:clone(state.settings),language:state.language,bindings:clone(state.bindings),packs:clone(state.packs),gui:{width:W,height:H,scale:S,viewportWidth:innerWidth,viewportHeight:innerHeight},assets:{panoramaLoaded,panoramaFallback},worlds:clone(state.worlds),servers:clone(state.servers),draft:clone(draft),routes:Object.keys(renderers),presentation:clone(presentationBaseline),errors:clone(failures),depth:stack.length}),
  freezeFrame:(angle=0,phase=0)=>{frozenFrame={angle,phase};spin=angle;lastDraw=-1000},
  unfreeze:()=>{frozenFrame=null;lastFrame=performance.now()},
  go,back,setSetting,
  prepare:(name,variant='')=>{
    stack=[{name:'main',data:{}}];
    chosenWorld=state.worlds[0]?.id;chosenServer=state.servers[0]?.id;chosenLanguage=state.language;
    if(name==='create')draft=newDraft();
    if(['packs','datapacks'].includes(name))packDraft=name==='packs'?clone(state.packs):[];
    if(name==='rules')rulesEdit=clone(draft.rules);
    if(name==='experiments')experimentsEdit=clone(draft.experiments);
    if(['flat','presets'].includes(name)){flatEdit=clone(draft.flat);flatBiomeEdit=draft.flatBiome}
    if(name==='biomes')biomeEdit=draft.biome;
    if(name==='serveredit')editServer=clone(state.servers[0]);
    const data=name==='create'?{tab:variant||'game'}:name==='linkconfirm'?{url:'https://example.invalid/'}:name==='message'?{title:'Offline mock',body:'No service was invoked.'}:name==='confirm'?{title:'Confirm mock action',body:'Session-only sample data.',yes:'Proceed',no:'Cancel',onYes:()=>{}}:{};
    current={name,data};beginStage(name);render();
  },
  definitions:()=>clone({settings:D,bindings:DATA.bindings,rules:DATA.rules,languages:DATA.languages,labels:L}),
  controls:()=>{
    annotateControls();
    return [...screen.querySelectorAll('[data-control-id]')].map(n=>{
      const id=n.dataset.setting||n.closest('[data-setting]')?.dataset.setting;
      return {screen:current.name,variant:current.data.tab||'',id:n.dataset.controlId,label:n.getAttribute('aria-label')||n.getAttribute('aria-valuetext')||n.textContent.trim(),type:n.getAttribute('role')||n.type||'button',setting:id||null,binding:n.dataset.binding||null,local:n.dataset.local||null,value:n.value??n.getAttribute('aria-checked')??n.getAttribute('aria-selected')??null,disabled:!!n.disabled,default:id?D[id].default:null,range:id?{min:D[id].min,max:D[id].max,step:D[id].step,choices:D[id].values}:null,tooltip:n.dataset.tooltip||n.closest('[data-tooltip]')?.dataset.tooltip||null};
    });
  }
};
window.MenuPrototype=MenuDiagnostics;
console.info('UnnamedMenu: offline mock ready; baseline English/Auto; no engine bridge');
