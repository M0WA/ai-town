---
name: mark-phase-done
description: Use this skill when the user wants to mark a phase as complete in the implementation plan and sync the GitHub board. Examples: "mark phase 1 done", "phase 1 is complete", "mark-phase-done", "close out phase 2".
---

# Mark Phase Done

Mark a phase as **DONE** in `implementation/INDEX.md` and sync the GitHub project board.

## Configuration

**Target phase** (optional): the phase number to mark done.

Parse `[TARGET_PHASE]` from the user's invocation at the very start:

- If the user specified a phase (e.g. `phase 1`, `phase-2`, `2`), use that number.
- If nothing was specified, infer from the current git branch name using the convention
  `planning/phase-N` → Phase N−1 is done (e.g. branch `planning/phase-2` → Phase 1 is done).
  Run `git branch --show-current` to read the branch name.
- If the branch does not follow the `planning/phase-N` convention and no phase was specified,
  ask the user which phase to mark done before continuing.

Confirm the resolved `[TARGET_PHASE]` before making any changes.

## Process

### Step 1 — Verify current status

Read `implementation/INDEX.md`. Find the Phase Overview table row for `[TARGET_PHASE]`.

- If the row already shows `**DONE**`, report "Phase [N] is already marked DONE — nothing to do."
  and stop.
- If the row shows any other status (`Planned`, `In Progress`, etc.), proceed to Step 2.

### Step 2 — Validate phase completion

Use the Skill tool to invoke `validate-phase-done` for `[TARGET_PHASE]`.

- If the result is **COMPLETE**: proceed to Step 3.
- If the result is **INCOMPLETE**: display the full validation report (incomplete deliverables
  and unmet exit criteria), then ask the user:

  ```
  Phase [N] has outstanding items (listed above).
  Mark it as DONE anyway? [yes / no]
  ```

  - If the user confirms: proceed to Step 3 (record that an override occurred).
  - If the user declines: stop without modifying any files.

### Step 3 — Update INDEX.md

Edit `implementation/INDEX.md`: in the Phase Overview table, change the Status cell for
`[TARGET_PHASE]` from its current value to `**DONE**`.

Do not touch any other rows, any per-phase files, or any deliverable checkboxes.

### Step 4 — Lint check

Run the markdown linter to confirm no formatting was broken:

```bash
npx markdownlint-cli 'implementation/INDEX.md'
```

Fix any violations before continuing.

### Step 5 — Report

Report to the user:

```
Phase [N] marked DONE.
INDEX.md updated.
```

If the validation in Step 2 required a user override, also note:

```
Note: phase was marked DONE with [M] incomplete deliverable(s) / [K] unmet exit criteria.
```

## Rules

- Only `INDEX.md` is modified — never per-phase files, spec files, or deliverable checkboxes.
- Always confirm the resolved phase number before writing (Step 1 read is mandatory).
- If the phase is already DONE, exit cleanly without writing.
- The git branch inference is a convenience only — when ambiguous, always ask.
