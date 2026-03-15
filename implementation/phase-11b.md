## Phase 11b: Pre-Built CI Docker Image

**Status: Planned**

### Goal

Eliminate cold-start overhead in Linux CI jobs by shipping a pre-built Docker image
to GitHub Container Registry (GHCR). The devcontainer extends this image — the CI
image is the single source of truth for the build toolchain and vcpkg binary cache.

### Motivation

Every CI run currently repeats:

- `apt-get install` of ~8 system packages (~30–60 s)
- `pip install mutagen Pillow` (~10–15 s)
- `lukka/run-vcpkg` cloning + bootstrapping vcpkg (~60 s)
- vcpkg manifest-mode compilation of all declared ports + transitive deps
  (~10–15 min on a cold binary cache; cache misses triggered by any baseline bump
  or dependency change)

The vcpkg binary cache (`actions/cache`) mitigates warm runs but does not help on
any cache miss. The Docker image approach eliminates the cold-start penalty entirely
by baking all of the above into a versioned, content-addressed image.

### Architecture

```text
docker/ci-linux/Dockerfile     ← CI base image (toolchain + vcpkg binary cache)
.devcontainer/Dockerfile       ← FROM ci-linux image + developer ergonomics layer
.github/workflows/docker-ci-image.yml  ← builds & pushes the CI image
.github/workflows/ci.yml       ← build-linux / coverage-linux use container: image:
```

The devcontainer extends the CI image via `FROM ghcr.io/OWNER/aitown-ci-linux:<tag>`.
This gives zero-drift between local dev and CI: the same toolchain, system libraries,
vcpkg binary cache, and CMake version are used in both contexts.

### Deliverables

- [ ] **`docker/ci-linux/Dockerfile`** — new CI base image:
  - Base: `debian:trixie` (same as devcontainer, for GCC 13 ABI parity)
  - Toolchain: `gcc-13`, `g++-13`, `ninja-build`, `cmake` (3.31.10 pinned),
    `ccache`, `pkg-config`, `git`, `jq`, `python3`, `pip`
  - CI runtime tools: `xvfb`, `libgl1-mesa-dev`, `mesa-utils`, `libxxf86vm-dev`,
    `libglew-dev`, `lcov`, `ffmpeg`, `sox`
  - Python packages: `mutagen`, `Pillow`
  - `ARG VCPKG_COMMIT` — must match `VCPKG_COMMIT_ID` in `ci.yml` (enforced by
    validation step in `docker-ci-image.yml`; see Atomicity Contract below)
  - vcpkg cloned and bootstrapped at `$VCPKG_COMMIT`
  - `COPY vcpkg-overlays/ /build/vcpkg-overlays/` — the overlay directory is
    copied into the Docker build context so that the openal-soft 1.23.1 pin is
    available during the vcpkg install layer inside the image
  - vcpkg install is invoked with `--overlay-ports=/build/vcpkg-overlays` to
    ensure openal-soft builds from the 1.23.1 pin and not the upstream vcpkg port
  - Full vcpkg binary cache pre-populated for `x64-linux` (all declared ports +
    transitive deps compiled with GCC 13): irrlicht, openal-soft (overlay-pinned
    1.23.1), libvorbis, fmt, glew, gtest, rapidcheck, and all transitive deps
  - vcpkg binary cache stored at `/root/.cache/vcpkg` inside the image
  - **Excluded**: Claude Code, zsh/fzf/delta, compressonator, Powerline10k,
    scrot, man-db, Node.js, `DEVCONTAINER=true` — devcontainer-only tooling
    is never in the CI image

- [ ] **`.devcontainer/Dockerfile` updated** — change first line to:

  ```dockerfile
  FROM ghcr.io/OWNER/aitown-ci-linux:<content-addressed-tag>
  ```

  then layer all existing developer-ergonomics tooling on top (Node.js, zsh, fzf,
  git-delta, Claude Code, compressonator, markdownlint-cli, etc.). All existing
  `ARG VCPKG_COMMIT` and vcpkg bootstrap lines are removed (now provided by base).
  The devcontainer must NOT re-bootstrap vcpkg or re-populate the binary cache.

