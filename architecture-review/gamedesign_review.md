# Game Design Architecture Review
**Date**: 2026-03-29
**Scope**: All files in `architecture/game-design/`, `architecture/ui-ux/`, `architecture/audio-architecture/dynamic-soundscape.md`, `architecture/audio-architecture/v1-audio-asset-manifest.md`

---

## Summary Table

| Severity | Count |
|---|---|
| CRITICAL | 8 |
| HIGH | 14 |
| MEDIUM | 12 |
| LOW | 7 |

---

## CRITICAL Issues

---

### C-01 — Service building placement rule contradicts zone placement rule
**Files**: `service-coverage.md` §Placement Rules, `zoning-system.md` §Multi-Tile Footprint Placement Rules §Service Building Street Adjacency
**Type**: [INCONSISTENCY]

`service-coverage.md` §Placement Rules states: "Service buildings may be placed on **any buildable tile** (slope ≤ 15.0°) regardless of whether that tile is already occupied by a zone designation or a road. The player is **not** required to demolish an existing zone or road before placing a service building. This exemption is authoritative and supersedes any implementation-phase plan note."

`zoning-system.md` §Service Building Street Adjacency states: "Service buildings may only be placed if **at least one tile in their 2×2 footprint is directly edge-adjacent (4-directional cardinal, distance = 1) to a road tile**. If no cardinal-adjacent road exists, placement is **rejected**."

The service-coverage spec says service buildings can be placed anywhere with no road requirement; zoning-system says they require direct road adjacency. These are irreconcilable in a single implementation — one spec must govern. Additionally, `service-coverage.md` says "The player is not required to demolish an existing zone or road before placing a service building. The zone or road occupancy on a tile does not block service building placement." This directly contradicts `zoning-system.md` §Placement Collision Check which implies all tiles in the footprint must be empty.

**Proposed resolution**: Decide which spec governs and update the other. The road-adjacency requirement in `zoning-system.md` is the more realistic design. The `service-coverage.md` statement "does not block service building placement" appears to mean service buildings can overlay zones but not roads (a road tile is not a zone). Clarify that: (a) road adjacency is required (zoning-system governs), (b) service buildings can be placed on unzoned or zone-occupied tiles but NOT on road-occupied tiles. Remove the contradictory absolute override clause from `service-coverage.md`.

---

### C-02 — Density upgrade demand threshold constant mismatch
**Files**: `zoning-system.md` §Density upgrade wave re-evaluation, §Construction Delay; `population-density-growth.md` §Demand-to-density gating
**Type**: [INCONSISTENCY]

`zoning-system.md` §Density upgrade wave re-evaluation says: "tile eligibility is re-evaluated... `demand_factor >= SimulationConstants::density_upgrade_wave_demand_threshold` (0.50)".

`zoning-system.md` §Construction Delay says: "if `effective_demand_factor >= SimulationConstants::construction_delay_demand_threshold` (0.50)".

`population-density-growth.md` §Demand-to-density gating says: "Tiles only auto-upgrade density when the demand factor for that zone type > **0.75** and the appropriate unlock has been achieved."

The density upgrade condition is specified as ≥ 0.50 in `zoning-system.md` but **> 0.75** in `population-density-growth.md`. These are two different thresholds for the same mechanic. The construction delay uses 0.50, which may or may not be the same as the upgrade threshold — the constant names suggest they are different constants but the values happen to be equal, creating confusion.

**Proposed resolution**: Declare one file authoritative for each constant. `population-density-growth.md` should govern the upgrade threshold (0.75 is the more defensible design — higher demand needed to justify the density jump). `zoning-system.md` should govern the construction delay threshold (0.50 is correct for initial construction). Update `zoning-system.md` §Density upgrade wave re-evaluation to reference `density_upgrade_threshold = 0.75` and ensure the constant name differs from `construction_delay_demand_threshold`.

---

### C-03 — Loan forced-loan gate vs. grace period: two overlapping 120-second windows with different scope
**Files**: `economy-model.md` §Early-game grace period, §Loan mechanic; `simulation-time.md` §Grace Period Interaction
**Type**: [PROBLEM] / [GAP]

The economy model spec states both:
1. "Early-game grace period: Road maintenance costs and service building upkeep are waived until at least 120 real seconds have elapsed."
2. "First loan real-time gate: the forced loan mechanic cannot trigger until at least **120 real seconds have elapsed since game start**."
3. "The grace period is shared with the forced loan real-time gate — both activate at 120 s for a unified new-player protection window."

But the spec also says "budget deficit warning gate: the `BudgetDeficitWarn` notification and `SFX_BUDGET_WARN` audio event (fired when `budget_surplus_pct ≤ −0.25`) are gated on the same `m_firstRevenueTicked` flag as the forced loan — neither fires before at least one non-zero revenue budget tick has been processed."

This creates a gap: the forced loan is gated on the 120s real-time AND `m_firstRevenueTicked`. The grace period waives expenses (preventing deficits) but taxes are collected from tick 0. Service upkeep constants (fire station $500/tick, power $1000/tick) fire as soon as the grace period ends and could immediately push a small city into −25% deficit. The spec does not define what happens if a forced loan triggers exactly at the 120s boundary on the same tick that grace period ends — specifically, whether the `m_firstRevenueTicked` gate or the 120s real-time gate is evaluated first, and whether both must pass simultaneously.

**Proposed resolution**: Clarify the gate evaluation order explicitly: both conditions must be true simultaneously before a forced loan triggers. Add a worked example: "If the 120s expires on a tick where the city first goes to −25% deficit, both gates clear on that tick and the loan dialog appears immediately." The spec text already says this is the intent but the interaction with `m_firstRevenueTicked` is ambiguous when the first revenue tick and the 120s expiry happen at the same simulation tick.

---

### C-04 — Sandbox game-over contradiction: toasts fire but counter is "not applicable"
**Files**: `game-over-flow.md` §Game Mode Applicability table
**Type**: [PROBLEM]

The game-over flow spec table states under "Sandbox mode": "`getConsecutiveDeficitMonths()` counter increments normally — YES" and "Month-1 CRITICAL toast ('2 months to bankruptcy') — YES" and "Month-3 `transitionToGameOver()` + blocking ModalDialog — NO."

This means in Sandbox mode, month-1 fires a toast that explicitly says "2 months to bankruptcy" even though no bankruptcy is possible. This is a player-deceptive message. A Sandbox player who keeps running a city in deficit will repeatedly see "2 months to bankruptcy" and "1 month to bankruptcy" without ever experiencing bankruptcy. After the counter reaches 3, nothing happens and the counter presumably resets — but the spec does not state what happens to the counter after month 3 in Sandbox mode.

Additionally, the spec says "the simulation remains in Running state indefinitely" when month 3 is reached in Sandbox, but does not specify whether the counter resets, continues incrementing, or clamps at 3. If the counter is clamped at 3 and the deficit persists, the month-2 CRITICAL toast ("1 month to bankruptcy") will never fire again, but the red flashing indicator remains active permanently. The spec does not address this edge case.

