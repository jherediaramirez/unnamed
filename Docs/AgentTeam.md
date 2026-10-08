# Agent team

A low-cost way to run large reviews and mechanical changes with Claude Code: an Opus 5.5 lead plans, verifies and writes up; Haiku 5.5 subagents do the reading and small edits, each in its own context window and each under 100,000 tokens.

## Files

| File | Role |
|---|---|
| `Scripts/AgentTeam/agents/haiku-reviewer.md` | Read-only reviewer for one slice of files. Returns at most 10 findings in a fixed format. Max 20 turns. |
| `Scripts/AgentTeam/agents/haiku-fixer.md` | Applies one fix the lead has already decided on. Edits only named files. Max 12 turns. |
| `Scripts/AgentTeam/commands/agent-review.md` | The `/agent-review` command: the full Opus-led review procedure. |
| `Scripts/InstallAgentTeam.ps1` | Copies the agent and command files into `.claude/`, where Claude Code loads them. Rerun after editing them. |
| `Scripts/MeasureAgentUsage.mjs` | Reads the session transcripts and reports tokens for the lead and each subagent. |

## Run

```powershell
Set-Location C:\dev\projects\Unnamed
& Scripts\InstallAgentTeam.ps1      # once, and after editing Scripts\AgentTeam
claude --model opus
```

Then type `/agent-review`. Both agent files pin `model: haiku`, so the subagents run on Haiku whatever the lead uses.

Afterwards, in a second terminal:

```powershell
node Scripts\MeasureAgentUsage.mjs                  # latest session
node Scripts\MeasureAgentUsage.mjs --json           # machine-readable
```

**Peak** is the largest single request (the context-window size, which the 100k budget applies to). **Processed** is every token summed across requests, which drives cost; most of it is cheap cache reads.

## How the budget holds

Claude Code has no per-subagent token cap, so the budget comes from four things:

- **Slicing.** The lead gives each reviewer about 60 KB of source (roughly 15-20k tokens), leaving room for grep, reasoning and the answer.
- **Rules in the agent files.** Read each file once, read ranges in files over 25 KB, at most 5 Grep calls, never open reference, generated or evidence folders.
- **Turn caps.** `maxTurns` stops a runaway agent; its output is marked partial.
- **Short returns.** Reviewers answer in under 400 words, so the lead's own context grows by only about 500 tokens per slice.
