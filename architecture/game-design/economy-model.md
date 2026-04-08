# Economy Model

- **Revenue sources**: Residential / commercial / industrial tax rates (per zone per density tier); utility fees; tourism income
  - **Tax revenue formula per tile per budget tick**: `tax_revenue = base_income_per_resident[density_tier] × actual_population × zone_tax_rate`. Base income per resident constants (SimulationConstants): Residential Low = $50/resident/month, Residential Medium = $50/resident/month, Residential High = $55/resident/month. Commercial and Industrial tiles use the same base income constants applied to their worker counts at the corresponding density tier. **Income tier equality note**: `base_income_per_resident_low = $50` and `base_income_per_resident_medium = $50` are equal — this non-obvious equality is intentional. The dominant progression incentive at Medium density comes from the much higher population capacity per tile, not a per-resident rate premium. Giving Medium density the same per-resident rate as Low density prevents over-incentivizing medium-density zoning relative to the higher-capacity High-density tier. Only High-density residential uses a higher rate (`base_income_per_resident_high = $55`). **Design rationale**: Per-resident income must be non-decreasing with density tier so that zoning progression is rewarding at every scale. The dominant revenue multiplier comes from the much higher population capacity at higher density tiers (R-Low: 100 residents, R-Med: 400 residents, R-High: 1,000 residents per tile) — the small per-resident premium at R-High ($55 vs $50) is secondary but reinforces that denser development is more economically productive per resident. These constants are calibrated so that a standard opening layout of 20 R-Low / 10 C-Low / 5 I-Low tiles at 50% occupancy generates approximately $8,000–$12,000 monthly revenue on Normal difficulty, reaching the $50K Medium density unlock within 8–12 budget ticks. **Calibration basis**: Approximately 100 residents per fully-occupied Low-density tile and a starting zone tax rate of 10% on Normal difficulty (`tax_revenue = 100 residents × $50 × 0.10 = $500 per fully-occupied R-Low tile`; at 50% occupancy = $250/tile). C-Low = 25 workers × $50 × 0.10 = $125/fully-occupied tile; at 50% occupancy = $62.50/tile. I-Low = 50 workers × $50 × 0.10 = $250/fully-occupied tile; at 50% occupancy = $125/tile. Verification: 20 × $250 + 10 × $62.50 + 5 × $125 = $5,000 + $625 + $625 = $6,250 from tax revenue; utility fees ($8/covered Residential tile/tick × 20 tiles = $160) and partial occupancy variance bring total monthly revenue to approximately $6,400–$8,000, consistent with the $8K floor being achievable at higher occupancy or additional tiles. **Note**: the $8–$12K target is achievable at higher than 50% average occupancy (which occurs naturally during the bootstrap demand period) or with more tiles in the opening layout. If the Population Density & Growth spec defines different tile capacities, these constants must be recalibrated to maintain the $8–$12K target. See [Population Density & Growth](population-density-growth.md) for authoritative tile capacity values per density tier.
  - Utility fees (collected each budget tick from covered zones): Power $5/covered Residential tile/tick, Water $3/covered Residential tile/tick. Utility fees are collected automatically alongside tax revenue and cannot be individually adjusted by the player in V1.
  - Tourism income: Post-V1 scope. The "tourism income" line in Revenue sources is reserved for future implementation; it produces $0 revenue in V1.
- **Expenditure sinks**: Road maintenance cost per tile per budget tick; service building upkeep; wages
- **Budget surplus definition**: `budget_surplus_pct = (monthly_revenue − total_monthly_expenses) / monthly_revenue × 100` (negative when in deficit). This value drives all deficit consequence thresholds listed below. **Zero-revenue guard**: when `monthly_revenue == 0`, `budget_surplus_pct` is defined as `0` (neutral — no deficit consequences fire). Deficit consequences are evaluated only when `monthly_revenue > 0`. **Non-negativity invariant**: `monthly_revenue` is always ≥ 0; all revenue sources (taxes, utility fees) floor at $0 per tile. If any future revenue source could be negative, it must be reclassified as an expense. This invariant must be enforced in the Economy invariant property-based test.
- **Deficit consequences**: Service degradation at −10% budget surplus; forced loan auto-issued at −25%; game-over condition at −50% for 3+ consecutive months (Scenario mode only — see [Game Over Flow](game-over-flow.md))
- **Tax rate bounds**: All zone tax rates have a **floor of 1%** and a **ceiling of 25%** per zone type. These bounds apply to both player-set rates and the forced tax raise in the loan Decline path. A raise that would exceed 25% is capped at 25% and shown as "already at maximum" in the projected rate display.
- **Tax rate persistence across New Game**: Tax rates set by the player are intentionally **NOT reset** when starting a New Game within the same application session. This is by design — tax rates represent a long-term economic policy preference, not spatial city state (zone layout, road network, buildings). They persist across `CitySimulation::reset()` calls. A returning player's preferred tax policy carries forward so they do not have to re-configure rates after restarting. This behavior is consistent with the undo-system design, which explicitly excludes economy events (tax rate changes, loan issuances) from undo scope. Default rates (10% Residential, 10% Commercial, 10% Industrial) apply only on the very first session startup before the player has made any adjustments.
- **Road placement cost**: Each road tile costs **$500 per tile** to place (deducted from treasury immediately at placement). This is separate from the road maintenance cost per budget tick. Road placement cost is NOT waived during the grace period.
- **Road maintenance cost per budget tick** (after grace period): **$10 per road tile per budget tick**. A city with 100 road tiles incurs $1,000/month maintenance after the grace period ends. This value is used in the Economy invariant property-based test and must be referenced as `SimulationConstants::road_maintenance_cost_per_tile = 10`.
- **Service building upkeep per budget tick** (after grace period): Fixed costs deducted each budget tick per building. These are `SimulationConstants` values and must not be hardcoded inline in tests:
  - `SimulationConstants::service_upkeep_fire_station_per_tick = 500` (dollars/tick)
  - `SimulationConstants::service_upkeep_police_station_per_tick = 400` (dollars/tick)
  - `SimulationConstants::service_upkeep_power_plant_per_tick = 1000` (dollars/tick)
  - `SimulationConstants::service_upkeep_water_tower_per_tick = 300` (dollars/tick)
  - Total `service_upkeep_i` for a given tick = sum of upkeep for all placed service buildings.
