import { strict as assert } from 'node:assert';
import { test } from 'node:test';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const runChecks = join(dirname(fileURLToPath(import.meta.url)), '..', 'Scripts', 'RunChecks.ps1');

// Runs PowerShell with RunChecks.ps1's KeepCrashes function taken from its syntax tree (the script itself never runs).
const withKeepCrashes = body => {
  const script = `$ErrorActionPreference='Stop'
$ast=[System.Management.Automation.Language.Parser]::ParseFile($env:RUNCHECKS,[ref]$null,[ref]$null)
$function=$ast.Find({param($Node) $Node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $Node.Name -eq 'KeepCrashes'},$true)
if(-not $function){throw 'RunChecks.ps1 has no KeepCrashes'}
. ([scriptblock]::Create($function.Extent.Text))
${body}`;
  return execFileSync('pwsh', ['-NoProfile', '-NonInteractive', '-Command', script], {
    encoding: 'utf8',
    env: { ...process.env, RUNCHECKS: runChecks },
  }).trim();
};

test('a crash folder in the native step profile survives the profile deletion', () => {
  const root = mkdtempSync(join(tmpdir(), 'runchecks-keepcrashes-'));
  try {
    const userDir = join(root, 'UnnamedChecksProfile-fake');
    const suite = join(root, 'Native-fake');
    const crash = join(userDir, 'Saved', 'Crashes', 'UECC-Windows-FAKE_0000');
    mkdirSync(crash, { recursive: true });
    mkdirSync(suite);
    writeFileSync(join(crash, 'CrashContext.runtime-xml'), 'fake crash context');
    // The native step's order: keep the crash folders, then delete the profile.
    const kept = withKeepCrashes(
      `$kept=KeepCrashes '${userDir}' '${suite}'; Remove-Item -LiteralPath '${userDir}' -Recurse -Force; $kept`,
    );
    assert.equal(kept, '1');
    assert.equal(existsSync(userDir), false);
    const survived = join(suite, 'Crashes', 'UECC-Windows-FAKE_0000', 'CrashContext.runtime-xml');
    assert.equal(readFileSync(survived, 'utf8'), 'fake crash context');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('a profile without crashes keeps nothing', () => {
  const root = mkdtempSync(join(tmpdir(), 'runchecks-keepcrashes-'));
  try {
    const userDir = join(root, 'profile');
    mkdirSync(join(userDir, 'Saved', 'Logs'), { recursive: true });
    assert.equal(withKeepCrashes(`KeepCrashes '${userDir}' '${root}'`), '0');
    assert.equal(existsSync(join(root, 'Crashes')), false);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('a failed copy warns and keeps nothing instead of failing the step', () => {
  const root = mkdtempSync(join(tmpdir(), 'runchecks-keepcrashes-'));
  try {
    const userDir = join(root, 'profile');
    mkdirSync(join(userDir, 'Saved', 'Crashes', 'UECC-Windows-FAKE_0001'), { recursive: true });
    // A file where the Crashes folder would go makes the copy fail.
    writeFileSync(join(root, 'Crashes'), 'in the way');
    assert.equal(withKeepCrashes(`KeepCrashes '${userDir}' '${root}' 3>$null`), '0');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('the native step keeps the crash folders before it deletes the profile, and says how many', () => {
  const text = readFileSync(runChecks, 'utf8');
  const keep = text.indexOf('$dumps=KeepCrashes $userDir $dir');
  const remove = text.indexOf('Remove-Item -LiteralPath $userDir');
  assert.ok(keep > 0, 'the native step calls KeepCrashes');
  assert.ok(remove > keep, 'KeepCrashes runs before the profile is deleted');
  assert.ok(text.includes('crash folders kept $dumps'), 'the CRASH line reports the kept folders');
});
