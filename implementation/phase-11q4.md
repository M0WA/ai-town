## Phase 11q4: Fix SonarCloud BLOCKER Security Issues (S7630 Script Injection)

**Status: Planned**

**Prerequisite**: none. Fully independent.

### Goal

Three BLOCKER-severity `githubactions:S7630` issues exist in the packaging workflows.
Each one interpolates a `workflow_call` input directly into a `run:` block shell
command — a script-injection vector where a caller-controlled string could inject
arbitrary shell commands. The fix is to assign each input to an environment variable
at the step level and reference the env var instead.

All three issues have status **REOPENED**, meaning they were previously suppressed or
resolved but have resurfaced.

---

### Issues to Fix

| File | Line | Input |
|---|---|---|
| `.github/workflows/_package-linux-deb.yml` | 82 | `inputs.vcpkg_commit_id` |
| `.github/workflows/_package-linux-deb.yml` | 109 | `inputs.pkg_version` |
| `.github/workflows/_package-windows.yml` | 48 | `inputs.pkg_version` |

---

### Deliverables

#### 1. Fix `_package-linux-deb.yml` — `vcpkg_commit_id` injection (line 82)

The "Set up vcpkg" step uses `${{ inputs.vcpkg_commit_id }}` directly in the `run`
block shell command.

Open `.github/workflows/_package-linux-deb.yml` and locate the "Set up vcpkg" step.
Add a step-level `env:` block that maps the input to `VCPKG_COMMIT_ID`, then replace
the inline expression with the env var reference:

**Before:**

```yaml
      - name: Set up vcpkg
        run: |
          ...
          git -C /opt/vcpkg fetch --depth=1 origin ${{ inputs.vcpkg_commit_id }}
```

**After:**

```yaml
      - name: Set up vcpkg
        env:
          VCPKG_COMMIT_ID: ${{ inputs.vcpkg_commit_id }}
        run: |
          ...
          git -C /opt/vcpkg fetch --depth=1 origin "$VCPKG_COMMIT_ID"
```

- [ ] `env:` block added to the "Set up vcpkg" step with `VCPKG_COMMIT_ID: ${{ inputs.vcpkg_commit_id }}`.
- [ ] Shell command uses `"$VCPKG_COMMIT_ID"` (double-quoted, no `${{ }}` expression).
- [ ] No other `${{ inputs.vcpkg_commit_id }}` expressions remain in `run:` blocks in this file.

#### 2. Fix `_package-linux-deb.yml` — `pkg_version` injection (line 109)

The "Resolve package version" step writes `${{ inputs.pkg_version }}` directly into
the shell command that sets `$GITHUB_OUTPUT`.

Locate the "Resolve package version" step and apply the same env-var pattern:

**Before:**

```yaml
      - name: Resolve package version
        id: pkgver
        run: echo "version=${{ inputs.pkg_version }}" >> "$GITHUB_OUTPUT"
```

**After:**

```yaml
      - name: Resolve package version
        id: pkgver
        env:
          PKG_VERSION: ${{ inputs.pkg_version }}
        run: echo "version=$PKG_VERSION" >> "$GITHUB_OUTPUT"
```

- [ ] `env:` block added to the "Resolve package version" step with `PKG_VERSION: ${{ inputs.pkg_version }}`.
- [ ] Shell command uses `$PKG_VERSION` (no `${{ }}` expression).
- [ ] No other `${{ inputs.pkg_version }}` expressions remain in `run:` blocks in this file.

#### 3. Fix `_package-windows.yml` — `pkg_version` injection (line 48)

The "Resolve package version" step uses `${{ inputs.pkg_version }}` in a PowerShell
`run:` block.

Open `.github/workflows/_package-windows.yml`, locate the "Resolve package version"
step, and apply the env-var pattern for PowerShell:

**Before:**

```yaml
      - name: Resolve package version
        id: pkgver
        shell: pwsh
        run: echo "version=${{ inputs.pkg_version }}" >> $env:GITHUB_OUTPUT
```

**After:**

```yaml
      - name: Resolve package version
        id: pkgver
        shell: pwsh
        env:
          PKG_VERSION: ${{ inputs.pkg_version }}
        run: echo "version=$env:PKG_VERSION" >> $env:GITHUB_OUTPUT
```

- [ ] `env:` block added to the "Resolve package version" step with `PKG_VERSION: ${{ inputs.pkg_version }}`.
- [ ] PowerShell command uses `$env:PKG_VERSION` (no `${{ }}` expression).
- [ ] No other `${{ inputs.pkg_version }}` expressions remain in `run:` blocks in this file.

---

### Exit Criteria

- [ ] `npx markdownlint-cli 'implementation/phase-11q4.md'` — no errors.
- [ ] All deliverable checkboxes above are checked.
- [ ] SonarCloud re-scan shows all three `githubactions:S7630` issues on
  `_package-linux-deb.yml` (lines 82 and 109) and `_package-windows.yml` (line 48)
  resolved.
- [ ] No new `${{ inputs.* }}` expressions appear in `run:` blocks in either file.
