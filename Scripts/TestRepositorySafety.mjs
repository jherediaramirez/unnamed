import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const allowedAndroidTokens = new Set(['', 'REDACTED_FOR_REVIEW', 'REMOVED_LOCAL_ONLY_TOKEN']);
const credentialPatterns = [
  ['GitHub credential', /\b(?:gh[pousr]_[A-Za-z0-9]{36,255}|github_pat_[A-Za-z0-9_]{60,255})\b/],
  ['OpenAI credential', /\bsk-(?:(?:proj|svcacct)-)?[A-Za-z0-9_-]{40,}/],
  ['AWS access key', /\b(?:AKIA|ASIA)[A-Z0-9]{16}\b/],
  ['Google API key', /\bAIza[A-Za-z0-9_-]{35}\b/],
  ['Private key', /-----BEGIN (?:RSA |EC |DSA |OPENSSH |ENCRYPTED )?PRIVATE KEY-----/],
];

export function findCredentialKinds(bytes) {
  const text = bytes.toString('utf8');
  const kinds = credentialPatterns.filter(([, pattern]) => pattern.test(text)).map(([kind]) => kind);
  for (const match of text.matchAll(/^\s*SecurityToken\s*=([^\r\n]*)/gm)) {
    if (!allowedAndroidTokens.has(match[1].trim())) {
      kinds.push('Android File Server token');
      break;
    }
  }
  return kinds;
}

function git(root, args, options = {}) {
  return execFileSync('git', ['-C', root, ...args], {
    windowsHide: true, maxBuffer: 1024 * 1024 * 1024, ...options,
  });
}

export function auditRepository(root, mode) {
  const problems = [];
  let checked = 0;
  const inspect = (name, bytes) => {
    checked++;
    for (const kind of findCredentialKinds(bytes)) problems.push({ name, kind });
  };
  if (mode === 'worktree') {
    const paths = git(root, ['ls-files', '--cached', '--others', '--exclude-standard', '-z'])
      .toString('utf8').split('\0').filter(Boolean);
    for (const name of new Set(paths)) inspect(name, readFileSync(resolve(root, name)));
  } else {
    const objects = new Map();
    if (mode === 'staged') {
      const entries = git(root, ['ls-files', '--stage', '-z']).toString('utf8').split('\0').filter(Boolean);
      for (const entry of entries) {
        const match = /^(\d+) ([a-f0-9]+) (\d)\t([\s\S]+)$/.exec(entry);
        if (!match || match[3] !== '0') throw new Error('Resolve index conflicts before backing up.');
        if (match[1] !== '160000') objects.set(match[2], match[4]);
      }
    } else if (mode === 'history') {
      const entries = git(root, ['rev-list', '--objects', '--all']).toString('utf8').trim().split('\n');
      for (const entry of entries.filter(Boolean)) {
        const space = entry.indexOf(' ');
        const oid = space < 0 ? entry : entry.slice(0, space);
        objects.set(oid, space < 0 ? oid : entry.slice(space + 1));
      }
    } else {
      throw new Error('Use --worktree, --staged, or --history.');
    }
    // Git supplies object boundaries and lengths; never interpret binary blobs as delimiters.
    const output = git(root, ['cat-file', '--batch'], { input: [...objects.keys()].join('\n') + '\n' });
    let offset = 0;
    for (const [oid, name] of objects) {
      const end = output.indexOf(10, offset);
      const header = output.subarray(offset, end).toString('ascii').split(' ');
      const length = Number(header[2]);
      if (end < 0 || header[0] !== oid || !Number.isSafeInteger(length) || length < 0) {
        throw new Error('Git object read failed; backup audit is incomplete.');
      }
      offset = end + 1;
      if (offset + length >= output.length || output[offset + length] !== 10) {
        throw new Error('Truncated Git object read; backup audit is incomplete.');
      }
      if (header[1] === 'blob') inspect(name, output.subarray(offset, offset + length));
      offset += length + 1;
    }
    if (offset !== output.length) throw new Error('Unexpected Git batch output.');
  }
  return { checked, problems };
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const options = process.argv.slice(2);
    if (options.length > 1 || (options.length && !/^--(worktree|staged|history)$/.test(options[0]))) {
      throw new Error('Use --worktree, --staged, or --history.');
    }
    const mode = (options[0] ?? '--worktree').slice(2);
    const root = git(process.cwd(), ['rev-parse', '--show-toplevel']).toString('utf8').trim();
    const { checked, problems } = auditRepository(root, mode);
    for (const { name, kind } of problems) console.error(`${kind} detected in ${name}; value withheld.`);
    console.log(`${mode}: checked ${checked} files/blobs; ${problems.length} credential findings.`);
    process.exitCode = problems.length ? 1 : 0;
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
