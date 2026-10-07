import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

// Test evidence (reports, automation folders and Captures/) lives in Tests/Evidence/<yyyy-mm-dd>/
// and is committed. New runs write to today's folder; named evidence is looked up across every
// date, newest first. Throwaway runtime profiles stay in the ignored Tests/Runtime/.
export const testsRoot = path.dirname(fileURLToPath(import.meta.url));
export const projectRoot = path.resolve(testsRoot, '..');
export const evidenceBase = path.join(testsRoot, 'Evidence');
const now = new Date();
export const today = [now.getFullYear(), now.getMonth() + 1, now.getDate()].map(n => String(n).padStart(2, '0')).join('-');
export const evidenceRoot = path.join(evidenceBase, today);

// Where a new piece of evidence goes, e.g. evidencePath('Captures', 'smoke-1920x1080').
export const evidencePath = (...parts) => path.join(evidenceRoot, ...parts);

// An existing piece of evidence from any date (newest first), or undefined.
export function findEvidence(...parts) {
  const days = fs.existsSync(evidenceBase)
    ? fs.readdirSync(evidenceBase).filter(day => /^\d{4}-\d{2}-\d{2}$/.test(day)).sort().reverse() : [];
  return days.map(day => path.join(evidenceBase, day, ...parts)).find(candidate => fs.existsSync(candidate));
}