**Proposed resolution**: (a) In Sandbox mode, change the month-1 and month-2 toast text to "City finances critical — consider raising taxes or issuing a loan" rather than implying impending bankruptcy. OR specify a Sandbox-specific variant of the toast text. (b) Explicitly specify that after the counter reaches 3 in Sandbox mode it resets to 0 on the next tick (regardless of whether the deficit continues), allowing subsequent streak warnings to fire normally. This prevents permanent CRITICAL toast lockout in Sandbox.

---

### C-05 — Music stem intensity trigger thresholds undefined
**Files**: `architecture/audio-architecture/dynamic-soundscape.md`
**Type**: [GAP]

The dynamic soundscape spec defines the three adaptive music intensity tiers (calm / growth / crisis) and their crossfade mechanics, but **never defines what simulation conditions trigger transitions between tiers**. There is no specification of:
- What city state causes the music to transition from calm to growth
- What condition triggers the crisis tier (is it the deficit streak? Service degradation? Both?)
- Whether the stinger_crisis fires on the same condition that triggers crisis music, or independently
- What causes the music to transition back from crisis to growth or calm

The asset manifest says `stinger_crisis` = "Crisis start" but does not define what "crisis start" means in simulation terms.

**Proposed resolution**: Add a section to `dynamic-soundscape.md` defining the intensity-tier transition rules explicitly, e.g.: calm = population < 1,000 or deficit_months = 0; growth = population ≥ 1,000 and deficit_months = 0; crisis = deficit_months ≥ 1 OR any service in degraded state. Align the `stinger_crisis` trigger with the crisis-tier entry condition. This is critical for the audio system implementation and currently forces the developer to invent these thresholds.

---

### C-06 — Population milestone thresholds partially misaligned with City Rating tiers
**Files**: `game-progression-modes.md` §City Rating tiers, `save-system.md` §Population milestone serialization, `v1-audio-asset-manifest.md` §stinger_milestone
**Type**: [INCONSISTENCY]

`game-progression-modes.md` states: "milestone notifications at 1K / 10K / 50K / 100K / 500K population."

`save-system.md` states: "a `population_milestone_fired` boolean array of exactly 5 elements, one per threshold (1K/10K/50K/100K/500K)."

City Rating transitions occur at: 1K (Village→Town), 10K (Town→City), 50K (City→Metropolis), 500K (Metropolis→Megalopolis).

The stinger fires at City Rating transitions: 1K, 10K, 50K, 500K — NOT at 100K.

The population milestones (1K, 10K, 50K, **100K**, 500K) include 100K which is NOT a City Rating threshold. The spec carefully notes "100K is a population milestone (toast only) but NOT a City Rating transition threshold." This is consistent.

However, `save-system.md` references `SimulationConstants::population_milestone_threshold_1..5` but these constants are not defined in any spec file. Their values (1000, 10000, 50000, 100000, 500000) can be inferred but are not canonically documented. If an implementer uses City Rating thresholds as a reference, they will miss 100K. The constants should be explicitly listed.

**Proposed resolution**: Add a table to `game-progression-modes.md` or `simulation-time.md` explicitly defining `population_milestone_threshold_1 = 1000`, `_2 = 10000`, `_3 = 50000`, `_4 = 100000`, `_5 = 500000`. Cross-reference these named constants in `save-system.md` with their values so there is no ambiguity.

---

### C-07 — `getDemandPressurePct()` semantics contradicted in two places
**Files**: `traffic-system.md` §Demand pressure readouts, `hud-layout.md` §Demand pressure bar
**Type**: [INCONSISTENCY]

`traffic-system.md` says: "`ICitySimulation::getDemandPressurePct(ZoneType)` returns the city-wide EFFECTIVE demand in [0.0, 1.0] (1.0 = maximum demand)."

`hud-layout.md` §Demand pressure bar says: "`getDemandPressurePct()` returns `float` in `[0.0, 1.0]` — NOT a percentage in `[0, 100]`. The HUD demand bars MUST multiply by `100.0f` before display."

Both agree on the return range and scale. However the bold warning in `hud-layout.md` about multiplying by 100 before display suggests the method name "PressurePct" is misleading — it returns a fraction not a percentage — yet the name ends in "Pct". More critically, `traffic-system.md` §Demand pressure readouts introduces `QueryResult::demandPressurePct` as `(1.0f − effective_demand_factor) × 100` (inverse semantics, 0–100 range), while `getDemandPressurePct(ZoneType)` from `ICitySimulation` returns [0.0, 1.0] in direct semantics. Two methods with similar names have **opposite semantics and different scales** — this is a high implementation-error risk.

**Proposed resolution**: Rename one of these to eliminate the confusion. Suggested rename: `ICitySimulation::getDemandPressurePct(ZoneType)` → `ICitySimulation::getZoneDemandFactor(ZoneType)` (returns [0.0, 1.0] in direct semantics). Keep `QueryResult::demandPressurePct` as-is (per-tile inverse percentage for Inspector panel). Update all cross-references. This removes the dangerous near-identical naming with opposite semantics.

---

### C-08 — Emergency Municipal Bond availability condition has circular dependency edge case
**Files**: `economy-model.md` §Loan mechanic; `modal-dialog-system.md` §Forced loan dialog Option 3
**Type**: [PROBLEM]

The Emergency Municipal Bond is available "only when `outstanding_debt >= 3 × max(monthly_revenue, $1,000)` (debt cap exhausted)." The bond amount is `2 × outstanding_debt`.

If `outstanding_debt` equals exactly the debt cap and `monthly_revenue = $0` (zero revenue), then: debt_cap = 3 × max(0, $1,000) = $3,000. Bond amount = 2 × $3,000 = $6,000. After issuance, outstanding_debt = $3,000 + $6,000 = $9,000. New debt cap at $0 revenue = $3,000. New outstanding_debt ($9,000) already vastly exceeds the new cap. The bond itself immediately re-exhausts the debt cap, making the city ineligible for a forced loan again immediately after the bond is issued.

The spec says "If the Emergency Municipal Bond is issued and the city remains in a ≥ −50% deficit for 3+ consecutive months thereafter, the game-over condition fires regardless of the bond's remaining repayment schedule." But it does not specify whether the increased outstanding_debt from the bond raises the debt cap (since debt_cap = 3 × monthly_revenue, not 3 × outstanding_debt). At zero revenue, the debt cap stays at $3,000 regardless of how much bond debt was issued, meaning the city can never get another forced loan after the bond is issued. This is likely intentional design (the bond is the final lifeline) but it is not explicitly stated.

**Proposed resolution**: Add a note explicitly stating: "After bond issuance, the debt cap (3 × max(monthly_revenue, $1,000)) does not increase merely because outstanding_debt increased — it scales only with monthly_revenue. At zero revenue the debt cap is $3,000 and both forced loans and bonds are unavailable unless the player raises monthly_revenue above $1,000. This is by design: the bond is the terminal recovery mechanism."

---

## HIGH Issues

---

### H-01 — `sfx_budget_warn` fires at −25% but the forced loan also triggers at −25%; audio/event order unspecified
**Files**: `economy-model.md` §Phase 10 Audio Callbacks; `modal-dialog-system.md` §Forced loan dialog
**Type**: [GAP]