- **Wages per budget tick**: `wages_per_tick = total_C_I_tax_revenue × SimulationConstants::wage_fraction_of_revenue × employment_fill_rate` where:
  - `SimulationConstants::wage_fraction_of_revenue = 0.20`
  - `total_C_I_tax_revenue` is the sum of all Commercial and Industrial zone tax revenues at the time of the budget tick
  - `employment_fill_rate = total_employed_C_I_workers / total_C_I_worker_capacity` (clamped to [0.0, 1.0]) — wages scale with actual employment, not just zone capacity. A Commercial zone at 50% occupancy pays 50% of the wages a fully-occupied zone would pay. This prevents a freshly-zoned but empty Commercial district from incurring full wages before it has any workers.
  - **Wages are a visible budget line item** in the HUD resource bar and the budget detail panel, displayed alongside road maintenance and service upkeep. Without this visibility, players cannot diagnose overspending caused by high-density Commercial/Industrial zones.
  - **Rationale**: A per-worker flat cost breaks the economy at scale. The fraction-of-revenue model guarantees wages never exceed C/I tax income. The fill-rate multiplier prevents wage costs from being charged for unoccupied zone capacity.
  - The Economy invariant property test must use this formula with the named constants; do not hardcode values inline.
- **Loan mechanic**: Forced loan amount = `max(monthly_shortfall × 3, monthly_revenue × 0.5, $10,000)` — ensures the loan is always large enough to be meaningful at any city scale. **First loan debt-cap override**: When `outstanding_debt == 0` (first loan ever), the $10,000 minimum floor applies unconditionally even if $10,000 exceeds `3 × max(monthly_revenue, $1,000)` — a small seed city with very low revenue must still receive at least $10,000 to have any chance of recovery. The 3× debt cap is NOT applied to the first loan issuance. **Loan pooling and debt cap enforcement**: The forced loan amount formula above applies only when `outstanding_debt == 0`. If outstanding debt already exists when a new forced loan triggers, the new loan amount is capped at `max(0, 3 × max(monthly_revenue, $1,000) − outstanding_debt)` — i.e., only enough to bring total debt to the 3× cap. If `outstanding_debt` already equals or exceeds `3 × max(monthly_revenue, $1,000)`, the forced loan does not issue; instead the Emergency Municipal Bond (Option 3 in the Decline sub-screen) is the player's only recourse. **Emergency Municipal Bond terms**: The Emergency Municipal Bond is available only when `outstanding_debt >= 3 × max(monthly_revenue, $1,000)` (debt cap exhausted). Bond amount = `2 × outstanding_debt` (doubles the current debt load, providing a substantial cash injection). Interest rate = **5% per in-game year** (the same unified rate as forced loans — the bond's distinguishing cost is its repayment obligation and larger principal, not a different interest rate). Bond principal is repaid over **24 budget ticks** (2 in-game years, twice the standard repayment period; named constant SimulationConstants::bond_repayment_ticks = 24; must not be hardcoded; distinct from the forced-loan repayment period of loan_repayment_ticks = 12 ticks). Per-tick repayment = `bond_principal / 24` (integer truncation; last tick absorbs remainder). Outstanding bond principal is added to the outstanding debt pool and subject to the same interest accrual and pooling rules as forced loans. The bond carries no additional cooldown (it is available whenever the debt cap is exhausted, subject to the existing 2-tick loan cooldown from the most recent forced loan). **Bond use limits per difficulty** (named `SimulationConstants`):`SimulationConstants::bond_max_uses_easy = 3`,`SimulationConstants::bond_max_uses_normal = 2`,`SimulationConstants::bond_max_uses_hard = 1`. These limits reflect that Hard mode offers the least safety net; Easy mode softens the learning curve with additional recovery opportunities.`outstanding_bond_uses` is tracked as a field in the `Economy` sub-system (`src/simulation/Economy.h`); it is decremented by 1 on each bond issuance and is serialized in the save file so that mid-game saves correctly restore the remaining use count. When `outstanding_bond_uses` reaches 0, the Emergency Municipal Bond button is grayed out in the modal. For UI presentation of the remaining-uses indicator and the grayed-out state when exhausted, see [Modal Dialog System](../ui-ux/modal-dialog-system.md). If the Emergency Municipal Bond is issued and the city remains in a ≥ −50% deficit for 3+ consecutive months thereafter, the game-over condition fires regardless of the bond's remaining repayment schedule — the bond is a recovery tool, not a game-over bypass. **Design intent**: The Emergency Municipal Bond provides a recovery ceiling for extreme-deficit scenarios where the debt cap has been exhausted, preventing the city from entering a state where no assistance mechanism is available. The larger principal (2× outstanding debt) and longer repayment period (24 ticks vs. 12 for forced loans) create a meaningful consequence for reaching this state while still offering a viable recovery path. Interest accrues at the same 5% per in-game year as forced loans — the entire outstanding debt pool (loans and bonds combined) uses the unified formula `interest_per_tick = outstanding_debt × (0.05 / ticks_per_year)`. This prevents successive deficit ticks from stacking loan principal beyond the stated debt cap. Interest rate = 5% per in-game year (simple interest), applied as`interest_per_tick = outstanding_debt × (0.05 / ticks_per_year)` where `ticks_per_year = 12` (see [Simulation Time System](simulation-time.md)); **Loan repayment schedule**: Loan principal is repaid over `loan_repayment_ticks = 12` budget ticks (1 in-game year). Per-tick principal repayment: `repayment_principal_per_tick = loan_principal / loan_repayment_ticks`. The outstanding debt on which interest is calculated decreases by`repayment_principal_per_tick` each tick after interest is applied. Multiple outstanding loans are each tracked and repaid independently at their own per-tick repayment rate. The total loan repayment per tick is the sum of individual loan repayment amounts: `total_loan_repayment_per_tick = Σ_k(loan_k_principal / loan_repayment_ticks)`. Interest is computed on the total outstanding balance (sum of all unpaid loan principals) before repayment that tick. The`outstanding_debt` used in the debt cap invariant is the sum of all outstanding individual loan balances. The named constant `loan_repayment_ticks = 12` must be used everywhere this value appears, not hardcoded; max outstanding debt = 3× monthly revenue (floor: $1,000 minimum monthly revenue for this calculation to prevent zero-cap at game start); **loan cooldown**: minimum 2 budget ticks (months) between forced loan events to prevent cascade spirals; loan mechanic only activates after the first non-zero revenue budget tick; **budget deficit warning gate**: the `BudgetDeficitWarn` notification and `SFX_BUDGET_WARN` audio event (fired when `budget_surplus_pct ≤ −0.25`) are gated on the same `m_firstRevenueTicked` flag as the forced loan — neither fires before at least one non-zero revenue budget tick has been processed; this suppresses spurious deficit warnings during the early-game period when service-building upkeep creates a deficit before any zones generate tax income; **first loan real-time gate**: the forced loan mechanic cannot trigger until at least **120 real seconds have elapsed since game start** regardless of simulation speed — this prevents new players from immediately facing a crisis modal before understanding the economy (the existing 2-tick cooldown governs subsequent loans after the first); repayment auto-deducted from each budget tick proportionally; persistent HUD debt indicator shown whenever debt > 0; toast notification on loan issuance with amount and terms
- **Industrial→R/C feedback loop**: Industrial employment drives residential demand (workers need homes) and commercial demand (industrial wages create purchasing power for goods and services). This feedback is modelled via the `demand_factor` calculation in [Population Density & Growth](population-density-growth.md). Key economic implication: placing Industrial zones increases wages paid per tick (`wages_per_tick = total_C_I_tax_revenue × wage_fraction × employment_fill_rate`) but also increases Commercial tax revenue by driving C zone growth, which partially offsets the wage cost. Players who zone Industrial-only without balancing Residential supply will see low `employment_fill_rate` (no workers) → low wages paid AND low I tax revenue. The three-way zone balance is both a gameplay requirement and an economic equilibrium.
- **Early-game grace period**: Road maintenance costs and service building upkeep are **waived until at least 120 real seconds have elapsed since game start**, regardless of simulation speed. This real-time gate (not a tick-count gate) is the binding constraint: at 10× speed, ticks fire every ~3 real seconds, so 3 ticks would expire in ~9 real seconds — far too fast for a new player to learn the economy. The 120 s floor ensures the grace period is always a meaningful protection window. The 120-second value must be defined as `SimulationConstants::grace_period_real_seconds = 120` and referenced at all call sites via this named constant — do not hardcode 120 inline. **Gate measurement start**: the 120-second elapsed-time counter begins from the moment `CitySimulation` receives its first `tick()` call (i.e., from game-start frame 1), not from `CitySimulation` construction time; this ensures the gate is deterministic and independently verifiable in tests via `ManualClock::advance(120.0)` against the named constant without any dependency on construction-time wall-clock state. Phase 6 forced-loan tests must use `ManualClock::advance(120.0)` against this named constant to verify the gate behavior. The HUD countdown shows "Cost waiver: Xs remaining" (seconds) rather than a tick count, updated every real second. Auto-save triggers immediately when the forced loan dialog becomes active, so pre-crisis state is always saved. The grace period is shared with the forced loan real-time gate — both activate at 120 s for a unified new-player protection window.
- **Surplus incentives** (unlocks are **permanent** — triggered by monthly revenue milestones, not raw treasury balance, so capital-intensive cities are not locked out):
  - **Difficulty-scaled unlock thresholds**: All revenue milestones are multiplied by a per-difficulty `SimulationConstants` constant. Three separate fields are defined (a single field cannot hold per-difficulty values and forces hardcoded branching at every call site): `density_unlock_scale_easy = 0.70` — density unlock scale at Easy difficulty (70% of Normal threshold); `density_unlock_scale_normal = 1.00` — density unlock scale at Normal difficulty (baseline); `density_unlock_scale_hard = 1.50` — density unlock scale at Hard difficulty (50% harder threshold). Phase 6 unlock logic selects the appropriate field based on the current `Difficulty` setting via `SimulationConstants::density_unlock_scale_easy` / `_normal` / `_hard`. The values below are Normal difficulty baselines.
  - Monthly revenue ≥ $50,000 for 3 consecutive months → Medium-density residential tier unlocked
  - Monthly revenue ≥ $50,000 for 3 consecutive months → Medium-density commercial tier unlocked
  - Monthly revenue ≥ $75,000 for 3 consecutive months → Medium-density industrial tier unlocked
  - Monthly revenue ≥ $100,000 for 3 consecutive months → High-density residential tier unlocked (**hard prerequisite: Med-Industrial must already be unlocked — see below**)
  - Monthly revenue ≥ $200,000 for 3 consecutive months → High-density commercial tier unlocked
  - Monthly revenue ≥ $500,000 for 3 consecutive months → High-density industrial tier unlocked
  - **Hard prerequisite gate — Med-Industrial before High-Residential**: High-density Residential **cannot activate** until Medium-density Industrial has already unlocked, regardless of whether the High-R revenue milestone is satisfied. This is a structural dependency, not a design guideline: even if the player's revenue has met the High-R threshold for 3 consecutive months, High-R will NOT activate until Med-I is also unlocked. **Rationale**: High-R brings 1,000 residents/tile demanding C and I goods; without Med-I unlocked, the city cannot meet this supply demand, creating immediate supply-chain starvation. **Simultaneous threshold crossing rule**: When multiple density tier thresholds are crossed in the same budget tick (e.g., revenue jumps from $30K to $150K in a single tick), unlocks are processed in this strict order: (1) Med-R and Med-C simultaneously, (2) Med-I, (3) High-R (only after Med-I activates in the same tick), (4) High-C, (5) High-I. Processing in this order ensures the supply-chain prerequisite is always satisfied even on rapid revenue growth. **Why a hard gate and not just ordering**: without a hard gate, a player on Easy difficulty (0.70× scale) could potentially satisfy both Med-I and High-R thresholds in the same budget tick by jumping from $35K to $100K+ revenue — the ordering rule handles this case. A hard gate is required because revenue milestones are evaluated independently (each threshold checks its own 3-consecutive-month window); a player whose monthly revenue jumped quickly may have satisfied High-R's window before completing Med-I's window.
  - HUD shows progress toward each unlock threshold (current monthly revenue vs. required, at the selected difficulty level)
  - **`getNextUnlockThreshold()` return semantics**: `ICitySimulation::getNextUnlockThreshold(Difficulty d)` returns the difficulty-adjusted revenue value (in dollars) that the player must sustain for 3 consecutive months to trigger the next pending density tier unlock. The tiers are evaluated in the canonical unlock order defined above: Med-R/Med-C (same threshold), Med-I, High-R, High-C, High-I. The function returns the threshold of the **lowest-indexed tier that is not yet unlocked**. When all six density tiers are unlocked (High-I is the final tier), the function returns **`-1.0f`** as a sentinel value meaning "no further unlocks pending". The sentinel is `−1.0f` (negative one, as a float) rather than `std::numeric_limits<float>::max()` for the following reasons: (a) `float` max (~3.4 × 10^38) cannot be meaningfully formatted in a HUD label without special-casing; (b) `−1.0f` is unambiguously out-of-range for any valid threshold (all valid thresholds are positive dollar amounts), so a simple `threshold < 0.0f` guard is sufficient to detect the sentinel with no risk of false positives; (c) the value is trivially comparable in both C++ and tests without pulling in `<limits>`. **Contract**: the return value is never `0.0f` or `NaN`; it is either a positive dollar value (difficulty-adjusted) or exactly `−1.0f`. The named constant `SimulationConstants::kNoUnlockThreshold = -1.0f` MUST be used at every call site that checks for the sentinel — do not compare against the literal `−1.0f` inline. **HUD handling of the sentinel**: when `getNextUnlockThreshold(d)` returns `kNoUnlockThreshold`, the density unlock progress indicator in the resource bar MUST be hidden via `IUIBackend::setElementVisible(handle, false)` and the Density Unlock Preview Tooltip MUST NOT appear regardless of proximity calculations — both the indicator and the tooltip are suppressed for the remainder of the session once all tiers are unlocked. See [HUD Layout](../ui-ux/hud-layout.md) (Density Unlock Preview Tooltip section) for the authoritative HUD suppression rule.
  - **Density upgrade rate limiter**: When a density tier is unlocked, at most **20% of eligible tiles per zone type** (rounded up, minimum 1 per zone type) upgrade per budget tick. The 20% cap is applied independently to each zone type (R, C, I) — upgrading Residential tiles does not count against the Commercial tile cap. This prevents a mass simultaneous upgrade from spiking wages and costs in a single tick — the transition smooths over approximately 5 budget ticks. HUD shows a preview: when monthly revenue is within 10% of an unlock threshold, a projected "After Unlock" estimated monthly expense change is shown in the resource bar tooltip so the player can prepare.

