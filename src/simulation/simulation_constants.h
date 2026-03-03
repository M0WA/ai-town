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
    static_assert(loan_repayment_ticks > 0, "must be positive");

    // Emergency Municipal Bond repayment period (2 in-game years = 24 budget ticks).
    // Distinct from loan_repayment_ticks (12 ticks for forced loans).
    // Populates LoanTerms::repaymentTicks when issuing an emergency bond.
    // See architecture/game-design/economy-model.md (Emergency Municipal Bond terms).
    static constexpr int bond_repayment_ticks = 24;
    static_assert(bond_repayment_ticks > 0, "must be positive");

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

    // Service building one-time placement costs (architecture/game-design/service-coverage.md)
    // Deducted immediately from treasury at the moment of placement, before any budget tick fires.
    // MUST NOT be hardcoded inline in CitySimulation.cpp or UIManager — all code must reference
    // these constants. Values calibrated to Normal difficulty starting funds ($500,000):
    // placing one of each ($22,000 total = 4.4% of starting capital) is affordable alongside
    // a 20-road-tile opening layout without threatening early treasury.
    static constexpr int service_placement_cost_power_plant   = 10000;
    static_assert(service_placement_cost_power_plant > 0, "must be positive");
    static constexpr int service_placement_cost_water_tower   = 3000;
    static_assert(service_placement_cost_water_tower > 0, "must be positive");
    static constexpr int service_placement_cost_fire_station  = 5000;
    static_assert(service_placement_cost_fire_station > 0, "must be positive");
    static constexpr int service_placement_cost_police_station = 4000;
    static_assert(service_placement_cost_police_station > 0, "must be positive");

    // Service upkeep costs per budget tick (monthly)
    static constexpr int service_upkeep_fire_station_per_tick   = 500;
    static_assert(service_upkeep_fire_station_per_tick > 0, "must be positive");
    static constexpr int service_upkeep_police_station_per_tick = 400;
    static_assert(service_upkeep_police_station_per_tick > 0, "must be positive");
    static constexpr int service_upkeep_power_plant_per_tick    = 1000;
    static_assert(service_upkeep_power_plant_per_tick > 0, "must be positive");
    static constexpr int service_upkeep_water_tower_per_tick    = 300;
    static_assert(service_upkeep_water_tower_per_tick > 0, "must be positive");

    // Emergency Municipal Bond usage limits per difficulty
    static constexpr int bond_max_uses_easy   = 3;
    static_assert(bond_max_uses_easy > 0, "must be positive");
    static constexpr int bond_max_uses_normal = 2;
    static_assert(bond_max_uses_normal > 0, "must be positive");
    static constexpr int bond_max_uses_hard   = 1;
    static_assert(bond_max_uses_hard > 0, "must be positive");

    // Zoning system constants (architecture/game-design/zoning-system.md)
    static constexpr float R_raw_material_rate    = 0.05f;  // Industrial zone raw material production rate
    static constexpr float C_goods_consumption_rate = 0.25f;  // Commercial zone goods consumption rate

    // Road costs (economy-model.md)
    // MUST be 10 — used directly in Economy invariant property-based test in Phase 3.
    // Stubbing to 0 causes all Phase 3 economy property tests that compute expected
    // monthly expenses to compute 0 road maintenance and silently pass against wrong expectations.
    static constexpr int road_maintenance_cost_per_tile = 10;
    static_assert(road_maintenance_cost_per_tile > 0, "must be positive");

    // MUST be 500 — treasury cost per road tile placed, deducted immediately at placement.
    // NOT waived during grace period.
    // Named with _per_tile suffix to parallel road_maintenance_cost_per_tile and distinguish
    // it from earthworks_base_cost_per_tile (same dollar value, different semantic).
    static constexpr int road_placement_cost_per_tile = 500;

    // MUST be 500 — base earthworks cost per tile, multiplied by slope_severity_factor.
    // Full formula (terrain-interaction.md): cost = earthworks_base_cost_per_tile * clamp((slope_deg - 15) / 30, 0, 2)
    // Applied at zone/road placement when slope > 15°; deducted from treasury immediately.
    // Coincidentally equal to road_placement_cost_per_tile but semantically distinct.
    static constexpr int earthworks_base_cost_per_tile = 500;

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

    // Service coverage radii (architecture/game-design/service-coverage.md)
    static constexpr int fire_station_coverage_radius_m = 800;
    static_assert(fire_station_coverage_radius_m > 0, "must be positive");
    static constexpr int police_station_coverage_radius_m = 600;
    static_assert(police_station_coverage_radius_m > 0, "must be positive");
    static constexpr int water_tower_coverage_radius_m = 700;
    static_assert(water_tower_coverage_radius_m > 0, "must be positive");
    static constexpr float service_deficit_radius_halving_threshold = -0.10f;

    // service_degradation_probability_per_tick: probability (0.0..1.0) that a radius-based
    // service building (Fire, Police, Water) enters reduced-coverage state on any given budget
    // tick while budget_surplus_pct <= service_deficit_radius_halving_threshold (-10%).
    // 0.5 = 50% chance per tick per service building at deficit. Evaluated independently per
    // building per tick via ISimulationRNG::nextFloat() < service_degradation_probability_per_tick.
    // See architecture/game-design/service-coverage.md (Budget deficit degradation).
    static constexpr float service_degradation_probability_per_tick = 0.5f;
    static_assert(service_degradation_probability_per_tick > 0.0f &&
                  service_degradation_probability_per_tick <= 1.0f,
                  "must be a valid probability");

    // Demand bootstrapping (architecture/game-design/zoning-system.md)
    // Bootstrap subsidies apply during ticks 0 through demand_bootstrapping_ticks-1 (i.e., ticks 0–5).
    // Correct conditional: if (currentTick < demand_bootstrapping_ticks)
    static constexpr int demand_bootstrapping_ticks = 6;
    static_assert(demand_bootstrapping_ticks > 0, "must be positive");
    static constexpr float demand_floor_residential = 0.20f;
    static constexpr float demand_floor_commercial = 0.10f;
    static constexpr float demand_floor_industrial = 0.10f;

    // Density upgrade wave (architecture/game-design/zoning-system.md)
    static constexpr float density_upgrade_wave_demand_threshold = 0.50f;
    static constexpr float density_max_upgrade_rate_per_tick = 0.20f;

    // Desirability system (architecture/game-design/zoning-system.md)
    // desirability_base_value: starting desirability for a newly zoned tile (neutral mid-point of [0, 100])
    static constexpr int desirability_base_value = 50;
    static_assert(desirability_base_value > 0, "must be positive");
    // adjacency_commercial_residential_bonus: bonus applied to Residential tile when Commercial is at Chebyshev d=1
    static constexpr int adjacency_commercial_residential_bonus = 10;
    static_assert(adjacency_commercial_residential_bonus > 0, "must be positive");
    // adjacency_industrial_residential_base_penalty: penalty when Industrial is at Chebyshev d=1; linear falloff to 0 at d=5
    static constexpr int adjacency_industrial_residential_base_penalty = 20;
    static_assert(adjacency_industrial_residential_base_penalty > 0, "must be positive");
    // service_uncovered_desirability_penalty_per_tick: desirability lost per budget tick for uncovered residential zone
    static constexpr int service_uncovered_desirability_penalty_per_tick = 5;
    static_assert(service_uncovered_desirability_penalty_per_tick > 0, "must be positive");
    // service_recovery_desirability_per_tick: desirability recovered per tick when coverage restored (60% faster than penalty)
    static constexpr int service_recovery_desirability_per_tick = 8;
    static_assert(service_recovery_desirability_per_tick > 0, "must be positive");

    // Starting funds by difficulty (architecture/game-design/game-progression-modes.md)
    // Used in CitySimulation constructor and verified by StartingFunds_Easy/Normal/Hard tests.
    static constexpr int starting_funds_easy   = 1'000'000;
    static constexpr int starting_funds_normal =   500'000;
    static constexpr int starting_funds_hard   =   200'000;
    static_assert(starting_funds_easy   > starting_funds_normal, "easy must have more than normal");
    static_assert(starting_funds_normal > starting_funds_hard,   "normal must have more than hard");

    // Traffic system constants (architecture/game-design/traffic-system.md)
    // null_path_demand_default: demand factor for tiles with no valid A* path to their destination;
    //   neither the 0-demand extreme nor full demand — represents "technically accessible but poorly
    //   connected" state. Value 0.5 is the midpoint of smoothstep range.
    static constexpr float null_path_demand_default = 0.5f;
    static_assert(null_path_demand_default > 0.0f && null_path_demand_default < 1.0f,
                  "null_path_demand_default must be in (0, 1)");

    // traffic_agent_timeout_seconds: A* agents that exceed this travel time are logged as
    //   "extreme travel" (not null-path). 120 simulation seconds at 1x speed.
    static constexpr float traffic_agent_timeout_seconds = 120.0f;
    static_assert(traffic_agent_timeout_seconds > 0.0f, "timeout must be positive");

    // Congestion tax-yield penalty thresholds (as fraction of max road speed):
    //   speed > congestion_none_threshold     → no penalty
    //   speed in (congestion_low_threshold, congestion_none_threshold] → -10% yield
    //   speed in (congestion_high_threshold,  congestion_low_threshold] → -18% yield
    //   speed <= congestion_high_threshold    → -25% yield (max penalty)
    // Closed-interval boundaries: 21-30% inclusive → -18%; 31-40% inclusive → -10%.
    static constexpr float congestion_none_threshold   = 0.40f;  // > 40% speed → no penalty
    static constexpr float congestion_low_threshold    = 0.30f;  // 31-40% → -10%
    static constexpr float congestion_high_threshold   = 0.20f;  // 21-30% → -18%; ≤20% → -25%
    static constexpr float congestion_penalty_low      = 0.10f;  // 10% tax-yield reduction
    static constexpr float congestion_penalty_medium   = 0.18f;  // 18% tax-yield reduction
    static constexpr float congestion_penalty_high     = 0.25f;  // 25% tax-yield reduction (cap)
    // min_speed_fraction: minimum road speed as fraction of max speed (road never fully stops)
    static constexpr float min_speed_fraction = 0.05f;
    static_assert(min_speed_fraction > 0.0f && min_speed_fraction < congestion_high_threshold,
                  "min_speed_fraction must be below high-congestion threshold");

    // Traffic rolling-window sizes (architecture/game-design/traffic-system.md)
    // R and C zones use a 5-tick window; I zones use a 3-tick window.
    // Must NOT be hardcoded as literals in the rolling-window initialization.
    static constexpr int traffic_rolling_window_r_c = 5;
    static_assert(traffic_rolling_window_r_c > 0, "must be positive");
    static constexpr int traffic_rolling_window_i = 3;
    static_assert(traffic_rolling_window_i > 0, "must be positive");

    // Loan cooldown: minimum budget ticks between consecutive forced loans
    // (architecture/game-design/economy-model.md)
    static constexpr int loan_cooldown_ticks = 2;
    static_assert(loan_cooldown_ticks > 0, "must be positive");

    // Road geometry parameters (architecture/game-design/traffic-system.md)
    static constexpr float road_max_speed_mps = 13.9f;  // 50 km/h
    static_assert(road_max_speed_mps > 0.0f, "must be positive");
    static constexpr int road_segment_capacity_per_tile = 8;  // vehicles/tile; clamped to min 1
    static_assert(road_segment_capacity_per_tile > 0, "must be positive");

    // Density unlock base thresholds (treasury balance required; scaled by density_unlock_scale_*)
    // (architecture/game-design/economy-model.md, density unlock table)
    //
    // TIER → THRESHOLD MAPPING (6 tiers, 5 threshold values):
    //   DensityUnlockState tier index 0 (Med-R):   uses density_unlock_base_threshold_1 ($50K)
    //   DensityUnlockState tier index 1 (Med-C):   uses density_unlock_base_threshold_1 ($50K)  ← SAME AS Med-R
    //   DensityUnlockState tier index 2 (Med-I):   uses density_unlock_base_threshold_2 ($75K)
    //   DensityUnlockState tier index 3 (High-R):  uses density_unlock_base_threshold_3 ($100K) ← requires Med-I unlocked first
    //   DensityUnlockState tier index 4 (High-C):  uses density_unlock_base_threshold_4 ($200K)
    //   DensityUnlockState tier index 5 (High-I):  uses density_unlock_base_threshold_5 ($500K)
    //
    // NOTE: Med-R (tier 0) and Med-C (tier 1) share threshold_1 and unlock simultaneously.
    // Do NOT map tier index i to density_unlock_base_threshold_i — that off-by-one error
    // assigns $75K to Med-C (wrong) and leaves High-I with no constant.
    // Correct mapping: use the table above; unlocks[0] and unlocks[1] both check threshold_1.
    static constexpr int density_unlock_base_threshold_1 = 50'000;
    static constexpr int density_unlock_base_threshold_2 = 75'000;
    static constexpr int density_unlock_base_threshold_3 = 100'000;
    static constexpr int density_unlock_base_threshold_4 = 200'000;
    static constexpr int density_unlock_base_threshold_5 = 500'000;
    static_assert(density_unlock_base_threshold_1 < density_unlock_base_threshold_2, "unlock thresholds must be ascending");
    static_assert(density_unlock_base_threshold_2 < density_unlock_base_threshold_3, "unlock thresholds must be ascending");
    static_assert(density_unlock_base_threshold_3 < density_unlock_base_threshold_4, "unlock thresholds must be ascending");
    static_assert(density_unlock_base_threshold_4 < density_unlock_base_threshold_5, "unlock thresholds must be ascending");

    // Population milestone thresholds (architecture/game-design/game-progression-modes.md)
    // Each threshold fires exactly once per playthrough (per-milestone boolean flag in CitySimulation).
    // Must NOT be hardcoded inline in CitySimulation.cpp or test bodies.
    static constexpr int population_milestone_threshold_1 =      1'000;
    static constexpr int population_milestone_threshold_2 =     10'000;
    static constexpr int population_milestone_threshold_3 =     50'000;
    static constexpr int population_milestone_threshold_4 =    100'000;
    static constexpr int population_milestone_threshold_5 =    500'000;
    static_assert(population_milestone_threshold_1 < population_milestone_threshold_2, "milestones must be ascending");
    static_assert(population_milestone_threshold_2 < population_milestone_threshold_3, "milestones must be ascending");
    static_assert(population_milestone_threshold_3 < population_milestone_threshold_4, "milestones must be ascending");
    static_assert(population_milestone_threshold_4 < population_milestone_threshold_5, "milestones must be ascending");
};
