## Phase 11q12: PLY-First Mesh Loading with B3D Fallback

**Status: TODO**

**Prerequisite**: phase-11q11 merged.

### Goal

Tripo3D high-poly LOD0 models crash on load because Irrlicht's B3D loader
(`SSkinMeshBuffer`) hardcodes 16-bit indices (`core::array<u16> Indices`).
Any mesh buffer with >65,535 vertices wraps its indices, producing garbage
geometry (vehicles) or a segfault (buildings) in `drawVertexPrimitiveList`.

Irrlicht's PLY loader (`CPLYMeshFileLoader`) uses `CDynamicMeshBuffer` which
automatically selects `EIT_32BIT` when vertex count exceeds a threshold. This
phase switches the mesh loading path to prefer `.ply` files and fall back to
`.b3d` only when no PLY exists, preserving backward compatibility with the
original low-poly `ind_*` / `res_*` assets that remain in B3D format.

**Upstream threshold bug**: `CPLYMeshFileLoader.cpp` line 233 uses
`vertCount > 65565` (not `> 65535`) — an off-by-30 typo in upstream Irrlicht.
Meshes with vertex counts in [65,536 .. 65,565] would be assigned `EIT_16BIT`
indices, causing silent index truncation and corrupt geometry. This phase
applies a one-line vendor patch to fix the threshold to `> 65535`. All current
Tripo3D LOD0 assets are well above this range (~495K vertices), so the bug does
not affect existing assets, but the fix prevents future edge-case corruption.

### Root Cause

- `SSkinMeshBuffer::getIndexType()` always returns `EIT_16BIT` (Irrlicht
  engine limitation, not AI Town code).
- The `B3DWriter` in `convert_vehicle_fbx.py` / `convert_building_fbx.py`
  splits vertices into 65,535-vertex VRTS chunks, but the TRIS indices within
  each chunk still reference indices >65,535, causing 16-bit truncation.
- All Tripo3D LOD0 B3D files have index overflow (up to 377K indices per
  buffer). LOD1 files are safe (<65K).

### Approach

Introduce a single free function `resolveModelPath()` that tries `.ply` first,
then `.b3d`, used by all three mesh-loading call sites. No format-specific
logic is needed in callers because `ISceneManager::getMesh()` dispatches to the
correct loader based on file extension and returns `IAnimatedMesh*` regardless.

---

### Deliverables

#### 1. New helper: `src/rendering/mesh_format_utils.h` + `mesh_format_utils.cpp`

- [ ] Create `src/rendering/mesh_format_utils.h` (declaration-only header)
- [ ] Create `src/rendering/mesh_format_utils.cpp` (implementation)
- [ ] The header forward-declares `irr::io::IFileSystem` and declares
      `resolveModelPath()`. It does **NOT** include `<IFileSystem.h>` or
      `<filesystem>`, so any translation unit that includes only the header
      (e.g. `vehicle_mesh_path.h` -> `VehicleZoneTest.cpp` in
      `simulation_tests`) will NOT pull in Irrlicht headers. This is the
      key design choice that prevents a compile failure in targets that
      lack Irrlicht on their include path.

**`mesh_format_utils.h`**:

```cpp
#pragma once
#include <string>

namespace irr { namespace io { class IFileSystem; } }

/// Try PLY first, fall back to B3D.
/// fs:       Irrlicht VFS handle (may be nullptr for offline tools / unit tests).
/// basePath: e.g. "assets/3d/vehicles/car_sedan"
/// suffix:   e.g. "_lod0"
/// Returns:  basePath + suffix + ".ply"  if that file exists,
///           basePath + suffix + ".b3d"  otherwise.
std::string resolveModelPath(irr::io::IFileSystem* fs,
                             const std::string& basePath,
                             const std::string& suffix);
```

**`mesh_format_utils.cpp`**:

```cpp
#include "mesh_format_utils.h"
#include <IFileSystem.h>   // irr::io::IFileSystem -- full definition
#include <filesystem>

std::string resolveModelPath(irr::io::IFileSystem* fs,
                             const std::string& basePath,
                             const std::string& suffix) {
    std::string ply = basePath + suffix + ".ply";
    if (fs) {
        if (fs->existFile(ply.c_str())) return ply;
    } else {
        if (std::filesystem::exists(ply)) return ply;
    }
    return basePath + suffix + ".b3d";
}
```

- [ ] Compiled into `aitown_render` via
      `target_sources(aitown_render PRIVATE src/rendering/mesh_format_utils.cpp)`.
- [ ] The `IFileSystem*` path is critical: `addFolderFileArchive()` paths used
      by the model validator are only visible through Irrlicht's VFS, not the
      OS filesystem. The `std::filesystem::exists()` fallback covers offline
      tools and unit tests that have no Irrlicht device.
- [ ] No class, no state, no templates. One free function, declared in the
      header and defined in the `.cpp`.

#### 2. Update `BuildingAssetLoader.cpp` (3 call sites)

- [ ] `#include "mesh_format_utils.h"` at the top.
- [ ] Replace the three hardcoded `.b3d` path constructions with
      `resolveModelPath()`:

| Line | Before | After |
|------|--------|-------|
| ~111 | `basePath + "_lod0.b3d"` | `resolveModelPath(m_smgr->getFileSystem(), basePath, "_lod0")` |
| ~120 | `basePath + "_lod1.b3d"` | `resolveModelPath(m_smgr->getFileSystem(), basePath, "_lod1")` |
| ~132 | `basePath + "_lod2.b3d"` | `resolveModelPath(m_smgr->getFileSystem(), basePath, "_lod2")` |

