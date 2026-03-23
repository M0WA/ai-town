---
name: fix-implementation
description: Iteratively fix all CRITICAL/HIGH issues in both specs and the implementation plan until a clean pass is achieved — using domain-scoped re-reviews, diff-based prompts, issue deduplication, fix verification, and deferred commits to minimize token usage and latency. Does NOT sync the plan from specs first.
---

# Fix Implementation

Iteratively review and fix issues across both the spec files and the implementation plan —
repeating until a full clean pass is achieved on the targeted phases. The implementation plan
is taken as-is; no Product Owner sync step is run.

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

**Review agent model** (optional, default: `haiku`): controls which Claude model is used for the
review agents launched in Step 2.

Parse `[TARGET_MODEL]`:

- If the user specified a model (e.g. `model=sonnet`, `model=opus`, `model=haiku`), use that value.
- If nothing was specified, default to **`haiku`**.

Valid values: `haiku`, `sonnet`, `opus`. Any unrecognised value should be rejected with an error
message before proceeding.

**Prompt substitution**: when constructing any agent prompt in Step 2, replace every occurrence of
`[TARGET_SEVERITIES]` with the resolved severity list (e.g. "CRITICAL and HIGH"), and replace
`[PHASE SCOPE IF SPECIFIED]` with "Focus only on [TARGET_PHASES]." — or omit that sentence
entirely if all phases are in scope. Never pass bracket-placeholders literally to agents.

---

## State (maintained across cycles)

Before the first cycle, initialise the following state. Update it after every cycle.

| Variable | Initial value | Updated after each cycle |
|---|---|---|
| `ROUND` | 1 | +1 each cycle |
| `CLEAN_DOMAINS` | `{}` (empty set) | Add agent domain when it reports NO ISSUES |
| `TOUCHED_DOMAINS` | `{}` (empty set) | Set to domains whose files were modified this cycle |
| `TOUCHED_FILES` | `{}` (empty set) | Set to files written/edited this cycle |
| `ISSUE_ROUNDS` | `{}` | Map from `(file § section)` → round first seen; drives 3-round persistence check |
| `SKIPPED_AGENT_RUNS` | 0 | +N per round for each agent skipped this cycle |
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

> **Note**: `implementation/**` appears in all domains because every agent reviews the
> implementation plan. Any plan fix therefore re-runs all agents next round — this is
> intentional: plan changes can introduce cross-domain regressions that a previously-clean
> agent must catch.

---

## Process

### Step 1 — Determine which agents to run this round

**Round 1**: run all 9 agents (full read, no diff available yet).

**Round 2+**: run only agents that meet at least one of these conditions:

- They reported at least one in-scope issue last round (they may have follow-on issues)
- Their domain is in `TOUCHED_DOMAINS` (their files were modified by fixes last round)

Agents in `CLEAN_DOMAINS` whose domain is **not** in `TOUCHED_DOMAINS` are skipped — their
previous clean result carries forward. Note in the round summary which agents were skipped and
why. Increment `SKIPPED_AGENT_RUNS` by the number of agents skipped this round.

---

### Step 2 — Launch agents in parallel

For each agent selected in Step 1, launch them simultaneously using the Task tool with
**`model: [TARGET_MODEL]`** (default: `haiku`) — review agents only read files and report
structured issues.

**Before constructing any prompt**, resolve both values and hard-code them into every agent prompt
— including when defaults apply:

- `[TARGET_SEVERITIES]` → the resolved severity list (e.g. `"CRITICAL and HIGH"` by default)
- `[PHASE SCOPE IF SPECIFIED]` → `"Focus only on <phases>."` if phases were specified, or omit
  the sentence entirely if all phases are in scope (the default)

Never pass bracket-placeholders literally to agents. Agents must receive the actual resolved
strings so they know exactly what to report and what to skip.

#### Round 1 prompt (full read):

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and
> OpenAL Soft. Read the implementation plan files under `./implementation/` and the relevant
> architecture spec files under `architecture/`. [PHASE SCOPE IF SPECIFIED] Review BOTH the
> implementation plan AND the spec files from your domain's perspective.
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
> Output EITHER one or more ISSUE blocks OR exactly `NO ISSUES FOUND` — never both.

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
> issues. [PHASE SCOPE IF SPECIFIED] Only report [TARGET_SEVERITIES] issues — do NOT report
> lower-severity issues. If you find issues but none reach [TARGET_SEVERITIES], output
> `NO ISSUES FOUND`.
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
> Output EITHER one or more ISSUE blocks OR exactly `NO ISSUES FOUND` — never both.