`sfx_budget_warn` fires when `budget_surplus_pct ≤ −0.25`. The forced loan dialog also triggers at `budget_surplus_pct ≤ −0.25`. Both fire at the same threshold on the same tick. The spec defines `sfx_loan_issued` as firing "after the loan principal is added to `outstanding_debt`," but does not define whether `sfx_budget_warn` fires before or after the forced loan dialog appears on the same budget tick. If both fire on the same tick: does `sfx_budget_warn` fire once at the crossing and then `sfx_loan_issued` fires, or is `sfx_budget_warn` suppressed because the loan is immediately issued? The `SFX_BUDGET_WARN` guard specifies "fires on first deficit crossing per streak" — if the forced loan is immediately issued, the next tick may not be in deficit, so the streak effectively ends and the next deficit could re-fire `sfx_budget_warn`. This interaction is unspecified.

**Proposed resolution**: Add an explicit ordering rule: on the tick that both the budget deficit threshold and the forced loan trigger simultaneously, `sfx_budget_warn` fires first (pre-loan), then the loan dialog modal opens and `sfx_loan_issued` fires on acceptance. If the forced loan is auto-issued (no dialog), `sfx_budget_warn` still fires once before `sfx_loan_issued`. This prevents silent audio on the first deficit crossing.

---

### H-02 — Demolition refund behavior during density upgrade transition undefined
**Files**: `zoning-system.md` §Density Upgrade Resolution; `undo-system.md` §V1 scope
**Type**: [GAP]

When a density upgrade automatically demolishes neighboring same-zone-type, lower-density buildings ("automatically demolish that neighbour **without cost** (no treasury refund, no undo window)"), the spec does not address:
1. Whether the demolished neighbor tiles count against the player's single-undo slot.
2. Whether the undo of the upgrading tile also restores the auto-demolished neighbors.
3. Whether the `SFX_BUILD_DEMOLISH` sound fires for each auto-demolished neighbor.

The undo system spec says "last destructive player action (zone placement, road placement, demolition)" — auto-demolition during upgrade is engine-driven, not a player action, so it presumably does not enter the undo stack. But this creates a situation where buildings can permanently disappear without the player explicitly demolishing them and without an undo option, with no spec guidance on what the player sees.

**Proposed resolution**: Explicitly state in `zoning-system.md` §Density Upgrade Resolution: "Auto-demolished neighbors do not enter the undo stack and are not recoverable via Ctrl+Z. The NORMAL toast 'Neighbouring [zone] building cleared for upgrade' is the sole acknowledgment. `SFX_BUILD_DEMOLISH` fires once per auto-demolished tile via `IAudioSystem::playPositionalSound` at the tile center."

---

### H-03 — Undo refund cap undefined: "clamped" to what?
**Files**: `undo-system.md` §Undo semantics
**Type**: [GAP]

"If the refund would exceed the starting capital cap, the treasury is clamped (no negative refunds)." This sentence is internally inconsistent. It says "exceed the starting capital cap" but the intent appears to be "exceed the treasury cap" or "cause the treasury to exceed some maximum." There is no treasury cap defined anywhere else in the specs — the player's treasury is unbounded. The parenthetical "(no negative refunds)" suggests the real intent is "refunds do not go negative" (i.e., the refund is clamped at 0). But "clamped to starting capital cap" implies there is a maximum treasury value equal to the starting funds, which would prevent the player from ever having more money than they started with.

**Proposed resolution**: Rewrite as: "Funds are refunded at the original placement cost. Refunds are always non-negative — if the original cost was already deducted in full, the full amount is restored. There is no maximum treasury cap; the refund may bring the treasury above its previous high."

---

### H-04 — Road abandonment desirability penalty for zones without road proximity not specified in service-coverage or economy model
**Files**: `zoning-system.md` §Zone Street Proximity §Abandonment and Recovery
**Type**: [GAP]

The zoning system spec (`zoning-system.md`) references "§Abandonment and Recovery" in the §Zone Street Proximity section, but the content was cut off at line 228 in the available text (the section heading exists but no body content is visible in what was read). This is a gap — the abandonment mechanic for zones that lose road proximity after being placed (e.g., road demolished post-zoning) is referenced but the actual content may be missing or incomplete in the spec.

**Proposed resolution**: Verify that the §Abandonment and Recovery section is fully specified beyond line 228 of `zoning-system.md`. If the content exists but was truncated in this review, disregard. If the content is absent, specify: "If a zone tile that was previously within 3 tiles of a road loses road proximity (e.g., the adjacent road is demolished), the zone enters 'abandoned' state: no new residents move in (growth capped at 0) but existing population decays at the standard decay rate (−15% of max_density per tick). The zone recovers when road proximity is restored within 3 tiles."

---

### H-05 — Demand floor interaction with `construction_delay_demand_threshold` creates immediate-construction anomaly
**Files**: `zoning-system.md` §Construction Delay, §Demand bootstrapping; `zoning-system.md` §Residential demand floor
**Type**: [PROBLEM]

The residential demand floor post-bootstrap is 0.20. The construction delay demand threshold is 0.50. The floor (0.20) is below the construction threshold (0.50), so at steady state with minimal congestion the floor alone is insufficient to trigger construction. This is correct behavior.

However, during bootstrap ticks 0–5 the effective demand can be 0.50 (bootstrap only, R_demand = 0 because no C/I zones exist). The construction delay gate checks `effective_demand_factor >= 0.50`. A newly placed Residential tile at tick 0 on a blank map has `effective_demand_factor(R) = 0.50 + 0.0 = 0.50` (bootstrap term only, no capacity-ratio demand because no C/I zones). This EXACTLY meets the 0.50 threshold, meaning construction begins immediately at tick 0 with no jobs in the city. This contradicts the design note: "Growth is exactly zero until at least one Commercial or Industrial zone is placed" — but that refers to the growth formula, not to building mesh placement. A building mesh spawns at exactly 0 population with no growth, then sits empty.

**Proposed resolution**: Either raise the construction delay threshold to > 0.50 (e.g., 0.55) to require genuine formula-driven demand above the null-path default before construction begins, or add a guard: "Construction delay demand threshold is evaluated against the formula-driven `capacity_ratio_demand`, NOT the `effective_demand_factor` (which includes the bootstrap subsidy). This ensures buildings only spawn when the city has genuine employment or consumer demand."

---

### H-06 — Utility fees collected from "covered" zones but power/water coverage is bidirectional: placing zones after service buildings gets immediate fees
**Files**: `economy-model.md` §Revenue sources (Utility fees); `service-coverage.md` §Coverage model
**Type**: [GAP]

"Utility fees (collected each budget tick from covered zones): Power $5/covered Residential tile/tick, Water $3/covered Residential tile/tick."

The spec defines utility fees as collected only from covered zones, which requires service buildings to be placed before zones to generate utility revenue. However, coverage is evaluated at the next budget tick — the spec does not define whether a zone placed in the same tick as a service building is counted as "covered" for that tick's utility fee calculation. More critically: does a zone tile placed 1 tick before the service building, and then retroactively covered, collect utility fees retroactively? The spec implies no, but the exact tick-synchrony is unspecified.

Also: utility fees are listed as Revenue but there is no mechanism for the player to view per-tile utility coverage status except through the minimap overlay or the Inspector panel. The Finances Panel Budget Section does not break utility fees down by service type — it shows a single "Utility fees" line. This prevents the player from understanding why utility revenue is low when they have power plants but no water towers (or vice versa).