- [ ] `BuildingAssetLoader` obtains `IFileSystem*` via `m_smgr->getFileSystem()`
      (`ISceneManager::getFileSystem()` is already available through the existing
      `m_smgr` pointer — no new member needed). Pass it to `resolveModelPath()`.
      `resolveModelPath()`.

- [ ] Update comments in `BuildingAssetLoader.h` and `.cpp` that reference
      `.b3d` to say ".ply / .b3d" or "mesh file" where appropriate (file
      header docstrings, `load()` docstring). Do not change comments that
      describe B3D-specific internals (tc_sets, VRTS layout, etc.) since those
      still apply when the fallback is used.

#### 3. Update `vehicle_mesh_path.h` (5 mesh filenames)

- [ ] `vehicleMeshPath()` signature is **unchanged** -- no `IFileSystem*` parameter.
- [ ] Refactor the return value to produce an **extensionless base path** (e.g.
      `assets/3d/vehicles/car_sedan_lod0`) instead of the current hardcoded
      `.b3d` path (`assets/3d/vehicles/car_sedan_lod0.b3d`). This is the only
      change -- the zone-to-vehicle-name mapping logic is untouched.
- [ ] **Path contract note**: `vehicleMeshPath()` continues to return a full
      path rooted at `getAssetsDir()` (e.g.
      `/home/user/.../assets/3d/vehicles/car_sedan_lod0`). When `IFileSystem*`
      is non-null, `resolveModelPath()` uses `IFileSystem::existFile()` which
      handles absolute paths because `addFolderFileArchive()` has already been
      called for the assets directory by `BuildingAssetLoader` / the model
      validator. When `IFileSystem*` is nullptr, the fallback uses
      `std::filesystem::exists()` which handles absolute paths natively.
- [ ] **Callers** in `IrrlichtRenderer.cpp` that pass the result to
      `getMesh()` must wrap it:
      `resolveModelPath(m_smgr->getFileSystem(), vehicleMeshPath(zone, variant), "")`
      where the empty suffix means "already includes _lodN" and
      `resolveModelPath` appends `.ply` or `.b3d`.
      **Note**: `IrrlichtRenderer` obtains `IFileSystem*` via
      `m_smgr->getFileSystem()` at each call site — same pattern as
      `BuildingAssetLoader` in Deliverable 2 (no new member needed).
- [ ] Update the file-header docstring to mention extensionless return value.
- [ ] **Design rationale**: `vehicleMeshPath()` lives in a header included by
      `simulation_tests`, which must NOT link `aitown_render` (per
      `testability-architecture.md`). By keeping `vehicleMeshPath()` as a pure
      string-construction helper with no external symbol dependencies, the
      link-time invariant is preserved. Format resolution happens at the
      `IrrlichtRenderer` call sites which already link `aitown_render`.

#### 4. Update `IrrlichtRenderer.cpp` — vehicle LOD1 loading (if applicable)

- [ ] Search for any direct `"_lod1.b3d"` construction for vehicles in
      `IrrlichtRenderer.cpp` (e.g. inside `spawnVehicleAgent` or
      `placeVehicle`). If found, replace with `resolveModelPath(m_smgr->getFileSystem(), ...)`.
- [ ] **Note**: vehicle mesh paths obtained from `vehicleMeshPath()` are now
      extensionless base paths (e.g. `assets/3d/vehicles/car_sedan_lod0`).
      All call sites in `IrrlichtRenderer.cpp` that pass these paths to
      `getMesh()` must wrap them through `resolveModelPath()` to append the
      correct `.ply` or `.b3d` extension.
- [ ] If vehicle LOD1 is loaded through `BuildingAssetLoader::load()` (which
      is also used for vehicles despite the class name), no additional change
      is needed -- Deliverable 2 already covers it.

#### 5. Verify no other `.b3d` references remain

- [ ] `grep -rn '\.b3d' src/` -- every hit should either be inside a comment
      describing the B3D format, or already routed through
      `resolveModelPath()`. Fix any remaining hardcoded `.b3d` path
      constructions.
- [ ] The model validator (`src/benchmark/model_validator_main.cpp`) hardcodes
      `{"_lod0.b3d", "_lod1.b3d", "_lod2.b3d"}` suffixes when iterating
      building/vehicle assets. Update it to try PLY first via
      `resolveModelPath()` (passing the device's `IFileSystem*`, which is
      already available since the validator calls `addFolderFileArchive()`).

#### 6. Integration test: `tests/integration/MeshFormatUtilsTest.cpp`

- [ ] Test `resolveModelPath()` with a temp directory (pass `nullptr` for
      `IFileSystem*` to exercise the `std::filesystem::exists()` fallback):
  - When `foo_lod0.ply` exists: returns `.ply` path.
  - When only `foo_lod0.b3d` exists: returns `.b3d` path.
  - When neither exists: returns `.b3d` path (caller handles load failure).
- [ ] Test `resolveModelPath()` with a real `IFileSystem*` (the primary
      production code path used by `BuildingAssetLoader` and
      `IrrlichtRenderer`): create an `EDT_NULL` device via
      `createDevice(video::EDT_NULL)`, call `device->getFileSystem()`, add
      the temp directory as a folder archive
      (`fs->addFolderFileArchive(tmpDir)`), then verify:
  - When `foo_lod0.ply` exists: `fs->existFile()` finds it, returns `.ply`.
  - When only `foo_lod0.b3d` exists: returns `.b3d` path.
  - When neither exists: add an empty temp directory as folder archive,
    verify `resolveModelPath()` returns `.b3d` fallback path (caller handles
    load failure). This mirrors the nullptr "neither-present" case above but
    exercises the `IFileSystem::existFile()` code path.
  - **TearDown fixture requirement**: these three `IFileSystem*` test cases
    (cases 4--6) must use a GTest test fixture (e.g.
    `class MeshFormatUtilsIFSTest : public ::testing::Test`) with a
    `TearDown()` override that calls `device_->drop()` and sets
    `device_ = nullptr` if non-null, preventing Irrlicht device resource
    leaks when an `EXPECT` or `ASSERT` fails mid-test. This follows the
    mandatory TearDown contract pattern established in
    `architecture/testing/testability-architecture.md` (see
    `UIManagerTest` and `CitySimulationUnitTest` fixture examples):
    explicitly release the owned resource in `TearDown()` so cleanup is
    guaranteed regardless of test outcome. The three `nullptr`-fallback
    cases (cases 1--3) do not create a device and may remain as bare
    `TEST()` functions or a separate fixture without device cleanup.
