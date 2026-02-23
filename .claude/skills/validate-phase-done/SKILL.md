---
name: validate-phase-done
description: Use this skill when the user wants to check whether a phase is complete, or to see what items are still outstanding before marking a phase done. Examples: "validate phase 2", "is phase 1 done?", "what's left in phase 3?", "check phase completion".
---

# Validate Phase Done

Inspect a phase's deliverable checkboxes and exit criteria, then report whether the phase is
complete or list every outstanding item.

## Configuration

**Target phase** (required): the phase number to validate.

Parse `[TARGET_PHASE]` from the user's invocation:

- If the user specified a phase (e.g. `phase 2`, `phase-3`, `3`), use that number.
- If nothing was specified, ask the user: "Which phase would you like to validate?" — wait for
  their answer before continuing.

## Process

### Step 1 — Read the phase file

Read `implementation/INDEX.md` to confirm the phase exists and note its current Status.

Read `implementation/phase-[N].md` in full.

### Step 2 — Extract incomplete deliverables

Scan every checkbox line in the Deliverables section:

- `- [x]` → complete
- `- [ ]` → **incomplete**

Collect all incomplete lines verbatim.

### Step 3 — Extract unmet exit criteria

Scan the Exit Criteria section for any condition that contains a `- [ ]` checkbox or is
explicitly marked as not yet satisfied (e.g. a TODO marker or a `[ ]` inline).

Collect any unmet exit criteria verbatim.

### Step 4 — Output the validation report

```
=== PHASE [N] VALIDATION ===
Phase: [N] — [Phase Name]
Current status in INDEX.md: [Planned | In Progress | Done | ...]

Result: COMPLETE  ✓
```

or, if anything is outstanding:

```
=== PHASE [N] VALIDATION ===
Phase: [N] — [Phase Name]
Current status in INDEX.md: [Planned | In Progress | Done | ...]

Result: INCOMPLETE — [M] deliverable(s) and [K] exit criterion/criteria outstanding

Incomplete deliverables ([M]):
  - [ ] <verbatim checkbox text>
  - [ ] <verbatim checkbox text>
  ...

Unmet exit criteria ([K]):
  - [ ] <verbatim criterion text>
  ...
  (none)  ← if K = 0
```

If `M = 0` and `K = 0`, the result is **COMPLETE**; otherwise it is **INCOMPLETE**.

## Rules

- Read-only: this skill never modifies any file.
- If the phase file does not exist, report the error and stop.
- Report incomplete items verbatim from the file — do not paraphrase or summarise.
- Exit criteria that are narrative (no checkbox) are assumed met unless they contain an explicit
  `[ ]` or `TODO` marker.
