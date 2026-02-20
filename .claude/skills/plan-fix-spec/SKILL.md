---
name: plan-fix-spec
description: Use this skill when the user wants to sync the implementation plan with the current specs AND iteratively fix all CRITICAL/HIGH issues in both the specs and the plan until a clean pass is achieved. Examples: "plan-fix-spec", "sync and fix the implementation plan", "fix the plan and specs together", "plan-fix-spec phase-3 phase-5".
---

# Plan Fix Spec

Sync the implementation plan from the current architecture specs, then iteratively review and fix issues across both the spec files and the implementation plan — repeating until a full clean pass is achieved on the targeted phases.

The user may optionally specify which phases to target (e.g. "phase-3 phase-5"). If no phases are specified, all phases are in scope.

## Configuration

**Severity filter** (optional, default: `CRITICAL+HIGH`): controls which issue levels agents report and which are fixed each cycle.

Parse `[TARGET_SEVERITIES]` from the user's invocation at the very start:

- If the user specified a list (e.g. `critical`, `critical+high+medium`, `all`), use those levels.
- If nothing was specified, default to **CRITICAL and HIGH**.

Express `[TARGET_SEVERITIES]` as a human-readable list (e.g. "CRITICAL and HIGH", or "CRITICAL, HIGH, and MEDIUM") and use it consistently in every step below. Issues below the threshold are out of scope for the entire run — agents must not report them.

## Process

### Step 1 — Product Owner updates the implementation plan

Launch the `prod-owner` agent with the following prompt:

> You are a Senior Product Owner for AI Town, a 3D city simulator built with C++, Irrlicht, and OpenAL Soft. Read ALL specification files under `architecture/` and `CLAUDE.md`, then update the implementation plan files under `./implementation/` (per-phase files + INDEX.md) so they accurately and completely reflect the current specs. Follow the Core Rule: Spec Consistency and the File Layout rules defined in your agent instructions. When done, confirm what was written and list any spec contradictions you flagged.

Wait for the `prod-owner` agent to finish before continuing.

### Step 2 — Parallel squad reviews

Launch **all 9 agents simultaneously** — 5 design + 4 tech — each reviewing both the implementation plan and the relevant spec files from their domain perspective.

**Scope note**: if the user specified target phases, include that in every agent prompt (e.g. "Focus your review on phases 3 and 5 of the implementation plan."). Otherwise review all phases.

#### Design Squad agents (in parallel)

| Agent | Subagent type |
|---|---|
| Senior Game Designer | `gamedesign-lookandfeel` |
| Senior UI/UX Designer | `gamedesign-ux` |
| Senior 2D Texture Artist | `graphics-artist-2d-texture` |
| Senior 3D Model Artist | `graphics-artist-3d-model` |
| Senior Sound Artist | `sound-artist-opensoftal` |

Each design agent prompt:

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and OpenAL Soft. Read the implementation plan files under `./implementation/` and the relevant architecture spec files under `architecture/`. [PHASE SCOPE IF SPECIFIED]. Review BOTH the implementation plan AND the spec files from your domain's perspective. Only report [TARGET_SEVERITIES] issues — do not report issues below that threshold. For each in-scope issue state: (a) whether the fix belongs in the SPEC FILES or in the IMPLEMENTATION PLAN, and (b) a concrete recommendation. If no [TARGET_SEVERITIES] issues in your domain, say "NO ISSUES FOUND".

#### Tech Squad agents (in parallel)

| Agent | Subagent type |
|---|---|
| Senior GitHub Pipeline Engineer | `cicd-dev-github` |
| Senior C++ Developer (Irrlicht) | `graphics-dev-irrlicht` |
| Senior C++ Developer (OpenAL Soft) | `sound-dev-opensoftal` |
| Senior C++ Test Engineer | `test-dev-cpp` |

Each tech agent prompt:

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and OpenAL Soft. Read the implementation plan files under `./implementation/` and the relevant architecture spec files under `architecture/`. [PHASE SCOPE IF SPECIFIED]. Review BOTH the implementation plan AND the spec files from your domain's perspective. Only report [TARGET_SEVERITIES] issues — do not report issues below that threshold. For each in-scope issue state: (a) whether the fix belongs in the SPEC FILES or in the IMPLEMENTATION PLAN, and (b) a concrete recommendation. If no [TARGET_SEVERITIES] issues in your domain, say "NO ISSUES FOUND".

