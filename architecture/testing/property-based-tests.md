# Property-Based Test Invariants (RapidCheck)

- **Economy invariant**: For any valid initial state and N budget ticks, the treasury accounting must be exact (all terms signed):

  ```text
  final_treasury == initial_treasury
                  + Σ(tax_revenue_i + utility_fee_revenue_i + loan_principal_credited_i)
                  − Σ(road_placement_cost_total + road_maintenance_i + service_upkeep_i + wages_i + loan_repayment_i)
                  − Σ(interest_i)
  where:
    utility_fee_revenue_i   = power_fee_per_covered_R_tile × covered_R_tiles_i
                             + water_fee_per_covered_R_tile × covered_R_tiles_i
                             (SimulationConstants::utility_fee_power_per_tile = 5, water = 3)
    road_placement_cost_total = total_road_tiles_placed_during_test × SimulationConstants::road_placement_cost_per_tile
                             (= $500/tile; deducted at placement time, outside the budget tick cycle;
                              placed during the test period must be summed separately)
    road_maintenance_i      = 0  while real elapsed time < 120 s  (grace period: real-time gate, not tick count; see Economy Model)
    wages_i                 = total_C_I_tax_revenue_i × SimulationConstants::wage_fraction_of_revenue × employment_fill_rate_i
                             (wage_fraction_of_revenue = 0.20; fraction-of-revenue model — not per-worker flat cost;
                              employment_fill_rate_i = fraction of jobs filled in tick i, in [0.0, 1.0];
                              wages are only paid for filled positions — a city at 60% employment pays 0.60 × the full wage pool)
    interest_i              = outstanding_debt_i × (0.05 / ticks_per_year)  where ticks_per_year = 12
  ```

  Loan principal credited counts as positive revenue; loan repayment counts as positive expense. **First-loan debt-cap override**: When `outstanding_debt == 0` at the time a forced loan triggers, the loan amount is unconditionally `max(monthly_shortfall × 3, monthly_revenue × 0.5, $10,000)` — the 3× debt cap is NOT applied to the first loan issuance. The economy invariant property test must model this two-path formula: (a) if `outstanding_debt == 0` before the loan, credit the full first-loan amount; (b) if `outstanding_debt > 0`, credit only `max(0, 3 × max(monthly_revenue, $1,000) − outstanding_debt)`. Failing to model path (a) causes the property to compute a lower credited principal than production, producing false invariant failures on first-loan scenarios. **Interest timing**: Interest is computed on the outstanding balance **before** repayment principal is deducted that tick (i.e., `interest_i = outstanding_debt_at_start_of_tick_i × (0.05 / ticks_per_year)`), then repayment is applied, reducing the balance for the next tick. The property test must use this order explicitly — applying repayment before interest would produce a different (slightly lower) interest total and would not match the production implementation. The test must instantiate `ticks_per_year` as the named constant (= 12) — do not hardcode `0.05 / 12` inline; use the named constant from `SimulationConstants::ticks_per_year`. The test must also use `SimulationConstants::loan_repayment_ticks` (= 12) when computing `repayment_principal_per_tick = loan_principal / SimulationConstants::loan_repayment_ticks` — do not hardcode 12 or the fraction inline. `service_upkeep_i` and `wages_i` must be computed using `SimulationConstants::service_upkeep_*_per_tick` and `SimulationConstants::wage_fraction_of_revenue` (see Economy Model) — not hardcoded values.
