#!/usr/bin/env node
// Reports token usage for one Claude Code session in this project: the lead
// agent and every subagent, with a flag for any subagent over the budget.
//
//   node Scripts/MeasureAgentUsage.mjs                     latest session
//   node Scripts/MeasureAgentUsage.mjs --session <id>      a specific session
//   node Scripts/MeasureAgentUsage.mjs --budget 100000 --json
//
// Reads transcripts from ~/.claude/projects/<project>/ (or $CLAUDE_CONFIG_DIR).
// Peak context = the largest single request (input + cache read + cache write
// + output). That is the number the 100k budget applies to. Processed = all
// tokens summed over every request, which is what drives cost.

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const args = process.argv.slice(2);
const option = (name, fallback) => {
  const index = args.indexOf(name);
  return index >= 0 && args[index + 1] ? args[index + 1] : fallback;
};
const budget = Number(option("--budget", "100000"));
const asJson = args.includes("--json");

const configDir = process.env.CLAUDE_CONFIG_DIR || path.join(os.homedir(), ".claude");
const projectsDir = path.join(configDir, "projects");
const projectRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function findProjectDir() {
  if (!fs.existsSync(projectsDir)) fail(`No transcripts folder at ${projectsDir}`);
  const encoded = projectRoot.replace(/[^A-Za-z0-9]/g, "-");
  const exact = path.join(projectsDir, encoded);
  if (fs.existsSync(exact)) return exact;
  const candidates = fs
    .readdirSync(projectsDir)
    .filter((name) => name.toLowerCase().endsWith("-unnamed"))
    .map((name) => path.join(projectsDir, name));
  if (candidates.length === 0) fail(`No transcript folder for ${projectRoot} under ${projectsDir}`);
  return newest(candidates);
}

function newest(paths) {
  return paths.sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs)[0];
}

function fail(message) {
  console.error(message);
  process.exit(1);
}

function readEntries(file) {
  const entries = [];
  for (const line of fs.readFileSync(file, "utf8").split("\n")) {
    if (!line.trim()) continue;
    try {
      entries.push(JSON.parse(line));
    } catch {
      // A partially written last line is normal while a session is running.
    }
  }
  return entries;
}

// One API response can be logged as several lines (one per content block)
// that share message.id and usage, so usage is counted once per message id.
function summarize(entries, { skipSidechain }) {
  const byMessage = new Map();
  let label = "";
  for (const entry of entries) {
    if (skipSidechain && entry.isSidechain) continue;
    if (!label && entry.type === "user") label = firstText(entry.message?.content);
    const message = entry.message;
    if (entry.type !== "assistant" || !message?.usage) continue;
    byMessage.set(message.id || entry.uuid, { model: message.model, usage: message.usage });
  }
  const totals = { requests: 0, input: 0, cacheRead: 0, cacheWrite: 0, output: 0, peak: 0, models: new Set() };
  for (const { model, usage } of byMessage.values()) {
    const input = usage.input_tokens || 0;
    const cacheRead = usage.cache_read_input_tokens || 0;
    const cacheWrite = usage.cache_creation_input_tokens || 0;
    const output = usage.output_tokens || 0;
    totals.requests += 1;
    totals.input += input;
    totals.cacheRead += cacheRead;
    totals.cacheWrite += cacheWrite;
    totals.output += output;
    totals.peak = Math.max(totals.peak, input + cacheRead + cacheWrite + output);
    if (model && model !== "<synthetic>") totals.models.add(model);
  }
  totals.processed = totals.input + totals.cacheRead + totals.cacheWrite + totals.output;
  totals.models = [...totals.models].join(", ");
  totals.label = label.replace(/\s+/g, " ").slice(0, 60);
  return totals;
}

function firstText(content) {
  if (typeof content === "string") return content;
  if (Array.isArray(content)) return content.find((part) => part.type === "text")?.text || "";
  return "";
}

const projectDir = findProjectDir();
const sessionId =
  option("--session", "") ||
  path.basename(
    newest(
      fs
        .readdirSync(projectDir)
        .filter((name) => name.endsWith(".jsonl"))
        .map((name) => path.join(projectDir, name)),
    ) || fail(`No sessions in ${projectDir}`),
    ".jsonl",
  );

const mainFile = path.join(projectDir, `${sessionId}.jsonl`);
if (!fs.existsSync(mainFile)) fail(`Session not found: ${mainFile}`);
const lead = summarize(readEntries(mainFile), { skipSidechain: true });

const subagentDir = path.join(projectDir, sessionId, "subagents");
const subagents = fs.existsSync(subagentDir)
  ? fs
      .readdirSync(subagentDir)
      .filter((name) => name.endsWith(".jsonl"))
      .map((name) => ({
        id: name.replace(/^agent-/, "").replace(/\.jsonl$/, ""),
        ...summarize(readEntries(path.join(subagentDir, name)), { skipSidechain: false }),
      }))
      .filter((agent) => agent.requests > 0)
  : [];

const report = { sessionId, budget, lead, subagents };

if (asJson) {
  console.log(JSON.stringify(report, null, 2));
  process.exit(0);
}

const k = (n) => (n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n));
const row = (name, t, flag = "") =>
  [
    name.padEnd(12),
    String(t.requests).padStart(5),
    k(t.peak).padStart(8),
    k(t.processed).padStart(10),
    k(t.cacheRead).padStart(10),
    k(t.output).padStart(8),
    flag.padEnd(5),
    t.models,
  ].join("  ");

console.log(`Session ${sessionId}   budget per subagent: ${k(budget)} peak context\n`);
console.log(["Agent".padEnd(12), "Reqs".padStart(5), "Peak".padStart(8), "Processed".padStart(10), "CacheRead".padStart(10), "Output".padStart(8), "Flag ", "Model"].join("  "));
console.log(row("lead", lead));
let over = 0;
for (const agent of subagents) {
  const flag = agent.peak > budget ? "OVER" : "ok";
  if (flag === "OVER") over += 1;
  console.log(row(agent.id.slice(0, 12), agent, flag) + `   "${agent.label}"`);
}
const sum = (key) => subagents.reduce((total, agent) => total + agent[key], 0);
console.log(`\nSubagents: ${subagents.length}, over budget: ${over}, total processed ${k(sum("processed"))}, total output ${k(sum("output"))}`);
console.log(`Lead: processed ${k(lead.processed)}, output ${k(lead.output)}`);
if (subagents.length === 0) console.log(`(No subagent transcripts found in ${subagentDir})`);
