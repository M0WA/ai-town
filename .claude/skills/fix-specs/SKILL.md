---
name: fix-specs
description: Use this skill when the user asks to "fix specs", "review specs", "validate the spec", "check the spec for issues", or wants all agent roles to review the project specifications and iteratively fix all high/critical issues until a clean pass is achieved.
---

# Fix Specs

Iteratively review and fix the project specification using all agent roles. Repeat until no high or critical issues remain in any domain.

## Process

### Step 1 — Identify spec files

Read all specification files in the project. This includes:
- `CLAUDE.md` — primary project spec
- `epic.txt` — if present, raw planning document
- Any other `.md` files at the project root that describe requirements, architecture, or design

### Step 2 — Parallel agent reviews

Launch ALL of the following agents **in parallel** using the Task tool, each specializing in their domain. Each agent must:
- Read the spec files
- Identify issues in their area of expertise
- Rate each issue: **CRITICAL**, **HIGH**, **MEDIUM**, or **LOW**
- For each CRITICAL or HIGH issue, provide a concrete **recommendation** (what to change and how)

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

> You are a [role title]. Review the following project specification for a 3D city simulator called AI Town built with C++, Irrlicht, and OpenAL Soft. Identify any CRITICAL or HIGH severity issues in your area of expertise. For each issue provide: severity (CRITICAL/HIGH), description of the problem, and a concrete recommendation to fix it. Be specific. If there are no issues in your domain, say "NO ISSUES FOUND".

Include the full contents of the spec files in each agent's prompt.

### Step 3 — Collect and display findings

After all agents respond, display a structured summary:

```
=== REVIEW ROUND [N] ===

CRITICAL issues: X
HIGH issues: Y

[Agent role]
  [CRITICAL] Issue description
    → Recommendation: ...

[Agent role]
  [HIGH] Issue description
    → Recommendation: ...

[Agent role]
  NO ISSUES FOUND
```

### Step 4 — Fix all CRITICAL and HIGH issues

For each CRITICAL or HIGH issue (prioritizing CRITICAL first):

1. Present the issue and the agent's recommendation clearly
2. Apply the fix to the relevant spec file(s) — edit `CLAUDE.md` or other spec files directly
3. Confirm what was changed

Apply fixes one at a time, updating the spec file after each. Do not batch multiple conflicting changes without verifying consistency.

If two agents make conflicting recommendations for the same issue, explicitly note the conflict, reason about the best resolution, and apply the most appropriate fix.

### Step 5 — Re-review

After all fixes from this round are applied, go back to **Step 2** and run all agents again on the updated spec.

### Step 6 — Completion check

After each review round, check: **did every agent report NO ISSUES for CRITICAL and HIGH severity?**

- If **yes** → the loop is complete. Output a final summary:

```
=== SPEC REVIEW COMPLETE ===

Rounds completed: N
Total issues fixed: X

The specification has passed a full review by all agents with no CRITICAL or HIGH issues remaining.
```

- If **no** → continue from Step 4 with the new findings.

## Rules

- Never mark a round as complete unless ALL agents explicitly report no CRITICAL or HIGH issues in their domain
- Do not skip any agent role — every domain must review every round
- All agents must run in parallel each round to minimize latency
- MEDIUM and LOW issues are noted but do not block completion
- If the same issue persists across 3 rounds without being resolved, flag it explicitly and ask the user for guidance before continuing
