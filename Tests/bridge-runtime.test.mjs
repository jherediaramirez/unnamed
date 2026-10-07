import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const bridgeScript = fs.readFileSync(new URL('../MenuWeb/Runtime/MenuBridgeRuntime.js', import.meta.url), 'utf8');
function fixture(ue = {}) {
  const context = vm.createContext({window: {ue}, console, setTimeout: (fn, ms) => setTimeout(fn, ms >= 5000 ? 20 : ms), clearTimeout});
  vm.runInContext(bridgeScript, context);
  return context.window;
}

test('client finds CEF-lowercased methods, parses JSON and validates replies', async () => {
  const window = fixture({svc: {getstate: async () => JSON.stringify({ok: true}), broken: async () => '{"nope":1}'}});
  const client = window.MenuBridge.client('svc', {service: 'demo', unavailable: 'Demo is unavailable.', validate: reply => reply.ok === true});
  assert.equal(await client.connect(), true);
  assert.deepEqual({...await client.call('GetState')}, {ok: true});
  await assert.rejects(client.call('Broken'), /Invalid response from the demo service/);
  await assert.rejects(client.call('Missing'), /Demo is unavailable/);
});

test('client times out an unanswered request without waiting for it', async () => {
  const window = fixture({svc: {hang: () => new Promise(() => {})}});
  const client = window.MenuBridge.client('svc', {service: 'demo', unavailable: 'x'});
  await client.connect();
  await assert.rejects(client.call('Hang'), /The demo connection did not respond/);
});

test('stores drain together and release in registration order', async () => {
  const window = fixture();
  const calls = [];
  for (const name of ['video', 'input', 'preferences'])
    window.MenuStores.register({
      prepareForExit: async () => calls.push(name + ':locked'),
      cancelExit: () => calls.push(name + ':unlocked'),
      settled: async () => calls.push(name + ':settled')
    });
  await window.MenuStores.prepareForExit();
  window.MenuStores.cancelExit();
  await window.MenuStores.settled();
  assert.deepEqual(calls, ['video:locked', 'input:locked', 'preferences:locked', 'video:unlocked', 'input:unlocked',
    'preferences:unlocked', 'video:settled', 'input:settled', 'preferences:settled']);
});