- [ ] Register in `CMakeLists.txt`:
      `target_sources(integration_tests PRIVATE tests/integration/MeshFormatUtilsTest.cpp)`.
      `integration_tests` already links `aitown_render` and has `src/rendering/`
      on its include path, so `#include "mesh_format_utils.h"` resolves and the
      `resolveModelPath()` definition is available without additional changes.
- [ ] **Placement rationale**: MeshFormatUtilsTest includes both
      `std::filesystem` nullptr-fallback tests and `EDT_NULL` `IFileSystem*`
      tests; it lives in `integration_tests` because (a) that target already
      links `aitown_render` (where `resolveModelPath()` is compiled) and
      (b) the `EDT_NULL` device tests meet the integration label definition
      in `framework.md`. Placing it in `simulation_tests` would require
      adding `aitown_render` to `simulation_tests`'s link dependencies,
      breaking the `simulation_tests`-does-not-link-Irrlicht compile-time
      header isolation invariant documented in `testability-architecture.md`.
- [ ] **Coverage note**: the test file lives in `tests/integration/` (under the
      `integration_tests` target, label `integration`) but the source it exercises
      (`src/rendering/mesh_format_utils.cpp`) is in `src/rendering/`, which is
      excluded from lcov coverage filtering (see `coverage_filtered.info` exclude
      list in `CLAUDE.md`). Therefore `resolveModelPath()` line coverage will not
      count toward the 95% gate. This is intentional -- the function body is a
      trivial 6-line implementation whose correctness is fully exercised by the
      six test cases above (three `nullptr` fallback + three `IFileSystem*` path).
- [ ] **Mock policy note**: no GMock mock objects are needed —
      `resolveModelPath()` is a pure free function; tests use direct assertions
      (`EXPECT_EQ` / `EXPECT_THAT` with `EndsWith`) without a mock fixture. The
      project's `StrictMock`/`NiceMock` policy does not apply to this test.
- [ ] Add Phase 11q12 Canonical Test Name Summary table to
      `architecture/testing/testability-architecture.md` (ref:
      architecture/testing/testability-architecture.md). This is tracked as a
      discrete deliverable to ensure the spec update is not overlooked during
      implementation. The table content is already specified in the Files
      Changed entry for `testability-architecture.md` below (6 canonical test
      names, columns: name, source file, CMake target, label, description).

#### 6b. Update `tests/simulation/VehicleZoneTest.cpp` assertions

- [ ] Update all 6 `vehicleMeshPath()` assertions in
      `tests/simulation/VehicleZoneTest.cpp` to check extensionless base paths
      (e.g. `EndsWith("car_sedan_lod0")` instead of
      `EndsWith("car_sedan_lod0.b3d")`). No `IFileSystem*` parameter change
      needed — `vehicleMeshPath()` signature is unchanged.

#### 6c. Vendor patch: fix PLY loader 32-bit index threshold

- [ ] Locate `CPLYMeshFileLoader.cpp` in the vendored Irrlicht source
      (`build/_deps/` or `vcpkg_installed/`). At line ~233, change:
      `vertCount > 65565` → `vertCount > 65535`
      This is a one-line fix for an upstream off-by-30 typo that causes
      `CDynamicMeshBuffer` to assign 16-bit indices to meshes with
      65,536–65,565 vertices. Apply via a vcpkg overlay patch file
      (`vcpkg-overlays/irrlicht/fix-ply-32bit-threshold.patch`) so the
      fix persists across `vcpkg install` rebuilds.
- [ ] Create `vcpkg-overlays/irrlicht/portfile.cmake`: copy the upstream
      Irrlicht portfile from the pinned vcpkg baseline and append the
      patch via the `PATCHES` argument to `vcpkg_from_github()` (or
      equivalent). Follow the same structure as
      `vcpkg-overlays/openal-soft/portfile.cmake`.
- [ ] Create `vcpkg-overlays/irrlicht/vcpkg.json`: mirror the upstream
      Irrlicht port metadata at the pinned baseline. This file, together
      with `portfile.cmake` and the `.patch` file, constitutes a complete
      vcpkg overlay port — without all three files, `vcpkg install` will
      not discover or apply the patch.
- [ ] **Docker CI image rebuild note**: adding content to
      `vcpkg-overlays/` triggers `docker-ci-image.yml` (which watches
      `vcpkg-overlays/**`), producing a new Docker CI image. After the
      overlay lands on `develop`, wait for the image rebuild, then update
      the image digest in `ci.yml` and `.devcontainer/Dockerfile` in a
      follow-up commit (two-commit sequence, same pattern as openal-soft
      overlay introduction).

#### 7. Update `tools/validate_assets.py` for PLY discovery

