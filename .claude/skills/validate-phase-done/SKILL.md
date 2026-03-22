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

Note which specialist roles own which deliverables (see the **Team** table in the phase file).

### Step 2 — Dispatch domain agents in parallel

Group deliverables and exit criteria by specialist domain. For each domain that has
deliverables, **launch the appropriate specialist agent** (using the Agent tool) with a
read-only verification prompt listing every deliverable and criterion in that domain. Launch
all domain agents in **a single parallel batch** so they run concurrently.

**Domain → agent mapping** (use the role ID from CLAUDE.md):

| Domain | Agent type |
|---|---|
| C++ graphics / Irrlicht / renderer / TextureCache / CMake | `graphics-dev-irrlicht` |
| C++ audio / OpenAL | `sound-dev-opensoftal` |
| C++ simulation logic | `graphics-dev-irrlicht` (or general-purpose) |
| C++ tests / mocks / CMakeLists test targets | `test-dev-cpp` |
| CI/CD / GitHub Actions / validate_assets.py | `cicd-dev-github` |
| 3D model assets (.b3d files, .meta files, generator) | `graphics-artist-3d-model` |
| 2D texture assets (.dds, .png atlases, texture specs) | `graphics-artist-2d-texture` |
| UI/UX spec documents | `gamedesign-ux` |
| Gameplay / simulation spec documents | `gamedesign-lookandfeel` |

**Agent prompt requirements** — each agent prompt must:

1. State "READ-ONLY — do not modify any files."
2. List every deliverable and exit criterion assigned to that domain, quoting the text verbatim
   from the phase file.
3. For each item, instruct the agent to find **specific code evidence**: exact file path,
   line numbers, function/constant names, or file sizes that confirm the item is present and
   correct.
4. Instruct the agent to classify each item as VERIFIED / PARTIAL / UNVERIFIED (deliverables)
   or MET / UNMET / UNVERIFIABLE (exit criteria) with the evidence inline.

Any deliverables not covered by a specialist agent should be verified directly using Glob,
Grep, and Read.

### Step 3 — Collect agent results and synthesise

Wait for all agents to complete. For each agent result:

- Record the VERIFIED / PARTIAL / UNVERIFIED / MET / UNMET / UNVERIFIABLE classification.
- Extract the specific code evidence (file:line, function names, file sizes, etc.) provided
  by the agent.

### Step 4 — Output the validation report

Every entry in the report **must include a code evidence citation** — the specific file,
line number, function name, or measurable fact that supports the classification. Entries
without code evidence are treated as UNVERIFIED / UNMET.

```text
=== PHASE [N] VALIDATION ===
Phase: [N] — [Phase Name]
Current status in INDEX.md: [Planned | In Progress | Done | ...]
(Note: checkbox states ignored — all items verified against real codebase by domain agents)

DELIVERABLES ([V] verified / [P] partial / [U] unverified of [T] total)

  VERIFIED
    ✓ <deliverable text verbatim>
      Evidence: <file:line — specific function/constant/content found>
      Agent: <agent role that verified this>
    ...

  PARTIAL
    ~ <deliverable text verbatim>
      Found: <what was found, with file:line>
      Missing: <what is absent or incomplete>
      Agent: <agent role>
    ...

  UNVERIFIED
    ✗ <deliverable text verbatim>
      Looked for: <what was searched>
      Not found: <what was missing>
      Agent: <agent role>
    ...

EXIT CRITERIA ([M] met / [UV] unverifiable / [F] unmet of [T] total)

  MET
    ✓ <criterion text verbatim>
      Evidence: <file:line or measurable fact>
      Agent: <agent role>
    ...

  UNVERIFIABLE (human review needed)
    ? <criterion text verbatim>
      Reason: <why this cannot be verified programmatically>
    ...

  UNMET
    ✗ <criterion text verbatim>
      Expected: <what was expected>
      Found: <what was actually found, with file:line if applicable>
      Agent: <agent role>
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
- **Every classification requires code evidence**: file path + line number, function name,
  file size, or other measurable artefact. "The spec says so" is not evidence — the actual
  file content must be confirmed.
- **Use domain agents**: do not rely solely on your own Grep/Read for all deliverables —
  delegate to specialist agents so each domain gets expert-level inspection.
- Be specific about what was searched for and what was (or was not) found.
- If a deliverable description is ambiguous, state the interpretation used.
- Do not paraphrase deliverable or criterion text — quote it verbatim in the report.