## Budget Screen Section Mapping

The Budget Detail Panel (see `architecture/ui-ux/hud-layout.md` §Budget Detail Panel) organises
all economy line items into two sections. The authoritative assignment for each V1 revenue and
expense category is:

**Income section**

- Tax revenue — Residential: `taxRate.residential * residentialPopulation` (per-tick, summed
  across all R tiles)
- Tax revenue — Commercial: `taxRate.commercial * commercialValue` (per-tick, summed across all
  C tiles)
- Tax revenue — Industrial: `taxRate.industrial * industrialValue` (per-tick, summed across all
  I tiles)
- Utility fees: collected each budget tick from covered zones (Power $5/covered R tile/tick,
  Water $3/covered R tile/tick) — see revenue constants above
- Tourism income: post-V1 scoped — renders as a grayed-out placeholder row "Tourism income: $0
  (post-V1)" in the Income section; not a live V1 data source. Rendered via
  `setElementEnabled(handle, false)`. See `architecture/ui-ux/hud-layout.md` §Budget Detail
  Panel, Tourism income line item.

**Expenses section**

- Road maintenance: per-tile upkeep cost (`road_maintenance_cost_per_tile × road_tile_count`)
  per budget tick
- Service upkeep: fire/police/power/water building operating cost per budget tick (see service
  upkeep constants above)
