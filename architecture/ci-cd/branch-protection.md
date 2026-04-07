# Branch Protection (repository settings)
<!-- Status: CONFIGURED — branch protection is active on both `main` and `develop` as of Phase 0. -->

- Branch protection on BOTH `main` and `develop`. Both branches require:
  1. **Required status check**: `all-checks-pass` (single gate covering both Linux + Windows + coverage)
  2. **Strict mode**: "Require branches to be up-to-date before merging" must be **enabled** alongside the required status check. Without strict mode, a PR that passed CI before a breaking commit landed on `main` can be merged without re-running CI.
  3. **Minimum 1 approving review**; stale reviews dismissed on new commits
  4. **Require conversation resolution before merging**: All PR inline review conversations (threads) must be marked as resolved before the merge button is enabled. This prevents unacknowledged review feedback from being silently bypassed at merge time. GitHub setting: "Require conversation resolution before merging".
  5. **Do not allow bypass by administrators**: The "Allow specified actors to bypass required pull requests" setting must be **disabled** for both branches. Administrators who bypass branch protection rules create a dual standard that erodes CI discipline — if admins can merge without CI passing, broken code can enter `main` invisibly. All contributors including admins must merge through PRs with passing CI. Emergency hotfixes that genuinely cannot wait for CI must go through an expedited PR (not a direct admin push), and the `workflow_dispatch` trigger on CI enables immediate re-runs without a dummy commit.
- The same configuration applies to both branches — `develop` must be explicitly configured with all the same rules as `main`. Simply enabling protection on `main` alone leaves `develop` unprotected; GitHub does not inherit branch protection rules across branches.

## `all-checks-pass` job — `if: always()` requirement

The `all-checks-pass` job in the CI workflow MUST use `if: always()` in its job condition AND enumerate all upstream jobs in its `needs:` list. Without `if: always()`, GitHub Actions skips the `all-checks-pass` job when any upstream job fails — a skipped job reports a "skipped" status check, not a "failed" one. Branch protection sees "pending" or "not run" rather than "failed", and the PR may remain mergeable despite failing CI. With `if: always()`, `all-checks-pass` always runs and can inspect the result of its dependencies.

The `needs:` list evolves in phases — see the canonical phased forms in `github-actions-workflow.md` (Phase 0 form and Phase 1+ form). The registration procedure below applies identically regardless of which phase form is active.

See `github-actions-workflow.md` for the phased job definitions and the full `all-checks-pass` implementation.

See the canonical `all-checks-pass` definition in `github-actions-workflow.md` — this file documents the registration procedure only, not the job implementation.

This `if: always()` + explicit result check pattern is the only reliable way to ensure `all-checks-pass` reports "failure" (not "skipped") when upstream jobs fail, making the branch protection gate effective.

### Registering `all-checks-pass` as a Required Status Check

GitHub's branch protection UI only lists status check names that have been **reported at least once** for the target branch. A freshly created repository will not show `all-checks-pass` as an available status check for `develop` until CI has run at least one successful workflow on a PR targeting `develop`.

**Required setup procedure for `develop`**:

1. Create a throwaway branch off `develop` (e.g. `ci/register-checks`).
2. Open a PR targeting `develop` and push at least one commit.
3. Wait for all CI jobs to complete — this registers `all-checks-pass`, `build-linux`, `build-windows`, and `coverage-linux` as known check names for `develop`. Note: `validate-assets` does not exist at Phase 0 and will not appear here; it is introduced in Phase 1 as a stub that always exits 0 and wired into `all-checks-pass` at that time (see the Phase 1 section below).
4. Navigate to **Settings → Branches → Branch protection rules** and add or edit the rule for `develop`.
5. Under "Require status checks to pass before merging", search for `all-checks-pass` — it will now appear in the autocomplete list. Select it.
6. Enable "Require branches to be up to date before merging" (strict mode).
7. Close the throwaway PR without merging. The status check registration persists even after the PR is closed.

**Merge queue guidance**: If GitHub's merge queue feature is enabled for `develop` or `main`, `all-checks-pass` must be added to the merge queue's required checks in addition to the branch protection required checks. Merge queues use a separate check configuration. Configure at **Settings → Code and automation → Merge queue → Required status checks for merge queue**.

### Known re-registration events

Branch protection rules that reference `all-checks-pass` by name do not automatically pick up changes to that job's `needs:` list — the job name is stable but its upstream dependency set is not tracked by GitHub's branch protection UI. Any time a new job is added to `all-checks-pass`'s `needs:` list, operators must verify the branch protection configuration is still correct.

**Phase 1 — `validate-assets` addition**: When Phase 1 introduces the `validate-assets` job (as a stub running `tools/validate_assets.py` that always exits 0) and it is added to the `all-checks-pass` `needs:` list, branch protection rules for both `main` and `develop` must be re-confirmed. The `all-checks-pass` check name remains the same, so the UI rule does not need to be re-added, but the following must be verified:

1. The `validate-assets` job, the `prepare` job, and the `supply-chain-lint` job have all run at least once on a PR targeting each protected branch (so their check names are known to GitHub). All three are upstream of `all-checks-pass` and must appear in GitHub's branch protection UI autocomplete before they can be confirmed.
2. `all-checks-pass` now reflects seven upstream jobs (`prepare`, `supply-chain-lint`, `validate-assets`, `build-linux`, `build-windows`, `coverage-linux`, `markdown-lint`) — confirm the branch protection rule still shows `all-checks-pass` as the required check and that it is not stale or misconfigured.
3. If using a merge queue, re-confirm the merge queue required-checks list also still contains `all-checks-pass`.

Note: The `validate-assets` job definition and `all-checks-pass` wiring introduced in Phase 1 remain unchanged in Phase 5 (when the stub script gains 18 real checks: Checks #1–#14 and Checks #16–#19, plus Check #15 as a stub) and Phase 9 (when Check #15 is fully implemented and Check #20 is added). Only the script content changes across those later phases — the CI wiring is stable from Phase 1 onward.

Any future job additions to `needs:` in `all-checks-pass` must follow the same re-confirmation procedure.
