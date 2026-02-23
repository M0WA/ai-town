# Zoning System

- **Zone types**: Residential (R), Commercial (C), Industrial (I) — each with Low / Medium / High density tiers
- **Desirability base value**: Base desirability for a newly zoned tile = **50** (neutral). Adjacency bonuses and penalties modify this value. Desirability is always clamped to [0, 100].
- **Adjacency rules**:
  - Industrial adjacent to Residential: −20 desirability points at distance d=1 tile; linear falloff to 0 penalty at distance d=5 tiles; **no penalty beyond 5 tiles**. Formula: `penalty(d) = max(0, −20 × (1 − (d−1)/4))` for d ∈ [1, 5]. **Distance metric: Chebyshev distance** (d = `max(|dx|, |dy|)`) — diagonal and cardinal neighbors are both at d=1, preventing fractional distance values and ensuring consistent penalty values for all neighbor positions.

  - **Commercial adjacent to Residential**: +10 desirability points when a Commercial tile is at Chebyshev distance d=1 (immediately adjacent). No falloff — Commercial adjacency bonus applies only at d=1 (direct neighbors, cardinal and diagonal). Rationale: commercial activity (shops, services) within walking distance is a positive amenity for residential desirability. This adjacency bonus is intentionally small (+10) relative to the industrial penalty (−20) to avoid a dominant strategy of pure C/R interlacing — players must still zone Industrial to drive the C→I supply chain.

  - Parks / greenspace adjacent to Residential: +15 desirability points per adjacent park tile; linear falloff over 2 tiles (post-V1 scope — park desirability bonus not implemented in V1; this adjacency rule is reserved for future expansion)
- **Demand curves**:
  - **Residential (R)**: Driven by job availability — `R_demand = clamp(total_C_I_worker_capacity / max(1, current_R_population), 0.0, 1.0)` where `total_C_I_worker_capacity` is the total number of C+I worker slots across all Commercial and Industrial tiles (regardless of occupancy fill level) and `current_R_population` is the number of residents currently occupying Residential tiles. **Semantics**: R_demand models "employment rate" — are current residents able to find work? When C/I capacity ≥ current R population, every resident can be employed, so demand approaches 1.0. A city with no C/I zones has R_demand = 0 (no jobs → no residential incentive). The `max(1, ...)` prevents division by zero at game start (0 residents). **Important**: at game start before the bootstrap demand kicks in, current_R_population = 0 → max(1,0) = 1 → R_demand = C/I capacity. The 5-tick rolling average travel time further modulates this value (see [Traffic System](traffic-system.md)). **Why current_R_population, not max_R_population**: Using `max_R_population` (total capacity) creates a permanent demand ceiling after density unlocks — a player who zones 50 High-R tiles (50,000 capacity) before unlocking High-C would see R_demand = C/I_capacity / 50,000, permanently capped at 40% even if every current resident has a job. Using `current_R_population` correctly models the actual city state: demand reflects whether today's residents are employed, not whether hypothetical future residents would be employed. **Do NOT use an unfilled-slots formula** such as `(C_worker_capacity + I_worker_capacity − current_workers) / total_R_capacity` — that formula collapses to R_demand = 0 at full employment, eliminating residential demand precisely when the city has a healthy job market. **Canonical authority**: this formula is the single source of truth; [Population Density & Growth](population-density-growth.md) references this spec.
  - **Commercial (C)**: Driven by residential population — `C_demand = clamp(current_R_population / total_C_worker_capacity, 0.0, 1.0)` where `total_C_worker_capacity` is the total number of worker slots across all Commercial zone tiles at their current density tier. Higher residential population creates more consumer demand for commercial services.
  - **Industrial (I)**: Driven by both Residential raw-material demand and Commercial goods consumption — `I_demand = clamp((R_raw_material_demand + C_goods_demand) / I_production_capacity, 0.0, 1.0)` where:
    - `R_raw_material_demand = current_R_population × SimulationConstants::R_raw_material_rate` (`R_raw_material_rate = 0.05` — each resident contributes 0.05 units of industrial demand through direct construction material consumption, independent of Commercial zones)
    - `C_goods_demand = current_C_population × SimulationConstants::C_goods_consumption_rate` (`C_goods_consumption_rate = 0.25` — each occupied Commercial unit consumes 0.25 units of Industrial output per budget tick)
    - If there is no Industrial zone (`I_production_capacity = 0`), Industrial demand defaults to 1.0 (maximum — all available industrial space will be absorbed).
    - **Design rationale**: Industrial demand has TWO independent sources — a small Residential raw-material component (construction, utilities) and a larger Commercial goods component. This ensures Industrial zones are viable from the start of a game even before any Commercial zones are placed, preventing a zero-demand deadlock where Industrial can never grow because Commercial hasn't been built yet. The Residential component (`R_raw_material_rate = 0.05`) is intentionally small so that Commercial activity remains the primary industrial driver — the distinction incentivises players to build a balanced R→C→I supply chain. Both constants must be defined in `SimulationConstants`.