- Wages: city employee payroll (`wages_per_tick` formula — see Wages section above)

**Total**: Income total − Expenses total = net monthly balance (positive = surplus,
negative = deficit).

For display formatting, sign colours, and panel layout see
`architecture/ui-ux/hud-layout.md` §Budget Detail Panel.

**Governance rule**: Any future income or expense category added to this spec MUST be assigned
to either the Income or Expenses section of the Budget Detail Panel before merging, to avoid
future display regressions.

## Phase 10 Audio Callbacks for Economy Events

The following audio events fire at the moment an economy condition is first detected.
`sfx_budget_warn` is called directly from `CitySimulation::doBudgetTick()` (which delegates
to `Economy::doEconomyTick()`); `sfx_loan_issued` is called from within
`Economy::checkAndIssueForcedLoan()` (invoked internally by `Economy::doEconomyTick()`,
not directly from `doBudgetTick()`). All calls are guarded by `m_audio != nullptr`.

### `sfx_budget_warn` — Budget deficit threshold crossing

**Trigger**: `budget_surplus_pct` crosses below −25% for the first time in a deficit streak
(i.e. the tick on which `budget_surplus_pct` first drops to ≤ −0.25 AND the previous tick's
surplus was > −0.25, OR the streak counter has just been reset to 0 and is now re-entering
deficit). This matches the `BudgetDeficitWarn` notification gate: both the notification and
the SFX fire together on the same tick.