- [ ] Checks that are hardcoded to `.b3d` must be updated to also discover and
      validate `.ply` files. At minimum the following checks are affected:
  - **Check 1** (file-presence): accept `_lodN.ply` as a valid LOD file
    alongside `_lodN.b3d`.
  - **Check 3** (triangle count + lightmap): load PLY geometry for tri-count
    validation when the PLY file is present. **PLY exemption for lightmap
    sub-check**: Check 3 also validates `_lod2_lm.dds` presence and DXT5
    format for `.b3d` LOD2 files; `.ply` LOD2 files are exempt from this
    lightmap sub-check because PLY meshes carry only UV channel 0 (no
    lightmap UV1). The script must still validate geometry presence and
    triangle budget for PLY LOD2 files.
  - **Check 4** (atlas UV bounds): PLY files carry UV channel 0 -- validate
    U/V within cell boundaries using `_parse_ply_uvs` (see Check 4c).
  - **Check 6** (LOD0 polygon budget per category): currently hardcodes
    `_lod0.b3d` for triangle-count validation; must discover `_lod0.ply`
    via PLY-first path resolution. **Tripo3D budget branching note**: the
    existing Check 6 thresholds (assembled LOD0 total ≤5,000 tris large /
    ≤1,500 tris small) apply only to hand-modeled assembled-stack buildings.
    Tripo3D building categories (`com_high_*`, `com_med_*`,
    `res_high_01`/`02`/`03` -- identified via the asset's `.meta` sidecar
    `category` field or the naming prefix `com_high`/`com_med`/`res_high_01`
    through `res_high_03`) must use the Tripo3D
    sub-row budget from the LOD Requirements table in
    `architecture/asset-standards/3d-model-standards.md` (≤510,000 tris
    LOD0). The implementation must branch on the asset's `.meta` category
    or naming prefix to select the correct threshold before comparing the
    parsed triangle count. Without this branching, Check 6 would
    incorrectly reject all Tripo3D LOD0 meshes (~489K–502K tris) as
    exceeding the assembled-stack 5,000/1,500-tri limit.
  - **Check 8** (pivot / origin): validate PLY mesh pivot (bounding-box
    center-bottom) within 5 mm tolerance.
  - **Note** (naming convention): `_lodN.ply` filenames are already validated
    by the file-discovery regex in **Check 1** (which accepts both `.b3d` and
    `.ply` extensions). No separate numbered check is needed for PLY naming.
  - **Check 10** (vehicle UV0 atlas cell): update PLY-first file discovery so
    vehicle `_lod0.ply` files are also validated against the assigned atlas
    cell using `_parse_ply_uvs` (see Check 4c) (check_10 validates vehicle UV
    channel 0 within assigned atlas cell, not LOD completeness — LOD
    completeness is implicitly covered by Check 1's file-presence validation
    accepting either `.ply` or `.b3d`).
  - **Check 11** (billboard / LOD2 co-existence): billboard `_billboard.dds`
    must NOT co-exist with `_lod2.b3d` OR `_lod2.ply` for the same base
    name -- update the co-existence check to test both extensions.
  - **Check 5** (UV1 lightmap): add PLY-format exemption -- skip UV1 validation
    for `_lodN.ply` files (PLY meshes carry only UV channel 0; UV1 lightmap is
    deferred to a future decimation phase per Notes section).
  - **Check 2** (floor-count conditional LOD2 prohibition): must detect
    `_lod2.ply` alongside `_lod2.b3d` -- if `height_floors <= 3`, flag the
    presence of either `_lod2.b3d` OR `_lod2.ply` as an error.
  - **Check 4b** (`_parse_b3d_positions` bounding box helper): the
    bounding-box center-bottom calculation used by check 8 (pivot) must also
    handle PLY vertex parsing, not only B3D VRTS parsing. Add a
    `_parse_ply_positions` helper (or extend the existing helper) to read PLY
    vertex data for bounding-box computation.
  - **Check 4c** (`_parse_ply_uvs` UV helper): Checks 4 and 10 validate UV
    channel 0 coordinates within atlas cell boundaries. The existing
    `_parse_b3d_uvs` helper reads UV data from B3D VRTS chunks; PLY files
    store UVs as `s`/`t` (or `u`/`v`) vertex properties. Add a
    `_parse_ply_uvs` helper that reads these properties from PLY vertex data
    and returns a list of (u, v) pairs. Reference this helper from Check 4
    (atlas UV bounds) and Check 10 (vehicle UV0 atlas cell) when the
    discovered file is `.ply`.
  - **Check 15** (`.meta` sidecar presence): currently globs exclusively for
    `*_lodN.b3d`; must also glob `*_lodN.ply` to discover PLY-only assets
    that require a `.meta` sidecar file.
  - **Check 32** (vehicle triangle budget): currently hardcodes
    `_lod0.b3d`/`_lod1.b3d` path construction; must also check
    `_lod0.ply`/`_lod1.ply` via the same PLY-first discovery logic used by
    `resolveModelPath()`. PLY triangle counting must read the triangle count
    from the PLY header's `element face` line (e.g. parse `element face 500000`
    to get 500,000 triangles) — vertex_count/3 is incorrect for indexed PLY
    meshes with shared vertices. The existing `_count_tris()` helper uses
    B3D-specific TRIS chunk headers and returns 0 for PLY files.
- [ ] **`_validate-assets.yml` guard loop update**: add `check_32` to the
      numbered guard loop (after `check_31`) inside the `Verify required checks
      present in validate_assets.py` step; update the file header comment from
      `Current highest check number: check_31` to
      `Current highest check number: check_32`. This is separate from the generic
      PLY guard step (`grep -qi "ply" ...`) which validates PLY-format discovery
      across multiple checks.
