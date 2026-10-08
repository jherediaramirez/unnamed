// Native menu preferences (MenuPreferences): 101 saved options, language, pack order and
// two flags. Language and packs commit on Done; navigation waits for a running commit.
export const name = 'Preferences';

const liveInFoundation = "['sensitivity','invertXMouse','invertYMouse','mouseWheelSensitivity','discreteMouseScroll','fov','fovEffectScale','autoJump','sprintWindow','toggleAttack','toggleUse']";

export function apply(patch) {
  patch.edit('function refreshSettings()', 'function refreshSettings(){', 'function refreshSettings(){window.MenuPreferences?.refreshAdditional();');
  patch.edit('function back()', 'function back(){', 'function back(){if(window.MenuPreferences?.isCommitting())return;');
  patch.edit('function go(', 'function go(name,data={}){', 'function go(name,data={}){if(window.MenuPreferences?.isCommitting())return;');
  // Sound Device is informational; it is neither a preference nor a native setting.
  patch.edit('function activeSetting(', 'function activeSetting(id){', "function activeSetting(id){if(id==='soundDevice')return false;");

  // Keep the reference tooltip and wrap it with the preference's save/effect status.
  patch.edit('function settingTooltip(', 'function settingTooltip(id){', 'function referenceSettingTooltip(id){');
  patch.edit('function settingTooltip(', "return d.tip?(DATA.tipKeys[d.tip]?tr(DATA.tipKeys[d.tip]):d.tip):''}",
    "return d.tip?(DATA.tipKeys[d.tip]?tr(DATA.tipKeys[d.tip]):d.tip):''}\n" +
    `function settingTooltip(id){const detail=referenceSettingTooltip(id);const live=${liveInFoundation}.includes(id);` +
    "return live?'Applies in the Playground; saved automatically.'+(detail?'\\n\\n'+detail:''):preferenceSettings.has(id)?MenuPreferences.tooltip()+(detail?'\\n\\n'+detail:''):detail}");

  patch.line('async function applyLanguage(', "async function applyLanguage(code,close=false){if(!DATA.languages[code])throw Error('Unknown locale');return MenuPreferences.commit({language:code},()=>{chosenLanguage=state.language;activeLabels=L;document.documentElement.lang='en-US';commitStage();if(close)back();else render()})}");
  patch.text("button(tr('gui.done'),C+4,H-26,150,()=>{void applyLanguage(chosenLanguage,true)})",
    "button(tr('gui.done'),C+4,H-26,150,()=>{void applyLanguage(chosenLanguage,true)}).dataset.preferenceCommit='language'");
  patch.text("if(!data){state.packs=clone(packDraft);state.settings.highContrast=packDraft.includes('high_contrast');persist();applyAppearance()}back()})",
    "if(data){commitStage();back()}else void MenuPreferences.commit({packs:clone(packDraft)},()=>{applyAppearance();commitStage();back()})}).dataset.preferenceCommit=data?'':'packs'");
  patch.text("state.settings.skipWarning=v;persist()", "void MenuPreferences.setFlag('skipWarning',v)");
  patch.text("ch.setAttribute('aria-label',tr('multiplayerWarning.check'))", "ch.dataset.preferenceFlag='skipWarning';ch.setAttribute('aria-label',tr('multiplayerWarning.check'))");
  patch.text("state.settings.telemetryOptInExtra=v;persist();rememberScroll();render()", "void MenuPreferences.setFlag('telemetryOptInExtra',v).then(()=>{if(current.name==='telemetry'){rememberScroll();render()}}).catch(()=>{})");
  patch.text("consent.setAttribute('aria-label',tr('telemetry_info.opt_in.description'))", "consent.dataset.preferenceFlag='telemetryOptInExtra';consent.setAttribute('aria-label',tr('telemetry_info.opt_in.description'))");
  patch.text("text(tr('options.languageAccuracyWarning'),", "text('Language choice is remembered; translations are planned.',");
  patch.text("text(tr('pack.dropInfo'),", "text(data?tr('pack.dropInfo'):'Pack order is remembered; appearance changes are planned.',");
}