**Proposed resolution**: (a) Specify that coverage is evaluated at the start of each budget tick (before revenue is calculated), so a zone and service building placed in the same inter-tick window are both counted at the next tick. (b) Consider splitting the utility fees line in the Budget Section into "Power utility fees" and "Water utility fees" sub-lines. This is LOW priority for V1 but should be noted as a future improvement.

---

### H-07 — Traffic timeout trips classified as "extreme-travel-time" but the rolling average window definition excludes them
**Files**: `traffic-system.md` §Model (agent timeout and null-path sections)
**Type**: [INCONSISTENCY]

The spec says: "agents that have not reached their destination within 120 simulation seconds are despawned; their trip is logged as unserved and counted as an **extreme-travel-time trip** in the rolling demand average (equivalent to the maximum travel time for demand calculation purposes)."

Later: "**Null-path behavior**: When A* finds no valid path for a zone tile (empty road graph or disconnected tile), that tick contributes `null_path_demand_default = 0.5f`."

And: "**Timeout trips must NOT be classified as null-path trips**."

So timed-out trips contribute a demand_factor equivalent to the maximum travel time (60s for R, 65s for C, 80s for I) → demand_factor = 0. But the spec also says "Both C and I null-path behavior mirrors Residential: tiles with no valid path are excluded from the rolling average; if all ticks in the window have no valid path, `demand_factor` defaults to 0.5 (neutral)."

The contradiction: timed-out trips have demand_factor = 0 (extreme travel time), while null-path trips contribute 0.5. If a tile has high congestion that causes consistent agent timeouts, it will have effective demand_factor ≈ 0, meaning demand collapses to 0 — not the 0.5 null-path floor. This is intentional per the spec text. However: the definition says timeout trips contribute a value "equivalent to the maximum travel time for demand calculation purposes." For Residential the max travel time is T=60s → smoothstep returns 0.0. For Industrial the max is T=80s → smoothstep returns 0.0. So timed-out trips ARE included in the rolling average as 0.0 values. This means the "all ticks null-path → 0.5 floor" rule only applies when ALL ticks are null-path, not when some ticks are timeout (0.0). A rolling window with 3 null-path (0.5 each) and 2 timeouts (0.0 each) averages to (1.5 + 0.0) / 5 = 0.30, not 0.5. The spec's "if all ticks in the window have no valid path, demand_factor defaults to 0.5" statement appears to only fire when zero valid paths exist (a completely disconnected graph), not when congestion causes timeouts. This is technically self-consistent but the boundary is confusing and not explicitly documented.

**Proposed resolution**: Add a clarifying note: "Timeout trips (demand_factor = 0.0) are distinct from null-path ticks (demand_factor = 0.5). A window containing both null-path ticks and timeout ticks averages them together without special treatment; the 0.5 floor only applies when ALL ticks in the window are null-path. Mixed windows with any timeout entries will average below 0.5."

---

### H-08 — `sfx_zone_upgrade` has no defined trigger call site
**Files**: `service-coverage.md`, `economy-model.md`, `zoning-system.md`; `v1-audio-asset-manifest.md`
**Type**: [GAP]

The audio asset manifest includes `sfx_zone_upgrade` (SoundId 5) described as "Zone tile auto-upgraded to higher density tier; positive/rewarding tone." However, unlike all other SFX (which have dedicated "Phase 10 Audio Callbacks" subsections in their respective game-design spec files), `sfx_zone_upgrade` has NO call site definition. There is no spec text specifying:
- Which method triggers it (`populationTick()` when an upgrade is detected? `doDensityUnlockTick()`?)
- Whether it fires once per upgrading tile or once per upgrade wave
- Its `SoundPriority`
- Whether it is positional or non-positional (the asset manifest says `AL_SOURCE_RELATIVE = AL_TRUE`, non-positional)
- Which file is the authoritative call site (economy-model.md? zoning-system.md? population-density-growth.md?)

**Proposed resolution**: Add a "Phase 10 Audio Callbacks for Zone Upgrade Events" section to `zoning-system.md` (the authoritative density upgrade spec) specifying: "sfx_zone_upgrade fires once per tile that successfully upgrades density tier (not once per upgrade wave), from within `CitySimulation::doDensityUpgrade()`, `SoundPriority::NORMAL`, non-positional (`AL_SOURCE_RELATIVE = AL_TRUE`)."

---

### H-09 — Wages formula uses `total_C_I_tax_revenue` but tax revenue is post-deficit; circular dependency risk
**Files**: `economy-model.md` §Wages per budget tick
**Type**: [PROBLEM]

`wages_per_tick = total_C_I_tax_revenue × wage_fraction_of_revenue × employment_fill_rate`. This formula creates a potential circular dependency in the budget calculation: wages depend on C/I tax revenue, which depends on zone occupancy, which depends on desirability, which depends on service coverage, which depends on whether wages were paid (since insufficient wages could represent the model of workers leaving). However, more concretely: the order of operations within a single budget tick is unspecified. If wages are calculated on the gross C/I tax revenue before expenses are subtracted, they correctly represent a fraction of gross revenue. But if "total_C_I_tax_revenue" refers to the net C/I revenue after some prior deduction within the same tick, the formula could produce incorrect results.

Additionally, the formula says wages are 20% of C/I tax revenue. On a budget tick where C/I revenue is $10,000, wages = $2,000 (20% of $10,000). The budget surplus is calculated as `(monthly_revenue − total_monthly_expenses) / monthly_revenue`. Wages are an expense, and they scale directly with C/I revenue — meaning they can never push the city into deficit by themselves (they are always ≤ 20% of C/I revenue). However, wages plus road maintenance plus service upkeep CAN create a deficit if service upkeep is large relative to revenue. This is a balance observation rather than a bug, but the spec should clarify the calculation order.

**Proposed resolution**: Add to the economy model: "Budget tick calculation order: (1) compute gross tax revenue (all zones), (2) compute utility fees, (3) compute wages using the gross C/I tax revenue from step 1, (4) compute road maintenance, (5) compute service upkeep, (6) sum revenue (steps 1+2), sum expenses (steps 3+4+5), compute budget_surplus_pct." This order must be documented to ensure deterministic tick behavior across implementations.

---

### H-10 — Construction delay and zone overlay color interact without player feedback for "empty lot" vs. "under construction" state
**Files**: `zoning-system.md` §Construction Delay; `terrain-interaction.md` §Zone placement feedback
**Type**: [GAP]

When a zone tile is placed, `underConstruction = true` and no building mesh spawns. Only the zone color overlay is shown. The tile contributes 0 population and no revenue while under construction. There is no specified visual difference between a zone tile that is "under construction but eligible for a building when demand rises" and a zone tile where "demand never reaches 0.50 so the building never appears." Both display the same colored overlay with no building. The player has no way to know whether their empty lot is a patience problem (demand coming) or a design problem (wrong zone mix).

**Proposed resolution**: Add a visual differentiation spec: "A zone tile that has been under construction for 3+ budget ticks without a building spawning (demand never reached the construction threshold) MUST display an additional visual indicator — either an animated pulsing in the zone overlay or a '?' badge on the tile. The Query/Inspector panel for such a tile should show 'Under construction — waiting for demand' with the current effective_demand_factor vs. the required threshold."

