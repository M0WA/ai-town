---
name: validate-phase-done
description: Use this skill when the user wants to check whether a phase is complete, or to see what items are still outstanding before marking a phase done. Examples: "validate phase 2", "is phase 1 done?", "what's left in phase 3?", "check phase completion".
---

# Validate Phase Done

Actively verify every deliverable and exit criterion of a phase against the real codebase,
test files, CI configuration, and spec files. **Ignore checkbox state entirely** — a ticked
`[x]` is not evidence; a missing file or failing criterion is.

## Configuration

**Target phase** (required): the phase number to validate.

Parse `[TARGET_PHASE]` from the user's invocation:

- If the user specified a phase (e.g. `phase 2`, `phase-3`, `3`), use that number.
- If nothing was specified, ask the user: "Which phase would you like to validate?" — wait for
  their answer before continuing.

## Process

### Step 1 — Read the phase file

Read `implementation/INDEX.md` to confirm the phase exists and note its current Status.

Read `implementation/phase-[N].md` in full. Extract:

- Every deliverable item (the text of every `- [ ]` or `- [x]` line in the Deliverables
  section, stripping the checkbox prefix)
- Every exit criterion (every line or sub-bullet in the Exit Criteria section, stripping any
  checkbox prefix)

### Step 2 — Verify each deliverable independently

For every deliverable item, **do not trust the checkbox**. Instead, interpret the deliverable
text and gather evidence from the codebase:

- **Source files / headers**: use `Glob` to check the file exists at the expected path;
  use `Grep` or `Read` to confirm key classes, functions, or constants are present.
- **Test files**: use `Glob` to find the test file; use `Grep` to confirm the described
  test cases or test names exist within it.
- **CI / CMake changes**: use `Read` or `Grep` to confirm the described job, step, flag, or
  target is present in `ci.yml` / `CMakeLists.txt` / `CMakePresets.json`.
- **Spec / architecture docs**: use `Read` to confirm the described section or content
  exists in the relevant `architecture/` file.
- **Asset files**: use `Glob` to confirm the asset exists at the expected path.
- **Implementation plan updates** (e.g. "phase marked DONE"): use `Read` on
  `implementation/INDEX.md` or the relevant phase file.

Classify each deliverable as:

- **VERIFIED** — evidence found; the deliverable is genuinely present.
- **UNVERIFIED** — no evidence found; file missing, content absent, or test not implemented.
- **PARTIAL** — some evidence found but the deliverable appears incomplete (e.g. stub exists
  but key logic is absent).

### Step 3 — Verify each exit criterion independently

For every exit criterion, **do not trust any checkbox or TODO marker**. Evaluate whether
the condition is actually satisfied:

- **"All tests pass"** / **"N tests passing"**: look for the test files and check that the
  described test cases exist and are not skipped/disabled. If CI artifacts are unavailable,
  note this and verify the test code exists instead.
- **"Coverage ≥ X%"**: check `architecture/testing/coverage.md` and the CMake gate in
  `CMakeLists.txt` or `ci.yml` for the threshold; note whether the gate is enforced in CI.
- **"CI passes"**: check that `ci.yml` contains the relevant jobs and that no obvious
  blockers are present (missing steps, wrong flags, etc.).
- **"Spec updated"** / **"architecture file contains X"**: `Read` the referenced file and
  confirm the content is present.
- **"Phase marked DONE"**: `Read` `implementation/INDEX.md` and confirm the status.
- **Narrative criteria with no testable artefact**: state the criterion and explain why it
  cannot be verified programmatically, then mark as **UNVERIFIABLE** (not a failure).

Classify each criterion as:

- **MET** — evidence confirms the condition is satisfied.
- **UNMET** — evidence is missing or contradicts the condition.
- **UNVERIFIABLE** — criterion is qualitative/narrative with no inspectable artefact;
  flag for human review.

### Step 4 — Output the validation report

```text
=== PHASE [N] VALIDATION ===
Phase: [N] — [Phase Name]
Current status in INDEX.md: [Planned | In Progress | Done | ...]
(Note: checkbox states ignored — all items verified against codebase)

DELIVERABLES ([V] verified / [P] partial / [U] unverified of [T] total)

  VERIFIED
    ✓ <deliverable text> — <one-line evidence summary>
    ...

  PARTIAL
    ~ <deliverable text> — <what was found vs what is missing>
    ...

  UNVERIFIED
    ✗ <deliverable text> — <what was looked for and not found>
    ...

EXIT CRITERIA ([M] met / [UV] unverifiable / [F] unmet of [T] total)

  MET
    ✓ <criterion text> — <one-line evidence summary>
    ...

  UNVERIFIABLE (human review needed)
    ? <criterion text>
    ...

  UNMET
    ✗ <criterion text> — <what was expected vs what was found>
    ...

Result: COMPLETE ✓
```

or if anything is UNVERIFIED or UNMET:

```text
Result: INCOMPLETE — [X] deliverable(s) unverified/partial, [Y] criterion/criteria unmet
```

The result is **COMPLETE** only when every deliverable is VERIFIED and every exit criterion
is either MET or UNVERIFIABLE (i.e. zero UNVERIFIED, zero PARTIAL, zero UNMET).

## Rules

- Read-only: this skill never modifies any file.
- If the phase file does not exist, report the error and stop.
- **Never use checkbox state as evidence** — treat `[x]` and `[ ]` identically; only
  codebase inspection counts.
- Be specific about what was searched for and what was (or was not) found.
- If a deliverable description is ambiguous, state the interpretation used.
- Do not paraphrase deliverable or criterion text — quote it verbatim in the report.
