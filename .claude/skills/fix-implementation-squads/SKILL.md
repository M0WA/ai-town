---
name: fix-implementation-squads
description: Iteratively fix all CRITICAL/HIGH issues in both specs and the implementation plan until a clean pass is achieved — using squad-scoped re-reviews (design-squad + tech-squad), diff-based prompts, issue deduplication, fix verification, and deferred commits to minimize token usage and latency. Does NOT sync the plan from specs first.
---

# Fix Implementation (Squads)

Iteratively review and fix issues across both the spec files and the implementation plan —
repeating until a full clean pass is achieved on the targeted phases. The implementation plan
is taken as-is; no Product Owner sync step is run.

Review agents are launched as two squads in parallel: **design-squad** (5 domains) and
**tech-squad** (4 domains), rather than as 9 individual agents.

---

## Configuration

Parse both variables from the user's invocation at the very start, before doing anything else.

**Severity filter** (optional, default: `CRITICAL+HIGH`): controls which issue levels agents
report and which are fixed each cycle.

Parse `[TARGET_SEVERITIES]`:

- If the user specified a list (e.g. `critical`, `critical+high+medium`, `all`), use those levels.
- If nothing was specified, default to **CRITICAL and HIGH**.

Express `[TARGET_SEVERITIES]` as a human-readable list (e.g. "CRITICAL and HIGH") and use it
consistently throughout. Issues below the threshold are out of scope — agents must not report them.

**Phase scope** (optional, default: all phases): controls which implementation plan phases are
reviewed and fixed.

Parse `[TARGET_PHASES]`:

- If the user specified phases (e.g. `phase-3`, `phase-3 phase-5`), restrict scope to those phases.
- If nothing was specified, default to **all phases**.

Express `[TARGET_PHASES]` as a human-readable phrase (e.g. "phase-3 and phase-5" or "all phases").
Issues in out-of-scope phases are noted but do not block completion.

**Prompt substitution**: when constructing any squad task in Step 2, replace every occurrence of
`[TARGET_SEVERITIES]` with the resolved severity list (e.g. "CRITICAL and HIGH"), and replace
`[PHASE SCOPE IF SPECIFIED]` with "Focus only on [TARGET_PHASES]." — or omit that sentence
entirely if all phases are in scope. Never pass bracket-placeholders literally to squads or agents.

---

## State (maintained across cycles)

Before the first cycle, initialise the following state. Update it after every cycle.

| Variable | Initial value | Updated after each cycle |
|---|---|---|
| `ROUND` | 1 | +1 each cycle |
| `CLEAN_DOMAINS` | `{}` (empty set) | Add domain when its agent reports NO ISSUES |
| `TOUCHED_DOMAINS` | `{}` (empty set) | Set to domains whose files were modified this cycle |
| `TOUCHED_FILES` | `{}` (empty set) | Set to files written/edited this cycle |
| `ISSUE_ROUNDS` | `{}` | Map from `(file § section)` → round first seen; drives 3-round persistence check |
| `SKIPPED_AGENT_RUNS` | 0 | +N per round for each agent domain skipped this cycle |
| `TOTAL_SPEC_FIXES` | 0 | +1 per spec fix applied |
| `TOTAL_PLAN_FIXES` | 0 | +1 per plan fix applied |

**Squad grouping** (used to determine which squads to launch):

| Squad | Domains |
|---|---|
| `design-squad` | `gamedesign-lookandfeel`, `gamedesign-ux`, `graphics-artist-2d-texture`, `graphics-artist-3d-model`, `sound-artist-opensoftal` |
| `tech-squad` | `cicd-dev-github`, `graphics-dev-irrlicht`, `sound-dev-opensoftal`, `test-dev-cpp` |

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

> **Note**: `implementation/**` appears in all domains because every agent reviews the
> implementation plan. Any plan fix therefore re-runs all agents next round — this is
> intentional: plan changes can introduce cross-domain regressions that a previously-clean
> agent must catch.

---

## Process

### Step 1 — Determine which squads to run this round

**Round 1**: run both squads (full read, no diff available yet).

**Round 2+**: for each squad, run it if **any** of its domains meet at least one of these
conditions:

- That domain reported at least one in-scope issue last round (it may have follow-on issues)
- That domain appears in `TOUCHED_DOMAINS` (its files were modified by fixes last round)

Skip a squad only if **all** of its domains are in `CLEAN_DOMAINS` **and** none of its domains
appear in `TOUCHED_DOMAINS`. Agents in clean, untouched domains within a running squad still
execute; their results are merged into the normal issue pool and their domains are added to
`CLEAN_DOMAINS` again if they return `NO ISSUES FOUND`.

Note in the round summary which squads were skipped and why. Increment `SKIPPED_AGENT_RUNS` by
the count of domains belonging to each skipped squad (design-squad = 5, tech-squad = 4).