---

### H-11 — Undo of service building placement does not specify coverage restoration timing
**Files**: `undo-system.md`, `service-coverage.md` §Placement Rules
**Type**: [GAP]

The undo system spec says undo covers "zone placement, road placement, demolition." `service-coverage.md` says `placeServiceBuilding` "Records an undo entry (expires at second budget tick after action)." So service building placement IS in the undo scope. However, when a service building is placed and then undone, the spec does not specify:
1. Whether coverage for the service building is immediately withdrawn on undo, or on the next budget tick.
2. Whether zones that went from uncovered to covered during the window (and received desirability recovery) lose that recovery on undo.
3. Whether the utility fees collected during the window (if a budget tick fired between placement and undo) are clawed back.

**Proposed resolution**: Add to `undo-system.md`: "Undoing service building placement immediately removes coverage (evaluated on the same frame as the undo, not deferred to the next budget tick). Utility fees and tax revenue collected during the undo window are NOT clawed back — the undo only affects spatial state (building removed, treasury refunded). Desirability gains from service coverage during the window are retained and will reverse organically over subsequent ticks as coverage is absent."

---

### H-12 — Terrain generator playability guarantee uses 50×50 minimum contiguous area but map supports 128×128 minimum; no test for realistic starting zone
**Files**: `terrain-interaction.md` §Map playability guarantee; `minimum-viable-simulation.md`
**Type**: [GAP]

The terrain spec guarantees "at least one contiguous region of flat-or-sub-15° tiles of minimum 50×50 tiles (2,500 tiles)." The spec justification is "enough for roads + 2 residential zones + 1 commercial zone + 1 power plant + 1 water tower." However this is severely undercalibrated relative to the actual V1 economy requirements:
- A standard opening layout requires 20 R-Low + 10 C-Low + 5 I-Low + roads connecting them.
- Medium density upgrade requires sustaining $50,000 monthly revenue — which requires far more than 35 zone tiles.
- Road network to connect zones on a 50×50 area is trivial, but there is no room for organic city growth.

On a Small map (128×128 = 16,384 tiles), a 50×50 guaranteed flat area represents 15% of the map. On a Large map (1024×1024 = 1,048,576 tiles), 50×50 is 0.24% of the map — essentially nothing. The guarantee was calibrated for early gameplay but provides no guarantee of long-term buildable terrain for reaching High-density tiers.

**Proposed resolution**: Scale the minimum contiguous area guarantee with map size: Small = 50×50 minimum; Medium = 100×100 minimum; Large = 200×200 minimum. This ensures all map sizes have a viable growth corridor to support High-density development. Update the terrain generator spec accordingly.

---

### H-13 — Zoning system demand curves produce R_demand = capacity (C/I_worker_capacity) at game start with no residents — unbounded at 0 residents
**Files**: `zoning-system.md` §Residential demand
**Type**: [PROBLEM]

`R_demand = clamp(total_C_I_worker_capacity / max(1, current_R_population), 0.0, 1.0)`. At game start with 0 R_population and N C/I worker slots: R_demand = N / max(1, 0) = N / 1 = N, clamped to 1.0. This is correct by design ("at game start before the bootstrap demand kicks in, current_R_population = 0 → R_demand = C/I capacity").

However, the spec notes this formula is only meaningful once C/I zones exist. At game start with only Residential zones placed (no C/I): R_demand = 0/max(1,0) = 0. The bootstrap subsidy of 0.50 raises `effective_demand_factor(R)` to 0.50 during ticks 0–5. But the spec says "the growth formula evaluates to max_density × 0 × (desirability/100) = 0" because `capacity_ratio_demand = R_demand = 0`. This means bootstrap does NOT help residential growth when only R zones are placed.

The issue is that `capacity_ratio_demand` for Residential is R_demand, and the effective_demand_factor is computed as: `effective_demand_factor = clamp(bootstrap + formula_demand_factor, 0, 1)` — but this bootstrap is the traffic-travel-time bootstrap, NOT the R_demand bootstrap. The bootstrap boosts travel-time connectivity signal but the growth formula uses `capacity_ratio_demand × (desirability/100)`, not `effective_demand_factor`. So the bootstrap subsidy CANNOT help R growth when R_demand = 0. The zoning-system clarification block notes this explicitly. But it means bootstrap is redundant for the most common new-player mistake (placing all Residential first) — the player sees their zone tiles sit empty for multiple ticks with no explanation.

**Proposed resolution**: This is a design balance issue, not a contradiction. However, the expected player failure mode (all-R start → nothing grows) is not surfaced anywhere in the UX. The Inspector panel should display R_demand explicitly so the player understands WHY growth is zero. Add to the Query/Inspector spec: "For Residential tiles, the Inspector panel MUST show 'Job availability: X%' where X = R_demand × 100, so the player can diagnose zero-growth caused by no C/I zones." Currently `demand_pressure_pct` is shown but its inverse semantics make it counterintuitive for diagnosis.

---

### H-14 — Save system does not specify `underConstruction` and upgrade retry fields in the serialization schema
**Files**: `save-system.md`, `zoning-system.md` §Construction Delay, §Density Upgrade Resolution
**Type**: [GAP]

`zoning-system.md` §Construction Delay: "`underConstruction` is tracked as a per-tile boolean field in `CitySimulation` and must be serialised in the save-file tile struct so that in-progress construction survives save/load cycles."

`zoning-system.md` §Density Upgrade Resolution: "`upgradeRetryCount` is tracked per tile in a `std::unordered_map<TileKey, int>` on `CitySimulation`." and "`upgradeBlocked` is tracked per tile in a `std::unordered_map<TileKey, bool>` on `CitySimulation`."

`service-coverage.md` §Per-Tile Audio Transition Fields: "`wasPowered`", "`wasWaterCovered`", "`alertFired`" must be serialized.

The save system spec (`save-system.md`) only enumerates a few named fields that must be serialized (`population_milestone_fired`, `speed_multiplier`, `building_variant_counters`). It does not provide a comprehensive tile-struct schema that includes all the per-tile fields that various specs require to be serialized: `underConstruction`, `upgradeRetryCount`, `upgradeBlocked`, `wasPowered`, `wasWaterCovered`, `alertFired`.

**Proposed resolution**: Add a "V1 Save File Schema — Per-Tile Fields" section to `save-system.md` (or cross-reference to a dedicated schema file) listing every per-tile boolean and counter that must be serialized. This is required to prevent silent data loss on save/load and to ensure Phase 11 implementers have a complete checklist.

---

## MEDIUM Issues

---

### M-01 — `sfx_zone_upgrade` SoundPriority and positional vs. non-positional specification conflicts with asset manifest
**Files**: `v1-audio-asset-manifest.md`, `zoning-system.md` (absent)
**Type**: [GAP]

The asset manifest says `sfx_zone_upgrade` is non-positional (`AL_SOURCE_RELATIVE = AL_TRUE`). However, during a density upgrade wave 20% of tiles can upgrade per tick — potentially firing many non-positional `sfx_zone_upgrade` events in rapid succession, creating an audio pile-up. No per-event throttle, cooldown, or stacking rule is defined for this sound.
**Proposed resolution**: Add a per-tick cap: "At most one `sfx_zone_upgrade` plays per budget tick, even if multiple tiles upgrade simultaneously."