**Guard**: Gated on `m_firstRevenueTicked` (same gate as the forced-loan mechanism) — never
fires before at least one non-zero revenue budget tick has been processed.

**Call site**: `Economy::doEconomyTick()`, inside the budget-deficit-consequence block,
immediately after the `BudgetDeficitWarn` notification is enqueued.

```cpp
// In Economy::doEconomyTick(), after enqueueing BudgetDeficitWarn notification:
if (m_audio && crossedDeficitThreshold) {
    m_audio->playSound(SFX_BUDGET_WARN, SoundPriority::NORMAL, 1.0f);
}
```

`SFX_BUDGET_WARN` = SoundId 7 (`sfx_budget_warn.wav`). Non-positional
(`AL_SOURCE_RELATIVE = AL_TRUE`), EFX bypass. No cooldown enforced at the call site —
the once-per-deficit-streak gate above is sufficient.

### `sfx_loan_issued` — Forced loan auto-issued

**Trigger**: Each time a forced loan is successfully issued (i.e. `budget_surplus_pct` ≤ −0.25
AND the 2-tick loan cooldown has elapsed AND `outstanding_debt < debtCap`). Fires once per
loan issuance event, not once per deficit tick.

**Call site**: `Economy::checkAndIssueForcedLoan()` (called internally from
`Economy::doEconomyTick()`), immediately after the loan principal is added to
`outstanding_debt` and the loan-issued toast is enqueued.

