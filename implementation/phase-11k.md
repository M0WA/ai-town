## Phase 11k: CI Deduplication & Workflow Split

**Status: TODO**

### Goal

`ci.yml` has grown to ~1 500 lines with two categories of problems:

1. **Step duplication** — `build-linux`, `build-windows`, and `coverage-linux` each
   contain identical or near-identical copies of seven preflight steps (SHA lint,
   asset validation, clouds/font/audio/terrain presence checks, vcpkg baseline). Every
   change must be applied in three places; a copy-paste error silently diverges one job.

2. **Single-file size** — all job definitions live in one file, making navigation and
   focused review difficult as the pipeline grows.

This phase fixes both problems:

- **Step deduplication**: introduce a `supply-chain-lint` job (new); extend the existing
  `validate-assets` job with the remaining duplicated preflight checks; wire all three
  build jobs to `needs: [supply-chain-lint, validate-assets]` and remove their local
  copies of those steps.

- **Workflow split**: extract each job into its own reusable workflow file under
  `.github/workflows/` (prefixed `_` to mark them as called, not triggered directly).
  `ci.yml` becomes a thin orchestrator whose jobs use `uses:` to call the reusable
  files; all `needs:` and gate wiring remain in `ci.yml`.

No source code, CMakeLists.txt, Dockerfile, `vcpkg.json`, `Makefile`, or
`architecture/` spec files are changed.

---

### Deliverables

#### 1. New job: `supply-chain-lint`

- [ ] Job added before all build jobs in `.github/workflows/ci.yml`.
- [ ] Runs on `ubuntu-latest` with no container.
- [ ] Steps:
  - Checkout (`actions/checkout`).
  - Bash step: SHA lint logic (updated from ci.yml-only to all-*.yml glob) — checks for placeholder `<TOKEN>` strings,
    short SHAs, and missing container image digest pins; must grep **all**
    `.github/workflows/*.yml` files (not just `ci.yml`) so that the reusable
    workflow files (`_build-linux.yml`, `_coverage-linux.yml`, etc.) are covered.
- [ ] No `needs:` on this job (runs immediately on push/PR).

#### 2. Extend existing `validate-assets` job

The `validate-assets` job already exists (runs `tools/validate_assets.py` and shader
checks). The three build jobs do not currently depend on it — they carry their own
duplicate preflight steps. This deliverable moves those duplicates into
`validate-assets` and wires the dependency.

- [ ] `needs: [supply-chain-lint]` added to `validate-assets` (ensures SHA lint passes
  before any asset work begins; no throughput cost — validate-assets is fast).
- [ ] Steps added to `validate-assets` (in order, after existing steps):

  > Note: the existing job steps — checkout, Python setup, `pip install mutagen Pillow`,
  > `sudo apt-get install ffmpeg`, all `Verify check_N present` guard steps, and
  > `Run asset validation` — are preserved unchanged. The five steps below are appended
  > after `Run asset validation`.

  1. Validate vcpkg baseline consistency: `jq` reads `builtin-baseline` from
     `vcpkg.json` and compares to `VCPKG_COMMIT_ID`; exits non-zero on mismatch.
  2. Verify `assets/textures/sky/clouds.png` exists (bash `test -f`).
  3. Verify font assets present: `assets/fonts/hud_font.xml` and
     `assets/fonts/hud_mono_font.xml`.
  4. Verify Phase 10 audio assets present: `sfx_build_place.wav`,
     `sfx_build_demolish.wav`, `stinger_milestone.wav`, `ambient_day.ogg`,
     `sfx_zone_residential.ogg`, `sfx_zone_commercial.ogg`,
     `sfx_zone_industrial.ogg`.
  5. Verify terrain assets present: `terrain_grass_d.dds`,
     `terrain_asphalt_d.dds`, `terrain_soil_d.dds`, `terrain_concrete_d.dds`
     (diffuse DDS; normal-map validation is out of scope for this phase) +
     `assets/textures/terrain/terrain_chunk_splat.png` + 2 shader files
     (`assets/shaders/terrain.vert`, `assets/shaders/terrain.frag`).
