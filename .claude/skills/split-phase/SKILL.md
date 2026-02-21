---
name: split-phase
description: Use this skill when the user wants to split a phase into multiple phases or redistribute its deliverables across other phases. Examples: "split phase 3", "split-phase phase-5", "break up phase 4 into smaller phases".
---

# Split Phase

Have the Product Owner propose how to split a given phase — either into multiple new phases or by
redistributing deliverables into existing phases — then validate the proposal with both squads
before applying it.

## Configuration

**Target phase**: the phase to split.

Parse `[TARGET_PHASE]` from the user's invocation at the very start:

- If the user specified a phase (e.g. `phase 3`, `phase-5`, `5`), set `[TARGET_PHASE]` to that
  phase (e.g. "Phase 3").
- If nothing was specified, **ask the user** before proceeding:

  > Which phase should be split? Please specify a phase number (e.g. "Phase 3").

  Wait for the user's response, then set `[TARGET_PHASE]` accordingly.

## Process

### Step 1 — Product Owner proposes the split

Launch the `prod-owner` agent with the following prompt:

> You are a Senior Product Owner for AI Town, a 3D city simulator built with C++, Irrlicht, and
> OpenAL Soft. Read `./implementation/INDEX.md` and all per-phase files under `./implementation/`
> as well as the relevant architecture spec files under `architecture/`.
>
> Your task: propose how to split **[TARGET_PHASE]**. The split may take one of two forms (or a
> combination):
>
> - **Sub-split**: divide [TARGET_PHASE] into two or more new consecutive phases, assigning
>   deliverables to each.
> - **Redistribution**: move some deliverables from [TARGET_PHASE] into other existing phases
>   where they fit naturally (earlier or later phases).
>
> For each proposed change state:
> - Which deliverables move where (new phase or existing phase), and why.
> - Any dependency constraints that must be respected (i.e. a deliverable that must precede
>   another).
> - Updated phase names and descriptions for any new or changed phases.
> - How `INDEX.md` phase numbering should change if new phases are inserted.
>
> Do **not** modify any files yet — only output the proposal. Be specific and concrete.

Wait for the `prod-owner` agent to finish before continuing.

### Step 2 — Display the proposal

Present the Product Owner's proposal clearly to the user before launching squad reviews:

```
=== SPLIT PROPOSAL — [TARGET_PHASE] ===

[prod-owner proposal verbatim]
```

### Step 3 — Parallel squad reviews

Launch **all 9 agents simultaneously** — 5 design + 4 tech — using **`model: haiku`** to review
the proposed split from their domain perspectives.

#### Design Squad (5 agents in parallel)

| Agent | Subagent type |
|---|---|
| Senior Game Designer | `gamedesign-lookandfeel` |
| Senior UI/UX Designer | `gamedesign-ux` |
| Senior 2D Texture Artist | `graphics-artist-2d-texture` |
| Senior 3D Model Artist | `graphics-artist-3d-model` |
| Senior Sound Artist | `sound-artist-opensoftal` |

Each design agent prompt:

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and
> OpenAL Soft. The Product Owner has proposed the following split of [TARGET_PHASE]:
>
> [PROPOSAL TEXT]
>
> Read `./implementation/INDEX.md` and the per-phase files under `./implementation/` and the
> relevant architecture spec files under `architecture/`. From your domain's perspective, review
> the proposed split: does it respect dependencies, make logical groupings, and produce phases of
> reasonable scope? Flag any issues (with severity CRITICAL, HIGH, MEDIUM, or LOW) and provide
> concrete recommendations. Before reading any files, scan the proposal text above — if
> [TARGET_PHASE] has no deliverables in your domain, output `NO ISSUES FOUND` immediately
> without reading any files.

#### Tech Squad (4 agents in parallel)

| Agent | Subagent type |
|---|---|
| Senior GitHub Pipeline Engineer | `cicd-dev-github` |
| Senior C++ Developer (Irrlicht) | `graphics-dev-irrlicht` |
| Senior C++ Developer (OpenAL Soft) | `sound-dev-opensoftal` |
| Senior C++ Test Engineer | `test-dev-cpp` |

