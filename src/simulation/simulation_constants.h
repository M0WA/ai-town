#pragma once

// SimulationConstants — tuning constants for simulation logic.
// Use struct with constexpr members (NOT free k-prefix constants).
// The k-prefix naming style is incompatible with architecture/game-design/economy-model.md
// naming convention and must not be used.
//
// Phase 3 fills in real simulation values for stubs marked "= 0".
// Fields with explicit "MUST be initialized" notes use their specified values.
// Never hardcode any of these values inline in simulation or UI source files.
struct SimulationConstants {
    // Time constants
    // ticks_per_year = 12 — mandated by both economy-model.md and simulation-time.md
    // for the interest formula: interest_per_tick = outstanding_debt * (0.05 / ticks_per_year)
    // MUST NOT be hardcoded inline in CitySimulation.cpp or anywhere else.
    static constexpr int ticks_per_year = 12;

    // Budget tick interval: 1 month = 30 in-game days; 1 in-game day = 1.0 real second at 1x speed.
    // Therefore one budget tick fires every 30.0 simulation seconds.
    // Referenced in CitySimulation::tick() accumulator loop — MUST NOT be hardcoded inline.
    // See architecture/game-design/simulation-time.md (Budget Tick Threshold Constant section).
    static constexpr float SECONDS_PER_BUDGET_TICK = 30.0f;

    // Forced loan repayment period (1 in-game year). Populates LoanTerms::repaymentTicks.
    // Emergency bonds use 24 ticks (2 in-game years) — a different mechanism.
    static constexpr int loan_repayment_ticks = 12;

    // Emergency Municipal Bond repayment period (2 in-game years = 24 budget ticks).
    // Distinct from loan_repayment_ticks (12 ticks for forced loans).
    // Populates LoanTerms::repaymentTicks when issuing an emergency bond.
    // See architecture/game-design/economy-model.md (Emergency Municipal Bond terms).
    static constexpr int bond_repayment_ticks = 24;

    // Grace period: 120 real seconds (wall-clock time).
    // Used for BOTH the grace period cost waiver AND the forced loan real-time gate.
    // Both activate at 120 real seconds per architecture/game-design/economy-model.md.
    // Phase 3 CitySimulation must reference this constant at BOTH call sites.
    // MUST be 120.0 — do NOT stub to 0.
    static constexpr double grace_period_real_seconds = 120.0;

    // base_day_duration — real seconds per in-game day at 1x speed.
    // All three tiers are 1.0 in V1.
    // MUST be 1.0 — do NOT stub to 0 (zero-length intervals break budget tick interval derivation).
    static constexpr double base_day_duration_easy   = 1.0;
    static constexpr double base_day_duration_normal = 1.0;
    static constexpr double base_day_duration_hard   = 1.0;

    // Economy constants
    static constexpr float wage_fraction_of_revenue = 0.20f;

    // Service upkeep costs per budget tick (monthly)
    static constexpr int service_upkeep_fire_station_per_tick   = 500;
    static constexpr int service_upkeep_police_station_per_tick = 400;
    static constexpr int service_upkeep_power_plant_per_tick    = 1000;
    static constexpr int service_upkeep_water_tower_per_tick    = 300;

    // Emergency Municipal Bond usage limits per difficulty
    static constexpr int bond_max_uses_easy   = 3;
    static constexpr int bond_max_uses_normal = 2;
    static constexpr int bond_max_uses_hard   = 1;

    // Zoning system constants (architecture/game-design/zoning-system.md)
    static constexpr float R_raw_material_rate    = 0.05f;  // Industrial zone raw material production rate
    static constexpr float C_goods_consumption_rate = 0.25f;  // Commercial zone goods consumption rate

    // Road costs (economy-model.md)
    // MUST be 10 — used directly in Economy invariant property-based test in Phase 3.
    // Stubbing to 0 causes all Phase 3 economy property tests that compute expected
    // monthly expenses to compute 0 road maintenance and silently pass against wrong expectations.
    static constexpr int road_maintenance_cost_per_tile = 10;

    // MUST be 500 — treasury cost per road tile placed, deducted immediately at placement.
    // NOT waived during grace period.
    static constexpr int road_placement_cost = 500;

    // Income per resident by density tier (economy-model.md)
    // MUST be initialized to specified values — used directly in Phase 3 revenue calculation tests.
    static constexpr int base_income_per_resident_low    = 50;
    static constexpr int base_income_per_resident_medium = 50;
    static constexpr int base_income_per_resident_high   = 55;  // premium density revenue

    // Utility fees charged per connected residential tile per month
    static constexpr int utility_fee_power_per_tile = 5;  // MUST be 5
    static constexpr int utility_fee_water_per_tile = 3;  // MUST be 3

    // Per-difficulty density unlock scale
    // Three separate fields required — a single field cannot hold three values and
    // would force hardcoded per-difficulty branching at every call site.
    static constexpr float density_unlock_scale_easy   = 0.70f;
    static constexpr float density_unlock_scale_normal = 1.00f;
    static constexpr float density_unlock_scale_hard   = 1.50f;

    // Sentinel returned by ICitySimulation::getNextUnlockThreshold() when all six density tiers
    // are unlocked and no further unlock is pending. Negative so it is unambiguously out-of-range
    // for any valid dollar threshold (all valid thresholds are positive).
    // HUD check: if (threshold < 0.0f) → hide density unlock progress indicator.
    // MUST be used at every call site that checks for the sentinel — never compare against -1.0f
    // inline. See architecture/game-design/economy-model.md (getNextUnlockThreshold() section).
    static constexpr float kNoUnlockThreshold = -1.0f;

    // Population growth and decay caps — expressed as fractions of max_density_for_tier.
    // Applied per budget tick; rounding via static_cast<int>(std::round(max_density * fraction)).
    // See architecture/game-design/population-density-growth.md (SimulationConstants Mapping).
    static constexpr float population_growth_cap_fraction = 0.10f;  // max +10% of tier capacity per tick
    static constexpr float population_decay_cap_fraction  = 0.15f;  // max -15% of tier capacity per tick (asymmetric: falls faster than rises)
};
