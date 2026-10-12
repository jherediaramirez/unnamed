import { strict as assert } from 'node:assert';
import { test } from 'node:test';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const scripts = join(dirname(fileURLToPath(import.meta.url)), '..', 'Scripts');
const runChecks = join(scripts, 'RunChecks.ps1');

// Runs PowerShell with Scripts/CrashFolders.ps1 dot-sourced, as the runners do.
const withCrashFolders = body => {
  const script = `$ErrorActionPreference='Stop'
. (Join-Path $env:SCRIPTS 'CrashFolders.ps1')
${body}`;
  return execFileSync('pwsh', ['-NoProfile', '-NonInteractive', '-Command', script], {
    encoding: 'utf8',
    env: { ...process.env, SCRIPTS: scripts },
  }).trim();
};
const close = (userDir, destination) =>
  withCrashFolders(`$c=Close-Profile -UserDir '${userDir}' -Destination '${destination}' 3>$null; "$($c.Crashes)|$($c.Kept)"`);

test('a crash folder in a profile survives the profile deletion', () => {
  const root = mkdtempSync(join(tmpdir(), 'runchecks-crashfolders-'));
  try {
    const userDir = join(root, 'UnnamedChecksProfile-fake');
    const suite = join(root, 'Native-fake');
    const crash = join(userDir, 'Saved', 'Crashes', 'UECC-Windows-FAKE_0000');
    mkdirSync(join(crash, 'Logs'), { recursive: true });
    mkdirSync(suite);
    writeFileSync(join(crash, 'CrashContext.runtime-xml'), 'fake crash context');
    writeFileSync(join(crash, 'Logs', 'Unnamed.log'), 'fake log');
    assert.equal(close(userDir, suite), '1|');
    assert.equal(existsSync(userDir), false);
    const survived = join(suite, 'Crashes', 'UECC-Windows-FAKE_0000');
    assert.equal(readFileSync(join(survived, 'CrashContext.runtime-xml'), 'utf8'), 'fake crash context');
    assert.equal(readFileSync(join(survived, 'Logs', 'Unnamed.log'), 'utf8'), 'fake log');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('a profile without crashes keeps nothing and is deleted', () => {
  const root = mkdtempSync(join(tmpdir(), 'runchecks-crashfolders-'));
  try {
    const userDir = join(root, 'profile');
    mkdirSync(join(userDir, 'Saved', 'Logs'), { recursive: true });
    assert.equal(close(userDir, root), '0|');
    assert.equal(existsSync(join(root, 'Crashes')), false);
    assert.equal(existsSync(userDir), false);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('a failed copy keeps the profile, names it, and does not throw', () => {
  const root = mkdtempSync(join(tmpdir(), 'runchecks-crashfolders-'));
  try {
    const userDir = join(root, 'profile');
    const crash = join(userDir, 'Saved', 'Crashes', 'UECC-Windows-FAKE_0001');
    mkdirSync(crash, { recursive: true });
    writeFileSync(join(crash, 'UEMinidump.dmp'), 'fake dump');
    // A file where the Crashes folder would go makes the copy fail.
    writeFileSync(join(root, 'Crashes'), 'in the way');
    assert.equal(close(userDir, root), `0|${userDir}`);
    assert.equal(readFileSync(join(crash, 'UEMinidump.dmp'), 'utf8'), 'fake dump');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('a copy that does not arrive whole keeps the profile', () => {
  const root = mkdtempSync(join(tmpdir(), 'runchecks-crashfolders-'));
  try {
    const userDir = join(root, 'profile');
    const crash = join(userDir, 'Saved', 'Crashes', 'UECC-Windows-FAKE_0002');
    mkdirSync(crash, { recursive: true });
    writeFileSync(join(crash, 'UEMinidump.dmp'), 'fake dump');
    // Copy-Item succeeds but leaves every copied file empty, as a partial copy would.
    const shortCopy = `function Copy-Item { [CmdletBinding()] param([string]$LiteralPath, [string]$Destination, [switch]$Recurse, [switch]$Force)
  Microsoft.PowerShell.Management\\Copy-Item -LiteralPath $LiteralPath -Destination $Destination -Recurse -Force
  Get-ChildItem -LiteralPath $Destination -File -Recurse | ForEach-Object { [IO.File]::WriteAllText($_.FullName, '') } }`;
    const kept = withCrashFolders(
      `${shortCopy}\n$c=Close-Profile -UserDir '${userDir}' -Destination '${root}' 3>$null; "$($c.Crashes)|$($c.Kept)"`,
    );
    assert.equal(kept, `0|${userDir}`);
    assert.equal(readFileSync(join(crash, 'UEMinidump.dmp'), 'utf8'), 'fake dump');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

// The text between a step's start and the next step's start.
const stepText = (text, name) => {
  const start = text.indexOf(`Step '${name}'`);
  assert.ok(start > 0, `RunChecks has a ${name} step`);
  const next = text.indexOf("Step '", start + 1);
  return text.slice(start, next > 0 ? next : undefined);
};

test('every runner that deletes an editor profile closes it through Close-Profile', () => {
  const text = readFileSync(runChecks, 'utf8');
  assert.ok(text.includes(". (Join-Path $PSScriptRoot 'CrashFolders.ps1')"), 'RunChecks dot-sources CrashFolders.ps1');
  assert.ok(!/function\s+KeepCrashes/.test(text), 'the old KeepCrashes is gone');
  const native = stepText(text, 'native');
  assert.ok(native.includes('$closed=Close-Profile -UserDir $userDir -Destination $dir'), 'the native step closes its profile');
  assert.ok(native.includes('crash folders kept $($closed.Crashes)'), 'the CRASH line reports the kept folders');
  assert.ok(native.includes("profile kept at '+$closed.Kept"), 'the CRASH line names a kept profile');
  const hashes = stepText(text, 'hashes');
  assert.ok(hashes.includes('$closed=Close-Profile -UserDir $userDir -Destination $dir'), 'the radius-8 editor closes its profile');
  assert.ok(hashes.includes('profile kept at $($closed.Kept)'), 'the radius-8 line names a kept profile');
  assert.ok(!/Remove-Item\s+-LiteralPath\s+\$userDir/.test(text), 'RunChecks deletes no profile by hand');
  const voxel = readFileSync(join(scripts, 'TestVoxel.ps1'), 'utf8');
  assert.ok(voxel.includes(". (Join-Path $PSScriptRoot 'CrashFolders.ps1')"), 'TestVoxel dot-sources CrashFolders.ps1');
  assert.ok(voxel.includes('$closed=Close-Profile -UserDir $profileDirectory -Destination $OutputDirectory'), 'TestVoxel closes its profile');
  assert.ok(!/Remove-Item\s+-LiteralPath\s+\$profileDirectory/.test(voxel), 'TestVoxel deletes no profile by hand');
  assert.ok(voxel.indexOf('Close-Profile -UserDir') < voxel.indexOf('Missing voxel report'), 'TestVoxel closes the profile before it can throw');
});