- [ ] **`.github/workflows/docker-ci-image.yml`** — new workflow, builds and pushes
  the CI image:
  - **Triggers**:
    1. Push to `main` or `develop` that touches `docker/ci-linux/Dockerfile`,
       `vcpkg.json`, or `vcpkg-overlays/**` (this last path pattern ensures image
       rebuilds are triggered whenever the openal-soft overlay pin changes)
    2. `workflow_dispatch` with optional `force_rebuild: boolean` input
    3. Monthly scheduled run (1st of month, 02:00 UTC) for OS security updates
  - **Permissions**: `packages: write`, `contents: read`
  - **`VCPKG_COMMIT_ID` extraction step** (runs before the validation step): uses
    `grep` (or `yq`) to extract the value of `VCPKG_COMMIT_ID` from `ci.yml` and
    export it to `$GITHUB_ENV`. Subsequent steps consume it as
    `${{ env.VCPKG_COMMIT_ID }}`. This step is separate from and must precede both
    the validation step and the build step, because `$GITHUB_ENV` writes are not
    visible within the same step (per CLAUDE.md toolchain ordering rule).
  - **Validation step** (runs after extraction, before docker build): reads
    `ARG VCPKG_COMMIT` from `docker/ci-linux/Dockerfile` and compares it to
    `${{ env.VCPKG_COMMIT_ID }}` exported in the extraction step. Fails the
    workflow if the two values diverge, enforcing the atomicity contract.
  - **Build step**: `docker/build-push-action` with:
    - `cache-from: type=gha` / `cache-to: type=gha,mode=max` (GHA layer cache
      so re-runs after small changes skip the multi-hour vcpkg compilation layer)
    - `build-args: VCPKG_COMMIT=${{ env.VCPKG_COMMIT_ID }}`
  - **Tags pushed to `ghcr.io/OWNER/aitown-ci-linux`**:
    - `:vcpkg-<short-sha>` — content-addressed by vcpkg commit (7 hex chars;
      this is the tag used in `ci.yml` and `.devcontainer/Dockerfile`)
    - `:latest` — convenience alias; never pinned in code
  - **Digest output**: the push step outputs the image digest (`sha256:...`); the
    workflow prints it so the committer can pin `ci.yml` by digest (see below)

- [ ] **`.github/workflows/ci.yml` updated** — `build-linux` and `coverage-linux`
  jobs:
  - Add `container:` spec:

    ```yaml
    container:
      image: ghcr.io/OWNER/aitown-ci-linux@sha256:<digest>
      options: --user root
    ```

  - **Remove** the "Install system dependencies" `apt-get install` step
  - **Remove** the "Install Pillow" `pip install Pillow` step
  - **Remove** `lukka/run-vcpkg` step (vcpkg binary cache is in the image)
  - **Remove** `mutagen` `pip install` step (pre-installed in image)
  - Retain `hendrikmuhs/ccache-action` — it caches compiled project source
    (`src/`) per-PR, which is separate from the vcpkg binary cache
  - Retain the GCC version detect step (writes to `$GITHUB_ENV` before
    `actions/cache` — ordering constraint still applies per CLAUDE.md)
  - Add the following complete job-level permissions block to both `build-linux`
    and `coverage-linux` (all three keys are required — omitting any one silently
    overrides the workflow-level permissions and breaks the corresponding step):

    ```yaml
    permissions:
      packages: read    # pull from GHCR
      checks: write     # required by dorny/test-reporter
      contents: read    # required by actions/checkout
    ```

  - `build-windows` and all other jobs: **unchanged**

- [ ] **Supply-chain lint extended** — add a lint step (either extending the
  existing supply-chain lint in `ci.yml` or adding a dedicated step in
  `docker-ci-image.yml`) that validates container image digest pinning:
  - Grep all workflow files for `container:` blocks.
  - Reject any `image:` value that does not match the pattern
    `@sha256:[0-9a-f]{64}` (a 64-character hex digest).
  - The lint must fail the workflow if a mutable tag (e.g., `:vcpkg-abc1234`
    or `:latest`) is used without a digest pin.
  - The existing supply-chain lint only validates 40-character hex SHAs in
    `uses:` action lines; it does NOT cover `container: image:` fields — this
    new/extended step closes that gap.

- [ ] **`CLAUDE.md` updated** — "Build & Toolchain" notes section gains:
  - CI image tag update procedure as part of the vcpkg baseline update protocol.
    This phase **introduces** a five-item atomicity rule for vcpkg baseline bumps
    (it is not an extension of any prior rule). The five items are: `vcpkg.json`,
    `VCPKG_COMMIT_ID` in `ci.yml`, `ARG VCPKG_COMMIT` in
    `docker/ci-linux/Dockerfile`, Dockerfile image tag in `ci.yml` and
    `.devcontainer/Dockerfile`, digest pin in `ci.yml`
  - Note: devcontainer `FROM` tag must be updated in the same PR as the CI
    image tag bump in `ci.yml`