---

### Step 2 — Launch squads in parallel

For each squad selected in Step 1, launch them **simultaneously** using the Task tool with
**`model: haiku`** — review agents only read files and report structured issues.

**Before constructing any task**, resolve both values and hard-code them into every squad task —
including when defaults apply:

- `[TARGET_SEVERITIES]` → the resolved severity list (e.g. `"CRITICAL and HIGH"` by default)
- `[PHASE SCOPE IF SPECIFIED]` → `"Focus only on <phases>."` if phases were specified, or omit
  the sentence entirely if all phases are in scope (the default)

Never pass bracket-placeholders literally to squads. Squads receive the actual resolved strings.

#### Round 1 task (full read):

Pass the following as the task to **each squad** (design-squad and tech-squad receive identical
wording — each squad's agents apply it to their own domain):

> Read the implementation plan files under `./implementation/` and the relevant architecture spec
> files under `architecture/`. [PHASE SCOPE IF SPECIFIED] Each agent must review BOTH the
> implementation plan AND the spec files from their own domain's perspective.
>
> Only report [TARGET_SEVERITIES] issues. Do NOT report lower-severity issues. If you find issues
> but none reach [TARGET_SEVERITIES], output `NO ISSUES FOUND`. For each in-scope issue found,
> output it in this exact schema:
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
> Each agent must output EITHER one or more ISSUE blocks OR exactly `NO ISSUES FOUND` — never
> both. Do **not** synthesize or merge agent outputs — present each agent's ISSUE blocks
> separately under their own labelled heading.

#### Round 2+ task (diff-based):

Pass the following as the task to each squad that is running this round:

> Since the last review round, the following files were modified:
>
> [TOUCHED_FILES — list each file with a brief change summary]
>
> Previous issues from round N-1, by domain:
> [list all issues from last round grouped by domain, or "none" for clean domains]
>
> Each agent must: (1) identify which touched files fall within their domain; (2) review only the
> changed sections relevant to their domain; (3) confirm whether their previous issues have been
> resolved or are still present; (4) check whether the fixes introduced any new [TARGET_SEVERITIES]
> issues. If no files in an agent's domain appear in the touched list, re-read the sections
> referenced in that agent's previous issues and confirm whether they are still present.
>
> [PHASE SCOPE IF SPECIFIED] Only report [TARGET_SEVERITIES] issues — do NOT report lower-severity
> issues. If you find issues but none reach [TARGET_SEVERITIES], output `NO ISSUES FOUND`.
>
> Output each in-scope issue (new or persisting) using the same schema:
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
> Each agent must output EITHER one or more ISSUE blocks OR exactly `NO ISSUES FOUND` — never
> both. Do **not** synthesize — present each agent's output separately under their own heading.

Do not start Step 3 until **both launched squads have returned their results**.

---

### Step 3 — Deduplicate and display findings

#### 3a — Deduplicate

**Severity filter (applied first)**: discard any `ISSUE` blocks whose `severity` field is below
`[TARGET_SEVERITIES]` before any further processing. Out-of-scope blocks are silently dropped —
they do not count as issues and do not affect the contradiction rule below.

**Contradiction rule**: after the severity filter, if an agent's response still contains both one
or more in-scope `ISSUE` blocks and the text `NO ISSUES FOUND`, treat the in-scope `ISSUE` blocks
as authoritative and discard the `NO ISSUES FOUND` text. Do not count that agent as clean. If the
severity filter removed all `ISSUE` blocks and only `NO ISSUES FOUND` remains, treat the response
as clean.

Before displaying findings, merge issues that target the same `(file, section)` pair from
multiple agents:

- If two agents flag the same location with compatible recommendations, merge into one issue
  and note both domains.
- If two agents flag the same location with conflicting recommendations, keep both but mark
  as `[CONFLICT]` — resolve in Step 5.

After deduplication, update `ISSUE_ROUNDS`: for each NEW issue (and all round-1 issues), add
`(file § section)` → `ROUND` if the key is not already present. Do not overwrite existing
entries — a PERSISTING issue retains its original round.

#### 3b — Display

```
=== IMPLEMENTATION REVIEW — Round N ===
Severity filter: [TARGET_SEVERITIES]
Squads run: X / 2  (domains active: Y / 9 | skipped domains: [list with reason])

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

... (one block per domain across both squads; domains in skipped squads show
    "SKIPPED — all squad domains clean and untouched")
```

---

### Step 4 — Fix [TARGET_SEVERITIES] issues (highest severity first)

**3-round persistence check**: before launching fix tracks, scan `ISSUE_ROUNDS` for any entry
where `ROUND - first_seen_round >= 3` (issue present for 3+ consecutive rounds without
resolution). If any exist, list them and ask the user for guidance before continuing.

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

- Re-work any items marked `NEEDS_REWORK` before proceeding. If an item remains `NEEDS_REWORK`
  after 2 rework attempts, flag it explicitly and ask the user for guidance before continuing.
- Update `TOUCHED_FILES` with every file written or edited this cycle.
- Update `TOUCHED_DOMAINS` based on the domain → file mapping table in State.
- Update `TOTAL_SPEC_FIXES` and `TOTAL_PLAN_FIXES`.

---

### Step 5 — Completion check

Update `CLEAN_DOMAINS`:

- Add any domain whose agent reported `NO ISSUES FOUND` this round (or was carried forward as
  clean within a running squad).
- Remove any domain from `CLEAN_DOMAINS` whose domain appears in `TOUCHED_DOMAINS` (their files
  changed, so their clean status is invalidated for the next round).

**Did all 9 domains (across both squads) reach `CLEAN_DOMAINS` this round?**

- If **no** → return to Step 1 for the next cycle.
- If **yes** → run a **verification pass** (Step 5a) before committing.

---

### Step 5a — Verification pass (mandatory clean confirmation)

Re-run **both squads** simultaneously using the **Round 1 task** (full read, no diff). This is
an independent re-review from scratch — same task as Round 1 but on the now-fixed files.
Increment `ROUND` by 1 for this pass.

Apply the same deduplication rules from Step 3a to the results.

**Did every domain across both squads return `NO ISSUES FOUND` on this verification pass?**

- If **yes** → all 9 domains confirmed clean on a fresh read; proceed to Step 6.
- If **no** → the prior clean pass does **not** count as done. Treat the reported issues as new
  findings: add them to `ISSUE_ROUNDS` (keyed from this round), reset `CLEAN_DOMAINS` to only the
  domains that returned `NO ISSUES FOUND` on this pass, set `TOUCHED_DOMAINS` to `{}`, and return
  to Step 1 for the next fix cycle.

---

### Step 6 — Lint, commit, and complete

Run the markdown linter once:

```bash
npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'
```

Fix any violations and re-run until the linter exits zero.

Then commit all accumulated changes in a single commit:

```
ACTION REQUIRED — COMMIT:
git add -A
git commit -m "fix(specs+plan): apply plan+spec fixes from squad review (N rounds)"
```

Replace `N` with `ROUND`. If no files were modified at all (all domains clean on round 1), output
`NO COMMIT NEEDED — no files changed`.

Output the final summary:

```
=== FIX IMPLEMENTATION COMPLETE ===

Severity filter: [TARGET_SEVERITIES]
Rounds completed: N
Spec fixes applied: TOTAL_SPEC_FIXES
Plan fixes applied: TOTAL_PLAN_FIXES
Agent runs saved: SKIPPED_AGENT_RUNS  (domain-scoped skips across all rounds)
Conflicts resolved: Y

The implementation plan and architecture specs have passed a full review
by all 9 domains with no [TARGET_SEVERITIES] issues remaining.
```

---

## Rules

- No Product Owner plan-sync step is run — the implementation plan is taken as-is
- Both squads run on round 1 (covering all 9 domains); later rounds are squad-scoped (Step 1)
- A squad is skipped only when ALL its domains are in `CLEAN_DOMAINS` and NONE appear in
  `TOUCHED_DOMAINS`; individual agents within a running squad cannot be selectively skipped
- Do not start Step 3 until both launched squads have returned their results
- Substitute `[TARGET_SEVERITIES]` and `[PHASE SCOPE IF SPECIFIED]` in every squad task — never
  pass them as literal placeholders
- Severity filter runs first (Step 3a): discard ISSUE blocks below `[TARGET_SEVERITIES]` before
  applying the contradiction rule
- Contradiction rule applies only to in-scope ISSUE blocks; if filtering leaves no in-scope
  issues, the response is treated as clean
- Issues below the severity threshold are out of scope — agents must not report them; if they
  do, discard silently
- Structured issue schema is mandatory — free-text issue reports must be parsed into schema
  before dedup
- Do not modify any files until the user confirms in Step 4
- Spec fixes go into `architecture/` files only — never into the implementation plan
- Plan fixes go into `implementation/phase-N.md` files only — never into spec files
- `INDEX.md` must be updated if any structural change is made to the implementation plan
- Each fixing agent must self-verify their own changes before the round closes
- `[CONFLICT]` items must be resolved before fix tracks launch
- Single deferred commit at the final clean pass — no per-round commits
- If the same issue persists across 3 rounds without resolution, flag it explicitly and ask the
  user for guidance before continuing
- Phase scope: agents review and fix only the target phases; issues in out-of-scope phases are
  noted but do not block completion
- A "clean pass" only counts as done after the Step 5a verification pass (both squads re-run
  from scratch on the full read task) also returns `NO ISSUES FOUND` for all 9 domains — if the
  verification pass finds issues, fix cycles continue
