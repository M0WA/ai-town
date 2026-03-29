# Test Engineering Spec Review

**Scope**: `/workspace/architecture/testing/` (all files), plus
`/workspace/architecture/game-design/minimum-viable-simulation.md`,
`/workspace/architecture/game-design/save-system.md`,
`/workspace/architecture/ui-ux/ui-manager.md`,
`/workspace/architecture/ui-ux/input-arbitration.md`.

---

## File-by-File Findings

---

### `architecture/testing/framework.md`

---

**[GAP] — MEDIUM**
No test coverage spec for `ManualClock` itself.
`ManualClock` is a critical test double used in `NotificationManager`, `AudioSystem`,
`SettingsPanel`, and `UIManager` fixtures. Its behaviour (advance, nowSeconds) is assumed
correct but no self-test cases are specified the way they are for `ManualRNG`
(which has 6 named self-tests in `manual_rng_test.cpp`).
_Proposed resolution_: Add a `manual_clock_test.cpp` (or append to `manual_rng_test.cpp`)
with cases analogous to the ManualRNG self-tests: monotonicity, accumulation across
multiple advances, and verification that `nowSeconds()` returns the exact accumulated value.

---

**[GAP] — MEDIUM**
`framework.md` specifies `aitown_add_tests()` as the canonical registration helper but does
not specify what the macro must do when `LABEL` is not one of the three allowed values
(`unit`, `integration`, `requires-opengl`). Only the `!AITOWN_TEST_LABEL` (missing) guard
is shown; no guard rejects an invalid label string.
_Proposed resolution_: Add a `cmake_parse_arguments` validation block:
`if(NOT AITOWN_TEST_LABEL MATCHES "^(unit|integration|requires-opengl)$") → message(FATAL_ERROR ...)`.
Document this in the macro spec.

---

**[PROBLEM] — MEDIUM**
The "one-label-per-target" rule and the prohibition on mixing unit and integration tests in
one binary are documented for human readers only. The macro `aitown_add_tests()` does not
mechanically enforce this. A developer who adds an integration test source file to
`simulation_tests` (labelled `unit`) will pass CI without a warning.
_Proposed resolution_: Either enforce via CMake (naming convention guard or a source-file
allowlist property) or document explicitly that no mechanical guard exists and the rule
relies on code review.

---

