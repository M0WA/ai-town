---
name: plan-fix-spec
description: Sync the implementation plan with the current specs AND iteratively fix all CRITICAL/HIGH issues in both specs and the plan until a clean pass is achieved — using domain-scoped re-reviews, diff-based prompts, issue deduplication, fix verification, and deferred commits to minimize token usage and latency.
---

# Plan Fix Spec

Sync the implementation plan from the current architecture specs, then iteratively review and
fix issues across both the spec files and the implementation plan — repeating until a full clean
pass is achieved on the targeted phases.

The user may optionally specify which phases to target (e.g. "phase-3 phase-5"). If no phases
are specified, all phases are in scope.

---

## Configuration

**Severity filter** (optional, default: `CRITICAL+HIGH`): controls which issue levels agents
report and which are fixed each cycle.

Parse `[TARGET_SEVERITIES]` from the user's invocation at the very start:

- If the user specified a list (e.g. `critical`, `critical+high+medium`, `all`), use those levels.
- If nothing was specified, default to **CRITICAL and HIGH**.

Express `[TARGET_SEVERITIES]` as a human-readable list (e.g. "CRITICAL and HIGH") and use it
consistently throughout. Issues below the threshold are out of scope — agents must not report them.

---

## State (maintained across cycles)

Before the first cycle, initialise the following state. Update it after every cycle.

| Variable | Initial value | Updated after each cycle |
|---|---|---|
| `ROUND` | 1 | +1 each cycle |
| `CLEAN_DOMAINS` | `{}` (empty set) | Add agent domain when it reports NO ISSUES |
| `TOUCHED_DOMAINS` | `{}` (empty set) | Set to domains whose files were modified this cycle |
| `TOUCHED_FILES` | `{}` (empty set) | Set to files written/edited this cycle |
| `ALL_ISSUES` | `{}` | Accumulate deduplicated issue list across rounds |
| `TOTAL_SPEC_FIXES` | 0 | +1 per spec fix applied |
| `TOTAL_PLAN_FIXES` | 0 | +1 per plan fix applied |

**Domain → file mapping** (used to determine TOUCHED_DOMAINS):

| Domain | Files |
|---|---|
| `gamedesign-lookandfeel` | `architecture/game-design/**`, `implementation/**` |
| `gamedesign-ux` | `architecture/ui-ux/**`, `implementation/**` |
| `graphics-artist-2d-texture` | `architecture/asset-standards/2d-texture-standards.md`, `implementation/**` |
| `graphics-artist-3d-model` | `architecture/asset-standards/3d-model-standards.md`, `architecture/asset-standards/building-atlas-layout.md`, `implementation/**` |
| `sound-artist-opensoftal` | `architecture/audio-architecture/**`, `implementation/**` |
| `cicd-dev-github` | `architecture/ci-cd/**`, `implementation/**` |
| `graphics-dev-irrlicht` | `architecture/graphics-architecture/**`, `implementation/**` |
| `sound-dev-opensoftal` | `architecture/audio-architecture/**`, `implementation/**` |
| `test-dev-cpp` | `architecture/testing/**`, `implementation/**` |

---

## Process

### Step 1 — Product Owner syncs the implementation plan (Round 1 only)

**Skip this step on rounds 2+.** The plan is already in sync after round 1; re-running risks
overwriting fixes applied by squad agents in previous rounds.

On round 1, launch the `prod-owner` agent:

> You are a Senior Product Owner for AI Town, a 3D city simulator built with C++, Irrlicht, and
> OpenAL Soft. Read ALL specification files under `architecture/` and `CLAUDE.md`, then update
> the implementation plan files under `./implementation/` (per-phase files + INDEX.md) so they
> accurately and completely reflect the current specs. Follow the Core Rule: Spec Consistency and
> the File Layout rules defined in your agent instructions. When done, confirm what was written
> and list any spec contradictions you flagged.

Wait for the `prod-owner` agent to finish before continuing.

---

### Step 2 — Determine which agents to run this round

**Round 1**: run all 9 agents (full read, no diff available yet).