**If no files in this agent's domain appear in `TOUCHED_FILES`**: replace the files-modified
opening with: "No files in your domain were modified since last round. Re-read the sections
referenced in your previous issues and confirm whether they are still present."

Do not start Step 3 until **all launched agents have returned their results**.

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
Review model:    [TARGET_MODEL]
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

- Add any agent that reported `NO ISSUES FOUND` this round (or was carried forward as clean).
- Remove any agent from `CLEAN_DOMAINS` whose domain appears in `TOUCHED_DOMAINS` (their files
  changed, so their clean status is invalidated for the next round).

**Did every agent (all 9) reach `CLEAN_DOMAINS` this round?**

- If **no** → return to Step 1 for the next cycle.
- If **yes** → run a **verification pass** (Step 5a) before committing.

---

### Step 5a — Verification pass (mandatory clean confirmation)

Re-run **all 9 agents** simultaneously using the **Round 1 prompt** (full read, no diff) and
**`model: [TARGET_MODEL]`**. This is an independent re-review from scratch — same prompt as
Round 1 but on the now-fixed files.
Increment `ROUND` by 1 for this pass.

Apply the same deduplication rules from Step 3a to the results.

**Did every agent return `NO ISSUES FOUND` on this verification pass?**

- If **yes** → all 9 agents confirmed clean on a fresh read; proceed to Step 6.
- If **no** → the prior clean pass does **not** count as done. Treat the reported issues as new
  findings: add them to `ISSUE_ROUNDS` (keyed from this round), reset `CLEAN_DOMAINS` to only the
  agents that returned `NO ISSUES FOUND` on this pass, set `TOUCHED_DOMAINS` to `{}`, and return
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

Replace `N` with `ROUND`. If no files were modified at all (all agents clean on round 1), output
`NO COMMIT NEEDED — no files changed`.

Output the final summary:

```
=== FIX IMPLEMENTATION COMPLETE ===

Severity filter: [TARGET_SEVERITIES]
Review model:    [TARGET_MODEL]
Rounds completed: N
Spec fixes applied: TOTAL_SPEC_FIXES
Plan fixes applied: TOTAL_PLAN_FIXES
Agent runs saved: SKIPPED_AGENT_RUNS  (domain-scoped skips across all rounds)
Conflicts resolved: Y

The implementation plan and architecture specs have passed a full review
by all 9 agents with no [TARGET_SEVERITIES] issues remaining.
```

---

## Rules

- No Product Owner plan-sync step is run — the implementation plan is taken as-is
- All 9 agents run on round 1; later rounds are domain-scoped (Step 1)
- Do not start Step 3 until all launched agents for the current round have returned
- Substitute `[TARGET_SEVERITIES]`, `[PHASE SCOPE IF SPECIFIED]`, and `[TARGET_MODEL]` in every agent prompt — never pass them as literal placeholders
- Severity filter runs first (Step 3a): discard ISSUE blocks below `[TARGET_SEVERITIES]` before applying the contradiction rule
- Contradiction rule applies only to in-scope ISSUE blocks; if filtering leaves no in-scope issues, the response is treated as clean
- Issues below the severity threshold are out of scope — agents must not report them; if they do, discard silently
- Structured issue schema is mandatory — free-text issue reports must be parsed into schema before dedup
- Do not modify any files until the user confirms in Step 4
- Spec fixes go into `architecture/` files only — never into the implementation plan
- Plan fixes go into `implementation/phase-N.md` files only — never into spec files
- `INDEX.md` must be updated if any structural change is made to the implementation plan
- Each fixing agent must self-verify their own changes before the round closes
- `[CONFLICT]` items must be resolved before fix tracks launch
- Single deferred commit at the final clean pass — no per-round commits
- If the same issue persists across 3 rounds without resolution, flag it explicitly and ask the user for guidance before continuing
- Phase scope: agents review and fix only the target phases; issues in out-of-scope phases are noted but do not block completion
- A "clean pass" only counts as done after the Step 5a verification pass (all 9 agents re-run from scratch) also returns `NO ISSUES FOUND` — if the verification pass finds issues, fix cycles continue