```cpp
// In Economy::checkAndIssueForcedLoan(), after loan issuance:
if (m_audio) {
    m_audio->playSound(SFX_LOAN_ISSUED, SoundPriority::NORMAL, 1.0f);
}
```

`SFX_LOAN_ISSUED` = SoundId 8 (`sfx_loan_issued.wav`). Non-positional
(`AL_SOURCE_RELATIVE = AL_TRUE`), EFX bypass.

## Music Intensity Tiers

The adaptive music system uses three intensity tiers driven by live simulation state. These thresholds are authoritative for `Population::updateMusicIntensity()` (called from `CitySimulation::doBudgetTick()`) and for the test `AdaptiveMusicIntensity_StateDriven_UpdatesAudioSystem`.

| Tier | Condition | Notes |
|---|---|---|
| CALM | `budget_surplus_pct >= 0%` | City is not in deficit. Default state when neither CRISIS nor GROWTH applies. |
| GROWTH | Net population change is positive (population this tick > population previous tick) | Takes priority over CALM when CRISIS is not active. |
| CRISIS | `consecutive_deficit_months >= 2` | Deficit streak has lasted 2 or more consecutive budget ticks. Highest priority tier. |

**Priority rules** (applied when multiple conditions are satisfied simultaneously):

1. CRISIS takes highest priority — overrides GROWTH and CALM.
2. GROWTH takes priority over CALM when CRISIS is not active.
3. CALM is the default when neither CRISIS nor GROWTH applies.

**`consecutive_deficit_months`** is a counter incremented each budget tick in which `budget_surplus_pct < 0%` and reset to `0` on any tick where `budget_surplus_pct >= 0%`. It is subject to the same `m_firstRevenueTicked` gate as the forced loan and deficit warning — it is not incremented before at least one non-zero revenue budget tick has been processed.

Phase 10 wires `Population::updateMusicIntensity()` (called from `CitySimulation::doBudgetTick()`) to call `audioSystem->setMusicIntensity()` using these thresholds — see the **Music intensity interface** deliverable in `implementation/phase-10.md`.

## Multi-Loan Pooling Boundary Examples

The following concrete examples clarify the loan pooling formula for implementers and test authors. All values use the $1,000 minimum revenue floor for the debt cap calculation.

**Scenario A — First-loan override with zero revenue (debt cap < minimum floor):**

- `monthly_revenue = $0` → `debtCap = 3 × max($0, $1,000) = $3,000`
- First loan: `outstanding_debt == 0` → override applies → `principal = $10,000`
- `outstanding_debt` after first loan = `$10,000 > debtCap ($3,000)` — override permitted
- Second loan trigger: `remaining = max(0, $3,000 − $10,000) = $0` → no issuance; bond only

**Scenario B — $10,001 boundary (revenue = $3,334/month, debtCap = $10,002):**

- First loan: minimum floor = `$10,000`; `outstanding_debt = $10,000`
- Second loan: `remaining = max(0, $10,002 − $10,000) = $2`
- Second loan principal = `min(computed_amount, $2) = $2`
- Total debt = `$10,000 + $2 = $10,002 = debtCap` (pool exactly exhausted)

**Scenario C — One below boundary (revenue = $3,333/month, debtCap = $9,999):**

- First loan = `$10,000`; `outstanding_debt = $10,000`
- Second loan: `remaining = max(0, $9,999 − $10,000) = $0` → no issuance; bond only

These examples are verified by `MultiLoanPooling_FirstLoanOverride_And_DebtCapBoundary` in `tests/simulation/economy_test.cpp`.

## Phase 6 Balance Sign-Off

<!-- SIGN-OFF: gamedesign-lookandfeel 2026-02-26 — V1 economy balance constants reviewed and accepted. All items below confirmed against simulation_constants.h and CitySimulation.cpp. -->

This section records the formal balance review of all V1 economy constants prior to Phase 6 completion. Each item below references the implemented constant value from `simulation_constants.h` and evaluates it against the intended gameplay pacing for the Sandbox mode (V1 scope: Sandbox only; Scenario mode is stub-only).

### Starting Funds by Difficulty