- [ ] **`_validate-assets.yml` Git LFS checkout**: add a selective LFS
      fetch step after checkout (do NOT use blanket `lfs: true` — that
      downloads ALL LFS objects including Tripo3D source zips/FBXs under
      `assets/tripo3d/`, which are not needed and could exceed the
      10-minute job timeout). Use `git lfs pull -I "assets/3d/**/*.ply"`
      to fetch only the PLY geometry files that content-parsing checks
      (3, 4, 4b, 6, 8, 32) require. Without this step, `.ply` files
      tracked by Git LFS (see `.gitattributes`:
      `assets/3d/**/*.ply filter=lfs ...`) remain as ~130-byte LFS
      pointer stubs — content-parsing checks will produce incorrect
      results or crash.
- [ ] **`_validate-assets.yml` PLY guard step**: add a new CI step
      "Verify PLY validation present in validate_assets.py" that runs
      `grep -q '\.ply' tools/validate_assets.py` (literal `.ply` extension,
      case-sensitive — avoids false positives from words like "apply") and
      fails if PLY support is absent; place it after the `check_32` guard step
      and before "Run asset validation". This is the generic PLY guard
      described in the CI spec (`architecture/ci-cd/github-actions-workflow.md`
      lines 1225–1231).
- [ ] This deliverable co-lands atomically with the C++ changes per the
      four-item atomicity rule: C++ source + test + `validate_assets.py` +
      CI must all be consistent in the same commit.

---

### Files Changed

