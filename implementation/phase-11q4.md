## Phase 11q4: Suppress `ValidationContext` Struct Size Warning

**Status: Planned**

**Prerequisite**: none. Fully independent.

### Goal

`src/benchmark/model_validator_main.cpp:146` has a local `ValidationContext` struct
with 25 fields — 5 over the SonarCloud `cpp:S1820` (max 20 fields) threshold.
This struct is internal to a standalone benchmark/tool binary, not shipped in the
production `aitown` build. Splitting it would add noise without benefit. The fix is a
targeted `// NOSONAR` annotation with a documented justification.

---

### Deliverables

- [ ] Open `src/benchmark/model_validator_main.cpp` and locate the `ValidationContext`
  struct declaration at approximately line 146.
- [ ] Add the `// NOSONAR` comment on the same line as the `struct` keyword (or on the
  line immediately above, whichever keeps the code readable):

  ```cpp
  // NOSONAR cpp:S1820 — ValidationContext is an internal tool struct in a
  // stand-alone benchmark binary; splitting it adds no maintenance benefit.
  struct ValidationContext {
  ```

  If the struct declaration spans the annotation in a way that SonarCloud does not
  recognise an inline comment, place `// NOSONAR cpp:S1820` on the struct declaration
  line itself:

  ```cpp
  struct ValidationContext { // NOSONAR cpp:S1820
  ```

- [ ] Run `make build` — confirm the file compiles without errors or new warnings.

---

### Exit Criteria

- [ ] `npx markdownlint-cli 'implementation/phase-11q4.md'` — no errors.
- [ ] All deliverable checkboxes above are checked.
- [ ] `make build` passes.
- [ ] SonarCloud re-scan shows `cpp:S1820` on
  `src/benchmark/model_validator_main.cpp:146` resolved (suppressed).