- **Treasury arithmetic contract**: All treasury values and monetary amounts in the simulation MUST be stored as integer types (e.g., `int64_t` representing whole dollars or cents). Floating-point interest calculations must be truncated to the nearest dollar before being applied to the treasury (truncation rule: `interest_applied = static_cast<int64_t>(outstanding_debt * 0.05 / ticks_per_year)`). This ensures the treasury invariant uses exact integer equality (`==`) rather than epsilon comparison, making the RapidCheck property deterministic and reproducible. If fractional cent amounts arise from formula design, the spec must define the rounding rule explicitly (truncation toward zero is the default).
- **Multi-loan pooling formula**: When multiple loans are outstanding simultaneously (loan 2 issued before loan 1 is fully repaid), each loan is tracked and repaid independently at its own per-tick rate. The total loan repayment per tick is the sum of all individual loan repayment amounts: `loan_repayment_i = Σ_k(loan_k_repayment_per_tick_i)`. The property test must model this pooled repayment schedule — failing to sum across all outstanding loans will compute a lower expense total, producing false invariant failures on multi-loan scenarios. Interest is also computed on the total outstanding balance (sum of all unpaid loan principals) before repayment that tick. The `outstanding_debt` used in the debt cap invariant is the sum of all outstanding individual loan balances.
- **Loan repayment remainder rule**: When `loan_principal` is not evenly divisible by `loan_repayment_ticks` (12), the **last repayment tick** absorbs the remainder: `repayment_on_last_tick = loan_principal − (repayment_principal_per_tick × (loan_repayment_ticks − 1))`. All earlier ticks use `repayment_principal_per_tick = loan_principal / loan_repayment_ticks` (integer truncation). This rule must be tested explicitly: `TEST_F(EconomyTest, LoanRepayment_NonDivisiblePrincipal_LastTickAbsorbsRemainder)` with `loan_principal = $10,001` over 12 ticks — verify ticks 1–11 each repay $833, tick 12 repays $838 (total = $10,001). **Math verification**: $10,001 / 12 = $833.41… → truncated to $833; $833 × 11 = $9,163 (ticks 1–11); last tick remainder = $10,001 − $9,163 = $838. Without this rule, truncating all 12 ticks by $833 gives 12 × $833 = $9,996, leaving **$5** of principal permanently unreduced (treasury invariant failure). Note: the stranded amount is $5, not $1 — a prior draft of this spec used $10,001 with the incorrect arithmetic `$10,001 − (12 × $833) = $10,001 − $9,996 = $5`; always compute from the 11-tick partial sum, not the 12-tick product.
- **Loan cooldown invariant** (separate property): For any sequence of deficit ticks, no two forced loan events occur with fewer than 2 budget ticks between them. Test as a standalone `rc::check` independent of the treasury invariant.
- **Debt cap invariant** (separate `rc::check`): For any sequence of deficit events, the total outstanding debt never exceeds `3 × max(monthly_revenue, $1,000)`. Property: generate **N ≥ 7** consecutive months of deficit; verify `outstanding_debt_after_each_tick ≤ 3 × max(monthly_revenue_that_tick, 1000)` holds after every tick. The generator must use `rc::gen::inRange(7, 24)` for N to guarantee multi-loan scenario coverage (N=7 allows first loan + 2-tick cooldown + second loan + 2-tick cooldown + third loan attempt = 1 + 2 + 1 + 2 + 1 = 7, fully exercising the cap enforcement path with pooled loans). N < 7 provides insufficient coverage of pooled multi-loan repayment scenarios. **ManualClock gate requirement**: The debt cap `rc::check` must call `manualClock.advance(121.0)` before starting the deficit tick sequence to ensure the 120 s real-time loan gate has elapsed — without this, no forced loans fire and the test trivially passes without exercising the cap enforcement path at all. Use `121.0` (not `120.1`) to avoid floating-point precision issues near the gate boundary.
- **Loan activation gate invariant** (separate `rc::check`): The forced loan mechanic must never fire before the first non-zero revenue budget tick has occurred AND before 120 **real seconds** (wall-clock seconds via `IClock::nowSeconds()`) have elapsed since game start. **The 120 s gate is a real-time gate, not a simulation-tick gate.** Property: inject a `ManualClock`, advance `ManualClock::nowSeconds()` to any value < 120.0 (e.g., RapidCheck generates values in [0, 119.99]), generate a deficit scenario, verify no forced loan is issued regardless of deficit magnitude. **Do not control tick count to test this gate** — the property must advance `ManualClock` real seconds directly, because the gate checks `IClock::nowSeconds()`, not tick count. Controlling tick count tests the wrong variable and will produce false positives even if the real-time gate is broken.
  - **Loan gate boundary test** (fixed-seed unit test in `tests/simulation/`): Required `TEST_F` case `LoanGate_FiresAtExactly120Seconds`: inject `ManualClock`, advance to `manualClock.advance(119.0)` + deficit tick → verify no loan; advance to `manualClock.advance(1.0)` (total = 120.0 s) + deficit tick → verify loan fires. **IEEE 754 note**: do NOT use `advance(119.9)` + `advance(0.1)` — these two floating-point additions do not produce exactly `120.0` in IEEE 754 double precision (119.9 + 0.1 ≈ 119.99999999999998... ≠ 120.0), causing the gate check `nowSeconds() >= 120.0` to fail spuriously. Use integer-valued advances (`119.0` + `1.0`) to guarantee an exact double result. The gate comparison is **`>=`**: `IClock::nowSeconds() >= 120.0` — the loan fires at exactly 120.0 s, not strictly after 120.0 s. The `>=` operator must be documented in the production implementation's gate check to prevent an off-by-one regression where the loan fails to fire at exactly 120.0 s. Validates the 120 s wall-clock gate boundary exactly.
