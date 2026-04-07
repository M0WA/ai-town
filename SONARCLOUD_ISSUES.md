# SonarCloud Issues — Fix Plan (CRITICAL)

> Fetched: 2026-04-06 from project `M0WA_ai-town` (org: `m0wa`)
> BLOCKER: 0 | CRITICAL: 174 | MAJOR: 249 (not addressed here)

This document covers only **CRITICAL** severity issues (0 BLOCKERs exist).
MAJOR issues are excluded.

---

## SonarCloud CFamily Suppression — Important Note

**`// NOSONAR` and CFamily:**
SonarCloud CFamily (the C++ analysis engine used by this project via
`sonar.cfamily.compile-commands`) raises issues via a Clang-based AST engine. Older
CFamily plugin versions did NOT honor `// NOSONAR`. Support was introduced in more
recent SonarCloud hosted plugin versions (~2024+). This project uses sonar-scanner-cli
`7.0.2.4839` — at this version `// NOSONAR` **may** work depending on the CFamily
plugin version hosted by SonarCloud at scan time. **Test it first.** If a `// NOSONAR`
annotation on the offending line removes the issue from the next scan, use it — it is
simpler than `sonar.issue.ignore.multicriteria`. If it has no effect, fall back to the
approaches below. Do not rely on it blind.

**`sonar.exclusions` and CFamily:**
`sonar.exclusions` patterns are evaluated against the source file index. For `.cpp`
files that match an exclusion pattern, CFamily will skip analysis of that compilation
unit. For **header files**, behaviour depends on how CFamily attributes the issue:
- If the issue is attributed to the **header file itself** (e.g. `VRAMProfiler.h:23`),
  adding the header path to `sonar.exclusions` will suppress it — e.g.
  `sonar.exclusions="assets/**,build/**,tools/**,src/rendering/VRAMProfiler.h"`.
- If the issue is attributed to the **including `.cpp`** (CFamily reported it under the
  `.cpp` path, not the header), then excluding the header has no effect; you must either
  exclude the `.cpp` or use `sonar.issue.ignore.multicriteria`.

For S5028 in `VRAMProfiler.h`, the issues are attributed to the header file, so either
`sonar.exclusions` (add the header) or `sonar.issue.ignore.multicriteria` will work.
Use `sonar.issue.ignore.multicriteria` for targeted, rule-scoped suppression without
removing the file from analysis entirely. The current `sonarcloud.yml` already uses
`sonar.exclusions="assets/**,build/**,tools/**"` to skip those directories.

**Supported suppression mechanisms for CFamily C++ issues:**

1. **Mark as Accepted in the SonarCloud UI** — removes the issue from the quality gate
   provided the gate condition is scoped to `Open` status issues (the SonarCloud
   default). Issues remain visible in the list but do not fail the gate. Note: if the
   quality gate uses an "Overall Code" condition on any-status CRITICALs, accepted
   issues will still count — verify the gate condition first.
2. **`sonar.issue.ignore.multicriteria`** — scanner-level suppression by rule key +
   file path pattern (supports glob). Example in `sonar-project.properties`:
   ```
   sonar.issue.ignore.multicriteria=e1,e2
   sonar.issue.ignore.multicriteria.e1.ruleKey=cpp:S5028
   sonar.issue.ignore.multicriteria.e1.resourceKey=src/rendering/VRAMProfiler.h
   sonar.issue.ignore.multicriteria.e2.ruleKey=cpp:S5028
   sonar.issue.ignore.multicriteria.e2.resourceKey=src/benchmark/**
   ```
   The top-level `sonar.issue.ignore.multicriteria=e1,e2,...` key is **mandatory** — without
   it the scanner silently ignores all sub-entries. Each entry index (`e1`, `e2`, …) must
   appear in both the top-level comma-separated list and as the sub-property prefix.
   This project does not currently have a `sonar-project.properties`; these
   properties can be passed as `-D` flags to the scanner invocation in `sonarcloud.yml`.

---

## Summary by Rule