**Round 2+**: run only agents that meet at least one of these conditions:
- They reported at least one in-scope issue last round (they may have follow-on issues)
- Their domain is in `TOUCHED_DOMAINS` (their files were modified by fixes last round)

Agents in `CLEAN_DOMAINS` whose domain is **not** in `TOUCHED_DOMAINS` are skipped — their
previous clean result carries forward. Note in the round summary which agents were skipped and why.

---

### Step 3 — Launch agents in parallel

For each agent selected in Step 2, launch them simultaneously using the Task tool with
**`model: haiku`** — review agents only read files and report structured issues.

**Scope note**: if the user specified target phases, include that in every prompt.

#### Round 1 prompt (full read):

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and
> OpenAL Soft. Read the implementation plan files under `./implementation/` and the relevant
> architecture spec files under `architecture/`. [PHASE SCOPE IF SPECIFIED]. Review BOTH the
> implementation plan AND the spec files from your domain's perspective.
>
> Only report [TARGET_SEVERITIES] issues. For each issue, output it in this exact schema:
>
> ```
> ISSUE
> severity: [CRITICAL|HIGH|MEDIUM|LOW]
> location: [SPEC|PLAN]
> domain: [your agent type, e.g. sound-dev-opensoftal]
> file: [path/to/file.md]
> section: [section heading or line reference]
> description: [one sentence]
> recommendation: [concrete fix]
> ```
>
> If no [TARGET_SEVERITIES] issues in your domain, output exactly: `NO ISSUES FOUND`

