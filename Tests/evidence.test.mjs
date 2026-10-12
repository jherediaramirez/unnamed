import { strict as assert } from 'node:assert';
import { test } from 'node:test';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { freshEvidenceDir, testsRoot } from './evidence.mjs';

test('freshEvidenceDir makes a new folder and its parent', () => {
  const root = mkdtempSync(join(tmpdir(), 'evidence-fresh-'));
  try {
    const dir = join(root, 'day', 'Menu-1');
    assert.equal(freshEvidenceDir(dir), dir);
    assert.ok(statSync(dir).isDirectory());
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('freshEvidenceDir refuses a folder that exists, and leaves it as it was', () => {
  const root = mkdtempSync(join(tmpdir(), 'evidence-fresh-'));
  try {
    const dir = join(root, 'Menu-1');
    mkdirSync(dir);
    writeFileSync(join(dir, 'travel.1.json'), '{"old":true}');
    assert.throws(() => freshEvidenceDir(dir), /Use a fresh output folder/);
    assert.equal(readFileSync(join(dir, 'travel.1.json'), 'utf8'), '{"old":true}');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('both E2E drivers make their output folder through freshEvidenceDir before anything launches', () => {
  for (const name of ['create-world.mjs', 'foundation-menu.mjs']) {
    const text = readFileSync(join(testsRoot, name), 'utf8');
    const fresh = text.indexOf('freshEvidenceDir(output);');
    assert.ok(fresh > 0, `${name} calls freshEvidenceDir(output)`);
    assert.ok(fresh < text.indexOf('spawn('), `${name} makes the folder before its first launch`);
    assert.ok(!/mkdirSync\(\s*output/.test(text), `${name} does not make the output folder itself`);
  }
});