| Rule | Severity | Count | Description |
|---|---|---|---|
| `cpp:S134` | CRITICAL | 117 | Deep nesting (>3 levels of `if`/`for`/`do`/`while`/`switch`) |
| `cpp:S3776` | CRITICAL | 26 | Cognitive complexity exceeds 25 |
| `cpp:S5025` | CRITICAL | 13 | Raw `new`/`delete` — use smart pointers |
| `cpp:S5028` | CRITICAL | 5 | C-preprocessor macros — replace with `constexpr`/`enum` |
| `cpp:S1186` | CRITICAL | 5 | Empty method body without explanatory comment |
| `cpp:S5008` | CRITICAL | 3 | `void*` usage — replace with typed pointer |
| `cpp:S5421` | CRITICAL | 2 | Non-`const` global variable |
| `cpp:S7127` | CRITICAL | 2 | Use `std::size()` instead of `sizeof` array hack |
| `cpp:S3973` | CRITICAL | 1 | Missing curly braces on conditional body |
| **Total** | | **174** | |

S134 (117) + S3776 (26) = 143 of 174 = **82%** of all CRITICALs. Fixes 8–9 are the
dominant work. Fix 8 is expected to eliminate most S3776 issues and substantially
reduce co-located S134 violations, but the 117 S134 issues may include violations in
functions not targeted by Fix 8 — re-evaluate the residual S134 count after Fix 8
lands before scoping Fix 9 effort.

## Summary by File

| File | Count | Top rules |
|---|---|---|
| `src/simulation/CitySimulation.cpp` | 68 | S134, S3776 |
| `src/benchmark/model_validator_main.cpp` | 33 | S134, S3776, S5025, S5028, S7127 |
| `src/rendering/IrrlichtRenderer.cpp` | 20 | S5025, S134, S3776 |
| `src/benchmark/benchmark_main.cpp` | 16 | S134, S3776, S5028, S3973, S7127 |
| `src/terrain/TerrainSystem.cpp` | 12 | S134, S3776 |
| `src/rendering/BuildingAssetLoader.cpp` | 5 | S134, S5025, S3776 |
| `src/platform/EventReceiver.cpp` | 4 | S134, S3776 |
| `src/rendering/VRAMProfiler.h` | 3 | S5028 |
| `src/audio/AudioSystem.cpp` | 2 | S5008 |
| `src/platform/PlatformUtils.cpp/.h` | 2 | S5421 |
| Others (9 files) | 9 | S5008, S5025, S1186, S3776, S7127 |

---

## Fix Plan

### Fix 1 — `cpp:S1186`: Empty method bodies (5 issues) ← Genuinely trivial

