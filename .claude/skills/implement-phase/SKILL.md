---
name: implement-phase
description: Use this skill to fully implement a phase from the implementation plan — writing all spec updates, C++ code changes, and tests described in the phase file, then building, running tests with auto-fix loops, verifying manual exit criteria with the user, and signing off. Trigger whenever the user says "implement phase X", "code up phase X", "execute phase X", "build phase X", "do phase X", "work on phase X", or describes wanting to turn a phase spec into working code. Also trigger if the user opens a phase file and says "do it", "go", or "start". Use this skill even if the user just says "implement" without specifying a phase — ask which phase before starting.
---

# Implement Phase

Fully implement a phase from the implementation plan end-to-end: apply spec updates, write all
C++ source, test files, and CMakeLists.txt changes; build and run tests in an auto-fix loop;
pause for manual exit criteria user confirmation; then validate, commit, and sign off.

---

## Configuration

Parse at the very start, before doing anything else.

**Target phase** (required): the phase to implement.

- If the user specified a phase (e.g. `phase 11m`, `phase-7`, `7`, `11m`), use that identifier.
- Otherwise, infer from the current git branch: `feat/phase-N` or `implementation/phase-N` → Phase N.
  Run `git branch --show-current`.
- If still ambiguous, ask: "Which phase would you like to implement?"

**Max fix iterations** (optional, default: `5`): the maximum number of build/test fix rounds
before escalating to the user. Parse `max=[N]` from the invocation if present.

**Agent model**: all Agent tool calls within this skill MUST use `model: "opus"`.

---

## State

Initialise before Step 1. Update after each step.

| Variable | Initial value | Updated |
|---|---|---|
| `PHASE_ID` | resolved phase identifier | — |
| `PHASE_TITLE` | parsed from phase file heading | Step 1 |
| `FIX_ROUND` | 0 | +1 each build/test fix cycle |
| `SPEC_UPDATES_APPLIED` | 0 | Step 2 |
| `DELIVERABLES_IMPLEMENTED` | 0 | Step 5 |

---

## Process

### Step 1 — Read the phase file

Read `implementation/phase-[PHASE_ID].md` in full. Parse:

- **Phase title** from the `## Phase [ID]:` heading line.
- **All unchecked deliverables**: every `- [ ]` line, with its parent section heading for context.
  Note adjacent code blocks and prose — they describe exactly how to implement the item.
- **Spec update items**: lines that reference `architecture/` files. Check whether each is followed
  by "**Already applied**" or similar — skip those; collect the rest.
- **Exit criteria section**: split each criterion into:
  - **Automated**: verifiable by running commands (build success, test pass/fail, lint, file existence).
  - **Manual**: requires running the game and human observation (visual rendering, audio behavior,
    gameplay interaction).

If the file does not exist, report the error and stop.

---

### Step 2 — Apply spec updates

Skip this step entirely if all spec update items are already marked "Already applied".

For any unapplied spec updates, group by target architecture directory and launch the appropriate
specialist agent **for each domain in parallel**:

| Spec directory | Agent |
|---|---|
| `architecture/game-design/` | `gamedesign-lookandfeel` |
| `architecture/ui-ux/` | `gamedesign-ux` |
| `architecture/audio-architecture/` | `sound-artist-opensoftal` |
| `architecture/graphics-architecture/` | `graphics-dev-irrlicht` |
| `architecture/asset-standards/` (3D) | `graphics-artist-3d-model` |
| `architecture/asset-standards/` (2D) | `graphics-artist-2d-texture` |
| `architecture/ci-cd/` | `cicd-dev-github` |
| `architecture/testing/` | `test-dev-cpp` |

Each spec agent prompt must include:

- The verbatim spec update instruction(s) from the phase file.
- The target `architecture/` file path(s).
- "Apply the spec changes exactly as described. After applying, confirm the updated section reads
  correctly. Do not change anything outside the specified section."

Wait for all spec agents to complete before proceeding. Code agents in Step 3 may read the updated
specs, so spec updates must land first.

After completion, increment `SPEC_UPDATES_APPLIED` by the number of items applied.

---

