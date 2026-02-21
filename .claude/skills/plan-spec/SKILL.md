---
name: plan-spec
description: Use this skill when the user wants to sync the implementation plan with the current specs, then have both design and technical squads review it. Examples: "plan-spec", "update the implementation plan from specs", "sync plan to spec", "refresh the implementation plan".
---

# Plan Spec

Update the implementation plan to reflect the current architecture specs, then run both the design squad and the tech squad over it to surface any gaps or issues.

## Configuration

**Severity filter** (optional, default: `CRITICAL+HIGH`): controls which issue levels agents report and which are fixed each cycle.

Parse `[TARGET_SEVERITIES]` from the user's invocation at the very start:

- If the user specified a list (e.g. `critical`, `critical+high+medium`, `all`), use those levels.
- If nothing was specified, default to **CRITICAL and HIGH**.

Express `[TARGET_SEVERITIES]` as a human-readable list (e.g. "CRITICAL and HIGH", or "CRITICAL, HIGH, and MEDIUM") and use it consistently in every step below. Issues below the threshold are out of scope for the entire run — agents must not report them.

## Process

### Step 1 — Product Owner updates the implementation plan

Launch the `prod-owner` agent with the following prompt:

> You are a Senior Product Owner for AI Town, a 3D city simulator built with C++, Irrlicht, and OpenAL Soft. Read ALL specification files under `architecture/` and `CLAUDE.md`, then update `./implementation/INDEX.md` (and the per-phase files under `./implementation/`) so the plan accurately and completely reflects the current specs. Follow the Core Rule: Spec Consistency defined in your agent instructions. If the files do not exist yet, create them. When done, confirm what was written and list any spec contradictions you flagged.

Wait for the `prod-owner` agent to finish and confirm the plan has been written to `./implementation/INDEX.md` before continuing.

### Step 2 — Parallel squad reviews

Once the plan is written, launch **both squads simultaneously** using the Task tool:

#### Design Squad (all 5 agents in parallel)

Use the `design-squad` skill behaviour: launch `gamedesign-lookandfeel`, `gamedesign-ux`, `graphics-artist-2d-texture`, `graphics-artist-3d-model`, and `sound-artist-opensoftal` in parallel. Each agent prompt:

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and OpenAL Soft. Read `./implementation/INDEX.md` and the per-phase files under `./implementation/` and the relevant architecture spec files under `architecture/`. Review the implementation plan from your domain's perspective. Only report [TARGET_SEVERITIES] issues — do not report issues below that threshold. For each in-scope issue provide a concrete recommendation. If no [TARGET_SEVERITIES] issues in your domain, say "NO ISSUES FOUND".

#### Tech Squad (all 4 agents in parallel)

Use the `tech-squad` skill behaviour: launch `cicd-dev-github`, `graphics-dev-irrlicht`, `sound-dev-opensoftal`, and `test-dev-cpp` in parallel. Each agent prompt:

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and OpenAL Soft. Read `./implementation/INDEX.md` and the per-phase files under `./implementation/` and the relevant architecture spec files under `architecture/`. Review the implementation plan from your domain's perspective. Only report [TARGET_SEVERITIES] issues — do not report issues below that threshold. For each in-scope issue provide a concrete recommendation. If no [TARGET_SEVERITIES] issues in your domain, say "NO ISSUES FOUND".

All 9 agents (5 design + 4 tech) run in parallel.

### Step 3 — Collect and display findings

After all agents respond, display a structured summary:

```
=== PLAN REVIEW ===
Severity filter: [TARGET_SEVERITIES]

[severity] issues: X
[severity] issues: Y

--- Senior Game Designer ---
  [SEVERITY] Issue description
    → Recommendation: ...

--- Senior UI/UX Designer ---
  ...

--- Senior 2D Texture Artist ---
  ...

--- Senior 3D Model Artist ---
  ...

--- Senior Sound Artist ---
  ...

--- Senior GitHub Pipeline Engineer ---
  ...

--- Senior C++ Developer (Irrlicht) ---
  ...

--- Senior C++ Developer (OpenAL Soft) ---
  ...

--- Senior C++ Test Engineer ---
  ...
```

### Step 4 — Fix [TARGET_SEVERITIES] issues in the plan

For each in-scope issue (highest severity first):

1. Present the issue and recommendation clearly
2. Apply the fix directly to the relevant file(s) under `./implementation/` (per-phase file or `INDEX.md`)
3. Confirm what was changed

If two agents make conflicting recommendations for the same issue, surface the conflict explicitly, reason about the best resolution, and apply the most appropriate fix.

### Step 5 — Markdown lint check

After all fixes are applied, run the markdown linter:

```bash
markdownlint 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'
```

If the linter exits with errors, fix every reported violation before continuing. Re-run until the linter exits zero. Only then proceed to Step 6.

### Step 6 — Re-review

After all fixes are applied and the linter is clean, return to **Step 2** and run all 9 agents again on the updated plan.

**Cycle synchronisation**: fixing (Step 4) may begin as soon as the first agent results arrive — there is no need to wait for all agents before starting fixes. However, do not start a new cycle (return to Step 2) until **all 9 agents from the current round have returned their results** — late-arriving results would otherwise be silently dropped, causing stale or conflicting fixes in the next round.

**Context compaction**: only run `/compact` before starting a new cycle if the context window is too full to survive another full round (9 parallel agents + fix pass). Do not compact routinely — compressing when unnecessary discards useful context and slows the process. Skip `/compact` entirely on the final cycle when all agents report clean — just output the completion summary.

### Step 7 — Completion check

After each review round, check: **did every agent report NO [TARGET_SEVERITIES] issues?**

- If **yes** → output a final summary:

```
=== PLAN REVIEW COMPLETE ===

Severity filter: [TARGET_SEVERITIES]
Rounds completed: N
Total issues fixed: X

The implementation plan has passed a full review by all agents with no [TARGET_SEVERITIES] issues remaining.
```

- If **no** → continue from Step 4 with the new findings.

## Rules

- The Product Owner step (Step 1) must complete before squad reviews begin
- All 9 squad agents run in parallel each review round — never skip any
- Issues below [TARGET_SEVERITIES] are out of scope — agents must not report them
- If the same issue persists across 3 rounds without resolution, flag it explicitly and ask the user for guidance before continuing
- Fixes go into `./implementation/INDEX.md` and the per-phase files only — do not modify spec files during this skill (use `/fix-specs` for that)
