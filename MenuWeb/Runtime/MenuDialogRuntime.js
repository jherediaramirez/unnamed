function validServerDraft() {
  return !!editServer.name.trim() && validAddress(editServer.address);
}
function refreshServerValidation() {
  const save = $('#server-save');
  if (save) save.disabled = !validServerDraft();
}
function validRuleText(rule, text) {
  const number = Number(text);
  return /^-?\d+$/.test(text) && Number.isSafeInteger(number) && number >= rule.min && number <= rule.max;
}
function ruleDraftsValid() {
  return DATA.rules.every(rule => rule.type === 'Boolean' ||
    validRuleText(rule, current.data.ruleText?.[rule.id] ?? String(rulesEdit[rule.id])));
}
function refreshRuleValidation() {
  const done = $('#rules-done');
  if (done) done.disabled = !ruleDraftsValid();
}
function drawRules() {
  const parent = $('#rule-list');
  if (!parent) return;
  parent.replaceChildren();
  const raw = current.data.ruleText ||= {};
  let y = 2, category = '';
  for (const rule of DATA.rules.slice().sort((a, b) => a.category.localeCompare(b.category) || a.id.localeCompare(b.id))) {
    if (searches.rules && !(rule.label + ' ' + rule.id).toLowerCase().includes(searches.rules.toLowerCase())) continue;
    if (rule.category !== category) {
      category = rule.category;
      y = heading(tr('gamerule.category.' + category), parent, y, 8);
    }
    const label = text(tr(rule.localizationFallback || 'gamerule.minecraft.' + rule.id), 0, y + 6, 230, '', parent);
    label.style.whiteSpace = 'normal';
    tip(label, (rule.tip ? rule.tip + '\n' : '') + 'minecraft:' + rule.id + '\n' + tr('editGamerule.default', rule.default));
    if (rule.type === 'Boolean') {
      localCycle('rule.' + rule.id, '', rulesEdit[rule.id], [false, true], 240, y, 70,
        value => rulesEdit[rule.id] = value, false, parent).textContent = tr(rulesEdit[rule.id] ? 'options.on' : 'options.off');
    } else {
      const value = raw[rule.id] ?? String(rulesEdit[rule.id]);
      const input = field(value, 240, y, 70, (value, input) => {
        raw[rule.id] = value;
        const valid = validRuleText(rule, value);
        input.classList.toggle('invalid', !valid);
        if (valid) rulesEdit[rule.id] = Number(value);
        refreshRuleValidation();
      }, {label: rule.label, parent});
      input.classList.toggle('invalid', !validRuleText(rule, value));
    }
    y += 30;
  }
  finishList(parent, y);
  refreshRuleValidation();
}