- **Residential demand floor**: After the bootstrap period ends (tick 6+), when a road network exists (at least one zone tile has a valid A* path to at least one other zone tile), Residential demand never falls below **0.20 (20%)**. This floor prevents complete demand collapse from isolated congestion events and ensures the city is always gradually growable. The floor does NOT apply when the road graph is entirely disconnected (zero valid paths for all tiles) — in that case demand may fall to the null-path default (0.5 if all ticks are null-path, or 0.0 for mixed windows). The floor is applied as a final clamp on `effective_demand_factor(R)` after bootstrap and formula values are combined: `effective_demand_factor(R) = max(road_exists ? 0.20 : 0.0, combined_demand_factor)`. **Demand floor and zombie population**: The 0.20 demand floor applies only to **new zone growth** (occupancy increase on unoccupied tiles). It does NOT prevent population from falling on already-occupied tiles due to depopulation/emigration from high costs or poor service. This distinction prevents "zombie" population tiles where buildings show declining occupancy from poor conditions but the demand floor keeps sending new residents in, masking the underlying problem. In production: the demand floor gates the `occupancy_increase_this_tick` computation; emigration/depopulation runs independently as a separate negative occupancy adjustment that the floor does not block. **Interaction with null-path default**: The null-path default of 0.5 (demand_factor = 0.5 when all rolling-window ticks are null-path) is applied BEFORE the demand floor check. Because 0.5 > 0.20 (R floor), the floor is effectively inert in the all-null-path case — the null-path default already exceeds the floor value. The demand floor is active only in the intermediate case where some (but not all) rolling-window ticks have a valid path and the weighted average drops below the floor. "Road network exists" is defined as: at least one zone tile in the simulation has a valid A\* path to at least one other zone tile at the start of the current budget tick.
- **Commercial and Industrial demand floors** (post-bootstrap): After tick 6+, when a road network exists, Commercial and Industrial demand also have minimum floors to prevent complete post-bootstrap deadlock. Floors: C demand ≥ **0.10 (10%)**, I demand ≥ **0.10 (10%)**. Rationale: without these floors, a city that had bootstrap-era C/I growth (from the initial C_demand=25%, I_demand=15% bootstrap) can reach tick 6 with non-zero C/I population but zero formula-driven demand (e.g., if R_population is still very low, C_demand formula = R_population / C_worker_capacity ≈ 0). The 10% floor ensures the city is always marginally growable across all three zone types post-bootstrap. These floors follow the same zombie-population rule as the R floor: they gate occupancy_increase_this_tick only, and do NOT prevent emigration/depopulation from bad conditions. Applied as: `effective_demand_factor(C) = max(road_exists ? 0.10 : 0.0, combined_C_demand)`, `effective_demand_factor(I) = max(road_exists ? 0.10 : 0.0, combined_I_demand)`.
- **Demand floor scope clarification — no zombie populations**: The demand floor (R ≥ 20%, C ≥ 10%, I ≥ 10%) gates ONLY `occupancy_increase_this_tick` — the amount of new growth to add per tick. It does NOT replace the unclamped `combined_demand_factor` when computing the population decay target. The unclamped `demand_factor` is used in the growth formula `actual_population = max_density_for_tier × demand_factor × (desirability / 100)` for emigration/decay calculations. A tile with a low `demand_factor` but non-zero desirability will still decline in population over time even while the demand floor prevents further growth, because the decay target (formula output) is below the current population. This is intentional: the floor prevents demand starvation from blocking new city growth in early game without creating zombie populations — tiles where occupancy stops declining despite poor underlying conditions. Concretely: if `combined_demand_factor = 0.05` for Residential and `desirability = 60`, the decay target is `max_density × 0.05 × 0.60`, which is far below current occupancy, so emigration proceeds normally. The floored `effective_demand_factor = 0.20` is substituted only into the `occupancy_increase_this_tick` path, not into the decay target computation. Both paths must read from separate variables — implementations that pass `effective_demand_factor` (post-floor) into the decay formula produce incorrect zombie-population behavior and violate this spec.
- **Demand bootstrapping** (prevents circular deadlock at game start when all demand signals are zero): At simulation start, apply fixed starter demand values that decay over the first 6 budget ticks toward the formula-driven values: R demand = 50% (representing in-migrants attracted to a new city), C demand = 25%, I demand = 15% (representing base raw-material demand from the new city's construction activity). After 6 ticks, all demand is purely formula-driven. This prevents the three-way circular deadlock where each zone type requires another to provide demand: Industrial growth begins immediately (I_demand defaults to 1.0 when no Industrial zone exists), and Residential growth begins as soon as at least one Commercial or Industrial zone is placed — without requiring all three zone types to be present simultaneously. **Industrial bootstrap decay curve**: Industrial bootstrap demand decays linearly from 15% at tick 0 to 0% at tick 6, reaching 0% by the end of the bootstrap period. Formula: `I_bootstrap(tick) = 0.15 × max(0, 1 − tick/6)` for tick ∈ [0, 6]. Residential and Commercial bootstrap values decay by an equal fraction per tick: `R_bootstrap(tick) = 0.50 × max(0, 1 − tick/6)`, `C_bootstrap(tick) = 0.25 × max(0, 1 − tick/6)`. **Bootstrap + formula combination rule**: During ticks 0–5, the effective demand factor used in the population growth formula is: `effective_demand_factor(tick) = clamp(bootstrap(tick) + formula_demand_factor(tick), 0.0, 1.0)` where `formula_demand_factor` is the smoothstep travel-time result (0.0–1.0). The bootstrap contribution provides a floor that decays as road connectivity delivers formula-driven demand. Example: a city with no roads at tick 0 has `effective_demand_factor(R) = 0.50 + 0.0 = 0.50`, decaying to pure formula by tick 6. The clamp to 1.0 prevents oversaturation.

  > **Clarification — what `formula_demand_factor` is and is NOT in this formula:**
  >
  > `formula_demand_factor(tick)` in the combination rule above is exclusively the **smoothstep travel-time demand factor** — the output of `clamp(1 − smoothstep(edge0, edge1, avg_travel_time), 0, 1)` for the relevant zone type (see [Traffic System](traffic-system.md)). It reflects how road connectivity and congestion affect zone attractiveness.
  >
  > It is **NOT** the capacity-ratio signal (`R_demand`, `C_demand`, or `I_demand`) defined in the "Demand curves" section above. Those capacity-ratio signals are a separate input that feeds the population growth target directly. Specifically:
  >
  > - `R_demand`, `C_demand`, and `I_demand` (the job-availability / consumer-demand / supply-chain ratios) are combined with desirability in the growth formula: `actual_population = max_density_for_tier × capacity_ratio_demand × (desirability / 100)`. They are never summed with the bootstrap term.
  > - The bootstrap term supplements only the **travel-time-based `formula_demand_factor`** — it provides a temporary floor on the connectivity signal so that growth can begin before roads are built. It does not modify the capacity-ratio signals.
  >
  > **Capacity-ratio signals during ticks 0–5**: `R_demand`, `C_demand`, and `I_demand` do **NOT** receive bootstrap treatment; they remain at their raw formula values throughout ticks 0–5. At tick 0 with no zones placed, `R_demand = 0` (no C/I jobs), `C_demand = 0` (no residents), and `I_demand = 1.0` (default when `I_production_capacity = 0`). These raw values are used directly as the `capacity_ratio_demand` in the growth formula.
  >
  > **Zero-growth at tick 0 with only Residential zones (intentional design)**: If a player places only Residential zones at game start and no Commercial or Industrial zones, then `R_demand = 0` (there are no C/I worker slots to employ residents) and the growth formula evaluates to `max_density × 0 × (desirability/100) = 0`. Population growth is exactly zero until at least one Commercial or Industrial zone is placed. This zero-growth behavior is intentional: the player must zone for employment or industry to stimulate residential growth, reinforcing the R→C→I supply-chain dependency. Similarly, `C_demand = 0` when `current_R_population = 0`, so Commercial zones placed before any residents arrive also produce zero growth until Residential population exists.
  >
  > **What the bootstrap travel-time floor does and does NOT do**: The bootstrap term (R_bootstrap = 50%, C_bootstrap = 25% at tick 0) supplements the **travel-time-based `formula_demand_factor`** — it prevents the connectivity signal from collapsing to zero before roads are built, and it prevents division-by-zero in the travel-time path. However, this bootstrap floor operates on a separate signal path from `capacity_ratio_demand`. When `capacity_ratio_demand = 0` (e.g., `R_demand = 0` because no C/I zones exist), the growth formula remains zero regardless of how large the bootstrap-supplemented `formula_demand_factor` is. The bootstrap floor is necessary and sufficient to prevent division-by-zero and to keep the travel-time component non-zero for formula evaluation — it is not sufficient to produce growth when the capacity-ratio signal is zero.
  >
  > **What produces early growth**: The `I_demand = 1.0` default ensures Industrial zones absorb capacity immediately (no C/I placement required to bootstrap Industrial growth). For Residential and Commercial zones, growth begins only once C/I zones are placed (providing non-zero `R_demand`) and R population begins to accumulate (providing non-zero `C_demand`). The bootstrap travel-time floor then ensures that growth is not further suppressed by a zero connectivity signal before roads are established. No additional floor override on the capacity-ratio signals is required or applied.
  >
  > **Implementer rule**: the two signal paths must remain separate. Passing `R_demand` (or any capacity-ratio signal) into the `bootstrap + formula_demand_factor` sum is incorrect and will produce double-counted demand during the bootstrap period.

  - **Bootstrap Oscillation Proof Requirement**: The oscillation criterion is satisfied if and only if `|effective_demand_factor(z, tick+1) − effective_demand_factor(z, tick)| ≤ 0.30` for all zone types z ∈ {R, C, I}, all tick pairs, and the three canonical scenarios:
    1. Blank map — no zones placed during the bootstrap period.
    2. All three zone types placed simultaneously at tick 0.
    3. Rapid C/I removal during the bootstrap period (place then demolish C/I tiles within ticks 0–5).

    The Phase 6 design review sign-off is valid ONLY when all three scenarios are evaluated and documented with numerical results. A blanket "analytically confirmed" statement without showing the scenario 3 calculation is not a passing sign-off.

  - **Demolition-Induced Swing Exemption**: Player demolition actions that directly reduce `total_C_I_worker_capacity` (or `I_production_capacity`) are player-driven capacity changes, not formula-driven oscillation. The oscillation criterion (`|effective_demand_factor(z, tick+1) − effective_demand_factor(z, tick)| ≤ 0.30`) applies to formula-driven evolution of demand given a **fixed city layout** (no zone placements or demolitions between ticks). For scenario 3, the analysis must evaluate the formula-driven demand trajectory AFTER the demolition completes — i.e., starting from the state immediately after the last demolition event — not across the demolition event itself. The sign-off reviewer must document: (a) the city state immediately after the final C/I demolition at tick T, (b) the resulting effective_demand_factor at tick T, and (c) the formula-driven trajectory from tick T onward through tick 6. The swing from tick T−1 (pre-demolition) to tick T (post-demolition) is exempt from the oscillation criterion because it is player-action-driven. Swings between consecutive ticks with no player actions are subject to the criterion.

  > **Bootstrap Oscillation Gate — Round 1 Review Sign-Off (PASSED):**
  >
  > All 3 canonical scenarios have been evaluated and passed the oscillation criterion (`|Δeffective_demand_factor| ≤ 0.30` for formula-driven tick-to-tick swings with a fixed city layout).
  >
  > | Scenario | Zone | Worst-case swing | Result |
  > |---|---|---|---|
  > | 1. Blank map — no zones placed during bootstrap | R | 0.083/tick (tick 0→1: bootstrap decay only) | PASS |
  > | 1. Blank map — no zones placed during bootstrap | C | 0.042/tick | PASS |
  > | 1. Blank map — no zones placed during bootstrap | I | 0.025/tick | PASS |
  > | 2. All three zone types placed simultaneously at tick 0 | R | 0.083/tick (tick 0→1) | PASS |
  > | 2. All three zone types placed simultaneously at tick 0 | C | 0.042/tick | PASS |
  > | 2. All three zone types placed simultaneously at tick 0 | I | 0.025/tick | PASS |
  > | 3. Rapid C/I removal during bootstrap (post-demolition trajectory) | R | 0.184/tick (tick 4→5, full derivation below) | PASS |
  > | 3. Rapid C/I removal during bootstrap (post-demolition trajectory) | C | 0.142/tick (tick 5→6, full derivation below) | PASS |
  > | 3. Rapid C/I removal during bootstrap (post-demolition trajectory) | I | 0.192/tick (tick 3→4, full derivation below) | PASS |
  >
  > - **Maximum formula-driven swing (any zone, any scenario)**: **0.083/tick** — Residential at tick 0→1 (pure bootstrap decay: 0.50 × 1/6 ≈ 0.083).
  > - **Worst-case combined swing (scenario 3, post-demolition trajectory)**: **0.192/tick** (Industrial, tick 3→4) — within the 0.30 threshold.
  > - **Contingency path**: NOT triggered. No HOLD flag is placed on Phase 6 DemandOscillation spike review. The oscillation gate is cleared and Phase 6 implementation may proceed without a DemandOscillation HOLD condition.
  >
  > ---
  >
  > **Scenario 3 — Full Numeric Derivation (Demolition at Tick 2)**
  >
  > **City state at tick T=2 (immediately after demolition completes):**
  >
  > - R tiles: 5 (remain, not demolished)
  > - C tiles: 0 (all 3 demolished this tick — were placed at tick 0)
  > - I tiles: 0 (both demolished this tick — were placed at tick 0)
  > - Roads: present (connected all zone tiles since tick 0)
  > - R population: ~0 (bootstrap period just started; growth begins from tick 0 but is minimal after 2 ticks)
  > - Treasury: ~$11,000 (starting grant minus road placement cost; no revenue yet — grace period active)
  >
  > **Rolling travel-time window state at tick T=2 (post-demolition):**
  >
  > Prior to demolition, roads existed and C/I destinations were reachable, so ticks 0 and 1 recorded valid A* paths with short travel times (≤ 25 s → `traffic_demand_factor = 1.0` per traffic-system.md smoothstep edge0 = 25 s). After demolition at tick 2, no C/I tiles exist, so R pathfinding has no valid destination → tick 2 is a null-path tick (`traffic_demand_factor = 0.5` default). R/C use a 5-tick rolling window; I uses a 3-tick rolling window.
  >
  > **effective_demand_factor formula used throughout:**
  > `effective_demand_factor(zone, tick) = clamp(bootstrap(zone, tick) + formula_demand_factor(zone, tick), 0.0, 1.0)`
  > where `formula_demand_factor` is the rolling-window average of `traffic_demand_factor` values.
  > Bootstrap formulas: `R_bootstrap(t) = 0.50 × max(0, 1 − t/demand_bootstrapping_ticks)`, `C_bootstrap(t) = 0.25 × max(0, 1 − t/demand_bootstrapping_ticks)`, `I_bootstrap(t) = 0.15 × max(0, 1 − t/demand_bootstrapping_ticks)`.
  > Bootstrap period ends after tick demand_bootstrapping_ticks − 1; tick demand_bootstrapping_ticks+ is purely formula-driven (the following worked example assumes demand_bootstrapping_ticks = 6).
  >
  > **Tick-by-tick trajectory — Residential (R), 5-tick rolling window:**
  >
  > | Tick | Window contents (tf = traffic_demand_factor per tick) | Window avg | R_bootstrap | eff_R | Swing from prev |
  > |---|---|---|---|---|---|
  > | T=2 (post-demo) | [1.0, 1.0, 0.5] (3 samples) | 2.5/3 = 0.833 | 0.50×(1−2/6) = 0.333 | clamp(0.333+0.833) = **1.000** | exempt (demolition tick) |
  > | T+1=3 | [1.0, 1.0, 0.5, 0.5] (4 samples) | 3.0/4 = 0.750 | 0.50×(1−3/6) = 0.250 | clamp(0.250+0.750) = **1.000** | \|1.000−1.000\| = **0.000** |
  > | T+2=4 | [1.0, 1.0, 0.5, 0.5, 0.5] (5 samples) | 3.5/5 = 0.700 | 0.50×(1−4/6) = 0.167 | clamp(0.167+0.700) = **0.867** | \|1.000−0.867\| = **0.133** |
  > | T+3=5 | [1.0, 0.5, 0.5, 0.5, 0.5] (5 samples, oldest 1.0 replaced) | 3.0/5 = 0.600 | 0.50×(1−5/6) = 0.083 | clamp(0.083+0.600) = **0.683** | \|0.867−0.683\| = **0.184** ← worst R |
  > | T+4=6 | [0.5, 0.5, 0.5, 0.5, 0.5] (all null-path) | 0.500 | 0 (bootstrap ended) | clamp(0+0.500) = **0.500** | \|0.683−0.500\| = **0.183** |
  >
  > R worst-case formula-driven swing: **0.184/tick** (tick 4→5). All swings ≤ 0.30. PASS.
  >
  > **Tick-by-tick trajectory — Commercial (C), 5-tick rolling window:**
  >
  > (C shares the same 5-tick rolling window but with lower bootstrap contribution)
  >
  > | Tick | Window avg (same as R above) | C_bootstrap | eff_C | Swing from prev |
  > |---|---|---|---|---|
  > | T=2 | 0.833 | 0.25×(1−2/6) = 0.167 | clamp(0.167+0.833) = **1.000** | exempt |
  > | T+1=3 | 0.750 | 0.25×(1−3/6) = 0.125 | clamp(0.125+0.750) = **0.875** | \|1.000−0.875\| = **0.125** |
  > | T+2=4 | 0.700 | 0.25×(1−4/6) = 0.083 | clamp(0.083+0.700) = **0.783** | \|0.875−0.783\| = **0.092** |
  > | T+3=5 | 0.600 | 0.25×(1−5/6) = 0.042 | clamp(0.042+0.600) = **0.642** | \|0.783−0.642\| = **0.141** |
  > | T+4=6 | 0.500 | 0 | clamp(0+0.500) = **0.500** | \|0.642−0.500\| = **0.142** ← worst C |
  >
  > C worst-case formula-driven swing: **0.142/tick** (tick 5→6). All swings ≤ 0.30. PASS.
  >
  > **Tick-by-tick trajectory — Industrial (I), 3-tick rolling window:**
  >
  > (I uses a 3-tick rolling window; the window flushes faster, producing a steeper initial drop)
  >
  > | Tick | Window contents | Window avg | I_bootstrap | eff_I | Swing from prev |
  > |---|---|---|---|---|---|
  > | T=2 (post-demo) | [1.0, 1.0, 0.5] (3 samples, window full) | 2.5/3 = 0.833 | 0.15×(1−2/6) = 0.100 | clamp(0.100+0.833) = **0.933** | exempt |
  > | T+1=3 | [1.0, 0.5, 0.5] (oldest 1.0 replaced by null-path 0.5) | 2.0/3 = 0.667 | 0.15×(1−3/6) = 0.075 | clamp(0.075+0.667) = **0.742** | \|0.933−0.742\| = **0.191** ← worst I (pre-clamp) |
  > | T+2=4 | [0.5, 0.5, 0.5] (all null-path) | 0.500 | 0.15×(1−4/6) = 0.050 | clamp(0.050+0.500) = **0.550** | \|0.742−0.550\| = **0.192** ← worst I |
  > | T+3=5 | [0.5, 0.5, 0.5] (all null-path, steady) | 0.500 | 0.15×(1−5/6) = 0.025 | clamp(0.025+0.500) = **0.525** | \|0.550−0.525\| = **0.025** |
  > | T+4=6 | [0.5, 0.5, 0.5] | 0.500 | 0 | clamp(0+0.500) = **0.500** | \|0.525−0.500\| = **0.025** |
  >
  > I worst-case formula-driven swing: **0.192/tick** (tick 3→4 and tick 2→3 are both near this value). All swings ≤ 0.30. PASS.
  >
  > **Summary — Scenario 3 worst-case swings across all zones:**
  >
  > | Zone | Window size | Worst-case post-demolition swing | At tick | Threshold | Result |
  > |---|---|---|---|---|---|
  > | R | 5-tick | 0.184/tick | tick 4→5 | 0.30 | PASS |
  > | C | 5-tick | 0.142/tick | tick 5→6 | 0.30 | PASS |
  > | I | 3-tick | 0.192/tick | tick 3→4 | 0.30 | PASS |
  >
  > The swing from tick T−1 (pre-demolition) to tick T (post-demolition) is exempt per the Demolition-Induced Swing Exemption — that is a player-action-driven capacity change, not formula oscillation. The swings documented above are all formula-driven (fixed city layout: no C/I tiles, 5 R tiles, roads present, ticks advancing automatically).

- **Density upgrade wave re-evaluation**: When a density tier is unlocked and the upgrade wave fires (at most 20% of eligible tiles per zone type per tick), tile eligibility is re-evaluated at the start of each tick during the multi-tick wave — not locked to the snapshot from when the unlock first triggered. A tile that met the demand_factor > 0.75 criterion at tick N may not meet it at tick N+1 (if congestion or service degradation occurs during the wave). In that case, the tile is skipped in the current wave tick and re-evaluated in the next tick. This prevents a density upgrade wave from pushing a city into deficit by upgrading tiles whose demand has since dropped, which would cause wages and upkeep to spike beyond the city's revenue. **Wave end condition**: the upgrade wave ends when either (a) all eligible tiles have been upgraded, or (b) no remaining eligible tiles meet the demand_factor > 0.75 criterion. A wave that ends condition (b) does NOT automatically restart; the unlock remains in effect and future ticks will attempt upgrades when demand recovers.
- **Terrain interaction**: See [Terrain Interaction](terrain-interaction.md) for the authoritative slope threshold (> 15.0°, exact), earthworks cost formula, and map playability guarantee.
- **Player action**: Player designates zones; engine auto-populates buildings based on demand and desirability scores

## SimulationConstants Mapping

The following named constants in `simulation_constants.h` canonicalize values that appear as inline literals elsewhere in this spec. Phase 6 property-based tests MUST reference these names rather than hardcoding magic numbers. Any change to a value requires updating the constant definition only — all references pick it up automatically.

| Constant name | Value | Spec meaning |
|---|---|---|
| `SimulationConstants::desirability_base_value` | `50` | Starting desirability for a newly zoned tile (neutral mid-point of the [0, 100] range). |
| `SimulationConstants::adjacency_commercial_residential_bonus` | `10` | Desirability bonus applied to a Residential tile when a Commercial tile is at Chebyshev distance d=1 (direct neighbor). No falloff beyond d=1. |
| `SimulationConstants::adjacency_industrial_residential_base_penalty` | `20` | Desirability penalty applied to a Residential tile when an Industrial tile is at Chebyshev distance d=1. Linear falloff to 0 at d=5. Formula: `penalty(d) = max(0, 20 × (1 − (d−1)/4))` for d ∈ [1, 5]. |
| `SimulationConstants::demand_floor_residential` | `0.20f` | Post-bootstrap minimum `effective_demand_factor` for Residential zones when a road network exists (at least one valid A* path). Applied only to the `occupancy_increase_this_tick` growth path; never applied to the decay-target computation. |
| `SimulationConstants::demand_floor_commercial` | `0.10f` | Post-bootstrap minimum `effective_demand_factor` for Commercial zones when a road network exists. Same zombie-population exclusion as `demand_floor_residential`. |
| `SimulationConstants::demand_floor_industrial` | `0.10f` | Post-bootstrap minimum `effective_demand_factor` for Industrial zones when a road network exists. Same zombie-population exclusion as `demand_floor_residential`. |

**Implementation note**: `desirability_base_value`, `adjacency_commercial_residential_bonus`, and `adjacency_industrial_residential_base_penalty` are integer constants (no fractional component). `demand_floor_residential`, `demand_floor_commercial`, and `demand_floor_industrial` are `float` constants. All six must be defined as `static constexpr` members of `SimulationConstants` in `simulation_constants.h`.