| File | Change |
|------|--------|
| `src/rendering/mesh_format_utils.h` | **NEW** -- `resolveModelPath()` declaration-only header (forward-declares `irr::io::IFileSystem`; includes only `<string>`; NO `<IFileSystem.h>` or `<filesystem>`) |
| `src/rendering/mesh_format_utils.cpp` | **NEW** -- `resolveModelPath()` implementation (`#include <IFileSystem.h>`, `#include <filesystem>`); compiled into `aitown_render` via `target_sources(aitown_render PRIVATE src/rendering/mesh_format_utils.cpp)` |
| `src/rendering/BuildingAssetLoader.cpp` | 3 path constructions → `resolveModelPath()` |
| `src/rendering/BuildingAssetLoader.h` | Docstring update (`.b3d` → `.ply / .b3d`); clarify that `IFileSystem*` is obtained via `m_smgr->getFileSystem()` (no new member needed — `ISceneManager::getFileSystem()` is already available through the existing `m_smgr` pointer) |
| `src/rendering/vehicle_mesh_path.h` | 5 filenames → extensionless base paths (strip `.b3d` extension); no `#include "mesh_format_utils.h"` needed — `vehicleMeshPath()` is a pure string-construction helper with no external symbol dependencies |
| `src/rendering/IrrlichtRenderer.cpp` | Any remaining hardcoded `.b3d` vehicle paths |
| `src/benchmark/model_validator_main.cpp` | LOD suffix array updated to use `resolveModelPath()` |
| `tests/integration/MeshFormatUtilsTest.cpp` | **NEW** -- integration tests for `resolveModelPath()` (in `integration_tests` target, label `integration` -- pure logic + EDT_NULL IFileSystem tests, no OpenGL context needed) |
| `tests/simulation/VehicleZoneTest.cpp` | Update all 6 `vehicleMeshPath()` assertions to check extensionless base paths (e.g. `EndsWith("car_sedan_lod0")` instead of `EndsWith("car_sedan_lod0.b3d")`); no `IFileSystem*` parameter change needed -- `vehicleMeshPath()` signature is unchanged |
| `CMakeLists.txt` | `target_sources(aitown_render PRIVATE src/rendering/mesh_format_utils.cpp)`; `target_sources(integration_tests PRIVATE tests/integration/MeshFormatUtilsTest.cpp)` — `simulation_tests` does NOT gain an `aitown_render` link dependency because `vehicleMeshPath()` no longer calls `resolveModelPath()` (extensionless base path design) |
| `architecture/testing/framework.md` | Add `MeshFormatUtilsTest` to `integration_tests` file list in the `integration_tests` example block with comment `# Phase 11q12 — resolveModelPath() tests` |
| `architecture/testing/testability-architecture.md` | Add note to the `simulation_tests` linkage constraint paragraph confirming that the `simulation_tests`-does-not-link-Irrlicht invariant is preserved: `vehicleMeshPath()` returns extensionless base paths with no `resolveModelPath()` call, so no `aitown_render` link is needed. `MeshFormatUtilsTest` lives in `integration_tests` (not `simulation_tests`). Add a "Phase 11q12 Canonical Test Name Summary" table listing all 6 test cases (3 nullptr-fallback: PLY-present / B3D-only / neither-present; 3 IFileSystem* via EDT_NULL: PLY-present / B3D-only / neither-present) with the following canonical test names: (1) `MeshFormatUtilsTest.NullFS_PLYPresent_ReturnsPLYPath`, (2) `MeshFormatUtilsTest.NullFS_B3DOnly_ReturnsB3DPath`, (3) `MeshFormatUtilsTest.NullFS_NeitherPresent_ReturnsB3DPath`, (4) `MeshFormatUtilsIFSTest.IFileSystem_PLYPresent_ReturnsPLYPath`, (5) `MeshFormatUtilsIFSTest.IFileSystem_B3DOnly_ReturnsB3DPath`, (6) `MeshFormatUtilsIFSTest.IFileSystem_NeitherPresent_ReturnsB3DPath`; columns: canonical name, source file (`tests/integration/MeshFormatUtilsTest.cpp`), CMake target (`integration_tests`), label (`integration`), and one-sentence description — matching the per-phase convention at lines 2707--2718 |
| `tools/validate_assets.py` | Checks 1, 2, 3, 4, 4b, 4c, 6, 8, 10, 11, 15, 32 updated to discover and validate `.ply` files |
| `architecture/asset-standards/3d-model-standards.md` | Update LOD Requirements table row for "Small buildings / props (height_floors >= 4)" (spec line 17) to read `_lod2.b3d` or `_lod2.ply` geometry shell (currently B3D-only); update checks 2 and 11 to include `_lod2.ply` alongside `_lod2.b3d` for floor-count conditional LOD2 prohibition and billboard co-existence; update Check #3 (Large building `_lod2` presence/tri-budget — generalize file reference to `.b3d` or `.ply`); update `BuildingAssetLoader` LOD Loading Contract section (lines 208--223) to describe PLY-first loading via `resolveModelPath()` for steps 2/3/5 (replace hardcoded `_lod0.b3d`/`_lod1.b3d`/`_lod2.b3d` paths with PLY-first resolution); annotate Multi-buffer split paragraph (line 344) as the root-cause limitation that PLY format resolves for high-poly LOD0 meshes; update Building LOD File Naming Convention (lines 319--332) to accept `.ply` alongside `.b3d` for all building LOD levels; add PLY exemption to LOD2 shell lightmap requirement (line 542) and LOD2 blend mode (line 544) — generalize these annotations to clarify that the UV1 lightmap exemption applies to `.ply` assets at **all** LOD levels (LOD0, LOD1, LOD2), not only LOD2, matching the check #5 validation exemption and the Notes section of this plan; update V1 Minimum Building Coverage section (lines 442--455) to accept `_lod0.ply`/`_lod1.ply`/`_lod2.ply` alongside `.b3d` in the three mesh bullet points; update Vehicle LOD File Naming Convention section (lines 395--403) to include `_lod0.ply` / `_lod1.ply` alongside `.b3d` examples; update Service Building Model Standards naming convention (lines 464--471) to note that `.ply` format is technically supported by the loader and validator but is not expected for service buildings in V1 (their LOD0 budgets of 2,000--4,000 tris are far below the B3D 65,535-vertex-per-buffer limit, so service buildings retain `.b3d` only); update Commercial High Skyscraper Standards LOD2 strategy bullet (spec line 163) to read `_lod2.b3d` or `_lod2.ply`; update `.meta` Sidecar `height_floors` field description (spec line 307) to say `_lod2.b3d` or `_lod2.ply` in all six occurrences; update Service Building LOD strategy narrative (line 482) to read "No `_lod2.b3d` geometry shell" (B3D-only — no `.ply` addition, consistent with the service building B3D-retention policy); update Commercial Medium Tripo3D Buildings pipeline section (line 380) to add a LOD2 pipeline difference: "LOD2: billboard baking only (no geometry shell) — all com_med variants have height_floors 2--3, triggering the floor-count conditional LOD2 prohibition (check #2) and the billboard imposter path; skip the voxel-remesh LOD2 geometry shell step inherited from Commercial High"; update Tripo3D Asset Processing Pipeline section (lines 350--396): annotate pipeline steps 8 (line 363) and 9 (line 378) to note that PLY export via `convert_tripo3d_to_ply.py` replaces B3DWriter export for LOD0 meshes exceeding the 65K-vertex-per-buffer B3D limit, and add a brief PLY pipeline subsection documenting the tool's usage; append check #32 (vehicle LOD0/LOD1 triangle budget with PLY-first file discovery and PLY header face-count parsing) to the Export Validation Script Required Checks list after check #20 (line 589), and update the Phase assignment note (line 591) to include "Check #32 (vehicle triangle budget with PLY discovery) is a Phase 11q12 addition."; extend LOD Requirements table: (a) expand the "Commercial Medium Tripo3D variants" row from "(com_med_01/02)" to "(com_med_01–04)" to cover all four variants that have PLY LOD0 files and change its LOD2 column from "≤500 tris shell" to "Billboard (point-sprite only)" to match the floor-count conditional LOD2 prohibition (all com_med variants have height_floors 2–3, so check #2 mandates billboard-only — the previous "≤500 tris shell" annotation was inconsistent), (b) add a new "Residential High Tripo3D variants (res_high_01/02/03)" row with the same elevated Tripo3D budget structure as Commercial High (≤510,000 LOD0 / ≤8,000 LOD1 / ≤600 LOD2), (c) add actual tri counts for res_high, com_med_03/04, and com_high_04 to the "Actual V1 Tripo3D tri counts" table (com_high_04 is in the PLY commit scope but has no binding limit row yet); also update existing com_med_01 and com_med_02 LOD2 entries from `500` to `N/A (billboard)` to match the updated LOD Requirements billboard-only strategy (all com_med variants have height_floors 2--3, so check #2 mandates billboard-only -- no geometry shell LOD2 is produced), and set com_med_03/04 LOD2 to `N/A (billboard)` as well, (d) extend the Tripo3D pipeline narrative (line 352) to include res_high alongside existing vehicle and commercial categories, and add a cross-reference for res_high noting that it follows the same pipeline as Commercial High (footprint_tiles=3, same scale/orientation conventions); update Residential High Building Variant Geometry Standards section (line ~727) to annotate that res_high_01/02/03 are subject to the Residential High Tripo3D sub-row budgets in the LOD Requirements table (same cross-reference pattern as the Commercial High section at line ~144: "These assets are subject to the Residential High Tripo3D sub-row budgets in the LOD Requirements table"), while noting that res_high_04 retains the general large-building budget |
| `architecture/graphics-architecture/model-validator-tool.md` | Update lines referencing `.b3d`-only to include PLY-first loading; adjust Phase 11d Asset Inventory table to note LOD0 may be `.ply` |
| `architecture/graphics-architecture/scene-graph-ownership.md` | Update `vehicleMeshPath()` documentation to note extensionless return value (signature unchanged — no `IFileSystem*` parameter); add PLY row to asset-source summary table; document that callers in `IrrlichtRenderer` wrap through `resolveModelPath()` at mesh load time; add PLY bounding-box loader contract note adjacent to existing B3D exemption (line ~202): "`CPLYMeshFileLoader` populates `CDynamicMeshBuffer` and calls `recalculateBoundingBox()` on each buffer before wrapping in `SAnimatedMesh`; `SAnimatedMesh` aggregates buffer BBs into its mesh-level `Box` — satisfying the mandatory two-level recalculation rule (buffer + parent mesh). No explicit `recalculateBoundingBox()` is required for PLY assets loaded via `getMesh()` provided both levels are confirmed during the Phase 11q12 source inspection exit criterion." |
| `architecture/ci-cd/github-actions-workflow.md` | Add Phase 11q12 entry to validate-assets phasing summary with PLY guard step; update `_validate-assets.yml`, `_package-linux-deb.yml`, and `_package-windows.yml` spec sections to document selective LFS fetch step (`git lfs pull -I "assets/3d/**/*.ply"`) — blanket `lfs: true` must NOT be used because it downloads all LFS objects including Tripo3D source zips/FBXs, risking timeout on validate-assets (10 min budget) and bloating distributed packages with source assets |
| `architecture/asset-standards/2d-texture-standards.md` | Extend billboard `_lod2` mutual-exclusion rule to `.ply`; add PLY UV channel 1 exemption to UV & Atlas Strategy section |
| `.github/workflows/_validate-assets.yml` | Add PLY guard step (`grep -q '\.ply' tools/validate_assets.py` — literal extension, case-sensitive); add `check_32` to the numbered guard loop (after `check_31`); update header comment to "Current highest check number: check_32"; add selective LFS fetch step after checkout: `git lfs pull -I "assets/3d/**/*.ply"` (do NOT use blanket `lfs: true` — avoids downloading Tripo3D source zips/FBXs) |
| `.github/workflows/_package-linux-deb.yml` | Add `git-lfs` to the `apt-get install` step (bare Debian/Ubuntu containers lack git-lfs); add selective LFS fetch step after checkout: `git lfs pull -I "assets/3d/**/*.ply"` (do NOT use blanket `lfs: true` — prevents Tripo3D source zips from being included in the distributed .deb package via the `install(DIRECTORY assets/ ...)` CPack rule) |
| `.github/workflows/_package-windows.yml` | Add selective LFS fetch step after checkout: `git lfs pull -I "assets/3d/**/*.ply"` (do NOT use blanket `lfs: true` — prevents Tripo3D source zips from being included in the NSIS installer) |
| `.gitattributes` | Add LFS tracking rule for PLY files: `assets/3d/**/*.ply filter=lfs diff=lfs merge=lfs -text`. Must co-land atomically with the PLY file commits so that `git lfs track` converts them to LFS pointers before they enter the object store. Without this rule, committing ~70 PLY files as regular git objects permanently bloats the repository |
| `vcpkg-overlays/irrlicht/fix-ply-32bit-threshold.patch` | **NEW** — one-line vendor patch fixing `CPLYMeshFileLoader.cpp` line ~233: `vertCount > 65565` → `vertCount > 65535` (upstream off-by-30 typo in the `EIT_32BIT` index type threshold) |
| `vcpkg-overlays/irrlicht/portfile.cmake` | **NEW** — overlay portfile that copies the upstream Irrlicht portfile and applies `fix-ply-32bit-threshold.patch` via the `PATCHES` argument (same pattern as `vcpkg-overlays/openal-soft/portfile.cmake`) |
| `vcpkg-overlays/irrlicht/vcpkg.json` | **NEW** — overlay port metadata mirroring the upstream Irrlicht port at the pinned vcpkg baseline; required alongside `portfile.cmake` and the `.patch` file for `vcpkg install` to discover and apply the overlay |

