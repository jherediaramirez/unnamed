import { strict as assert } from 'node:assert';
import { test } from 'node:test';
import { execFileSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { auditRepository, findCredentialKinds } from '../Scripts/TestRepositorySafety.mjs';

const scan = text => findCredentialKinds(Buffer.from(text));

test('allows empty and explicitly redacted Android tokens', () => {
  for (const value of ['', 'REDACTED_FOR_REVIEW', 'REMOVED_LOCAL_ONLY_TOKEN']) {
    assert.deepEqual(scan(`SecurityToken=${value}\r\n`), []);
  }
});

test('rejects generated or auto-generating Android tokens without revealing them', () => {
  for (const value of ['[AUTO]', 'test-only-not-a-live-credential']) {
    const findings = scan(`SecurityToken=${value}\r\n`);
    assert.deepEqual(findings, ['Android File Server token']);
    assert.ok(!JSON.stringify(findings).includes(value));
  }
});

test('recognizes current GitHub token formats', () => {
  assert.deepEqual(scan('gh' + 'p_' + 'x'.repeat(36)), ['GitHub credential']);
  assert.deepEqual(scan('github' + '_pat_' + 'x'.repeat(82)), ['GitHub credential']);
});

test('recognizes service credentials and private keys', () => {
  assert.deepEqual(scan('sk-' + 'proj-' + 'x'.repeat(48)), ['OpenAI credential']);
  assert.deepEqual(scan('AK' + 'IA' + 'A'.repeat(16)), ['AWS access key']);
  assert.deepEqual(scan('AI' + 'za' + 'x'.repeat(35)), ['Google API key']);
  assert.deepEqual(scan('-----BEGIN ' + 'OPENSSH PRIVATE KEY-----'), ['Private key']);
});

test('does not mistake hashes, regex source, or normal configuration for secrets', () => {
  assert.deepEqual(scan('sha256=' + 'a'.repeat(64) + '\nConnectionType=USBOnly\n'), []);
  assert.deepEqual(scan(readRegexExample()), []);
});

function readRegexExample() {
  return String.raw`const replacement = /^SecurityToken=[^\r\n]*/gm;`;
}

test('checks index and old reachable history, not only the current working file', () => {
  const runtime = join(dirname(fileURLToPath(import.meta.url)), 'Runtime');
  mkdirSync(runtime, { recursive: true });
  const root = mkdtempSync(join(runtime, 'safety-test-'));
  const git = (...args) => execFileSync('git', ['-C', root, ...args], { windowsHide: true });
  try {
    git('init', '--quiet');
    git('config', 'user.name', 'Repository Safety Test');
    git('config', 'user.email', 'test@example.invalid');
    git('config', 'core.autocrlf', 'false');
    const config = join(root, 'engine.ini');
    writeFileSync(config, 'SecurityToken=test-only-not-a-live-credential\n');
    git('add', 'engine.ini');
    writeFileSync(config, 'SecurityToken=\n');
    assert.equal(auditRepository(root, 'worktree').problems.length, 0);
    assert.equal(auditRepository(root, 'staged').problems.length, 1);
    git('commit', '--quiet', '-m', 'Synthetic credential fixture');
    git('add', 'engine.ini');
    git('commit', '--quiet', '-m', 'Sanitized current file fixture');
    assert.equal(auditRepository(root, 'staged').problems.length, 0);
    assert.equal(auditRepository(root, 'history').problems.length, 1);
    assert.throws(() => auditRepository(root, 'unsupported'));
  } finally {
    if (dirname(root) !== runtime) throw new Error('Unsafe test cleanup target.');
    rmSync(root, { recursive: true, force: true });
  }
});