- **Zero-revenue surplus tests** (unit tests in `tests/simulation/`): These tests exercise a deterministic code path (zero revenue → no deficit consequences) and must use `StrictMock<MockAudioSystem>` and `StrictMock<MockRenderer>` — NOT `ManualRNG`. `ManualRNG` is only needed for tests that exercise random building selection (service degradation). Zero-revenue tests have no random draws; injecting ManualRNG would be misleading (it implies randomness is involved). Using StrictMock ensures that no unexpected audio/render calls fire during the zero-revenue path. Required `TEST_F` cases:
  1. `BudgetSurplus_ZeroRevenue_ReturnsZero`: when `monthly_revenue == 0`, `budget_surplus_pct` returns 0 (no division by zero).
  2. `BudgetSurplus_ZeroRevenue_NoDeficitConsequences`: when `monthly_revenue == 0`, no deficit consequence events fire (no loan, no service degradation).
  3. `BudgetSurplus_LargeDeficit_Representable`: `budget_surplus_pct` may return negative values; verify the formula does NOT clamp at −100%. Use concrete values: `monthly_revenue = $1,000`, `total_monthly_expenses = $3,000` → `budget_surplus_pct = (1000 − 3000) / 1000 × 100 = −200%`. Assert `budget_surplus_pct == -200` (not clamped to −100). **Note**: the previous name `BudgetSurplus_NegativeResult_Clamped` was self-contradictory (the test verifies no clamping occurs). The canonical name is `BudgetSurplus_LargeDeficit_Representable`. This test verifies only the surplus formula; it does NOT verify the game-over consecutive-tick requirement — that is test 4 below.
  4. `GameOverThreshold_RequiresConsecutiveTicks`: Explicitly verifies the consecutive-month requirement for game-over. Use `monthly_revenue = $1,000`, `total_monthly_expenses = $3,000` (−200% surplus, well below the −50% threshold). Run ONE budget tick → verify `isGameOverSurplusThresholdBreached()` returns **false** (streak = 1, not yet 3). Run a SECOND consecutive tick → verify returns **false** (streak = 2). Run a THIRD consecutive tick → verify returns **true** (streak = 3; game-over condition met). `isGameOverSurplusThresholdBreached()` MUST check both (a) `budget_surplus_pct ≤ −50%` AND (b) this condition has persisted for 3+ **consecutive** budget ticks — testing only (a) on the current tick would trigger game-over on transient single-tick anomalies. Splitting this from test 3 makes the consecutive-state-machine logic independently verifiable.
