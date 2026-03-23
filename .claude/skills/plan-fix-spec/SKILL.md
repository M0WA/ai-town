---
name: plan-fix-spec
description: Sync the implementation plan with the current specs AND iteratively fix all CRITICAL/HIGH issues in both specs and the plan until a clean pass is achieved — using domain-scoped re-reviews, diff-based prompts, issue deduplication, fix verification, and deferred commits to minimize token usage and latency.
---

# Plan Fix Spec

Sync the implementation plan from the current architecture specs, then delegate all review and
fix cycles to the `fix-implementation` skill.

---

## Configuration

Parse both variables from the user's invocation before doing anything else — they will be
forwarded to `fix-implementation` unchanged.

**Severity filter** (optional, default: `CRITICAL+HIGH`): controls which issue levels are
reported and fixed. If the user specified a list (e.g. `critical`, `critical+high+medium`, `all`),
use those levels. If nothing was specified, default to **CRITICAL and HIGH**.

**Phase scope** (optional, default: all phases): controls which phases are reviewed and fixed.
If the user specified phases (e.g. `phase-3`, `phase-3 phase-5`), use those. Otherwise default
to **all phases**.

---

## Process

### Step 1 — Product Owner syncs the implementation plan

Launch the `prod-owner` agent:

> You are a Senior Product Owner for AI Town, a 3D city simulator built with C++, Irrlicht, and
> OpenAL Soft. Read ALL specification files under `architecture/` and `CLAUDE.md`, then update
> the implementation plan files under `./implementation/` (per-phase files + INDEX.md) so they
> accurately and completely reflect the current specs. Follow the Core Rule: Spec Consistency and
> the File Layout rules defined in your agent instructions. When done, confirm what was written
> and list any spec contradictions you flagged.

Wait for the `prod-owner` agent to finish before continuing.

---

### Step 2 — Run fix-implementation

Use the Skill tool to invoke `fix-implementation`, passing the user's original invocation
arguments verbatim as the `args` parameter so `fix-implementation` receives the same severity
filter and phase scope without re-parsing.

`fix-implementation` handles all remaining work: parallel domain-agent reviews, deduplication,
spec and plan fix tracks, iteration until clean, markdown lint, and the single deferred commit.
