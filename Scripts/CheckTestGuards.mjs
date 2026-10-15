// Every native test sits inside `#if WITH_DEV_AUTOMATION_TESTS`: a test macro outside it breaks every build without dev
// automation tests (Shipping, packages), which the editor build never sees (Codex review 31, finding 1).
//   node Scripts/CheckTestGuards.mjs [files...]   (default: Source/**/Tests/*.cpp; exit 1 on a test outside the guard)
import { readdirSync, readFileSync } from 'node:fs';
import { join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(fileURLToPath(new URL('.', import.meta.url)), '..');
const testMacro = /^\s*(IMPLEMENT_\w*AUTOMATION_TEST\w*|BEGIN_DEFINE_SPEC|BEGIN_CUSTOM_COMPLEX_AUTOMATION_TEST)\s*\(/;

function testFiles(dir, out = []) {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
        const path = join(dir, entry.name);
        if (entry.isDirectory()) testFiles(path, out);
        else if (entry.name.endsWith('.cpp') && /[\\/]Tests$/.test(dir)) out.push(path);
    }
    return out;
}

// The lines with a test macro outside a WITH_DEV_AUTOMATION_TESTS block. Each open #if records whether it is the guard;
// an #else or #elif of the guard leaves it.
export function unguarded(text) {
    const stack = [], found = [];
    text.split(/\r?\n/).forEach((line, index) => {
        const directive = line.match(/^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$/);
        if (directive) {
            const [, kind, rest] = directive;
            if (kind.startsWith('if')) stack.push(kind === 'if' && /^\s*WITH_DEV_AUTOMATION_TESTS\s*(\/\/.*)?$/.test(rest));
            else if (kind === 'endif') stack.pop();
            else if (stack.length) stack[stack.length - 1] = false;
            return;
        }
        if (testMacro.test(line) && !stack.includes(true)) found.push(index + 1);
    });
    return found;
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
    const files = process.argv.length > 2 ? process.argv.slice(2) : testFiles(join(root, 'Source'));
    let bad = 0;
    for (const file of files) {
        const lines = unguarded(readFileSync(file, 'utf8'));
        if (!lines.length) continue;
        bad += lines.length;
        console.log(`${relative(root, file)}: test macro outside WITH_DEV_AUTOMATION_TESTS at line ${lines.join(', ')}`);
    }
    console.log(`${files.length} test files, ${bad} tests outside the guard`);
    process.exit(bad ? 1 : 0);
}
