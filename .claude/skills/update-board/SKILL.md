---
name: update-board
description: Use this skill when the user wants to sync the GitHub project board with the implementation plan, optionally for a specific phase. Examples: "update-board", "update board for phase 1", "sync the GitHub board", "update the project board for phase 3", "refresh the board".
---

# Update Board

Sync the GitHub project board ("AI Town") with the current implementation plan. If a phase is
specified, only that phase is updated; otherwise all phases are synced.

## Configuration

**Target phase** (optional, default: all phases): controls which phase(s) are synced to GitHub.

Parse `[TARGET_PHASE]` from the user's invocation at the very start:

- If the user specified a phase (e.g. `phase 1`, `phase-3`, `3`), set `[TARGET_PHASE]` to that
  phase (e.g. "Phase 1").
- If nothing was specified, set `[TARGET_PHASE]` to **"all phases"**.

## Process

### Step 1 — Launch the Project Manager

Launch the `proj-manager` agent with the following prompt, substituting `[TARGET_PHASE]`:

> You are a Senior Project Manager for AI Town, a 3D city simulator. Your job is to keep the
> GitHub project board ("AI Town") in sync with the implementation plan files under
> `./implementation/`.
>
> **Scope**: [TARGET_PHASE]
>
> Instructions:
>
> 1. Read `./implementation/INDEX.md` to understand the full plan structure.
> 2. If the scope is a specific phase (e.g. "Phase 1"), read only that phase file
>    (e.g. `./implementation/phase-1.md`). If the scope is "all phases", read every phase file
>    under `./implementation/` (phase-0.md through the highest numbered phase, plus
>    `post-v1-backlog.md` if present).
> 3. For each phase in scope:
>    a. Ensure a GitHub milestone exists for the phase (create if missing, update title /
>       description / due date if stale).
>    b. For each deliverable / task in the phase file, ensure a corresponding GitHub issue exists
>       on the "AI Town" project board (create if missing, update title / body / labels /
>       milestone if stale).
>    c. Close any GitHub issues whose corresponding deliverables have been removed from the plan.
>    d. Move cards to the correct status column based on the phase / deliverable status noted in
>       the plan file (e.g. "In Progress", "Done", "To Do").
> 4. Report a summary of every create / update / close / move action taken, grouped by phase.
>    If nothing needed changing, say "Board already up to date for [TARGET_PHASE]."

### Step 2 — Display result

After the `proj-manager` agent finishes, display its summary to the user verbatim. If the agent
reported errors (e.g. GitHub API failures), surface them clearly so the user can act.

## Rules

- Only the `proj-manager` agent is launched — no squad reviews needed.
- Do not modify any `implementation/` files during this skill; it is read-only with respect to
  the plan.
- If `[TARGET_PHASE]` is a specific phase and the corresponding file does not exist, report the
  error to the user rather than silently syncing all phases.