All 9 agents run in parallel.

### Step 3 — Collect and display findings

After all agents respond, display a structured summary:

```
=== PLAN + SPEC REVIEW — Round N ===
Severity filter: [TARGET_SEVERITIES]

[severity] issues: X  (spec: A | plan: B)
[severity] issues: Y  (spec: C | plan: D)

--- Senior Game Designer ---
  [SEVERITY][SPEC] Issue description
    → Recommendation: ...
  [SEVERITY][PLAN] Issue description
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

### Step 4 — Fix [TARGET_SEVERITIES] issues (highest severity first)

Issues are fixed in two parallel tracks simultaneously:

#### Track A — Spec fixes (design-squad and tech-squad agents)

For all in-scope issues flagged as belonging in the **spec files**:

- Group spec fixes by domain:
  - Game design spec issues → have the relevant `gamedesign-*` or `graphics-artist-*` or `sound-artist-*` agent fix the `architecture/` files
  - Technical spec issues → have the relevant `graphics-dev-irrlicht`, `sound-dev-opensoftal`, `cicd-dev-github`, or `test-dev-cpp` agent fix the `architecture/` files
- Launch all spec-fixing agents in parallel
- Each agent edits only the `architecture/` files in their domain — never the implementation plan
- If a fix requires a new `architecture/` file, the agent must also update `architecture/DOCUMENT_INDEX.md` and the index table in `CLAUDE.md`

#### Track B — Implementation plan fixes (Product Owner)

For all in-scope issues flagged as belonging in the **implementation plan**:

- Launch the `prod-owner` agent with the full list of plan issues and their recommendations
- The Product Owner applies all plan fixes to the appropriate `implementation/phase-N.md` files (and updates `INDEX.md` if structure changes)
- The Product Owner must not modify spec files

Run Track A and Track B in parallel where possible. If a plan fix depends on a spec fix being applied first, complete Track A before launching Track B for that issue.

If two agents make conflicting recommendations for the same issue, surface the conflict explicitly, reason about the best resolution, and apply the most appropriate fix.

### Step 5 — Markdown lint check

After all fixes from this round are applied, run the markdown linter:

```bash
markdownlint 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'
```

If the linter exits with errors, fix every reported violation before continuing. Re-run until the linter exits zero. Only then proceed to Step 6.

### Step 6 — Commit round changes

After the linter is clean, use the **Bash tool** to commit all modified files directly — do NOT delegate this to a sub-agent or merely describe the commands:

```bash
git add -A
git commit -m "fix(specs+plan): apply round-N plan+spec fixes from squad review"
```

Replace `N` with the current round number. If nothing has changed since the last commit (e.g. all agents reported NO ISSUES and no files were modified), skip the commit and proceed directly to Step 8.

### Step 7 — Compress and re-review

After committing, run `/compress` to reduce context window usage, then return to **Step 2** and run all 9 agents again on the updated plan and specs. (Skip `/compress` on the final cycle when all agents report clean — just output the completion summary.)

Note: `/compress` is the built-in context compression command. Do NOT invoke it as a skill via the Skill tool — type it directly as a slash command.

### Step 8 — Completion check

After each review round, check: **did every agent report NO [TARGET_SEVERITIES] issues in their domain (for the targeted phases)?**

- If **yes** → output a final summary:

```
=== PLAN FIX SPEC COMPLETE ===

Severity filter: [TARGET_SEVERITIES]
Rounds completed: N
Spec fixes applied: X
Plan fixes applied: Y

The implementation plan and architecture specs have passed a full review by all 9 agents with no [TARGET_SEVERITIES] issues remaining.
```

- If **no** → continue from Step 4 with the new findings (back through Steps 4 → 5 → 6 → 7 → 8).

## Rules

- The Product Owner sync step (Step 1) must complete before any squad reviews begin
- All 9 squad agents run in parallel each review round — never skip any
- Issues below [TARGET_SEVERITIES] are out of scope — agents must not report them
- Spec fixes go into `architecture/` files only — never into the implementation plan
- Plan fixes go into `implementation/phase-N.md` files only — never into spec files
- `INDEX.md` must be updated if any structural change is made to the implementation plan
- If the same issue persists across 3 rounds without resolution, flag it explicitly and ask the user for guidance before continuing
- Phase scope: if the user specified target phases, agents review and fix only those phases; issues in other phases are noted but do not block completion