Each tech agent prompt:

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and
> OpenAL Soft. The Product Owner has proposed the following split of [TARGET_PHASE]:
>
> [PROPOSAL TEXT]
>
> Read `./implementation/INDEX.md` and the per-phase files under `./implementation/` and the
> relevant architecture spec files under `architecture/`. From your domain's perspective, review
> the proposed split: does it respect technical dependencies, produce buildable increments, and
> make logical groupings? Flag any issues (with severity CRITICAL, HIGH, MEDIUM, or LOW) and
> provide concrete recommendations. If you have no concerns, say "NO ISSUES FOUND".

All 9 agents run in parallel. Do not start Step 4 until **all 9 agents have returned their
results** — late-arriving results must not be missed.

### Step 4 — Collect and display squad findings

After all 9 agents respond, display a structured summary:

```
=== SQUAD REVIEW — Split of [TARGET_PHASE] ===

--- Senior Game Designer ---
  [findings or NO ISSUES FOUND]

--- Senior UI/UX Designer ---
  [findings or NO ISSUES FOUND]

--- Senior 2D Texture Artist ---
  [findings or NO ISSUES FOUND]

--- Senior 3D Model Artist ---
  [findings or NO ISSUES FOUND]

--- Senior Sound Artist ---
  [findings or NO ISSUES FOUND]

--- Senior GitHub Pipeline Engineer ---
  [findings or NO ISSUES FOUND]

--- Senior C++ Developer (Irrlicht) ---
  [findings or NO ISSUES FOUND]

--- Senior C++ Developer (OpenAL Soft) ---
  [findings or NO ISSUES FOUND]

--- Senior C++ Test Engineer ---
  [findings or NO ISSUES FOUND]
```

If two agents make conflicting recommendations for the same concern, surface the conflict
explicitly and reason about the best resolution before passing it to the Product Owner.

### Step 5 — Product Owner applies the split

Launch the `prod-owner` agent with the original proposal and the full squad findings:

> You are a Senior Product Owner for AI Town. You previously proposed the following split of
> [TARGET_PHASE]:
>
> [PROPOSAL TEXT]
>
> The design and tech squads reviewed it and provided the following feedback:
>
> [SQUAD FINDINGS]
>
> Now apply the split to the implementation plan files:
> - Update or create the relevant `./implementation/phase-N.md` files.
> - Update `./implementation/INDEX.md` to reflect any new phases, renumbered phases, or changed
>   phase names and statuses.
> - Incorporate all CRITICAL and HIGH squad feedback into the final split. Note any MEDIUM/LOW
>   items you chose to address or defer, with a brief reason.
> - Do not modify any `architecture/` spec files.
>
> When done, confirm every file changed and summarise the final split applied.

Wait for the `prod-owner` agent to finish before continuing.

### Step 6 — Commit

Commit all implementation plan changes:

```bash
git add implementation/
git commit -m "refactor(plan): split [TARGET_PHASE] per squad review"
```

### Step 7 — Markdown lint check

Run the markdown linter once after the commit:

```bash
markdownlint 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'
```

If the linter exits with errors, fix every reported violation, re-run until zero, then amend or
create a follow-up commit for lint fixes only.

### Step 8 — Update GitHub project board

Launch the `proj-manager` agent to sync the updated implementation plan to the GitHub project
board:

> You are a Senior Project Manager for AI Town. The implementation plan has just been updated
> by a phase split. Read `./implementation/INDEX.md` and all per-phase files under
> `./implementation/`, then update the GitHub project board ("AI Town", repo `M0WA/ai-town`)
> to reflect the new phase structure: create or update milestones, create or close issues, and
> move cards to the correct status columns. Report a summary of every action taken.

Wait for the `proj-manager` agent to finish, then output a completion summary:

```
=== SPLIT PHASE COMPLETE ===

Target: [TARGET_PHASE]
Files changed: [list]
New phases: [list, if any]
Deliverables redistributed: [count]

Squad issues addressed: [count CRITICAL+HIGH]
Squad issues deferred: [count MEDIUM+LOW, with reasons]

GitHub board: updated (see proj-manager summary above)
```

## Rules

- If no phase is specified, always ask the user before proceeding — never guess or default to a phase
- The Product Owner proposes first; squads review the proposal, not the final files
- All 9 squad agents run in parallel — never skip any
- Do not start Step 4 until all 9 agents have returned their results
- CRITICAL and HIGH squad issues must be incorporated into the final split; MEDIUM and LOW are at the Product Owner's discretion
- Never modify `architecture/` spec files during this skill
- `INDEX.md` must always be updated to reflect the new phase structure
- If inserting new phases causes renumbering, all affected phase files and `INDEX.md` references must be updated consistently