### Step 3 — Implement code changes and tests

Group all unchecked code and test deliverables from Step 1 and launch implementation agents
**in parallel**. A deliverable that touches files in two domains (e.g. a simulation function that
also calls an audio interface) belongs to the domain of the primary file being edited; give the
secondary agent the relevant context in their prompt.

**Agent assignments:**

| Domain | Files | Agent |
|---|---|---|
| C++ source: simulation, UI, interfaces, renderer, `main.cpp` | `src/` (non-audio) | `graphics-dev-irrlicht` |
| C++ source: audio | `src/audio/`, `src/interfaces/IAudioSystem.h` | `sound-dev-opensoftal` |
| Test files + CMakeLists.txt test registration | `tests/`, `CMakeLists.txt` (test targets only) | `test-dev-cpp` |
| Build system, CI, non-test CMakeLists changes | `.github/`, `CMakePresets.json`, `Makefile`, `CMakeLists.txt` (non-test) | `cicd-dev-github` |

Each agent prompt must include:

1. The full text of every deliverable section assigned to that agent (copy verbatim from the phase
   file including root cause analysis, code snippets, and inline specifications — agents need the
   full detail to implement correctly).
2. An explicit list of the unchecked `- [ ]` items assigned to that agent.
3. Cross-references: if this agent's code interacts with another agent's work (e.g. the test agent
   needs to know what the C++ agent will add to an interface), include the relevant sections from
   the other agent's deliverables too.
4. "Implement exactly as specified in the phase file. Do not add features or refactor beyond what is
   described. After implementing, confirm each `- [ ]` item is done."
5. The project's coding conventions from `CLAUDE.md` (CamelCase filenames, interface `I` prefix,
   no `std::rand()`, inject clocks/RNG, `StrictMock` for unit tests, etc.).

Wait for all agents to complete before proceeding.

---

### Step 4 — Build and test loop

Run build, then tests. On failure, dispatch targeted fix agents and retry. Repeat up to
`[MAX_FIX_ITERATIONS]` total fix rounds. `FIX_ROUND` counts fix attempts (not total runs).

#### 4a — Build

```bash
make build 2>&1
```

If the build fails:

- Capture the **full compiler output** (errors + warnings).
- Identify the failing files from the error messages.
- Dispatch the appropriate agent with the full error text and the files that need fixing:
  - Compilation errors in `src/` (non-audio) → `graphics-dev-irrlicht`
  - Compilation errors in `src/audio/` → `sound-dev-opensoftal`
  - Compilation errors in `tests/` → `test-dev-cpp`
  - CMakeLists / linker / undefined-symbol errors → `test-dev-cpp` (if test targets) or `cicd-dev-github`
- Increment `FIX_ROUND`, then retry from 4a.

If the build still fails after `[MAX_FIX_ITERATIONS]` fix rounds, stop and present the error to
the user with: "Build is failing after [N] fix attempts. Please review the errors below and advise
how to proceed."

#### 4b — Unit and integration tests

Run unit tests (no display required):

```bash
ctest --test-dir build -LE "integration|requires-opengl" --output-on-failure 2>&1
```

Run integration tests (no display required):

```bash
ctest --test-dir build -L "^integration$" --output-on-failure 2>&1
```

If any tests fail:

- Capture the **full test output** including assertion failure lines, expected vs actual values,
  and stack context.
- Identify the failing test names and their source files.
- Dispatch the appropriate fix agent with the full failure output:
  - Failing tests in `tests/ui/` or `tests/simulation/` → `graphics-dev-irrlicht`
  - Failing tests in `tests/audio/` → `sound-dev-opensoftal`
  - GMock/fixture setup failures → `test-dev-cpp`
- Increment `FIX_ROUND`, rebuild (step 4a), then rerun tests.

If tests still fail after `[MAX_FIX_ITERATIONS]` fix rounds, stop and present the failure to the
user with: "Tests are failing after [N] fix attempts. Please review the output below."

---

### Step 5 — Update checkboxes

Edit `implementation/phase-[PHASE_ID].md`: change `- [ ]` → `- [x]` for every deliverable that
has been implemented and verified by the passing build + test run.