### Lines of Code Estimate

~50 lines new (`mesh_format_utils.h` + `mesh_format_utils.cpp` + test), ~25 lines changed across
existing C++ files, ~150–250 lines changed in `validate_assets.py` (13 checks
updated for PLY discovery + 2 new PLY-parsing helpers + check #32 extension),
~40 lines across CI workflow YAML files (`_validate-assets.yml`,
`_package-linux-deb.yml`, `_package-windows.yml`), ~1 line in `.gitattributes`.
No new dependencies (only `<filesystem>` added in the `.cpp`, which is already
available in GCC 12+ with `-lstdc++fs` if needed).

### Exit Criteria

- [ ] `grep -rn '\.b3d' src/rendering/` returns zero hardcoded path
      constructions (only comments and format-description strings).
- [ ] `grep -rn '\.b3d' src/benchmark/model_validator_main.cpp` returns zero
      hardcoded path constructions (only comments).
- [ ] `resolveModelPath()` integration tests pass (all six cases: three
      nullptr-fallback — PLY-present, B3D-only, neither-present — and three
      IFileSystem* via EDT_NULL — PLY-present, B3D-only, neither-present). These run under
      `ctest -L "^integration$"`, consistent with the test's placement in
      `integration_tests`.
- [ ] All 6 `vehicleMeshPath()` assertions in `VehicleZoneTest.cpp` pass with
      extensionless base path expectations (e.g. `EndsWith("car_sedan_lod0")`).
- [ ] Existing B3D-only assets (original `ind_*`, `res_*`) still load
      correctly (fallback path exercised).
- [ ] When PLY files are present alongside B3D files, the PLY is loaded
      (verified at `IrrlichtRenderer` call sites via `resolveModelPath()`,
      not in `vehicleMeshPath()`; confirmed by log output or debugger).
- [ ] At least one high-poly PLY asset with >65,535 vertices per buffer
      (e.g. `com_high_01_lod0.ply` at ~495K tris) loads and renders correctly
      in the model validator (`--model com_high_01`) without segfault or
      visible geometry corruption; verify via debug log that
      `CDynamicMeshBuffer` index type is `EIT_32BIT`. This is the primary
      validation that the 16-bit index truncation root cause is resolved.
- [ ] Vendor patch applied: `CPLYMeshFileLoader.cpp` threshold reads
      `vertCount > 65535` (not the upstream typo `65565`). Verify by
      inspecting the patched source after `vcpkg install`. The overlay
      port is complete: `vcpkg-overlays/irrlicht/` contains
      `portfile.cmake`, `vcpkg.json`, and
      `fix-ply-32bit-threshold.patch` — all three files are required for
      `vcpkg install` to discover and apply the patch.
- [ ] `validate_assets.py` PLY checks pass: all **committed** `.ply` building
      and vehicle assets pass checks 1, 2, 3, 4, 4b, 4c, 5, 6, 8, 10, 11, 15, 32
      with PLY support enabled. **Note**: `_lod2.ply` files for any asset
      whose `.meta` sidecar has `height_floors <= 3` must NOT be committed —
      they violate the floor-count conditional LOD2 prohibition (check #2).
      This includes all `res_low_*`, `res_med_*` (height_floors 2–3),
      `com_low_*`, small `com_med_*` variants (height_floors <= 3), and all
      `svc_*` variants. Remove or exclude these files before running
      validation; only `_lod0.ply` and `_lod1.ply` files (and `_lod2.ply` for
      assets with `height_floors >= 4`) are valid commit candidates.
      **Variant limit**: only variants 01–04 per zone-tier combination
      (e.g. `com_high_01` through `com_high_04`) and the four service
      building types have atlas cell assignments in
      `building-atlas-layout.md` and `.meta` sidecars. PLY files for
      variants 05+ (e.g. `com_high_05_lod0.ply`, `com_low_05_lod0.ply`,
      `res_low_05_lod0.ply`) must be excluded from the commit — they have
      no atlas cell (fail check #4) and no `.meta` sidecar (fail
      check #15).
      **PLY commit scope (by category)**: PLY files are only valid commit
      candidates for categories whose Tripo3D LOD0 meshes exceed the
      B3D 65,535-vertex-per-buffer limit: `com_high_01`–`04`,
      `com_med_01`–`04`, `res_high_01`–`03`, and the five vehicle types.
      All other categories (`res_low_*`, `res_med_*`, `com_low_*`,
      `ind_*`, `svc_*`) have LOD budgets that fit within B3D's 16-bit
      index limit and must retain B3D format — do NOT commit PLY files
      for these categories even if generated by the conversion tool.
      **Conversion tool prerequisite**: before committing PLY files,
      verify that `tools/convert_tripo3d_to_ply.py`'s MANIFEST atlas
      cell assignments (`atlas_row`/`atlas_col`) match the canonical
      values in each variant's `.meta` sidecar and
      `building-atlas-layout.md`. Also verify `footprint` values match
      the spec (service buildings use footprint=2 per
      `3d-model-standards.md` line 239). If mismatches exist, fix the
      MANIFEST and regenerate affected PLY files before validation.
      Exit-criteria check #4 (atlas UV bounds) catches atlas-cell
      mismatches; check #8 (pivot) catches footprint/scale errors.
- [ ] PLY bounding-box loader contract verified (two-level): inspect
      `CPLYMeshFileLoader.cpp` in the vendored Irrlicht source
      (`build/_deps/` or `vcpkg_installed/`) and confirm:
      (a) buffer-level — each `CDynamicMeshBuffer` has
      `recalculateBoundingBox()` called after vertex data load;
      (b) mesh-level — the wrapping `SAnimatedMesh` has its bounding box
      computed (either via explicit `recalculateBoundingBox()` or by
      `SAnimatedMesh` constructor aggregating buffer BBs into its `Box`
      field). Both levels are required per the mandatory two-level
      recalculation rule in `scene-graph-ownership.md` lines 34–36.
      If either call is absent, add a defensive recalculation in the
      PLY loading path or immediately after `getMesh()` returns in
      `IrrlichtRenderer`: buffer-level via
      `buf->recalculateBoundingBox()` (on the `IMeshBuffer` interface);
      mesh-level via `animMesh->setBoundingBox(aggregatedBox)` where
      `aggregatedBox` is computed by iterating all mesh buffers and
      unioning their bounding boxes — `setBoundingBox()` is on the
      `IMesh` interface and callable through `IAnimatedMesh*`. Do NOT
      use `animMesh->recalculateBoundingBox()` — that method is on
      `SAnimatedMesh` (concrete), not `IAnimatedMesh` (interface), and
      Irrlicht is compiled with `-fno-rtti` so downcasting is
      prohibited per `scene-graph-ownership.md` lines 153–164.
- [ ] Build succeeds on Linux (`make build`).
- [ ] All existing tests pass (`make test`).

### Notes

- **PLY UV channel limitation**: PLY-loaded meshes carry only UV channel 0
  (diffuse atlas coordinates). They do NOT carry a lightmap UV channel 1.
  This means PLY assets are **exempt from the UV1 (lightmap) requirement**
  that applies to hand-authored B3D models. Lightmap baking for these
  high-poly Tripo3D assets at all LOD levels (LOD0, LOD1, LOD2) is deferred to a future
  decimation-then-B3D-re-export phase (see `post-v1-backlog.md`).
  `validate_assets.py` must not flag missing UV1 on `.ply` files.