---

### M-02 — Difficulty scaling constants are only defined for density unlock thresholds, but no other economic mechanic uses difficulty scaling
**Files**: `economy-model.md` §Difficulty-scaled unlock thresholds
**Type**: [GAP]

Difficulty affects: starting funds (Easy=$1M / Normal=$500K / Hard=$200K) and density unlock thresholds (0.70×/1.00×/1.50×). No other mechanics scale with difficulty. Emergency Municipal Bond uses (Easy: 3, Normal: 2, Hard: 1) uses, which is effectively difficulty scaling. But road costs, upkeep, service placement costs, and tax rate bounds are identical across all difficulties. The spec never states whether this is intentional. On Hard ($200K start), placing one Power Plant ($10K), one Fire Station ($5K), one Police Station ($4K), one Water Tower ($3K), and 20 road tiles ($10K) costs $32K — leaving $168K. But upkeep after 120s grace period is $500 + $400 + $1,000 + $300 = $2,200/tick for services + road maintenance. On Hard, with the 1.50× density unlock threshold, the revenue needed to reach Medium is $75K/month, which at low density requires far more zone tiles than the starting funds can support. This may be intentionally punishing but creates a potential impossibility scenario on Hard with a minimum opening that has no viable path to Medium density.
**Proposed resolution**: Document the intended Hard mode solvency path explicitly. Either reduce service upkeep on Easy difficulty, or document that Hard mode is intended to require perfect play and confirm that a viable recovery path exists with worked example.

---

### M-03 — `demand_bootstrapping_ticks = 6` constant is not defined in `simulation_constants.h` listing
**Files**: `zoning-system.md` §Demand bootstrapping; `simulation-time.md` §Phase 1 sign-off constants table
**Type**: [GAP]

The bootstrap period lasts 6 budget ticks (`demand_bootstrapping_ticks`). This constant is referenced throughout `zoning-system.md` but is NOT listed in the `simulation_constants.h` verification table in `simulation-time.md`, and the constant is not given an explicit `SimulationConstants` name in a canonical definition block. The sign-off table in `simulation-time.md` lists Part A constants but not `demand_bootstrapping_ticks`. The oscillation derivation in `zoning-system.md` uses "demand_bootstrapping_ticks = 6" inline.
**Proposed resolution**: Add `SimulationConstants::demand_bootstrapping_ticks = 6` to the `simulation_constants.h` canonical constants list and verify it in Phase 1 sign-off. All inline uses of `6` in the bootstrap formulas should reference this constant.

---

### M-04 — Traffic system intersection signal cycle duration not defined
**Files**: `traffic-system.md` §Intersections
**Type**: [GAP]

The spec defines intersections track `IntersectionSignalState` structs and emit signals on phase transitions, and that signals have "configurable green/red durations." However, the default green/red cycle duration, the unit (simulation seconds), and the configurable range are never specified. Without a default, the game cannot start with intersections functioning. The spec does not define how many simulation seconds constitute a full signal cycle, or how the cycle interacts with traffic agents waiting at the red phase.
**Proposed resolution**: Define: default green phase = 15 simulation seconds, default red phase = 15 simulation seconds (total cycle = 30s, same duration as one budget tick for easy mental modeling). Configurable range: 5–60 simulation seconds per phase. Define how waiting agents are handled: they pause movement on their current tile until the signal clears.

---

### M-05 — Population density growth cap interacts unexpectedly with density upgrades
**Files**: `population-density-growth.md` §Growth formula per tile
**Type**: [PROBLEM]

Growth cap: `+10% of max_density_for_tier per tick`. When a tile upgrades from Low (max=100) to Medium (max=400), the growth cap changes from 10 residents/tick to 40 residents/tick. On the tick of the upgrade, the tile's current population might be, say, 85 residents (near-full Low density). After the upgrade to Medium (max=400), the current population is still 85, and the tile now needs to grow to 400. The demand_factor and desirability targets an intermediate value (e.g., target = 400 × 0.8 × 0.7 = 224). Growth per tick is capped at 40 residents. This is functional but the spec does not address whether the population is re-scaled or preserved across a density upgrade. If the current population is 85 out of 400 (21%), the tile contributes much less tax revenue post-upgrade than pre-upgrade when it was at 85/100 = 85% occupancy. Revenue could temporarily drop post-upgrade — the spec does not warn the player about this.
**Proposed resolution**: Add a note to `population-density-growth.md`: "When a tile density-upgrades, its current population is preserved (not scaled). This means a near-full Low-density tile (e.g., 90/100 = 90% occupancy) drops to low occupancy percentage post-upgrade (90/400 = 22.5%), temporarily reducing tax revenue until population grows. The density-upgrade progress toast should warn the player: 'Upgrading to Medium density — expect temporary revenue dip as population catches up.'"

---

### M-06 — `getConsecutiveDeficitMonths()` returns 0 during grace period but grace period ends mid-session; transition behavior unspecified
**Files**: `game-over-flow.md` §ICitySimulation interface
**Type**: [GAP]

"This method returns 0 during the grace period." If the city is in a ≥ −25% deficit during the grace period, the forced loan cannot trigger (120s gate). After 120s the grace period ends, the forced loan triggers on the next deficit tick, and if the deficit is ≥ −50% immediately (e.g., player placed expensive upkeep buildings with no revenue), the `getConsecutiveDeficitMonths()` counter starts at 0 but the deficit may have been ≥ −50% for several ticks during the grace period. The streak counter could realistically reach 3 within 3 ticks post-grace-period (≈3 months at 1×), triggering game-over very quickly.

The spec says "The grace-period return value of 0 is not a special override: it reflects the fact that deficit consequence evaluation is suppressed during the grace period, so the counter is never incremented." This means the grace period acts as a clean slate for the streak counter. However this creates a cliff: immediately after grace period ends, a city in continuous deep deficit begins a fresh 3-month countdown. The player has already been in trouble for 120+ real seconds with no warning (because deficit warnings require `m_firstRevenueTicked` and the streak counter to be non-zero). The transition from 0 deficit months to "2 months until bankruptcy" has no context for the player.

**Proposed resolution**: Add: "Immediately when the grace period ends (`IClock::nowSeconds() - m_startTime >= 120s`) and the simulation is in a ≥ −10% deficit state, the `BudgetDeficitWarn` notification fires on the next budget tick even if the deficit has been present throughout the grace period. This gives the player context-aware feedback at the grace period boundary."

---

### M-07 — Power plant BFS brownout "farthest 30% of covered nodes" is non-deterministic for nodes at the same BFS depth
**Files**: `service-coverage.md` §Budget deficit degradation (brownout model)
**Type**: [PROBLEM]

"Farthest 30% of covered nodes by BFS depth become uncovered." If there are 100 covered nodes and BFS depth distribution is: depth-1: 30 nodes, depth-2: 50 nodes, depth-3: 20 nodes — the farthest 30% are the 20 depth-3 nodes plus 10 depth-2 nodes. Which 10 of the 50 depth-2 nodes are uncovered? The spec says "always means highest BFS depth from the power plant — not a random selection," but when there are more nodes at the cutoff depth than needed to reach exactly 30%, the selection within that depth tier is unspecified. This could produce non-deterministic coverage patterns.
**Proposed resolution**: Specify tie-breaking: "When the 30% threshold falls within a depth tier, prioritize uncovering tiles with the highest tile index (tileZ * mapWidth + tileX) until the 30% threshold is met. This produces a deterministic, reproducible brownout pattern."