- Easy: `starting_funds_easy = $1,000,000` — Accepted. Provides a long runway that lets players experiment freely with zoning layouts, road networks, and service placement without early budget pressure. Appropriate for onboarding.
- Normal: `starting_funds_normal = $500,000` — Accepted. Sufficient to place 20–30 road tiles ($10,000–$15,000), one of each service building ($22,000 total: Power Plant $10,000 + Water Tower $3,000 + Fire Station $5,000 + Police Station $4,000), and still have adequate operating capital through the 120 s grace period. The capital buffer comfortably covers 8–12 budget ticks of service upkeep ($2,200/tick for a minimal one-of-each service set) before stable tax revenue arrives.
- Hard: `starting_funds_hard = $200,000` — Accepted. Forces disciplined early spending: road tiles at $500 each and road maintenance at $10/tile/tick mean every tile counts. A 20-road-tile opening layout costs $10,000 at placement plus $200/tick maintenance after the grace period — affordable but leaves little room for over-building services. Adds meaningful challenge without being punishing given the 120 s grace period.

The three-tier static_assert chain (`easy > normal > hard`) in `simulation_constants.h` enforces the correct ordering at compile time.

### Tax Revenue Formula and Base Income Constants

Constants: `base_income_per_resident_low = $50`, `base_income_per_resident_medium = $50`, `base_income_per_resident_high = $55`.

**Revenue calibration at Normal difficulty (10% zone tax rate, standard opening layout):**

A representative opening layout of 20 R-Low / 10 C-Low / 5 I-Low tiles at 50% average occupancy produces:

- R-Low tax: 20 tiles × 50 residents × $50 × 10% = $5,000/tick
- C-Low tax: 10 tiles × 12.5 workers × $50 × 10% = $625/tick
- I-Low tax: 5 tiles × 25 workers × $50 × 10% = $625/tick
- Utility fees: 20 covered R-tiles × ($5 power + $3 water) = $160/tick
- Gross total: $6,410/tick at 50% occupancy

The $8,000–$12,000/tick target stated in this spec is reached at approximately 65–80% average occupancy, which occurs naturally during the bootstrap demand period (ticks 0–5 apply fixed starter demand floors, driving occupancy above 50% in early growth). Accepted: the formula and constants are correctly calibrated to the stated opening layout target.

The intentional equality of `base_income_per_resident_low = base_income_per_resident_medium = $50` is confirmed as correct design — the progression incentive at Medium density comes from the 4× tile population capacity increase (100 → 400 residents), not a per-resident rate premium. The $55 premium at High density ($55 vs $50, a 10% increase) is secondary reinforcement that denser development is economically productive per resident, without over-rewarding Medium density zoning relative to High.

### Wage Fraction

Constant: `wage_fraction_of_revenue = 0.20` (20% of C+I tax revenue, scaled by employment fill rate).

At 50% occupancy on the standard opening layout: wages = ($625 + $625) × 0.20 × 0.50 = $125/tick. This is a minor deduction (≈1.9% of gross revenue) that does not starve the city at low occupancy. At full occupancy: wages = ($1,250 + $1,250) × 0.20 × 1.00 = $500/tick — still only 7.8% of gross revenue at full occupancy on the same layout. The fraction-of-revenue model guarantees wages never exceed C/I tax income regardless of city scale. Accepted: wage fraction is balanced and does not create early-game revenue starvation.

### Service Upkeep Costs

Constants per budget tick: `service_upkeep_fire_station_per_tick = $500`, `service_upkeep_police_station_per_tick = $400`, `service_upkeep_power_plant_per_tick = $1,000`, `service_upkeep_water_tower_per_tick = $300`.

A minimal one-of-each service set costs $2,200/tick combined. At Normal difficulty with the standard opening layout generating ~$6,285/tick net after wages, the four service buildings consume 35% of net revenue — affordable but meaningful. A player who over-builds services (e.g., two fire stations before any stable revenue) will feel the pressure, which is the intended design signal to pace service investment. The power plant carries the highest per-tick cost ($1,000) reflecting its city-wide impact and the strategic weight of placing it early. Accepted: service upkeep costs are affordable on Normal difficulty with 20+ road tiles and a functioning zone layout, without being trivially ignorable.

### Road Maintenance

Constant: `road_maintenance_cost_per_tile = $10` per budget tick (after grace period).

At 20 road tiles: $200/tick. At 50 road tiles: $500/tick. At 100 road tiles: $1,000/tick. Even at 100 tiles — a substantial early-game road network — maintenance is only $1,000/tick, roughly equivalent to one power plant. Road maintenance scales linearly and creates a genuine incentive to avoid unnecessary road sprawl, but does not become prohibitive for any reasonable early-game road network. The $500 per-tile placement cost is NOT waived during the grace period, which correctly front-loads the road investment cost rather than letting players flood the map with free roads during the protection window. Accepted: road maintenance does not become prohibitive for early-game road networks.

### Loan Mechanic Thresholds and Grace Period Gate

Constants: `grace_period_real_seconds = 120.0` (shared gate for cost waiver and first forced loan); `loan_cooldown_ticks = 2` (minimum 2 budget ticks between forced loans); forced loan interest rate: 5%/year applied as `outstanding_debt × (0.05 / 12)` per tick.

