# SonarCloud Fixes 1–7 — Implementation Plan

> Source of truth: [`SONARCLOUD_ISSUES.md`](SONARCLOUD_ISSUES.md)
> Scope: 31 CRITICAL issues addressed by Fixes 1–7 (143 remaining in Fixes 8–9)
> Prerequisite: Read the **SonarCloud CFamily Suppression** section at the top of
> `SONARCLOUD_ISSUES.md` before starting — it governs all suppression decisions.

---

## Execution Order

| # | Fix | Rule | Issues | Effort | Type |
|---|---|---|---|---|---|
| 1 | [Fix 1](#fix-1--s1186-empty-method-bodies) | S1186 | 5 | < 15 min | Code comment |
| 2 | [Fix 2](#fix-2--s7127-sizeof-array-hack) | S7127 | 2 | < 15 min | Mechanical |
| 3 | [Fix 3](#fix-3--s3973-missing-curly-braces) | S3973 | 1 | < 5 min | Mechanical |
| 4 | [Fix 5](#fix-5--s5028-gl-extension-macros) | S5028 | 5 | 30 min | Suppression |
| 5 | [Fix 6](#fix-6--s5008-void-pointer-usage) | S5008 | 3 | ~1 h | Code + suppression |
| 6 | [Fix 7](#fix-7--s5025-raw-newdelete) | S5025 | 13 | 2–4 h | Code refactor + suppression |
| 7 | [Fix 4](#fix-4--s5421-non-const-global-variable) | S5421 | 2 | 1–2 h | Code refactor |

Fixes 1–3 are purely mechanical — no design decisions. Fixes 4–7 each carry a
non-obvious gotcha documented in the per-fix sections below.

---

## Fix 1 — S1186: Empty method bodies

**Rule:** `cpp:S1186` — empty method body with no explanatory comment
**Issues:** 5
**Effort:** < 15 min

### Files and changes

Add a brief inline comment inside each empty body. No logic change.

**[src/interfaces/IRenderer.h:180](src/interfaces/IRenderer.h#L180)**
```cpp
// Before:
virtual void setZoneHoverColour(unsigned int argb) {}

// After:
virtual void setZoneHoverColour(unsigned int argb) { /* intentionally empty — default no-op; renderers that do not support per-zone hover colour ignore this */ }
```

**[src/rendering/BuildingShaderCallback.h:80](src/rendering/BuildingShaderCallback.h#L80)**
```cpp
// Before:
void OnSetMaterial(const irr::video::SMaterial& /*material*/) override {}

// After:
void OnSetMaterial(const irr::video::SMaterial& /*material*/) override { /* intentionally empty — all state set per-draw in OnSetConstants() */ }
```

**[src/rendering/RoadShaderCallback.h:87](src/rendering/RoadShaderCallback.h#L87)**
```cpp
// Before:
void OnSetMaterial(const irr::video::SMaterial& /*material*/) override {}

// After:
void OnSetMaterial(const irr::video::SMaterial& /*material*/) override { /* intentionally empty — all state set per-draw in OnSetConstants() */ }
```

**[src/rendering/TerrainShaderCallback.h:92](src/rendering/TerrainShaderCallback.h#L92)**
```cpp
// Before:
void OnSetMaterial(const irr::video::SMaterial& /*material*/) override {}

// After:
void OnSetMaterial(const irr::video::SMaterial& /*material*/) override { /* intentionally empty — all state set per-draw in OnSetConstants() */ }
```

**[src/terrain/terrain_types.h:13](src/terrain/terrain_types.h#L13)**
```cpp
// Before:
virtual void onChunkRebuilt(int done, int total) {}

// After:
virtual void onChunkRebuilt(int done, int total) { /* intentionally empty — default no-op progress callback */ }
```

### Done criteria

- All 5 bodies have an inline comment
- Build passes (`make build`)

---

## Fix 2 — S7127: `sizeof` array hack

**Rule:** `cpp:S7127` — use `std::size()` instead of `sizeof(arr)/sizeof(arr[0])`
**Issues:** 2
**Effort:** < 15 min

### Files and changes

**[src/benchmark/model_validator_main.cpp:196](src/benchmark/model_validator_main.cpp#L196)**
```cpp
// Before:
static const int kN = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));

// After:
static const int kN = static_cast<int>(std::size(kNames));
```

Add `#include <iterator>` near the top of the file if not already present (canonical
C++17 header for `std::size`).

**[src/benchmark/benchmark_main.cpp:646](src/benchmark/benchmark_main.cpp#L646)**
```cpp
// Before:
const int kNumTestLevels = static_cast<int>(sizeof(kTestLevels) / sizeof(kTestLevels[0]));

// After:
const int kNumTestLevels = static_cast<int>(std::size(kTestLevels));
```

Add `#include <iterator>` near the top of the file if not already present.

> Note: both files already include `<vector>` which in libstdc++/libc++ transitively
> pulls `std::size`, so the build will not break. The explicit `<iterator>` include is
> for portability and strict-conformance.

### Done criteria

- Both `sizeof/sizeof` patterns replaced
- `#include <iterator>` present in both files
- Build passes

---

## Fix 3 — S3973: Missing curly braces

**Rule:** `cpp:S3973` — conditional/loop body without curly braces
**Issues:** 1
**Effort:** < 5 min

### File and change

**[src/benchmark/benchmark_main.cpp:581](src/benchmark/benchmark_main.cpp#L581)**

The outer `for (irr::u32 py ...)` at line 581 has no braces around its body. Its body
is the entire inner `for (irr::u32 px ...)` statement at line 582, which already has
`{ }` on lines 583–593. Only the outer wrapper braces are missing.

```cpp
// Before (line 581):
for (irr::u32 py = 0; py < 32u; ++py)
for (irr::u32 px = 0; px < 32u; ++px)
{
    // ... body ...
}

// After:
for (irr::u32 py = 0; py < 32u; ++py)
{
    for (irr::u32 px = 0; px < 32u; ++px)
    {
        // ... body ...
    }
}
```

Do **not** add braces around the inner `for`'s body — it already has them.

### Done criteria

- Outer `for` at line 581 has `{ }` wrapper
- Inner `for` body unchanged
- Build passes

---

## Fix 5 — S5028: GL extension macros

**Rule:** `cpp:S5028` — C preprocessor macro, replace with `constexpr`/`enum`
**Issues:** 5
**Effort:** 30 min

### Why these are false positives

All five macros are OpenGL extension constants inside `#ifndef … #endif` guards (e.g.
`#ifndef GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX`). GLEW may or may not have
already defined the symbol depending on version; the guard prevents a
duplicate-definition error. Replacing with `constexpr` would break the conditional
override (`#ifndef` does not work with `constexpr`). Do not change the code.

> `// NOSONAR` on `#define` lines is **unreliable** even when CFamily NOSONAR support
> is active — the rule fires at AST construction time before the comment can be
> associated with the macro node. Do not use it here.

### Suppression — choose one option

#### Option A: Mark as Accepted in the SonarCloud UI (lowest effort)

Log into SonarCloud → project `M0WA_ai-town` → Issues → filter by rule `cpp:S5028`
→ mark all 5 as **Accepted**. Verify your quality gate condition is scoped to
`Open` status (the default) so accepted issues do not fail the gate.

#### Option B: `sonar.issue.ignore.multicriteria` in `sonarcloud.yml`

Add the following `-D` flags to the **Run SonarScanner** step in
`.github/workflows/sonarcloud.yml`:

```
-Dsonar.issue.ignore.multicriteria=e1,e2,e3 \
-Dsonar.issue.ignore.multicriteria.e1.ruleKey=cpp:S5028 \
-Dsonar.issue.ignore.multicriteria.e1.resourceKey=src/rendering/VRAMProfiler.h \
-Dsonar.issue.ignore.multicriteria.e2.ruleKey=cpp:S5028 \
-Dsonar.issue.ignore.multicriteria.e2.resourceKey=src/benchmark/benchmark_main.cpp \
-Dsonar.issue.ignore.multicriteria.e3.ruleKey=cpp:S5028 \
-Dsonar.issue.ignore.multicriteria.e3.resourceKey=src/benchmark/model_validator_main.cpp \
```

**The top-level `sonar.issue.ignore.multicriteria=e1,e2,e3` key is mandatory** —
without it the scanner silently ignores all sub-entries.

If other multicriteria suppressions from Fix 6 or Fix 7 are added in the same PR,
extend the top-level comma-separated list (e.g. `=e1,e2,e3,e4,e5,...`) and keep all
entries consistent.

### Files affected

| File | Line | Macro |
|---|---|---|
| `src/rendering/VRAMProfiler.h` | 23 | `GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX` |
| `src/rendering/VRAMProfiler.h` | 26 | `GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX` |
| `src/rendering/VRAMProfiler.h` | 29 | `GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX` |
| `src/benchmark/model_validator_main.cpp` | 15 | GL extension macro |
| `src/benchmark/benchmark_main.cpp` | 28 | GL extension macro |

### Done criteria

- All 5 S5028 issues are either Accepted in SonarCloud UI **or** suppressed via
  `sonar.issue.ignore.multicriteria` in `sonarcloud.yml`
- Next scan shows 0 open S5028 issues in the quality gate

---

## Fix 6 — S5008: `void*` pointer usage

**Rule:** `cpp:S5008` — use of `void*`, replace with typed pointer
**Issues:** 3
**Effort:** ~1 h

### Site 1 — `IrrlichtRenderer.h:237` — fixable with forward declaration

**[src/rendering/IrrlichtRenderer.h:237](src/rendering/IrrlichtRenderer.h#L237)**

`void* m_cloudShaderCbRaw` exists to avoid pulling `CloudDomeShaderCallback`'s class
definition into the header. The class is defined **only** inside `IrrlichtRenderer.cpp`
(at line 62) and must stay there.

**Step 1 — Add a forward declaration to `IrrlichtRenderer.h`** (before the class
body, near the other forward declarations at lines 9–14):

```cpp
class CloudDomeShaderCallback;
```

**Step 2 — Change the member type** (line 237):

```cpp
// Before:
void*  m_cloudShaderCbRaw{nullptr};

// After:
CloudDomeShaderCallback* m_cloudShaderCbRaw{nullptr};
```

Update the adjacent comment to reflect the type change.

**Step 3 — Remove the redundant cast in the destructor** (`IrrlichtRenderer.cpp`
~line 129). After the type change `m_cloudShaderCbRaw` is already
`CloudDomeShaderCallback*`, so the `static_cast` is a no-op same-type cast:

```cpp
// Before:
if (m_cloudShaderCbRaw) {
    static_cast<CloudDomeShaderCallback*>(m_cloudShaderCbRaw)->drop();
    m_cloudShaderCbRaw = nullptr;
}

// After:
if (m_cloudShaderCbRaw) {
    m_cloudShaderCbRaw->drop();
    m_cloudShaderCbRaw = nullptr;
}
```

`CloudDomeShaderCallback` is fully defined earlier in the same `.cpp` file (line 62),
so `->drop()` resolves correctly through `IReferenceCounted` at the destructor call site.

**Constraints:**
- Do **not** extract `CloudDomeShaderCallback` to a new header — it is an
  implementation detail of `IrrlichtRenderer` and must remain `.cpp`-local.
- `~IrrlichtRenderer()` must remain defined in the `.cpp` (not `= default` in the
  header) — a forward-declared incomplete type cannot be deleted/dropped from an inline
  destructor.

### Site 2 — `AudioSystem.cpp:84` — do NOT change; suppress

**[src/audio/AudioSystem.cpp:84](src/audio/AudioSystem.cpp#L84)**

`alcCheckError_real(void* device, ...)` uses `void*` intentionally: `al_check.h`
must not include `<AL/alc.h>` to preserve the zero-AL-headers contract that allows
test TUs to compile without OpenAL hardware. Changing to `ALCdevice*` would break
headless-CI compilation.

Action: **Mark as Accepted in SonarCloud UI**, or add to `sonar.issue.ignore.multicriteria`:

```
-Dsonar.issue.ignore.multicriteria.eN.ruleKey=cpp:S5008 \
-Dsonar.issue.ignore.multicriteria.eN.resourceKey=src/audio/AudioSystem.cpp \
```

(Replace `eN` with the next available index and add `eN` to the top-level list.)

### Site 3 — `AudioSystem.cpp:106` — do NOT change; suppress

**[src/audio/AudioSystem.cpp:106](src/audio/AudioSystem.cpp#L106)**

`getProcAddress` returns `void*` for two independent reasons:
1. `alcGetProcAddress` returns `void*` by the OpenAL C API contract.
2. `IAlcFunctions::getProcAddress` at `src/interfaces/IAlcFunctions.h:33` is declared
   `virtual void* getProcAddress(...)` — `DefaultAlcFunctions::getProcAddress` is an
   `override` and must match it exactly.

Action: same as Site 2 — **Mark as Accepted** or add to `sonar.issue.ignore.multicriteria`
(one entry covers both lines if `resourceKey=src/audio/AudioSystem.cpp`).

### Combined multicriteria block for all three suppressed sites

If using Option B from Fix 5 and extending the same multicriteria block:

```
-Dsonar.issue.ignore.multicriteria=e1,e2,e3,e4 \
-Dsonar.issue.ignore.multicriteria.e1.ruleKey=cpp:S5028 \
-Dsonar.issue.ignore.multicriteria.e1.resourceKey=src/rendering/VRAMProfiler.h \
-Dsonar.issue.ignore.multicriteria.e2.ruleKey=cpp:S5028 \
-Dsonar.issue.ignore.multicriteria.e2.resourceKey=src/benchmark/benchmark_main.cpp \
-Dsonar.issue.ignore.multicriteria.e3.ruleKey=cpp:S5028 \
-Dsonar.issue.ignore.multicriteria.e3.resourceKey=src/benchmark/model_validator_main.cpp \
-Dsonar.issue.ignore.multicriteria.e4.ruleKey=cpp:S5008 \
-Dsonar.issue.ignore.multicriteria.e4.resourceKey=src/audio/AudioSystem.cpp \
```

### Done criteria

- `IrrlichtRenderer.h:237` member type changed to `CloudDomeShaderCallback*`
- Forward declaration `class CloudDomeShaderCallback;` added to `IrrlichtRenderer.h`
- `static_cast` removed from destructor in `IrrlichtRenderer.cpp`
- Both `AudioSystem.cpp` S5008 sites suppressed (UI or multicriteria)
- Build passes; no new compiler warnings

---

## Fix 7 — S5025: Raw `new`/`delete`

**Rule:** `cpp:S5025` — raw `new`/`delete`, use smart pointers
**Issues:** 13
**Effort:** 2–4 h

The 13 issues split into three groups with different treatments.

### Group A — Irrlicht shader callbacks (suppress; do NOT convert to `unique_ptr`)

**Sites:**
- `IrrlichtRenderer.cpp:1669` — `new RoadShaderCallback(...)`
- `IrrlichtRenderer.cpp:1743` — `new TerrainShaderCallback(...)`
- `IrrlichtRenderer.cpp:2859` — `new CloudDomeShaderCallback(...)`
- `src/benchmark/model_validator_main.cpp:558` — `new RoadShaderCallback(...)`

These use the Irrlicht ref-counted ownership model: Irrlicht calls `grab()` internally
on the pointer passed to `addHighLevelShaderMaterialFromFiles`, so wrapping in
`unique_ptr` causes a double-free. The existing `new` + `->drop()` pattern is correct.

Before accepting, verify the ref-counting is complete:

- **Immediately-dropped** (lines 1669, 1743, `model_validator_main.cpp:558`):
  `cb->drop()` called after `addHighLevelShaderMaterialFromFiles` returns.
  `TerrainShaderCallback` (line 1743) has **two drop sites** — both must be preserved:
  - Line 1753: early-return drop when `gpu` is null
  - Line 1762: unconditional drop after `addHighLevelShaderMaterialFromFiles`

- **Long-lived** (`IrrlichtRenderer.cpp:2859`, `CloudDomeShaderCallback`):
  reference retained as `m_cloudShaderCbRaw`; `->drop()` called in the destructor.
  This is a valid alternative — the immediate-drop rule does not apply here.

**Action:** Mark all four Group A sites as **Accepted in the SonarCloud UI**, or add:

```
-Dsonar.issue.ignore.multicriteria.eN.ruleKey=cpp:S5025 \
-Dsonar.issue.ignore.multicriteria.eN.resourceKey=src/rendering/IrrlichtRenderer.cpp \
-Dsonar.issue.ignore.multicriteria.eM.ruleKey=cpp:S5025 \
-Dsonar.issue.ignore.multicriteria.eM.resourceKey=src/benchmark/model_validator_main.cpp \
```

(Extend the top-level list accordingly.)

### Group B — LODNode maps (refactor to `unique_ptr`)

**Sites:**
- `IrrlichtRenderer.cpp:161, 164, 173` — `delete` in `~IrrlichtRenderer()` destructor loops
- `IrrlichtRenderer.cpp:223, 1396, 2502` — `delete` in separate eviction helper methods
- `IrrlichtRenderer.cpp:2244` — `return new LODNode(...)`
- `BuildingAssetLoader.cpp:235` — `return new LODNode(...)`

`LODNode` is plain C++ (not Irrlicht ref-counted), so `unique_ptr` is safe.

#### Step 1 — Audit tests first

```bash
grep -rn "LODNode\*" tests/
```

Any test storing a raw `LODNode*` from these maps will have a dangling pointer after
the map entry is erased. Fix those tests before merging (typically: store the index/key
instead of the pointer, or call `.get()` only within a scope where the map entry is
guaranteed to exist).

#### Step 2 — Change map types in `IrrlichtRenderer`

In `IrrlichtRenderer.h`, change the three LOD node maps from:

```cpp
std::unordered_map<uint64_t, LODNode*> m_buildingNodes;
std::unordered_map<uint64_t, LODNode*> m_roadNodes;
std::unordered_map<uint32_t, LODNode*> m_vehicleNodes;
```

to:

```cpp
std::unordered_map<uint64_t, std::unique_ptr<LODNode>> m_buildingNodes;
std::unordered_map<uint64_t, std::unique_ptr<LODNode>> m_roadNodes;
std::unordered_map<uint32_t, std::unique_ptr<LODNode>> m_vehicleNodes;
```

#### Step 3 — Remove destructor `delete` loops (lines 161, 164, 173)

The `~IrrlichtRenderer()` destructor loops that iterate the maps and call `delete` on
each `LODNode*` are no longer needed — the `unique_ptr` destructors fire automatically
when the maps are destroyed. Remove the explicit `delete kv.second` calls and the loops
if their sole purpose was cleanup (verify they do nothing else before removing).

#### Step 4 — Update `evictLODNodeRegistry` template (lines 201–226)

This template is the source of the `delete` calls at lines 223, 1396, and 2502. It
requires three edits:

```cpp
// Before signature:
template<typename KeyT>
void IrrlichtRenderer::evictLODNodeRegistry(
    std::unordered_map<KeyT, LODNode*>& registry)

// After signature:
template<typename KeyT>
void IrrlichtRenderer::evictLODNodeRegistry(
    std::unordered_map<KeyT, std::unique_ptr<LODNode>>& registry)
```

```cpp
// Before (line 206):
LODNode* lodNode = kv.second;

// After:
LODNode* lodNode = kv.second.get();  // raw ptr still needed for scene-graph steps
```

```cpp
// Remove (line 223) — causes double-free after unique_ptr:
delete lodNode;
// registry.clear() at line 225 already invokes each unique_ptr destructor
```

Update the two explicit instantiations at lines 231–234 to match the new signature:

```cpp
template void IrrlichtRenderer::evictLODNodeRegistry<uint64_t>(
    std::unordered_map<uint64_t, std::unique_ptr<LODNode>>&);
template void IrrlichtRenderer::evictLODNodeRegistry<uint32_t>(
    std::unordered_map<uint32_t, std::unique_ptr<LODNode>>&);
```

#### Step 5 — Change factory return types

**`IrrlichtRenderer.cpp:2244`** and **`BuildingAssetLoader.cpp:235`** — change from
`LODNode*` to `std::unique_ptr<LODNode>`:

```cpp
// Before:
LODNode* createLODNode(...) {
    ...
    return new LODNode(...);
}

// After:
std::unique_ptr<LODNode> createLODNode(...) {
    ...
    return std::make_unique<LODNode>(...);
}
```

Update the corresponding declarations in headers and all call sites that receive the
returned pointer (they will now receive a `unique_ptr` and must take ownership via
`std::move` or store it directly).

#### Step 6 — Update all insertion sites

Every `m_buildingNodes[key] = new LODNode(...)` must become:
```cpp
m_buildingNodes[key] = std::make_unique<LODNode>(...);
```

Every lookup that returns `LODNode*` for non-owning use:
```cpp
// Raw pointer access for non-owning use (map still owns):
LODNode* node = m_buildingNodes[key].get();
```

### Group C — `model_validator_main.cpp:1207` (self-contained)

**Site:** `src/benchmark/model_validator_main.cpp:1207` — `delete ln` in a loop over
`std::vector<LODNode*>`.

This is self-contained to the benchmark tool. Change the vector type:

```cpp
// Before:
std::vector<LODNode*> nodes;
...
for (LODNode* ln : nodes) delete ln;

// After:
std::vector<std::unique_ptr<LODNode>> nodes;
// loop removed — unique_ptrs destroyed when vector goes out of scope
```

Update all `nodes.push_back(new LODNode(...))` to `nodes.push_back(std::make_unique<LODNode>(...))`.
Update any `LODNode* n = nodes[i]` accesses to `LODNode* n = nodes[i].get()`.

### Done criteria

- Group A: all 4 sites accepted/suppressed in SonarCloud
- Group B: maps use `unique_ptr`, `evictLODNodeRegistry` template updated, explicit
  instantiations updated, destructor loops removed, factory return types updated,
  all insertion and call sites updated, test audit complete
- Group C: vector converted to `unique_ptr`, explicit `delete` loop removed
- Build passes, all tests pass

---

## Fix 4 — S5421: Non-`const` global variable

**Rule:** `cpp:S5421` — non-`const` variable with static storage duration
**Issues:** 2
**Effort:** 1–2 h

### Background

`g_assetsDir` is set once at startup in `main()` and read-only afterward. A
file-scope `static` rename (`static std::string s_assetsDir`) does **not** satisfy
S5421 — a file-scope static also has static storage duration and is still flagged.

The Sonar-compliant approach is a **function-local static** (Meyers singleton) — which
is function-scoped, not namespace or file-scoped, and is not expected to trigger S5421.
Verify this after the first post-refactor scan; if S5421 persists on the local static,
fall back to **Mark as Accepted** (see below).

### Read sites to update (25 occurrences across 5 files)

```
src/main.cpp                   — 1 write (line 48), 4 reads (lines 209, 245, 499, 559)
src/rendering/IrrlichtRenderer.cpp — 13 reads
src/rendering/IrrlichtUIBackend.cpp — 5 reads
src/audio/AudioSystem.cpp      — 1 read (line 164, inside buildPath())
src/ui/UIManager.cpp           — 1 read (line 1996)
```

### Step 1 — Replace definition and declaration in `PlatformUtils`

**`src/platform/PlatformUtils.cpp`** — replace the global definition:

```cpp
// Before:
std::string g_assetsDir;

// After (full replacement):
namespace {
    std::string& mutableAssetsDir() {
        static std::string s;
        return s;
    }
}

void setAssetsDir(std::string dir) { mutableAssetsDir() = std::move(dir); }
const std::string& getAssetsDir()  { return mutableAssetsDir(); }
```

**`src/platform/PlatformUtils.h`** — remove the `extern` declaration and add the two
accessor declarations. The `extern std::string g_assetsDir;` at line 21 **must be
explicitly removed** — leaving it causes the S5421 violation at `PlatformUtils.h:21`
to remain even after the `.cpp` is fixed. Also remove or update the comment at
line 16 that references `g_assetsDir`:

```cpp
// Before (lines 16–21):
// Called once at startup; result stored in g_assetsDir.
...
// g_assetsDir — the resolved assets directory, set once at startup by main()
// and read-only thereafter.
extern std::string g_assetsDir;

// After:
// setAssetsDir / getAssetsDir — assets directory accessor.
// setAssetsDir() must be called exactly once at startup (in main()) before any
// subsystem threads are started. getAssetsDir() is read-only after that point.
void setAssetsDir(std::string dir);
const std::string& getAssetsDir();
```

### Step 2 — Update the write site in `main.cpp`

**`src/main.cpp:48`**:
```cpp
// Before:
g_assetsDir = resolveAssetsDir();

// After:
setAssetsDir(resolveAssetsDir());
```

### Step 3 — Update all read sites

Replace every `g_assetsDir` with `getAssetsDir()` in all files listed above. The
return type is `const std::string&` so all existing concatenation patterns
(`g_assetsDir + "/..."`) continue to work without change.

Example (pattern applies uniformly):
```cpp
// Before:
const std::string vsPath = g_assetsDir + "/shaders/road.vert";

// After:
const std::string vsPath = getAssetsDir() + "/shaders/road.vert";
```

The one local copy at `IrrlichtRenderer.cpp:1721`:
```cpp
// Before:
const std::string assetsDir = g_assetsDir;

// After:
const std::string assetsDir = getAssetsDir();
```

### Thread-safety contract

`setAssetsDir()` must be called **before** any subsystem thread (audio thread in
`AudioSystem`, rendering thread if present) is started. The current design satisfies
this: `main()` calls `setAssetsDir()` at line 48 before constructing any subsystem.

**This contract must be preserved.** The Meyers singleton provides no synchronization
on writes — calling `setAssetsDir()` after subsystem threads have started is a data
race on the internal `std::string` and is undefined behaviour.

### Alternative (if Sonar still fires after the refactor)

If a post-refactor scan shows S5421 persisting on the function-local static, mark both
issues as **Accepted in the SonarCloud UI**. The design intent (set-once at startup,
read-only thereafter) is documented and the mutability is by design.

### Done criteria

- `extern std::string g_assetsDir;` removed from `PlatformUtils.h`
- `std::string g_assetsDir;` definition removed from `PlatformUtils.cpp`
- `setAssetsDir` / `getAssetsDir` declared in `.h`, defined in `.cpp`
- All 25 `g_assetsDir` occurrences replaced (1 write → `setAssetsDir`, 24 reads → `getAssetsDir()`)
- Build passes, all tests pass
- Post-scan confirms 0 open S5421 issues (or both marked Accepted if violation persists)

---

## Verification — after all 7 fixes

1. Build: `make build` — must pass with no new warnings
2. Tests: `make test` — coverage gate must hold (≥95%)
3. Push branch → CI green → SonarCloud scan completes
4. SonarCloud dashboard: confirm 0 open issues for rules
   S1186, S7127, S3973, S5028, S5008, S5025, S5421
5. Quality gate: green (or only Fixes 8–9 issues remaining as expected)