---

### M-08 — `sfx_fire_alert` and `sfx_police_alert` both fire from desirability threshold but only one fires per tile — priority rule exists but creates silent police event when fire covers the same tile
**Files**: `service-coverage.md` §Phase 10 Audio Callbacks — sfx_fire_alert and sfx_police_alert
**Type**: [PROBLEM]

"Fire Station takes priority when both station types cover the tile." This means a tile covered by both Fire and Police stations that crosses the alert threshold fires only `SFX_FIRE_ALERT`. The Police alert is silenced even though the Police station has a genuine service event. Since both services are physically present, the missing Police alert is an invisible service failure.

More critically: `tile.alertFired` is a single boolean. When the tile recovers and desirability rises above the threshold, `alertFired` is reset. On the next decline, `SFX_FIRE_ALERT` fires again (Fire takes priority). In a scenario where only the Police station was degraded (fire still at full coverage), the Police alert never fires because `tile.alertFired` is permanently claimed by Fire. The player receives no indication of a Police-specific service crisis.

**Proposed resolution**: Use two separate `alertFired` booleans — `fireAlertFired` and `policeAlertFired` — one per station type. Each resets independently when desirability recovers. Both can fire simultaneously on the same tile. Remove the priority rule — fire both alerts when both coverage types exist and the tile crosses the threshold.

---

### M-09 — `loan_repayment_ticks = 12` and `bond_repayment_ticks = 24` but `ticks_per_year = 12` — mismatch in repayment period framing
**Files**: `economy-model.md` §Loan mechanic; `simulation-time.md`
**Type**: [PROBLEM]

The spec states: "Loan principal is repaid over `loan_repayment_ticks = 12` budget ticks (1 in-game year)." and "Bond principal is repaid over `bond_repayment_ticks = 24` budget ticks (2 in-game years)." With `ticks_per_year = 12`, this is internally consistent. However, the interest formula is: `interest_per_tick = outstanding_debt × (0.05 / ticks_per_year)`. This computes 5%/year in per-tick installments. For a $10,000 loan over 12 ticks: total interest = 12 × $10,000 × (0.05/12) = $500. Per-tick interest on the initial balance is $41.67.

However, the spec says "The outstanding debt on which interest is calculated decreases by `repayment_principal_per_tick` each tick after interest is applied." This means interest is calculated on the declining balance. In tick 1: interest = $10,000 × (0.05/12) = $41.67. In tick 2: principal remaining = $10,000 - $833 = $9,167; interest = $9,167 × (0.05/12) = $38.20. Total interest over life of loan = approximately $271 (declining balance), not $500 (flat balance). The spec says "simple interest" but the declining-balance implementation is technically amortized (not simple interest). This discrepancy in terminology could cause test failures if tests assume flat-balance simple interest.

**Proposed resolution**: Either clarify that it IS amortized (declining balance interest) and remove "simple interest" from the description, or switch to flat-balance (calculate interest on original principal each tick). Specify which one is intended, and update the `Economy_InvariantSatisfied_LoanInterestAccrual` test comment accordingly.

---

### M-10 — City Rating tier table does not include starting state (0 population)
**Files**: `game-progression-modes.md` §City Rating tiers
**Type**: [GAP]

The City Rating table starts at "Village: 0–999." A brand new city with 0 population is technically a Village. However, the HUD displays "City Rating label (`getCityRating()` → `CityRatingTier` display name)." At 0 population the HUD shows "Village" — which may feel dismissive to the player who just started. The spec does not define whether the initial rating is displayed before any zones are placed. Also, `stinger_milestone` fires at Village→Town (1K population), but the player starts as a Village — meaning the stinger fires on the first meaningful milestone. This is correct design but should be explicitly confirmed.

**Proposed resolution**: Add: "The City Rating is 'Village' from game start until 1,000 population is reached. The HUD always displays the current tier, including 'Village' for new cities. The first stinger milestone fires at 1,000 population (Village→Town transition)."

---

### M-11 — Service coverage `service_level_pct` formula denominator anchors to placement time but building can be demolished and re-placed
**Files**: `service-coverage.md` §Service level %
**Type**: [PROBLEM]

"The denominator is computed once at placement time and cached on the building entity." If a player demolishes and re-places a service building on the same tile (or nearby), the cached denominator is recomputed. This is correct for re-placement. However, if the terrain or surrounding buildable tiles change between placements (Phase 10b allows terrain modification), the denominator at re-placement may differ from the original placement. This creates inconsistency in `service_level_pct` displayed in the Inspector panel if the player queries the building before and after terrain modification. The spec does not define whether `service_level_pct` is re-evaluated when terrain changes affect the buildable-tile count within the service radius.

**Proposed resolution**: Add: "The `total_buildable_tiles_in_range` denominator is recomputed whenever the service building is placed or when `ITerrainQuery::setTileHeight()` modifies tiles within the service building's coverage radius. If a terrain modification reduces the buildable tile count, the Inspector panel immediately reflects the updated `service_level_pct`."

---

### M-12 — "Auto-save pauses while game is paused" contradicts auto-save on forced loan dialog trigger
**Files**: `settings-pause-menu.md` §Auto-save, `save-system.md` §Auto-save triggers, `economy-model.md` §Early-game grace period
**Type**: [INCONSISTENCY]

`settings-pause-menu.md`: "The simulation auto-saves silently every 120 real seconds... (the timer pauses when the game is paused or a blocking modal is active)."

`save-system.md`: "auto-save triggers **immediately when the forced loan dialog becomes active** (before the modal is shown)."

The forced loan dialog IS a blocking modal. The settings spec says the auto-save timer pauses when a blocking modal is active. But the save-system spec says auto-save fires before the modal is shown. These are not strictly contradictory (auto-save fires once when the modal BECOMES active, then the timer pauses while the modal IS active), but the ordering could be misinterpreted. The phrase "before the modal is shown" in save-system.md implies auto-save runs when the loan condition is detected, THEN the modal opens, THEN the timer pauses. This sequence is correct but should be explicitly stated.

**Proposed resolution**: Add to `save-system.md`: "The forced-loan auto-save triggers on the same tick that the deficit condition is detected, before `UIManager::showModal()` is called. This means the auto-save timer fires once, saves, and then is suspended for the duration of the modal. The 120-second timer resets after the modal is dismissed."

---

## LOW Issues

---

### L-01 — `SimulationConstants` names for service alert threshold never declared formally
**Files**: `service-coverage.md` §sfx_fire_alert and sfx_police_alert
**Type**: [GAP]

`service_alert_desirability_threshold = 20` is defined inline in the audio callbacks section but is not included in the `SimulationConstants` mapping table in `population-density-growth.md` or any central constants listing. The service-coverage spec adds it as a `static constexpr int` but it is not cross-referenced in the Phase 1 sign-off table in `simulation-time.md`.
**Proposed resolution**: Add `service_alert_desirability_threshold` to the Phase 1 sign-off constants table.

