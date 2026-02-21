---
name: fix-specs
description: Use this skill when the user asks to "fix specs", "review specs", "validate the spec", "check the spec for issues", or wants all agent roles to review the project specifications and iteratively fix all high/critical issues until a clean pass is achieved.
---

# Fix Specs

Iteratively review and fix the project specification using all agent roles — with domain-scoped
re-reviews, diff-based prompts, structured issue reporting, and issue deduplication to minimise
token usage and latency.

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
| `TOTAL_FIXES` | 0 | +1 per fix applied |

**Domain → file mapping** (used to determine TOUCHED_DOMAINS):

| Domain | Spec files |
|---|---|
| `gamedesign-lookandfeel` | `architecture/game-design/**`, `CLAUDE.md` |
| `gamedesign-ux` | `architecture/ui-ux/**`, `CLAUDE.md` |
| `graphics-artist-2d-texture` | `architecture/asset-standards/2d-texture-standards.md`, `CLAUDE.md` |
| `graphics-artist-3d-model` | `architecture/asset-standards/3d-model-standards.md`, `architecture/asset-standards/building-atlas-layout.md`, `CLAUDE.md` |
| `sound-artist-opensoftal` | `architecture/audio-architecture/**`, `CLAUDE.md` |
| `cicd-dev-github` | `architecture/ci-cd/**`, `CLAUDE.md` |
| `graphics-dev-irrlicht` | `architecture/graphics-architecture/**`, `CLAUDE.md` |
| `sound-dev-opensoftal` | `architecture/audio-architecture/**`, `CLAUDE.md` |
| `test-dev-cpp` | `architecture/testing/**`, `CLAUDE.md` |

---

## Process

### Step 1 — Determine which agents to run this round

**Round 1**: run all 9 agents.

**Round 2+**: run only agents that meet at least one of these conditions:
- They reported at least one in-scope issue last round
- Their domain is in `TOUCHED_DOMAINS` (their spec files were modified by fixes last round)

Agents in `CLEAN_DOMAINS` whose domain is **not** in `TOUCHED_DOMAINS` are skipped — their
previous clean result carries forward. Note in the round summary which agents were skipped and why.

---

### Step 2 — Launch agents in parallel

For each agent selected in Step 1, launch them **simultaneously** using the Task tool with
**`model: haiku`** — review agents only read files and report structured issues.

#### Round 1 prompt (full read):

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and
> OpenAL Soft. Read the following spec files (and only these): [DOMAIN FILE LIST FROM TABLE ABOVE].
> Review the spec from your domain's perspective.
>
> Only report [TARGET_SEVERITIES] issues. For each issue, output it in this exact schema:
>
> ```
> ISSUE
> severity: [CRITICAL|HIGH|MEDIUM|LOW]
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
> Read only the modified files and review the changed sections. For each of your previous issues,
> confirm whether it was resolved or is still present. Also check whether the fixes introduced
> any new [TARGET_SEVERITIES] issues.
>
> Output each issue (new or persisting) in this schema:
>
> ```
> ISSUE
> severity: [CRITICAL|HIGH|MEDIUM|LOW]
> domain: [your agent type]
> file: [path/to/file.md]
> section: [section heading or line reference]
> description: [one sentence]
> recommendation: [concrete fix]
> status: [NEW|PERSISTING]
> ```
>
> If no [TARGET_SEVERITIES] issues remain in your domain, output exactly: `NO ISSUES FOUND`

Do not start Step 3 until **all launched agents have returned their results**.

---

### Step 3 — Deduplicate and display findings

#### 3a — Deduplicate

Merge issues that target the same `(file, section)` pair from multiple agents:

- Compatible recommendations → merge into one issue, note both domains.
- Conflicting recommendations → keep both, mark `[CONFLICT]` — resolve before Step 4 begins.

#### 3b — Display

```
=== SPEC REVIEW — Round N ===
Severity filter: [TARGET_SEVERITIES]
Agents run: X / 9  (skipped: [list of skipped agents and reason])

[severity] issues: X  (new: C | persisting: D)

--- Senior Game Designer ---
  [SEVERITY][NEW] architecture/game-design/file.md § Section
    Description: ...
    → Recommendation: ...

--- Senior UI/UX Designer ---
  NO ISSUES FOUND  ✓ (carried forward — domain untouched)

... (skipped agents show "SKIPPED — domain untouched, previously clean")
```

---

### Step 4 — Fix [TARGET_SEVERITIES] issues

Resolve `[CONFLICT]` items first — reason about the best fix and pick one recommendation.

Fix remaining issues highest severity first. For each:

1. Apply the fix to the relevant spec file(s) using your own tools:
   - Detailed technical content → edit the appropriate `architecture/` file
   - Project overview, guidelines, or index entries → edit `CLAUDE.md`
   - If no `architecture/` file exists for the topic, create one and add it to
     `architecture/DOCUMENT_INDEX.md` and the index table in `CLAUDE.md`
2. After applying, briefly confirm the issue is resolved and no new issues were introduced.

After all fixes this cycle:
- Update `TOUCHED_FILES` with every file written or edited.
- Update `TOUCHED_DOMAINS` based on the domain → file mapping table.
- Update `TOTAL_FIXES`.

---

### Step 5 — Completion check

Update `CLEAN_DOMAINS`:
- Add any agent that reported `NO ISSUES FOUND` this round (or was carried forward as clean).
- Remove any agent from `CLEAN_DOMAINS` whose domain appears in `TOUCHED_DOMAINS`.

**Did every agent (all 9) reach `CLEAN_DOMAINS` this round?**

- If **yes** → proceed to Step 6 (lint + summary).
- If **no** → return to Step 1 for the next cycle. Only run `/compress` before the next cycle
  if the context window is too full to survive another full round. Do not compress routinely.

---

### Step 6 — Lint and complete

Run the markdown linter once:

```bash
markdownlint 'architecture/**/*.md' 'CLAUDE.md'
```

Fix any violations and re-run until the linter exits zero.

Output the final summary:

```
=== SPEC REVIEW COMPLETE ===

Severity filter: [TARGET_SEVERITIES]
Rounds completed: N
Total fixes applied: TOTAL_FIXES
Agent runs saved: X  (domain-scoped skips across all rounds)
Conflicts resolved: Y

The specification has passed a full review by all agents with no [TARGET_SEVERITIES] issues remaining.
```

---

## Rules

- All 9 agents run on round 1; later rounds are domain-scoped (Step 1)
- Launch review agents with `model: haiku` — they only read and report structured issues
- Never embed full file contents in agent prompts — agents read files using their own tools
- Do not start Step 3 until all launched agents for the current round have returned
- Issues below [TARGET_SEVERITIES] are out of scope — agents must not report them
- Structured issue schema is mandatory — free-text reports must be parsed into schema before dedup
- Fixes go into `architecture/` files or `CLAUDE.md` only — never in `implementation/`
- `[CONFLICT]` items must be resolved before fixes begin
- If the same issue persists across 3 rounds without resolution, flag it and ask the user for guidance
- Never mark a round complete unless ALL 9 agents are in `CLEAN_DOMAINS`