- [ ] **xvfb-run spike PR** — before `ci.yml` is switched to `container:` mode,
  a standalone spike PR must be merged that verifies `xvfb-run` succeeds for
  `requires-opengl` tests inside the Docker container on a GitHub-hosted runner.
  The spike PR description must document the outcome (pass or required workaround).
  This spike is a blocking prerequisite for the Exit Criteria gate below.

### Atomicity Contract

Any vcpkg baseline bump MUST land these five changes atomically in one PR:

1. `vcpkg.json` — `builtin-baseline` updated
2. `ci.yml` — `VCPKG_COMMIT_ID` env var updated
3. `docker/ci-linux/Dockerfile` — `ARG VCPKG_COMMIT` updated to same value
4. `.devcontainer/Dockerfile` — `FROM` tag updated to new `:vcpkg-<short-sha>`
5. `ci.yml` and `.devcontainer/Dockerfile` — image digest pin updated to the
   `sha256:...` value output by the `docker-ci-image.yml` push step

The `docker-ci-image.yml` validation step enforces items 2 and 3 agree at
image build time. The supply-chain lint step (see "Supply-chain lint extended"
deliverable above) enforces the digest pin is a full SHA256 (not a mutable
tag) — note that the existing lint covers only `uses:` action SHAs; the new
container-image lint step is required to cover `container: image:` fields.

**Sequencing prerequisite**: the xvfb-run spike PR (see Deliverables above)
must be merged before any PR that switches `ci.yml` to `container:` mode.
This is a hard sequencing constraint independent of the five-item atomicity
rule above.

### Image Pinning Policy

`ci.yml` and `.devcontainer/Dockerfile` MUST pin the CI image by digest:

```text
ghcr.io/OWNER/aitown-ci-linux@sha256:<64-hex-char-digest>
```

Using only the content-addressed tag (`:vcpkg-<short-sha>`) is insufficient
— Docker tags are mutable. Digest pinning follows the same principle as the
project's existing `uses: action@<40-char-SHA>` pinning for GitHub Actions.

### Trade-offs

| Factor | Notes |
|---|---|
| Cold-start savings | ~10–15 min eliminated per run (largest gain on cache misses) |
| Warm-run savings | ~2–3 min (apt/pip always ran even on vcpkg cache hits) |
| Image build time | ~30–60 min first build; subsequent rebuilds fast via GHA layer cache |
| GHCR storage | ~1.5–2 GB per image; free on public repos |
| Determinism | Locks toolchain + syslibs — no `ubuntu-latest` GCC rotation surprises |
| Devcontainer drift | Zero drift — devcontainer is a superset of the CI image |
| GHCR dependency | `devcontainer up` requires network access to GHCR; document local fallback (`docker build docker/ci-linux/`) |
| Windows CI | Unchanged — `build-windows` stays on `windows-latest` + `actions/cache` |

### Exit Criteria

- xvfb-run spike PR merged and outcome documented before `ci.yml` is switched
  to `container:` mode (blocking gate — see xvfb-run spike deliverable above).
  Specifically: "A standalone spike PR confirms that `xvfb-run` succeeds for
  `requires-opengl` tests inside the Docker container on a GitHub-hosted runner,
  and the outcome is documented in the PR description."
- `docker-ci-image.yml` builds and pushes successfully; digest is printed
- `build-linux` and `coverage-linux` jobs use the container image and omit
  `apt-get install`, `pip install`, and `lukka/run-vcpkg` steps
- After switching `build-linux` and `coverage-linux` to container mode, `ctest -N`
  output for each label category (unit, integration, requires-opengl) shows
  non-zero test counts, confirming no silent test discovery failures caused by
  the container environment
- `devcontainer up` succeeds using the extended image (no re-bootstrapping vcpkg)
- CI green on `develop` with the new image
- `CLAUDE.md` updated with the five-item atomicity rule

### Team

| Role | Responsibility |
|---|---|
| `cicd-dev-github` | `docker/ci-linux/Dockerfile`, `docker-ci-image.yml`, `ci.yml` updates, digest pinning, supply-chain lint |
| `graphics-dev-irrlicht` | Verify `.devcontainer/Dockerfile` extension builds correctly; no regressions in OpenGL/xvfb test paths |

### Dependencies

- Requires Phase 11 complete (save system and game flow)
- No simulation or rendering deliverables — pure CI/CD infrastructure

### Risks & Spikes

- **RISK**: GHCR package visibility on forks. Public forks can pull from the
  upstream GHCR package with `packages: read` permission; private forks require
  explicit package access grant or building the image locally.
- **SPIKE**: Confirm `container:` jobs on `ubuntu-latest` correctly mount
  `$GITHUB_WORKSPACE` and that `actions/checkout` populates it as expected inside
  the container before committing the full `ci.yml` switch.
