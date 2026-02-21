---
name: fix-specs
description: Use this skill when the user asks to "fix specs", "review specs", "validate the spec", "check the spec for issues", or wants all agent roles to review the project specifications and iteratively fix all high/critical issues until a clean pass is achieved.
---

# Fix Specs

Iteratively review and fix the project specification using all agent roles. Repeat until no issues at or above the configured severity remain in any domain.

## Configuration

**Severity filter** (optional, default: `CRITICAL+HIGH`): controls which issue levels agents report and which are fixed each cycle.

Parse `[TARGET_SEVERITIES]` from the user's invocation at the very start:

- If the user specified a list (e.g. `critical`, `critical+high+medium`, `all`), use those levels.
- If nothing was specified, default to **CRITICAL and HIGH**.

Express `[TARGET_SEVERITIES]` as a human-readable list (e.g. "CRITICAL and HIGH", or "CRITICAL, HIGH, and MEDIUM") and use it consistently in every step below. Issues below the threshold are out of scope for the entire run — agents must not report them.

## Process

### Step 1 — Identify spec files

Read all specification files in the project. This includes:
- `CLAUDE.md` — primary project spec (project overview, guidelines, and index of architecture files)
- `architecture/` — canonical detailed spec files; **all files here must be read, maintained, and extended as needed**. The `architecture/DOCUMENT_INDEX.md` lists every file. When a fix requires adding or expanding detail, update the relevant `architecture/` file (not just `CLAUDE.md`).
- `epic.txt` — if present, raw planning document
- Any other `.md` files at the project root that describe requirements, architecture, or design

### Step 2 — Parallel agent reviews

Launch ALL of the following agents **in parallel** using the Task tool, each specializing in their domain.

**Agents to launch (all in parallel):**

| Agent role | Expertise |
|---|---|
| `gamedesign-lookandfeel` | Gameplay, balance, traffic systems, economy, simulation depth |
| `gamedesign-ux` | UI/UX, player interaction, interface design |
| `graphics-artist-3d-model` | 3D model requirements, asset specifications |
| `graphics-artist-2d-texture` | 2D texture requirements, art style consistency |
| `graphics-dev-irrlicht` | Irrlicht engine technical correctness, rendering pipeline |
| `sound-artist-opensoftal` | Audio design, sound/music requirements |
| `sound-dev-opensoftal` | OpenAL Soft technical correctness, audio pipeline |
| `test-dev-cpp` | Testability, C++ testing best practices, coverage strategy |
| `cicd-dev-github` | CI/CD pipeline completeness, GitHub Actions requirements |

Each agent prompt should be:

> You are a [role title]. Review the following project specification for a 3D city simulator called AI Town built with C++, Irrlicht, and OpenAL Soft. Only report [TARGET_SEVERITIES] issues in your area of expertise — do not report issues below that threshold. For each issue provide: severity, description of the problem, and a concrete recommendation to fix it. Be specific. If there are no [TARGET_SEVERITIES] issues in your domain, say "NO ISSUES FOUND".

Include the full contents of the spec files in each agent's prompt.

### Step 3 — Collect and display findings

After all agents respond, display a structured summary:

```
=== REVIEW ROUND [N] ===
Severity filter: [TARGET_SEVERITIES]

[severity] issues: X
[severity] issues: Y

[Agent role]
  [SEVERITY] Issue description
    → Recommendation: ...

[Agent role]
  NO ISSUES FOUND
```

### Step 4 — Fix all [TARGET_SEVERITIES] issues

For each in-scope issue (highest severity first):

1. Present the issue and the agent's recommendation clearly
2. Apply the fix to the relevant spec file(s):
   - Detailed technical content → edit the appropriate `architecture/` file
   - Project overview, guidelines, or index entries → edit `CLAUDE.md`
   - If no `architecture/` file exists for the topic, create one and add it to `architecture/DOCUMENT_INDEX.md` and the index table in `CLAUDE.md`
3. Confirm what was changed

Apply fixes one at a time, updating the spec file after each. Do not batch multiple conflicting changes without verifying consistency.

If two agents make conflicting recommendations for the same issue, explicitly note the conflict, reason about the best resolution, and apply the most appropriate fix.

### Step 6 — Re-review

After all fixes are applied, go back to **Step 2** and run all agents again on the updated spec.

**Cycle synchronisation**: fixing (Step 4) may begin as soon as the first agent results arrive — there is no need to wait for all agents before starting fixes. However, do not start a new cycle (return to Step 2) until **all agents from the current round have returned their results** — late-arriving results would otherwise be silently dropped, causing stale or conflicting fixes in the next round.

**Context compaction**: only run `/compact` before starting a new cycle if the context window is too full to survive another full round (9 parallel agents + fix pass). Do not compact routinely — compressing when unnecessary discards useful context and slows the process. Skip `/compact` entirely on the final cycle when all agents report clean — just output the completion summary.

### Step 7 — Completion check

After each review round, check: **did every agent report NO [TARGET_SEVERITIES] issues?**

- If **yes** → the loop is complete. Run the markdown linter once:

  ```bash
  markdownlint 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'
  ```

  Fix any violations and re-run until the linter exits zero. Then output a final summary:

```
=== SPEC REVIEW COMPLETE ===

Severity filter: [TARGET_SEVERITIES]
Rounds completed: N
Total issues fixed: X

The specification has passed a full review by all agents with no [TARGET_SEVERITIES] issues remaining.
```

- If **no** → continue from Step 4 with the new findings.

## Rules

- Never mark a round as complete unless ALL agents explicitly report no [TARGET_SEVERITIES] issues in their domain
- Do not skip any agent role — every domain must review every round
- All agents must run in parallel each round to minimize latency
- Issues below [TARGET_SEVERITIES] are out of scope — agents must not report them
- If the same issue persists across 3 rounds without being resolved, flag it explicitly and ask the user for guidance before continuing
- **All spec files must be maintained and extended**: fixes go into `architecture/` files (canonical detail) or `CLAUDE.md` (overview/index). Never leave a fix as a comment or note — write it into the spec. If a topic lacks an `architecture/` file, create one.
