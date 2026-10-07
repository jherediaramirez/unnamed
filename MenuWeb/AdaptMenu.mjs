// Generates Content/Menu/Web/index.html (and the preference schema) from the unchanged
// reference menu. Each file in Patches/ adapts one feature by editing ORIGINAL reference
// text only; ReferencePatcher rejects overlapping edits. Edit the patches and Runtime/,
// never the generated HTML.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {preferenceSchema} from './MenuPreferenceSchema.mjs';
import {ReferencePatcher} from './Patches/ReferencePatcher.mjs';
import * as Core from './Patches/Core.mjs';
import * as Realms from './Patches/Realms.mjs';
import * as Application from './Patches/Application.mjs';
import * as Video from './Patches/Video.mjs';
import * as Input from './Patches/Input.mjs';
import * as Preferences from './Patches/Preferences.mjs';
import * as SettingsRouting from './Patches/SettingsRouting.mjs';
import * as Worlds from './Patches/Worlds.mjs';
import * as Dialogs from './Patches/Dialogs.mjs';

const patches = [Core, Realms, Application, Video, Input, Preferences, SettingsRouting, Worlds, Dialogs];
// Concatenated in this order into the page's single script.
const runtimeFiles = ['MenuBridgeRuntime.js', 'MenuPrototypeRuntime.js', 'MenuDialogRuntime.js', 'MenuVideoRuntime.js',
  'MenuInputRuntime.js', 'MenuPreferencesRuntime.js', 'MenuWorldsRuntime.js', 'MenuApplicationRuntime.js',
  'FoundationMenuRuntime.js'];

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const reference = path.join(root, 'Reference/minecraft-menu');
const options = {debugTools: !process.argv.includes('--without-debug-tools')};

const source = fs.readFileSync(path.join(reference, 'index.html'), 'utf8');
const expected = JSON.parse(fs.readFileSync(path.join(root, 'Reference/OriginalHashes.json'), 'utf8')).index;
if (crypto.createHash('sha256').update(source).digest('hex') !== expected)
  throw new Error('Reference changed. Review every patch before regeneration.');

const patch = new ReferencePatcher(source.replace(/\r\n/g, '\n'));
for (const feature of patches) {
  patch.owner = feature.name;
  feature.apply(patch, options);
}

// Assembly: embed credits, panorama faces (file:// cannot upload them to WebGL), the
// preference contract and the runtime modules in place of the reference prototype hook.
const preferences = preferenceSchema(patch.resolveData(), patch.lines);
const runtime = 'const PREFERENCE_SCHEMA=' + JSON.stringify(preferences) + ';\n' +
  runtimeFiles.map(file => fs.readFileSync(path.join(root, 'MenuWeb/Runtime', file), 'utf8')).join('\n');
const credits = JSON.parse(fs.readFileSync(path.join(reference, 'assets/minecraft/texts/credits.json'), 'utf8'));
const panorama = Array.from({length: 6}, (_, i) => 'data:image/png;base64,' +
  fs.readFileSync(path.join(reference, `assets/minecraft/textures/gui/title/background/panorama_${i}.png`)).toString('base64'));
patch.owner = 'AdaptMenu';
patch.line('window.MenuPrototype=', `const BUNDLED_CREDITS=${JSON.stringify(credits)};\nconst PANORAMA_FACES=${JSON.stringify(panorama)};\n${runtime}`);

const output = patch.apply();
new vm.Script(output.match(/<script>([\s\S]*?)<\/script>/)[1], {filename: 'adapted-menu.js'});
for (const feature of patches) feature.verify?.(output);

const web = path.join(root, 'Content/Menu/Web');
fs.mkdirSync(web, {recursive: true});
// Raw runtime data, not an Unreal asset. A .json file here triggers DataTable auto-import.
fs.writeFileSync(path.join(web, 'preferences-schema.menuprefs'), JSON.stringify(preferences, null, 2) + '\n');
fs.writeFileSync(path.join(web, 'index.html'), output);
console.log(`Adapted ${output.length} characters from ${patches.length} feature patches; reference unchanged; effect audit passed.`);
