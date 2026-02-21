# Simulation Time System

- **Base tick rate**: 1 in-game day = 1 real second at 1× speed
- **Month definition**: 1 in-game month = 30 in-game days = 30 real seconds at 1× speed
- **Economy budget tick**: Taxes collected and expenses processed at the end of each in-game month (every 30 real seconds at 1× speed)
- **Base day duration**: V1 uses exactly **1.0 real second per in-game day on all difficulty tiers**. Named constants: `SimulationConstants::base_day_duration_easy = 1.0` s, `SimulationConstants::base_day_duration_normal = 1.0` s, `SimulationConstants::base_day_duration_hard = 1.0` s. Configurable per-difficulty day duration (the previously noted 0.5–3 s tuning range) is **post-V1 scope** — it is not implemented in V1. All timing calculations (month length, interest accrual intervals, grace period real-time gates) assume exactly 1.0 s/day at 1× speed. If per-difficulty day duration is introduced post-V1, all downstream timing constants must be re-evaluated.
- **Speed multipliers**: 1× / 3× / 10× simulation speed; pause
- **Default starting speed**: **3×** (not 1× — too slow for feedback, not 10× — too punishing for new players). Speed selector is always user-adjustable.
- **SpeedMultiplier enum definition**:

  ```cpp
  enum class SpeedMultiplier {
      Paused = 0,  // simulation frozen; real-time multiplier = 0
      x1     = 1,  // 1× speed  (1 in-game day per real second)
      x3     = 2,  // 3× speed  (3 in-game days per real second)
      x10    = 3,  // 10× speed (10 in-game days per real second)
  };
  ```

  The integer values (0–3) are the storage representation only and must not be used directly in logic — always reference enum names. The default starting speed maps to `SpeedMultiplier::x3`. The auto-slow-to-1× mechanic (e.g., game-over deficit warning on month 1 of streak) calls `setSpeed(SpeedMultiplier::x1)`. The paused state is `SpeedMultiplier::Paused`.
- **`ticks_per_year = 12`** — this is a named constant used in the interest formula and in tests. Derived from 12 budget ticks (months) per in-game year at 30 days/month. Do not compute it inline; reference this constant everywhere.
- **Simulation second**: 1 simulation second = 1 real second at 1× speed. The traffic subsystem uses simulation seconds as its time unit for travel time calculations (path travel time = `path_length_meters / segment_speed_m_per_s`). At the default road max speed of 13.9 m/s, a 350 m commute takes approximately 25 simulation seconds; an 835 m commute takes approximately 60 simulation seconds — these are the T=25 and T=60 demand coupling thresholds. Traffic simulation is independent of the day/tick rate: agents run at simulation-second granularity regardless of speed setting. The traffic demand coupling thresholds (T=25, T=60 for Residential; T=30, T=65 for Commercial; T=40, T=80 for Industrial) are in **simulation seconds**, not in-game minutes — the label "in-game minutes" that may appear in earlier draft text was a documentation error and refers to simulation seconds.

## Main Loop Integration

This section defines the calling convention between the main loop and `CitySimulation::tick()` so that Phase 6 implementers have an unambiguous contract.

### Calling Convention

The main loop calls `CitySimulation::tick(float realDeltaSeconds)` **every frame**, passing the raw real-world frame delta — the value is **not** pre-multiplied by the speed multiplier. `CitySimulation` owns all time-scaling logic internally.

```cpp
// Main loop — pseudocode (called every rendered frame)
// IClock exposes nowSeconds() only — the main loop owns the previousTime variable and
// computes realDelta each frame. IClock has no deltaSeconds() method.
double now = clock.nowSeconds();
float realDelta = static_cast<float>(now - m_previousTime);  // raw wall-clock frame time
m_previousTime = now;
citySimulation.tick(realDelta);           // NOT realDelta * multiplier
// Note: m_previousTime is initialised to clock.nowSeconds() before the loop begins.
```

### Internal Accumulator

`CitySimulation` maintains a private accumulator member:

```cpp
float m_accumulatedSimSeconds = 0.0f;
```

Each call to `tick()` advances the accumulator by the speed-scaled delta:

```cpp
void CitySimulation::tick(float realDeltaSeconds) {
    // SpeedMultiplier::Paused has integer value 0, so multiplication zeroes out naturally,
    // but an explicit guard is preferred for clarity.
    if (m_currentSpeed == SpeedMultiplier::Paused) return;

    float multiplier = speedMultiplierValue(m_currentSpeed); // 1.0f, 3.0f, or 10.0f
    m_accumulatedSimSeconds += realDeltaSeconds * multiplier;

    while (m_accumulatedSimSeconds >= SimulationConstants::SECONDS_PER_BUDGET_TICK) {
        m_accumulatedSimSeconds -= SimulationConstants::SECONDS_PER_BUDGET_TICK;
        fireBudgetTick();
    }
}
```

### Budget Tick Threshold Constant

```cpp
namespace SimulationConstants {
    // 1 month = 30 in-game days; 1 in-game day = 1.0 real second at 1× speed.
    // Therefore one budget tick fires every 30.0 simulation seconds.
    constexpr float SECONDS_PER_BUDGET_TICK = 30.0f;
}
```

