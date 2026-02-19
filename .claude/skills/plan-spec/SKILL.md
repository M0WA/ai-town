---
name: plan-spec
description: Use this skill when the user wants to sync the implementation plan with the current specs, then have both design and technical squads review it. Examples: "plan-spec", "update the implementation plan from specs", "sync plan to spec", "refresh the implementation plan".
---

# Plan Spec

Update the implementation plan to reflect the current architecture specs, then run both the design squad and the tech squad over it to surface any gaps or issues.

## Process

### Step 1 — Product Owner updates the implementation plan

Launch the `prod-owner` agent with the following prompt:

> You are a Senior Product Owner for AI Town, a 3D city simulator built with C++, Irrlicht, and OpenAL Soft. Read ALL specification files under `architecture/` and `CLAUDE.md`, then update `./implementation/INDEX.md` (and the per-phase files under `./implementation/`) so the plan accurately and completely reflects the current specs. Follow the Core Rule: Spec Consistency defined in your agent instructions. If the files do not exist yet, create them. When done, confirm what was written and list any spec contradictions you flagged.

Wait for the `prod-owner` agent to finish and confirm the plan has been written to `./implementation/INDEX.md` before continuing.

### Step 2 — Parallel squad reviews

Once the plan is written, launch **both squads simultaneously** using the Task tool:

#### Design Squad (all 5 agents in parallel)

Use the `design-squad` skill behaviour: launch `gamedesign-lookandfeel`, `gamedesign-ux`, `graphics-artist-2d-texture`, `graphics-artist-3d-model`, and `sound-artist-opensoftal` in parallel. Each agent prompt:

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and OpenAL Soft. Read `./implementation/INDEX.md` and the per-phase files under `./implementation/` and the relevant architecture spec files under `architecture/`. Review the implementation plan from your domain's perspective. Identify any issues, gaps, misalignments with the specs, missing deliverables, or incorrect sequencing. Rate each issue CRITICAL, HIGH, MEDIUM, or LOW. For CRITICAL and HIGH issues provide a concrete recommendation. If no issues in your domain, say "NO ISSUES FOUND".

#### Tech Squad (all 4 agents in parallel)

Use the `tech-squad` skill behaviour: launch `cicd-dev-github`, `graphics-dev-irrlicht`, `sound-dev-opensoftal`, and `test-dev-cpp` in parallel. Each agent prompt:

> You are a [role title] working on AI Town, a 3D city simulator built with C++, Irrlicht, and OpenAL Soft. Read `./implementation/INDEX.md` and the per-phase files under `./implementation/` and the relevant architecture spec files under `architecture/`. Review the implementation plan from your domain's perspective. Identify any issues, gaps, misalignments with the specs, missing deliverables, incorrect sequencing, or technical risks. Rate each issue CRITICAL, HIGH, MEDIUM, or LOW. For CRITICAL and HIGH issues provide a concrete recommendation. If no issues in your domain, say "NO ISSUES FOUND".

All 9 agents (5 design + 4 tech) run in parallel.

### Step 3 — Collect and display findings

After all agents respond, display a structured summary:

```
=== PLAN REVIEW ===

CRITICAL issues: X
HIGH issues: Y

--- Senior Game Designer ---
  [CRITICAL/HIGH/...] Issue description
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

### Step 4 — Fix CRITICAL and HIGH issues in the plan

For each CRITICAL or HIGH issue (CRITICAL first):

1. Present the issue and recommendation clearly
2. Apply the fix directly to the relevant file(s) under `./implementation/` (per-phase file or `INDEX.md`)
3. Confirm what was changed

If two agents make conflicting recommendations for the same issue, surface the conflict explicitly, reason about the best resolution, and apply the most appropriate fix.

### Step 5 — Re-review

After all fixes are applied, return to **Step 2** and run all 9 agents again on the updated plan.

### Step 6 — Completion check

After each review round, check: **did every agent report NO CRITICAL or HIGH issues?**

- If **yes** → output a final summary:

```
=== PLAN REVIEW COMPLETE ===

Rounds completed: N
Total issues fixed: X

The implementation plan has passed a full review by all agents with no CRITICAL or HIGH issues remaining.
```

- If **no** → continue from Step 4 with the new findings.

## Rules

- The Product Owner step (Step 1) must complete before squad reviews begin
- All 9 squad agents run in parallel each review round — never skip any
- MEDIUM and LOW issues are noted but do not block completion
- If the same issue persists across 3 rounds without resolution, flag it explicitly and ask the user for guidance before continuing
- Fixes go into `./implementation/INDEX.md` and the per-phase files only — do not modify spec files during this skill (use `/fix-specs` for that)