- **Seed reproduction** applies to all RapidCheck invariants (economy, traffic, zoning, terrain): on failure, print `// Reproduce with seed: 0x<hex>` and add a fixed-seed regression test before closing the finding.
- **Traffic invariant**: For any connected road graph, pathfinding must return a path of finite length for any source/destination pair
- **Zoning invariant**: Desirability scores must remain in [0, 100] for any valid zone + adjacency configuration
- **Terrain generator playability invariants** (RapidCheck + fixed-seed regression, in `tests/terrain/`):
  - **`TerrainGenerator_AlwaysTerminates_WithinReSeedLimit`** (re-seed termination property): Inject a mock `ITerrainRNG` that counts the number of re-seed calls. For any generated map with any `uint64_t` seed, verify the generator calls re-seed at most 100 times before either succeeding or returning an explicit error. This property can be falsified by RapidCheck (a pathological seed could trigger > 100 re-seeds if the constraint logic is broken). **Must use the two-argument constructor form**: `TerrainGenerator(seed, &mockRng)` where `mockRng` is a `MockTerrainRNG` instance that counts `reseed()` calls. Do NOT use the single-argument production form for this test — it would construct an internal `std::mt19937_64` and the reseed count would be unobservable.
  - **`TerrainGenerator_OutputAlwaysMeetsConstraint`** (output regression check): Because this property is NOT falsifiable by RapidCheck (the generator satisfies it by construction), it must be written as a **`TEST_F` with a fixed set of seeds** rather than a bare `rc::check` loop. Using `rc::check` for a non-falsifiable property wastes RapidCheck iterations and provides no additional coverage signal. Use at least **6 seeds** covering edge cases (zero, max uint64_t, small values, large values, and known regression seeds):

    ```cpp
    TEST_F(TerrainGeneratorTest, OutputAlwaysMeetsConstraint) {
        for (uint64_t seed : {
            0xDEADBEEF00000001ULL,  // primary regression seed (must be pinned once impl passes)
            42ULL,                   // small value
            0ULL,                    // zero seed
            0xFFFFFFFFFFFFFFFFULL,   // max uint64_t
            12345678901234567ULL,    // arbitrary large value
            0xCAFEBABE12345678ULL    // bit-pattern coverage
        }) {
            auto map = TerrainGenerator(seed).generate();
            EXPECT_GE(map.flatTileRatio(), 0.20) << "Seed: " << std::hex << seed;
        }
    }
    ```

    This regression check verifies that `flatTileRatio()` returns the correct value for a completed generation and that the constraint enforcement code has not been accidentally disabled.
  - **`TerrainGenerator_OutputHasContiguousFlatArea`**: For any generated map, the largest contiguous region of flat-or-sub-15° tiles spans >= 2,500 tiles (50×50). Property verifies BFS/flood-fill result. **Same caveat as `OutputAlwaysMeetsConstraint` — this is an output check, not falsifiable by RapidCheck.** It MUST be written as a `TEST_F` with a fixed set of seeds, not a bare `rc::check` loop:

    ```cpp
    TEST_F(TerrainGeneratorTest, OutputHasContiguousFlatArea) {
        for (uint64_t seed : {
            0xDEADBEEF00000001ULL,  // primary regression seed
            42ULL,
            0ULL,
            0xFFFFFFFFFFFFFFFFULL,
            12345678901234567ULL,
            0xCAFEBABE12345678ULL
        }) {
            auto map = TerrainGenerator(seed).generate();
            EXPECT_GE(map.largestContiguousFlatArea(), 2500u) << "Seed: " << std::hex << seed;
        }
    }
    ```

  - **Fixed-seed regression (combined)**: Seed `0xDEADBEEF00000001` must produce **both** >=20% flat tiles AND >=2,500 contiguous flat tiles in a single `TEST_F` assertion. This combined check ensures both constraints are jointly satisfied and is the primary regression guard for the terrain generator:

    ```cpp
    TEST_F(TerrainGeneratorTest, PrimaryRegressionSeed_MeetsBothConstraints) {
        auto map = TerrainGenerator(0xDEADBEEF00000001ULL).generate();
        EXPECT_GE(map.flatTileRatio(), 0.20) << "flatTileRatio failed for primary regression seed";
        EXPECT_GE(map.largestContiguousFlatArea(), 2500u) << "contiguousFlatArea failed for primary regression seed";
    }
    ```

    This seed is locked in once the generator implementation passes it.
- **Power grid service coverage unit tests** (in `tests/simulation/`, pure C++ logic):
  1. `PowerCoverage_ConnectedTiles_AreCovered`: all tiles reachable via the power grid graph from a placed power plant are covered; disconnected tiles are not.
  2. `PowerCoverage_IsolatedBuilding_Reports100Pct`: a power plant with no buildable tiles reachable in the grid reports `service_level_pct = 100%`.
  3. `PowerCoverage_DeficitDegradation_ReducesBFSRadius`: at −10% budget surplus, the power plant applies the BFS-distance brownout — nodes at BFS depth > `floor(max_depth × 0.70)` from the plant transition from covered to uncovered. All nodes at or below `floor(max_depth × 0.70)` remain covered. **Use a concrete N=10 linear chain** for deterministic verification: nodes 0 (plant) through 9, max_depth = 9, threshold = `floor(9 × 0.70)` = `floor(6.3)` = 6. Nodes 0–6 (depth ≤ 6) must remain covered; nodes 7–9 (depth > 6) must be uncovered after the brownout. The test must explicitly verify: (a) node at depth 6 (boundary node) IS covered; (b) node at depth 7 (first uncovered node) is NOT covered. Without this boundary node verification, a common off-by-one bug (using `>=` vs `>` in the threshold comparison) passes all non-boundary assertions while leaving the boundary node in the wrong state.
  4. `PowerCoverage_MultipleBuildings_NoStacking`: two power plants whose grids overlap a tile: the tile counts as covered once, not twice, for `service_level_pct` purposes.