This value is derived directly from the base tick rate and month definition above. If either is changed post-V1, `SECONDS_PER_BUDGET_TICK` must be updated to match.

### Pause Behaviour

While paused (`m_currentSpeed == SpeedMultiplier::Paused`), `tick()` returns immediately. No accumulation occurs and no budget ticks fire. The accumulator value is preserved across pause/unpause transitions so that a partially accumulated month is not lost.

### Grace Period Interaction

The 120-real-second grace period defined in `economy-model.md` gates cost deductions (road maintenance, service upkeep) **inside** `fireBudgetTick()` — it does NOT gate the accumulation of `m_accumulatedSimSeconds` or the firing of `fireBudgetTick()` itself.

- Budget ticks fire normally from tick 0.
- Tax revenue is collected from tick 0.
- Only the expense line items (road maintenance, service upkeep) are suppressed until the `IClock`-based gate clears at 120 real seconds.

This distinction is required for the bootstrap demand mechanism (ticks 0–5) documented in `zoning-system.md` to function correctly: if tick firing were gated by the grace period, the bootstrap demand decay would not advance and the city would fail to generate initial zone demand.

### Frame-Loop Position Constraint

`CitySimulation::tick()` (the game tick / simulation advance) **must occupy step 2 in the canonical 8-step per-frame loop**, as shown below. It must run BEFORE `CameraController::update()`, `UIManager::update()`, `syncListenerToCamera()`, `AudioSystem::update()`, and `beginScene()`. It must NOT run after `beginScene()` has been called.

```text
1. Poll events
2. CitySimulation::tick(realDelta)    ← game tick MUST be here
3. CameraController::update()
3b. UIManager::update()
4a. syncListenerToCamera(cameraState)
4b. AudioSystem::update(realDeltaSeconds)
5. beginScene()
6. drawScene()  (includes UIManager::draw())
7. endFrame()
```

Any implementation that advances the simulation tick after `beginScene()` has been called violates this ordering constraint. The city simulation must not advance while rendering for the frame is already in progress, as this would cause the rendered frame to reflect a simulation state that is inconsistent with the state that was current when camera, UI, and audio were updated.

### Design Rationale

The accumulator lives inside `CitySimulation`, not in the main loop. This keeps the main loop unconditionally simple — it calls `tick()` every frame with the raw delta — and means time-scaling policy is fully encapsulated in one place. If a future speed setting (e.g., 30×) is added, only `CitySimulation` needs updating; the main loop is unchanged.

---

## Phase 1 gamedesign-lookandfeel sign-off

**Date**: 2026-02-21
**Agent**: gamedesign-lookandfeel

### DEFAULT SPEED CONTRACT stub comment verified

Code inspection of `src/main.cpp` at the step 2 call site (lines 135-144) confirms the DEFAULT SPEED CONTRACT block is present verbatim:

```cpp
// DEFAULT SPEED CONTRACT: CitySimulation must be constructed or initialized at
// SpeedMultiplier::x3 (not x1 or Paused) — see architecture/game-design/simulation-time.md.
// Phase 6 MUST verify setSpeed(SpeedMultiplier::x3) or equivalent initialization is
// called; initializing at x1 silently breaks the default starting speed contract.
```

The stub call `// TODO Phase 6: citySimulation.tick(realDeltaSeconds);` appears at line 144, which is step 2 in the 8-step frame loop. The `cameraController.update(realDeltaSeconds);` call appears at line 149, which is step 3. The Frame-Loop Position Constraint is satisfied: the tick stub is at step 2, before step 3 (CameraController::update), before any beginScene() call.

The default starting speed of 3x (SpeedMultiplier::x3) is correctly preserved by this stub for Phase 6 implementation. Phase 6 implementers are put on notice via this comment that CitySimulation must NOT initialize at x1 or Paused.

### simulation_constants.h Part A values verified

Code inspection of `src/simulation/simulation_constants.h` confirms all Part A constants are set to their spec-default values:

| Constant | Required value | Actual value | Status |
|---|---|---|---|
| `grace_period_real_seconds` | 120.0 | 120.0 | PASS |
| `road_maintenance_cost_per_tile` | 10 | 10 | PASS |
| `road_placement_cost` | 500 | 500 | PASS |
| `base_day_duration_easy` | 1.0 | 1.0 | PASS |
| `base_day_duration_normal` | 1.0 | 1.0 | PASS |
| `base_day_duration_hard` | 1.0 | 1.0 | PASS |
| `base_income_per_resident_low` | 50 | 50 | PASS |
| `base_income_per_resident_medium` | 50 | 50 | PASS |
| `base_income_per_resident_high` | 55 | 55 | PASS |
| `utility_fee_power_per_tile` | 5 | 5 | PASS |
| `utility_fee_water_per_tile` | 3 | 3 | PASS |

All Part A constants are correctly initialized. No stubs remain at zero for these values. The Phase 3 economy and property-based tests that depend on these exact values will compute correct expected outcomes.