**[GAP] — LOW**
The note about `audio_tests` Phase 10 extension lists 4 additional files
(`crossfade_interrupted_formula_test.cpp`, `stinger_milestone_test.cpp`,
`audio_stream_bar_boundary_test.cpp`, `notification_sfx_efx_bypass_test.cpp`) but does not
specify a corresponding `DISCOVERY_TIMEOUT` override for the Phase-10 expanded binary. The
Phase-3 `simulation_tests` binary overrides to 60 s with rationale ("8-file binary with
RapidCheck property tests under coverage instrumentation"). The Phase-10 `audio_tests`
binary will also have 8+ source files including RapidCheck tests; whether the default 30 s
discovery timeout is sufficient is unaddressed.
_Proposed resolution_: Explicitly state whether `audio_tests` needs a `DISCOVERY_TIMEOUT`
override after Phase 10, or confirm 30 s is adequate for the audio test binary.

---

### `architecture/testing/coverage.md`

---

**[INCONSISTENCY] — CRITICAL**
The system prompt and `CLAUDE.md` both state the coverage gate is **80%** ("`coverage gate:
lcov 80%`") and `coverage.md` Phase 5 section confirms the Phase 5 gate is 80%. However,
`coverage.md` also states the **target range is 95–98%** at the top (Phase 6+), and
`CLAUDE.md` confirms `make test` enforces **≥95%**. The system prompt in the agent
configuration header says "80% — Linux only". This creates an inconsistency: the agent
system prompt presents 80% as the standing gate, while `coverage.md` + `CLAUDE.md` agree
that 95% is the gate from Phase 6 onward.
_Proposed resolution_: Update the agent system prompt (the project instructions header, not
a spec file) to read "95% (Phase 6+, 80% at Phase 5, informational at Phase 4 and below)".
This is an agent-prompt inconsistency, not a spec-internal inconsistency; all three spec
files agree internally.

---

**[GAP] — HIGH**
The Phase 4 `src/ui/` 25% gate uses `lcov --list` output parsed with `awk -F'|'`.
`coverage.md` documents a preflight check that validates the `'|'` delimiter is present,
but the spec acknowledges: "if the format changes, $NF+0 coercion produces 0 → gate FAILS
with misleading '0% coverage' message." There is no equivalent preflight gate for Phase 5
and Phase 6 total-line coverage (which use the direct `.info` file `LH`/`LF` parser, not
`lcov --list`). If `lcov --capture` produces a `.info` file with zero entries (e.g., due
to a linker issue stripping `.gcda` files), the total coverage awk returns `0` and the gate
correctly fails — but the failure message says "0% < 80%" rather than "no coverage data
found". The Phase 6 spec adds a `src/simulation/ SF preflight` check but there is no
analogous preflight for `src/terrain/` or `src/ui/` SF entries at Phase 5 and 6.
_Proposed resolution_: Add preflight `grep -q "SF:.*src/terrain/"` and
`grep -q "SF:.*src/ui/"` checks before the Phase 5 and Phase 6 awk gates, mirroring the
`src/simulation/` preflight already specified for Phase 6.

---

**[GAP] — MEDIUM**
The coverage gate sequence (`lcov --remove → --list → genhtml → awk gate`) is specified in
`coverage.md` and in the `CLAUDE.md` running-tests section. The CI YAML itself is not
validated against `coverage.md` by any spec check. If the CI job is edited to reorder steps
(e.g., gate before genhtml), the HTML artifact is silently lost. There is no contract test
or CI lint step that verifies this ordering in the spec.
_Proposed resolution_: Document the mandatory step order as a numbered constraint (not just
prose) in `coverage.md`, and reference it from the CI architecture file as a constraint
that any CI job edit must preserve.

---

**[GAP] — MEDIUM**
`coverage.md` excludes `'*/src/rendering/*'`, `'*/src/audio/*'`, and `'*/src/platform/*'`
from the coverage gate but does NOT exclude `src/interfaces/`. The `IUIBackend.h`,
`ICitySimulation.h`, and `ITerrainRNG.h` pure-virtual interface headers live in
`src/interfaces/`. Coverage data attributed to inline virtual destructors, inline constants,
or struct definitions in these headers may appear in the filtered report and affect the
total percentage. `testability-architecture.md` notes "`src/interfaces/` is not excluded
from lcov, so coverage is captured correctly under the 80% gate" — but no spec describes
what happens when interface headers have partially-covered inline code (e.g.,
`kInvalidUIElement`, `Rect` struct members) that the tests do not exercise.
_Proposed resolution_: Add a note in `coverage.md` that `src/interfaces/` headers
contribute to the total gate, and specify whether inline-only headers (pure-virtual
interfaces) should be excluded or accepted as contributing denominator lines.

---

**[GAP] — LOW**
`coverage.md` specifies the `--ignore-errors` flag as `mismatch,inconsistent` in the
Phase 5 section, but `CLAUDE.md` (Running Tests section) adds `version` as a third comma-
separated value: `mismatch,inconsistent,version`. The divergence means a developer
following `coverage.md` exactly will get GCC/gcov version-mismatch stderr noise that
`CLAUDE.md` suppresses.
_Proposed resolution_: Update `coverage.md` to match `CLAUDE.md`: add `version` to the
`--ignore-errors` list and add a comment explaining it suppresses GCC/gcov version-string
differences.

---

**[DUPLICATE] — LOW**
The lcov exclusion patterns (`'/usr/*'`, `"*/.fetchcontent_cache/*"`, `'*/tests/*'`, etc.)
are specified verbatim in three places: `coverage.md` (local developer script),
`CLAUDE.md` (Running Tests section), and referenced from CI YAML. This triplication creates
drift risk — the Phase 10b addition of CamelCase `'*/Mock*.h'` and `'*/Manual*.h'` patterns
is documented in `coverage.md` but requires three synchronised updates. Currently all three
appear consistent, but the spec has no single-source-of-truth mechanism.
_Proposed resolution_: Designate `coverage.md` as the single source of truth for the
exclusion pattern list; `CLAUDE.md` and CI YAML cross-reference it rather than restating it.

---

### `architecture/testing/testability-architecture.md`

---

**[INCONSISTENCY] — HIGH**
`testability-architecture.md` (line 4 approximation) states:
> `IUIBackend.h` lives in `src/interfaces/` (moved from `src/ui/` in Phase 10b Feature 3)

`ui-manager.md` §IUIBackend Header Placement states:
> `IUIBackend.h` is placed in `src/ui/` (not `src/interfaces/`) because it is part of the
> UI subsystem abstraction boundary.

These two authoritative spec files directly contradict each other on the canonical location
of `IUIBackend.h`. The contradiction exists within the current checked-in spec.
_Proposed resolution_: Resolve to a single canonical location. The Phase 10b Feature 3
intent (per `testability-architecture.md`) is to move it to `src/interfaces/`. The
`ui-manager.md` statement predates this plan. Update `ui-manager.md` to remove the
conflicting statement and cross-reference `testability-architecture.md` as the authority.

---

**[GAP] — HIGH**
No mock contract is specified for `ISaveSystem`. `save-system.md` defines `ISaveSystem`
with `loadMostRecentSave()`, `autoSave()`, and `saveToSlot(int)`. Tests that exercise
`UIManager`'s unsaved-changes quit flow, load-game flow, and auto-save tick wiring need a
`MockSaveSystem`. The spec documents the `UIManager`→`SaveSystem` wiring contract but
defines no mock, no test file location, and no named test cases for the save system's
`UIManager` integration paths.
_Proposed resolution_: Add a `MockSaveSystem` contract to `testability-architecture.md`
(source location: `tests/ui/MockSaveSystem.h`); add named test cases for
`UnsavedChanges_QuitToDesktop_ShowsModal`, `SaveSystem_AutoSave_TriggersOnBudgetTick5`,
and `SaveSystem_LoadResult_Corrupted_StaysInMainMenu`.

---

**[GAP] — HIGH**
No test coverage is specified for the `SaveSystem` itself — `save-system.md` defines
detailed serialization requirements (population milestone flags, speed multiplier, building
variant counters, `LoadResult` enum) but `testability-architecture.md` does not document
a `save_system_test.cpp` fixture, mock contracts for `ICitySimulationSerializable`, or the
`save_system_real_test.cpp` companion file.
`coverage.md` §Coverage Test Placement Convention references `save_system_real_test.cpp`
as an example of the stub/real split pattern, but this is only a naming example — no test
cases, fixture setup, or round-trip invariants are specified.
_Proposed resolution_: Add a `SaveSystemTest` section to `testability-architecture.md`
specifying: fixture setup (MockClock, temporary directory injection), named test cases
(`SaveSystem_RoundTrip_PreservesFullCityState`, `SaveSystem_LoadResult_NoSaveFound`,
`SaveSystem_LoadResult_Corrupted`), and the stub/real split pattern.

---

**[GAP] — HIGH**
No test coverage is specified for `EventReceiver` — the class that translates Irrlicht
`SEvent` to `InputEvent` and synthesises `EGET_BUTTON_CLICKED` → `MouseButtonDown` events.
`input-arbitration.md` defines an extremely detailed `EventReceiver` contract (RMB drag
tracking `m_rmbDragActive`/`m_rmbMoved`, button-click synthesis, RMB-up always forwarded
to CameraController). These are pure logic decisions that could be tested without a display
if `EventReceiver` is given a seam to inject a callback instead of holding a raw
`CameraController*` and `UIManager*`.
_Proposed resolution_: Either (a) add `EventReceiver` to the testability architecture with
an injection seam for the callback layer, or (b) explicitly document that `EventReceiver`
is tested via integration tests requiring EDT_NULL with the full stack.

---

**[GAP] — MEDIUM**
`ICitySimulation` exposes `getTrafficDemandFactor(ZoneType)` (added in Phase 11 for save
round-trip tests) but no test fixture or named test case is specified for this method's
contract. The comment says it is exposed "solely for Phase 11 save/load round-trip tests"
but no corresponding test structure is specified.
_Proposed resolution_: Add a named test case `SaveSystem_RoundTrip_PreservesTrafficRollingWindow`
to the save system test spec section.

---

**[GAP] — MEDIUM**
`testability-architecture.md` specifies `MockCitySimulation` lives in
`tests/ui/MockCitySimulation.h`, but `ICitySimulation` also has simulation-domain users
(e.g., `UIManagerDeficitIntegrationTest` uses `MockCitySimulation` from `tests/ui/`). The
spec does NOT address whether simulation tests that need a lightweight `ICitySimulation`
mock (e.g., for testing cross-subsystem paths that originate in `CitySimulation`) should
also use `tests/ui/MockCitySimulation.h` or maintain a separate simulation-domain copy.
This creates ambiguity about whether `simulation_tests` CMake target can include
`tests/ui/MockCitySimulation.h` without violating the include-directory separation.
_Proposed resolution_: Explicitly document whether `MockCitySimulation` is shared across
`simulation_tests` and `ui_tests` targets, and if so, confirm both targets have the
`tests/ui/` path in their `target_include_directories`.

---

**[GAP] — MEDIUM**
The `SettingsPanelTest` fixture specifies `StrictMock<MockAudioSystem>` for the three
volume-control tests but does not specify a mock for `IKeyBindings` or the file I/O seam
(`keybindings.json` read/write). The `KeyBindings` test cases (#1–#5) require file I/O
isolation (the spec says "keybindings.json is NOT written" for conflict cases, and "written
exactly once" for swap). There is no `IKeyBindingsStorage` seam defined, meaning tests
would actually write to the filesystem at test time.
_Proposed resolution_: Define an `IKeyBindingsStorage` seam (or document the
`KeyBindings::setStorageRoot(path)` pattern) to allow tests to redirect writes to a
temporary directory. Specify the seam in `testability-architecture.md`.

---

**[GAP] — MEDIUM**
No property-based tests are specified for the zoning desirability invariant beyond the
single bullet: "Desirability scores must remain in [0, 100] for any valid zone + adjacency
configuration." No generator strategy, no GTest fixture class name, no file location
(`tests/simulation/zoning_test.cpp`?), and no RapidCheck generator for `ZoneAdjacencyConfig`
are documented.
_Proposed resolution_: Expand the zoning invariant in `property-based-tests.md` to include
fixture name, generator strategy, and specific adjacency edge cases (all-same zone, mixed
R/C/I, zone with no neighbours).

---

**[PROBLEM] — MEDIUM**
The `UIManagerDeficitIntegrationTest` fixture mandates `TearDown()` resets `ui_` before
mock destruction. The eight test cases in this fixture all use `NiceMock<MockAudioSystem>`
and `NiceMock<MockUIBackend>`. However, the spec does not specify the `TearDown()` reset
order when BOTH `MockUIBackend` AND `MockAudioSystem` are held by the fixture. The comment
says "MockUIBackend destruction while UIManager holds a pointer causes use-after-free" but
does not address whether `MockAudioSystem` destruction order matters. If `UIManager` also
calls audio methods during its destructor (e.g., stopping a playing sound on teardown),
`MockAudioSystem` being destroyed before `UIManager` is reset could produce a dangling
pointer.
_Proposed resolution_: Explicitly document the required TearDown order for all multi-mock
fixtures: reset `ui_` (or the class-under-test) to `nullptr` first, then let mocks destruct
in declaration-reverse order.

---

**[GAP] — LOW**
The `QueryPanel` test cases (#1–#4 in `testability-architecture.md`) test
`computePanelPosition()` as a pure function. But `InspectorPanel::populate()` — which
creates UI elements via `IUIBackend` — has no named test cases specified for it, other than
being mentioned as a call site. `populate()` exercises `addStaticText`, `setElementText`,
`setElementMonoFont`, `setElementTextColor` — high-priority coverage paths for the 95% gate.
_Proposed resolution_: Add named test cases for `InspectorPanel::populate()` covering
road tile, zone tile, empty tile, and service building results.

---

**[GAP] — LOW**
`testability-architecture.md` does not specify test cases for `UIManager::setLoadingTerrain(bool)`.
This method gates the entire `update()` dispatch loop. A missing test means the loading
guard could be inadvertently removed without breaking any test.
_Proposed resolution_: Add a named test case:
`UIManager_LoadingTerrainGate_UpdateReturnsEarlyWithoutPolling` — verify that when
`setLoadingTerrain(true)`, a subsequent `ui_.update(dt)` call does NOT invoke
`sim_.pollPendingNotification()` or `sim_.getConsecutiveDeficitMonths()`.

---

### `architecture/testing/headless-ci-testing.md`

---

**[GAP] — MEDIUM**
`headless-ci-testing.md` specifies the three CTest filter commands for CI but does not
document the expected minimum test count for each label bucket. If label routing is broken
(e.g., all tests end up unlabelled), `ctest -LE "integration|requires-opengl"` still passes
(it runs everything). The Phase 1 integration routing verification step ("at least 1 test")
is mentioned in `framework.md` but not in this file, and no equivalent minimum-count
requirement is stated for the `unit` and `requires-opengl` labels.
_Proposed resolution_: Add a `Minimum test count verification` section that documents the
expected minimum count per label and the CI step that enforces it (or cross-reference the
label routing verification step from `framework.md` and CI YAML).

---

**[GAP] — LOW**
The containerised CI section (Phase 11b) describes the temporary `test-container-xvfb` job
but does not specify what happens if the Phase 11b spike PR fails that job. There is no
documented rollback plan or fallback to non-container CI for the `requires-opengl` tests.
_Proposed resolution_: Add a one-line note: "If `test-container-xvfb` fails, the Phase 11b
container migration is blocked; `build-linux` and `coverage-linux` remain on non-container
mode until the xvfb issue is resolved."

---

### `architecture/testing/property-based-tests.md`

---

**[GAP] — HIGH**
The traffic invariant is one bullet: "For any connected road graph, pathfinding must return
a path of finite length for any source/destination pair." No generator strategy is
specified, no fixture class name, no test file (`tests/simulation/traffic_test.cpp`?), no
RapidCheck generator for road graph topologies (grid, ring, tree, disconnected), and no
treatment of disconnected source/destination pairs (which should presumably return "no path"
rather than "finite path"). The invariant as written is incomplete — it does not distinguish
reachability from connectivity.
_Proposed resolution_: Expand the traffic invariant to specify: graph topology generator,
expected return type when no path exists, whether the invariant tests only connected graphs
or also disconnected cases, and the GTest fixture name.

---

**[GAP] — HIGH**
No property-based tests are specified for population growth or density unlocking. The
population growth invariant should cover: population is monotonically non-decreasing while
demand is positive; population does not grow beyond map capacity; density tier unlocks fire
at the correct consecutive-month threshold. These are simulation invariants with combinatorial
input spaces that are well-suited to RapidCheck.
_Proposed resolution_: Add a "Population growth invariants" subsection to
`property-based-tests.md` with at least: (a) a monotonicity invariant and (b) a density
unlock firing-threshold invariant.

---

**[GAP] — MEDIUM**
The economy property-based tests are extremely detailed (correct, good coverage), but the
`BudgetDeficitWarn` notification dispatching is not covered by any property test. The
dispatch condition (`budget_surplus_pct ≤ −0.25`) is a threshold gate that interacts with
RNG (service degradation), loan issuance, and tick timing — a property test verifying that
the warn notification is enqueued exactly once per qualifying tick (not zero times, not
twice) would improve confidence.
_Proposed resolution_: Add a `BudgetWarn_QueuedExactlyOncePerQualifyingTick` RapidCheck
property to `property-based-tests.md` and assign it to `tests/simulation/economy_test.cpp`.

---

**[GAP] — LOW**
The `LoanGate_FiresAtExactly120Seconds` fixed-seed boundary test specifies IEEE 754 integer
advances (`119.0 + 1.0`) to avoid floating-point precision issues. However, the spec does
not address what happens at the boundary of `ManualClock` overflow: if a test advances the
clock by a very large double value (e.g., `1e308`), `nowSeconds()` would return a `double`
near infinity, and the `>= 120.0` check still passes (infinity >= 120.0 is true). This is
an unlikely but latent edge case for clock tests that use large advances.
_Proposed resolution_: Add a `ManualClock` overflow / saturation policy note (or confirm
that `ManualClock::nowSeconds()` is expected to return IEEE 754 infinity for very large
cumulative advances, and that this is acceptable in test contexts).

---

### `architecture/testing/procedural-generation-seeds.md`

---

**[MISSING] — HIGH**
`procedural-generation-seeds.md` is 5 lines and functions only as a summary pointer to
`property-based-tests.md`. It does not document: the seed registration table (which fixed
seeds are pinned and why), the process for pinning a new seed after a RapidCheck failure,
whether seeds are shared across terrain, simulation, and audio tests or kept per-domain,
or the relationship between the seed used for `std::mt19937_64` initialization and the
`uint64_t` parameter accepted by `TerrainGenerator`.
_Proposed resolution_: Expand this file significantly. Add: (a) a seed registry table with
at least the primary terrain regression seed `0xDEADBEEF00000001` documented with the
reason it was chosen; (b) the workflow for adding a new regression seed after a CI failure;
(c) the policy on seed reuse across test domains.

---

**[GAP] — MEDIUM**
There is no spec for how RapidCheck's own seed is controlled in CI. RapidCheck by default
uses a time-based seed, which means property test failures are non-reproducible without the
printed hex seed. The spec says "print `// Reproduce with seed: 0x<hex>`" but does not
document where this output appears in CI logs, whether GTEST_OUTPUT XML captures it, or
whether CI should use a fixed RapidCheck seed (via `RC_PARAMS=seed=<value>`) for
reproducible CI runs.
_Proposed resolution_: Add a policy on whether CI uses a fixed or random RapidCheck seed
and document how to extract the failing seed from CI logs (grep pattern, log line format).

---

## Cross-File Findings

---

**[INCONSISTENCY] — HIGH**
`testability-architecture.md` states `IUIBackend.h` moved to `src/interfaces/` in Phase
10b Feature 3. The same file states `MockUIBackend` lives in `tests/ui/MockUIBackend.h`
(renamed from lowercase in Phase 10b). `ui-manager.md` §IUIBackend Header Placement
states `IUIBackend.h` is in `src/ui/`. These two files give different answers for the same
file. This is distinct from issue TA-01 above: it also impacts `coverage.md` which notes
"`src/interfaces/` is not excluded from lcov, so coverage is captured correctly under the
80% gate" — if `IUIBackend.h` were in `src/ui/` instead, it would be in the gate scope
under `src/ui/` rather than `src/interfaces/`, changing which exclusion pattern applies.
_See_ "testability-architecture.md [INCONSISTENCY] — HIGH" above for the primary entry.

---

**[MISSING] — HIGH**
No integration test scope is specified for the full `UIManager` → `CitySimulation` →
`SaveSystem` round-trip. `testability-architecture.md` defines unit-level fixtures for each
subsystem in isolation, but the integration test spec (`tests/integration/`) only covers
the Phase 1 compile-check. No integration test is specified that exercises the complete
"place zone → budget tick → deficit notification → modal → dismiss → save" flow using the
EDT_NULL Irrlicht device. This is the most important end-to-end path for city simulation
correctness, and it has no spec-level integration test requirement.
_Proposed resolution_: Add an integration test spec section to either
`testability-architecture.md` or `headless-ci-testing.md` defining at least one full-stack
integration test fixture (`CitySimulationIntegrationTest`) with EDT_NULL Irrlicht, null
audio, and real `CitySimulation` + `UIManager` instances (no mocks for the domain logic).

---

**[MISSING] — HIGH**
No test coverage is specified for the `SaveSystem` ↔ `UIManager` wiring described in
`save-system.md`. Specifically:
- Auto-save on budget tick 5 (`consumeBudgetTicks()` return value forwarded to
  `SaveSystem::onBudgetTick()`)
- Auto-save on forced loan dialog activation (before modal is shown)
- "Load Last Save" button grayed when `LoadResult::NoSaveFound`
- Unsaved changes indicator (amber dot) toggled by `setUnsavedChanges()`

None of these paths have named test cases. All four are exercised by `UIManager::update()`
and can be tested with `MockSaveSystem` + `NiceMock<MockCitySimulation>` without a display.
_Proposed resolution_: Add a `UIManagerSaveIntegrationTest` fixture in
`testability-architecture.md` with the four named test cases above.

---

**[MISSING] — HIGH**
`save-system.md` specifies three serialized fields that are Phase 11 scope:
`population_milestone_fired`, `speed_multiplier`, and `building_variant_counters`. A single
named test is referenced: `SaveSystem_RoundTrip_PreservesFullCityState`. However:
1. No fixture setup is documented (which class provides the simulation state? how is a
   temporary save directory injected?).
2. No failure mode test is documented (what if the JSON is missing the
   `building_variant_counters` key — backward compatibility from Phase 10 saves?).
3. No checksum/corruption test is documented (for `LoadResult::Corrupted`).

All three gaps exist at the spec level.
_Proposed resolution_: Expand the save system test spec to cover backward-compatibility
deserialization (missing optional keys use defaults) and corruption detection.

---

**[INCONSISTENCY] — MEDIUM**
`testability-architecture.md` says `NotificationManager::dismissCriticalToast(UIElementHandle)`
"is the production API called by the UI event handler when the player clicks, presses Enter,
or presses Delete on a CRITICAL toast; it is not a test-only backdoor."
`input-arbitration.md` §Priority 2 states CRITICAL toast dismiss fires "click, Enter, or
Delete" but does not say who calls `dismissCriticalToast`. The event routing from
`UIManager::onEvent()` at Priority 2 to `NotificationManager::dismissCriticalToast()` is
unspecified in `input-arbitration.md` — it is only documented in `testability-architecture.md`.
Tests that verify the Priority 2 dismiss path (as an event-routing test) would need to
exercise `UIManager::onEvent()` with Enter/Delete input events and verify
`dismissCriticalToast()` is called. No such test is specified.
_Proposed resolution_: Add named test cases for Priority 2 event routing:
`Priority2_EnterKey_DismissesCriticalToast` and
`Priority2_ModalActive_EnterKey_DoesNotDismissCriticalToast` to
`testability-architecture.md` (or the world_interaction_test.cpp mapping section).

---

**[MISSING] — MEDIUM**
No test coverage spec exists for the `input-arbitration.md` §RMB drag suppression in
non-gameplay states. The spec says `EventReceiver` guards RMB down with
`UIManager::isGameplayOrPaused()` — but `UIManager::isGameplayOrPaused()` is not listed
as a method on `ICitySimulation` or `UIManager` in `testability-architecture.md`. It is
unclear whether this method is on `UIManager` directly (concrete, not behind an interface)
or needs to be tested via `EventReceiver` integration.
_Proposed resolution_: Document whether `isGameplayOrPaused()` is tested via a unit test
on `UIManager` or only via integration. If unit-testable, add it to the world_interaction
test mapping.

---

**[GAP] — MEDIUM**
`minimum-viable-simulation.md` specifies three map sizes (Small 128×128, Medium 512×512,
Large 1024×1024). No test coverage spec addresses map-size parameterisation. The terrain
generator tests use fixed seeds but do not parameterise over map size. A Large 1024×1024
map may exercise different code paths (chunk boundaries, BFS distance limits for service
coverage) than the default Medium size.
_Proposed resolution_: Add a note in `property-based-tests.md` or the terrain generator
section of `testability-architecture.md` specifying which tests are parameterised over
map size vs. which assume the default Medium size.

---

**[INCONSISTENCY] — MEDIUM**
`testability-architecture.md` specifies `MockAudioSystem` lives in
`tests/simulation/MockAudioSystem.h`. The `UIManagerDeficitIntegrationTest` section says:
"Include path: `MockAudioSystem` is in `tests/simulation/MockAudioSystem.h` (NOT
`tests/ui/` — audio mocks live alongside simulation mocks)."
However, the `SettingsPanelTest` fixture section says `StrictMock<MockAudioSystem>` is
used but does not explicitly state the include path. The `audio_tests` target includes
`tests/simulation/` in its include directories (per `framework.md`), but `ui_tests` may
not include `tests/simulation/` by default. If `ui_tests` does not include
`tests/simulation/`, the `SettingsPanelTest` and `NotificationSFX` test files will fail
to find `MockAudioSystem.h`.
_Proposed resolution_: Verify that `tests/simulation/` is listed in
`target_include_directories(ui_tests ...)` (per `framework.md`, it is listed as
`tests/simulation/`). Confirm this explicitly in `testability-architecture.md`'s
`SettingsPanelTest` fixture section.

---

**[MISSING] — MEDIUM**
`input-arbitration.md` documents the Hover State Switching section (IGUIButton Image Swap
via `EGET_ELEMENT_HOVERED`/`EGET_ELEMENT_LEFT`). This logic lives in `IrrlichtUIBackend`
(a rendering-layer class). No test coverage is specified for it. Hover state switching is
an `IrrlichtUIBackend`-internal concern and cannot be tested through `IUIBackend` (which
has no hover methods). This path would only be exercised by integration or OpenGL tests.
_Proposed resolution_: Either add a `requires-opengl` integration test for hover state
switching to the `opengl_tests` target, or explicitly document that this is untestable
headlessly and accepted as an untested rendering detail.

---

**[GAP] — LOW**
`save-system.md` documents "Quit to Desktop / Quit to Main Menu safety" — a blocking
`ModalDialog::showUnsavedQuit()` with three options (Save and Quit, Quit Without Saving,
Cancel). None of the three action paths have named test cases in `testability-architecture.md`.
The `UnsavedChanges_QuitToDesktop_ShowsModal` gap is identified above as a missing
`MockSaveSystem` test, but the `Quit Without Saving` (no modal for no unsaved changes) and
`Cancel` (modal dismissed, game continues) paths are also unspecified.
_Proposed resolution_: Add three named test cases for the quit-safety modal paths to the
`UIManager` test coverage section.

---

**[GAP] — LOW**
The `UIManager::transitionToMainMenu()` call order is specified in `ui-manager.md`:
(1) `m_audio->transitionToMainMenu()`, (2) `onNewGame()`, (3) save-state refresh,
(4) `m_mainMenu->show()`. No test verifies this ordering. A test using `::testing::InSequence`
on `MockAudioSystem` and `MockUIBackend` would catch regressions where, e.g., `m_mainMenu->show()`
is called before `m_audio->transitionToMainMenu()`.
_Proposed resolution_: Add a named test case `UIManager_TransitionToMainMenu_CallOrder`
using `InSequence` to enforce the four-step order.

---

**[DUPLICATE] — LOW**
The `IUIBackend` interface definition (all 21 methods with doc comments) appears verbatim
in both `testability-architecture.md` (test-facing authority) and `ui-manager.md`
(production-facing authority). Both files state they "must remain consistent". The duplicate
creates a maintenance obligation with no mechanical enforcement. The Phase 10b addition of
method 21 required updating both files simultaneously — any future method addition carries
the same dual-update burden.
_Proposed resolution_: Consider moving the canonical interface definition to a single file
(e.g., a dedicated `src/interfaces/IUIBackend.md`) with `testability-architecture.md` and
`ui-manager.md` cross-referencing it. If duplication must be maintained, add a CI lint step
that counts virtual methods in both spec blocks and fails if they diverge.

---

## Summary Table

| # | File | Category | Severity | Title |
|---|---|---|---|---|
| 1 | framework.md | GAP | MEDIUM | No self-tests specified for ManualClock |
| 2 | framework.md | GAP | MEDIUM | aitown_add_tests() does not validate label value |
| 3 | framework.md | PROBLEM | MEDIUM | One-label-per-target rule has no mechanical enforcement |
| 4 | framework.md | GAP | LOW | audio_tests Phase 10 binary DISCOVERY_TIMEOUT unaddressed |
| 5 | coverage.md | INCONSISTENCY | CRITICAL | Agent system prompt states 80% gate; spec states 95% from Phase 6 |
| 6 | coverage.md | GAP | HIGH | No src/terrain/ and src/ui/ SF preflight for Phase 5/6 gates |
| 7 | coverage.md | GAP | MEDIUM | Step ordering constraint not specified as a numbered rule |
| 8 | coverage.md | GAP | MEDIUM | src/interfaces/ coverage contribution not documented |
| 9 | coverage.md | GAP | LOW | --ignore-errors missing 'version' flag vs CLAUDE.md |
| 10 | coverage.md | DUPLICATE | LOW | lcov exclusion patterns tripled across coverage.md/CLAUDE.md/CI |
| 11 | testability-architecture.md | INCONSISTENCY | HIGH | IUIBackend.h location contradicts ui-manager.md |
| 12 | testability-architecture.md | GAP | HIGH | No MockSaveSystem contract specified |
| 13 | testability-architecture.md | GAP | HIGH | No SaveSystemTest fixture, mock contracts, or round-trip test spec |
| 14 | testability-architecture.md | GAP | HIGH | No EventReceiver testability seam or test spec |
| 15 | testability-architecture.md | GAP | MEDIUM | getTrafficDemandFactor test case not specified |
| 16 | testability-architecture.md | GAP | MEDIUM | MockCitySimulation cross-target usage not clarified |
| 17 | testability-architecture.md | GAP | MEDIUM | No IKeyBindingsStorage seam for KeyBindings file-I/O isolation |
| 18 | testability-architecture.md | GAP | MEDIUM | Zoning invariant has no generator strategy or fixture name |
| 19 | testability-architecture.md | PROBLEM | MEDIUM | Multi-mock TearDown order not fully specified for audio+UI fixtures |
| 20 | testability-architecture.md | GAP | LOW | InspectorPanel::populate() has no named test cases |
| 21 | testability-architecture.md | GAP | LOW | setLoadingTerrain() gate has no named test case |
| 22 | headless-ci-testing.md | GAP | MEDIUM | Minimum test count per label bucket not specified |
| 23 | headless-ci-testing.md | GAP | LOW | Phase 11b xvfb container spike has no rollback plan |
| 24 | property-based-tests.md | GAP | HIGH | Traffic invariant incomplete (no generator, no disconnected-graph case) |
| 25 | property-based-tests.md | GAP | HIGH | No population growth or density-unlock property tests |
| 26 | property-based-tests.md | GAP | MEDIUM | BudgetDeficitWarn dispatch not covered by any property test |
| 27 | property-based-tests.md | GAP | LOW | ManualClock overflow/saturation policy not documented |
| 28 | procedural-generation-seeds.md | MISSING | HIGH | File is 5 lines; no seed registry, pinning workflow, or domain policy |
| 29 | procedural-generation-seeds.md | GAP | MEDIUM | RapidCheck CI seed policy not documented |
| 30 | Cross-file | INCONSISTENCY | HIGH | IUIBackend.h location (src/ui/ vs src/interfaces/) — see items 11/5 |
| 31 | Cross-file | MISSING | HIGH | No integration test spec for full UIManager→CitySimulation→SaveSystem round-trip |
| 32 | Cross-file | MISSING | HIGH | SaveSystem↔UIManager wiring paths have no named test cases |
| 33 | Cross-file | MISSING | HIGH | Phase 11 save fields (milestone flags, speed multiplier, variant counters): no fixture spec |
| 34 | Cross-file | INCONSISTENCY | MEDIUM | Priority 2 CRITICAL dismiss routing to NotificationManager not tested |
| 35 | Cross-file | MISSING | MEDIUM | RMB drag suppression guard (isGameplayOrPaused) not test-specified |
| 36 | Cross-file | GAP | MEDIUM | Map size not parameterised in terrain/service-coverage tests |
| 37 | Cross-file | INCONSISTENCY | MEDIUM | MockAudioSystem include path not confirmed in ui_tests target_include_directories |
| 38 | Cross-file | MISSING | MEDIUM | Hover State Switching (IrrlichtUIBackend) has no test plan |
| 39 | Cross-file | GAP | LOW | Quit-safety modal three paths not specified as named test cases |
| 40 | Cross-file | GAP | LOW | transitionToMainMenu() call order not verified with InSequence test |
| 41 | Cross-file | DUPLICATE | LOW | IUIBackend 21-method definition duplicated in testability-architecture.md and ui-manager.md |