---

### L-02 — Dynamic soundscape does not specify zone-loop culling radius relative to population density
**Files**: `dynamic-soundscape.md` §City zone layer
**Type**: [GAP]

"Per-zone positional loops (R/C/I) culled beyond 300 m; max 16 simultaneous zone sources." The culling is by distance only. There is no specification of how zone loops scale with population — a fully occupied High-density block vs. an empty Low-density lot should presumably differ in zone loop volume or playback presence. Currently, both produce the same zone loop at 100% volume within 300m.
**Proposed resolution**: Add: "Zone loop gain scales with tile occupancy: `gain = 0.3 + 0.7 × (actual_population / max_density_for_tier)`. An empty lot plays at 30% gain; a fully occupied tile plays at 100% gain."

---

### L-03 — `zoning-system.md` inline sign-off blocks (Bootstrap Oscillation Gate — Round 1) are spec content intermixed with process artifacts
**Files**: `zoning-system.md` §Bootstrap Oscillation Gate Round 1 Review Sign-Off
**Type**: [DUPLICATE]

The detailed sign-off tables and numeric derivations (approximately 50 lines) within the Bootstrap Oscillation Gate section are implementation-verification artifacts embedded in the canonical spec. This data is not useful as ongoing spec content — it was a one-time verification. Having it inline in the spec file makes the file significantly harder to read and may cause future reviewers to treat the numeric derivation as part of the active spec rather than a historical verification record.
**Proposed resolution**: Move the sign-off derivation tables to a separate `architecture/game-design/zoning-bootstrap-signoff.md` file or to the relevant `implementation/` phase file, and replace with a single line in `zoning-system.md`: "Bootstrap oscillation gate: PASSED (see implementation/phase-6.md for derivation)."

---

### L-04 — No spec for what happens when `upgradeBlocked` is cleared by a blocking tile that is demolished and IMMEDIATELY re-placed
**Files**: `zoning-system.md` §Density Upgrade Resolution
**Type**: [GAP]

"`upgradeBlocked` flag is cleared... when a tile in the previously expanded footprint that was a road, different zone type, service building, or out-of-bounds boundary is removed." If the player demolishes a blocking road and immediately re-places it in the same game tick (before `populationTick()` runs), `upgradeBlocked` would clear then immediately re-block. This edge case is trivial at budget-tick granularity but could produce confusing behavior if demolish and re-place happen in the same inter-tick window.
**Proposed resolution**: Clarify: "`upgradeBlocked` clearing is evaluated at the start of `populationTick()`, not at demolish time. If a blocking tile is re-placed before the next `populationTick()`, it is still considered a blocked tile at tick evaluation time."

---

### L-05 — No defined behavior for the Sandbox post-Megalopolis progression
**Files**: `game-progression-modes.md` §City Rating tiers
**Type**: [GAP]

The City Rating progression ends at Megalopolis (500K+). The spec states "This replaces the need for a formal win condition in Sandbox mode." But there is no defined behavior once Megalopolis is reached and the city continues to grow past 500K. Is Megalopolis the terminal state forever? Does the HUD show population continuing to climb with the same "Megalopolis" label? The stinger fires once at 500K. No further long-term goals are defined.
**Proposed resolution**: Explicitly state: "Megalopolis is the terminal City Rating tier. No further rating changes occur above 500K population. The HUD continues to display population count and 'Megalopolis' label indefinitely. No additional stingers or milestone toasts fire above 500K." If a V2+ progression layer is planned, note it as post-V1.

---

### L-06 — `base_income_per_resident_low = $50` and `base_income_per_resident_medium = $50` equal values — confusingly named
**Files**: `economy-model.md` §Tax revenue formula
**Type**: [PROBLEM]

The spec explicitly notes "this non-obvious equality is intentional" but still defines two separate `SimulationConstants` names for what is the same value. This doubles the constant space for no functional benefit. If both are $50, using either constant name in code produces the same result, but having two names suggests they could diverge — making maintenance harder. Any future change to Medium density income must update two constants.
**Proposed resolution**: Keep both named constants (for API clarity at call sites) but add inline code comment: "These constants are intentionally equal in V1 — see economy-model.md for rationale. Do not unify into a single constant as they may diverge in a future rebalance pass."

---

### L-07 — Minimap traffic congestion overlay threshold boundary is ambiguous (≤30% vs 31-39% vs ≥40%)
**Files**: `minimap.md` §Traffic Congestion overlay; `traffic-system.md` §Congestion threshold
**Type**: [DUPLICATE] / minor [INCONSISTENCY]

`minimap.md` speed bands: "≥ 40% — Green, 31–39% — Orange, ≤ 30% — Red." `traffic-system.md` penalties: "Average segment speed 31–39% of max → −10%, 21–30% → −18%, ≤ 20% → −25%." The congestion penalty thresholds use 31–39% and 21–30% as tiers, but the minimap only uses two penalty bands (31–39% and ≤30%). This means the minimap shows all ≤30% roads as Red regardless of whether they are at 25% (−18% penalty) or ≤20% (−25% penalty). The minimap overlay cannot convey the worst-case congestion band. There is also a potential off-by-one: traffic-system has "speed ≤ 20%" for the worst tier but the minimap groups "≤ 30%" as all Red. A road at 25% speed is shown as "moderate-heavy" on the minimap but incurs only the −18% penalty. This is a minor inconsistency in how severe the Red band appears vs. what it costs.
**Proposed resolution**: Either align the minimap to three penalty tiers (≥40% Green, 21–39% Orange, ≤20% Red) matching the traffic-system bands, or explicitly document that the minimap uses a simplified 3-tier display that intentionally collapses the two worse penalty tiers into one visual band.

---

## Cross-Cutting Observations

### OBS-01 — File length and density make spec navigation difficult
Several spec files (`economy-model.md`, `zoning-system.md`, `traffic-system.md`) pack extensive content into single dense paragraphs or single bullet points that run hundreds of words. This makes cross-referencing difficult and increases the chance of implementers missing a clause. The files are not spec issues per se but represent a maintainability risk. Recommend adding heading-level sections within these files for major mechanics (e.g., separating the loan mechanic, deficit consequences, and grace period into their own `##` sections in `economy-model.md`).

### OBS-02 — No spec for difficulty-scaling of starting tax rates
Easy/Normal/Hard differ in starting funds and density unlock thresholds. The default tax rate is 10% for all difficulties. On Hard with $200K starting funds, service upkeep alone ($2,200/tick) requires approximately $22,000/month in revenue before expenses to break even — achievable with a small zone layout but tight. The spec is silent on whether the default tax rate or the grace period duration should also scale with difficulty. This is a balance gap.

### OBS-03 — Audio stinger `stinger_crisis` has no cooldown or de-escalation trigger defined
Once the crisis music tier is active, there is no spec for when (or if) it exits. The dynamic soundscape spec defines intensity tier escalation (calm → growth → crisis) but not de-escalation conditions. Combined with issue C-05 (intensity tier conditions undefined), this means crisis music could play indefinitely with no exit condition. The stinger fires once on crisis entry but the ongoing crisis music has no off-ramp.

---

*End of review. Total issues: 8 CRITICAL, 14 HIGH, 12 MEDIUM, 7 LOW, 3 Observations.*
