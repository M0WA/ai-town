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
float realDelta = clock.deltaSeconds();   // raw wall-clock frame time
citySimulation.tick(realDelta);           // NOT realDelta * multiplier
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

### Design Rationale

The accumulator lives inside `CitySimulation`, not in the main loop. This keeps the main loop unconditionally simple — it calls `tick()` every frame with the raw delta — and means time-scaling policy is fully encapsulated in one place. If a future speed setting (e.g., 30×) is added, only `CitySimulation` needs updating; the main loop is unchanged.