- [ ] Note: `pip install Pillow` / `pip install mutagen` already present in the job
  (from existing check_21–#23 steps) — no new pip step needed for the above.

#### 3. `build-linux` changes

- [ ] `needs:` updated to `[supply-chain-lint, validate-assets]` (appended to any existing
  `needs:` entries).
- [ ] Removed steps:
  - "Lint workflow for placeholder SHAs" (supply-chain lint).
  - `pip install Pillow` + `python3 tools/validate_assets.py` (preflight asset validation).
  - Verify `clouds.png` present (preflight).
  - Verify font assets present (preflight).
  - Verify Phase 10 audio assets present.
  - Verify terrain assets present.
  - Validate vcpkg baseline consistency.
  - Post-build font assets duplicate (step previously after build — pure duplicate of
    preflight font check; removed entirely).
- [ ] Step number comments in the job updated so there are no gaps.

#### 4. `build-windows` changes

- [ ] `needs:` updated to `[supply-chain-lint, validate-assets]`.
- [ ] Removed steps:
  - "Lint workflow for placeholder SHAs" (supply-chain lint).
  - `pip install Pillow` (was Windows-specific; now runs once in `validate-assets`).
  - `python tools/validate_assets.py` (preflight asset validation).
  - Verify `clouds.png` present (preflight, PowerShell syntax).
  - Verify font assets present (preflight, PowerShell syntax).
  - Verify Phase 10 audio assets present (PowerShell syntax).
  - Verify terrain assets present (PowerShell syntax).
  - Validate vcpkg baseline consistency.
  - Post-build font assets duplicate (PowerShell, post-build position; removed entirely).
- [ ] Step number comments in the job updated so there are no gaps.

#### 5. `coverage-linux` changes

- [ ] `needs:` updated to `[supply-chain-lint, validate-assets]`.
- [ ] Removed steps (identical list to `build-linux`):
  - "Lint workflow for placeholder SHAs".
  - `pip install Pillow` + `python3 tools/validate_assets.py`.
  - Verify `clouds.png` present (preflight).
  - Verify font assets present (preflight).
  - Verify Phase 10 audio assets present.
  - Verify terrain assets present.
  - Validate vcpkg baseline consistency.
  - Post-build font assets duplicate (removed entirely).
- [ ] Step number comments in the job updated so there are no gaps.

#### 6. `all-checks-pass` gate job changes

- [ ] `supply-chain-lint` added to `needs:` list.
- [ ] `if: always()` condition preserved (required for branch protection to work on
  cancelled runs — do not remove).
- [ ] Note: `validate-assets` is already in `all-checks-pass` `needs:` — no change
  needed for that entry.

---

#### 7. Split `ci.yml` into reusable workflow files

GitHub Actions supports **reusable workflows** (`on: workflow_call:`). A job in the
calling workflow uses `jobs.<id>.uses: ./.github/workflows/<file>.yml` instead of
inline `steps:`. `needs:`, `if:`, and gate wiring remain in the calling `ci.yml`.

**New files to create** (all under `.github/workflows/`, `_` prefix marks called-only):

| File | Contents | Inputs needed |
|---|---|---|
| `_supply-chain-lint.yml` | SHA lint bash step | none |
| `_validate-assets.yml` | All asset validation + vcpkg baseline steps | `vcpkg_commit_id: string` |
| `_build-linux.yml` | Full `build-linux` job steps | `vcpkg_commit_id: string` |
| `_build-windows.yml` | Full `build-windows` job steps | `vcpkg_commit_id: string` |
| `_coverage-linux.yml` | Full `coverage-linux` job steps | `vcpkg_commit_id: string` |
| `_markdown-lint.yml` | Markdown lint steps | none |
| `_package-windows.yml` | NSIS installer steps | `vcpkg_commit_id: string` |
| `_package-linux-deb.yml` | Debian/Ubuntu `.deb` matrix steps | `vcpkg_commit_id: string` |

Each reusable workflow declares:

```yaml
on:
  workflow_call:
    inputs:
      vcpkg_commit_id:   # only for files that need it
        type: string
        required: true
```

`VCPKG_COMMIT_ID` is declared as a workflow-level `env:` in `ci.yml` only. The caller
passes it via `with: vcpkg_commit_id: ${{ env.VCPKG_COMMIT_ID }}` on each job that
needs it. Reusable workflows do not inherit the caller's `env:` automatically.

**`ci.yml` after the split** — thin orchestrator only:

```yaml
# Only top-level keys remain: name, on, env (VCPKG_COMMIT_ID), permissions, jobs.
# Each job uses: ./.github/workflows/_<name>.yml  (no inline steps:).
# needs:, if:, with:, and permissions: are set on the calling-side job entry.
jobs:
  supply-chain-lint:
    uses: ./.github/workflows/_supply-chain-lint.yml
    permissions: { contents: read }

  validate-assets:
    needs: [supply-chain-lint]
    uses: ./.github/workflows/_validate-assets.yml
    with:
      vcpkg_commit_id: ${{ env.VCPKG_COMMIT_ID }}
    permissions: { contents: read }

  build-linux:
    needs: [supply-chain-lint, validate-assets]
    uses: ./.github/workflows/_build-linux.yml
    with:
      vcpkg_commit_id: ${{ env.VCPKG_COMMIT_ID }}
    permissions: { packages: read, checks: write, contents: read }

  build-windows:
    needs: [supply-chain-lint, validate-assets]
    uses: ./.github/workflows/_build-windows.yml
    with:
      vcpkg_commit_id: ${{ env.VCPKG_COMMIT_ID }}
    permissions: { checks: write, contents: read }

  coverage-linux:
    needs: [supply-chain-lint, validate-assets]
    uses: ./.github/workflows/_coverage-linux.yml
    with:
      vcpkg_commit_id: ${{ env.VCPKG_COMMIT_ID }}
    permissions: { packages: read, checks: write, contents: read }

  markdown-lint:
    uses: ./.github/workflows/_markdown-lint.yml
    permissions: { contents: read }

  all-checks-pass:
    needs: [supply-chain-lint, validate-assets, build-linux, build-windows,
            coverage-linux, markdown-lint]
    runs-on: ubuntu-latest
    if: always()
    steps: ...   # inline — not a reusable workflow (simple gate script)

  package-windows:
    needs: [build-windows]
    if: github.event_name == 'push' && ...
    uses: ./.github/workflows/_package-windows.yml
    with:
      vcpkg_commit_id: ${{ env.VCPKG_COMMIT_ID }}
    permissions: { contents: read }

  package-linux-deb:
    needs: [build-linux]
    if: github.event_name == 'push' && ...
    uses: ./.github/workflows/_package-linux-deb.yml
    with:
      vcpkg_commit_id: ${{ env.VCPKG_COMMIT_ID }}
    permissions: { contents: read }
```

**Implementation notes:**

- [ ] `all-checks-pass` stays as inline `steps:` in `ci.yml` — it is a simple bash
  gate script and does not benefit from being a reusable workflow.
- [ ] Supply-chain lint in `_supply-chain-lint.yml` must grep **all**
  `.github/workflows/*.yml` files (not just `ci.yml`) so digest-pin and SHA
  checks cover the new reusable workflow files too.
- [ ] Container image digest pins (`ghcr.io/...@sha256:...`) move into
  `_build-linux.yml` and `_coverage-linux.yml` where the container is declared.
  The supply-chain lint glob catches them there.
- [ ] Matrix strategy (`strategy: matrix:`) for `package-linux-deb` is declared
  inside `_package-linux-deb.yml`, not in the caller — reusable workflows own
  their own `strategy:`.
- [ ] `VCPKG_COMMIT_ID` must be updated in exactly one place (`ci.yml` `env:`) for
  the baseline-atomicity rule to remain satisfied.
- [ ] `ilammy/msvc-dev-cmd` SHA pin moves into `_build-windows.yml`; must be checked
  by the supply-chain lint glob.

---

### Exit Criteria

- [ ] `supply-chain-lint` job exists; no duplicated SHA lint step in any build job.
- [ ] `validate-assets` gains `needs: [supply-chain-lint]` and the five new presence
  steps (clouds, fonts, audio, terrain, vcpkg baseline); existing check_N steps
  and Python setup are preserved unchanged.
- [ ] `build-linux` has `needs: [supply-chain-lint, validate-assets]` and zero
  duplicated preflight steps (including the post-build font duplicate).
- [ ] `build-windows` has `needs: [supply-chain-lint, validate-assets]` and zero
  duplicated preflight steps (including `pip install Pillow` and post-build font
  duplicate).
- [ ] `coverage-linux` has `needs: [supply-chain-lint, validate-assets]` and zero
  duplicated preflight steps (including the post-build font duplicate).
- [ ] `all-checks-pass` includes `supply-chain-lint` in its `needs:` list and retains
  `if: always()`.
- [ ] Eight reusable workflow files exist under `.github/workflows/` with `_` prefix,
  each declared with `on: workflow_call:` and the correct `inputs:` block.
- [ ] `ci.yml` contains no inline `steps:` for any job except `all-checks-pass`.
- [ ] Supply-chain lint in `_supply-chain-lint.yml` greps all `*.yml` files in
  `.github/workflows/` (not just `ci.yml`).
- [ ] CI passes: all build jobs run, coverage gate enforced, `all-checks-pass` green.
- [ ] No changes to CMakeLists.txt, source files, Dockerfiles, `vcpkg.json`,
  `Makefile`, or any `architecture/` spec file.

---

### Sign-offs

| Role | Area | Status |
|---|---|---|
| `cicd-dev-github` | supply-chain-lint, validate-assets extension, needs wiring, reusable workflow split, gate job | |