**Files:**
- [src/interfaces/IRenderer.h:180](src/interfaces/IRenderer.h#L180)
- [src/rendering/BuildingShaderCallback.h:80](src/rendering/BuildingShaderCallback.h#L80)
- [src/rendering/RoadShaderCallback.h:87](src/rendering/RoadShaderCallback.h#L87)
- [src/rendering/TerrainShaderCallback.h:92](src/rendering/TerrainShaderCallback.h#L92)
- [src/terrain/terrain_types.h:13](src/terrain/terrain_types.h#L13)

**Action:** Add an inline comment inside each empty method body explaining the intent,
e.g. `/* intentionally empty — Irrlicht callback stub */`. No logic change required.

---

### Fix 2 — `cpp:S7127`: Use `std::size()` (2 issues) ← Genuinely trivial

**Files:**
- [src/benchmark/model_validator_main.cpp:196](src/benchmark/model_validator_main.cpp#L196)
- [src/benchmark/benchmark_main.cpp:646](src/benchmark/benchmark_main.cpp#L646)

**Action:** Replace `sizeof(arr)/sizeof(arr[0])` with `std::size(arr)`.

The canonical include for `std::size` is `<iterator>` (C++17). Both files already
include `<vector>`, which in libstdc++ and libc++ transitively provides `std::size`,
so no additional include is required in practice. However, add `#include <iterator>`
explicitly for strict conformance and portability.

---

### Fix 3 — `cpp:S3973`: Missing curly braces (1 issue) ← Genuinely trivial

**File:**
- [src/benchmark/benchmark_main.cpp:581](src/benchmark/benchmark_main.cpp#L581)

**Action:** Add `{` after the outer `for` at line 581 and a matching `}` after the
inner `for`'s closing brace at line 593. The outer `for (irr::u32 py ...)` at line 581
has no braces around its body. That body is the entire inner `for (irr::u32 px ...)`
statement at line 582, which already has its own `{ }` on lines 583–593. Only the
outer `for`'s wrapper braces are missing — do not add braces around the inner `for`'s
body (it already has them).

---

### Fix 4 — `cpp:S5421`: Non-`const` global variable (2 issues) ← Requires refactoring

**Files:**
- [src/platform/PlatformUtils.cpp:8](src/platform/PlatformUtils.cpp#L8)
- [src/platform/PlatformUtils.h:21](src/platform/PlatformUtils.h#L21)

**Analysis:** `g_assetsDir` is a `std::string` set once at program startup by
`main()` and read-only afterward. Simply adding `const` is not possible for a
runtime-initialized `std::string`.

**Important:** S5421 fires on **non-`const` variables with static storage duration**,
which includes both `extern`-linkage globals and file-scope `static` variables.
Renaming `g_assetsDir` to `static std::string s_assetsDir` does **not** satisfy the
rule — the variable is still a mutable global with static storage duration.

**Sonar-compliant fix:** Use a **function-local static** (Meyers-singleton style).
S5421 targets non-`const` variables at namespace or file scope; function-local statics
are function-scoped and are not expected to trigger S5421. However, verify this after
applying by running a scan — if the violation persists, fall back to "Mark as Accepted"
(see alternative below).

```cpp
// PlatformUtils.cpp
namespace {
    std::string& mutableAssetsDir() {
        static std::string s;
        return s;
    }
}
void setAssetsDir(std::string dir) { mutableAssetsDir() = std::move(dir); }
const std::string& getAssetsDir()  { return mutableAssetsDir(); }
```

```cpp
// PlatformUtils.h
void setAssetsDir(std::string dir);
const std::string& getAssetsDir();
```

All `g_assetsDir` read sites must be updated to `getAssetsDir()` and the one write
site in `main()` updated to `setAssetsDir(...)`. This is a moderate mechanical
refactor (~30–60 min) touching every file that currently reads `g_assetsDir`.

**Thread-safety contract:** `getAssetsDir()` returns a `const std::string&` to the
internal singleton string. `setAssetsDir()` must only be called **before** any thread
that reads the value (e.g. the audio thread in `AudioSystem`) is started. The current
design satisfies this — `g_assetsDir` is set once in `main()` before any subsystems
are constructed. The accessor refactor must preserve this single-write-before-all-reads
contract.

**Warning:** Never call `setAssetsDir()` after subsystem threads have started. The
function-local-static Meyers singleton provides **no synchronization on writes** — a
concurrent write to the singleton `std::string` while another thread holds a reference
from `getAssetsDir()` is a data race and undefined behaviour, not merely a usage
guideline violation. The read-only guarantee after startup is the sole thread-safety
mechanism.

**Important:** The `extern std::string g_assetsDir;` declaration at `PlatformUtils.h:21`
must be **explicitly removed** from the header as part of this refactor. Simply
adding the accessor functions is not sufficient — leaving the `extern` declaration in
the header means the S5421 violation attributed to `PlatformUtils.h:21` remains. After
the refactor, `PlatformUtils.h` should declare only `setAssetsDir` and `getAssetsDir`.

**Alternative:** Mark as Accepted in the SonarCloud UI — the intent (set-once
at startup) is documented and the mutability is by design.

---

### Fix 5 — `cpp:S5028`: Macros → `constexpr` (5 issues) ← False positives for GL extension guards

**Files:**
- [src/rendering/VRAMProfiler.h:23](src/rendering/VRAMProfiler.h#L23),
  [src/rendering/VRAMProfiler.h:26](src/rendering/VRAMProfiler.h#L26),
  [src/rendering/VRAMProfiler.h:29](src/rendering/VRAMProfiler.h#L29)
- [src/benchmark/model_validator_main.cpp:15](src/benchmark/model_validator_main.cpp#L15)
- [src/benchmark/benchmark_main.cpp:28](src/benchmark/benchmark_main.cpp#L28)

**Analysis:** All five macros are OpenGL extension constants defined inside
`#ifndef ... #endif` guards (e.g. `#ifndef GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX`).
This pattern is **required** — GLEW may or may not have already defined the symbol
depending on the version; the guard prevents a duplicate-definition compile error.
Replacing with `constexpr` variables would break this conditional override behavior
(you cannot `#ifndef` a `constexpr`). These are confirmed false positives.

**Note:** `// NOSONAR` support for CFamily C++ is version-dependent — see the
"SonarCloud CFamily Suppression" section at the top of this document for the
test-first guidance. Additionally, placing `// NOSONAR` on a `#define` line is
unreliable even when NOSONAR is otherwise supported, because the rule fires at AST
construction time before the comment can be associated with the macro node.

**Options:**

1. **Mark as Accepted in the SonarCloud UI** — lowest effort; confirmed false
   positives are the intended use case for this workflow.
2. **`sonar.issue.ignore.multicriteria`** — scanner-level suppression targeting
   `cpp:S5028` in the specific files. No code change required. Example:
   ```
   sonar.issue.ignore.multicriteria=e1,e2,e3
   sonar.issue.ignore.multicriteria.e1.ruleKey=cpp:S5028
   sonar.issue.ignore.multicriteria.e1.resourceKey=src/rendering/VRAMProfiler.h
   sonar.issue.ignore.multicriteria.e2.ruleKey=cpp:S5028
   sonar.issue.ignore.multicriteria.e2.resourceKey=src/benchmark/benchmark_main.cpp
   sonar.issue.ignore.multicriteria.e3.ruleKey=cpp:S5028
   sonar.issue.ignore.multicriteria.e3.resourceKey=src/benchmark/model_validator_main.cpp
   ```
   The top-level key listing all entry indices (`=e1,e2,e3`) is required.

---

### Fix 6 — `cpp:S5008`: `void*` usage (3 issues) ← Partially fixable; two sites are mandatory design

**Files:**
- [src/rendering/IrrlichtRenderer.h:237](src/rendering/IrrlichtRenderer.h#L237)
- [src/audio/AudioSystem.cpp:84](src/audio/AudioSystem.cpp#L84)
- [src/audio/AudioSystem.cpp:106](src/audio/AudioSystem.cpp#L106)

**Analysis per site:**

**`IrrlichtRenderer.h:237` — `void* m_cloudShaderCbRaw` ← Fixable:**
This member is `void*` to avoid pulling `CloudDomeShaderCallback`'s class definition
into the header. `CloudDomeShaderCallback` is defined as a **file-local class inside
`IrrlichtRenderer.cpp`** — it is NOT in any header.

Fix: add a forward-declaration in `IrrlichtRenderer.h` and change the member type:

```cpp
// IrrlichtRenderer.h — add forward declaration before the class
class CloudDomeShaderCallback;

// Change member:
CloudDomeShaderCallback* m_cloudShaderCbRaw{nullptr};
```

**Also update the destructor in `IrrlichtRenderer.cpp`:** after the type change, the
`static_cast<CloudDomeShaderCallback*>(m_cloudShaderCbRaw)->drop()` at line 129 becomes
a redundant same-type cast. Replace it with a direct call:

```cpp
// IrrlichtRenderer.cpp destructor (~line 129) — after type change:
if (m_cloudShaderCbRaw) {
    m_cloudShaderCbRaw->drop();   // cast removed; type is now CloudDomeShaderCallback*
    m_cloudShaderCbRaw = nullptr;
}
```

The full `CloudDomeShaderCallback` definition is visible at that point in the `.cpp`
(defined earlier in the file), so `->drop()` resolves correctly through `IReferenceCounted`.

**Critical constraint:** Do NOT extract `CloudDomeShaderCallback` to a new header
file. It must remain defined in `IrrlichtRenderer.cpp`. It is an implementation detail
of `IrrlichtRenderer` — exposing it in a separate header would make it part of the
public compilation surface without any benefit. The forward declaration
(`class CloudDomeShaderCallback;`) in `IrrlichtRenderer.h` is safe for a pointer member
— no other TU needs the full class definition.

**`AudioSystem.cpp:84` — `alcCheckError_real(void* device, ...)` ← Do NOT change:**
This signature is intentional. `al_check.h` uses `void*` to avoid including
`<AL/alc.h>` in the header, preserving the zero-AL-headers contract that allows test
TUs to compile without OpenAL hardware. Changing the parameter to `ALCdevice*` would
force `<AL/alc.h>` into `al_check.h`, breaking headless-CI compilation.
Action: **Mark as Accepted in the SonarCloud UI** or use
`sonar.issue.ignore.multicriteria` targeting `cpp:S5008` on `AudioSystem.cpp`.

**`AudioSystem.cpp:106` — `getProcAddress` returning `void*` ← Cannot be changed:**
`getProcAddress` wraps `alcGetProcAddress`, which returns `void*` by the OpenAL C API
contract. Additionally, `IAlcFunctions::getProcAddress` at
`src/interfaces/IAlcFunctions.h:33` is declared with `void*` return type —
`DefaultAlcFunctions::getProcAddress` is an `override` of that interface and must
match it exactly. These are two independent reasons this cannot be changed.
Action: **Mark as Accepted in the SonarCloud UI** or suppress via
`sonar.issue.ignore.multicriteria`.

---

### Fix 7 — `cpp:S5025`: Raw `new`/`delete` → smart pointers (13 issues) ← Partially fixable

**Analysis per site:**

**Group A — Irrlicht shader callbacks (must NOT use `unique_ptr`):**
`IrrlichtRenderer.cpp:1669` (`RoadShaderCallback`),
`IrrlichtRenderer.cpp:1743` (`TerrainShaderCallback`),
`IrrlichtRenderer.cpp:2859` (`CloudDomeShaderCallback`),
`model_validator_main.cpp:558` (`RoadShaderCallback`).

These follow the Irrlicht ref-counted `new` + `->drop()` ownership pattern.
`unique_ptr` causes double-free here because Irrlicht also calls `grab()` internally
on the pointer passed to `addHighLevelShaderMaterialFromFiles`.

Before accepting, verify each site has correct Irrlicht ref-counting:
- **Immediately-dropped callbacks** (lines 1669, 1743, `model_validator_main.cpp:558`):
  `->drop()` must be called on `cb` after `addHighLevelShaderMaterialFromFiles`.
  Note: `TerrainShaderCallback` at line 1743 has **two drop sites**:
  - **Early-return drop (line 1753):** fires when `gpu` (`IGPUProgrammingServices*`) is
    null (e.g. EDT_NULL driver — no GPU programming services). `cb->drop()` is at line
    1753 and `return;` is at line 1754 — the drop precedes the return statement.
  - **Unconditional post-call drop (line 1762):** called regardless of whether
    `addHighLevelShaderMaterialFromFiles` succeeds or fails (matType == -1). This single
    drop covers both the success and shader-compile-failure paths. There is no separate
    drop inside the matType == -1 failure block (lines 1764–1771).
  Both drop sites are correct and must be preserved when accepting or suppressing this issue.
- **Long-lived callbacks** (`IrrlichtRenderer.cpp:2859`, `CloudDomeShaderCallback`):
  the caller intentionally retains its reference (stored as `m_cloudShaderCbRaw`) and
  `->drop()` is called in the destructor. This is a valid alternative ownership pattern;
  the immediate-drop rule does not apply here.

Action: **Mark as Accepted in the SonarCloud UI** or suppress via
`sonar.issue.ignore.multicriteria` targeting `cpp:S5025` on the specific files.

**Group B — LODNode wrapper `delete` in destructor and eviction helpers, plus factory returns:**
`IrrlichtRenderer.cpp:161, 164, 173` (destructor `delete` loops in `~IrrlichtRenderer()`),
`IrrlichtRenderer.cpp:223, 1396, 2502` (`delete` in separate eviction helper methods — NOT in the destructor),
`IrrlichtRenderer.cpp:2244` (`return new LODNode(...)`),
`BuildingAssetLoader.cpp:235` (`return new LODNode(...)`).

All eight are plain C++ `LODNode` wrapper objects — not Irrlicht ref-counted. Lines 161, 164, and 173
are directly inside `~IrrlichtRenderer()`. Lines 223, 1396, and 2502 are in separate eviction helper
methods that also delete `LODNode*` values from `std::unordered_map<K, LODNode*>`.
The two factory sites return `LODNode*` to callers that own the object.

Converting to `std::unique_ptr<LODNode>` requires:
- Changing map types to `std::unordered_map<K, std::unique_ptr<LODNode>>` and
  updating every insertion, lookup, and iteration site across `IrrlichtRenderer`.
- Changing factory return types from `LODNode*` to `std::unique_ptr<LODNode>`.
- **Updating `evictLODNodeRegistry` template (critical):** The template at
  `IrrlichtRenderer.cpp:201–226` has signature
  `void evictLODNodeRegistry(std::unordered_map<KeyT, LODNode*>&)`. After the map
  type change its body requires three edits:
  1. Change the parameter type to `std::unordered_map<KeyT, std::unique_ptr<LODNode>>&`.
  2. Change `LODNode* lodNode = kv.second;` (line 206) to
     `LODNode* lodNode = kv.second.get();` — the raw pointer is still needed for the
     scene-graph eviction steps (lines 208–220).
  3. **Remove** `delete lodNode;` at line 223 — `registry.clear()` at line 225 already
     invokes each `unique_ptr` destructor, freeing the `LODNode`. Leaving the explicit
     `delete` causes a double-free.
  The two explicit instantiations at lines 231–234 need their parameter types updated
  to match the new signature but otherwise require no further changes.
- **Test impact:** any test code that stores a raw `LODNode*` obtained from these maps
  must be updated — the `unique_ptr` map owns the object and any externally held raw
  pointer becomes dangling as soon as the map entry is erased. Audit `tests/` for
  `LODNode*` usage before merging this refactor.

This is a **substantial refactor** (~2–4 hours), not a one-liner. Worth doing for
correctness (exception-safe destruction) but plan accordingly.

**Group C — `model_validator_main.cpp:1207` (`delete ln` in loop):**
Stores `LODNode*` in a `std::vector<LODNode*>`. Changing to
`std::vector<std::unique_ptr<LODNode>>` is self-contained to this benchmark tool.
Medium effort (~1 hour).

---

### Fix 8 — `cpp:S3776`: Cognitive complexity > 25 (26 issues) ← Requires decomposing monolithic functions

**Affected functions (highest complexity first):**

| File | Line | Complexity | Notes |
|---|---|---|---|
| [src/simulation/CitySimulation.cpp:3518](src/simulation/CitySimulation.cpp#L3518) | 3518 | 598 | `deserializeFromJson` — JSON deserializer, NOT a tick function |
| [src/benchmark/model_validator_main.cpp:321](src/benchmark/model_validator_main.cpp#L321) | 321 | 329 | Main validation/display loop |
| [src/benchmark/benchmark_main.cpp:181](src/benchmark/benchmark_main.cpp#L181) | 181 | 257 | Main benchmark loop |
| [src/simulation/CitySimulation.cpp:813](src/simulation/CitySimulation.cpp#L813) | 813 | 114 | `applyDesirabilityScores` — per-tile service-coverage scorer |
| [src/simulation/CitySimulation.cpp:1064](src/simulation/CitySimulation.cpp#L1064) | 1064 | 105 | `applyDensityUpgrade` — N×N footprint blocker-scan + demo loop |
| [src/simulation/CitySimulation.cpp:2144](src/simulation/CitySimulation.cpp#L2144) | 2144 | 78 | `placeZone` — footprint-guard + service-overlap checker |
| [src/platform/EventReceiver.cpp:18](src/platform/EventReceiver.cpp#L18) | 18 | 83 | Event dispatch |
| [src/terrain/TerrainSystem.cpp:360](src/terrain/TerrainSystem.cpp#L360) | 360 | 89 | |
| [src/terrain/TerrainSystem.cpp:678](src/terrain/TerrainSystem.cpp#L678) | 678 | 49 | |
| [src/rendering/IrrlichtRenderer.cpp:3229](src/rendering/IrrlichtRenderer.cpp#L3229) | 3229 | 55 | |
| [src/rendering/TextureCache.cpp:155](src/rendering/TextureCache.cpp#L155) | 155 | 48 | |
| [src/main.cpp:46](src/main.cpp#L46) | 46 | 47 | |
| Other functions (14) | — | 26–64 | |

**Action per function group:**

1. **`CitySimulation.cpp:3518` — `deserializeFromJson` (complexity 598):**
   This is a JSON deserializer, not a tick function. Decompose by extracting
   per-section parsing helpers: `parseVersion`, `parseTreasury`, `parseTaxRates`,
   `parseZones`, `parseRoads`, etc. Each helper handles one JSON section and fits
   within the complexity limit.
   **Test-seam guidance:** Two approaches, with different testability implications:
   - **Complexity reduction only** (not independently unit-testable): implement extracted
     helpers as `private static` methods of `CitySimulation` or as free functions in an
     anonymous namespace. Private static methods are not callable from external test TUs
     without friend declarations; anonymous-namespace functions have internal linkage and
     are invisible to test binaries. Choose this approach when only complexity reduction
     is needed.
   - **Independently unit-testable:** extract to a `CitySimulationSerializer` class with
     a public API, or to free functions in a named (non-anonymous) namespace in a
     separately-included header. This is the preferred approach if the parsing logic
     needs its own test coverage.

2. **`CitySimulation.cpp:813, 1064, 2144` — computation workers (not tick dispatchers):**
   These are per-subsystem computation functions, not thin orchestrators:
   - `applyDesirabilityScores` (813): extract per-service-type scoring helpers and the
     Chebyshev neighbourhood loop body into separate functions.
   - `applyDensityUpgrade` (1064): extract the N×N footprint blocker-scan and the
     demolition sub-pass into named helpers.
   - `placeZone` (2144): extract the footprint-guard pass and the service-overlap
     detector into separate functions, using early-return guards to flatten nesting.

3. **`benchmark_main.cpp:181` (257) and `model_validator_main.cpp:321` (329):**
   Extract each test-case block or validation pass into a named helper function.

4. **`EventReceiver.cpp:18` (83):** `OnEvent` already uses `if` chains over
   `event.EventType` and a `switch (event.MouseInput.Event)` for mouse sub-events —
   adding another dispatch table or top-level switch would duplicate structure that
   already exists. The complexity comes from deep inline logic within individual cases,
   not from missing dispatch structure. Correct approach:
   - Extract the mouse-event block into a private `handleMouseEvent(const irr::SEvent&)`
     helper and the keyboard block into a `handleKeyEvent(const irr::SEvent&)` helper,
     reducing the nesting depth inside `OnEvent` itself.
   - Further extract the RMB-up inline drag-state logic (lines 142–159) into a named
     helper to reduce per-case nesting within `handleMouseEvent`.

**Note:** Fixing S3776 on `CitySimulation.cpp` will eliminate most co-located S134
issues in the decomposed functions. However, some deeply-nested constructs inside
those functions may survive decomposition if they are in the extracted helpers rather
than eliminated (e.g. a single 4-deep loop nest extracted as its own helper still
fires S134 in that helper). Re-evaluate the residual S134 count after Fix 8 lands.

---

### Fix 9 — `cpp:S134`: Deep nesting > 3 levels (117 issues) ← Largely resolved by Fix 8; residual requires guard-clause refactoring

Most S134 issues are co-located with the monolithic functions targeted in Fix 8.
After Fix 8, a large fraction will disappear automatically. However, some S134
violations exist in shorter functions not targeted by S3776, and will persist.

**Re-evaluate the residual S134 count after Fix 8 lands** before scoping the
remaining effort.

**Known residual S134 issues** (outside Fix 8 function scope or in files not
decomposed by Fix 8):

| File | Lines with S134 |
|---|---|
| [src/simulation/CitySimulation.cpp](src/simulation/CitySimulation.cpp) | Residual S134 in functions outside Fix 8 scope — re-evaluate after Fix 8 lands |
| [src/rendering/IrrlichtRenderer.cpp](src/rendering/IrrlichtRenderer.cpp) | 213, 434, 2575, 3371–3392 |
| [src/rendering/BuildingAssetLoader.cpp](src/rendering/BuildingAssetLoader.cpp) | 218, 322, 330 |
| [src/terrain/TerrainSystem.cpp](src/terrain/TerrainSystem.cpp) | 206, 450–458, 556, 614, 793 |

**Action for residual cases:**

- **Early-return / guard clauses:** invert `if (cond) { ... long block ... }` to
  `if (!cond) { return/continue; }` to flatten nesting by one level.
- **Extract helper lambda or function** for the innermost loop body.
- **Combine consecutive `if` checks** on the same variable into a `switch`.

---

## Recommended Fix Order

| Priority | Fix # | Rule(s) | Actual effort | Notes |
|---|---|---|---|---|
| 1 | Fix 1 | S1186 | < 15 min | Comment-only change |
| 2 | Fix 2 | S7127 | < 15 min | 2-line mechanical change |
| 3 | Fix 3 | S3973 | < 5 min | Add braces around outer `for` body at line 581 |
| 4 | Fix 5 | S5028 | 30 min | All false positives — Mark as Accepted in UI or add `sonar.issue.ignore.multicriteria`; `// NOSONAR` on `#define` lines is unreliable even when CFamily NOSONAR support is present (rule fires at AST construction before comment association — see Fix 5 body and top-of-doc for details) |
| 5 | Fix 6 | S5008 | ~1 h | Forward-declare `CloudDomeShaderCallback` (keep class in `.cpp`); Mark the 2 OpenAL sites as Accepted in UI |
| 6 | Fix 7 | S5025 | 2–4 h | Mark Irrlicht shader callback sites as Accepted in UI; refactor LODNode maps to `unique_ptr` (audit tests first) |
| 7 | Fix 4 | S5421 | 1–2 h | Replace `g_assetsDir` with function-local-static Meyers-singleton (a naive file-scope static rename is NOT sufficient — see Fix 4 body); update all read sites |
| 8 | Fix 8 | S3776 | 2–4 days | Decompose monolithic functions; `CitySimulation.cpp:3518` is `deserializeFromJson`, not a tick fn |
| 9 | Fix 9 | S134 | Re-scope after Fix 8; residual ~1 day | Guard-clause refactoring; `CitySimulation.cpp` will have residual S134 after Fix 8 |

Fixes 1–3 are genuinely mechanical.
Fixes 4–7 each have a gotcha: S5028 macros are GL guards that cannot be `constexpr`;
`// NOSONAR` support in CFamily is version-dependent (test before committing to the
approach — see the suppression section at the top); the S5421 accessor must use a
function-local static (not a file-scope static); and most S5025 `new`/`delete` sites
are either Irrlicht-mandated (suppress via UI) or require a substantial map refactor.
Fixes 8–9 are the dominant work and together address 82% of the 174 CRITICALs.