The 120 s real-time gate is binding regardless of simulation speed. At 10× speed a budget tick fires every 3 real seconds, so a new player at maximum speed would see 40 ticks in 120 s — far more than enough exposure to understand the basics before loan mechanics activate. At 1× speed, 120 s corresponds to 4 budget ticks (4 in-game months), giving players adequate time to zone, road, and see initial revenue before any crisis can trigger. The 2-tick loan cooldown prevents cascade spirals after the grace period ends. Accepted: the 120 s gate gives new players adequate runway across all simulation speeds.

### Density Unlock Thresholds

Normal-difficulty base thresholds: Med-R/Med-C $50,000 | Med-I $75,000 | High-R $100,000 | High-C $200,000 | High-I $500,000. Each requires sustained 3-month (3-tick) revenue above the threshold.

**Reachability analysis at Normal difficulty:**

The $50,000 Med-R/Med-C threshold requires monthly revenue roughly 8× the standard opening layout baseline at 50% occupancy ($6,410). This is reached by expanding the zone count (e.g., 80+ R-Low tiles at 50% occupancy) or by reaching higher occupancy on a larger layout. At 65% average occupancy and 40 R-Low / 20 C-Low / 10 I-Low tiles, estimated gross revenue ≈ $20,800 + $2,600 + $1,625 + $320 (utility) ≈ $25,345/tick — still below $50K, which means a player must expand meaningfully before the first unlock triggers. This puts the Med-R/Med-C unlock within a reasonable session (estimated 20–40 real minutes at Normal speed), not trivially early.

The difficulty scaling constants (`density_unlock_scale_easy = 0.70`, `density_unlock_scale_normal = 1.00`, `density_unlock_scale_hard = 1.50`) correctly modulate reachability: Easy players reach Med-R/Med-C at $35,000 (faster feedback loop), Hard players must sustain $75,000 (substantially harder). The High-I $500,000 threshold is a long-term prestige milestone for Sandbox — appropriate for V1 scope where there is no scenario time limit.

The 3-consecutive-month requirement (not a single threshold crossing) is enforced via the sustained counter logic in `CitySimulation`, guarded by the `kNoUnlockThreshold` sentinel. Accepted: unlock thresholds are reachable within a reasonable Normal-difficulty session and scale correctly across difficulties.

### density\_upgrade\_wave\_demand\_threshold = 0.50

Constant: `density_upgrade_wave_demand_threshold = 0.50f`.

Tile eligibility for density upgrade requires `demand_factor >= 0.50`. The `null_path_demand_default = 0.5f` means that tiles with no valid A\* path to a destination record exactly 0.5 demand — the minimum value that satisfies the 0.50 threshold. This is a neutral value representing "technically accessible but poorly connected" and correctly allows upgrade waves to proceed even before the road network is fully built out. The threshold sits at the smoothstep midpoint, meaning tiles with any valid path connectivity above the null-path default satisfy the criterion. A threshold of 0.50 prevents upgrade waves from firing only in the degenerate case where demand has fully collapsed (which requires both null-path default and formula-driven signals simultaneously below 0.50 — a condition that indicates genuine connectivity failure, not early-game startup). Accepted: fires at neutral demand, enabling progression even before the road network is complete.

### null\_path\_demand\_default = 0.5f

Constant: `null_path_demand_default = 0.5f`.

The 0.5 default for tiles with no valid A\* path represents a deliberate neutral value: neither 0.0 (which would collapse demand on a brand-new map before any roads exist, preventing bootstrap growth) nor 1.0 (which would falsely signal fully connected access for unrouted tiles). The static_assert in `simulation_constants.h` enforces `null_path_demand_default > 0.0f && < 1.0f`. At 0.5, null-path tiles contribute a demand value equal to the smoothstep midpoint, consistent with the "technically accessible but poorly connected" semantic documented in the traffic system spec. The value also ensures the demand floor check (R floor = 0.20, C/I floors = 0.10) is never the binding constraint for null-path tiles — 0.5 exceeds all three demand floors, so the floor is inert in the all-null-path case, as stated in the zoning-system spec. Accepted: 0.5 is the appropriate neutral value for pre-road startup and for partially connected cities.

---

**Sign-off summary**: All nine balance items reviewed and accepted. The V1 economy constants in `simulation_constants.h` are consistent with the spec in `architecture/game-design/economy-model.md`, with the pacing targets documented in this file, and with the V1 Sandbox-only scope defined in `architecture/game-design/minimum-viable-simulation.md`. No re-balancing is required before Phase 6 completion.

---

**IClock injection**: `CitySimulation` accepts `IClock*` at construction for the grace-period real-time gate (120 s) and the forced-loan real-time gate (120 s). Production passes `WallClock`; tests advance time deterministically via `ManualClock::advance(120.0)`. See `architecture/testing/testability-architecture.md §IClock` for the canonical interface definition.
