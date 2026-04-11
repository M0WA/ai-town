# Code Quality Analysis — Cognitive Complexity

## Tool

`tools/cognitive_complexity.py` — local static analysis tool that measures cognitive
complexity for every C++ function in a file or directory tree.

**Dependency**: `pip install tree-sitter tree-sitter-cpp` (pure Python, no LLVM needed).

```bash
# Show all violations sorted worst-first
python3 tools/cognitive_complexity.py --only-violations --sort score src/

# JSON output (for CI integration or diffing)
python3 tools/cognitive_complexity.py --format json src/ > complexity.json

# Grep-friendly: file:line:score:STATUS:name
python3 tools/cognitive_complexity.py --format grep src/ | grep CRITICAL

# Single file
python3 tools/cognitive_complexity.py src/ui/UIManager.cpp
```

Exit code 0 = no CRITICAL violations; 1 = one or more CRITICAL violations.

## Scoring Rules (SonarSource Specification — cpp:S3776)

| Category | Constructs | Score |
|---|---|---|
| Structural | `if`, `else if`, `else`, `switch`, `for`, range-`for`, `while`, `do-while`, `catch`, ternary `?:` | +1 + current nesting depth |
| Flat | Each unbroken run of `&&` or `\|\|` operators, `goto` | +1 (no nesting bonus) |
| Nesting-only | Lambda expressions | Increase depth for body; no own score |

**Nesting depth** starts at 0 inside a function body and increments inside every
structural construct's body. `else if` and `else` do not add a nesting bonus —
they are flat +1.

## Thresholds

| Status | Score | Action |
|---|---|---|
| OK | < 16 | No action required |
| WARN | 16–25 | Refactor when convenient |
| CRITICAL | ≥ 26 | Must refactor — violates the project limit of 25 |

The project hard limit is **25** (CLAUDE.md: *"No function may exceed a Cognitive
Complexity score of 25"*). The tool defaults to CRITICAL ≥ 26 so the boundary
matches: a function scoring exactly 25 is WARN, not CRITICAL.

## SonarCloud Comparison

The project uses SonarCloud with rule **cpp:S3776** (threshold 25), which implements
the same SonarSource cognitive complexity specification.

**Accuracy**: on benchmark functions where SonarCloud and the local tool can be
directly compared, scores agree within ≈2–3%:

| Function | SonarCloud | Local tool |
|---|---|---|
| `benchmark_main.cpp:182 main` | 257 | 261 |
| `model_validator_main.cpp:323 main` | 324 | 333 |
| `model_validator_main.cpp:61 parseArgs` | 37 | 37 |
| `model_validator_main.cpp:227 OnEvent` | 35 | 35 |

Minor divergence is expected from small differences in how each tool handles
edge constructs (chained binary expressions inside complex template arguments, etc.).

**Baseline as of 2026-04-11** (develop branch):

- Functions analyzed: **862**
- OK (< 16): **785**
- WARN (16–25): **46**
- CRITICAL (≥ 26): **31**

SonarCloud shows **0 open** S3776 issues (threshold 25) in production code.
The 55 CLOSED issues represent functions that were previously over-threshold
and have since been refactored. The 4 RESOLVED/FALSE-POSITIVE issues are in
`src/benchmark/` (tool code, not production logic).

The discrepancy — local tool finds 31 CRITICAL, SonarCloud finds 0 open — is due
to SonarCloud's **new code** period: issues in code that pre-dates the tracking
baseline are not re-opened as new violations even if the function still exceeds the
threshold. The local tool has no such exclusion and reports every function above the
threshold regardless of age.

**Practical implication**: the local tool is the authoritative source for identifying
all functions that need refactoring. SonarCloud tracks regressions in new/modified
code; the local tool tracks the full backlog.

## Top Violations (2026-04-11)

The worst offenders are large event-dispatch `onEvent()` methods and the audio
streaming core. These are the highest-priority refactoring candidates:

| File | Function | Score |
|---|---|---|
| `src/ui/UIManager.cpp:258` | `UIManager::onEvent` | 728 |
| `src/ui/MainMenuPanel.cpp:289` | `MainMenuPanel::onEvent` | 132 |
| `src/ui/SettingsPanel.cpp:418` | `SettingsPanel::onEvent` | 112 |
| `src/ui/KeyBindingsPanel.cpp:255` | `KeyBindingsPanel::onEvent` | 90 |
| `src/ui/ModalDialog.cpp:707` | `ModalDialog::onEvent` | 87 |
| `src/ui/UIManager.cpp:1579` | `UIManager::updateHUDState` | 66 |
| `src/ui/Minimap.cpp:163` | `Minimap::drawOverlay` | 65 |
| `src/rendering/IrrlichtUIBackend.cpp:84` | `IrrlichtUIBackend::IrrlichtUIBackend` | 54 |
| `src/audio/AudioSystem.cpp:626` | `AudioSystem::~AudioSystem` | 52 |
| `src/ui/NotificationManager.cpp:248` | `NotificationManager::onEvent` | 51 |

The benchmark tool files (`src/benchmark/`) are intentionally excluded from the
hard limit — they are developer tools, not production simulation code, and their
complexity has been marked FALSE-POSITIVE in SonarCloud.

## Refactoring Patterns

For large `onEvent()` dispatchers: decompose by event type into private handler
methods (`handleKeyEvent`, `handleMouseEvent`, etc.) and route from `onEvent` with
a top-level `switch`/`if` chain that immediately delegates. Each handler should
score < 16.

For complex `update()` / `draw()` functions: extract named helper methods for each
conceptual phase (e.g., `updateBudgetRow`, `drawLegendItem`).

For audio stream management (`refillStream`, `updateStreams`): extract state-machine
transitions into named step methods keyed on stream state enum values.