Batch all checkbox updates into a single edit pass. Do not change any already-checked items or
prose outside the checkboxes.

Set `DELIVERABLES_IMPLEMENTED` to the count of items checked.

---

### Step 6 — Manual exit criteria (blocking)

Collect all **manual** exit criteria identified in Step 1 from the phase file's Exit Criteria
section. These are checks that require running the game (`./build/aitown`) and observing behavior.

Present them to the user:

```text
=== MANUAL VERIFICATION REQUIRED ===

The automated build and tests pass. The following exit criteria require you to run the game
and verify manually:

1. <criterion text verbatim from phase file>
2. <criterion text verbatim from phase file>
...

Run ./build/aitown and confirm each item, then tell me the results.
```

**Wait** for the user to respond before continuing.

If the user reports a failure:

- Ask them to describe the symptom in detail (what they see vs what they expect).
- Dispatch the appropriate fix agent with their description.
- After the fix, return to Step 4 (rebuild + retest), then return to Step 6 and re-present the
  remaining manual checks.

Only proceed to Step 7 once the user confirms all manual criteria pass (or explicitly accepts
any that cannot be verified in the current environment).

---

### Step 7 — Lint check

```bash
npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'
```

Fix any violations inline and re-run until the linter exits zero. Focus on files modified during
this phase (spec updates in Step 2 and checkbox updates in Step 5 are the most likely sources of
lint issues).

---

### Step 8 — Validate

Use the Skill tool to invoke `validate-phase-done` for `[TARGET_PHASE]`.

- **COMPLETE** (all VERIFIED, all criteria MET or UNVERIFIABLE): proceed to Step 9.
- **INCOMPLETE**: review each outstanding item.
  - If the gap is a missing code change: dispatch the appropriate fix agent, rebuild/retest, and
    re-invoke `validate-phase-done`.
  - If the gap is an UNVERIFIABLE criterion (requires human review): present it to the user and
    ask for confirmation.
  - Loop until `validate-phase-done` returns COMPLETE or all remaining items are user-confirmed.

---

### Step 9 — Commit and sign off

Commit all accumulated changes in a single commit:

```bash
git add -A
git commit -m "feat(phase-[PHASE_ID]): implement [PHASE_TITLE]"
```

Then use the Skill tool to invoke `mark-phase-done` for `[TARGET_PHASE]`.

Output the final summary:

```text
=== PHASE [PHASE_ID] COMPLETE ===

Phase: [PHASE_TITLE]
Spec updates applied: SPEC_UPDATES_APPLIED
Deliverables implemented: DELIVERABLES_IMPLEMENTED
Fix rounds (build/test): FIX_ROUND
Manual checks: verified by user

Phase [PHASE_ID] has been committed and marked DONE.
```

---

## Rules

- Read the phase file completely in Step 1 before dispatching any agent — never skip or skim items.
- Skip spec updates already marked "Already applied" — do not re-apply them.
- Do not update checkboxes (Step 5) until the build and tests pass (Step 4 succeeds).
- **Manual exit criteria are blocking** — do not proceed to Step 8 until the user explicitly
  confirms them.
- Never merge a PR — commit only (project policy: PRs are never merged without explicit user request).
- Commit on the current branch; do not create a new branch or force-push.
- Use the exact phase title from the `## Phase [ID]:` heading in the commit message.
- When a deliverable touches both audio and non-audio code, the audio agent handles `src/audio/`
  and `IAudioSystem.h`; the irrlicht agent handles everything else — include cross-domain context
  in both prompts.
- Do not add error handling, logging, docstrings, or refactoring beyond what the phase file
  specifies. Implement exactly what is written.
- If the same file is assigned to two different agents (rare), serialize those agents: complete
  one before launching the other, and pass the first agent's output as context to the second.
- If `[MAX_FIX_ITERATIONS]` is reached in Step 4, do not loop further — escalate to the user.
- **All Agent tool calls must set `model: "opus"`** — this applies to every specialist agent
  launched in Steps 2, 3, and 4 (spec agents, implementation agents, and fix agents).