#### Round 2+ prompt (diff-based):

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and
> OpenAL Soft. Since the last review round, the following files in your domain were modified:
>
> [TOUCHED_FILES filtered to this agent's domain — list each file with a brief change summary]
>
> Your previous issues from round N-1:
> [list this agent's issues from last round, or "none" if clean]
>
> Review only the changed sections. For each of your previous issues: confirm whether it has been
> resolved or is still present. Also check whether the fixes introduced any new [TARGET_SEVERITIES]
> issues. [PHASE SCOPE IF SPECIFIED].
>
> Output each issue (new or persisting) using the same schema:
>
> ```
> ISSUE
> severity: [CRITICAL|HIGH|MEDIUM|LOW]
> location: [SPEC|PLAN]
> domain: [your agent type]
> file: [path/to/file.md]
> section: [section heading or line reference]
> description: [one sentence]
> recommendation: [concrete fix]
> status: [NEW|PERSISTING]
> ```
>
> If no [TARGET_SEVERITIES] issues remain in your domain, output exactly: `NO ISSUES FOUND`

Do not start Step 4 until **all launched agents have returned their results**.

---

### Step 4 — Deduplicate and display findings

#### 4a — Deduplicate

Before displaying findings, merge issues that target the same `(file, section)` pair from
multiple agents:

- If two agents flag the same location with compatible recommendations, merge into one issue
  and note both domains.
- If two agents flag the same location with conflicting recommendations, keep both but mark
  as `[CONFLICT]` — resolve in Step 5.

#### 4b — Display

```
=== PLAN + SPEC REVIEW — Round N ===
Severity filter: [TARGET_SEVERITIES]
Agents run: X / 9  (skipped: [list of skipped agents and reason])

[severity] issues: X  (spec: A | plan: B | new: C | persisting: D)
[severity] issues: Y  ...

--- Senior Game Designer ---
  [SEVERITY][SPEC][NEW] file.md § Section
    Description: ...
    → Recommendation: ...

  [SEVERITY][PLAN][PERSISTING] implementation/phase-N.md § Section
    Description: ...
    → Recommendation: ...

--- Senior UI/UX Designer ---
  NO ISSUES FOUND  ✓ (carried forward — domain untouched)

... (one block per agent; skipped agents show "SKIPPED — domain untouched, previously clean")
```

---

### Step 5 — Fix [TARGET_SEVERITIES] issues (highest severity first)

Issues are fixed in two parallel tracks. Resolve `[CONFLICT]` items before launching either
track — reason about the best fix and pick one recommendation.

#### Track A — Spec fixes

For all in-scope issues with `location: SPEC`:

- Group by domain and launch fixing agents in parallel.
- Each agent edits only the `architecture/` files in their domain.
- If a fix requires a new `architecture/` file, the agent must also update
  `architecture/DOCUMENT_INDEX.md` and the index table in `CLAUDE.md`.
- After applying their fix, **each agent immediately self-reviews the changed section** and
  confirms: (a) the issue is resolved, (b) no new issues were introduced. Report result as
  `VERIFIED` or `NEEDS_REWORK`.

#### Track B — Plan fixes

For all in-scope issues with `location: PLAN`:

- Launch the `prod-owner` agent with the full list of plan issues and their recommendations.
- The Product Owner applies all plan fixes to `implementation/phase-N.md` files (and updates
  `INDEX.md` if structure changes).
- The Product Owner must not modify spec files.
- After applying fixes, the Product Owner self-reviews each changed section and confirms
  resolution, reporting `VERIFIED` or `NEEDS_REWORK` per issue.

Run Track A and Track B in parallel. If a plan fix depends on a spec fix, complete that Track A
item before launching the dependent Track B item.

After both tracks finish:
- Re-work any items marked `NEEDS_REWORK` before proceeding.
- Update `TOUCHED_FILES` with every file written or edited this cycle.
- Update `TOUCHED_DOMAINS` based on the domain → file mapping table in State.
- Update `TOTAL_SPEC_FIXES` and `TOTAL_PLAN_FIXES`.

---

### Step 6 — Completion check

Update `CLEAN_DOMAINS`:
- Add any agent that reported `NO ISSUES FOUND` this round (or was carried forward as clean).
- Remove any agent from `CLEAN_DOMAINS` whose domain appears in `TOUCHED_DOMAINS` (their files
  changed, so their clean status is invalidated for the next round).

**Did every agent (all 9) reach `CLEAN_DOMAINS` this round?**

- If **yes** → proceed to Step 7 (lint + commit + summary).
- If **no** → return to Step 2 for the next cycle. Only run `/compress` before the next cycle
  if the context window is too full to survive another full round. Do not compress routinely.

---

### Step 7 — Lint, commit, and complete

Run the markdown linter once:

```bash
markdownlint 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'
```

Fix any violations and re-run until the linter exits zero.

Then commit all accumulated changes in a single commit:

```
ACTION REQUIRED — COMMIT:
git add -A
git commit -m "fix(specs+plan): apply plan+spec fixes from squad review (N rounds)"
```

Replace `N` with `ROUND`. If no files were modified at all (all agents clean on round 1), output
`NO COMMIT NEEDED — no files changed`.

Output the final summary:

```
=== PLAN FIX SPEC COMPLETE ===

Severity filter: [TARGET_SEVERITIES]
Rounds completed: N
Spec fixes applied: TOTAL_SPEC_FIXES
Plan fixes applied: TOTAL_PLAN_FIXES
Agent runs saved: X  (domain-scoped skips across all rounds)
Conflicts resolved: Y

The implementation plan and architecture specs have passed a full review
by all 9 agents with no [TARGET_SEVERITIES] issues remaining.
```

---

## Rules

- Step 1 (prod-owner sync) runs **only on round 1** — never on subsequent rounds
- All 9 agents run on round 1; later rounds are domain-scoped (Step 2)
- Do not start Step 4 until all launched agents for the current round have returned
- Issues below [TARGET_SEVERITIES] are out of scope — agents must not report them
- Structured issue schema is mandatory — free-text issue reports must be parsed into schema before dedup
- Spec fixes go into `architecture/` files only — never into the implementation plan
- Plan fixes go into `implementation/phase-N.md` files only — never into spec files
- `INDEX.md` must be updated if any structural change is made to the implementation plan
- Each fixing agent must self-verify their own changes before the round closes
- `[CONFLICT]` items must be resolved before fix tracks launch
- Single deferred commit at the final clean pass — no per-round commits
- If the same issue persists across 3 rounds without resolution, flag it explicitly and ask the user for guidance before continuing
- Phase scope: if the user specified target phases, agents review and fix only those phases; issues in other phases are noted but do not block completion
