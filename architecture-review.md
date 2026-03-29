# AI Town — Architecture Specification Review

**Review Date:** March 29, 2026  
**Reviewers:** Design Squad & Technical Squad  
**Scope:** All 57 architecture specification files  
**Policy:** Read-only review — no spec files were modified

---

## Table of Contents

1. [Game Design Review](#game-design)
2. [UI / UX Review](#ui---ux)
3. [3D Asset Standards Review](#3d-asset-standards)
4. [2D Texture Standards Review](#2d-texture-standards)
5. [Audio Design Review](#audio-design)
6. [Audio Architecture Review](#audio-architecture)
7. [Graphics Architecture Review](#graphics-architecture)
8. [Testing Architecture Review](#testing-architecture)
9. [CI/CD Pipeline Review](#ci-cd-pipeline)
10. [Cross-Domain Technical Review](#cross-domain-technical)

---

## 1. Game Design Review

**Reviewer:** Senior Game Designer

---

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


---

## 2. UI / UX Review

**Reviewer:** Senior UI/UX Designer

---

# AI Town UI/UX Architecture Review

**Reviewer**: Senior UI/UX Designer
**Date**: 2026-03-29
**Scope**: All files under `architecture/ui-ux/`, cross-referenced with `architecture/game-design/` and `architecture/graphics-architecture/irrlicht-device-lifecycle.md`

---

## Table of Contents

1. [File-by-File Findings](#file-by-file-findings)
2. [Cross-File Issues](#cross-file-issues)
3. [Missing Specs](#missing-specs)
4. [Summary Table](#summary-table)

---

## File-by-File Findings

---

### `camera-controls.md`

**CC-1** [GAP] MEDIUM
The spec states that `kKeyboardPanRate` is a named constant that allows Arrow-key pan to be tuned independently, but it does not appear in the `ui_constants.h` constant listing in `ui-manager.md`. The constant is described textually but never formally listed in the toolbar-constants block. If Arrow-key pan speed is tunable at implementation time and the constant lives in `CameraController.h`, the spec should state that explicitly. Currently there is no canonical home declared for `kKeyboardPanRate`, `kBasePanSpeed`, `kDefaultZoomDistance`, `kMinZoomDistance`, or `kMaxZoomDistance`.
**Proposed resolution**: Add a section to `camera-controls.md` explicitly stating these five constants are `constexpr` in `src/ui/CameraController.h` (not in `ui_constants.h`), and list their purpose alongside the constraint that they are tunable at implementation time with no prescribed values.

---

**CC-2** [GAP] MEDIUM
The mouse-sensitivity slider is described as applying to "MMB drag and edge-scroll" pan inputs only, and explicitly not to Arrow-key pan. However, there is no spec for whether the sensitivity slider also applies to RMB drag (camera rotate/pitch). RMB drag uses physical pixel delta by design (drag-delta coordinate space section), but the sensitivity multiplier relationship to RMB rotate speed is never stated.
**Proposed resolution**: Add a sentence explicitly stating whether `sensitivityMultiplier` applies to RMB drag pan/rotate or whether RMB rotate speed has a separate constant.

---

**CC-3** [GAP] LOW
The spec defines edge-scroll activation as a 20 px band in virtual 1920×1080 space but does not specify what happens when the application window is not 16:9 (e.g., ultrawide 21:9). The UIScaler letterbox/pillarbox model means that mouse coordinates in the black bars are clamped to virtual edge values, which could cause spurious edge-scrolling in the bars. There is no explicit statement that edge-scroll should be suppressed when the mouse is in the letterbox/pillarbox region.
**Proposed resolution**: Add a note that edge-scroll fires only when the physical mouse coordinate is within the active viewport (not in a letterbox/pillarbox bar), cross-referenced with `UIScaler::getViewportRect()`.

---

**CC-4** [GAP] LOW
The spec describes the `CameraController` constructor as accepting a `bool startInFullscreen` parameter but does not specify the full constructor signature (parameters, whether `UIScaler*` or another type is passed). `resolution-ui-scaling.md` defines `UIScaler` construction but never references `CameraController`'s dependency on it for edge-scroll threshold evaluation.
**Proposed resolution**: Add the full `CameraController` constructor signature with all parameters so it can be locked before implementation.

---

### `finances-panel.md`

**FP-1** [GAP] MEDIUM
The spec says the panel is "horizontally centered" at 360×520 px below the resource bar, but no explicit `y` anchor is given for the panel's top edge. "Below the resource bar" is ambiguous — the resource bar occupies y: 0–56 px; does the panel start at y: 56, y: 64, or some other value? This is needed to verify that the panel does not overlap the left toolbar (which starts at x: 8) and that its bottom edge (y_top + 520) does not exceed virtual 1080 px.
**Proposed resolution**: Add explicit virtual-space bounding box coordinates: `panel_x = (1920 - 360) / 2 = 780`, `panel_y = 64` (or chosen value), `w = 360`, `h = 520`.

---

**FP-2** [GAP] LOW
The "key-repeat cap" of ±5 percentage points per continuous hold is defined for the +/- buttons, but the interaction between the cap and direct text entry is not specified. If a user holds + to reach the cap (+5 ppt), then immediately uses direct entry to type a new value, is the cap reset? The spec only mentions "releasing and re-pressing resets the cap" for the hold path; it is silent on whether direct-entry clears the hold-cap state.
**Proposed resolution**: Add a sentence clarifying that activating the direct-entry text field unconditionally resets the hold-cap accumulator.

---

**FP-3** [INCONSISTENCY] LOW
The Budget Section color for surplus is specified as `SColor(255, 128, 200, 80)` = `#80C850` (green). The Glass City canonical color palette in `resolution-ui-scaling.md` does not include this green — the palette defines `#F04E37` (error red), `#F0B429` (amber), `#EBF4F6` (near-white), `#4A7FA5` (mid-blue), `#E8960C` (warning amber), and `#00C9C8` (teal). A surplus-green is not in the locked palette table. This could cause inconsistency when other panels need to display a positive/surplus state.
**Proposed resolution**: Add `#80C850` surplus-green to the Glass City canonical palette table in `resolution-ui-scaling.md`, or substitute it with an existing palette token and document the substitution in `finances-panel.md`.

---

**FP-4** [GAP] LOW
The spec says the Finances Panel receives `IAudioSystem*` in its constructor, matching the HUD pattern. However, it does not specify whether `FinancesPanel` forward-declares `IAudioSystem` in its header (keeping UI headers free of audio headers, matching the `NotificationManager` pattern). This is a code-structure concern with direct UX testing implications (the `MockAudioSystem` injection pattern for panel tests).
**Proposed resolution**: Add an explicit note mirroring the `NotificationManager.h` pattern: `IAudioSystem` is forward-declared in `FinancesPanel.h`; the full include is in `FinancesPanel.cpp` only.

---

### `hotkey-scheme.md`

**HS-1** [PROBLEM] HIGH
The Pause/Unpause key (Space) and the speed controls (+/= and -) are listed as "system-reserved" and do not appear in the rebinding table. However, `input-arbitration.md` does not include Space in the Escape routing rules section, and there is no explicit specification for what priority in the 6-level arbitration chain intercepts Space. If a blocking modal is active, can the player still press Space? The modal section of `input-arbitration.md` says "All keyboard events... ARE consumed by the modal" — but the `notification-system.md` CRITICAL-toast auto-pause section explicitly says the speed selector remains enabled during CRITICAL-toast pause (not modal pause), implying Space must reach `UIManager` during CRITICAL-toast state. The exact arbitration path for Space is never stated.
**Proposed resolution**: Add Space to the `input-arbitration.md` Escape routing rules section, specifying at which priority level it is processed and whether it is consumed by blocking modals.

---

**HS-2** [GAP] MEDIUM
The hotkey table lists `+ / =` for speed increase and `-` for speed decrease. These are specified as "system-reserved" and not rebindable, but the spec never addresses the numpad variants (Numpad+ and Numpad-). On keyboards where the main `+` key requires Shift, players may expect numpad speed controls to work. The spec is silent.
**Proposed resolution**: Either explicitly include or exclude numpad variants in the non-rebindable speed control spec.

---

**HS-3** [GAP] LOW
The conflict detection flow describes two resolution choices (Swap / Cancel) but does not specify what happens when the player attempts to swap a rebindable action with a non-rebindable informational row (e.g., if Ctrl+Z or Ctrl+S somehow appears as a conflict target). The conflict detection spec says "If a conflict is found" but Ctrl+Z and Ctrl+S use a chord format (`"Ctrl+KeyZ"`) while single-key actions use bare names — it is unclear whether the chord validator correctly excludes modifier chords from appearing as single-key conflicts.
**Proposed resolution**: Add a sentence to the conflict detection spec explicitly stating that Ctrl-chord bindings (Ctrl+Z, Ctrl+S) are never flagged as conflicts with single-key bindings and cannot be swap targets.

---

**HS-4** [GAP] LOW
The WASD preset confirmation modal spec (cross-referenced to `modal-dialog-system.md`) is thorough, but `hotkey-scheme.md` itself does not specify what happens if the player has already manually rebound some of the WASD keys before clicking the preset button. For example, if the player has already rebound W to Zone and D to Road, the modal says "current binding of each affected key" — but those custom bindings would be overwritten by the preset. The modal body text ("Any custom bindings for W/A/S/D will be overwritten") covers this, but the `hotkey-scheme.md` atomic rebinding description does not restate this behavior, creating a potential implementation gap.
**Proposed resolution**: Cross-reference the atomic rebinding note in `hotkey-scheme.md` to confirm that the preset applies regardless of current W/A/S/D bindings.

---

### `hud-layout.md`

**HL-1** [GAP] HIGH
The Density Unlock Preview Tooltip is mentioned in `economy-model.md` ("HUD shows a preview: when monthly revenue is within 10% of an unlock threshold, a projected 'After Unlock' estimated monthly expense change is shown in the resource bar tooltip") but there is no corresponding spec in `hud-layout.md`. The HUD layout file describes the resource bar content (treasury, debt, city rating, population, date) but contains no definition of the density unlock progress indicator element, its virtual bounds, the 10% proximity trigger, or the tooltip content format. The `economy-model.md` `getNextUnlockThreshold()` section cross-references `hud-layout.md §Density Unlock Preview Tooltip` — but that section does not exist.
**Proposed resolution**: Add a `Density Unlock Progress Indicator` and `Density Unlock Preview Tooltip` subsection to `hud-layout.md` with virtual bounds, trigger condition (≥10% proximity), tooltip content (threshold value, current revenue, projected expense change), and the sentinel-suppression rule when `getNextUnlockThreshold()` returns `-1.0f`.

---

**HL-2** [GAP] HIGH
The resource bar (y: 0–56 px) contains treasury, debt indicator, City Rating, population, and in-game date. The `economy-model.md` states wages are a "visible budget line item in the HUD resource bar". However, `hud-layout.md` does not include wages as a resource bar element. This is a direct contradiction: the economy spec promises a HUD element that the HUD layout spec does not define. Whether wages appear in the resource bar or only in the Finances Panel Budget Section is unresolved.
**Proposed resolution**: Clarify in `hud-layout.md` whether wages appear as a resource bar element (and if so, add virtual bounds and update logic) or only in the Finances Panel Budget Section (and if so, update `economy-model.md` to remove the "HUD resource bar" reference).

---

**HL-3** [GAP] MEDIUM
The Utilities sub-panel in `hud-layout.md` specifies a 4×1 layout (`width = (64×4)+(4×3) = 268 px`) but the `ui_constants.h` block in `ui-manager.md` shows `kUtilSubPanelWidth = 196` and `kUtilSubBtnW = 96`, with a comment "2×2 button grid: 2 columns × 2 rows". This is a direct numerical contradiction: `hud-layout.md` says 4 buttons in a single horizontal row; `ui-manager.md` says a 2×2 grid of 96×48 px buttons. The total widths differ (268 vs. 196). One of these is wrong.
**Proposed resolution**: Determine the canonical layout (single-row 4 buttons at 64 px each, or 2×2 grid of 96×48 px buttons) and update whichever file is incorrect. This is a CRITICAL implementation conflict that will produce a broken sub-panel.

---

**HL-4** [INCONSISTENCY] MEDIUM
The Utilities sub-panel visibility is anchored at y:64 (same as Zone sub-panel) per `hud-layout.md`. The `ui-manager.md` `kUtilSubPanelTop = 176` (aligned with Utilities button row at y:176). These are contradictory top-y values (64 vs. 176). The comment in `ui-manager.md` says "aligned with Utilities button row" which makes functional sense, but the `hud-layout.md` text says the Utilities sub-panel shares "the same top anchor (y:64) as the Zone sub-panel."
**Proposed resolution**: Determine the correct top anchor and update both files. The `y:176` value in `ui-manager.md` (aligned with the Utilities toolbar button) is almost certainly more correct ergonomically. Update `hud-layout.md` to match.

---

**HL-5** [GAP] MEDIUM
The in-game date/time display is listed as a resource bar element but the spec gives no format for it (e.g., "Month 3, Year 1", "Year 1 Month 3", "Day 42"). The spec also does not specify which `ICitySimulation` accessor provides date/time data, how many characters the element needs, or whether it uses the monospace font.
**Proposed resolution**: Add a `Date/Time Display` subsection specifying: format string, data source accessor (e.g., `getSimulatedDate()` or derived from tick count), character budget, monospace font requirement.

---

**HL-6** [GAP] LOW
The demand pressure bar spec says `getDemandPressurePct(ZoneType)` returns `float` in `[0.0, 1.0]` and the bar must multiply by 100.0f. It also warns about inverse semantics with `QueryResult::demandPressurePct`. However, the spec does not define what "0% demand pressure" means visually — is an empty bar good (no unmet demand) or bad (no demand at all)? The visual meaning (full bar = high unmet demand, i.e., opportunity to zone) vs. (full bar = high demand satisfaction, i.e., city is doing well) is ambiguous from the bar design alone.
**Proposed resolution**: Add a legend label below the bars (e.g., "Unmet demand" / "Low" → "High") with a brief tooltip that explains the direction: "Higher bar = more demand for this zone type." This is both a spec gap and a UX clarity issue.

---

**HL-7** [GAP] LOW
The `IUIBackend::setMouseCursor()` method is described as "not part of the V1 19-method `IUIBackend` interface" and cursor shape changes are deferred to Phase 12. However, the method is described as a future addition. When Phase 12 adds it, it will become method 22 (since the interface already has 21 methods). There is no placeholder or stub in the `IUIBackend` method contract in `ui-manager.md` for it. If Phase 12 implementers add it without updating the method count comment, `ui-manager.md` will become stale.
**Proposed resolution**: Add a numbered comment (method 22, reserved / Phase 12) to the `IUIBackend` interface listing in `ui-manager.md` so the contract stays auditable.

---

### `input-arbitration.md`

**IA-1** [GAP] HIGH
The Priority 7 rules include a detailed "Demolition Tool" section and a "Road tool — straight-line drag-select" section, but there is no specification for what happens during drag-selection when the cursor leaves the terrain entirely (ray-cast returns false mid-drag). The Zone tool spec says "If the LMB is released while the hover tile is invalid (m_hoveredTileX == -1, e.g., ray missed all terrain), no tiles are filled and the anchor is cleared silently." The Road tool has no equivalent mid-drag miss rule.
**Proposed resolution**: Add an explicit "ray-cast miss during drag" paragraph to the Road tool section stating behavior when `pickTerrainTile()` fails mid-drag (e.g., keep last valid anchor, do not extend preview, or clear preview).

---

**IA-2** [GAP] MEDIUM
The Priority 5 dispatch table lists toolbar y-ranges for Zone (64–112), Road (120–168), Utilities (176–224), Demolish (232–280), Query (288–336), Undo (608–656). These six entries account for y ranges up to 656. The text above the table says input carve-out runs to y:784 to cover demand bars (y:664–748) and active tool indicator (y:752–784). However, the dispatch table has no entries for clicks in the demand bar or active tool indicator regions. Clicks in y:657–784 would fall through the dispatch table with no handler — are they simply consumed by the carve-out (preventing accidental terrain interaction) without any action, or is this a gap?
**Proposed resolution**: Add a note to the dispatch table clarifying that clicks within the carve-out but not matching any button y-range are silently consumed (no action, but event is not passed to Priority 7).

---

**IA-3** [GAP] MEDIUM
The "Escape during keybinding capture" section specifies that pressing Escape while a chip is in Capturing state cancels capture AND closes Settings in the same event frame. However, it does not specify what happens if the player is in the Controls tab and has other unsaved keybinding changes (not the currently capturing chip) — do those pending changes get discarded along with the capture cancellation, or does the usual "Cancel reverts all pending keybinding changes" behavior still apply via the normal Settings Cancel path?
**Proposed resolution**: Clarify that the escape-during-capture path follows the same "Cancel reverts all pending changes" semantics as the normal Escape-closes-Settings path.

---

**IA-4** [GAP] LOW
The "RMB drag suppression in non-gameplay states" section guards `EMIE_RMOUSE_PRESSED_DOWN` with `UIManager::isGameplayOrPaused()`. However, this method is not declared anywhere in the `ui-manager.md` `UIManager` class structure listing. It is referenced here but has no formal spec.
**Proposed resolution**: Add `bool isGameplayOrPaused() const` to the UIManager public API section in `ui-manager.md` with its contract: returns `true` when `m_state == GameState::Gameplay || m_state == GameState::Paused`.

---

**IA-5** [DUPLICATE] LOW
The Finances Panel dismiss behavior (outside clicks not consumed) is described both in `input-arbitration.md` Priority 4 and in `finances-panel.md` (Dismiss click event consumption section). Both descriptions are accurate and consistent, but the `finances-panel.md` section is considerably more detailed. The duplication creates a maintenance risk if either is updated without updating the other.
**Proposed resolution**: In `input-arbitration.md` Priority 4, replace the local explanation with a cross-reference sentence: "Dismiss click event consumption: see `finances-panel.md — Dismiss click event consumption` for the full spec." Keep the authoritative text only in `finances-panel.md`.

---

**IA-6** [PROBLEM] LOW
The QueryPanel dismiss spec (Priority 3) specifies that the toolbar carve-out exception allows toolbar clicks to close the inspector AND activate the new tool in a single click. The implementation contract explicitly states "call `m_inspector->hide()`, set `m_inspectorOpen = false`, fall through to Priority 5." However, the dispatch table at Priority 5 uses a y-range hit test to identify the button. When the inspector-dismiss path falls through from Priority 3 to Priority 5, the original event's y coordinate is used for the Priority 5 hit test — but Priority 3 only fires the close sequence when the click is within the toolbar carve-out (x: 8–72). If a player clicks in x: 8–72 but in a y-range not matching any button (e.g., y: 344–607, which is between Query's bottom at y:336 and the undo button at y:608), the inspector closes but no tool is activated. This gap (y:337–607 within the toolbar carve-out) produces a silent click that only dismisses the inspector.
**Proposed resolution**: Document this as intentional behavior: clicks in the toolbar carve-out between buttons (y:337–607) close the inspector but do not activate any tool. Add this as an explicit note in the Priority 3 dispatch block.

---

### `main-menu-new-game-flow.md`

**MM-1** [GAP] MEDIUM
The Main Menu keyboard navigation spec says "Default keyboard focus on launch: 'New Game' button." But the spec does not address what happens if the player launches with a gamepad or other non-keyboard/mouse input device. This is low priority for V1 (desktop-only) but the Main Menu spec should at minimum note that non-keyboard/mouse input is out of scope for V1.
**Proposed resolution**: Add a V1 scope note: "Only keyboard and mouse input are supported in V1. Gamepad/controller support is post-V1."

---

**MM-2** [GAP] MEDIUM
The New Game screen has a "Disaster toggle (checkbox, default off for Easy/Normal, forced off in V1)". The spec says this control is "grayed out in V1" but does not specify via `setElementEnabled()` or `setElementVisible()`. Based on the pattern for the Scenario mode radio button (which is "grayed out with 'Post-launch' label"), the intent is to show the control as disabled. However, the spec for the Disaster toggle does not specify a label suffix (like "Post-launch") or a tooltip explaining why it is disabled. The `settings-pause-menu.md` Gameplay tab specifies the Disaster toggle there uses `setElementEnabled(..., false)` with "Post-launch" label suffix — the New Game screen variant should mirror this.
**Proposed resolution**: Specify that the New Game screen Disaster toggle uses `setElementEnabled(handle, false)` and displays a "(Post-launch)" suffix, matching the pattern in `settings-pause-menu.md`.

---

**MM-3** [GAP] LOW
The `MainMenuPanel → UIManager Communication` section specifies `consumeStartGameRequest()` and `consumeSettingsRequest()` polling flags, but it does not specify a `consumeLoadGameRequest()` polling flag for the Load Game button. The flow says "On activation: the loading-screen path is used (same as New Game start)" but the communication mechanism (polling flag from `MainMenuPanel` to `UIManager`) is not defined for load game, only for new game and settings.
**Proposed resolution**: Add `consumeLoadGameRequest()` to the MainMenuPanel polling contract table with the same consume-once semantics.

---

**MM-4** [GAP] LOW
The loading screen "Cancel" button is specified as "shown in the lower-right" but no virtual bounds are given. During the loading screen, normal HUD layout is not active. The exact position and size of the Cancel button, progress bar, and "Generating terrain..." label are unspecified. These need virtual coordinates for implementation.
**Proposed resolution**: Add virtual coordinate specs for the loading screen progress bar (e.g., centered, width 640 px, y: 490–530 px), status label (centered above bar), and Cancel button (x: 1740–1900, y: 980–1040).

---

### `minimap.md`

**MI-1** [GAP] MEDIUM
The minimap spec defines the `getBounds()` return value as the 200×200 px render area (x: 1720–1920, y: 880–1080) and explicitly excludes the toggle row, label strip, and legend panel. However, the click-to-pan implementation ("Click-to-pan camera to clicked minimap position") maps minimap pixel coordinates to world positions — this transformation formula is never specified. Phase 11 defers the actual camera pan wiring, but even the coordinate mapping formula (minimap pixel → world tile coordinate) is absent.
**Proposed resolution**: Add a "Coordinate Mapping" subsection specifying: `worldX = (minimapClickX - minimapLeft) / minimapW * mapTilesX`, `worldZ = (minimapClickY - minimapTop) / minimapH * mapTilesZ`, where minimap bounds are the 200×200 render area.

---

**MI-2** [GAP] MEDIUM
The minimap Traffic Congestion overlay is specified for V1 but `traffic-system.md` is not yet referenced by any UI spec. The three speed bands (≥40%, 31–39%, ≤30% of free-flow speed) are defined in `minimap.md` but the data source (`ICitySimulation` method that returns per-road-segment speed ratios) is not specified. The minimap spec says "Overlay data is rendered into the minimap texture at budget-tick cadence" but does not define which accessor provides the data.
**Proposed resolution**: Add a cross-reference to the traffic system spec and specify the `ICitySimulation` accessor used to query road segment speed ratios (e.g., `getRoadSegmentSpeedRatio(tileX, tileZ)` returning float in [0, 1]).

---

**MI-3** [INCONSISTENCY] LOW
The minimap spec says the colorblind pattern for Water Tower coverage is "Cross-hatch", and the `resolution-ui-scaling.md` Service Coverage overlay colorblind patterns also say Water Tower = "Cross-hatch". However, the demand pressure bar colorblind patterns in `hud-layout.md` / `resolution-ui-scaling.md` assign "cross-hatch" to Industrial (I), while Water Tower uses cross-hatch for the service overlay. This creates a cross-context pattern collision: players in colorblind mode will see "cross-hatch" meaning both Industrial demand and Water Tower coverage depending on context, with no label differentiation. While this may be acceptable given the different visual contexts (toolbar bar vs. minimap tile), it is worth noting.
**Proposed resolution**: Document the cross-context pattern reuse as intentional and note that context (minimap vs. demand bars) is sufficient disambiguation, OR assign distinct patterns. At minimum, add a note acknowledging the overlap.

---

### `modal-dialog-system.md`

**MD-1** [GAP] HIGH
The "Unsaved changes" modal is referenced in `settings-pause-menu.md` (Quit to Desktop / Quit to Main Menu flows) but its full spec is not defined in `modal-dialog-system.md`. The settings file says it has title "Unsaved Progress", body "You have unsaved changes.", three buttons (Save and Quit / Quit Without Saving / Cancel), and is dismissible via Escape (Cancel). But this modal is not listed in the Modal sizes table or the Visual Design section of `modal-dialog-system.md`, and has no Tab order specification. Specifically, which button gets default Tab focus (least destructive = Cancel) is not stated.
**Proposed resolution**: Add a full `Unsaved Changes Confirmation` modal entry to `modal-dialog-system.md`, including size (Small 480×240 px), Tab order (default focus: Cancel), Escape behavior (Cancel), and button layout.

---

**MD-2** [GAP] MEDIUM
The "Save Failed" modal triggered on manual save failure (Ctrl+S) is described in `settings-pause-menu.md` as "a blocking error modal (not a silent failure): title 'Save Failed', body '[reason]', buttons: 'Retry' / 'Cancel'." This modal is not specified in `modal-dialog-system.md`. There is no size, Tab order, or Escape behavior defined. The modal is described as "dismissable (not forced)" in `settings-pause-menu.md` but the full ModalDialog spec for it is absent.
**Proposed resolution**: Add a `Save Failed Error Modal` section to `modal-dialog-system.md` with size, Tab order (default focus: Retry — the safer/most common recovery action), Escape behavior (Cancel), and confirm that Escape closes without retrying.

---

**MD-3** [GAP] MEDIUM
The forced loan dialog's "last-resort deadlock prevention" path overrides the debt cap with inline text: "Debt cap overridden — emergency credit issued to prevent soft-lock." This scenario (bonds exhausted + all rates at max + no demolishable building) is the only path where the "Back — Accept original loan terms" button force-issues a loan. However, there is no specification for how large this forced loan is when the debt cap is overridden. The regular forced loan formula caps at `3 × max(monthly_revenue, $1,000) − outstanding_debt` but that formula would return 0 or negative when the cap is exhausted. The spec says "force-issues the loan regardless of the outstanding debt cap" but gives no loan amount for the override case.
**Proposed resolution**: Specify a loan amount for the debt-cap-override path, e.g., the standard `monthly_shortfall × 3` formula without the cap constraint, or a fixed emergency amount. Cross-reference `economy-model.md`.

---

**MD-4** [INCONSISTENCY] LOW
The forced loan dialog lists the WASD preset modal as `Small (480×240 px)` and the demolish confirmation as `Small`. The Modal sizes table at the top of the file says "Small = 480×240 px; Medium = 560×320 px; Large = 640×400 px." The demolish confirmation modal body includes "Demolish [N] tiles? You can press Ctrl+Z to undo." and a "Do not ask again" checkbox. At 480×240 px, fitting a title, body text, checkbox, and two buttons is tight. There is no explicit content layout or font-size guidance for the Small modal to verify content fits without scrolling (the rule says "Use the smallest size that fits all required content without scrolling").
**Proposed resolution**: Add a content layout sketch or character-budget note to each modal size to verify the "no scrolling" rule.

---

### `notification-system.md`

**NS-1** [GAP] MEDIUM
The notification log panel spec says "Most-recent notification is at the top of the list" and "Shows the last 50 notifications." But there is no specification for what happens to notifications beyond 50 — are they permanently dropped, archived to a file, or pushed out of the visible list but retained in memory? The "Session persistence" note says the log "is NOT cleared on save or load within the same session" but does not clarify the 50-entry cap semantics (drop oldest vs. ring buffer vs. persistent scroll archive).
**Proposed resolution**: Explicitly state that entries beyond 50 are dropped from the in-memory log (oldest entry is removed when a 51st notification arrives), and this is permanent — they cannot be recovered within the session.

---

**NS-2** [GAP] MEDIUM
The CRITICAL toast keyboard navigation spec says "The first (oldest) CRITICAL toast receives keyboard focus automatically when it becomes visible." But the spec does not address what happens to keyboard focus when the first CRITICAL toast is dismissed — does focus move to the second CRITICAL toast automatically, or does it return to the previously focused element (e.g., a toolbar button)? The "two visible CRITICAL toasts are Tab-navigable" clause implies Tab can move between them, but auto-focus-shift on dismiss is unspecified.
**Proposed resolution**: Add a focus-transfer rule: "When a CRITICAL toast is dismissed, if another CRITICAL toast is visible, keyboard focus moves to it automatically. If no CRITICAL toast remains, focus returns to the previously focused toolbar element or defaults to the first toolbar button."

---

**NS-3** [GAP] LOW
The toast height enforcement note says `NotificationManager` must enforce the 63 px Normal toast height cap "at element creation time." But the actual mechanism for enforcing this via `IUIBackend` is not specified — there is no `setElementHeight()` or `setElementMaxHeight()` method in the IUIBackend interface. If text wraps, the `IGUIStaticText` element grows unless explicitly constrained. How the 63 px cap is enforced (e.g., by passing `h=63` to `addStaticText()` and relying on Irrlicht clipping, or by pre-truncating text) is unspecified.
**Proposed resolution**: Specify the enforcement mechanism: the element is created with `addStaticText(text, x, y, w, 63)` (h=63 px cap applied at creation), relying on Irrlicht's `IGUIStaticText` to clip overflow. Text is pre-truncated to 80 characters before element creation to prevent wrapped-text height growth.

---

**NS-4** [DUPLICATE] LOW
The `dismissCriticalToast(UIElementHandle)` API is described in both the `NotificationManager API` section of `notification-system.md` and in the `CLAUDE.md` "Notes for AI Assistants" section ("`NotificationManager::dismissCriticalToast(UIElementHandle)` is the production API for player-dismissal of CRITICAL toasts"). This is a minor redundancy that increases maintenance surface.
**Proposed resolution**: This is acceptable as `CLAUDE.md` is an overview doc. No action required.

---

### `query-inspector-panel.md`

**QI-1** [GAP] MEDIUM
The panel has 8 rows with `kLineH=33` and 16 px padding, totaling 8×33+16 = 280 px height. This matches `kPanelH=280`. However, the "Updated N seconds ago" label is specified at the bottom of the panel. With 8 data rows already filling the panel, it is unclear whether this label occupies one of the 8 rows or is a 9th row that causes height overflow. A 9th row would require `kPanelH` to be 313 px. If the "Updated" label is inside the 280 px height, one of the 8 rows must be that label, leaving only 7 for data — which may not be enough for the most data-rich tile type (zone tile: 5 fields; service building: 3 fields; road: 3+ fields).
**Proposed resolution**: Either explicitly state that the "Updated" label is one of the 8 counted rows (with a numbered field layout table), or increase `kPanelH` to accommodate it as an extra row, and update the layout constant table accordingly.

---

**QI-2** [GAP] MEDIUM
The Panel has no specified close affordance (e.g., an [X] button in the corner). Dismissal is via pressing I, clicking outside, or pressing Escape. For users who rely solely on mouse input, there is no visible close button on the panel itself — the dismiss mechanism is invisible to them unless they already know the hotkey. This is a usability gap for discoverability.
**Proposed resolution**: Add a close affordance to the panel spec: a small [X] button or close icon at the top-right corner of the panel (within the 340×280 px bounds), with the Glass City focus/hover border spec already defined for it in the visual design section.

---

**QI-3** [GAP] LOW
The data refresh policy distinguishes budget/economy data (every ~120 frames) and traffic data (every 10 frames). However, the policy uses draw-frame count as a proxy for budget-tick cadence. This means the refresh interval is sensitive to actual FPS — at 30 FPS, 120 frames ≈ 4 seconds; at 60 FPS, 120 frames ≈ 2 seconds. The spec acknowledges "approximately 2 s at 60 FPS" but does not specify whether the intent is time-based or frame-based. If frame-based, 30 FPS users get stale data for longer.
**Proposed resolution**: Clarify whether the refresh should be time-based (use `IClock` and `realDeltaSeconds` to accumulate elapsed time toward a 2 s threshold) or frame-count-based as currently specified. Time-based is more robust.

---

**QI-4** [PROBLEM] LOW
The third-fallback edge-snap formula uses `edge_x = cursor_x <= 960 ? 1920 − 340 : 0`. This means when the cursor is exactly at x=960 (horizontal center), the panel snaps to the right edge. However, when the cursor is in the right half (x > 960), the panel snaps to the left edge (x = 0). For a cursor in the right half with a tile near center-screen, snapping to x=0 places the panel far from the tile AND the cursor — the player's eyes must travel the full width of the screen. The snap direction logic appears inverted for ergonomic comfort.
**Proposed resolution**: Review the third-fallback direction: `cursor_x <= 960 → snap right (1920-340)` means cursor in left half → panel at right edge, cursor in right half → panel at left edge. This is the furthest possible panel from the cursor in both cases. Consider whether the intent is nearest edge (right if cursor is right of center) or furthest edge. If the goal is to maximize panel/tile distance, the current logic is correct but should be explicitly justified.

---

### `resolution-ui-scaling.md`

**RS-1** [GAP] MEDIUM
The Colorblind Accessibility section specifies that Zone placement preview/cursor tints must include a zone-type label overlay ('R', 'C', 'I') in colorblind mode. However, the spec for colorblind support for the density-tier-based fixed-color overlay system (Phase 11m and beyond) says "Full colorblind support for the density-tier-based fixed-color overlay system is deferred to a post-V1 colorblind QA pass." This creates a V1 delivery gap: the zone placement cursor in colorblind mode must show zone-type letters, but the zone overlay (the persistent unbuilt-tile color overlay) in colorblind mode does NOT show letters in V1. This inconsistency means colorblind users see zone letters on the hover cursor but not on already-placed unbuilt tiles.
**Proposed resolution**: Document this explicitly as a known V1 limitation: "In V1 (through Phase 12), zone-type letter labels appear on the placement cursor (hover) in colorblind mode but NOT on placed-tile overlays. Full per-tile label rendering is post-V1 scope."

---

**RS-2** [GAP] LOW
The colorblind mode toggle spec says it is "located in Settings > Graphics tab, Accessibility subsection" and "the toggle MUST NOT appear in any other tab or panel." However, there is no spec for the Accessibility subsection's visual layout within the Graphics tab — how is it separated from the Resolution/Vsync/MSAA controls? Is it a labeled section header? A horizontal rule? This affects whether the subsection is discoverable by users who need it.
**Proposed resolution**: Add a brief layout note: "The Accessibility subsection is separated from the main Graphics controls by a 1 px horizontal rule and a 'Accessibility' section header label in `#4A7FA5` mid-blue."

---

**RS-3** [GAP] LOW
The `UIScaler::setViewportSize()` spec says "The main loop MUST call `uiScaler.setViewportSize()` each frame." However, `irrlicht-device-lifecycle.md` describes an 11-step per-frame sequence and does not include `uiScaler.setViewportSize()` as one of the steps. The two specs are inconsistent about when this call happens relative to other frame steps.
**Proposed resolution**: Add `uiScaler.setViewportSize(screenW, screenH)` as an explicit step in the per-frame sequence in `irrlicht-device-lifecycle.md`, positioned before event processing (step 1: poll events), consistent with the `resolution-ui-scaling.md` specification.

---

### `settings-pause-menu.md`

**SP-1** [GAP] HIGH
The Settings panel keyboard navigation spec states "Default focused tab on open: the previously active tab, or Graphics on first open." But the spec does not define where tab state is persisted — is it a `UIManager` member? A `SettingsPanel` member? A settings config file field? If the player exits Settings and re-enters later in the same session, "previously active tab" implies in-memory state. If the game is restarted, "first open" implies `Graphics`. This persistence contract needs to be explicit.
**Proposed resolution**: Add a note that last-active-tab is stored in `SettingsPanel::m_activeTab` (session-only, not persisted to config file). The tab state resets to Graphics on each application launch.

---

**SP-2** [GAP] MEDIUM
The auto-save triggers are described in two places: `settings-pause-menu.md` (every 120 real seconds) and `save-system.md` (every 120 real seconds OR every 5 budget ticks, whichever comes first; also on pause-menu open and on forced loan dialog activation). The `settings-pause-menu.md` description is incomplete — it only mentions the 120-second trigger and omits the 5-tick trigger and the pause-menu-open trigger. This creates a documentation discrepancy where a user reading only the settings spec would not know about the richer auto-save behavior.
**Proposed resolution**: Either update `settings-pause-menu.md` auto-save description to match the full spec in `save-system.md`, or replace it with a cross-reference: "See `architecture/game-design/save-system.md` for the full auto-save trigger list."

---

**SP-3** [GAP] MEDIUM
The Graphics tab has a "Confirm display change?" modal with a 10-second countdown. This is a modal dialog but it is not listed in `modal-dialog-system.md`. Its size, keyboard navigation, Tab order, and Escape behavior are not specified there. The settings spec describes it only briefly ("modal with 10-second countdown").
**Proposed resolution**: Add a `Display Change Confirmation Modal` section to `modal-dialog-system.md` with size (Small 480×240 px is appropriate), Tab order, Escape behavior (auto-revert immediately, same as countdown expiry), and countdown rendering spec (amber `#E8960C` text color matches the Glass City visual design table in `settings-pause-menu.md`).

---

**SP-4** [GAP] MEDIUM
The Pause Menu spec says "Quit to Main Menu" and "Quit to Desktop" both check `m_hasUnsavedChanges`. The resulting modal has three buttons: Save and Quit / Quit Without Saving / Cancel. However, `settings-pause-menu.md` does not specify the keyboard navigation for this modal (which button gets default Tab focus). Based on the global modal rule in `modal-dialog-system.md` (default focus = least destructive = Cancel), the focus should be Cancel. This is not restated for this specific modal.
**Proposed resolution**: Add a note confirming default Tab focus is Cancel (least destructive action per the global modal rule), consistent with the game-over modal and WASD confirmation modal specs.

---

**SP-5** [GAP] LOW
The post-V1 manual save slot picker (3 slots with timestamps) is described with "saving to an occupied slot shows 'Overwrite [slot name]? Yes / Cancel'" confirmation. This is a UI modal flow that will need a full spec when implemented. The current V1 spec does not define the slot picker's virtual dimensions, content layout, or keyboard navigation. While this is post-V1, a placeholder reference in `modal-dialog-system.md` would prevent it from being forgotten.
**Proposed resolution**: Add a "(Post-V1) Save Slot Picker" placeholder entry to `modal-dialog-system.md`.

---

### `ui-manager.md`

**UM-1** [GAP] MEDIUM
The `UIManager` class structure lists `m_finances` (FinancesPanel) as a private member, but there is no corresponding `m_newGamePending` or `m_gameSessionActive` member visible in the private section. Both are referenced in the `transitionToMainMenu()` comment ("Does NOT reset m_gameSessionActive — keeps it true so the next handleNewGameRequest() takes the subsequent-game path (sets m_newGamePending=true)"). These members are not documented in the class structure block, making the flow hard to follow for implementors.
**Proposed resolution**: Add `m_gameSessionActive`, `m_newGamePending`, and `m_pendingQuit` (referenced in `save-system.md`) to the UIManager private members section with brief contracts.

---

**UM-2** [GAP] LOW
The `UIManager::draw()` doc comment says "Render all GUI panels — call AFTER sceneManager->drawAll() and BEFORE endScene()." This is accurate but incomplete relative to the irrlicht-device-lifecycle.md spec which adds "and BEFORE guiEnvironment->drawAll()". The ordering distinction (UIManager::draw() before guiEnvironment->drawAll()) is architecturally critical (UIManager::draw sets visibility state; guiEnvironment::drawAll renders it) but the doc comment in `ui-manager.md` does not state the `guiEnvironment->drawAll()` relationship.
**Proposed resolution**: Update the `draw()` doc comment to: "Call AFTER sceneManager->drawAll() and BEFORE guiEnvironment->drawAll() and endScene()."

---

**UM-3** [DUPLICATE] LOW
The `kOverlayArgbResidential`, `kOverlayArgbCommercial`, `kOverlayArgbIndustrial` constants (and their colorblind variants) are defined in both the `ui-manager.md` Toolbar Carve-Out Constants block (the `ui_constants.h` example) AND in the `hud-layout.md` Zone Colour Overlay section. Both files repeat the same hex values. Any value update requires editing two spec files.
**Proposed resolution**: Make `ui-manager.md` the single canonical definition of the `ui_constants.h` block, and have `hud-layout.md` cross-reference it rather than repeat the hex values.

---

---

## Cross-File Issues

---

**CF-1** [INCONSISTENCY] CRITICAL
**Files**: `hud-layout.md` (Utilities Sub-Panel), `ui-manager.md` (Toolbar Carve-Out Constants)
The Utilities sub-panel layout is contradicted between the two files:
- `hud-layout.md`: 4×1 single-row grid, 4 buttons at 64×40 px, total width = (64×4)+(4×3) = 268 px, top = y:64.
- `ui-manager.md`: 2×2 button grid, each button 96×48 px, total width = 196 px, top = y:176.

The virtual bounds (x:80–348 vs. x:80–276), height (40 px vs. 100 px), button count (4×1 vs. 2×2), and top-y anchor (y:64 vs. y:176) all differ. This cannot be resolved by implementation; one spec must be wrong.
**Proposed resolution**: Resolve the canonical layout. The 2×2 arrangement at y:176 in `ui-manager.md` is more ergonomic (aligns with Utilities toolbar button) but the constants and text must match exactly. Update whichever file is incorrect. This must be fixed before Phase 9 implementation.

---

**CF-2** [INCONSISTENCY] HIGH
**Files**: `hud-layout.md` (demand bar y:664–748), `ui-manager.md` (Toolbar Carve-Out Constants note says demand bars at y:664–744)
The demand bars top and bottom differ by 4 px across the two files: `hud-layout.md` says "y: 664–748 px" and the commentary in `hud-layout.md` also says "y:664–744 px" in an inline layout note within the same document. This is an internal self-contradiction in `hud-layout.md` itself (the heading says y:664–748, the inline note says y:664–744), and `ui-manager.md`'s text says "demand bars (y:664–744)".
**Proposed resolution**: Pick one value. The element height of 56 px for the colored bar columns (y:692–748 per the detailed description) makes 748 the correct bottom. Update all references to use y:664–748.

---

**CF-3** [INCONSISTENCY] HIGH
**Files**: `hud-layout.md` (resource bar y: 0–56 px), `input-arbitration.md` (Priority 3 toolbar carve-out: y: 64–784), `notification-system.md` (CRITICAL band y: 20–116)
The CRITICAL toast band starts at y:20, which is within the resource bar (y: 0–56). No spec defines what happens when a CRITICAL toast overlaps the resource bar visually. The toast Z-order versus resource bar Z-order is unspecified. The notification system spec says CRITICAL toasts are in a "reserved top band" but does not address the visual overlap with the always-present resource bar.
**Proposed resolution**: Add a Z-order note to `notification-system.md` stating that CRITICAL toasts render above the resource bar (higher Z-order) and that the resource bar content at y:20–56 may be partially obscured by CRITICAL toast elements. Alternatively, adjust the CRITICAL band to start below the resource bar at y:64 if 44 px per toast is sufficient.

---

**CF-4** [INCONSISTENCY] MEDIUM
**Files**: `hud-layout.md` (unsaved changes dot at x: 1796–1812), `notification-system.md` (bell icon at x: 1820–1868)
The unsaved changes indicator (x: 1796–1812) and the notification bell icon (x: 1820–1868) are correctly non-overlapping per `hud-layout.md` ("8 px gap before the bell's x:1820 left edge"). However, the time controls block (x: 1600–1796) per the HUD layout description ends at x:1796, meaning the unsaved changes dot (1796–1812) overlaps the right edge of the time controls area by 0 px (they share the edge at 1796). The dot BEGINS at x:1796 which equals the time controls block's right edge. This is an exact edge-share with no visual gap between the speed selector buttons and the dot. Whether this zero-gap is intentional or a rounding artifact is unspecified.
**Proposed resolution**: Verify the time controls right boundary. If x:1796 is the last pixel of the time controls, the dot at x:1796 starts on that boundary — add at least an 8 px gap: set dot to x:1804–1820 and shift the bell accordingly, or verify the 8 px gap is preserved.

---

**CF-5** [INCONSISTENCY] MEDIUM
**Files**: `finances-panel.md`, `settings-pause-menu.md`
The unsaved-changes tracking spec in `settings-pause-menu.md` says `m_hasUnsavedChanges` is set to `true` by "tax rate change" among other actions. `finances-panel.md` confirms "tax changes... DO set `UIManager::m_hasUnsavedChanges = true`." However, `economy-model.md` says tax rates are intentionally NOT reset on New Game start (they persist across `CitySimulation::reset()` calls). This creates a scenario where: player adjusts tax rates in Session 1, saves and quits, relaunches, starts a New Game (resumes at old tax rates), immediately quits without placing anything — `m_hasUnsavedChanges` is `false` because no explicit tax-rate-change event fired in Session 2. The player's inherited tax rates are never saved unless they trigger a save action. The spec for what constitutes a "change" in the context of inherited cross-session tax rates is unspecified.
**Proposed resolution**: Add a note to `settings-pause-menu.md` clarifying that `m_hasUnsavedChanges` is only set by in-session tax rate changes (i.e., player explicitly presses +/- or direct-enters a value). Inherited cross-session tax rates that have not been explicitly changed in the current session do NOT set the flag.

---

**CF-6** [INCONSISTENCY] MEDIUM
**Files**: `input-arbitration.md` (Priority 3 minimap carve-out), `minimap.md` (widget footprint)
`input-arbitration.md` Priority 3 says the full minimap widget footprint for the carve-out uses constants `kMinimapWidgetTopOverlayActive` and `kMinimapWidgetTop` from `ui_constants.h`. However, these constants are not listed anywhere in the `ui-manager.md` Toolbar Carve-Out Constants block, which is the authoritative listing of `ui_constants.h` contents. The constants are referenced but never defined in any spec file with their numeric values.
**Proposed resolution**: Add `kMinimapWidgetTop`, `kMinimapWidgetTopOverlayActive`, `kMinimapLeft = 1576`, `kMinimapRight = 1920`, `kMinimapBottom = 1080` to the `ui_constants.h` block in `ui-manager.md` with their computed values.

---

**CF-7** [INCONSISTENCY] LOW
**Files**: `notification-system.md` (Normal toast start at y:130), `hud-layout.md` (resource bar y: 0–56, grace period indicator y: 60–92)
Normal toasts start at y:130 (fixed, regardless of CRITICAL count). The grace period indicator occupies y: 60–92 (directly below the resource bar). A Normal toast starting at y:130 and ending at y:193 (one 63 px toast) does not overlap the grace period indicator at y:60–92. However, during the early-game window when both a Normal toast and the grace period indicator are visible simultaneously, they occupy y:60–193. The spec does not verify that this is acceptable and does not mention the visual stacking of these two elements.
**Proposed resolution**: Add a note confirming that the grace period indicator (y:60–92) and Normal toasts (starting y:130) have a 38 px gap and do not overlap. Mark this as a verified layout constraint.

---

**CF-8** [DUPLICATE] LOW
**Files**: `input-arbitration.md`, `hud-layout.md`
The Zone Rectangular Selection interaction sequence (press anchor, drag preview, release fill) is described in both `hud-layout.md` (Zone Rectangular Selection section) and `input-arbitration.md` Priority 7 (Zone tool — rectangular drag-select). The two descriptions are largely consistent but the `input-arbitration.md` version is more complete (includes the `freeTiles`/`blockedTiles` partition and `setTilePlacementPreview` calls). The `hud-layout.md` version describes the same sequence without the preview color detail.
**Proposed resolution**: In `hud-layout.md`, replace the full interaction sequence with a cross-reference: "See `input-arbitration.md` Priority 7 for the authoritative Zone tool rectangular drag-select interaction spec." Keep only the "V1 Option B" hover-only preview note in `hud-layout.md` if it is not redundant.

---

---

## Missing Specs

---

**MISSING-1** [MISSING] HIGH
**Loading Screen UI Spec**
The loading screen is referenced throughout multiple files (main-menu-new-game-flow.md, modal-dialog-system.md game-over section, game-over-flow.md "Load Last Save" path, save-system.md) but has no dedicated spec file. Required content: virtual coordinate layout of progress bar, status label, and Cancel button; font and color choices (Glass City palette); progress data source; minimum display time handling (0.5 s floor); Cancel button state transitions; loading screen → gameplay transition signal (`onGameLoaded()` call timing).
**Proposed resolution**: Create `architecture/ui-ux/loading-screen.md` with a full spec.

---

**MISSING-2** [MISSING] HIGH
**HUD Resource Bar Full Element Layout**
The resource bar (y: 0–56 px, full width) contains multiple elements (treasury balance, debt indicator, City Rating label, population count, date/time display, and the density unlock progress indicator from `economy-model.md`). No spec defines the horizontal position of each element within the bar (x ranges, alignment, order from left to right, separators between fields). The only visual spec is the Glass City background colour. With 6+ elements in a 1920 px wide bar, the layout needs to be fully specified.
**Proposed resolution**: Add a `Resource Bar Element Layout` section to `hud-layout.md` with per-element virtual x ranges, text alignment, separator positions, and the element ordering (e.g., [Treasury | Debt] [Population] [Date] [City Rating] [Density Unlock Progress] [Grace period]).

---

**MISSING-3** [MISSING] MEDIUM
**City Rating Display Spec**
The resource bar includes a "City Rating label (`getCityRating()` → `CityRatingTier` display name)" but no spec defines what `CityRatingTier` values look like in the HUD (e.g., "Bronze City", "Silver City", "Gold City", or "Rating: B+"). The color of the rating label (presumably `#F0B429` amber for values or `#EBF4F6` near-white for labels) is not specified. The stinger_milestone event in `ui-manager.md` fires on upward `CityRatingTier` transitions — but the tier names themselves are not documented in any UI spec file.
**Proposed resolution**: Add a `City Rating Display` section to `hud-layout.md` or `hud-layout.md` referencing `architecture/game-design/game-progression-modes.md` for tier names, with the display format and color.

---

**MISSING-4** [MISSING] MEDIUM
**Outstanding Debt Indicator Full Spec**
The resource bar mentions an "outstanding debt indicator (`getOutstandingDebt()` — hidden when debt is zero)" but provides no visual spec: no virtual bounds, no color (is it `#F04E37` red? amber?), no format (e.g., "Debt: $12,500" or just a red badge with the amount), no tooltip content. This element is also referenced in `hud-layout.md`'s notification system cross-reference and in the loan/economy model but never fully specified as a HUD element.
**Proposed resolution**: Add a `Debt Indicator` subsection to `hud-layout.md` with format, color (`#F04E37` red, consistent with deficit indicator), virtual bounds, tooltip, and hide/show conditions.

---

**MISSING-5** [MISSING] MEDIUM
**"Paused — [event name]" Indicator in Time Controls**
`notification-system.md` specifies: "A 'Paused — [event name]' indicator appears in the Time Controls area to distinguish CRITICAL-toast-pause from player-initiated pause; after CRITICAL toast dismissal the indicator changes to the standard 'Paused' label." The Time Controls area (x: 1600–1796, y: 8–56) has the four speed buttons (Pause/1×/3×/10×). There is no spec for where this additional text indicator appears within the Time Controls area, what color it uses, or what the "standard 'Paused' label" looks like. Whether it is a 5th element in the speed controls row or an overlay on the Pause button is unspecified.
**Proposed resolution**: Add a `Pause State Indicator` section to `hud-layout.md` Time Controls specifying: the indicator's virtual bounds, text format ("Paused — [event]" vs. "Paused"), color (`#E8960C` warning amber for CRITICAL-toast pause; `#EBF4F6` near-white for player-initiated pause), and how it coexists with the four speed buttons.

---

**MISSING-6** [MISSING] LOW
**Post-V1 Scenario Mode New Game Screen**
The New Game screen has a Scenario mode radio button grayed out with "Post-launch" label. No spec describes what the Scenario mode new game flow would look like post-V1 (scenario selection, difficulty presets, scenario-specific options). While this is intentionally deferred, a placeholder stub in `main-menu-new-game-flow.md` would prevent the flow from being designed inconsistently in a future phase.
**Proposed resolution**: Add a brief "(Post-launch) Scenario Mode New Game" stub section to `main-menu-new-game-flow.md` noting it is out of scope for V1 and will require a dedicated spec.

---

**MISSING-7** [MISSING] LOW
**Notification Bell Unread Count Badge Spec**
`notification-system.md` specifies the unread count badge has "`#F04E37` red circular badge with `#EBF4F6` white numeral." But the badge dimensions, positioning relative to the bell icon (top-right corner? bottom-right?), font size, and behavior when count exceeds 9 (single digit → "9+" or "99+"?) are unspecified.
**Proposed resolution**: Add a `Bell Icon Unread Badge` subsection with: badge diameter (16 px), position (top-right of bell icon, partially overlapping), font size (minimum 9 px virtual), overflow behavior ("9+" when count > 9).

---

---

## Summary Table

| ID | File | Type | Severity | Short Description |
|---|---|---|---|---|
| HL-1 | `hud-layout.md` | GAP | HIGH | Density Unlock Progress Indicator section missing — referenced by `economy-model.md` |
| HL-2 | `hud-layout.md` | GAP | HIGH | Wages not in HUD resource bar spec but `economy-model.md` says it is |
| CF-1 | `hud-layout.md` / `ui-manager.md` | INCONSISTENCY | CRITICAL | Utilities sub-panel layout contradicted (4×1 @64px vs 2×2 @96px, width 268 vs 196, y:64 vs y:176) |
| CF-2 | `hud-layout.md` (internal) | INCONSISTENCY | HIGH | Demand bar bottom y contradicted within same file (748 vs 744) |
| CF-3 | `hud-layout.md` / `notification-system.md` | INCONSISTENCY | HIGH | CRITICAL toast band starts at y:20, within resource bar (y:0–56); Z-order unspecified |
| HS-1 | `hotkey-scheme.md` | PROBLEM | HIGH | Space key arbitration path through 6-level priority chain never specified |
| MD-1 | `modal-dialog-system.md` | GAP | HIGH | Unsaved Changes modal spec missing (referenced from settings but not defined in modal spec) |
| MISSING-1 | — | MISSING | HIGH | No loading-screen UI spec file |
| MISSING-2 | — | MISSING | HIGH | Resource bar element layout (horizontal positions) never fully specified |
| SP-1 | `settings-pause-menu.md` | GAP | HIGH | Settings tab persistence (session vs. config-file) not specified |
| HL-3 | `hud-layout.md` | GAP | MEDIUM | Utilities sub-panel width 268 vs. 196 in ui-manager.md constants |
| HL-4 | `hud-layout.md` / `ui-manager.md` | INCONSISTENCY | MEDIUM | Utilities sub-panel top-y: 64 vs. 176 across files |
| HL-5 | `hud-layout.md` | GAP | MEDIUM | In-game date/time display format, accessor, and monospace requirement unspecified |
| IA-1 | `input-arbitration.md` | GAP | HIGH | Road tool mid-drag ray-cast miss behavior unspecified |
| IA-2 | `input-arbitration.md` | GAP | MEDIUM | Toolbar carve-out y:657–783 click behavior (between buttons) never stated |
| IA-3 | `input-arbitration.md` | GAP | MEDIUM | Escape-during-capture interaction with other pending keybinding changes unspecified |
| CF-4 | `hud-layout.md` / `notification-system.md` | INCONSISTENCY | MEDIUM | Unsaved dot and time controls share x:1796 edge — zero-gap ambiguity |
| CF-5 | `finances-panel.md` / `settings-pause-menu.md` | INCONSISTENCY | MEDIUM | m_hasUnsavedChanges and cross-session inherited tax rates: when flag is set unspecified |
| CF-6 | `input-arbitration.md` / `ui-manager.md` | INCONSISTENCY | MEDIUM | kMinimapWidgetTop / kMinimapWidgetTopOverlayActive referenced but never defined with values |
| FP-1 | `finances-panel.md` | GAP | MEDIUM | Finances panel y anchor not specified (only "below resource bar") |
| HS-2 | `hotkey-scheme.md` | GAP | MEDIUM | Numpad +/- speed keys not addressed |
| IA-4 | `input-arbitration.md` | GAP | LOW | `UIManager::isGameplayOrPaused()` referenced but not declared in UIManager class spec |
| MD-2 | `modal-dialog-system.md` | GAP | MEDIUM | Save Failed error modal not defined in modal spec |
| MD-3 | `modal-dialog-system.md` | GAP | MEDIUM | Debt-cap-override loan amount for last-resort deadlock path unspecified |
| MI-1 | `minimap.md` | GAP | MEDIUM | Click-to-pan coordinate mapping formula (minimap px → world tile) not specified |
| MI-2 | `minimap.md` | GAP | MEDIUM | Traffic overlay data source accessor (`ICitySimulation` method) not specified |
| MM-3 | `main-menu-new-game-flow.md` | GAP | LOW | consumeLoadGameRequest() polling flag not defined |
| MM-4 | `main-menu-new-game-flow.md` | GAP | LOW | Loading screen element virtual bounds not specified |
| MISSING-3 | — | MISSING | MEDIUM | City Rating display format and color not specified in any UI file |
| MISSING-4 | — | MISSING | MEDIUM | Outstanding debt indicator HUD spec missing |
| MISSING-5 | — | MISSING | MEDIUM | "Paused — [event]" text indicator in Time Controls area unspecified |
| NS-1 | `notification-system.md` | GAP | MEDIUM | Notification log cap behavior (>50 entries) not specified |
| NS-2 | `notification-system.md` | GAP | MEDIUM | CRITICAL toast keyboard focus transfer on dismiss not specified |
| QI-1 | `query-inspector-panel.md` | GAP | MEDIUM | "Updated N seconds ago" label vs. 8-row height budget unresolved |
| QI-2 | `query-inspector-panel.md` | GAP | MEDIUM | No visible close affordance (X button) on inspector panel |
| SP-2 | `settings-pause-menu.md` | GAP | MEDIUM | Auto-save description incomplete vs. save-system.md |
| SP-3 | `settings-pause-menu.md` | GAP | MEDIUM | Display Change Confirmation modal not defined in modal-dialog-system.md |
| SP-4 | `settings-pause-menu.md` | GAP | MEDIUM | Quit confirmation modal Tab order/default focus not stated |
| CC-1 | `camera-controls.md` | GAP | MEDIUM | Named constants (kBasePanSpeed etc.) have no declared canonical home |
| CC-2 | `camera-controls.md` | GAP | MEDIUM | Sensitivity slider applicability to RMB rotate not stated |
| FP-3 | `finances-panel.md` | INCONSISTENCY | LOW | Surplus green #80C850 not in Glass City canonical palette table |
| HS-3 | `hotkey-scheme.md` | GAP | LOW | Chord vs. single-key conflict exclusion rule not specified |
| IA-5 | `input-arbitration.md` | DUPLICATE | LOW | Finances Panel dismiss behavior described in both input-arbitration.md and finances-panel.md |
| IA-6 | `input-arbitration.md` | PROBLEM | LOW | Inspector dismiss + toolbar carve-out gap at y:337–607 produces silent clicks |
| MI-3 | `minimap.md` | INCONSISTENCY | LOW | Cross-hatch pattern used for both Water Tower (minimap) and Industrial (demand bars) in colorblind mode |
| MISSING-6 | — | MISSING | LOW | No post-V1 Scenario mode new game flow placeholder |
| MISSING-7 | — | MISSING | LOW | Bell icon unread count badge dimensions and overflow not specified |
| MM-2 | `main-menu-new-game-flow.md` | GAP | MEDIUM | Disaster toggle disabled-state pattern not specified (no "Post-launch" label suffix) |
| NS-3 | `notification-system.md` | GAP | LOW | 63 px height enforcement mechanism via IUIBackend not specified |
| QI-3 | `query-inspector-panel.md` | GAP | LOW | Data refresh frame-count vs. time-based policy ambiguous; FPS-sensitive |
| QI-4 | `query-inspector-panel.md` | PROBLEM | LOW | Third-fallback edge-snap direction unintuitive (panel placed furthest from cursor) |
| RS-1 | `resolution-ui-scaling.md` | GAP | MEDIUM | V1 colorblind gap: hover cursor shows zone letters but placed tile overlays do not |
| RS-2 | `resolution-ui-scaling.md` | GAP | LOW | Accessibility subsection layout within Graphics tab not specified |
| RS-3 | `resolution-ui-scaling.md` / `irrlicht-device-lifecycle.md` | INCONSISTENCY | LOW | setViewportSize() not in the 11-step frame loop spec |
| CF-7 | `notification-system.md` / `hud-layout.md` | INCONSISTENCY | LOW | Normal toast / grace period indicator visual stacking not verified |
| CF-8 | `hud-layout.md` / `input-arbitration.md` | DUPLICATE | LOW | Zone rectangular selection described in both files |
| CC-3 | `camera-controls.md` | GAP | LOW | Edge-scroll and letterbox/pillarbox black bars — spurious scroll in bars not addressed |
| CC-4 | `camera-controls.md` | GAP | LOW | Full CameraController constructor signature not declared |
| FP-2 | `finances-panel.md` | GAP | LOW | Key-repeat cap interaction with direct-entry activation unspecified |
| FP-4 | `finances-panel.md` | GAP | LOW | IAudioSystem forward-declaration pattern not specified for FinancesPanel.h |
| HL-6 | `hud-layout.md` | GAP | LOW | Demand bar visual meaning (direction: full=high unmet demand) not labeled |
| HL-7 | `hud-layout.md` | GAP | LOW | setMouseCursor() IUIBackend method 22 placeholder absent from interface listing |
| HS-4 | `hotkey-scheme.md` | GAP | LOW | WASD preset with pre-existing custom W/A/S/D bindings behavior not cross-referenced |
| MD-4 | `modal-dialog-system.md` | INCONSISTENCY | LOW | Small (480×240) modal content fit for demolish confirmation (title+body+checkbox+2 buttons) not verified |
| MM-1 | `main-menu-new-game-flow.md` | GAP | MEDIUM | Non-keyboard/mouse input not explicitly scoped out |
| NS-4 | `notification-system.md` | DUPLICATE | LOW | dismissCriticalToast API described in both notification-system.md and CLAUDE.md |
| SP-5 | `settings-pause-menu.md` | GAP | LOW | Post-V1 save slot picker has no placeholder in modal-dialog-system.md |
| UM-1 | `ui-manager.md` | GAP | MEDIUM | m_gameSessionActive, m_newGamePending, m_pendingQuit not in UIManager class structure |
| UM-2 | `ui-manager.md` | GAP | LOW | draw() doc comment missing guiEnvironment->drawAll() ordering relationship |
| UM-3 | `ui-manager.md` | DUPLICATE | LOW | Zone overlay ARGB constants defined in both ui-manager.md and hud-layout.md |


---

## 3. 3D Asset Standards Review

**Reviewer:** Senior 3D Model Artist

---

# 3D Model Artist Spec Review — Asset Standards and Graphics Architecture

**Reviewer**: Senior 3D Model Artist (graphics-artist-3d-model)
**Date**: 2026-03-29
**Scope**: All files in `architecture/asset-standards/` and `architecture/graphics-architecture/`
**Purpose**: Gap analysis, consistency check, inconsistency and duplication audit — read-only review, no changes made.

---

## Files Reviewed

- `architecture/asset-standards/3d-model-standards.md`
- `architecture/asset-standards/2d-texture-standards.md`
- `architecture/asset-standards/building-atlas-layout.md`
- `architecture/graphics-architecture/scene-graph-ownership.md`
- `architecture/graphics-architecture/model-validator-tool.md`
- `architecture/graphics-architecture/texture-cache.md`
- `architecture/graphics-architecture/procedural-terrain.md`
- `architecture/graphics-architecture/shader-loading.md`
- `architecture/graphics-architecture/irrlicht-device-lifecycle.md`
- `architecture/graphics-architecture/sky-clouds.md`
- `architecture/graphics-architecture/benchmark-tool.md`

---

## Issue Index

| # | Severity | Type | File(s) | Short Description |
|---|---|---|---|---|
| 1 | CRITICAL | INCONSISTENCY | 3d-model-standards.md | V1 minimum coverage: 18 sets stated in old sign-off but current spec requires 36 |
| 2 | CRITICAL | INCONSISTENCY | 3d-model-standards.md | `kTileSize` stated as 4.0 in sign-off comment but 10.0 in the binding spec |
| 3 | CRITICAL | INCONSISTENCY | 3d-model-standards.md | LOD2 geometry shell tri budget conflict: 300–500 vs 400–600 |
| 4 | HIGH | INCONSISTENCY | 3d-model-standards.md, model-validator-tool.md | Model validator tile boundary overlay uses 10 m but sign-off records 4 m kTileSize |
| 5 | HIGH | INCONSISTENCY | 3d-model-standards.md | Sign-off records car LOD0 <= 1500 / LOD1 <= 300 but binding spec is <=2000 / <=400 |
| 6 | HIGH | INCONSISTENCY | 3d-model-standards.md | Sign-off records bus/truck LOD0 <= 2500 / LOD1 <= 450 but binding spec is <=3000 / <=500 |
| 7 | HIGH | GAP | 3d-model-standards.md | LOD2 shell budget (300–500 tris) in Modular Kit section never reconciled with LOD Requirements table (400–600 tris) for large buildings |
| 8 | HIGH | INCONSISTENCY | 3d-model-standards.md, CLAUDE.md | CLAUDE.md says "10 floors maximum" but spec has an exemption range (15–30) for `com_high_*` — the CLAUDE.md summary omits the exemption |
| 9 | HIGH | MISSING | 3d-model-standards.md | No animation spec at all — the `.b3d` format explicitly supports skinned animation, but no bones/joint/animation-frame spec exists for any asset category |
| 10 | HIGH | GAP | 3d-model-standards.md | Road tile UV tiling (2× per 10 m tile) specified only in a prose note; no named shader constant or validation check for it |
| 11 | HIGH | DUPLICATE | 3d-model-standards.md | The 4-floor billboard/geometry-shell rule is restated at least four separate times in the same file with near-identical wording |
| 12 | HIGH | INCONSISTENCY | 3d-model-standards.md, building-atlas-layout.md | Service buildings: spec says all four share atlas cell (3,2) in one section; building-atlas-layout.md cell table gives each its own row-4 cell (cols 4–7) |
| 13 | MEDIUM | INCONSISTENCY | 3d-model-standards.md | `com_high_*` LOD0 budget described as 7,000–10,000 in the LOD table row but 8,000–10,000 in the variant geometry section |
| 14 | MEDIUM | INCONSISTENCY | 3d-model-standards.md, model-validator-tool.md | Validator says "LOD2 assets — validate in-game at distances > 40 m" but LOD2 switch-in for small buildings is 90–100 m, not 40 m |
| 15 | MEDIUM | INCONSISTENCY | 3d-model-standards.md | LOD2 shell spec for small buildings with height_floors >= 4 is "300–500 tris" in the Modular Kit section but "400–600 tris" in the LOD Requirements table |
| 16 | MEDIUM | GAP | 3d-model-standards.md | No specular/roughness map spec for buildings — only normal map UV channel is specified; no _sp channel assignment, atlas binding, or anisotropy for buildings |
| 17 | MEDIUM | GAP | 3d-model-standards.md | Collision mesh vertical extent rule says "authored at Y=0" but gives no maximum X/Z overhang tolerance relative to the tile footprint |
| 18 | MEDIUM | INCONSISTENCY | 3d-model-standards.md | Carriageway lane-width spec: two 3.6 m lanes + center strip (0.3 m) = 7.5 m, but the two 1.25 m kerb strips bring the total tile to 10 m only if the center-line strip is within the carriageway — this maths is ambiguous |
| 19 | MEDIUM | DUPLICATE | 3d-model-standards.md | Export validation check descriptions for checks #2 and #11 are functionally identical (same condition, different phrasing) — likely meant to be complementary; should be clearly labeled as paired |
| 20 | MEDIUM | GAP | 2d-texture-standards.md | Building facade normal map spec is missing — terrain normal maps are fully specified but no equivalent spec exists for building wall normals (intensity, scale, feature list) |
| 21 | MEDIUM | GAP | 2d-texture-standards.md | No specular/roughness map authoring spec for building facades — `_sp` channel packing format is named in the GL dispatch table but never defined per-asset-type |
| 22 | MEDIUM | INCONSISTENCY | 2d-texture-standards.md | The "Frosted Glass" UI sprite art style is marked superseded by "Glass City" but both styles are fully documented inline — the superseded section is not removed or clearly demoted to archive status |
| 23 | MEDIUM | GAP | building-atlas-layout.md | Road marking atlas spec exists (1024×1024, 4×4 cells) but is not cross-referenced from 3d-model-standards.md — road tile spec makes no mention of the road marking atlas for LOD0 decal binding |
| 24 | MEDIUM | INCONSISTENCY | building-atlas-layout.md, texture-cache.md | Building atlas GL_TEXTURE_MAX_LEVEL: building-atlas-layout.md says level=4 for 4096px (5 mips, levels 0–4); texture-cache.md dispatch table says the same — but the building atlas fallback (2048px) uses level=3 and only 4 mips in one table row but the VRAM budget mentions "4-level mip" for the 2k fallback, which would be GL_TEXTURE_MAX_LEVEL=3, consistent. This is technically consistent but the framing differs between files. |
| 25 | MEDIUM | GAP | model-validator-tool.md | Validator only shows LOD0; LOD1 and LOD2 are validated only "in-game at distances > 40 m" — no structured validator mode or CLI flag for LOD1/LOD2 visual review |
| 26 | MEDIUM | GAP | model-validator-tool.md | Billboard imposter atlases are never displayed by the validator — there is no category or mode to review the 8-frame baked billboard strips at any zoom level |
| 27 | MEDIUM | INCONSISTENCY | model-validator-tool.md | Model placement uses `setScale(10,10,10)` for building nodes — but the current spec says buildings are authored at native world scale with no runtime setScale(). Scale 10 at validator would make a 10 m tile-footprint building appear as a 100 m structure |
| 28 | LOW | GAP | 3d-model-standards.md | No spec for prop assets (street furniture, lamp posts, signs) beyond a tri budget row in the LOD table — no naming convention, UV channel requirements, atlas assignment, or collision mesh policy for props specifically |
| 29 | LOW | GAP | 3d-model-standards.md | No `.meta` schema spec for prop or infrastructure prop assets — only building and vehicle `.meta` fields are defined |
| 30 | LOW | DUPLICATE | 3d-model-standards.md, scene-graph-ownership.md | `LODNode::swapMesh()` bounding box recalculation requirement is described in detail in both files, with slightly different code blocks |
| 31 | LOW | INCONSISTENCY | 3d-model-standards.md | The naming convention stated in CLAUDE.md system prompt (`building_residential_low_lod0.b3d`) uses a different separator style from the binding spec (`res_low_01_lod0.b3d`) — the CLAUDE.md example is outdated |
| 32 | LOW | GAP | 2d-texture-standards.md | No wrap mode spec for building atlas, road marking atlas, or vehicle normal atlas — only billboard atlas (`GL_CLAMP_TO_EDGE`) and terrain (`GL_REPEAT`) are specified |
| 33 | LOW | MISSING | architecture/asset-standards/ | No spec file for road marking atlas art content — what each of the 8 reserved cells should contain, their art style, color values, and alpha channel usage |
| 34 | LOW | GAP | building-atlas-layout.md | The `ground_tarmac` cell (5,4) was listed as "industrial zone default" in the table but the notes column says "variant override, no longer the industrial zone default" — the table header and the notes are in direct contradiction |

---

## Detailed Issue Descriptions

### Issue 1 — CRITICAL — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Sign-off block (2026-02-28), item (20) vs. V1 Minimum Building Coverage section

The 2026-02-28 sign-off comment block states:
> "(20) V1 minimum building coverage (18 sets: 2 variants x 3 zones x 3 tiers) verified."

The V1 Minimum Building Coverage section in the same file states:
> "Artists must deliver a minimum of **36 building sets** across all zone/tier combinations: 4 variants × 3 zones × 3 density tiers = 36 sets total."

These two figures are irreconcilable. 18 = 2 variants × 3 zones × 3 tiers (old spec). 36 = 4 variants × 3 zones × 3 tiers (current spec). The sign-off artifact is permanently embedded in the file and reads as a contradiction to every reader. The sign-off predates the Phase-11e expansion to 4 variants per slot, but it has not been updated.

**Proposed resolution**: Append a correction note to the 2026-02-28 sign-off block stating that item (20) is superseded by the Phase-11e 36-set count, and update item (20) text in-place to read "36 sets (4 variants × 3 zones × 3 tiers)". Alternatively, strike the old value in the sign-off with an inline comment linking to the updated coverage section.

---

### Issue 2 — CRITICAL — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Sign-off block (2026-03-04) vs. World-Space Tile Positioning section

The 2026-03-04 sign-off comment (near line 835) states:
> "kTileSize = 4.0f constexpr float declared in src/rendering/render_constants.h; consistent with 4×4 m road tile mesh and 4×4×3 m modular building kit grid"

The binding World-Space Tile Positioning section (near line 226) states:
> "**`kTileSize` value**: `10.0f` Irrlicht units (10 metres). Each simulation tile occupies a 10 m × 10 m footprint. This is consistent with the road tile LOD budget (road tile mesh = 10×10 m quad)."

And the kTileSize declaration spec says:
> "`kTileSize` is declared as `static constexpr float kTileSize = 10.0f;` directly on `IrrlichtRenderer`"

The sign-off embeds the old value (4.0 m) which was retired when tile size changed to 10 m. Any reader consulting the sign-off gets a contradictory picture of the canonical value. The Modular Building Kit also references "4 m × 4 m × 3 m per floor unit" which describes the module grid step, not the tile size — this is a different concept but adds to the confusion.

**Proposed resolution**: Append a correction note to the 2026-03-04 sign-off block clearly stating that `kTileSize` was updated from 4.0 to 10.0 m after this sign-off was written, referencing the World-Space Tile Positioning section as authoritative. Add a prominent inline note in the Modular Building Kit section distinguishing the 4 m module grid from the 10 m tile size.

---

### Issue 3 — CRITICAL — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: LOD Requirements table vs. Modular Building Kit per-module caps

The LOD Requirements table states for large buildings (general):
> "LOD2 (far): 400–600 tris"

The Modular Building Kit per-module caps section states:
> "LOD2: single hand-authored baked shell mesh (not assembled from modules) — ≤500 tris total for large building (300–500 tris range allows meaningful silhouette features)"

These two ranges do not agree: 400–600 tris vs. 300–500 tris. A budget of 300 tris satisfies the Modular Kit floor but fails the LOD Requirements table floor (400 tris). A budget of 600 tris satisfies the table ceiling but exceeds the Modular Kit ceiling (500 tris). The current validation check #3 references "300–500 tri budget" specifically. If an asset is authored to 550 tris it passes check #3 but violates the LOD Requirements table upper bound.

**Proposed resolution**: Resolve to a single authoritative range. Given that the validation script uses 300–500 and that the table is intended as a guideline while the script is the enforcement mechanism, either (a) update the table to read "300–500 tris" to match the enforced range, or (b) update check #3 to use 400–600 tris to match the table. The chosen range should be documented as binding in exactly one place and cross-referenced from the other.

---

### Issue 4 — HIGH — INCONSISTENCY
**File**: `architecture/graphics-architecture/model-validator-tool.md`
**Location**: Tile Boundary Overlay section

The model validator tile boundary overlay section states:
> "a **red 10×10 m square outline** is rendered on the ground centred on each loaded model slot"

This is consistent with the current 10 m tile size (`kTileSize = 10.0f`). However, model validator building nodes are scaled `10×10×10` (see "Building nodes are scaled 10×10×10 m (same as `IrrlichtRenderer`)"). If buildings are now authored at native world scale (no runtime setScale) per the current 3d-model-standards.md ("No runtime `setScale()` is applied"), then applying `setScale(10, 10, 10)` in the validator would make native-10m-footprint buildings appear as 100 m × 100 m structures, completely overflowing the 10 m tile boundary overlay.

This is internally inconsistent within `model-validator-tool.md` itself and contradicts the native-scale authoring convention in `3d-model-standards.md`.

**Proposed resolution**: The validator building scale should be `1.0` (no scaling) to match the native-scale authoring convention. Update the model-validator-tool.md to remove the `setScale(10,10,10)` statement and confirm the tile overlay correctly represents the building's natural footprint.

---

### Issue 5 — HIGH — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: 2026-02-28 sign-off block item (12) vs. Vehicle Polygon Budget section

The 2026-02-28 sign-off states:
> "car: LOD0 <= 1500 / LOD1 <= 300"

The Vehicle Polygon Budget table states:
> "Car (sedan, hatchback, SUV): LOD0 ≤2,000 tris | LOD1 ≤400 tris"

The sign-off records tighter values (1500/300) than the current binding budget (2000/400). An artist reading the sign-off would under-budget their car models.

**Proposed resolution**: Append a correction note to the sign-off block noting that car budgets were subsequently raised from 1500/300 to 2000/400, citing the Vehicle Polygon Budget table as authoritative.

---

### Issue 6 — HIGH — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: 2026-02-28 sign-off block item (12) vs. Vehicle Polygon Budget section

The 2026-02-28 sign-off states:
> "bus/truck: LOD0 <= 2500 / LOD1 <= 450"

The Vehicle Polygon Budget table states:
> "Bus: ≤3,000 tris LOD0 | ≤500 tris LOD1"
> "Truck: ≤3,000 tris LOD0 | ≤500 tris LOD1"

The sign-off records tighter values (2500/450) than the current binding budget (3000/500). Same class of problem as Issue 5.

**Proposed resolution**: Same approach as Issue 5 — append a correction note to the sign-off block.

---

### Issue 7 — HIGH — GAP
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Modular Building Kit — LOD2 per-module cap

The per-module caps state "LOD2 ≤500 tris total for large building". But the LOD Requirements table row for "Large buildings (general)" specifies "400–600 tris" as the LOD2 budget, with a note explaining why that range is needed:
> "400–600 tris is required to represent building silhouettes (setbacks, rooftop details, entry bays) at the 185–200 m switch-in distance where tall buildings still occupy 50–80 vertical pixels."

A 500 tri cap from the Modular Kit section is below the 600 tri upper bound that the architectural rationale considers achievable. No explanation is given for why the Modular Kit section uses a tighter cap than the LOD Requirements table's upper end allows.

**Proposed resolution**: Reconcile by either (a) raising the Modular Kit cap to 600 to match the table, or (b) explicitly documenting that the LOD2 shell is capped at 500 as a conservative budget choice despite the 600 tris upper bound being technically permitted, and updating the LOD Requirements table ceiling to 500.

---

### Issue 8 — HIGH — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`, `CLAUDE.md`
**Location**: CLAUDE.md "Floor cap: 10 floors maximum" project rule vs. spec exemption

CLAUDE.md (system-level rules) states:
> "**Floor cap**: 10 floors maximum for any building in V1."

`3d-model-standards.md` Modular Building Kit states:
> "**Exemption — `com_high_*` only**: Commercial High skyscraper variants (`com_high_01` through `com_high_04`) are the sole exception to the 10-floor cap. Their `height_floors` must be in the range 15–30."

CLAUDE.md is missing this exemption entirely. The omission is understandable as a summary document, but it creates a genuine contradiction for anyone reading CLAUDE.md as authoritative: the system prompt rules say no building exceeds 10 floors, the spec permits 15–30 for `com_high_*`. Any automated rule-checking or agent acting on CLAUDE.md will incorrectly flag `com_high_*` assets.

**Proposed resolution**: Add "(except `com_high_*` skyscrapers: 15–30 floors)" to the floor cap entry in CLAUDE.md.

---

### Issue 9 — HIGH — MISSING
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Throughout — no animation section exists

The `.b3d` format is described as "Blitz3D format — Irrlicht native, supports multiple UV channels including UV2/lightmap" and is also the format used for all animated and rigged assets. However, there is no animation specification anywhere in the asset standards:

- No bones/joint count per asset type
- No animation clip naming convention (idle, drive, etc.)
- No frame rate or frame count requirements
- No constraint on IK or weighted influences per vertex
- No guidance on whether any V1 building or vehicle uses skeletal animation at all

If all `.b3d` assets are static, this should be explicitly stated. If some vehicles (e.g., wheels) use bone animation, that pipeline is entirely unspecified.

**Proposed resolution**: Add a dedicated "Animation" section to `3d-model-standards.md` that either (a) explicitly states "No V1 asset uses skeletal animation — all `.b3d` files are static meshes" and explains what that means for Blender export settings, or (b) defines the animation pipeline for any asset type that does use animation.

---

### Issue 10 — HIGH — GAP
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Road tile spec

The road tile UV tiling factor (2× per 10 m tile) is described in prose:
> "Road tile UV-channel 0 tiling is specified in the road shader (UV tiles 2× per 10 m road quad — both U and V scale by 2.0 in the vertex shader), not authored per-asset."

There is no named shader constant for this tiling factor, no export validation check for road tile UV, and no cross-reference to the road shader source. If the tiling factor changes for a different road texture, there is no single canonical place to update it and no way for a validator to verify it.

**Proposed resolution**: Define `static constexpr float kRoadUVTilingFactor = 2.0f` in `render_constants.h` alongside `kLaneCenterOffset` and `kCarriagewayHalfWidth`. Add a cross-reference note in `3d-model-standards.md` pointing to that constant. Add a comment to the road shader binding it to that constant.

---

### Issue 11 — HIGH — DUPLICATE
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Multiple sections

The 4-floor billboard/geometry-shell threshold rule is restated at least four times in full:
1. LOD File Naming Convention section (small building paragraph)
2. Billboard LOD note in the same section (longer version)
3. Export validation check #2 description
4. Export validation check #11 description
5. Service Building Model Standards section

Each restatement is nearly word-for-word identical. This creates a maintenance burden: any change to the threshold (e.g., changing it from 4 to 5) requires updates in at least five places, with high risk of inconsistency. The repetition also inflates file size significantly and makes the spec harder to scan.

**Proposed resolution**: Define the rule exactly once in a clearly labeled canonical section (e.g., "Billboard vs. Geometry Shell LOD2 Selection Rule") and replace all other occurrences with a cross-reference: "See LOD2 Selection Rule above" or "(per the 4-floor threshold rule)".

---

### Issue 12 — HIGH — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md` vs. `architecture/asset-standards/building-atlas-layout.md`
**Location**: Service Building Model Standards section vs. Cell Assignment Table

`3d-model-standards.md` Service Building section states:
> "Service buildings share atlas cell (3, 2) for all four types in V1 — they share a common material palette (concrete, glass, utility panels). A second reserved cell (3, 3) is available if a distinct material per service type is required in a later phase."

`building-atlas-layout.md` Cell Assignment Table shows:
> Row 4, col 4: `svc_fire_station` — dedicated cell
> Row 4, col 5: `svc_police_station` — dedicated cell
> Row 4, col 6: `svc_power_plant` — dedicated cell
> Row 4, col 7: `svc_water_tower` — dedicated cell

These are mutually exclusive. One source says all four service buildings share cell (3,2); the other gives each a separate dedicated cell in row 4. The sign-off in `3d-model-standards.md` also references "reserved cells (3,2) and (3,3) for service buildings" which is now superseded by the row-4 assignment but the old text remains.

**Proposed resolution**: Update the Service Building Model Standards section to reference the current row-4 dedicated cell assignments from `building-atlas-layout.md`. Remove the outdated shared-cell (3,2) language. Update the `.meta` sidecar example JSON (currently shows `"atlas_cell": {"row": 3, "col": 2}`) to show the correct per-type cells, or provide a table with one example per type.

---

### Issue 13 — MEDIUM — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: LOD Requirements table row vs. Building Variant Geometry section

LOD Requirements table (Commercial High sub-row):
> "7,000–10,000 tris LOD0"

Building Variant Geometry — Commercial High section:
> "LOD0 target: 8,000–10,000 tris (elevated budget reflecting landmark status)"

The lower bound differs: 7,000 tris (table) vs. 8,000 tris (variant section). An asset with 7,500 LOD0 tris satisfies the table but is below the variant section target.

**Proposed resolution**: Standardize to one range. Since the variant section explicitly justifies the 8,000 lower bound with "landmark status," prefer 8,000–10,000 as the binding range and update the LOD Requirements table row to match.

---

### Issue 14 — MEDIUM — INCONSISTENCY
**File**: `architecture/graphics-architecture/model-validator-tool.md`
**Location**: LOD display section

The validator states:
> "LOD1 (`_lod1.b3d`) and LOD2 (geometry shells `_lod2.b3d` for High-density zones, billboard imposters for Low/Med) are not displayed by the validator — validate LOD2 assets visually in the game at distances > 40 m."

The LOD Distance Thresholds table shows LOD2 switch-in for large buildings is at < 185 m, and for small buildings/props at < 90 m. The guidance "distances > 40 m" is far too close: at 40 m, large buildings would still be in LOD0 and small buildings would still be in LOD0 or only just at LOD1. This is a significant navigation error for artists reviewing their LOD2 assets.

**Proposed resolution**: Update the validator guidance to state "validate LOD2 assets visually in-game at distances > 100 m (small buildings) and > 185 m (large buildings)" to match the actual thresholds.

---

### Issue 15 — MEDIUM — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Modular Building Kit section vs. LOD File Naming Convention section

Modular Building Kit states (for small/prop buildings with `height_floors >= 4`):
> "must ship a `_lod2.b3d` geometry shell (300–500 tris)"

LOD Requirements table states (large buildings general):
> "LOD2 (far): 400–600 tris"

The "small building / props (height_floors >= 4)" row in the LOD Requirements table states:
> "400–600 tris (`_lod2.b3d` geometry shell)"

So a tall small-building asset has the 400–600 tris bound from the LOD Requirements table, but the Modular Kit section says 300–500. This is the same root cause as Issue 3 but affects the small-building category rather than large buildings.

**Proposed resolution**: Same resolution as Issue 3 — pick one authoritative range and cross-reference it from all other locations.

---

### Issue 16 — MEDIUM — GAP
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: UV Channel Convention section

Buildings are specified to use UV channel 0 (diffuse atlas) and UV channel 1 (lightmap). Vehicles are specified to use UV channel 0 only. However, there is no spec for a **specular/roughness map** UV channel for buildings:
- No `_sp` (specular packed) atlas cell is assigned anywhere in the building atlas
- No roughness or metalness pipeline is described for buildings
- The anisotropy spec in `2d-texture-standards.md` lists "specular/roughness maps: minimum 4× anisotropy" and the GL dispatch table has a `_sp` row — but no corresponding artist guidance for building `_sp` authoring

This leaves the building PBR material pipeline incomplete: artists know the diffuse and lightmap channels but have no spec for reflectance data.

**Proposed resolution**: Either (a) explicitly state "V1 building assets use diffuse + lightmap only; no specular/roughness channel is authored for buildings" in the UV Channel Convention section, or (b) define a building specular atlas cell assignment, per-asset naming convention, and `_sp` authoring guidance.

---

### Issue 17 — MEDIUM — GAP
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Collision Meshes section

The collision mesh spec states geometry must be "flat at Y=0" and not exceed 24 triangles. It defines the collision volume as extruded vertically at runtime. However, it gives no tolerance for X/Z overhang beyond the tile footprint. A collision mesh that slightly overhangs into adjacent tiles would cause incorrect road adjacency blocking without any validation error. The 5 mm Y-axis tolerance is specified for floor modules, but no equivalent XZ tolerance is given for collision footprints.

**Proposed resolution**: Add an explicit XZ footprint constraint to the collision mesh spec: "Collision mesh vertices must not exceed the building's tile footprint boundary (±N×5 m for tier N, consistent with the geometry bounds in the Multi-Tile Footprint table). Tolerance: 0.1 m (100 mm)." Add a corresponding validation check (check #21 if needed).

---

### Issue 18 — MEDIUM — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Carriageway width and center-line strip sections

The spec states:
- "The asphalt surface covers **7.5 m** of the 10 m tile width"
- "remaining 1.25 m on each side is rendered as a kerb/verge strip"
- "A 0.3 m wide white painted strip implements a two-way road divider"
- "Left lane (local X = −1.875 m center, 3.6 m wide)"
- "Right lane (local X = +1.875 m center, 3.6 m wide)"

Check: 3.6 m + 3.6 m + 0.3 m center strip = 7.5 m. This sums correctly. But the lane center offsets: if left lane center is at −1.875 m and it is 3.6 m wide, it occupies −3.675 m to −0.075 m. Right lane occupies +0.075 m to +3.675 m. The center strip of 0.3 m would occupy −0.15 m to +0.15 m. Left lane right edge is at −0.075 m, which overlaps with the center strip's left edge at −0.15 m by 0.075 m. This geometry overlaps and the spec's stated lane center offsets do not account for the center-line strip thickness correctly.

**Proposed resolution**: Recalculate lane offsets to account for the center strip: left lane center at −(1.8 + 0.15) = −1.95 m, right lane at +1.95 m, with each lane being exactly 3.6 m wide, leaving 0.3 m for the center strip. Alternatively verify the implementation is correct and update the spec to reflect actual authored vertices.

---

### Issue 19 — MEDIUM — DUPLICATE
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Export validation checks #2 and #11

Check #2:
> "Small building / prop `_lod2.b3d` file presence is floor-count conditional... if `height_floors <= 3`, must NOT have a `_lod2.b3d` file and must have a `_billboard.dds` instead; if `height_floors >= 4`, must have a `_lod2.b3d` geometry shell... and must NOT rely on `_billboard.dds`"

Check #11:
> "Small building / prop assets with `height_floors >= 4` must have a `_lod2.b3d` geometry shell (not just billboard). Conversely, small building / prop assets with `height_floors <= 3` must NOT have a `_lod2.b3d` file — they use point-sprite LOD2 only."

These two checks are testing the same condition from both directions. While having complementary checks is reasonable for a validator, the descriptions are so nearly identical that the intent of having two separate checks is not clear from the spec. If they are intentionally separate checks (e.g., check #2 is about billboard presence and check #11 is about geometry shell presence), the distinction should be clearly stated.

**Proposed resolution**: Add an explicit note at check #2 and check #11 explaining how they differ: "Check #2 validates the billboard DDS presence/absence; check #11 validates the `_lod2.b3d` presence/absence. Both check the same condition (4-floor threshold) but test different file types."

---

### Issue 20 — MEDIUM — GAP
**File**: `architecture/asset-standards/2d-texture-standards.md`
**Location**: Throughout — building texture authoring

The terrain normal map section is exhaustively specified (per-texture intensity, pixel scale, primary features, mip behavior, authoring checklist). But there is no equivalent specification for:
- Building facade normal maps: what intensity ranges are appropriate for glass, brick, concrete?
- Which UV space is the normal authored in (tangent-space, per atlas cell)?
- What mip levels are required for building normal maps?
- Are building normals ever authored (Phase 9 assets exist but no normal map is referenced in the building pipeline)?

If building normals are deferred post-V1, that should be explicitly stated. Currently the reader cannot determine whether building normal maps are expected.

**Proposed resolution**: Add a "Building Normal Map Authoring" subsection that either defines building normal map standards (intensity, format, atlas layout) or explicitly states "V1 building assets do not include normal maps; the building shader uses diffuse + lightmap only."

---

### Issue 21 — MEDIUM — GAP
**File**: `architecture/asset-standards/2d-texture-standards.md`
**Location**: Throughout — specular pipeline for buildings

The `_sp` suffix (specular packed, BC3) appears in the GL_TEXTURE_MAX_LEVEL dispatch table and in the `2d-texture-standards.md` anisotropy section ("Specular/roughness maps: minimum 4× anisotropy"). But there is no:
- Specular/roughness authoring pipeline for buildings
- Atlas cell assignment for building specular data
- Named `_sp` file for any building asset type
- Guidance on channel packing (which channel is roughness, metalness, AO?)

This creates an underspecified material pipeline for buildings.

**Proposed resolution**: Either (a) state explicitly that V1 buildings have no specular map and are rendered with Lambertian + lightmap only, or (b) define the channel packing convention, atlas strategy, and per-asset naming for building specular maps.

---

### Issue 22 — MEDIUM — INCONSISTENCY
**File**: `architecture/asset-standards/2d-texture-standards.md`
**Location**: Frosted Glass vs. Glass City sections

The "Frosted Glass" section ends with:
> "The 'Frosted Glass' art style described above documents the Phase 10 signed-off sprite sheet... and is retained as a historical record. All new icon authoring... must follow the Glass City spec below."

Both styles remain fully documented in the file: Frosted Glass occupies roughly 60 lines and Glass City occupies roughly another 80 lines. The "Superseded Frosted Glass Active-State Values" table at the end lists differences. While keeping historical context is fine, the two styles are interleaved in a way that a new artist authoring icons must determine which guidance applies. The section header for Frosted Glass does not include any "SUPERSEDED" or "ARCHIVED" label.

**Proposed resolution**: Add a bold "**SUPERSEDED — retained for historical reference only — do not use for new authoring**" note at the very top of the Frosted Glass section header so artists cannot accidentally author to the wrong style.

---

### Issue 23 — MEDIUM — GAP
**File**: `architecture/asset-standards/building-atlas-layout.md` vs. `architecture/asset-standards/3d-model-standards.md`
**Location**: Road marking atlas

`building-atlas-layout.md` defines the Road Marking Atlas (1024×1024, DXT5, 4×4 cells, 8 assigned decal types) but `3d-model-standards.md` makes no mention of this atlas when describing road tile mesh authoring. The road tile spec describes the road shader binding `road_asphalt_tileable.dds` but does not state where or how road decals (lane markings, crosswalks, turn arrows) are applied, which UV channel binds the decal atlas, or how decals interact with the road LOD system (e.g., "road marking decals from the road atlas are disabled at LOD2" appears in the LOD table note but the binding mechanism is not described).

**Proposed resolution**: Add a cross-reference from the road tile authoring section in `3d-model-standards.md` to the Road Marking Atlas section in `building-atlas-layout.md`, and specify which texture unit and shader uniform binds the road marking atlas.

---

### Issue 24 — MEDIUM — INCONSISTENCY (minor framing)
**File**: `architecture/asset-standards/building-atlas-layout.md`, `architecture/graphics-architecture/texture-cache.md`
**Location**: Building atlas VRAM budget

`texture-cache.md` dispatch table lists the primary building atlas as GL_TEXTURE_MAX_LEVEL=4 (5 mip levels, levels 0–4, 4096×4096). `building-atlas-layout.md` states "Mip chain: 5-level mandatory (`GL_TEXTURE_MAX_LEVEL = 4`; 4096→2048→1024→512→256)". These are consistent.

However, the VRAM budget table in `2d-texture-standards.md` states:
> "City building atlas — primary (4096×4096 DXT1, 5-level mip) | ≤12 MB (`ceil(4096/4)^2 × 8 × 1.33 ≈ 10.6 MB`)"

And the sign-off in `building-atlas-layout.md` states "4096×4096 DXT1 = 8,388,608 bytes ≈ 8 MB for the primary atlas". The 8 MB figure is for mip level 0 only (base level). With the full 5-level mip chain, the total is ~10.6 MB. The sign-off is citing the base level only, which is correct raw data but misleading in a VRAM context that includes all mip levels.

**Proposed resolution**: Add a clarifying note to the sign-off that the 8 MB figure is for the base mip level only, and the total VRAM including the full 5-level mip chain is approximately 10.6 MB as stated in the VRAM budget table.

---

### Issue 25 — MEDIUM — GAP
**File**: `architecture/graphics-architecture/model-validator-tool.md`
**Location**: Asset categories and display modes

The validator tool only displays LOD0 for all categories. LOD1 and LOD2 are not shown. There is no `--lod 1` or `--lod 2` CLI argument described. For a tool described as "the canonical tool for per-release asset sign-off," the omission of LOD1 and LOD2 visual review is a significant gap. LOD1 silhouette fidelity (e.g., "must retain balcony slab extrusion profile") is a binding spec requirement but has no tooling support.

**Proposed resolution**: Add a `--lod N` command-line argument to the validator (N = 0, 1, 2) that selects which LOD level to display. For LOD2, show geometry shells for High-density and billboard impostors for Low/Med categories, with the billboard rotating to show all 8 frames.

---

### Issue 26 — MEDIUM — GAP
**File**: `architecture/graphics-architecture/model-validator-tool.md`
**Location**: Asset categories

Billboard imposter atlases (`*_billboard.dds`, 28 files per the Phase 11d inventory) are never displayed by the validator. The spec states "validate LOD2 assets visually in the game at distances > 40 m" but billboard atlases should have a direct review path. Bake errors (wrong elevation, incorrect alpha, frame ordering) are only catchable visually but have no tooling support.

**Proposed resolution**: Add a "Billboard" review category to the validator that displays each small building's 8 bake frames as a flat 2D strip on a plane, allowing the operator to check elevation, alpha, and frame count directly.

---

### Issue 27 — MEDIUM — INCONSISTENCY
**File**: `architecture/graphics-architecture/model-validator-tool.md`
**Location**: Model Placement section

The validator states:
> "Building nodes are scaled 10×10×10 m (same as `IrrlichtRenderer`)."

But `3d-model-standards.md` (Native-size authoring convention) states:
> "**No runtime `setScale()` is applied** — `placeBuildingMesh()` and `placeServiceBuildingMesh()` place nodes at scale 1.0."

These contradict each other. If the validator applies `setScale(10, 10, 10)` and the game engine does not, the validator is not exercising the same rendering code path as production, which undermines its purpose as a sign-off tool.

**Proposed resolution**: Update `model-validator-tool.md` to remove the `setScale(10, 10, 10)` statement and confirm that building nodes are placed at `scale = 1.0`, consistent with the game engine's `placeBuildingMesh()`.

---

### Issue 28 — LOW — GAP
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: LOD Requirements table

Infrastructure props (lamp posts, signs) have an entry in the LOD Requirements table (≤300 tris LOD0, ≤75 tris LOD1, Billboard LOD2). But there is no:
- Naming convention for prop assets (`lamp_post_01_lod0.b3d`? or something else?)
- UV channel requirement (lightmap UV required or NOLIGHTMAP?)
- Collision mesh requirement (required for props? 4 m radius threshold applies?)
- `.meta` sidecar requirement (required for all `.b3d` files per check #15, but no `category` value "prop" is defined in the schema)

The category value "prop" appears in the `.meta` schema description (`category: large_building | small_building | prop | vehicle`) but no corresponding prop pipeline exists.

**Proposed resolution**: Add a "Prop and Infrastructure Prop Standards" subsection defining the naming convention, UV requirements, collision mesh policy, and `.meta` schema usage for lamp posts, signs, and other infrastructure props.

---

### Issue 29 — LOW — GAP
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: `.meta` sidecar schema

The `.meta` schema defines `category` as one of: `large_building | small_building | prop | vehicle`. The `vehicle` category is covered by the Vehicle section. `large_building` and `small_building` are covered by the building sections. But the `prop` category has no documented behavior: no LOD strategy, no collision mesh policy, no `height_floors` interpretation. The validation script would read `category: "prop"` and have no policy to apply.

**Proposed resolution**: Document `category: "prop"` behavior explicitly: which LOD strategy applies (same as small_building? NOLIGHTMAP exception?), whether `height_floors` is required, and how the export validation script handles props.

---

### Issue 30 — LOW — DUPLICATE
**File**: `architecture/graphics-architecture/scene-graph-ownership.md`, `architecture/graphics-architecture/procedural-terrain.md`
**Location**: LOD swap bounding box requirement

Both files contain a detailed description of the mandatory bounding box recalculation before `setMesh()`, including code blocks that are nearly identical. `scene-graph-ownership.md` is the canonical home for this rule and `procedural-terrain.md` has its own copy. While cross-referencing with brief code examples is acceptable, the full copy in `procedural-terrain.md` creates a maintenance hazard.

**Proposed resolution**: Replace the full code block in `procedural-terrain.md` with a reference: "See `scene-graph-ownership.md — LOD Swap — Bounding Box Requirement` for the full rule and code sequence." Retain a brief one-line summary in `procedural-terrain.md` for context.

---

### Issue 31 — LOW — INCONSISTENCY
**File**: `CLAUDE.md` (system prompt), `architecture/asset-standards/3d-model-standards.md`
**Location**: Naming convention example in CLAUDE.md

The CLAUDE.md system prompt Project-Specific Rules section states:
> "**Naming convention**: `<type>_<zone>_<tier>_lod<N>.b3d` (e.g. `building_residential_low_lod0.b3d`)"

The binding spec in `3d-model-standards.md` uses:
> "`<zone>_<tier>_<variant>_lod<N>.<ext>` (e.g. `res_low_01_lod0.b3d`)"

The CLAUDE.md example is wrong in two ways: (1) it includes a `building_` prefix that does not exist in the spec, and (2) it uses long zone names (`residential`) instead of the short form (`res`), and omits the mandatory `<variant>` field (`_01` etc.).

**Proposed resolution**: Update the CLAUDE.md naming convention entry to use the correct format from the spec: `<zone>_<tier>_<variant>_lod<N>.b3d` with the example `res_low_01_lod0.b3d`.

---

### Issue 32 — LOW — GAP
**File**: `architecture/asset-standards/2d-texture-standards.md`
**Location**: Wrap mode conventions

`texture-cache.md` specifies that billboard atlases use `GL_CLAMP_TO_EDGE` and non-billboard sRGB atlases use `GL_REPEAT`. But `2d-texture-standards.md` itself does not document wrap mode requirements per texture type. An artist authoring a new atlas has no guidance in the primary texture standards document on wrap mode unless they also read the implementation file.

**Proposed resolution**: Add a "Wrap Mode per Texture Category" table to `2d-texture-standards.md` mirroring the policy in `texture-cache.md`: billboard `_billboard` = CLAMP_TO_EDGE, diffuse atlases and road tileables = REPEAT, splat maps = CLAMP_TO_EDGE (to prevent border bleed between terrain tiles).

---

### Issue 33 — LOW — MISSING
**File**: `architecture/asset-standards/` (no file)
**Topic**: Road marking atlas art content spec

`building-atlas-layout.md` defines the Road Marking Atlas structure (cell count, format, cell assignment table listing 8 types: straight lane, dashed lane, crosswalk, turn arrows left/right/straight, stop line, intersection center). However, there is no art specification for these markings:
- What color are the lane markings (white? yellow?)
- What are the dimensions and proportions of each marking type within the 240×240 px usable cell?
- What is the alpha channel usage (decal mask)?
- What is the correct rendering blend mode (additive? alpha blend over asphalt)?

**Proposed resolution**: Add art content guidance for each of the 8 road marking types to `building-atlas-layout.md` (since it already owns the cell assignment table), or create a new `architecture/asset-standards/road-marking-standards.md` file.

---

### Issue 34 — LOW — INCONSISTENCY
**File**: `architecture/asset-standards/building-atlas-layout.md`
**Location**: Cell Assignment Table, cell (5,4) `ground_tarmac`

The Cell Assignment Table "Notes" column for `ground_tarmac` at (5,4) states:
> "Phase 11f: dark-grey asphalt. Used by industrial building ground quads."

But the Ground Feature Cells section for the same cell states:
> "Zone default: — (variant override) | Artistic tarmac choice for specific variants only (e.g., urban residential forecourts, auto garage forecourts); no longer the industrial zone default"

The "Notes" column in the Cell Assignment Table and the Ground Feature Cells table give contradictory information about whether `ground_tarmac` is the industrial zone default. One says it is used by industrial building ground quads (implying a zone default), the other says it is not the industrial zone default.

**Proposed resolution**: Update the Cell Assignment Table Notes column for `ground_tarmac` to read: "Phase 11f: dark-grey asphalt. Variant override for specific buildings only (e.g. auto garage forecourts); NOT the industrial zone default (which is `ground_paving`)."

---

## Summary by Severity

| Severity | Count |
|---|---|
| CRITICAL | 3 |
| HIGH | 8 |
| MEDIUM | 13 |
| LOW | 10 |
| **Total** | **34** |

## Priority Fixes Before Phase 12 Asset Authoring

The following issues should be resolved before any new asset authoring begins, as they create direct contradictions that will mislead artists:

1. **Issue 1** (CRITICAL) — V1 coverage count in sign-off (18 vs. 36)
2. **Issue 2** (CRITICAL) — `kTileSize` in sign-off (4.0 vs. 10.0)
3. **Issue 3** (CRITICAL) — LOD2 shell budget (300–500 vs. 400–600 tris)
4. **Issue 12** (HIGH) — Service building atlas cell assignment conflict
5. **Issue 27** (MEDIUM) — Model validator applies `setScale(10,10,10)` against the no-scale policy
6. **Issue 31** (LOW) — CLAUDE.md naming convention example is wrong

These six issues will directly mislead any new artist reading the spec for the first time and should be treated as blocking.


---

## 4. 2D Texture Standards Review

**Reviewer:** Senior 2D Texture Artist

---

# Texture Artist Spec Review — AI Town

**Reviewer**: Senior 2D Texture Artist (`graphics-artist-2d-texture`)
**Date**: 2026-03-29
**Files reviewed**:
- `/workspace/architecture/asset-standards/2d-texture-standards.md`
- `/workspace/architecture/asset-standards/building-atlas-layout.md`
- `/workspace/architecture/graphics-architecture/texture-cache.md`
- `/workspace/architecture/graphics-architecture/shader-loading.md`

---

## Summary

The texture specifications are unusually thorough and well-integrated across files. The majority of
runtime formats, upload paths, sRGB correctness, and mip chain rules are precisely specified. The
issues below are genuine gaps, contradictions, or missing content that would block production or
cause silent rendering errors if not resolved. Issues are ordered by severity within each file
grouping.

---

## Issues

---

### A. `2d-texture-standards.md`

---

**[PROBLEM] A-1 — CRITICAL: Resolution Matrix building facade cell description contradicts atlas layout**

The Resolution Matrix (§Resolution Matrix, line 234) states:

> `Building facade atlas cell | 512×512 per module face (wall tile); up to 4 module faces packed per 1024×1024 city building atlas cell`

This contradicts the current atlas architecture. The primary atlas is 4096×4096 with an 8×8 cell
grid at 512×512 px per cell — there is no 1024×1024 city building atlas cell. The parenthetical
"up to 4 module faces packed per 1024×1024 city building atlas cell" is a leftover from the
pre-phase-11e 2048×2048 or 4×4 grid design. The current atlas gives each variant its own 512×512
cell; no 1024×1024 composite cell exists anywhere in the spec.

**Proposed resolution**: Remove the "up to 4 module faces packed per 1024×1024 city building atlas
cell" clause entirely. Replace with: "One 512×512 cell per building variant in the 4096×4096
primary atlas (8×8 cell grid); 256×256 per variant in the 2048×2048 fallback atlas."

---

**[PROBLEM] A-2 — HIGH: `_tileable` suffix is not in the naming convention table but is used as a dispatch key**

The naming convention table (§Naming convention, line 214–226) defines exactly six valid suffixes:
`_d`, `_n`, `_s`, `_sp`, `_lm`, `_billboard`. The `_tileable` suffix is NOT listed. However,
`texture-cache.md` uses `_tileable` as a distinct `loadSRGB()` dispatch suffix row for
`road_asphalt_tileable.dds` (texture-cache.md line 83). The spec also states:
"validate_assets.py must reject any DDS file whose name does not end with one of these six
suffixes" — which means `road_asphalt_tileable.dds` would be rejected by its own validator since
`_tileable` is not in the approved suffix list.

**Proposed resolution**: Either (a) add `_tileable` as a seventh valid suffix to the naming
convention table with its dispatch rule documented, or (b) rename `road_asphalt_tileable.dds` to
`road_asphalt_d.dds` and add a named-exception dispatch rule for it similar to the
`vehicles_sprite_atlas_d.dds` exception. Option (a) is preferred because `_tileable` communicates
authoring intent (seamless repeat) that `_d` does not.

---

**[GAP] A-3 — HIGH: No canonical filename specified for the road marking atlas**

The road marking atlas (1024×1024 DXT5/BC3, 4×4 grid of 16 road decal cells) is fully spec'd
in terms of format, resolution, upload path, and mip chain. However, no canonical DDS filename is
ever stated anywhere in either `2d-texture-standards.md` or `building-atlas-layout.md`.
`validate_assets.py`, the shader binding, and artists all require a known filename. The
`road_asphalt_tileable.dds` road surface texture does have a filename, but the separate road
marking overlay atlas does not.

**Proposed resolution**: Assign and document an explicit filename. Suggested:
`road_markings_d.dds`. Add this to the naming convention and validator checks alongside
`road_asphalt_tileable.dds`.

---

**[PROBLEM] A-4 — HIGH: Road marking atlas upload path contradicts its own texture classification**

The road marking atlas upload path is specified as "linear pool (`IVideoDriver::getTexture()`)"
because "the atlas encodes a decal mask (alpha channel = blending opacity), not diffuse color data"
(§UV & Atlas Strategy). However, `building-atlas-layout.md` §Road Marking Atlas states the format
as "DDS DXT5/BC3 (alpha = decal mask)" and the upload path as "linear (NOT sRGB — decal mask data,
not diffuse color)". While both files agree on linear upload, the road marking atlas also contains
visible road surface color data in its RGB channels (white lane marking color, yellow center lines,
crosswalk stripes). These are perceptual colors that will appear gamma-incorrect if not sRGB-decoded
— identical to the billboard atlas problem the spec explicitly warns about. The current spec treats
road marking color data as "mask data" to justify linear upload, but artists author these RGB values
in a perceptual working space.

**Proposed resolution**: Clarify authoring intent. If road marking RGB channels encode alpha-blend
masks only (R=G=B, grayscale mask intensity), linear upload is correct. If RGB channels encode
actual visible colors (white, yellow), the atlas needs the sRGB upload path. The spec must state
explicitly which is required and add a corresponding `validate_assets.py` check.

---

**[GAP] A-5 — HIGH: No texture specification for building facade normal maps**

The Resolution Matrix specifies `Specular/roughness (_s,_sp) — building facades | 512×512` and
`Normal maps (_n) — all categories | Same resolution as specular/roughness for that category`,
implying building facade normal maps at 512×512. However:

- No building facade normal atlas is specified anywhere. Are building facade normals per-asset
  standalone files or packed into an atlas?
- If standalone per-asset, no naming convention example is given (e.g.,
  `res_low_01_n.dds`).
- If atlased, the building atlas layout file contains zero mention of a normal atlas — it covers
  only the diffuse atlas.
- The `building-atlas-layout.md` sign-off records reference "normal at DXT5nm 256×256 per island"
  (sign-off, line 330) which contradicts the Resolution Matrix's 512×512 for building facade
  normals.
- No `GL_TEXTURE_MAX_LEVEL` dispatch row exists in `texture-cache.md` for building facade normals.
- No VRAM budget line exists for building facade normals.

**Proposed resolution**: Explicitly state whether building facade normals are (a) per-cell within a
separate normal atlas (e.g., `buildings_atlas_n.dds`) or (b) standalone per-variant files. If (a),
add the atlas to `building-atlas-layout.md`. If (b), add a naming convention example and VRAM
budget entry. The sign-off mention of "256×256 per island" must be reconciled with the Resolution
Matrix's "same resolution as specular/roughness" (512×512).

---

**[GAP] A-6 — HIGH: `loading_screen.png` has no texture specification**

`irrlicht-device-lifecycle.md` (line 130) references `assets/textures/ui/loading_screen.png` as a
loading screen asset rendered during startup. An untracked PNG file at this path already exists in
the working tree (listed in git status: `?? assets/textures/ui/loading_screen.png`). No texture
specification exists in any architecture file for this asset:

- No format specified (PNG at what resolution?)
- No color space specified
- No upload path specified (is it loaded via `IVideoDriver::getTexture()`? Direct pixel blit?)
- No mip requirement stated
- No runtime format (should it be PNG, or converted to RGBA8 DDS like the UI sprite sheet?)
- No VRAM budget entry

**Proposed resolution**: Add a `loading_screen.png` specification to `2d-texture-standards.md`
(probably under the UI section). State: resolution (e.g., full target window size or fixed 1920×1080),
color space (sRGB for visible imagery), upload path (PNG via `IVideoDriver::getTexture()` is
simplest), no mip required, and VRAM budget (at 1920×1080 RGBA8 ≈ 8 MB).

---

**[INCONSISTENCY] A-7 — MEDIUM: Normal map checklist uses BC3_UNORM (DXGI 77) but context is ambiguous**

The normal map authoring quality checklist (line 636) states:

> `DDS FourCC: 0x35545844 (DXT5) or DX10 header with BC3_UNORM (DXGI 77)`

The value 77 is correct for `DXGI_FORMAT_BC3_UNORM` (linear BC3). However, earlier in the same
file at line 122, the spec lists `BC3_UNORM_SRGB = DXGI_FORMAT value 78` (for sRGB usage). The
checklist item is technically correct — normal maps should NOT be sRGB, so DXGI 77 is right — but
the checklist does not explain why 77 is correct, leaving artists who read the sRGB validation
section first with potential confusion (they might try to use 78 for all DXT5 files). The `BC3_UNORM
(DXGI 77)` label also does not match the DXT5nm encoding — DXT5nm is a swizzle convention, not a
DXGI format variant. A file produced with Compressonator `-fd BC3` without the sRGB flag will
produce DXGI_FORMAT_BC3_UNORM (77), which is correct, but this is never stated.

**Proposed resolution**: Add an inline note to the checklist item: "DXGI 77 = BC3_UNORM (linear)
— correct for normal maps (not sRGB). Do not use DXGI 78 (BC3_UNORM_SRGB) for normal maps —
sRGB decode corrupts the encoded direction vectors."

---

**[PROBLEM] A-8 — MEDIUM: Terrain normal maps loaded via `loadLinear()` (`IVideoDriver::getTexture()`) cannot have `GL_TEXTURE_MAX_LEVEL` set, but spec requires pre-baked mip chain**

The spec states terrain normal maps require "4 mip levels pre-baked via bicubic downsample" and
that the "driver must not generate additional levels beyond level 3." The `texture-cache.md`
dispatch table confirms `_n` textures use `loadLinear()` with `GL_TEXTURE_MAX_LEVEL = (driver
default)` — meaning `GL_TEXTURE_MAX_LEVEL` cannot be set. The V1 workaround for vehicle normal
atlases (pre-bake exactly 4 mip levels so the driver loads no more than authored levels) is
documented in `building-atlas-layout.md`, but the same constraint for terrain normal maps
(`terrain_grass_n.dds` etc.) is never explicitly stated or pointed to.

**Proposed resolution**: Add a cross-reference note in the terrain normal map section:
"Terrain normal maps use the same mip-capping workaround as the vehicle normal atlas — pre-bake
exactly 4 mip levels into the DDS file header so the driver reads no more than 4 levels. See
`building-atlas-layout.md` §Vehicle Normal Atlas for the V1 workaround rationale."

---

**[GAP] A-9 — MEDIUM: No prop/street-furniture texture authoring spec beyond resolution**

The Resolution Matrix states props/street furniture use 256×256 or 512×512. Beyond this, there is
no authoring spec for prop textures:

- No color palette / style direction
- No named prop texture files
- No spec for whether props use standalone diffuse files or are atlased
- No spec for whether props require normal maps or specular maps
- No upload path stated for prop diffuse (are they sRGB `loadSRGB()` or linear `loadLinear()`?)
- The "Miscellaneous (normal maps, roughness, props, per-type vehicle textures)" VRAM budget line
  allocates 48 MB as a catch-all but provides no per-asset breakdown

**Proposed resolution**: Add a "Props & Street Furniture Textures" subsection. State: standalone
DDS files per prop, sRGB upload path for diffuse, optional normal map, max 256×256 per prop
diffuse, naming convention example.

---

**[INCONSISTENCY] A-10 — MEDIUM: Mip chain description for terrain base textures is internally ambiguous**

The spec states terrain base textures use "4 mip levels (2048→1024→512→256)" in the diffuse
texture spec, but the VRAM budget entry (line 700–701) states "up to 4 layers, 2048×2048 DXT1
with 4-level mip" and the per-file VRAM table (line 682) states "DXT1/BC1, 4 mip ~ 2.8 MB." The
VRAM calculation `ceil(2048/4)² × 8 × 1.33 ≈ 2.8 MB` applies the 1.33× mip overhead factor,
which accounts for a full mip chain — but a full mip chain on a 2048×2048 texture has 12 levels
(down to 1×1), not 4. The 1.33× factor is correct for a full pyramid but the spec caps at 4
levels in the DDS file for `nvcompress` output (when `GL_TEXTURE_MAX_LEVEL = 3` is set at
runtime) and 4 levels exactly for Compressonator output. There is a subtle discrepancy: if the
DDS file contains 12 mip levels (nvcompress output) but only 4 are sampled at runtime, the VRAM
consumed is the full 1.33× overhead (GPU driver loads the full DDS content), not just the 4
active levels. The spec's 2.8 MB figure is therefore correct for nvcompress output (full chain
uploaded, 1.33× overhead), but not for Compressonator output with exactly 4 levels (overhead is
smaller: ~1.25× of mip 0 size). This is a minor budget inconsistency but could mislead artists
computing VRAM per asset.

**Proposed resolution**: Add a note: "VRAM budget uses 1.33× overhead for nvcompress-generated
full-chain DDS files. For Compressonator-generated 4-level files, actual VRAM overhead is
approximately 1.25×, which is slightly under the budget figure — this is acceptable."

---

**[GAP] A-11 — MEDIUM: No specular/roughness texture art direction or content spec**

The spec defines resolution and format for `_s` (grayscale specular) and `_sp` (packed
roughness/metallic/AO) textures but provides zero authoring guidance:

- No channel packing convention for `_sp` (which channel = roughness? metallic? AO?)
- No target roughness ranges per material type
- No metallic flags (is concrete metallic? Is glass metallic?)
- No authoring style direction
- No shader documentation for how `_sp` channels map to lighting model inputs

Without a channel packing convention, multiple artists will produce incompatible files that break
in the lighting shader silently.

**Proposed resolution**: Add a "Specular/Roughness (_sp) Channel Packing Convention" subsection.
Specify: R = roughness, G = metallic, B = AO (or whatever the project chooses). State target ranges
per material category (e.g., concrete: roughness 0.85, metallic 0.0, AO from bake). Cross-reference
the shader that reads these channels.

---

**[PROBLEM] A-12 — MEDIUM: Mip chain byte-size reference table contains incorrect calculation for DXT5 1024×1024**

The DDS Mip Chain Integrity reference table (line 163) states:

> `DXT5/BC3 | 1024×1024 | 4 | 1,392,768 bytes (128 header + (262144 + 65536 + 16384 + 4096) × 1 byte-per-raw)`

The annotation "× 1 byte-per-raw" is misleading/incorrect. DXT5 uses 16 bytes per 4×4 block.

- Mip 0: 1024×1024 = 65536 blocks × 16 bytes = 1,048,576 bytes
- Mip 1: 512×512 = 16384 blocks × 16 bytes = 262,144 bytes
- Mip 2: 256×256 = 4096 blocks × 16 bytes = 65,536 bytes
- Mip 3: 128×128 = 1024 blocks × 16 bytes = 16,384 bytes

Total data: 1,048,576 + 262,144 + 65,536 + 16,384 = 1,392,640 bytes + 128 header = 1,392,768 bytes.

The final total is correct but the per-mip breakdown parenthetical `(262144 + 65536 + 16384 + 4096)`
sums to 348,160 (not 1,392,640). The individual values are wrong — they appear to be DXT1 block
counts (not byte sizes). The correct individual mip byte sizes for DXT5 1024×1024 are
1,048,576 + 262,144 + 65,536 + 16,384 = 1,392,640. The label "× 1 byte-per-raw" is meaningless.

**Proposed resolution**: Fix the per-mip breakdown for the DXT5 1024×1024 row to show correct byte
sizes: `(1,048,576 + 262,144 + 65,536 + 16,384)`. Remove the incorrect "× 1 byte-per-raw"
annotation. Also verify that the DXT1 2048×2048 and DXT5 2048×2048 rows are similarly correct.

---

**[DUPLICATE] A-13 — LOW: DXT5nm swizzle procedure is described three times in the same file**

The DXT5nm packing procedure appears in full or near-full detail in:
1. The Runtime Formats preamble (lines 6–25): full shader unpack GLSL and swizzle steps
2. The DDS Authoring Pipeline section (lines 180–194): 7-step swizzle procedure
3. The Normal Map Y-Channel Convention note (line 66): partial re-statement with Y-flip ordering

Each appearance adds small details the others omit (the Y-flip ordering note in item 3, the 7-step
numbered procedure in item 2). However, this structure creates three sources of truth that can
diverge during edits.

**Proposed resolution**: Keep the full 7-step procedure in §DDS Authoring Pipeline as the canonical
reference. In the Runtime Formats section, replace the detailed repeat with a single-sentence
cross-reference ("For the DDS export pipeline, see §DDS Authoring Pipeline — DXT5nm swizzle
procedure"). In the Y-channel convention note, keep only the Y-flip ordering point (the detail that
is unique there) with a pointer to the canonical procedure.

---

**[GAP] A-14 — LOW: No VRAM budget entry for terrain normal maps in the Scene VRAM Budget table**

The Scene VRAM Budget table (lines 699–713) includes terrain base textures but lists only the
diffuse layer budget (≤11 MB for up to 4 layers). The per-file VRAM table earlier in the file
(lines 678–693) correctly shows terrain normal maps at ~5.6 MB each (4 files × 5.6 MB = 22.4 MB),
and the running total "Total terrain detail texture VRAM: 33.6 MB" is stated below that table.
However, the Scene VRAM Budget table does not have a distinct row for terrain normal maps — the
33.6 MB total is not captured anywhere in the budget table, and the "Miscellaneous" catch-all
(≤48 MB) would need to absorb it. This makes the budget table's total of ≤180 MB unverifiable
against the per-file totals.

**Proposed resolution**: Add a "Terrain normal maps (4 × 2048×2048 DXT5, 4-level mip)" row to the
Scene VRAM Budget table with ≤23 MB budget. Update the running total accordingly. Confirm the grand
total remains within the ≤180 MB ceiling.

---

### B. `building-atlas-layout.md`

---

**[INCONSISTENCY] B-1 — HIGH: Phase 5 sign-off text references obsolete 4×4 grid cell dimensions**

The Phase 5 sign-off block (lines 318–342) states:

> "diffuse at DXT1 256×256 effective per island, normal at DXT5nm 256×256 per island"

This was authored when cells were 512×512 (sub-divided into four 256×256 island slots per the
original shared-cell design). After the phase-11e 8×8 expansion, each variant now occupies a full
512×512 cell with 496×496 usable area. The "256×256 effective per island" figure is now stale.
Artists referencing this sign-off record might incorrectly constrain their UV islands to a quarter
of the available cell space.

**Proposed resolution**: Add a correction note directly under the Phase 5 sign-off block: "UPDATED
phase-11e: effective resolution per UV island is now 496×496 px (full usable area of each 512×512
dedicated cell). The '256×256 effective per island' figure in this sign-off record reflects the
pre-phase-11e shared-cell design and is superseded." Alternatively, strike through the outdated
figure.

---

**[GAP] B-2 — HIGH: Road marking atlas upload path `GL_TEXTURE_MAX_LEVEL` dispatch not specified in `texture-cache.md`**

The road marking atlas (1024×1024 DXT5/BC3, linear upload) has no row in the
`texture-cache.md` `GL_TEXTURE_MAX_LEVEL` dispatch table. `building-atlas-layout.md` states it
uses a 4-level mip chain ("4-level mip chain (1024→512→256→128); clamp at 4 levels via
`GL_TEXTURE_MAX_LEVEL = 3`"), but `texture-cache.md`'s dispatch table does not include a row for
road marking atlases. The linear pool path (`IVideoDriver::getTexture()`) cannot set
`GL_TEXTURE_MAX_LEVEL` — the same constraint that affects `_n` and `_lm` textures applies here.
If the road marking atlas is loaded via the linear pool, the 4-level mip clamp specified in
`building-atlas-layout.md` cannot be enforced at runtime.

**Proposed resolution**: Either (a) add the road marking atlas to the `loadSRGB()` path (resolves
the `GL_TEXTURE_MAX_LEVEL` enforcement gap and is appropriate if RGB channels encode visible color),
or (b) explicitly document that linear-pool upload means the mip clamp is not enforced and the
driver may sample additional mip levels beyond level 3, then confirm this is visually acceptable.
Add a row to the `texture-cache.md` dispatch table for road marking atlas.

---

**[GAP] B-3 — MEDIUM: No normal or specular atlas specification for the building facade pipeline**

`building-atlas-layout.md` is entirely dedicated to the diffuse atlas. The spec mentions building
facade normals in passing (sign-off records reference "normal at DXT5nm 256×256 per island") but
there is no:

- Normal atlas filename
- Normal atlas resolution (4096×4096? Same as diffuse? Separate file?)
- Normal atlas cell layout
- Normal atlas upload path or `GL_TEXTURE_MAX_LEVEL` specification
- Specular/roughness atlas for building facades

If building facade normals and specular maps exist as production assets, they need equivalent
atlas specification. If they are deferred post-V1, that deferral must be explicitly stated in
the file.

**Proposed resolution**: Add a "Building Facade Normal Atlas (V1 status)" section. Either specify
the atlas layout (filename, resolution, cell grid — likely matching the diffuse atlas grid) or
explicitly state "Building facade normal and specular maps are out of scope for V1 — facades use
flat normals (no per-fragment normal mapping) in V1." If deferred, document the future atlas
filename convention so artists know what to produce in later phases.

---

**[INCONSISTENCY] B-4 — MEDIUM: Road marking atlas sRGB vs linear classification conflicts with visual content**

`building-atlas-layout.md` §Road Marking Atlas states upload path is "linear (NOT sRGB — decal
mask data, not diffuse color)" (line 133). The format is DXT5/BC3 with "alpha = decal mask."
However, the road markings spec in `2d-texture-standards.md` describes road marking content as
"white lane markings, crosswalk stripes, turn arrows" — these are visually authored colors
requiring perceptual correctness. Lane marking white and crosswalk white are perceptual colors
that will appear visually darker in linear space than authored in a DCC tool (because DCC tools
work in sRGB by default). This is the same gamma-correctness issue the spec explicitly documents
for billboard atlases.

Note: This is the same root problem as Issue A-4 above, appearing in a different file. Tracking
separately because both files need to be updated.

**Proposed resolution**: Same as A-4 — decide whether road marking RGB channels are grayscale mask
values (linear upload is fine) or perceptual colors (sRGB upload required). Add a clarifying
statement and update upload path consistently in both files.

---

**[MISSING] B-5 — MEDIUM: No specification for the minimap texture or procedural terrain chunk overlay textures**

The minimap system (`architecture/ui-ux/minimap.md`) renders a top-down view of the city.
No texture specification exists for:

- Minimap render target format (what GL texture is the minimap drawn into?)
- Minimap texture dimensions and update frequency
- Whether the minimap texture is a separate atlas or a framebuffer attachment
- Zone color overlay textures (residential green, commercial blue, industrial yellow tints applied
  to the minimap or world view)

These are texture pipeline concerns with VRAM, format, and upload path implications.

**Proposed resolution**: Add a "Minimap Render Target" entry to `2d-texture-standards.md` under
the UI textures section, even if the answer is "512×512 RGBA8 render texture, updated once per
simulation tick, no mip chain, linear upload, not in VRAM budget table (transient framebuffer
attachment)."

---

**[GAP] B-6 — LOW: No cell assignment for road marking atlas content in V1 validator checks**

`validate_assets.py` checks are enumerated for building and vehicle atlas cells. The road marking
atlas (1024×1024, 16 cells) has content specified in the cell assignment table, but no
corresponding `validate_assets.py` check number or validation rule is referenced anywhere in either
atlas spec file. The building atlas cells have explicit check numbers (e.g., check #10 for registry
UV cell assignments, check #12 for vehicle normal atlas UV coordinates). Road marking atlas cells
have no analogous validator check.

**Proposed resolution**: Define a validate_assets.py check for road marking atlas cells (e.g.,
check #16: verify road_marking atlas DDS dimensions = 1024×1024, DXT5/BC3 format, mip count >= 4,
and that no cell assignment references a cell index outside 0–15). Add the check number to the road
marking atlas spec in `building-atlas-layout.md`.

---

### C. `texture-cache.md`

---

**[MISSING] C-1 — HIGH: `loadLinear()` file format constraint not consistently propagated to all `_n` and `_lm` users**

`texture-cache.md` states "`loadLinear()` only accepts formats that Irrlicht's active image loaders
support — PNG, JPG, TGA, BMP" because the Irrlicht DDS loader is disabled. All `_n` (normal map)
and `_lm` (lightmap) textures are loaded via `loadLinear()`. This means runtime `_n.dds` files
CANNOT be loaded through the linear pool path — only PNG, JPG, TGA, or BMP files can be.

However:
- The `2d-texture-standards.md` naming convention says `_n` files are DDS
- The Resolution Matrix references `_n.dds` files
- All terrain normal export commands output `.dds` files
- The GL dispatch table shows `_n | loadLinear() | GL_COMPRESSED_RGBA_S3TC_DXT5_EXT`

This is a fundamental contradiction: normal maps are specced as DDS but `loadLinear()` cannot
load DDS files. The spec resolves this implicitly by saying vehicle normals use the
"pre-bake exactly 4 mip levels" workaround, but that workaround address `GL_TEXTURE_MAX_LEVEL`
control only, not the format loading issue. There is no explicit statement that terrain or building
normal maps must be stored as PNG (non-DDS) if they use the linear pool, or that they must use
a raw-GL path instead.

**Proposed resolution**: Explicitly state the V1 normal map file format. Options: (a) normal maps
are stored as PNG (lossy-free, no block compression) and loaded via `loadLinear()` — simple but
larger files and no DXT5nm compression; (b) normal maps use a third raw-GL upload path similar to
sRGB textures but without sRGB internal format — this requires `TextureCache` expansion. Document
whichever is chosen. If option (a), update the naming convention table to show `_n` files may be
either `.dds` or `.png` with clear guidance.

---

**[MISSING] C-2 — HIGH: No `getSRGBGLuint()` method documented in TextureCache public API**

`shader-loading.md` (line 46) references `textureCache->getSRGBGLuint(filename)` as a method that
"provides the raw `GLuint` for the sRGB texture to bind" in `OnSetConstants()`. This method is
also referenced in the terrain splat shader binding sequence (shader-loading.md lines 107, 108).
However, `texture-cache.md` does not document `getSRGBGLuint()` as part of the `TextureCache`
public API. Only `getSplatMapGLuint()` is documented as an external accessor (texture-cache.md
line 162). The `getSRGBGLuint()` method is effectively undocumented in the canonical TextureCache
spec file.

**Proposed resolution**: Add `getSRGBGLuint(const std::string& filename) const` to the
`texture-cache.md` API section with its contract: "Returns the raw `GLuint` for a loaded sRGB
texture, for binding to a GLSL `sampler2D` uniform in building or terrain shaders. Returns `0`
if the path is not loaded. Must not be called from within `evictUnreferenced()` (eviction-during-
draw safety — same constraint as `evictUnreferenced()` itself)."

---

**[INCONSISTENCY] C-3 — MEDIUM: sRGB pool VRAM estimation applies ×1.33 mip overhead but building atlas has 5 mip levels, not the standard 4**

The VRAM estimation formula for DXT1/BC1 is `ceil(w/4) × ceil(h/4) × 8 × 1.33`. The 1.33 factor
represents a standard 4-level mip pyramid overhead (geometric series 1 + 0.25 + 0.0625 + ... ≈
1.333). The primary building atlas (4096×4096 DXT1) has 5 mip levels (4096→2048→1024→512→256),
not 4. The 5-level overhead factor is 1 + 0.25 + 0.0625 + 0.015625 + 0.00390625 ≈ 1.332, which
is nearly identical to the 4-level case (within 0.001%). So the formula produces the correct result
numerically. However, the spec says "×1.33 mip overhead" without explaining this near-equivalence,
which may cause confusion for developers checking the 5-level atlas calculation.

**Proposed resolution**: Add an inline note: "The 1.33× factor applies to both 4-level and 5-level
mip chains — the fifth level adds only ~0.4% of mip-0 size and is negligible for budget estimation."

---

**[INCONSISTENCY] C-4 — MEDIUM: Dispatch table row for road tileable texture uses `_tileable` suffix but naming convention table does not include it**

The `GL_TEXTURE_MAX_LEVEL` dispatch table in `texture-cache.md` (line 83) has a row:

> `Road tileable texture (sRGB DXT5) | _tileable | loadSRGB() | GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT | 3`

The suffix `_tileable` is used as a dispatch key here. As noted in Issue A-2, this suffix does not
appear in the `2d-texture-standards.md` naming convention table. The dispatch table's use of
`_tileable` as a distinct suffix implies the `TextureCache` is expected to identify this texture
by its suffix, but the naming convention table explicitly forbids any suffix other than the six
listed. These two specifications directly contradict each other.

**Proposed resolution**: Same as A-2. Add `_tileable` to the naming convention table, or document
that `road_asphalt_tileable.dds` is a named exception identified by exact filename (not suffix)
and update the dispatch table row comment accordingly.

---

**[GAP] C-5 — LOW: `loadSplatMap()` PNG decoder does not specify what to do if `createImageFromFile()` returns null**

`texture-cache.md` documents the splat map PNG decoder using `IVideoDriver::createImageFromFile()`.
The documentation shows `IImage* img = m_driver->createImageFromFile(path.c_str())` but does not
specify what `loadSplatMap()` must do if `createImageFromFile()` returns `nullptr` (file not
found, unsupported format, or corrupt PNG). Subsequent calls to `img->lock()` on a null pointer
would crash.

**Proposed resolution**: Add null-check documentation: "If `createImageFromFile()` returns null,
log an error, set `texId = 0`, and return `0`. Do not proceed to `lock()` or `glTexImage2D()`."

---

### D. `shader-loading.md`

---

**[GAP] D-1 — MEDIUM: No specification for building shader texture bindings**

`shader-loading.md` specifies the terrain splat shader's 5-unit binding sequence in detail. No
equivalent specification exists for the building shader — which texture units does the building
fragment shader bind? The building shader samples at minimum: diffuse (unit 0, sRGB raw-GL),
possibly normal map (unit 1), possibly specular/lightmap. Without an authoritative binding spec,
building shader authors may use arbitrary unit numbers that conflict with the terrain shader's
reserved units 4–8 or the billboard unit 9.

**Proposed resolution**: Add a "Building Shader Texture Binding Sequence" section specifying which
texture units the building fragment shader uses (at minimum: unit 0 = diffuse, unit 1 = normal,
unit 2 = specular, unit 3 = lightmap) and confirm these are consistent with `shader_constants.h`.
Note which units require `glActiveTexture`/`glBindTexture` raw binding (sRGB diffuse) vs. Irrlicht
material binding (normal, specular if linear-pool `ITexture*`).

---

**[GAP] D-2 — LOW: No shader file listed for the building/vehicle custom shader**

The GLSL Shader Files section (lines 182–191) lists six mandatory shader files: two for lighting,
two for terrain, two for billboard. No building diffuse/normal shader pair is listed. The road
shader (`road.vert` referenced by name on line 169) is also not in the mandatory shader files list.
The UI quad shaders are noted as Phase 8 additions. This means `validate_assets.py` / CI cannot
verify that building and road shader files exist.

**Proposed resolution**: Add the following to the mandatory GLSL shader files list:
`assets/shaders/building.vert` and `assets/shaders/building.frag`,
`assets/shaders/road.vert` and `assets/shaders/road.frag`,
and add a corresponding CI check (analogous to `ShaderLoadingTest::LightingShaderCompilesWithoutError`).

---

## Cross-File Inconsistencies

---

**[INCONSISTENCY] X-1 — CRITICAL: `2d-texture-standards.md` suffix table and `texture-cache.md` dispatch table have incompatible `_splat` treatment**

`2d-texture-standards.md` naming convention table (line 218–225) lists exactly six suffixes and
states "validate_assets.py must reject any DDS file whose name does not end with one of these six
suffixes." `_splat` is not in this list. However `texture-cache.md` dispatch table (line 85)
includes a `_splat` row as if it is a valid suffix. The `texture-cache.md` text below the dispatch
table clarifies: "`_splat` is NOT a DDS suffix — it is an internal documentation convention."

However, this clarification is only in `texture-cache.md`, not in `2d-texture-standards.md`. The
CLAUDE.md system prompt states the `_splat.png` suffix is valid (under the note "Splat maps: PNG
(exception)"), which implies `_splat.png` is a legitimate runtime file suffix. But:

- `2d-texture-standards.md` says `validate_assets.py` must reject files not ending in the six
  approved suffixes — yet `terrain_blend_splat.png` (or whatever the splat map is named) would
  need a suffix not in that table.
- If splat maps are named `terrain_blend.png` (no `_splat` in the name), the naming convention
  table is consistent. But no canonical splat map filename is stated in either spec.

**Proposed resolution**: (1) Add `_splat.png` (or `_splat`) as a seventh valid recognized suffix
in `2d-texture-standards.md`'s naming convention table, marked as "PNG only — not DDS." (2) State
the canonical splat map filename pattern (e.g., `chunk_<x>_<z>_splat.png` or `terrain_blend.png`).
(3) Confirm `validate_assets.py` will accept PNG files with `_splat` suffix and not reject them
for being PNG instead of DDS.

---

**[INCONSISTENCY] X-2 — HIGH: `building-atlas-layout.md` states primary atlas format is "DDS DXT1 sRGB" but also simultaneously documents a "V1 PNG workaround"**

The document opens with "Format: DDS DXT1 sRGB" (line 11) as the authoritative format, then
immediately below (lines 14–29) documents a "V1 implementation exception" where the on-disk file
is actually `buildings_atlas_d.png` loaded via `IVideoDriver::getTexture()`. The "Production target
(Phase 11+)" clause says DDS migration is pending. This means the stated format and the actual V1
runtime format differ:

- Stated format: DXT1 sRGB DDS
- Actual V1 runtime format: PNG loaded via `IVideoDriver::getTexture()` (linear, uncompressed)

`2d-texture-standards.md` does NOT mention this PNG workaround at all — it consistently describes
`buildings_atlas_d.dds` as the building atlas. A developer reading only the texture standards file
will implement DDS loading; a developer reading only the atlas layout file will see the PNG
exception. The two files are inconsistent on a critical implementation detail.

**Proposed resolution**: Add a matching "V1 PNG workaround" note in `2d-texture-standards.md` in
the building atlas section, cross-referencing the `building-atlas-layout.md` caveat. Mark the
production DDS path clearly as "Phase 11+" and the V1 PNG path as the current implementation.

---

**[INCONSISTENCY] X-3 — MEDIUM: VRAM budget for UI sprite sheet states 16 MB but format differs from system prompt**

`2d-texture-standards.md` §VRAM Budget (line 708) states the UI sprite sheet is "2048×2048 RGBA8,
no mips, ≤16 MB." The CLAUDE.md system prompt states "UI sprite sheet: 2048×2048 RGBA8 UNORM DDS
(exported via `export_textures.py --format rgba8`)." Both agree on format and resolution. However,
`2d-texture-standards.md` §UV & Atlas Strategy (line 263) states the UI sprite sheet "must be
uploaded via `glTexImage2D` with `GL_RGBA8` internal format (not DXT)." The `texture-cache.md`
dispatch table does not contain a row for the UI sprite sheet at all — neither `loadLinear()` nor
`loadSRGB()` nor `loadSplatMap()`. How the UI sprite sheet is loaded at runtime is unspecified in
`texture-cache.md`.

**Proposed resolution**: Add a `TextureCache` method documentation entry (or a note in
`texture-cache.md`) for the UI sprite sheet: "UI sprite sheet is NOT managed by `TextureCache`.
It is uploaded directly by `IrrlichtUIBackend` via a dedicated raw-GL `glTexImage2D` call during
UIManager initialization and is not subject to `TextureCache` eviction." If the UI sprite sheet IS
intended to go through `TextureCache`, add it to the dispatch table.

---

## Issues Not Found / Areas Confirmed Clean

The following areas were reviewed and found internally consistent:

- Billboard atlas format (DXT5 sRGB, 1024×128, 8 frames, mip chain, clamp-to-edge) — fully
  specified and consistent across both files
- Vehicle diffuse atlas, vehicle sprite atlas, vehicle normal atlas — format, resolution, upload
  path, and exception rationale are consistent across `building-atlas-layout.md` and
  `2d-texture-standards.md`
- sRGB upload path for diffuse textures — extension check, GL_EXT_texture_sRGB, fallback shader
  gamma correction — fully specified and consistent
- DXT5nm swizzle pipeline (modulo the duplication noted in A-13) — technically correct and complete
- Splat map upload as uncompressed RGBA8 via `glTexImage2D` — consistent across all three files
- `GL_TEXTURE_MAX_LEVEL` for sRGB pool textures (primary atlas = 4, fallback atlas = 3, billboard =
  3, road tileable = 3) — correctly specified and consistent
- anisotropic filtering tiers and extension check — complete and consistent
- Entity destroy / sRGB release / `evictUnreferenced()` sequence — complete and consistent
- LOD swap sRGB tracking policy — complete and consistent
- `EDT_NULL` guards — comprehensive and consistent across both files
- Normal map Y-convention (OpenGL, green = +Y) — correctly specified
- Power-of-two constraint — stated clearly
- Terrain splat channel-to-material assignment — consistent across spec and shader-loading.md

---

## Issue Count by Severity

| Severity | Count |
|---|---|
| CRITICAL | 2 (A-1, X-1) |
| HIGH | 7 (A-2, A-3, A-4, A-5, A-6, B-2, B-3, C-1, C-2, X-2) |
| MEDIUM | 8 (A-7, A-8, A-9, A-10, A-11, A-12, B-1, B-4, B-5, C-3, C-4, D-1, X-3) |
| LOW | 4 (A-13, A-14, B-6, C-5, D-2) |

*(Note: Issue count in table reflects actual issues listed above; per-row totals may differ from
heading counts due to cross-file issues appearing in both category counts.)*


---

## 5. Audio Design Review

**Reviewer:** Senior Sound Artist

---

# Audio Architecture Specification Review
**Reviewer**: sound-artist-opensoftal
**Date**: 2026-03-29
**Scope**: All files in `/workspace/architecture/audio-architecture/` including `production-briefs/`

---

## Summary

The audio architecture is mature and deeply specified. The majority of issues found are
medium or low severity — primarily authoring gaps, missing CI coverage, and minor internal
inconsistencies. Two HIGH severity issues were identified relating to missing asset QA
tracking documents and an unverified stinger onset/duck timing interaction. No CRITICAL
issues were found.

---

## File-by-File Review

### `audio-asset-formats.md`

**ISSUE-01** — [INCONSISTENCY] — HIGH

**Title**: Ambient bed dual-requirement spec internally contradicts itself on which loop
mechanism the player actually hears.

**Location**: `audio-asset-formats.md`, section "Seamless loop requirement — Ambient beds"
and sub-section "Ambient bed loop authoring — dual-requirement"

**Description**: The spec states in the main bullet: "the runtime seeks to sample 0 (via
`ov_pcm_seek(vf, 0)`) at end-of-file; the pre-baked crossfade in the file eliminates phase
discontinuity." Immediately below it qualifies this with a "Dual-requirement" note: "because
the streaming runtime seeks to sample 0 BEFORE the crossfade tail is reached, bypassing it
entirely. The seamless loop the player hears is produced entirely by the sample-0 boundary,
not the crossfade tail. The 200 ms crossfade tail is a DAW-audition fallback only."

These two statements are in direct conflict. The opening bullet asserts the crossfade
eliminates phase discontinuity as if it participates in the runtime loop; the dual-requirement
clarification correctly states the crossfade tail is never decoded at runtime. An author
reading only the first sentence will misunderstand the runtime loop path.

**Proposed resolution**: Remove or rewrite the first sentence in the main ambient-beds
bullet to not imply the crossfade tail functions at runtime. The opening sentence should
state from the outset that the sample-0 boundary is the only runtime loop mechanism, and
the 200 ms crossfade tail is a DAW-only safeguard.

---

**ISSUE-02** — [GAP] — MEDIUM

**Title**: No CI check specified for ambient bed duration range compliance (90–120 s).

**Location**: `audio-asset-formats.md` / `v1-audio-asset-manifest.md` CI checks table

**Description**: The CI asset validation table covers: music sample rate/channels (check_16),
vehicle engine duration/format (check_17), zone loop duration/format (check_18), zone loop
silence floor (check_21), stinger mono WAV (check_19), and music JSON sidecar (check_14).
There is no CI check that validates ambient bed duration falls within the 90–120 s range
specified in the manifest. An ambient bed authored at 60 s or 140 s would not be caught by
the automated pipeline.

**Proposed resolution**: Add check_22 (or next available number) to `v1-audio-asset-manifest.md`
covering `ambient_*.ogg`: duration must be >= 90.0 s and <= 120.0 s; sample rate must be
44100 Hz; channels must be 2. Assign Phase 10 implementation responsibility.

---

**ISSUE-03** — [GAP] — MEDIUM

**Title**: No CI check for WAV SFX format compliance (mono, PCM, 44100 Hz).

**Location**: `v1-audio-asset-manifest.md` CI checks table, `wav-sfx-production-brief.md`

**Description**: `validate_assets.py` check_19 only covers `stinger_*.wav` mono PCM
verification. None of the 14 non-stinger WAV SFX assets (`sfx_build_place.wav`,
`sfx_fire_alert.wav`, `ui_click.wav`, etc.) are covered by any automated CI check. A
stereo or ADPCM-encoded WAV submitted for any of these would pass all CI gates.

**Proposed resolution**: Add a CI check (e.g., check_20) covering all `*.wav` files in
the audio directory: channels must be 1, audio format tag must be 0x0001 (PCM), sample
rate must be 44100 Hz. This is a superset of check_19 (stingers only) and could replace
it if the stinger check is folded into the broader WAV check. Assign Phase 10.

---

### `audio-occlusion.md`

**ISSUE-04** — [PROBLEM] — MEDIUM

**Title**: `onSourceRecycled()` spec requires acquiring `m_occlusionMutex`, but the caller
is also the pool acquisition path — creating a potential self-deadlock if any future call
site inadvertently holds the mutex before calling `acquireSFXSource()`.

**Location**: `audio-occlusion.md`, "Pool slot recycle — mandatory occlusion state reset" /
"Implementation note" paragraph at bottom

**Description**: The spec states: "The caller must NOT already be holding `m_occlusionMutex`
when calling `onSourceRecycled(i)` — doing so would deadlock." This is a latent danger
because `onSourceRecycled()` is called from pool eviction paths inside `acquireVehicleEnginePair()`
and `acquireSFXSource()`. If any future call site wraps `acquireSFXSource()` with a mutex
that eventually maps to `m_occlusionMutex`, the deadlock fires silently. The spec mentions
the constraint in a single implementation note but does not encode it as a structural
invariant (e.g., a lock-order document or an assert).

**Proposed resolution**: Add a lock-order comment to the `AudioSystem` class definition in
`audio-system.md` that lists `m_streamMutex` and `m_occlusionMutex` as independent mutexes
that must never be held simultaneously (unless explicitly ordered). Document the constraint
in `audio-occlusion.md` as a named invariant: "LOCK ORDER INVARIANT: `m_occlusionMutex`
must never be acquired while holding any other `AudioSystem` mutex."

---

**ISSUE-05** — [GAP] — MEDIUM

**Title**: Occlusion raycast uses simplified `_col.obj` meshes but the spec does not define
how occlusion state is derived when no collision mesh is loaded at a building tile.

**Location**: `audio-occlusion.md`, line 5: "Raycasts performed against simplified
collision-only scene layer (building `_col.obj` meshes + terrain; not full visual geometry)"

**Description**: The spec states raycasts are performed against `_col.obj` meshes. If a
building tile has no `_col.obj` (e.g., during early-game before any buildings are placed,
or if a building model is authored without a collision mesh), the raycast will simply miss.
The source will be treated as unoccluded (gain = 1.0) — which is correct for empty land
but may be incorrect for a built tile with a missing mesh. There is no spec for the
fallback behaviour when a tile's expected collision mesh is absent.

**Proposed resolution**: Add a fallback rule: "If a tile's `_col.obj` is absent or not
loaded, that tile contributes no occlusion to any raycast. The source is treated as
unoccluded for that tile. Log a warning during tile load if a building model has no
accompanying `_col.obj`." This closes the open question explicitly rather than leaving
it to implementation discretion.

---

**ISSUE-06** — [GAP] — LOW

**Title**: No per-source distance check before spending the raycast budget.

**Location**: `audio-occlusion.md`, "Raycast frequency" specification

**Description**: The spec says "only for sources within 100 m of the listener; max 8
raycasts per frame total." The 8-per-frame budget and 100 m distance gate are specified,
but there is no prioritisation rule for which sources get raycast slots when more than 8
sources are within 100 m. At large city sizes with many simultaneous service-event SFX,
the 8-slot budget could be exhausted by distant 99 m sources before nearby 10 m sources
get occlusion updates.

**Proposed resolution**: Add a prioritisation clause: "When more than 8 sources are within
100 m, allocate raycast slots starting with the nearest sources (closest listener distance
first). Sources that are skipped due to budget exhaustion retain their previous occlusion
state until the next raycast slot becomes available."

---

### `audio-system.md`

**ISSUE-07** — [GAP] — HIGH

**Title**: `assets/audio/ambient_bed_qa.md` and `assets/audio/zone_loop_qa.md` are
referenced as Phase 10 mandatory deliverables in both `ambient-bed-production-brief.md`
and `zone-loop-production-brief.md` respectively, but their required content format is
said to be specified in `v1-audio-asset-manifest.md` "Phase 10 QA Delivery Artifacts"
section — and that section does not actually define the format of those two QA documents.

**Location**: `ambient-bed-production-brief.md` line 341–346; `zone-loop-production-brief.md`
lines 256–257; `v1-audio-asset-manifest.md` "Phase 10 QA Delivery Artifacts" section

**Description**: The ambient bed production brief states: "See `architecture/audio-architecture/
v1-audio-asset-manifest.md` Phase 10 QA Delivery Artifacts section for the required format."
The zone loop brief states: "Complete the `assets/audio/zone_loop_qa.md` entry for this
file." However, the Phase 10 QA Delivery Artifacts section in `v1-audio-asset-manifest.md`
defines only the music crossfade demo WAV files and `crossfade_demo_qa.md`. The format
for `ambient_bed_qa.md` and `zone_loop_qa.md` is never defined anywhere in the spec files.
An author following the cross-reference will find nothing.

**Proposed resolution**: Add a "Ambient Bed QA Document Format" and "Zone Loop QA Document
Format" subsection to `v1-audio-asset-manifest.md` "Phase 10 QA Delivery Artifacts".
Specify the minimum required fields for each document (e.g., for ambient bed: filename,
sample-0 gate result, DAW loop cycles count, crossfade tail present, measured LUFS, measured
dBTP, author sign-off name, date).

---

**ISSUE-08** — [GAP] — MEDIUM

**Title**: `IAudioSystem::setSpeed()` is called to notify the audio system of simulation
speed for dawn/dusk collapse, but there is no spec for what `SimSpeed` values are valid
or how the audio system decides which speed thresholds collapse dawn/dusk.

**Location**: `audio-system.md` lines 92–94 (`setSpeed()` method comment), `dynamic-soundscape.md`
dawn/dusk collapse logic

**Description**: `dynamic-soundscape.md` specifies "at simulation speed >= x3, dawn and
dusk transitions are collapsed." The interface method `setSpeed(SimSpeed speed)` is defined,
and `SimSpeed` is aliased to `SpeedMultiplier`. However, the `SpeedMultiplier` enum values
(x1, x2, x3, x10) are defined in `simulation_types.h`, not the audio architecture specs.
The audio architecture specs reference "speed >= x3" but do not enumerate all legal
`SpeedMultiplier` values, nor define what happens with x10 (which is >x3 and should
also trigger collapse). A reader consulting only the audio specs cannot determine the full
set of speed-collapse thresholds.

**Proposed resolution**: Add a table to `dynamic-soundscape.md` listing all `SpeedMultiplier`
values and their dawn/dusk collapse effect: "x1: dawn/dusk crossfades execute normally;
x2: dawn/dusk crossfades execute normally; x3: dawn and dusk collapsed (pre-emptive skip);
x10: dawn and dusk collapsed (pre-emptive skip)."

---

**ISSUE-09** — [DUPLICATE] — LOW

**Title**: The `StingerType` enum values and their coupling to pool source indices are
documented in detail in both `audio-system.md` (lines 24–33) and `source-pool.md` (lines
79–105), with largely overlapping content.

**Location**: `audio-system.md` lines 24–33 forward-declare comment block; `source-pool.md`
lines 79–96

**Description**: Both files explain: CRISIS=55, MILESTONE=56, the intentional coupling to
pool indices, the `triggerStinger()` API safety rule, the required static_assert, and the
post-V1 promotion sequence. `source-pool.md` has the more complete version. The
`audio-system.md` version is a forward-declare context note but duplicates the semantic
explanation unnecessarily.

**Proposed resolution**: Trim `audio-system.md`'s forward-declare comment to a single line
("Values are intentionally equal to pool indices. See source-pool.md for full coupling
rationale and static_assert requirement.") and keep the authoritative definition in
`source-pool.md`.

---

### `dynamic-soundscape.md`

**ISSUE-10** — [GAP] — HIGH

**Title**: Stinger onset timing requirement (0.25 s minimum before peak) is specified
only in `stinger-production-brief.md` and is never cross-referenced from the duck state
machine spec in `dynamic-soundscape.md`.

**Location**: `dynamic-soundscape.md` DUCKING state machine; `stinger-production-brief.md`
"Onset Timing Requirement" section

**Description**: `stinger-production-brief.md` specifies: "The most prominent musical
content of each stinger MUST begin no earlier than 0.25 s into the file. Rationale: the
music duck ramp takes 0.2 s. A stinger whose peak lands at t=0 plays over music still at
full gain, undermining the intended ducked-stinger mix balance."

The duck state machine in `dynamic-soundscape.md` specifies the 0.2 s DUCKING ramp
duration but does not mention the 0.25 s onset authoring requirement. An implementation
team member reading only `dynamic-soundscape.md` when designing the duck trigger will
not see the authoring constraint, and a sound artist reading only the production brief
may not realise the 0.25 s onset is derived from the duck timing in the spec. If the
duck ramp duration is ever changed (e.g., to 0.3 s), the 0.25 s onset minimum must also
change — but the link is implicit.

**Proposed resolution**: Add a cross-reference note to the DUCKING state machine in
`dynamic-soundscape.md`: "NOTE: The 0.2 s duck ramp duration is the basis for the 0.25 s
stinger onset minimum specified in `production-briefs/stinger-production-brief.md`.
Any change to the 0.2 s ramp duration requires a simultaneous update to the stinger onset
minimum in that brief." Mirror this with a back-reference in the production brief.

---

**ISSUE-11** — [GAP] — MEDIUM

**Title**: The music state machine has no defined behaviour for what happens when
`setMusicIntensity()` is called while a music crossfade is already in progress AND the
requested tier matches the incoming (not outgoing) stem.

**Location**: `dynamic-soundscape.md`, "Variant selection policy" and "Interrupted crossfade"

**Description**: The interrupted crossfade spec handles the case where a new crossfade
target differs from the current incoming stem (A→B interrupted by C). The spec for the
no-op guard states: "Calling `setMusicIntensity()` with the tier already active is a no-op."
However, "already active" is ambiguous when a crossfade is in progress. If `AudioSystem` is
crossfading from calm→growth, `m_currentMusicIntensity` has presumably been set to GROWTH
(or is it still CALM until the crossfade completes?). If a new `setMusicIntensity(GROWTH)`
arrives mid-crossfade, is it a no-op because GROWTH is the incoming target, or does it
trigger a new interrupted crossfade because GROWTH is not yet the fully-active stem?

**Proposed resolution**: Add a clarifying note: "The no-op guard compares `intensity`
against `m_pendingMusicIntensity` (the target intensity of any in-progress or queued
crossfade), not `m_currentMusicIntensity` (the intensity of the outgoing stem). If a
crossfade to tier X is already in progress or queued, a new `setMusicIntensity(X)` call
is a no-op."

---

**ISSUE-12** — [GAP] — MEDIUM

**Title**: No spec for how the variant selection (01 vs 02) is reset or preserved when
the game transitions to the main menu and back.

**Location**: `dynamic-soundscape.md` "Variant selection policy"; `audio-system.md`
`transitionToGameplay()` / `transitionToMainMenu()`

**Description**: On `transitionToMainMenu()`, gameplay stems are stopped. On `transitionToGameplay()`,
the first gameplay stem begins with a fresh variant selection. The spec says "randomly
select between variant 01 and 02 ... provided the selected variant is not the currently
playing one (no immediate repeat)." After a main menu round-trip, there is no "currently
playing" gameplay stem, so the no-immediate-repeat rule cannot apply. The spec does not
say whether `m_lastPlayedVariant[]` is reset on transition or preserved. If preserved, the
first stem after returning from the main menu could always play the opposite variant from
the last session — which may be desirable or undesirable depending on intent.

**Proposed resolution**: Specify the reset behaviour explicitly: "On `transitionToGameplay()`,
all per-tier variant-last-played tracking is reset. The first stem of each tier is selected
uniformly at random on the first play in the new session, with no prior-session no-repeat
constraint."

---

**ISSUE-13** — [GAP] — LOW

**Title**: "Crisis audio during nighttime disaster events" section specifies that
`sfx_fire_alert` is still played during nighttime crisis events, but `sfx_fire_alert` is
a CRITICAL-priority positional SFX, not gated by time-of-day music intensity — yet this
is stated in the time-of-day music intensity override section, which may confuse readers
into thinking SFX are also governed by the music override.

**Location**: `dynamic-soundscape.md`, "Time-of-Day Music Intensity Override", "Crisis
audio during nighttime disaster events"

**Description**: The note "A city under crisis at night retains the night Calm ambient bed
and night Calm music stem — the crisis is communicated through notification stingers
(`sfx_fire_alert`, `stinger_crisis`) rather than music intensity escalation" correctly
documents that SFX and stingers are NOT affected by the time-of-day music override. However,
placing this note inside the music intensity override section implies these SFX are somehow
related to that gate, which they are not. `sfx_fire_alert` fires unconditionally on service
failure regardless of time-of-day.

**Proposed resolution**: Move this explanatory note to the top-level "Dynamic Soundscape"
section as a standalone clarification: "SFX events (sfx_fire_alert, sfx_police_alert,
stinger_crisis, etc.) are not gated by the time-of-day music intensity override — they fire
on simulation state transitions regardless of the current time-of-day period."

---

### `streaming-architecture.md`

**ISSUE-14** — [INCONSISTENCY] — MEDIUM

**Title**: The `openStreamOGG` spec mandates calling `alSourceStop` before resetting
`m_samplesQueued`, but `alSourceStop` on a streaming source would also trigger the
starvation recovery check on the audio thread — creating a potential double-refill if the
stop and refill occur on different wakes.

**Location**: `streaming-architecture.md`, "MANDATORY — `openStreamOGG` must flush AL buffer
queue when reopening an active slot"

**Description**: The spec states: "When closing the existing stream, the implementation
MUST call `alSourceStop` and `alSourceUnqueueBuffers` on the slot's source before resetting
`m_samplesQueued = 0`." The starvation recovery spec states: "If source has entered
`AL_STOPPED` AND `stream.m_intentionallyStopped == false`, refill buffers then call
`alSourcePlay()`." When `openStreamOGG` calls `alSourceStop` WITHOUT first setting
`m_intentionallyStopped = true`, the audio thread's next wake will see `AL_STOPPED` and
`m_intentionallyStopped == false` and attempt a spurious starvation recovery on the old
stream before the new stream is ready.

The spec does partially address this with the instruction to set `m_intentionallyStopped = true`
and `alSourceStop()` in the same mutex scope, but the `openStreamOGG` re-open flush code
block does not show `m_intentionallyStopped = true` being set. This is an incomplete
guard.

**Proposed resolution**: Update the `openStreamOGG` re-open flush code block to explicitly
set `stream.m_intentionallyStopped = true` before calling `alSourceStop`, inside the same
`m_streamMutex` lock scope. Add a comment: "Set intentionallyStopped before alSourceStop
to prevent the audio thread from triggering starvation recovery on the old stream."

---

**ISSUE-15** — [GAP] — MEDIUM

**Title**: The spec defines `kSamplesPerBuffer = 64 * 1024 / (2 * 2) = 16384` frames, but
there is no validation check that OGG files decoded for streaming actually produce samples
in multiples of this buffer size. A short OGG (e.g., an ambient bed that is exactly 90.000 s
long) may not fill a buffer exactly at EOF, leaving the last buffer partially filled.
The spec handles EOF via `ov_pcm_seek(vf, 0)` but does not state how partial last-buffer
frames are handled when the file does not align to `kSamplesPerBuffer`.

**Location**: `streaming-architecture.md`, `kSamplesPerBuffer` definition, ambient bed
loop handling

**Description**: `ov_read()` returns a variable number of decoded bytes per call, not
necessarily `kSamplesPerBuffer` frames. The streaming loop presumably reads in a loop until
the buffer is full, but the spec does not explicitly describe this inner decode loop. A
partial buffer at EOF followed by a seek-to-0 and continued reading is implicitly correct
but is never stated — leaving the implementation to guess.

**Proposed resolution**: Add a clarification: "When decoding a buffer slot, call `ov_read()`
in a loop until either `kSamplesPerBuffer` frames have been decoded or EOF is reached. If
EOF is reached mid-buffer (partial fill), call `ov_pcm_seek(vf, 0)` and continue filling
the same buffer from sample 0. The final `alBufferData` call uses only the number of bytes
actually decoded (which may be less than `kSamplesPerBuffer × channels × sizeof(ALshort)`)
for the last buffer before loop wrap."

---

**ISSUE-16** — [INCONSISTENCY] — LOW

**Title**: `streaming-architecture.md` refers to "Pattern A (SPSC queue)" as the primary
pattern with Pattern B (std::vector) as the alternative, but `audio-system.md` Step 1.6
uses `m_preloadQueue.push()` which is SPSC-queue syntax — and `streaming-architecture.md`
notes this inconsistency in a caveat rather than resolving it.

**Location**: `streaming-architecture.md` lines 78–105 "IMPLEMENTER NOTE — `push()` vs
`push_back()` (Pattern B only)"

**Description**: The spec acknowledges the `push()` vs `push_back()` ambiguity between
the two files, but leaves it as an "implementer responsibility to substitute" rather than
aligning the two files. This is a maintainability hazard: future authors editing either
file may not notice the cross-file dependency. The correct approach is to pick one pattern
and use it in both files.

**Proposed resolution**: Choose Pattern A (SPSC queue with `push()`) as canonical, update
`audio-system.md` Step 1.6 to use Pattern A syntax exclusively, and remove the Pattern B
"IMPLEMENTER NOTE" caveat from `streaming-architecture.md` (or keep it only as an
acknowledged alternative with a clear statement that Pattern A is the default).

---

### `spatial-audio.md`

**ISSUE-17** — [GAP] — MEDIUM

**Title**: No reference distance or rolloff factor is specified for `sfx_build_place`,
`sfx_build_demolish`, `sfx_road_build`, and `sfx_earthworks`, which are all positional
3D sources. These assets are not listed in the spatial audio rolloff table.

**Location**: `spatial-audio.md` rolloff table; `wav-sfx-production-brief.md`; `v1-audio-asset-manifest.md`

**Description**: The spatial audio table covers: Traffic/vehicles, Ambient crowd,
Construction/industry, Zone ambient loops, UI/notification sounds, and Service events.
The four placement/construction SFX (`sfx_build_place`, `sfx_build_demolish`, `sfx_road_build`,
`sfx_earthworks`) are positional (`AL_SOURCE_RELATIVE = AL_FALSE`) per the manifest and
WAV SFX brief, yet they appear in none of the rolloff categories. The "Construction /
industry" row in the spatial table is likely intended to cover these (ref=15m, max=120m,
rolloff=1.2), but it is labelled ambiguously — "industry" could be confused with the
industrial zone loop. No source explicitly maps these SFX to a spatial category.

**Proposed resolution**: Rename the "Construction / industry" spatial row to "Placement
and construction SFX (`sfx_build_place`, `sfx_build_demolish`, `sfx_road_build`,
`sfx_earthworks`)" or add an explicit row. Add a row note: "Note: industrial zone ambient
loops use the Zone ambient loops row (ref=30m, max=300m, rolloff=0.6) — not this row."

---

**ISSUE-18** — [GAP] — LOW

**Title**: `spatial-audio.md` does not specify `sfx_intersection_tick` rolloff parameters.

**Location**: `spatial-audio.md` rolloff table; `v1-audio-asset-manifest.md` (intersection_tick)

**Description**: `sfx_intersection_tick` has an "optional distance cull at >80 m" per the
manifest and a cull at >80 m per `vehicle-sfx-production-brief.md`, but it is not in the
spatial audio rolloff table. As a positional WAV source it needs reference distance, max
distance, and rolloff factor defined so the implementation uses consistent values.

**Proposed resolution**: Add `sfx_intersection_tick` to the spatial audio table:
suggested ref=5m, max=80m, rolloff=2.0. The high rolloff reflects the very subtle nature
of the sound and ensures it is essentially inaudible beyond ~40–50m, consistent with the
"very subtle ambient detail" loudness target of −28 LUFS.

---

### `hrtf-initialization.md`

**ISSUE-19** — [GAP] — LOW

**Title**: No spec for HRTF dataset selection when multiple `.mhr` files are present.

**Location**: `hrtf-initialization.md` — HRTF data file section

**Description**: The spec says "ship `default.mhr` (HRTF data file) alongside the OpenAL
runtime." OpenAL Soft supports multiple HRTF datasets. If the OS also has system-level
HRTF datasets installed (common on Linux), and `default.mhr` conflicts with or is
superseded by a system dataset, the HRTF actually applied may differ from the bundled one.
The spec does not specify how to ensure `default.mhr` takes priority, or whether a specific
named dataset must be selected via `ALC_HRTF_ID_SOFT`.

**Proposed resolution**: Add a note: "On Linux, system HRTF datasets may be discovered by
OpenAL Soft before the bundled `default.mhr`. To ensure the bundled dataset is used,
either set `hrtf_paths` in `alsoft.conf` to point to the binary directory, or use the
`ALC_HRTF_ID_SOFT` attribute to select by dataset name. For V1, the default OpenAL Soft
HRTF selection is acceptable — if `ALC_HRTF_STATUS_SOFT` returns `ALC_HRTF_ENABLED_SOFT`,
the dataset in use is suitable regardless of which file it came from."

---

### `source-pool.md`

**ISSUE-20** — [GAP] — MEDIUM

**Title**: The `acquireVehicleEnginePair()` eviction algorithm targets the pair with
"lowest combined priority," but all vehicle pairs are acquired at NORMAL priority — meaning
all pairs have equal combined priority, and the tiebreak (greatest listener distance) is
always the operative criterion. The spec does not clarify that "combined priority" is
effectively always equal for vehicle pairs.

**Location**: `source-pool.md`, `acquireVehicleEnginePair()` step 3

**Description**: The eviction algorithm step 3 states: "select the eviction candidate:
the pair with the lowest combined priority and, as a tiebreak, the greatest average
listener distance squared." Vehicle engine pairs are always acquired at NORMAL priority
(per the SoundPriority enum note: "Traffic/vehicle engine sounds: NORMAL"). Therefore all
`VehiclePairSlot` entries will have `priority = NORMAL = 1`, making the priority comparison
a tautological tie. The eviction logic in practice will always proceed directly to the
distance tiebreak. This is not wrong but could lead an implementer to write an unnecessary
priority comparison step.

**Proposed resolution**: Add a note: "All vehicle pairs are acquired at NORMAL priority;
the combined priority comparison will always be a tie and eviction is determined purely by
the distance tiebreak. The priority field in `VehiclePairSlot` is retained for forward
compatibility with post-V1 multi-priority vehicle types."

---

**ISSUE-21** — [INCONSISTENCY] — MEDIUM

**Title**: The source budget table row for "Reserve" counts 4 sources (3 evictable burst
slots + sources[57]), but the arithmetic note "(55 total evictable − 16 zone − 24 traffic
− 8 service − 4 UI = 3)" is misleading — it implies those 3 sources are permanently
reserved as burst headroom, but they are in fact fully evictable and can be acquired by
any HIGH/CRITICAL caller.

**Location**: `source-pool.md`, Source Budget Allocation table, "Reserve" row

**Description**: The budget table row for "Reserve" includes 3 "evictable-pool burst slots
beyond the named categories" that total to 3 when the named categories are subtracted. These
3 slots are standard evictable SFX pool sources that happen to be in the transient reserve
range ([51..54] minus the 4th = indices [51], [52], [53] that aren't claimed by the 4 UI
slots). Calling them "Reserve" implies they behave like the 4 UI slots that are
soft-reserved via `kTransientReserveStart`, but these 3 are simply unallocated capacity
within the HIGH/CRITICAL range, not a distinct reservation mechanism.

**Proposed resolution**: Rename the row to "Unallocated evictable headroom" and add a
clarifying note: "These 3 sources are normal evictable pool entries within the
HIGH/CRITICAL-accessible range [51..54]. They are not reserved in any programmatic sense —
they represent the unallocated gap between the 4 explicitly-named categories and
kEvictableSFXCount = 55. Any HIGH/CRITICAL-priority sound may acquire them."

---

### `v1-audio-asset-manifest.md`

**ISSUE-22** — [GAP] — MEDIUM

**Title**: The manifest specifies `sfx_fire_alert` fires at "tile desirability ≤ 20 with
`!tile.alertFired`" but `wav-sfx-production-brief.md` specifies a different trigger
condition: "tile desirability falls critically low due to service coverage failure." These
are subtly different conditions — the spec should use one canonical description.

**Location**: `v1-audio-asset-manifest.md` `sfx_fire_alert` row; `wav-sfx-production-brief.md`
Service Alert SFX intro; `service-coverage.md` (referenced indirectly)

**Description**: The manifest says the trigger is desirability <= 20. The WAV SFX brief
intro says "service coverage failure." The WAV SFX brief per-asset spec says: "CRITICAL priority
— they must not be evicted under pool pressure" and "Trigger: `CitySimulation::tick()` on
tile desirability ≤ 20 with `!tile.alertFired`." This is consistent with the manifest, so
the brief intro's "service coverage failure" is an informal description, not a separate
condition. However, having two different phrasings creates reader confusion about whether
both conditions must be true.

**Proposed resolution**: Align the WAV SFX brief intro to match the manifest's precise
trigger: "fire/police alert SFX fire when a tile's desirability score falls to ≤ 20 for
the first time in its current state (`!tile.alertFired`). The exact desirability model
governing when this occurs is defined in `architecture/game-design/service-coverage.md`."

---

**ISSUE-23** — [INCONSISTENCY] — MEDIUM

**Title**: `v1-audio-asset-manifest.md` music duration range specifies "90–180 s" in
the Deep Review Amendment section of `music-production-brief.md` (line 382), but the
manifest itself locks all gameplay stems to exactly 96 s and main menu to exactly 128 s.
There is no "90–180 s range" constraint defined in `v1-audio-asset-manifest.md`.

**Location**: `music-production-brief.md` line 382: "v1-audio-asset-manifest.md requires
all music stems to be 90–180 s"; `v1-audio-asset-manifest.md` locked durations

**Description**: The music production brief's Deep Review Amendment (2026-03-03) states:
"`v1-audio-asset-manifest.md` requires all music stems to be 90–180 s, and 85.33 s is
4.67 s below that floor." This "90–180 s" range constraint does not appear anywhere in
`v1-audio-asset-manifest.md` itself. The manifest specifies exact durations (96.00 s,
128.00 s) with no minimum/maximum range. The "90–180 s" range appears to be an informal
design constraint used to justify the 32→36 bar correction but was never codified in the
authoritative manifest.

**Proposed resolution**: Either add an explicit "Minimum gameplay stem duration: 90 s;
Maximum gameplay stem duration: 180 s" constraint to `v1-audio-asset-manifest.md` (as a
rationale note, since exact durations are already locked), or remove the cross-reference
from the production brief and replace it with: "The SA-3 bar-count lock supersedes any
general duration range; all durations are exact."

---

**ISSUE-24** — [DUPLICATE] — LOW

**Title**: The "Ambient bed JSON sidecar exemption" blockquote in `v1-audio-asset-manifest.md`
is repeated in essentially identical form in three places within the same file and in
`audio-asset-formats.md`, `ambient-bed-production-brief.md`, and the Phase 1 sign-off in
`dynamic-soundscape.md`.

**Location**: `v1-audio-asset-manifest.md` (blockquote above asset table + notes table + after
table); `audio-asset-formats.md` music stem section; `ambient-bed-production-brief.md`
"No JSON Sidecar Required"; `dynamic-soundscape.md` Phase 1 sign-off item 7

**Description**: The exemption rule is stated 5–6 times across the spec files in identical
or near-identical language. While ensuring no author misses it, this level of repetition
creates a maintenance hazard — any future change to the sidecar policy requires updating
all locations simultaneously.

**Proposed resolution**: Retain the full authoritative statement in `audio-asset-formats.md`
(most detailed) and in `v1-audio-asset-manifest.md` (mandatory for the manifest table).
Reduce all other occurrences to a single-sentence cross-reference: "Ambient beds are
exempt from the JSON sidecar requirement — see `audio-asset-formats.md` for the full
exemption specification."

---

### `production-briefs/vehicle-sfx-production-brief.md`

**ISSUE-25** — [GAP] — MEDIUM

**Title**: The vehicle SFX brief specifies OGG encoding at `-q 6` for engine loops, but
`v1-audio-asset-manifest.md` "OGG Vorbis Encoding Quality Minimums" table does not include
a row for vehicle engine loops. The table only covers music stems (-q 8), ambient beds (-q 7),
and zone loops (-q 6).

**Location**: `v1-audio-asset-manifest.md` "OGG Vorbis Encoding Quality Minimums";
`vehicle-sfx-production-brief.md` "Engine Loop Authoring Requirements"

**Description**: Vehicle engine loops (`sfx_vehicle_engine_idle.ogg`,
`sfx_vehicle_engine_move.ogg`) are OGG Vorbis assets with a `-q 6` encoding minimum per
the vehicle SFX brief. This constraint is not reflected in the manifest's OGG encoding
quality table, making the manifest's table incomplete for implementers who rely on it as
the single-source quality reference.

**Proposed resolution**: Add a row to the manifest's OGG encoding quality table:
"Vehicle engine loops (`sfx_vehicle_engine_*.ogg`): `-q 6` minimum (~192 kbps VBR —
same as zone loops; content is mono tonal, -q 6 is sufficient)."

---

**ISSUE-26** — [GAP] — MEDIUM

**Title**: The vehicle SFX brief specifies the idle/move crossblend thresholds as
3 m/s and 8 m/s, but `audio-system.md` `updateVehicleAudio()` comment uses a different
formulation: "idle gain = max(0, 1 − (speedFraction − 0.21) / 0.36)" which implies
thresholds of approximately 2.9 m/s and 7.9 m/s (given max road speed 13.9 m/s). These
round to 3 and 8 m/s but the fractional discrepancy means the formula and the round
numbers are not exactly equivalent.

**Location**: `audio-system.md` `updateVehicleAudio()` comment (0.21 ≈ 3/13.9; 0.36 ≈
5/13.9); `vehicle-sfx-production-brief.md` crossblend table (3 m/s, 8 m/s)
; `dynamic-soundscape.md` Vehicle Engine Audio section (3 m/s, 8 m/s)

**Description**: `dynamic-soundscape.md` Vehicle Engine Audio specifies: "sfx_vehicle_engine_idle
(gain 1.0 at speed < 3 m/s, linearly to 0.0 at speed ≥ 8 m/s)." The `audio-system.md`
formula uses 0.21 and 0.36 which correspond to 3/13.9 ≈ 2.9281... m/s and 5/13.9 ≈
0.3597..., yielding fade-in at ~2.93 m/s not exactly 3.0 m/s and fade-out at ~7.93 m/s not
exactly 8.0 m/s. The vehicle SFX brief uses the round numbers, as does the crossblend
verification table (simulating at "5 m/s mid-blend point"). At the exact 3.0 m/s threshold
boundary the formula and the round numbers diverge slightly.

**Proposed resolution**: Either round the formula constants to exact values (0.2158... → 3/13.9
is fine as a comment; the round numbers are close enough for perceptual purposes), or update
the spec to explicitly note: "The formulas use 0.21 and 0.36 as rounded approximations of
3/13.9 and 5/13.9 respectively. The perceptual crossblend boundary is approximately 3 m/s
and 8 m/s. The formula is authoritative; the round numbers in the production brief and
soundscape spec are approximate documentation for authors."

---

### `production-briefs/stinger-production-brief.md`

**ISSUE-27** — [INCONSISTENCY] — MEDIUM

**Title**: The stinger production brief describes `stinger_crisis` trigger as "fires when a
crisis event is triggered by the simulation (e.g., budget collapse, service failure cascade)"
in the trigger rules section, but the authoritative trigger spec in `dynamic-soundscape.md`
explicitly states it fires only when `consecutive_deficit_months >= 2` is first reached,
NOT on service failure.

**Location**: `stinger-production-brief.md`, "Stinger Trigger Rules — stinger_crisis";
`dynamic-soundscape.md`, "Stinger_crisis trigger specification"

**Description**: The stinger brief's trigger rule says: "Fires when a crisis event is
triggered by the simulation (e.g., budget collapse, service failure cascade)." This is
incorrect — `dynamic-soundscape.md` explicitly states: "`sfx_service_degrade` does NOT
trigger `stinger_crisis` (service degradation is an advisory event; crisis is reserved for
imminent bankruptcy)." And: "stinger_crisis fires in Phase 8 UIManager::update() by direct
polling of `getConsecutiveDeficitMonths()` — NOT on `BudgetDeficitWarn` receipt."

An artist reading only the production brief could conclude that service failure events
trigger `stinger_crisis`, which is incorrect.

**Proposed resolution**: Update the stinger production brief trigger rule to: "Fires when
`consecutive_deficit_months >= 2` is first reached in a deficit streak (the city has been
in deficit for two consecutive months). Trigger is in `UIManager::update()` via direct
polling — NOT on service failure events. Service degradation fires `sfx_service_degrade`
only. See `dynamic-soundscape.md §Stinger_crisis trigger specification` for the full
trigger contract."

---

### `production-briefs/zone-loop-production-brief.md`

**ISSUE-28** — [GAP] — LOW

**Title**: The zone loop brief specifies a minimum duration of 12 s but `validate_assets.py`
check_18 only verifies a maximum cap of 18 s — there is no lower bound check in the CI gate.

**Location**: `v1-audio-asset-manifest.md` check_18 row; `zone-loop-production-brief.md`
locked parameters

**Description**: check_18 tests: "Duration must be <= `kZoneLoopMaxPreloadDurationSeconds`
(18.0 s); file must be mono (1 channel); sample rate must be 44100 Hz." No lower bound is
checked. A zone loop authored at 10 s (below the 12 s minimum) would pass all CI gates.
The 12 s minimum is specified in both the brief and the manifest notes table but is not
automated.

**Proposed resolution**: Update check_18 to also require duration >= 12.0 s. Add
`kZoneLoopMinPreloadDurationSeconds = 12.0f` to `audio_types.h` alongside the existing
max constant, and reference it in check_18.

---

### `production-briefs/ambient-bed-production-brief.md`

**ISSUE-29** — [GAP] — LOW

**Title**: The ambient bed brief specifies `ambient_dawn.ogg` content requires "crickets
fading" from the night bed — but there is no cross-compatibility authoring requirement
between `ambient_night.ogg` insect texture and `ambient_dawn.ogg` opening, analogous to
the day→night direct crossfade compatibility requirement.

**Location**: `ambient-bed-production-brief.md`, `ambient_dawn.ogg` content spec

**Description**: The day→night compatibility requirement is formally documented: "must be
compatible with a direct day→night crossfade at x3 speed; deliver `crossfade_demo_day_to_night.ogg`."
The night→dawn transition also uses a 3 s constant-power crossfade at x1 speed, and the
spec notes the dawn bed should "accept a night→dawn crossfade gracefully." However, no
analogous demo or formal gate is required for the night→dawn crossfade — it is described
only in narrative guidance, without a delivery artifact.

**Proposed resolution**: Either explicitly note that night→dawn crossfade has no demo gate
requirement (as a conscious scoping decision), or add a brief note specifying that the
night→dawn crossfade is informally verified by the author (no committed demo required) and
the acceptance criterion is the author's judgment.

---

## Cross-File Issues

**ISSUE-30** — [MISSING] — MEDIUM

**Title**: No audio specification exists for what happens when `AudioSystem` is constructed
in "silent mode" (alcOpenDevice returns null) and game-level code calls `IAudioSystem` methods.

**Location**: `audio-system.md` constructor note; all files referencing `IAudioSystem` methods

**Description**: `audio-system.md` states: "alcOpenDevice failure: logs warning, sets
`m_deviceLost=true`, returns early (silent mode — all IAudioSystem calls become no-ops)."
This is correctly specified as a "no-op" fallback, but no file details the implementation
contract for each method in silent mode: does `playSound()` return a valid `SoundHandle`
or 0? Does `acquireVehicleEnginePair()` return `{-1, -1}`? If callers do not check for
{-1, -1} (the documented valid failure return), they may log spurious warnings ("Releasing
unknown source pair" from `releaseVehicleEnginePair(-1, -1)` which is specified as a
no-op — but the SoundHandle return from `playSound()` in silent mode is unspecified).

**Proposed resolution**: Add a "Silent mode fallback contract" section to `audio-system.md`:
list each IAudioSystem method and its silent-mode return value:
`playSound()` → returns 0 (invalid handle);
`playPositionalSound()` → returns 0;
`acquireVehicleEnginePair()` → returns {-1, -1};
All void methods → return immediately without action.

---

**ISSUE-31** — [MISSING] — LOW

**Title**: No spec for audio validation on save-game load — if a saved game resumes at
NIGHT time-of-day, the audio system must initialise with the night ambient bed and a Calm
music stem without playing a crossfade from DAY first.

**Location**: `audio-system.md` `transitionToGameplay()` spec; `dynamic-soundscape.md`
time-of-day schedule

**Description**: The `transitionToGameplay()` spec states: "setTimeOfDay() must be called
at least once before transitionToGameplay() is invoked. transitionToGameplay() reads
`m_currentTimeOfDay` to determine which ambient bed to start." This covers the
initialisation contract, but does not address whether the initial ambient bed start uses
a crossfade (from silence/nothing to the ambient bed) or plays immediately at full gain.
If a crossfade is used, the 3 s minimum hold time applies but there is nothing to cross
from (silence → bed), which is a degenerate case. The spec does not clarify whether this
initial "start from silence" is a real crossfade or an immediate gain-set-to-1.0 play.

**Proposed resolution**: Add a note to `transitionToGameplay()`: "The initial ambient bed
start (from silence) is not a crossfade — it is an immediate `alSourcePlay()` with gain
set to 1.0. No crossfade outgoing source exists at start time. The 3 s minimum hold time
begins counting from `alSourcePlay()`, so the first time-of-day transition will queue until
3 s of real wall-clock time has elapsed from session start."

---

## Issues Summary Table

| ID | File(s) | Type | Severity | Title |
|---|---|---|---|---|
| ISSUE-01 | audio-asset-formats.md | INCONSISTENCY | HIGH | Ambient bed loop mechanism self-contradicting |
| ISSUE-02 | audio-asset-formats.md, v1-audio-asset-manifest.md | GAP | MEDIUM | No CI check for ambient bed duration range |
| ISSUE-03 | v1-audio-asset-manifest.md, wav-sfx-production-brief.md | GAP | MEDIUM | No CI check for non-stinger WAV SFX format |
| ISSUE-04 | audio-occlusion.md | PROBLEM | MEDIUM | Mutex lock-order undocumented; latent deadlock risk |
| ISSUE-05 | audio-occlusion.md | GAP | MEDIUM | Undefined behaviour when building has no _col.obj |
| ISSUE-06 | audio-occlusion.md | GAP | LOW | No prioritisation rule for raycast budget allocation |
| ISSUE-07 | v1-audio-asset-manifest.md, ambient-bed-production-brief.md, zone-loop-production-brief.md | GAP | HIGH | ambient_bed_qa.md and zone_loop_qa.md format undefined |
| ISSUE-08 | audio-system.md, dynamic-soundscape.md | GAP | MEDIUM | Speed collapse thresholds not enumerated for x10 |
| ISSUE-09 | audio-system.md, source-pool.md | DUPLICATE | LOW | StingerType coupling rationale duplicated |
| ISSUE-10 | dynamic-soundscape.md, stinger-production-brief.md | GAP | HIGH | Duck ramp / stinger onset timing not cross-referenced |
| ISSUE-11 | dynamic-soundscape.md | GAP | MEDIUM | No-op guard ambiguous during in-progress crossfade |
| ISSUE-12 | dynamic-soundscape.md, audio-system.md | GAP | MEDIUM | Variant selection state on main menu round-trip undefined |
| ISSUE-13 | dynamic-soundscape.md | GAP | LOW | SFX/stinger independence from music override not clearly scoped |
| ISSUE-14 | streaming-architecture.md | INCONSISTENCY | MEDIUM | openStreamOGG flush path missing m_intentionallyStopped guard |
| ISSUE-15 | streaming-architecture.md | GAP | MEDIUM | Partial-buffer decode loop at EOF not specified |
| ISSUE-16 | streaming-architecture.md, audio-system.md | INCONSISTENCY | LOW | push() vs push_back() left as implementer responsibility |
| ISSUE-17 | spatial-audio.md, wav-sfx-production-brief.md | GAP | MEDIUM | Placement SFX rolloff parameters not in spatial table |
| ISSUE-18 | spatial-audio.md, v1-audio-asset-manifest.md | GAP | LOW | sfx_intersection_tick rolloff parameters missing |
| ISSUE-19 | hrtf-initialization.md | GAP | LOW | HRTF dataset selection priority on Linux not specified |
| ISSUE-20 | source-pool.md | GAP | MEDIUM | Vehicle pair eviction priority tie always occurs; undocumented |
| ISSUE-21 | source-pool.md | INCONSISTENCY | MEDIUM | "Reserve" row misleadingly implies reserved mechanism |
| ISSUE-22 | v1-audio-asset-manifest.md, wav-sfx-production-brief.md | GAP | MEDIUM | sfx_fire_alert trigger description inconsistency |
| ISSUE-23 | v1-audio-asset-manifest.md, music-production-brief.md | INCONSISTENCY | MEDIUM | 90–180 s range referenced but not defined in manifest |
| ISSUE-24 | Multiple files | DUPLICATE | LOW | Ambient bed sidecar exemption stated 5–6 times |
| ISSUE-25 | v1-audio-asset-manifest.md, vehicle-sfx-production-brief.md | GAP | MEDIUM | Vehicle engine OGG quality floor missing from manifest table |
| ISSUE-26 | audio-system.md, vehicle-sfx-production-brief.md, dynamic-soundscape.md | INCONSISTENCY | MEDIUM | Idle/move crossblend thresholds: formula vs round numbers |
| ISSUE-27 | stinger-production-brief.md, dynamic-soundscape.md | INCONSISTENCY | MEDIUM | stinger_crisis trigger wrongly includes service failure |
| ISSUE-28 | v1-audio-asset-manifest.md, zone-loop-production-brief.md | GAP | LOW | check_18 missing lower bound (12 s minimum not verified) |
| ISSUE-29 | ambient-bed-production-brief.md | GAP | LOW | Night→dawn crossfade compatibility gate undefined |
| ISSUE-30 | audio-system.md, multiple | MISSING | MEDIUM | Silent mode return value contract not specified per-method |
| ISSUE-31 | audio-system.md, dynamic-soundscape.md | MISSING | LOW | Initial ambient bed start at session load: no crossfade spec |

---

## High/Critical Issue Count: 3 HIGH, 0 CRITICAL
## Medium Issue Count: 15 MEDIUM
## Low Issue Count: 13 LOW

---

*End of review.*


---

## 6. Audio Architecture Review

**Reviewer:** Senior C++ Developer (OpenAL)

---

# Audio Architecture Specification Review
## Senior C++ Developer (OpenAL Soft) — Technical Gap Analysis

**Scope**: All files under `architecture/audio-architecture/`, `architecture/testing/testability-architecture.md`, and `architecture/testing/framework.md`.

**Reviewer perspective**: OpenAL Soft C++ implementation — correctness, thread safety, resource lifecycle, test coverage.

---

## Summary Statistics

| Severity | Count |
|---|---|
| CRITICAL | 7 |
| HIGH | 12 |
| MEDIUM | 9 |
| LOW | 6 |
| DUPLICATE | 5 |

---

## CRITICAL Issues

---

### CRIT-1 — `onSourceRecycled` AL calls made from main thread with no current context guarantee

**File**: `audio-occlusion.md` (§ Pool slot recycle — mandatory occlusion state reset)
**Severity**: CRITICAL

**Description**: The `onSourceRecycled(i)` spec calls `m_fnFilterf`, `alSourcei(AL_DIRECT_FILTER)`, and `alCheckError` from the **main thread** (at SFX pool acquisition time). The spec justifies this with the process-wide context established by `alcMakeContextCurrent(m_context)` in the constructor. However, that process-wide context is only guaranteed to be current until `alcMakeContextCurrent(nullptr)` is called — the destructor teardown sequence calls that in Step 6. During the narrow race window where the main thread calls `onSourceRecycled` while the destructor is executing Step 6 (after the audio thread has joined but before AL resource deletion is complete), the main thread makes AL calls with no current context, producing undefined behaviour.

More importantly, the spec in `audio-occlusion.md` explicitly states "called from the main thread at SFX pool acquisition time" and that `m_occlusionMutex` must be held — but `acquireSFXSource()` is callable at any time including between audio thread join and context destruction. The audio thread join in destructor step 3, followed by `alcMakeContextCurrent(m_context)` in step 3.5, followed by source stop calls — all happen while `acquireSFXSource()` is theoretically still callable from the game loop (which continues until `AudioSystem` is destroyed).

**The real gap**: there is no spec for quiescing callers before destruction begins. The destructor spec (`audio-thread-shutdown.md`) does not address the requirement to stop all main-thread AL callers (including `onSourceRecycled`, `syncListenerToCamera`, `playSound`, etc.) before step 3.5 re-binds the context. If any of these are called concurrently with the destructor, they race against the context teardown sequence.

**Proposed resolution**: Add a spec section to `audio-thread-shutdown.md` describing how the main-thread AL call window is closed: a `m_deviceLost`-style atomic `m_shutdownInitiated` flag must be checked at the top of every main-thread AL path (`onSourceRecycled`, `syncListenerToCamera`, `playSound`, `playPositionalSound`, `acquireSFXSource`). Set `m_shutdownInitiated.store(true)` as the very first destructor step, before `m_stopThread.store(true)`.

---

### CRIT-2 — Starvation recovery `alSourcePlay` must NOT be called under `m_streamMutex` if it can block

**File**: `streaming-architecture.md` (§ Starvation recovery)
**Severity**: CRITICAL

**Description**: The spec mandates that the starvation recovery check and `alSourcePlay()` call occur "in the same lock scope as `alSourceQueueBuffers`" (step 3 of the split-lock pattern, inside `m_streamMutex`). Meanwhile, the spec separately mandates that OGG decoding must occur outside `m_streamMutex` because it is "variable-duration CPU work". However, `alSourcePlay()` calls the AL driver, which on PulseAudio/ALSA can block for a non-trivial duration during buffer flushing. Calling `alSourcePlay()` while holding `m_streamMutex` violates the stated goal of not blocking the main thread on the mutex.

The spec in the starvation recovery section says: "The starvation-recovery check MUST be performed in the same lock scope as `alSourceQueueBuffers`" and then at the end of step 3 — but this creates a contradiction: `alSourceQueueBuffers` is an AL call that can also block on some backends. The issue is that the rationale for "same lock scope" is a TOCTOU race between flag setting and state check, but `alSourcePlay()` is a separate driver call that may block the main thread via the mutex.

**Proposed resolution**: Clarify that the "same lock scope" requirement is correct for the `AL_SOURCE_STATE` check and `m_intentionallyStopped` flag read, but that `alSourcePlay()` must be preceded by `alGetError()` to clear error state before the call. Add a note that the implementation must use a non-blocking `m_streamMutex` try-lock fallback path on the main thread to prevent main-thread stalls if the audio thread is slow. Alternatively, explicitly state that `alSourcePlay()` under `m_streamMutex` is acceptable because the lock is never held across the full decode cycle — only across the AL queue/state calls.

---

### CRIT-3 — `alcCheckError` void* parameter is technically UDR on C-strict compilers

**File**: `error-checking.md` (§ Phase 3 Stub Signature)
**Severity**: CRITICAL

**Description**: The spec permanently freezes `alcCheckError` with a `void*` parameter in the header. While the spec correctly states that `ALCdevice*` converts implicitly to `void*` in C++, the reverse conversion (`reinterpret_cast<ALCdevice*>(device)` in `al_check.cpp`) is technically implementation-defined, not guaranteed, for types that are not standard-layout. OpenAL Soft's `ALCdevice_struct` is an opaque internal struct; the round-trip through `void*` relies on a specific ABI assumption.

More critically, the spec acknowledges the stub is "frozen permanently" but the `al_check.cpp` implementation must `reinterpret_cast` the `void*` back to `ALCdevice*`. If the `IAlcFunctions` seam mock also passes mock device pointers through this `void*` round-trip, and the mock's `ALCdevice` type differs in alignment from OpenAL Soft's, the cast produces undefined behaviour in tests.

**The real gap**: the spec does not specify what happens in tests when `alcCheckError` is called with a mock device pointer. The `IAlcFunctions` seam does not wrap `alcCheckError` (error checking is separate from device/context management), meaning tests that exercise code paths calling `alcCheckError` would call the real `al_check.cpp` implementation with whatever pointer the mock system produced.

**Proposed resolution**: Add a note that `alcCheckError` should be a no-op (early return) when `m_deviceLost == true`, and document explicitly that in headless CI tests `alcCheckError` is not called because all ALC paths gate on `m_deviceLost`. If `alcCheckError` can be called in tests, the spec must provide a safe implementation for the null/mock device case.

---

### CRIT-4 — `m_gameOverFade` / `m_gameOverFadeT` are plain `bool`/`float` but written by main thread, read by audio thread

**File**: `audio-system.md` (§ private members section — game-over fade state)
**Severity**: CRITICAL

**Description**: The spec declares:
```cpp
bool  m_gameOverFade{false};   // set to true by setGameOverState()
float m_gameOverFadeT{0.0f};   // advanced by audio thread dt each wake
```

`setGameOverState()` is a main-thread call. `m_gameOverFade` is written on the main thread and read by the audio thread. `m_gameOverFadeT` is written by the audio thread and read by… the spec says "advanced by audio thread dt each wake, used to compute per-stem gain during fade" — so it appears to be audio-thread only. But `setGameOverState(false)` is described as "resets `m_gameOverFade` and `m_gameOverFadeT`" — so the main thread also writes `m_gameOverFadeT` on the false path, while the audio thread writes it each wake.

Neither `m_gameOverFade` nor `m_gameOverFadeT` are declared as `std::atomic`, creating a C++ data race (UB). The spec declares other cross-thread members as `std::atomic<float>` and `std::atomic<DuckState>` but misses these two. The comment "post-V1 code-path stubs" does not exempt them from the data race if they are declared and visible to both threads.

**Proposed resolution**: Declare `m_gameOverFade` as `std::atomic<bool>` and `m_gameOverFadeT` as `std::atomic<float>` in the spec. Alternatively, add a note that `m_gameOverFade` must be read under `m_streamMutex` on the audio thread and written under `m_streamMutex` on the main thread — but `std::atomic<bool>` is the simpler and more correct approach.

---

### CRIT-5 — `alGenSources` failure is not handled

**File**: `audio-occlusion.md` (§ alGenSources placement requirement), `audio-system.md` (§ constructor sequence Step 1.5)
**Severity**: CRITICAL

**Description**: The constructor sequence spec calls `alGenSources(kTotalSources, m_sources)` followed by `alCheckError("alGenSources")`. `alCheckError` throws `std::runtime_error` on failure. However, `alGenSources` can partially succeed: it fills as many entries of `m_sources` as it can before failing. If the throw path is taken after partial source generation (e.g., 47 of 62 sources allocated before failure), those 47 source handles are leaked — there is no cleanup spec for the partial-generation case.

The spec does not specify that on `alGenSources` failure the already-generated source handles must be freed with `alDeleteSources(partialCount, m_sources)` before throwing. On OpenAL Soft `alGenSources` is documented as atomic (all or nothing on some implementations) but this is not guaranteed across all backends.

**Proposed resolution**: Add a RAII guard or explicit cleanup step to `audio-system.md` constructor sequence: if `alCheckError` after `alGenSources` throws, the implementation must call `alDeleteSources(kTotalSources, m_sources)` before propagating the exception (using `std::fill_if` to delete only non-zero entries). Alternatively, specify that a successful `alGenSources` call on OpenAL Soft is always atomic and document the OpenAL Soft version contract.

---

### CRIT-6 — `cleanupFinishedSFX` and `acquireSFXSource` have an unspecced concurrent access path on `m_sfxSlots`

**File**: `source-pool.md` (§ SFX Pool Thread Safety)
**Severity**: CRITICAL

**Description**: The spec correctly identifies the race on `m_sfxSlots[i].soundId` and introduces `m_sfxVehicleReserved[]` as the fix. However, the spec does not address a second race: `acquireSFXSource()` (main thread) writes `m_sfxSlots[idleIdx]` (the non-atomic slot struct) while `cleanupFinishedSFX()` (audio thread) reads `m_sfxSlots[i]` for the `AL_SOURCE_STATE` query iteration. The vehicle-reserved fix only skips vehicle engine sources; for all other SFX slots, `cleanupFinishedSFX()` reads the slot's `soundId` field (or other fields) to perform its state check, while the main thread may be writing the same struct via `acquireSFXSource()`.

The spec says "replace any read of `m_sfxSlots[i].soundId` with `m_sfxVehicleReserved[i].load()`" but does not specify whether the `SFXSlot` struct is wholly replaced by this pattern or whether other fields (e.g., a `bool occupied` flag, a `SoundHandle`) still require atomic access. If `SFXSlot` has any field read by the audio thread and written by the main thread, that field needs to be `std::atomic` or protected by a mutex.

**Proposed resolution**: The spec must either (a) fully define the `SFXSlot` struct and specify which fields are accessed by which thread, with `std::atomic` for any cross-thread fields; or (b) specify that `cleanupFinishedSFX()` only uses `m_sfxVehicleReserved[i]` and `alGetSourcei(AL_SOURCE_STATE)` (both thread-safe paths) and does NOT read any field of `m_sfxSlots[i]` — making `m_sfxSlots` a purely main-thread data structure with the audio thread accessing only the atomic flag.

---

### CRIT-7 — No spec for device loss recovery during runtime (beyond silent-mode constructor path)

**File**: `audio-system.md`, `audio-thread-shutdown.md`, `streaming-architecture.md`
**Severity**: CRITICAL

**Description**: The spec handles `alcOpenDevice` returning null at construction by entering silent mode. However, there is no spec for runtime device loss — an OpenAL device can be disconnected (e.g., USB headset unplugged, PulseAudio daemon crash) at any point during gameplay. When this happens on OpenAL Soft, AL calls begin silently failing; eventually `alcGetError` returns a device-specific error code. The audio thread may enter an infinite retry loop, or worse, `ov_read` and `alBufferData` calls may succeed (returning data to a silently-failed AL) while the starvation recovery path fires repeatedly.

There is no spec for: detecting device loss during the audio thread wake cycle, setting `m_deviceLost` atomically on detection, and quiescing the audio thread cleanly after device loss. The `m_deviceLost` flag is described as a constructor-time guard only; no runtime detection path is specified.

**Proposed resolution**: Add a new spec section `## Runtime Device Loss Detection` to `audio-system.md` specifying: (1) after each `alBufferData` or `alSourceQueueBuffers` call in `updateStreams()`, check `alcGetError(m_device)` for `ALC_INVALID_DEVICE`; (2) on detection, set `m_deviceLost.store(true)`, log an error, and break out of the streaming loop; (3) from that point all IAudioSystem calls become no-ops via the `m_deviceLost` gate; (4) the audio thread exits cleanly without joining-deadlock.

---

## HIGH Issues

---

### HIGH-1 — `alCheckError` in `onSourceRecycled` is called from main thread but spec calls `alCheckError_real` (throws)

**File**: `audio-occlusion.md` (§ Pool slot recycle)
**Severity**: HIGH

**Description**: The `onSourceRecycled` code snippet calls `alCheckError("alFilterf(AL_LOWPASS_GAIN) in onSourceRecycled")` — which resolves to `alCheckError_real` per the error-checking spec, and throws `std::runtime_error` on failure. However, `alCheckError_real` is designed to be called on the audio thread (where throws can be caught by the streaming loop). If `onSourceRecycled` is called on the main thread and throws, the exception propagates up through `acquireSFXSource()` → caller code in the main game loop. The spec does not specify a catch handler at any call site of `acquireSFXSource()` or `releaseVehicleEnginePair()`.

This is particularly dangerous because it means an EFX driver error during slot recycle (e.g., filter object corrupted after partial EFX allocation failure) would terminate the game with an unhandled exception.

**Proposed resolution**: Specify that `onSourceRecycled` must use a `try/catch` around EFX calls and log errors rather than propagating throws, since it runs on the main thread in a context where exception propagation is not safe. Alternatively, change the EFX calls in `onSourceRecycled` to use `alGetError()` directly with explicit error logging rather than the throwing wrapper.

---

### HIGH-2 — HRTF initialization: `alcCheckError` called after `alcMakeContextCurrent` returns `ALC_FALSE` does not throw in the documented code path

**File**: `hrtf-initialization.md`
**Severity**: HIGH

**Description**: The code snippet shows:
```cpp
if (!alcMakeContextCurrent(context_)) {
    alcCheckError(device_, "alcMakeContextCurrent");
    throw std::runtime_error("AudioSystem: alcMakeContextCurrent failed");
}
```

The `alcCheckError` call here is redundant — if `alcMakeContextCurrent` returned `ALC_FALSE`, `alcCheckError` will call `alcGetError(device_)`. Per OpenAL Soft behaviour, `alcMakeContextCurrent` returning `ALC_FALSE` does not always set a device error — the error state may be `ALC_NO_ERROR` even when the function fails (e.g., when the context is already current on another thread). This means `alcCheckError` may NOT throw, and execution falls through to the explicit `throw std::runtime_error(...)`. This is correct behaviour, but the code pattern implies `alcCheckError` is expected to throw and the explicit throw is a fallback — which is the wrong mental model. If the programmer later removes "the redundant explicit throw", the path silently changes from always-throws to sometimes-does-not-throw.

Additionally, the `hrtf-initialization.md` code uses `context_` (trailing underscore naming) while `audio-system.md` uses `m_context` (m_ prefix). This is a naming inconsistency within the spec.

**Proposed resolution**: Remove the `alcCheckError` call from this specific path and keep only the explicit `throw`. Add a spec note explaining that `alcMakeContextCurrent` failure is explicitly thrown regardless of `alcGetError` state. Standardize naming to `m_context`/`m_device` throughout `hrtf-initialization.md` to match `audio-system.md`.

---

### HIGH-3 — `syncListenerToCamera` must check `m_deviceLost` before making AL calls

**File**: `spatial-audio.md` (§ Listener sync)
**Severity**: HIGH

**Description**: `syncListenerToCamera()` calls `alListener3f` and `alListenerfv` from the main thread. The spec documents thread safety extensively (process-wide context, permanent binding) but does not specify what happens when `m_deviceLost == true`. In silent mode (device absent or lost), the main thread has no current AL context, so these AL calls would fail silently or produce `AL_INVALID_OPERATION` errors. More critically, during destruction (after step 6: `alcMakeContextCurrent(nullptr)`), if `syncListenerToCamera()` is still being called by the game loop, it makes AL calls with no context.

The spec says `syncListenerToCamera` is called "once per frame from the main thread after Irrlicht updates the camera" but does not specify a guard condition.

**Proposed resolution**: Add to the `syncListenerToCamera()` spec: "Must check `m_deviceLost` at entry and return early without any AL calls when `m_deviceLost == true`." This is consistent with the pattern used for all other IAudioSystem methods in silent mode.

---

### HIGH-4 — `computeNextBarBoundary` uses `double` arithmetic but `spb` is cast from `double` to `uint64_t` with potential loss

**File**: `dynamic-soundscape.md` (§ Beat-boundary synchronization)
**Severity**: HIGH

**Description**: The spec shows:
```cpp
uint64_t spb = static_cast<uint64_t>((sr * 60.0 / bpm) * beatsPerBar);
```

At 90 BPM / 4 beats per bar / 44100 Hz: `(44100 * 60.0 / 90.0) * 4 = 44100 * 0.6667 * 4 = 117600.0`. This is exact. However, for other BPM values or fractional beat-per-bar counts, the `static_cast<uint64_t>` truncates fractional samples. Over 36 bars (96 s at 90 BPM), an error of even 0.5 samples per bar accumulates to 18 samples — negligible (~0.4 ms at 44100 Hz) and perceptually irrelevant. But the spec locks BPM to 90 and beatsPerBar to 4, so this is only a MEDIUM concern for V1.

More importantly, `computeNextBarBoundary` takes `uint32_t sr` but `m_samplesQueued` is `uint64_t`. In `computeSamplesPlayed`, `samplesQueued - queued` is a `uint64_t` subtraction — if `samplesQueued < queued` the underflow protection `? samplesQueued - queued : 0` is correct. However the guard only checks if `samplesQueued > queued`, which is strictly greater than. When they are equal (exactly `kNumBuffers` buffers queued, none played yet), `samplesPlayed = 0` — correct behaviour. This edge case is handled but only by accident of the `>` vs `>=` choice.

Additionally: the spec says `bpm` parameter is `float` in the function signature, but `sr` is `uint32_t`. The computation `sr * 60.0 / bpm * beatsPerBar` mixes unsigned integer and floating-point arithmetic — if `sr` overflows `uint32_t` during promotion to double, the result is wrong. At 44100 Hz this is not a problem, but the spec should document the type requirements.

**Proposed resolution**: Specify the exact types for all parameters of `computeNextBarBoundary`. Add a note that this formula is only valid for the V1 locked BPM/rate values and document the accumulated drift calculation showing it is negligible for V1 play sessions.

---

### HIGH-5 — Interrupted crossfade spec uses `t_offset = (2/π) × arccos(current_gain_out)` but this can produce `NaN` when `current_gain_out > 1.0`

**File**: `dynamic-soundscape.md` (§ Interrupted crossfade)
**Severity**: HIGH

**Description**: The interrupted crossfade formula `t_offset = (2/π) × arccos(current_gain_out)` is mathematically correct when `current_gain_out ∈ [0.0, 1.0]`. However, the spec does not specify clamping of `current_gain_out` before computing `arccos`. If any floating-point rounding produces a gain slightly above `1.0f` (which can happen with `sin(t × π/2)` at `t=1.0f` due to IEEE 754 precision), `arccos` returns `NaN`. `NaN` propagated into `m_musicCrossfadeT` causes all subsequent crossfade gain updates to produce `NaN` gains on sources, which OpenAL Soft handles as implementation-defined (often produces `AL_INVALID_VALUE` or ignores).

The spec also states "current_gain_out refers to stem B's current gain AT THE MOMENT OF INTERRUPTION — i.e., stem B's gain_in value from the interrupted crossfade (`sin(interrupted_t × π/2)`)". This is the incoming stem's gain becoming the outgoing gain. The `sin` function at `t = 1.0f` should return exactly `1.0f`, but floating-point `sin(π/2)` is not exactly `1.0f` on all implementations.

**Proposed resolution**: Specify `current_gain_out = std::clamp(current_gain_out, 0.0f, 1.0f)` before `arccos`. Add this clamp to the interrupted-crossfade spec pseudo-code. Also add clamping of `t_offset` to `[0.0f, 1.0f]` after the arccos computation.

---

### HIGH-6 — `updateVehicleEngines` on audio thread calls `alSourcef(AL_GAIN)` and `alSourcef(AL_PITCH)` but no `alCheckError` requirement is specified

**File**: `audio-system.md` (§ updateVehicleAudio method spec), `dynamic-soundscape.md` (§ Vehicle Engine Audio)
**Severity**: HIGH

**Description**: The spec for `updateVehicleAudio` says the audio thread reads the atomic fields and applies `AL_PITCH`, `AL_GAIN`, and `AL_POSITION` calls. The project rule is "All AL calls → `alCheckError()`". However, the per-vehicle update path runs on every audio thread wake for up to 24 sources (12 pairs × 2). The spec does not state whether `alCheckError` is called after each of the 3 AL calls per source, or whether a batch error check is acceptable.

At 100 wakes/s × 24 sources × 3 calls = 7,200 `alCheckError` calls per second on the audio thread. Each `alCheckError` calls `alGetError()` which is a driver round-trip. This performance cost is unspecced and may be significant on PulseAudio backends.

**Proposed resolution**: Add a spec section to `audio-system.md` clarifying that `alCheckError` is required after each `alSourcef`/`alSource3f` call on the audio thread in `updateVehicleEngines()`, with a note that the performance cost at 24 sources × 100 Hz must be benchmarked and that a batch error check (call `alGetError()` once after all 24 sources, not after each individual call) is an acceptable optimization — provided the error is attributed to the full batch rather than a specific call.

---

### HIGH-7 — No spec for WAV file loading: WAV decoder implementation, error handling, mono/stereo format selection

**File**: `audio-asset-formats.md`, `audio-system.md`, `v1-audio-asset-manifest.md`
**Severity**: HIGH

**Description**: The spec extensively covers OGG loading via libvorbisfile (`ov_fopen`, `ov_read`, `ov_pcm_seek`, `ov_clear`, `ov_info`) but has no corresponding spec for WAV loading. V1 has 16 WAV PCM SFX assets plus 2 stingers (18 WAV files total). The spec does not specify:
- Which WAV decoder library to use (libaudiofile? custom RIFF parser? dr_wav?).
- How to validate WAV format tags (must be PCM 0x0001, not ADPCM or IEEE float).
- How to handle multi-chunk WAV files (LIST chunks, INFO chunks before the data chunk).
- Error handling when `data` chunk is not found or truncated.
- How `alBufferData(AL_FORMAT_MONO16, ...)` is populated from the raw WAV bytes.

The `audio-asset-formats.md` spec says "Pre-loaded AL buffer" for Tier 1 WAV assets but does not specify the loading path, only that it is "pre-loaded". Without a WAV loading spec, implementers may use incompatible approaches.

**Proposed resolution**: Add a `## WAV Loading` section to `audio-system.md` or `audio-asset-formats.md` specifying: the decoder library (or minimal RIFF parser requirements), format validation checks, error handling (log + skip asset vs. throw), and how the decoded PCM is passed to `alBufferData`. Given the V1 WAV assets are all 44100 Hz mono 16-bit, a minimal RIFF parser is sufficient and should be specified.

---

### HIGH-8 — `m_occlusionGainTarget` initialization uses `memory_order_relaxed` but audio thread reads it on first wake without a synchronization point

**File**: `audio-system.md` (§ private members — `m_occlusionGainTarget`)
**Severity**: HIGH

**Description**: The spec specifies:
```cpp
// Required initialization (in AudioSystem constructor, before thread launch):
for (auto& t : m_occlusionGainTarget) t.store(1.0f, std::memory_order_relaxed);
m_audioThread = std::thread(&AudioSystem::audioThreadFunc, this);  // launch AFTER init
```

`std::thread` constructor is a happens-before barrier for all writes performed before it — so `memory_order_relaxed` stores before the `std::thread` constructor ARE visible to the thread. This is correct per the C++ memory model (§6.9.2.1: thread launch synchronizes-with the beginning of the new thread). The spec note is technically correct but misleadingly places the burden on the `std::thread` constructor without explaining WHY `relaxed` is safe here.

The deeper issue: the spec notes say `m_occlusionGainTarget` is "std::atomic<float>" but the `std::atomic<float>` array is declared as `std::atomic<float> m_occlusionGainTarget[kEvictableSFXCount]`. In C++11/14/17, `std::atomic` is not copy-constructible and arrays of atomics are not value-initialized through normal means — they are default-initialized (indeterminate state for floats). The `for` loop initializing them before thread launch is mandatory; if an implementer omits it (relying on the `{}` initializer on the member declaration to zero-initialize), the `{}` initializer on non-trivially-constructible members like `std::atomic<float>` initializes to 0.0f, not 1.0f. A 0.0f occlusion gain target means all 55 sources play at zero gain immediately. The spec says "MANDATORY: Initialize all elements to 1.0f" but the `{}` declaration initializes to 0.0f — this is a trap for implementers.

**Proposed resolution**: The member declaration `std::atomic<float> m_occlusionGainTarget[kEvictableSFXCount];` must NOT use `{}` in the spec (which would suggest zero-initialization to 0.0f). Remove the `{}` from the spec declaration and rely exclusively on the explicit `for` loop. Add a note: "The `{}` initializer initializes `std::atomic<float>` to 0.0f (its value-initialized state), NOT 1.0f — do NOT rely on member `{}` initialization for this array."

---

### HIGH-9 — `DUCKED` state checks only V1 stinger sources [55..56] but spec says "query AL_SOURCE_STATE for both V1 stinger sources" — no per-wake error check

**File**: `dynamic-soundscape.md` (§ DUCKED state)
**Severity**: HIGH

**Description**: The spec says: "Each audio thread wake, query `AL_SOURCE_STATE` for **both V1 stinger sources** (crisis at sources[55], milestone at sources[56])." There is no `alCheckError` requirement after these `alGetSourcei(AL_SOURCE_STATE, ...)` calls. Per the project error-checking rule ("All AL calls → `alCheckError()`"), these calls require error checking. At 100 Hz × 2 sources this is 200 checks/second in the DUCKED state, but they are mandatory.

More importantly, the DUCKED state check involves querying source state after the stinger has potentially been stopped and its buffer rebound (e.g., if `onSourceRecycled` ran on the stinger source — which the spec says should not happen because stingers are non-evictable, but the spec does not explicitly prohibit `cleanupFinishedSFX` from querying stinger source state and acting on AL_STOPPED stingers). `cleanupFinishedSFX` is documented to iterate `m_sfxSlots[0..kEvictableSFXCount-1]` — stingers are at indices 55/56, which are outside this range (range is 0..54 = kEvictableSFXCount-1 = 54). So stinger sources are correctly excluded from `cleanupFinishedSFX`. This is correct but only because `kEvictableSFXCount = 55` and the loop is `< kEvictableSFXCount`. A future misread could break this.

**Proposed resolution**: Add `alCheckError` calls after each `alGetSourcei(AL_SOURCE_STATE)` call in the DUCKED state wake loop. Add a comment in the `cleanupFinishedSFX` spec explicitly noting that the loop bound `< kEvictableSFXCount` ensures stinger sources (55/56) and stream sources (58..61) are never touched by the cleanup loop.

---

### HIGH-10 — `transitionToMainMenu` spec says "stops all active gameplay music stems and ambient beds on sources[58..61]" — no spec for in-flight crossfades

**File**: `audio-system.md` (§ IAudioSystem::transitionToMainMenu)
**Severity**: HIGH

**Description**: `transitionToMainMenu()` "stops all active gameplay music stems and ambient beds on sources[58..61]". However, the spec does not describe what happens when a crossfade is currently in progress (e.g., music is mid-crossfade between calm and growth stems when the player quits). The crossfade state includes `m_musicCrossfadeT` (atomic), the outgoing source (with ongoing gain curve), and the incoming source (with rising gain). Calling `alSourceStop` on both without properly resetting the crossfade state machine will leave `m_musicCrossfadeT` at a non-zero value, which means the next `transitionToGameplay()` call will attempt a mid-crossfade continuation using stale gain values.

The spec says "stops all sources[58..61] unconditionally via alSourceStop" but does not specify: (1) reset `m_musicCrossfadeT` to 0; (2) reset the crossfade command queue (pending crossfade commands must be discarded); (3) reset `m_ambientCrossfadeT` to 0; (4) set `m_intentionallyStopped = true` for all 4 streams before calling `alSourceStop`.

Without (4) especially, the starvation recovery path will fire on the next audio thread wake and attempt to restart the stopped streams — restarting gameplay audio that was supposed to be stopped.

**Proposed resolution**: Expand the `transitionToMainMenu()` spec to enumerate all state that must be reset: crossfade progress counters, crossfade command queue flush, `m_intentionallyStopped` flag for all 4 stream sources set under `m_streamMutex`, and `alSourceStop` calls for all 4 sources also under `m_streamMutex` to prevent starvation recovery race.

---

### HIGH-11 — `IAlcFunctions` seam is mentioned but not defined in audio architecture specs

**File**: `audio-system.md` (§ AudioSystem constructor signature, `alcFunctions: non-owning pointer to IAlcFunctions`)
**Severity**: HIGH

**Description**: The spec references `IAlcFunctions` as the "seam so audio tests can run without an AL device" and `DefaultAlcFunctions` as the production implementation. The CLAUDE.md project rules reference `src/audio/ialc_functions.h`. However, none of the audio architecture spec files define the `IAlcFunctions` interface — what methods it must expose, how `DefaultAlcFunctions` implements them, or how the mock bypasses the real ALC calls. The memory notes mention it exists at `src/audio/ialc_functions.h` but this is a source path, not a spec.

Without a spec for `IAlcFunctions`, test engineers cannot implement `MockAlcFunctions` correctly. The spec also does not state which specific ALC calls are intercepted by the seam (`alcOpenDevice`, `alcCreateContext`, `alcMakeContextCurrent`, `alcSetThreadContext` — or only the thread-local context function?).

**Proposed resolution**: Add a `## IAlcFunctions Interface` section to `audio-system.md` specifying the interface methods, their signatures, and the `DefaultAlcFunctions` wrapper. This should include which calls the seam intercepts and the rationale for why those specific calls are in the seam vs. hardcoded.

---

### HIGH-12 — No spec for `AudioSystem::loadSound()` method signature, buffer caching, or error handling on missing assets

**File**: `audio-system.md`, `v1-audio-asset-manifest.md`
**Severity**: HIGH

**Description**: The manifest spec mentions `AudioSystem::loadSound()` as the path used for stinger loading, and the SoundId table documents 25 SoundIds. However, no spec file defines the `loadSound()` method signature, buffer caching behavior (is it idempotent? can the same SoundId be loaded twice?), or error handling when the asset file is missing or corrupt.

The pre-load queue mechanism is described for Tier 2 OGG SFX (via `processPreloadCommand` on the audio thread), but Tier 1 WAV loading (which does NOT need to be on the audio thread per the spec — there is no process-wide context constraint because WAV loading only calls `alBufferData` once) is not specified at all in terms of threading, call sequence, or error policy.

**Proposed resolution**: Add a `## Asset Loading API` section to `audio-system.md` specifying `loadSound(SoundId id, const std::string& path, bool looping)` signature, idempotency contract, and error policy (missing file → log error + assign null buffer to SoundId → `playSound(id)` becomes a no-op).

---

## MEDIUM Issues

---

### MED-1 — Zone ambient loop EFX bypass not specified for `alSourcei(AL_DIRECT_FILTER, AL_FILTER_NULL)`

**File**: `spatial-audio.md`, `audio-occlusion.md`
**Severity**: MEDIUM

**Description**: `spatial-audio.md` specifies `alSourcei(src, AL_DIRECT_FILTER, AL_FILTER_NULL)` for UI / notification sounds to bypass EFX occlusion. However, `sfx_earthworks` (in `v1-audio-asset-manifest.md`) specifies "AL_DIRECT_FILTER: AL_FILTER_NULL — EFX bypass because construction occurs on open, unoccluded tiles". There is no spec for WHEN this bypass is set — at `acquireSFXSource()` time? Per `playPositionalSound()` call? The audio-occlusion.md spec pre-binds filters at pool construction, meaning every evictable SFX source already has an EFX filter attached. The bypass for `sfx_earthworks` requires explicitly unbinding the filter at play time, then what happens when the source is recycled — is the filter re-bound by `onSourceRecycled`?

The spec in `audio-occlusion.md` says `onSourceRecycled` calls `alSourcei(m_sources[i], AL_DIRECT_FILTER, m_occlusionFilter[i])` — this re-binds the filter. So the bypass is automatically cleared on recycle. But the spec does not document this as a deliberate design choice or specify that `sfx_earthworks` and similar "EFX bypass at tile center" sounds MUST be played and NOT reused (one-shot), relying on the recycle step to restore the filter.

**Proposed resolution**: Add a note to `audio-occlusion.md` and `spatial-audio.md` that per-call EFX bypass (setting `AL_DIRECT_FILTER` to `AL_FILTER_NULL` at play time) is a runtime override that `onSourceRecycled` will automatically undo. Document that this pattern is correct and expected for all non-occluded positional sources.

---

### MED-2 — `AL_SOFT_loop_points` extension loaded on audio thread but `alBufferiv` proc address lookup may return the wrong function type

**File**: `streaming-architecture.md` (§ Pre-loaded looping OGG SFX — loop point handling)
**Severity**: MEDIUM

**Description**: The spec uses:
```cpp
auto alBufferiv = reinterpret_cast<LPALBUFFERIV>(alGetProcAddress("alBufferiv"));
```

`LPALBUFFERIV` requires `<AL/alext.h>`. The spec does not specify how this type is declared without including `alext.h`. If `processPreloadCommand` is in `audio_system.cpp` (which includes `<AL/alc.h>` and presumably `<AL/al.h>`), then `<AL/alext.h>` can also be included there — but this is not stated. If `alext.h` is not included, the `LPALBUFFERIV` type is undefined, forcing a manual `using LPALBUFFERIV = void(*)(ALuint, ALenum, const ALint*)` — which must be specified in the spec.

**Proposed resolution**: Add to the `processPreloadCommand` context in `streaming-architecture.md`: "The `LPALBUFFERIV` type is available from `<AL/alext.h>`. Since `audio_system.cpp` may freely include OpenAL headers, include `<AL/alext.h>` in `audio_system.cpp` and use the standard typedef. Do NOT include `<AL/alext.h>` in any `.h` file."

---

### MED-3 — No spec for `AudioStream` struct/class layout and ownership

**File**: `streaming-architecture.md`
**Severity**: MEDIUM

**Description**: The streaming spec describes `AudioStream` as holding `OggVorbis_File` as a persistent member, `m_samplesQueued`, `m_nextBarBoundary`, `m_intentionallyStopped`, `isOpen`, `vf` (VorbisFile), and AL buffer handles. But no spec document defines the full `AudioStream` struct/class: its fields, constructors, ownership, and where it lives (`AudioSystem` member? separate file?). The spec says "AudioStream holds OggVorbis_File as a persistent member" but is `AudioStream` a struct in `AudioSystem`, a separate class, or a private inner class?

The 4 stream sources use sources[58..61] and the spec documents stream[0..1] = music stems, stream[2..3] = ambient beds. But whether `AudioSystem` has `AudioStream m_streams[4]` or `std::array<AudioStream, 4>` is not specified.

**Proposed resolution**: Add a `## AudioStream Data Structure` section to `streaming-architecture.md` defining the full field list, the array size (`kStreamSourceCount = 4`), and the relationship between stream index and source pool index (stream[i] uses source at pool index `kSFXPoolSize + i`).

---

### MED-4 — `sfx_vehicle_horn` simultaneous cap (max 3) enforcement is mentioned in the manifest but not in any implementation spec

**File**: `v1-audio-asset-manifest.md`, `source-pool.md`, `audio-system.md`
**Severity**: MEDIUM

**Description**: The manifest spec for `sfx_vehicle_horn` says: "global simultaneous cap: max 3 horn sources playing at any time across all vehicles". This is a runtime enforcement requirement. However, no implementation spec file (source-pool.md, audio-system.md) describes how this cap is tracked, which data structure counts active horn sources, or how `playSound(SFX_VEHICLE_HORN, HIGH)` determines whether the cap has been reached before acquiring a source.

The per-vehicle 2 s cooldown also requires a per-vehicle timestamp, but no data structure is specified to hold per-vehicle horn cooldowns.

**Proposed resolution**: Add a `## Vehicle Horn Rate Limiting` section to `source-pool.md` or `dynamic-soundscape.md` specifying: the `m_activeHornCount` atomic counter, `m_vehicleHornCooldown[vehicleId]` timestamp map, the check sequence in `playVehicleHorn()`, and what happens when the cap is reached (silent drop with no error).

---

### MED-5 — `m_duckTimer` units and reset semantics are underspecified across state transitions

**File**: `dynamic-soundscape.md` (§ DuckState machine)
**Severity**: MEDIUM

**Description**: The spec defines `m_duckTimer` as "seconds elapsed in current duck phase (audio thread only)". The DUCKING phase resets `m_duckTimer = 0` on stinger-during-DUCKING re-entry. The RELEASING phase says "where `m_duckTimer` is the elapsed time since the RELEASING state was entered (0 at entry, capped at 1.5 s)". However, the spec does not explicitly state that `m_duckTimer` is reset to 0 on DUCKED → RELEASING transition. The reader must infer this from the "0 at entry" phrase.

More importantly: on RELEASING → DUCKING re-entry (stinger fires while releasing), the spec says "set `m_duckStartGain = m_musicDuckGain` (capture current gain at interruption), reset `m_duckTimer = 0`, transition to DUCKING". This resets `m_duckTimer` to 0, correct. But the DUCKING ramp formula is `m_musicDuckGain = m_duckStartGain + (0.4f - m_duckStartGain) * (m_duckTimer / 0.2f)`. If `m_duckStartGain > 0.4f` (true when releasing from above 0.4) this produces the correct downward ramp. If `m_duckStartGain < 0.4f` (impossible — gain can't go below 0.4 in normal operation), the formula would ramp UP. The spec does not guard against this case, relying on the state machine invariant that RELEASING always starts at 0.4 and ramps up.

**Proposed resolution**: Add explicit state-transition reset tables showing which fields are reset on each transition: `m_duckTimer = 0` on IDLE→DUCKING, DUCKED→RELEASING, and RELEASING→DUCKING; `m_duckStartGain` captured on IDLE→DUCKING and RELEASING→DUCKING.

---

### MED-6 — No spec for how `setMusicTrack(MusicTrackId)` differs from `setMusicIntensity(MusicIntensity)`

**File**: `audio-system.md` (§ IAudioSystem interface)
**Severity**: MEDIUM

**Description**: The `IAudioSystem` interface exposes both `setMusicTrack(MusicTrackId id)` and `setMusicIntensity(MusicIntensity intensity)`. The intent of `setMusicTrack` is documented as "Begin streaming the specified music track (with beat-boundary crossfade from the current track)" — this implies it is a direct track selection API. `setMusicIntensity` selects by tier (CALM/GROWTH/CRISIS) and the audio system picks the variant.

However, the spec does not define what `setMusicTrack` does that `setMusicIntensity` does not: Can callers use `setMusicTrack` to force a specific variant (e.g., always `music_crisis_02` during testing)? Or is `setMusicTrack` only used for main menu music (`MusicTrackId::MainMenu` → picks main_menu_01 or 02)? The crossfade behaviour for a `setMusicTrack` call is undefined — does it use bar-boundary synchronization? Does it bypass the time-of-day forced-Calm override?

**Proposed resolution**: Add a clarifying paragraph to the `setMusicTrack()` method comment in `audio-system.md` explaining: (1) `setMusicTrack` is used ONLY for main menu music selection by `UIManager` at startup; (2) `setMusicIntensity` is the API for all gameplay music; (3) `setMusicTrack` bypasses bar-boundary synchronization (it is a direct track replacement); (4) `setMusicTrack` is NOT subject to the time-of-day forced-Calm override (main menu runs in a separate audio context from gameplay).

---

### MED-7 — `alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED)` call has no `alCheckError` requirement in HRTF spec

**File**: `hrtf-initialization.md`
**Severity**: MEDIUM

**Description**: The spec shows `alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED)` followed by `alCheckError("alDistanceModel")`. This is correct. However, there is no spec for what happens if this call fails (e.g., if the context was not made current before the call — which cannot happen per the constructor sequence, but the spec does not make this dependence explicit). The `hrtf-initialization.md` is a standalone document that was clearly written independently of `audio-system.md`'s constructor sequence. A reader implementing from only `hrtf-initialization.md` might not know to call `alcMakeContextCurrent` first.

**Proposed resolution**: Add a cross-reference in `hrtf-initialization.md`: "Step 1 (`alcMakeContextCurrent`) in the constructor sequence (`audio-system.md`) MUST succeed before any call in this section — `alDistanceModel` and all subsequent `al*` calls require a current context."

---

### MED-8 — Production briefs define `kZoneLoopMaxPreloadDurationSeconds = 18.0` but no spec file defines this constant in C++

**File**: `production-briefs/zone-loop-production-brief.md`
**Severity**: MEDIUM

**Description**: The zone-loop production brief mentions `kZoneLoopMaxPreloadDurationSeconds = 18.0` as a C++ constant that enforces the 18 s hard cap. This constant is not defined or referenced in any implementation spec file (`audio-asset-formats.md`, `source-pool.md`, `audio-system.md`). There is no spec for WHERE in the C++ code this constant lives, whether it belongs in `audio_types.h`, `source-pool.h`, or a new `audio_constants.h`, or how it is used at load time to validate zone loop durations.

**Proposed resolution**: Add `kZoneLoopMaxPreloadDurationSeconds` to the compile-time constants section of `source-pool.md` (§ Phase 3 Compile-Time Constants) alongside the existing pool layout constants. Specify that `AudioSystem::processPreloadCommand()` validates OGG duration against this constant before calling `alBufferData`.

---

### MED-9 — `testability-architecture.md` MockAudioSystem is described as having 19 methods but does not document the `updateVehicleAudio` signature

**File**: `architecture/testing/testability-architecture.md`
**Severity**: MEDIUM

**Description**: `testability-architecture.md` references `MockAudioSystem` with 19 `MOCK_METHOD` entries and says it lives at `tests/simulation/MockAudioSystem.h`. The `audio-system.md` spec documents the three Phase 11d vehicle methods and notes their `MOCK_METHOD` signatures in inline comments. However, `testability-architecture.md` does not reference these three new methods, leaving the test architecture spec out of sync with the IAudioSystem method count expansion. A test engineer reading `testability-architecture.md` alone would see no mention of the vehicle engine pair mock methods.

**Proposed resolution**: Add to `testability-architecture.md` a reference to the Phase 11d vehicle engine audio mock methods (`acquireVehicleEnginePair`, `releaseVehicleEnginePair`, `updateVehicleAudio`) and confirm the method count is 19 (matching `audio-system.md`'s comment "Phase history: Phase 7 (base 14 methods) → ... → Phase 11d (+3 = 18) → Phase 11m (+1 = 19)").

---

## DUPLICATE Content Issues

---

### DUP-1 — Shutdown sequence for streaming sources is documented in BOTH `audio-thread-shutdown.md` AND partially in `streaming-architecture.md`

**Files**: `audio-thread-shutdown.md` (§ Step 4), `streaming-architecture.md` (§ MANDATORY — `openStreamOGG` must flush AL buffer queue)
**Severity**: MEDIUM

`audio-thread-shutdown.md` specifies the authoritative shutdown loop for streaming sources (query `AL_BUFFERS_QUEUED`, call `alSourceUnqueueBuffers`). `streaming-architecture.md` shows a nearly identical pattern inside `openStreamOGG` for the re-open path. While the two uses are different (shutdown vs. stream restart), the code is so similar that an implementer might copy from one and miss a detail in the other. The shutdown spec additionally checks `alCheckError` after `alSourceStop` which is not shown in the `openStreamOGG` pattern.

**Proposed resolution**: Add a cross-reference note in `streaming-architecture.md` `openStreamOGG` section pointing to `audio-thread-shutdown.md` for the authoritative unqueue pattern. Note the difference: `openStreamOGG` does not call `alCheckError` after `alSourceStop` in the re-open path (the error would be stale from the playing state — not a failure).

---

### DUP-2 — Non-positional source setup (`AL_SOURCE_RELATIVE`, `AL_ROLLOFF_FACTOR = 0`) is duplicated across `source-pool.md`, `spatial-audio.md`, and `dynamic-soundscape.md`

**Files**: `source-pool.md` (§ Stinger source non-positional setup), `spatial-audio.md` (§ UI / notification sounds), `dynamic-soundscape.md` (§ Stinger source allocation)
**Severity**: LOW

Three separate spec sections describe the same non-positional source setup pattern:
```cpp
alSourcei(s, AL_SOURCE_RELATIVE, AL_TRUE);
alSource3f(s, AL_POSITION, 0.f, 0.f, 0.f);
alSourcef(s, AL_ROLLOFF_FACTOR, 0.f);
alSource3f(s, AL_VELOCITY, 0.f, 0.f, 0.f);
```
Each is slightly different (stinger version includes `AL_VELOCITY`; UI sound version does not mention `AL_VELOCITY`; `dynamic-soundscape.md` version only mentions `AL_SOURCE_RELATIVE = AL_TRUE`). The inconsistency creates ambiguity: should `AL_VELOCITY` be set for UI sources?

**Proposed resolution**: Define a canonical "non-positional source setup" procedure in `spatial-audio.md` (which already covers all distance model setup). Cross-reference this procedure from `source-pool.md` and `dynamic-soundscape.md` rather than re-specifying it.

---

### DUP-3 — `m_lastDuckWakeTime` initialization requirement is duplicated in both `dynamic-soundscape.md` and `streaming-architecture.md`

**Files**: `dynamic-soundscape.md` (§ `m_duckTimer` advancement), `streaming-architecture.md` (§ Audio thread init block — "IMPORTANT: initialize `m_lastDuckWakeTime` BEFORE notify_one()")
**Severity**: LOW

Both files contain near-identical explanation of why `m_lastDuckWakeTime` must be initialized before `notify_one()`. The text in `streaming-architecture.md` is slightly more condensed but the essential spec content is the same. This duplication means a change to the initialization requirement must be applied in two places.

**Proposed resolution**: Make `dynamic-soundscape.md` the single authoritative source for the duck state machine initialization sequence. In `streaming-architecture.md`, replace the duplicate with a single cross-reference sentence.

---

### DUP-4 — StingerType enum and coupling rationale is documented in both `source-pool.md` and `audio-system.md`

**Files**: `source-pool.md` (§ Stinger source reservation — structurally enforced), `audio-system.md` (§ IAudioSystem — `StingerType` enum comment block)
**Severity**: LOW

Both files contain the StingerType enum values (CRISIS=55, MILESTONE=56), the coupling rationale, and the post-V1 promotion sequence. The `audio-system.md` version is less detailed; `source-pool.md` is authoritative. But having two places creates drift risk — a post-V1 promotion that updates `source-pool.md` must also update `audio-system.md`.

**Proposed resolution**: Remove the StingerType enum detail from `audio-system.md`'s IAudioSystem comment and replace with "See source-pool.md for enum values and coupling rationale." Retain only the forward-declaration in `audio-system.md`.

---

### DUP-5 — EFX filter allocation loop and shutdown loop are duplicated between `audio-occlusion.md` and `audio-thread-shutdown.md`

**Files**: `audio-occlusion.md` (§ Full pool construction loop and shutdown cleanup), `audio-thread-shutdown.md` (§ Step 4b — Release EFX lowpass filter objects)
**Severity**: MEDIUM

`audio-thread-shutdown.md` contains the exact same filter deletion loop as `audio-occlusion.md`. The `audio-thread-shutdown.md` version is the "authoritative shutdown sequence" but the actual filter handling detail (loop bound `kEvictableSFXCount`, per-filter null check, reset to `AL_FILTER_NULL`) is in `audio-occlusion.md`. The two versions agree but any future change must be applied in two places.

**Proposed resolution**: In `audio-thread-shutdown.md` step 4b, present only the high-level contract ("delete EFX filters; loop bound kEvictableSFXCount; check != AL_FILTER_NULL") and add "See audio-occlusion.md for the authoritative code pattern." Remove the duplicate code block from `audio-thread-shutdown.md`.

---

## LOW / MINOR Issues

---

### LOW-1 — `hrtf-initialization.md` uses `context_`/`device_` variable names inconsistent with `audio-system.md` `m_context`/`m_device`

**File**: `hrtf-initialization.md`
**Severity**: LOW

All member variable names in the hrtf-initialization spec use trailing underscore (`context_`, `device_`) while `audio-system.md` uses `m_` prefix convention (`m_context`, `m_device`). This inconsistency will confuse implementers cross-referencing the two files.

**Proposed resolution**: Update `hrtf-initialization.md` to use `m_context`/`m_device` naming throughout.

---

### LOW-2 — Production briefs are partially approved but approval metadata format is inconsistent

**Files**: `production-briefs/music-production-brief.md` (HTML comment approval), others (no approval metadata)
**Severity**: LOW

`music-production-brief.md` uses an HTML comment for approval: `<!-- APPROVED: sound-artist-opensoftal 2026-02-25 -->`. Other production briefs (`vehicle-sfx-production-brief.md`, `ambient-bed-production-brief.md`, `stinger-production-brief.md`, `wav-sfx-production-brief.md`, `zone-loop-production-brief.md`) have no approval comment. The `dynamic-soundscape.md` has a signed-off section at the bottom using a different format. Inconsistent approval tracking creates ambiguity about which briefs are final and which are still provisional.

**Proposed resolution**: Standardize on a single approval format in all production brief documents (e.g., the HTML comment approach used in `music-production-brief.md`) and mark each brief as either `APPROVED` or `DRAFT`.

---

### LOW-3 — No spec for `MusicTrackId` persistence — if the game is saved during CRISIS music, what track resumes on load?

**File**: `audio-system.md`, `v1-audio-asset-manifest.md`
**Severity**: LOW

Save/load is a separate architecture domain, but `IAudioSystem` is called by `UIManager` after game load. There is no spec for whether `AudioSystem` state (current music track, ambient bed, duck state) is persisted or reset on load. Given that `transitionToGameplay()` is called on load, and the music system will restart from the initial state, the current music intensity on load depends on `CitySimulation::update()` being called before the first music intensity set — which may produce a brief silence or wrong-stem start.

**Proposed resolution**: Add to `audio-system.md` §transitionToGameplay: "Music intensity state at load is re-established by the first `setMusicIntensity()` call from `CitySimulation::update()` after load. `transitionToGameplay()` does NOT restore saved music intensity — it begins fresh with `MusicIntensity::CALM`. The first `CitySimulation::update()` call will set the correct intensity immediately."

---

### LOW-4 — `sfx_earthworks` is `AL_SOURCE_RELATIVE = AL_FALSE` (positional) but listed under "build/demolish feedback" with other non-positional SFX in the manifest table notes

**File**: `v1-audio-asset-manifest.md`
**Severity**: LOW

The manifest notes for `sfx_earthworks` explicitly clarify "This does NOT make the sound non-positional — `sfx_earthworks` remains a world-space positional source with `AL_SOURCE_RELATIVE = AL_FALSE`. Do NOT set `AL_SOURCE_RELATIVE = AL_TRUE`." The manifest for `sfx_build_place`, `sfx_build_demolish`, and `sfx_road_build` also specify positional at tile center. But `sfx_earthworks` additionally says "EFX bypass: AL_FILTER_NULL" — meaning it is positional but EFX-bypassed. This combination (positional + EFX bypass) is unusual and is only documented in the manifest, not in `spatial-audio.md` or `audio-occlusion.md`.

**Proposed resolution**: Add a note to `audio-occlusion.md` § Source Recycle Requirement: "Per-sound EFX bypass (setting `AL_DIRECT_FILTER = AL_FILTER_NULL` at play time) is correct and supported for specific assets that are positional but play in inherently unoccluded contexts (e.g., `sfx_earthworks` played at tile center in an open construction site)."

---

### LOW-5 — Vehicle engine OGG encoding quality is unspecified in `vehicle-sfx-production-brief.md` vs. `audio-asset-formats.md` and `v1-audio-asset-manifest.md`

**File**: `production-briefs/vehicle-sfx-production-brief.md`
**Severity**: LOW

`vehicle-sfx-production-brief.md` specifies "encode at **libvorbis -q 6** (minimum) for vehicle engine loops" but `audio-asset-formats.md` does not have an explicit entry for vehicle engine OGG quality (only music stems -q 8, ambient beds -q 7, zone loops -q 6). The `v1-audio-asset-manifest.md` OGG Vorbis Encoding Quality table also does not include vehicle engine loops. A reader of `v1-audio-asset-manifest.md` alone would not know the engine loop encoding quality.

**Proposed resolution**: Add a "Vehicle engine loops" row to the OGG Vorbis Encoding Quality table in `v1-audio-asset-manifest.md`: `-q 6` (same as zone loops — mono tonal content).

---

### LOW-6 — `dynamic-soundscape.md` Phase 1 sign-off section is misplaced

**File**: `dynamic-soundscape.md` (end of file)
**Severity**: LOW

The file ends with:
```
## Phase 1 sound-artist-opensoftal sign-off
**Date**: 2026-02-21
**Role**: sound-artist-opensoftal
```

This appears to be an incomplete section (no sign-off text or status). Having a sound artist sign-off section in a developer specification file is inconsistent with the file's purpose (implementation spec). This section should be in a separate review artifact or the production brief.

**Proposed resolution**: Remove the Phase 1 sign-off section from `dynamic-soundscape.md`. Sign-off should be tracked in production briefs or a separate review document. If the sign-off needs to be in the spec, add a complete entry (who reviewed what and whether approved or conditionally approved).

---

## MISSING Technical Specifications

---

### MISSING-1 — No spec for audio thread panic handling / AL error escalation from audio thread

**Severity**: HIGH

The spec describes `alCheckError_real` as throwing `std::runtime_error`. If this throw occurs on the audio thread (inside `updateStreams`, `updateOcclusion`, or `updateVehicleEngines`), it will propagate up through the audio thread's top-level function (`audioThreadFunc`). An unhandled exception on a `std::thread` calls `std::terminate()` — crashing the entire process. No spec document describes what the audio thread should do on a fatal AL error:
- Should it catch all exceptions, log the error, set `m_deviceLost = true`, and exit cleanly?
- Should it signal `m_initCV` with an error to notify the main thread?
- Is `std::terminate()` acceptable for audio thread panics?

**Proposed resolution**: Add a `## Audio Thread Error Handling` section to `streaming-architecture.md` specifying that the audio thread's top-level loop MUST wrap all work in a `try/catch(std::exception&)` block, log the error, set `m_deviceLost.store(true, memory_order_release)`, call `alGetError()` to clear the error state, and continue the loop (degraded mode) OR exit if `m_deviceLost` was already set (unrecoverable).

---

### MISSING-2 — No spec for `AudioSystem::update()` main thread responsibilities beyond "advance occlusion raycast budget"

**Severity**: MEDIUM

The `IAudioSystem::update(float realDeltaSeconds)` method is described as: "advance occlusion raycast budget, push time-of-day transitions, and forward any pending crossfade or zone-layer source updates." But the spec does not define what "advance occlusion raycast budget" means in terms of implementation — how many raycasts are dispatched per `update()` call, how the round-robin source selection works, and what data is written to `m_occlusionGainTarget` after a raycast hit/miss. The `audio-occlusion.md` file describes the result (gain target values, smoothing) but not the main-thread raycast scheduling algorithm.

**Proposed resolution**: Add a `## Main Thread Raycast Budget` section to `audio-occlusion.md` specifying: per-frame budget (max 8 raycasts), source iteration order (round-robin over occupied evictable SFX slots within 100 m), how sources beyond 100 m are handled (target set to 1.0f without raycasting), and the per-source cooldown (1 raycast per 6 frames tracked via a `uint32_t m_occlusionFrameCounter[kEvictableSFXCount]` array).

---

### MISSING-3 — No spec for `AudioStream` object reuse across `transitionToGameplay` / `transitionToMainMenu` calls

**Severity**: MEDIUM

`transitionToMainMenu()` and `transitionToGameplay()` both operate on `sources[58..61]` (the stream partition). There is no spec for the state of the `AudioStream` objects after these transitions. Specifically: when `transitionToMainMenu()` stops all 4 streams, are the `AudioStream` objects reset to a fresh state (closing `OggVorbis_File`, resetting `m_samplesQueued`, etc.) or left in a stopped state? When `transitionToGameplay()` is called next, does it call `openStreamOGG` on already-open slots (triggering the re-open path) or on fresh slots?

**Proposed resolution**: Add to `streaming-architecture.md` a section on stream slot reuse across transition calls, specifying that `transitionToMainMenu()` must call `openStreamOGG(slot, nullptr)` or equivalent to close the OGG file handle and mark slots as `isOpen = false`, preventing the re-open path from being triggered on `transitionToGameplay()`.

---

End of review.


---

## 7. Graphics Architecture Review

**Reviewer:** Senior C++ Developer (Irrlicht)

---

# Irrlicht / Graphics Architecture Review
## AI Town — Senior C++ Developer (graphics-dev-irrlicht) Pass

Review date: 2026-03-29
Scope: all files under `architecture/graphics-architecture/`, `architecture/asset-standards/`,
and `architecture/testing/testability-architecture.md`.

---

## Summary

The specifications are detailed and broadly correct. Several CRITICAL and HIGH issues
exist that will cause silent bugs, double-frees, resource leaks, or incorrect rendering
if implemented literally from the current text. MEDIUM and LOW issues are specification
gaps or minor inconsistencies that will complicate implementation or maintenance.

---

## Issue Catalogue

### CRITICAL

---

#### CRIT-01 — `kCardinalFalloff` / `kDiagonalFalloff` values not specified in spec
**File**: `architecture/graphics-architecture/procedural-terrain.md` (§setTileHeight Neighbour Blending)
**Type**: [GAP]
**Description**: The spec states these falloff constants "are confirmed by gamedesign-lookandfeel sign-off in Phase 10b (reference starting point: cardinal 0.5, diagonal 0.25)" but simultaneously says "MUST NOT be committed before the sign-off is recorded." No sign-off is actually recorded in this file and no final binding values are given. The C++ constants `kCardinalFalloff` and `kDiagonalFalloff` therefore have no authoritative source. Different implementers may use different values, causing divergent terrain flattening behaviour that affects zone placement and road adjacency detection. The lack of confirmed values is a blocking gap for any phase that calls `setTileHeight()`.
**Proposed resolution**: Record the gamedesign sign-off in the spec with the confirmed decimal values and mark them `static constexpr float` in `src/rendering/render_constants.h`. Update the spec to reference that header as the canonical source.

---

#### CRIT-02 — `SceneEntityManager::destroy()` does not release sRGB textures for entities whose material slots contain only `ITexture*` placeholders
**File**: `architecture/graphics-architecture/texture-cache.md` (§sRGB texture entity lifetime)
**Type**: [GAP]
**Description**: The spec defines a two-tier release protocol: Step 1 iterates material slots and calls `releaseLinear(ITexture*)`, Step 1b iterates `m_srgbTextureFilenames` and calls `releaseSRGB(filename)`. However, the spec also states that "sRGB diffuse textures are raw `GLuint` values (not `ITexture*`) and are NOT present in node material slots." This means that any entity whose material slots were never populated with the sRGB texture (because the binding was done per-draw-call via `glActiveTexture/glBindTexture` in `OnSetConstants()`) will correctly fall through to Step 1b. But the spec does NOT specify how `m_srgbTextureFilenames` gets populated for the entity in the first place. If `BuildingAssetLoader::load()` or `SceneEntityManager::spawnBuilding()` fails to push filenames into `m_srgbTextureFilenames`, Step 1b is a no-op and the sRGB pool ref_count is never decremented — causing unbounded VRAM growth over the game session. There is no spec text telling `BuildingAssetLoader` or `spawnBuilding()` exactly when and how to push filenames.
**Proposed resolution**: Add an explicit contract in `texture-cache.md` (and cross-reference in `scene-graph-ownership.md`) specifying that `BuildingAssetLoader::load()` must call `textureCache->loadSRGB(filename, format)` and push the returned filename to the `BuildingAsset::srgbFilenames` vector, and that `SceneEntityManager::spawnBuilding()` must transfer those filenames to the entity's `m_srgbTextureFilenames` before the entity is considered live.

---

#### CRIT-03 — `CloudDomeShaderCallback` stored as `void*` — type erasure breaks safety
**File**: `architecture/graphics-architecture/sky-clouds.md` (§Shader Callback)
**Type**: [PROBLEM]
**Description**: The spec mandates that `m_cloudShaderCbRaw` is stored as `void*` in the `IrrlichtRenderer` header, with a cast to `CloudDomeShaderCallback*` inside the `.cpp`. This is stated as a deliberate choice to avoid exposing `CloudDomeShaderCallback` in the header. However, `void*` member storage defeats every compile-time type check: if the pointer is accidentally cast to the wrong type (another callback class, an Irrlicht object, etc.) the code compiles without error and causes undefined behaviour at runtime when `setCameraY()` accesses members via the wrong type. Using `void*` for a reference-counted Irrlicht callback stored on an internal class member is non-standard, harder to audit, and provides no benefit when the callback is defined in the `.cpp` anyway via a forward declaration.
**Proposed resolution**: Forward-declare `CloudDomeShaderCallback` in `IrrlichtRenderer.h` and store `CloudDomeShaderCallback* m_cloudShaderCb{nullptr}` directly. The forward declaration avoids pulling the full definition into the header and retains full type safety.

---

#### CRIT-04 — `evictUnreferenced()` may be called from `SceneEntityManager::destroy()` which can be invoked during a frame render pass
**File**: `architecture/graphics-architecture/texture-cache.md` (§CRITICAL constraint)
**Type**: [INCONSISTENCY]
**Description**: `texture-cache.md` states: "`evictUnreferenced()` must NOT be called from within `OnSetConstants()`" and further says it is called "strictly between `beginScene()`/`endScene()` boundaries but NOT within any `drawAll()` call." However, the spec does not define a clear lifecycle gate that prevents `SceneEntityManager::destroy()` from being called during a simulation tick that itself is invoked from inside the main render loop. If the game loop calls `citySimulation->tick()` between `beginScene()` and `drawAll()`, and that tick destroys an entity (calls `SceneEntityManager::destroy()`, which calls `evictUnreferenced()`), the eviction happens before `drawAll()` — correct. But if any caller invokes `destroy()` inside an event callback that fires during `sceneManager->drawAll()` (e.g. a pick-result callback), `glDeleteTextures` would execute mid-draw-call. The spec says nothing about ensuring this cannot happen via event callbacks.
**Proposed resolution**: Add an explicit rule in `texture-cache.md` that `evictUnreferenced()` MUST only be called in the game-logic update phase (before `driver->beginScene()`), never in response to Irrlicht scene callbacks or event handlers that may fire inside `drawAll()`. Cross-reference in `irrlicht-device-lifecycle.md` render loop ordering.

---

#### CRIT-05 — `addMeshSceneNode(static_cast<IMesh*>(lod0))` used for B3D assets: `static_cast` from `IAnimatedMesh*` to `IMesh*` is a base-class upcast and is correct, but the spec phrase "Cast `IAnimatedMesh*` to `IMesh*`" contradicts the SMesh downcast WARNING
**File**: `architecture/graphics-architecture/scene-graph-ownership.md` (§B3D Building Assets)
**Type**: [INCONSISTENCY]
**Description**: The B3D section correctly describes the `IAnimatedMesh*` → `IMesh*` upcast as "safe — `IAnimatedMesh` publicly inherits `IMesh`". However, the earlier WARNING section states "Never use `static_cast<SMesh*>` on an `IMesh*` pointer" while the B3D section uses a `static_cast` to do the upcast. These two uses of `static_cast` on mesh pointers appear in the same file and may confuse implementers: one is a downcast (UB risk) and one is an upcast (safe), but the file does not clearly distinguish them in the WARNING text. An implementer reading the WARNING may apply it too broadly and avoid the necessary upcast.
**Proposed resolution**: Add a clarifying sentence to the WARNING: "This prohibition applies only to downcasts from a base-class mesh pointer (`IMesh*`) to a derived type (`SMesh*`). Upcasting from `IAnimatedMesh*` to `IMesh*` is always safe because `IAnimatedMesh` publicly inherits `IMesh`."

---

### HIGH

---

#### HIGH-01 — `flushPendingRebuilds()` 100 ms budget not reset between loading-screen frames
**File**: `architecture/graphics-architecture/procedural-terrain.md` (§flushPendingRebuilds)
**Type**: [GAP]
**Description**: The spec defines `flushPendingRebuilds()` as breaking after 100 ms of CPU time measured by `m_clock->nowSeconds()`. It computes `start = m_clock->nowSeconds()` at the beginning of each call. This is correct. However, the spec also says "`TerrainSystem::update(dt)` is also called every loading-screen frame" and describes a cooperative drain. It does not specify whether `flushPendingRebuilds()` and `update()` share any elapsed-time budget or independently consume wall time. If both are called in the same loading-screen loop iteration and `flushPendingRebuilds()` has already consumed 100 ms, calling `update()` immediately afterward in the same frame tick will process two additional rebuilds (the normal 2-per-frame cap) on top of whatever `flushPendingRebuilds()` achieved — causing up to 102 ms of rebuild latency per frame rather than the stated 100 ms cap. On a slow CPU this can stall the loading screen spinner visibly. The spec does not call this out.
**Proposed resolution**: Clarify whether `update()` should be skipped in the same frame if `flushPendingRebuilds()` already exhausted its budget, or whether the 2 extra rebuilds from `update()` are acceptable overhead. A note in the spec either way eliminates implementer ambiguity.

---

#### HIGH-02 — Hover highlight `recalculateBoundingBox()` not specified for the pre-allocated buffer after in-place vertex update
**File**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (§Hover Highlight)
**Type**: [GAP]
**Description**: The spec says `setTileHoverHighlight()` "updates vertex positions in the existing buffer … calls `recalculateBoundingBox()` on the buffer, then sets `m_hoverVisible = true`." However, it does not specify that `recalculateBoundingBox()` must also be called on the parent `SMesh*` (`m_hoveredTileMesh`) after the buffer recalculation. The `scene-graph-ownership.md` and `procedural-terrain.md` both require `recalculateBoundingBox()` on the `SMesh` after all buffer recalculations. Since `m_hoveredTileMesh` is drawn via raw `IVideoDriver::drawMeshBuffer()` (not `sceneManager->drawAll()`), Irrlicht's frustum culling does not apply, so the missing mesh-level bounding box does not cause incorrect culling. But it is an inconsistency with the project rule and may silently matter if the draw path changes in a future phase to use a scene node.
**Proposed resolution**: Add an explicit step in `setTileHoverHighlight()` contract: "After buffer `recalculateBoundingBox()`, also call `m_hoveredTileMesh->recalculateBoundingBox()`" with a parenthetical note that culling correctness is not currently affected because the mesh is drawn via raw `drawMeshBuffer()`, but the call must be present for consistency with the project bounding-box policy.

---

#### HIGH-03 — Terrain DDA `pickTerrainTile()` returns the first cell where `rayY <= h`, which is the cell AFTER the crossing — not the cell at the actual intersection
**File**: `architecture/graphics-architecture/procedural-terrain.md` (§pickTerrainTile DDA Algorithm)
**Type**: [PROBLEM]
**Description**: The DDA traversal loop computes `tc` as approximately the ray parameter at the centre of the current cell and tests `if (rayY <= h)`. When the ray descends into terrain at a boundary between two cells, the detection fires at the cell the ray first enters when the Y coordinate drops below the heightmap sample — but the cell's height sample `getHeightAt(cx, cz)` is defined as the height at the grid-centre, not the boundary vertex. If the terrain is sloped, the boundary vertex between cell `(cx, cz)` and the previously traversed cell has a different height than either cell's grid-centre sample. The ray may be detected as "below terrain" one or two cells too late (the ray crossed the boundary vertex but the grid-centre height of the next cell is lower than the boundary vertex). For a 10 m cell size and terrain slopes of up to 26 m / (terrain dimension), this can produce a 1-cell (10 m) positioning error on steeply sloped terrain — a zone placed on the wrong tile.
**Proposed resolution**: Document the 1-cell potential error explicitly as a known limitation of the grid-centre height sampling approach and state the maximum error bound (`kCellSize` metres). Optionally propose a correction: after detecting `rayY <= h`, check whether the previous cell (`cx - stepX`, `cz - stepZ`) would also satisfy the criterion — if not, the current cell is the true hit; if it does, the previous cell is the actual intersection. This refinement is a low-cost one-step backtrack.

---

#### HIGH-04 — Zone overlay `SMesh*` lifecycle on `setZoneOverlay()` call: old mesh `drop()` before or after `addMeshSceneNode()`?
**File**: `architecture/graphics-architecture/scene-graph-ownership.md` (§Zone Overlay SMeshBuffer Batching)
**Type**: [GAP]
**Description**: The spec says the zone overlay mesh IS attached to the scene graph as a persistent `ISceneNode*` and "rebuilt (remove-old / add-new) on every `setZoneOverlay()` call." However, the spec does not specify the exact sequence for destroying the old scene node and creating the new one. Specifically: (a) which scene node pointer member on `IrrlichtRenderer` tracks the zone overlay node; (b) whether `SceneEntityManager::destroy()` is used or `node->remove()` is called directly; (c) whether the eviction sequence (texture clear → `setMaterial({})` → `evictUnreferenced()` → `remove()`) is required for an untextured overlay mesh. Without this sequence, if the overlay mesh is an `ISceneNode*`, leaving texture slots non-null between calls is not applicable here (no textures), but the destroy sequence is still required documentation for implementers.
**Proposed resolution**: Add a "Zone Overlay Node Rebuild Sequence" subsection to `scene-graph-ownership.md` documenting: (1) if `m_zoneOverlayNode != nullptr`, call `m_zoneOverlayNode->remove()` (no texture eviction needed — untextured overlay); (2) drop the old `SMesh*` via `->drop()`; (3) build new `SMesh*`; (4) `addMeshSceneNode()`; (5) drop the new `SMesh*` after `addMeshSceneNode()` grabs it; (6) store the new node pointer.

---

#### HIGH-05 — `TextureCache::loadSRGB()` — DDS header parser reads `dwMipMapCount` at offset 28 but does not validate that the mip level count is non-zero
**File**: `architecture/graphics-architecture/texture-cache.md` (§Truncated DDS files)
**Type**: [GAP]
**Description**: The spec warns about truncated DDS files where `dwMipMapCount` is declared but data is absent, but does not specify what the parser should do when `dwMipMapCount` is 0 (which is valid per the DDS spec — it means "one level, no mip chain"). If the parser iterates `for (int mip = 0; mip < dwMipMapCount; ++mip)` and `dwMipMapCount` is 0, the loop body never executes and the texture object is allocated but contains no uploaded data — resulting in a black surface with no error. The spec must mandate a minimum of 1 upload iteration.
**Proposed resolution**: Add a rule: "Before the mip upload loop, clamp `numMips = max(1, dwMipMapCount)` — DDS files with `dwMipMapCount == 0` must still upload mip level 0. Log a WARNING when `dwMipMapCount == 0` to flag incorrectly authored assets."

---

#### HIGH-06 — `GL_ACTIVE_TEXTURE` save/restore in `OnSetConstants()` for single-texture callbacks (cloud dome, building) is not specified
**File**: `architecture/graphics-architecture/shader-loading.md` (§sRGB texture binding in shader callbacks)
**Type**: [GAP]
**Description**: The 5-unit terrain splat shader sequence correctly specifies the `GL_ACTIVE_TEXTURE` save/restore. However, the cloud dome callback and the single-unit building/road shader callback patterns described earlier in the same section do NOT include this save/restore. The spec says the pattern "is mandatory for ALL raw-GL texture bindings in `OnSetConstants()`" in the splat shader section, but the earlier single-unit examples lack it. An implementer writing a building or road callback following the earlier code block will omit the save/restore and corrupt Irrlicht's internal texture unit tracking on every frame that draws that object.
**Proposed resolution**: Update the single-unit `OnSetConstants()` example to include the `glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit)` / `glActiveTexture(static_cast<GLenum>(savedUnit))` pattern. Add a bold note before the first example: "ALL `OnSetConstants()` implementations that call `glActiveTexture()` MUST save and restore `GL_ACTIVE_TEXTURE` — even single-unit bindings."

---

#### HIGH-07 — `VRAMProfiler` duplicate translation unit in `aitown_benchmark` CMake target
**File**: `architecture/graphics-architecture/benchmark-tool.md` (§CMake Target)
**Type**: [PROBLEM]
**Description**: The CMake target spec lists `src/rendering/VRAMProfiler.cpp` directly as a source of `aitown_benchmark` AND also links `aitown_render` which compiles `VRAMProfiler.cpp` as part of its static library. This produces `VRAMProfiler.cpp` in two translation units in the final link: once inside `libaitown_render.a` and once as a standalone object file in `aitown_benchmark`. On Linux with GNU ld, this typically results in duplicate symbol warnings or linker errors (`multiple definition of VRAMProfiler::init`). On MSVC, the linker silently resolves to one of the definitions — which one is implementation-defined and may produce ODR violations.
**Proposed resolution**: Remove `src/rendering/VRAMProfiler.cpp` from the `aitown_benchmark` sources list. `VRAMProfiler` is already provided transitively by linking `aitown_render`. The comment in the spec "The benchmark target is a separate translation unit context; linking `aitown_render` provides the compiled object transitively via `PRIVATE` linkage" acknowledges this but contradicts the explicit source addition. Remove the explicit source entry.

---

#### HIGH-08 — `u_srgbLinear` uniform bool in terrain splat shader: `setPixelShaderConstant` takes `int`, but the fragment shader declares `uniform bool`
**File**: `architecture/graphics-architecture/shader-loading.md` (§sRGB Gamma Fallback)
**Type**: [PROBLEM]
**Description**: The spec shows passing `&srgbLinearInt` (an `int`) to `services->setPixelShaderConstant("u_srgbLinear", &srgbLinearInt, 1)` and declares the GLSL uniform as `uniform bool u_srgbLinear`. Irrlicht's `IMaterialRendererServices::setPixelShaderConstant` signature is `virtual bool setPixelShaderConstant(const c8* name, const s32* ints, s32 count)` — it takes `s32*`. Passing `int` (which is `s32` on all target platforms) is fine. However, in GLSL `#version 130` and later, `bool` uniforms are set via `glUniform1i` with value 0 or 1, which is what Irrlicht's GLSL backend does internally. This part is correct. The issue is the GLSL fragment shader has `if (u_srgbLinear) { ... }` — this is valid GLSL `#version 130` (`bool` branching is supported). However, the spec does not state which GLSL version the terrain fragment shader uses. If the shader uses `#version 110` (GLSL 1.10), `bool` uniforms set via `glUniform1i` are undefined behaviour. The spec requires `#version 130` elsewhere for `texture()` but is silent on the terrain splat shader's version pragma.
**Proposed resolution**: Add a requirement that `terrain.frag` must begin with `#version 130` (minimum) and cite the `texture()` and `uniform bool` dependency. Verify that `uniform bool` set via `glUniform1i` is confirmed to work on the Mesa/GLSL 1.30 path.

---

#### HIGH-09 — `model-validator-tool.md` specifies 45 LOD0 `.b3d` files but asset inventory table totals 40 zone buildings + 4 service buildings = 44, not 45
**File**: `architecture/graphics-architecture/model-validator-tool.md` (§Phase 11d Asset Inventory)
**Type**: [INCONSISTENCY]
**Description**: The spec states "The validator tool exercises the 45 LOD0 `.b3d` files across categories 1–11." The asset inventory table shows: Zone buildings LOD0 = 36, Service building LOD0 = 4, and 5 vehicle `.b3d` files (LOD0 for car_sedan, car_hatchback, car_suv, bus_standard, truck_cargo). 36 + 4 + 5 = 45. However, the table lists "Zone building LOD0: 36" and "Service building LOD0: 4" for a sub-total of 40, and vehicles are not listed with a LOD0 count row. The total statement "45" is only correct if vehicles (5) are counted. The table needs a "Vehicle LOD0" row with count 5 and the math must be explicitly shown.
**Proposed resolution**: Add a "Vehicle LOD0 | 5 | car_sedan, car_hatchback, car_suv, bus_standard, truck_cargo" row to the inventory table and update the sub-total to "40 zone+service + 5 vehicles = 45 total" for clarity.

---

#### HIGH-10 — `IrrlichtUIBackend` GL_ARRAY_BUFFER global state rule is in `irrlicht-device-lifecycle.md` but not in `testability-architecture.md` or any test specification
**File**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (§CRITICAL GL STATE RULE)
**Type**: [MISSING]
**Description**: The spec documents a critical OpenGL state bug: after `IrrlichtUIBackend`'s constructor creates a VAO/VBO, it must call `glBindBuffer(GL_ARRAY_BUFFER, 0)` to avoid corrupting Irrlicht's subsequent scene rendering. This rule exists only in `irrlicht-device-lifecycle.md`. There is no corresponding test case in `testability-architecture.md` or any headless CI testing spec that verifies this invariant. Under `EDT_NULL` (used in headless CI), this bug is invisible — `IrrlichtUIBackend` is not constructed in headless tests. There is no `requires-opengl` test that constructs `IrrlichtUIBackend` and then calls `sceneManager->drawAll()` to verify vertex geometry is non-degenerate.
**Proposed resolution**: Add a `requires-opengl` test `IrrlichtUIBackend_VBO_GL_ARRAY_BUFFER_Unbound_AfterConstruct` in `tests/rendering/` that: (1) constructs `IrrlichtUIBackend`; (2) queries `GL_ARRAY_BUFFER_BINDING`; (3) asserts the binding is 0. Reference this test in the architecture spec.

---

### MEDIUM

---

#### MED-01 — `drawMeshBuffer()` for overlay quads requires driver material state to be pre-set; spec does not describe this
**File**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` and `scene-graph-ownership.md` (§raw drawMeshBuffer calls)
**Type**: [GAP]
**Description**: `IVideoDriver::drawMeshBuffer()` draws the provided mesh buffer using the current material state set on the driver via `IVideoDriver::setMaterial()`. The spec says the hover highlight and preview mesh are drawn via `driver->drawMeshBuffer(...)` in `drawScene()` but does not specify that `driver->setMaterial(buf->getMaterial())` must be called first. Without setting the material, Irrlicht uses whatever material was last set by `sceneManager->drawAll()` — which may have blending disabled, depth writes enabled, or a stale texture bound. The overlay mesh relies on `EMT_TRANSPARENT_ALPHA_CHANNEL` blending and depth-write disabled, both of which are in the buffer's material. If the previous scene material does not match, the overlay renders incorrectly (fully opaque or not at all).
**Proposed resolution**: Add to the `drawScene()` ordering spec: "Before each raw `drawMeshBuffer()` call for overlay or preview meshes, call `driver->setMaterial(buf->getMaterial())` where `buf` is the buffer being drawn. This ensures correct blending mode and depth state for transparent overlays."

---

#### MED-02 — LOD hysteresis spec repeated in three files with slightly different phrasing
**File**: `architecture/graphics-architecture/procedural-terrain.md`, `architecture/graphics-architecture/scene-graph-ownership.md`, `architecture/asset-standards/3d-model-standards.md`
**Type**: [DUPLICATE]
**Description**: The LOD hysteresis requirement ("Use separate swap-in / swap-out distances (5–10 m band)") is specified in `CLAUDE.md` project rules, `procedural-terrain.md`, `scene-graph-ownership.md`, and the LOD Distance Thresholds table in `3d-model-standards.md`. Each copy uses slightly different wording:
- `CLAUDE.md`: "Use separate swap-in / swap-out distances (5–10 m band). Never bare threshold comparisons."
- `procedural-terrain.md`: "never transitions up and down in the same frame"
- `3d-model-standards.md`: "Hysteresis bands are mandatory; ≥5 m for close thresholds, ≥10 m for far thresholds."
None of the three files cross-references the others as the normative source. An implementer reading only one file may get an incomplete picture. The canonical values (5 m close, 10 m far) exist only in `3d-model-standards.md`.
**Proposed resolution**: Designate `3d-model-standards.md` (LOD Distance Thresholds table) as the normative source for all hysteresis band values. In `procedural-terrain.md` and `scene-graph-ownership.md`, replace the local hysteresis descriptions with a cross-reference sentence pointing to the table.

---

#### MED-03 — `getTexture()` for cloud PNG: no `ET_NULL` / null return guard specified at the call site
**File**: `architecture/graphics-architecture/sky-clouds.md` (§Cloud Asset)
**Type**: [GAP]
**Description**: The spec says `IVideoDriver::getTexture()` is used to load `clouds.png`. It specifies an `EDT_NULL` early-return guard at the top of `initCloudPlane()`. However, it does not specify what happens if `getTexture()` returns null on a real OpenGL driver (e.g., because the file is missing). The sky dome init proceeds, `setTexture(0, nullptr)` is called on the material (which is valid in Irrlicht but produces a grey flat-shaded surface), and the cloud dome renders incorrectly with no error. The CI asset gate checks the file exists, but at runtime the file may be absent after incorrect deployment.
**Proposed resolution**: Add: "After `getTexture()`, null-check the returned `ITexture*`. If null: log an error, fall back to `EMT_TRANSPARENT_VERTEX_ALPHA` with no texture (invisible dome), and do not set `m_cloudNode->getMaterial(0).Texture[0]`."

---

#### MED-04 — Benchmark Scene 2 uses `addShadowVolumeSceneNode()` but no spec exists for shadow volume ownership
**File**: `architecture/graphics-architecture/benchmark-tool.md` (§Scene 2)
**Type**: [GAP]
**Description**: Scene 2 adds stencil shadow volumes to each building proxy box via `addShadowVolumeSceneNode()`. The returned `IShadowVolumeSceneNode*` is not mentioned in the cleanup sequence. Shadow volume nodes are child nodes of the mesh scene node and are automatically removed when their parent is removed. The spec's cleanup at the end of Scene 2 only says `smgr2->drop()`. This is correct (dropping the scene manager removes all its nodes recursively). However, the spec does not document why `drop()` alone is sufficient and a naive implementer may attempt to store and manually remove the shadow volume nodes, potentially calling `remove()` on already-removed children.
**Proposed resolution**: Add a comment in the benchmark spec: "Shadow volume nodes are children of their parent mesh node and are automatically removed by `smgr2->drop()`. Do not store or manually remove `IShadowVolumeSceneNode*` pointers."

---

#### MED-05 — `PolygonOffsetFactor = 1` for road tiles contradicts the center-line strip which requires `PolygonOffsetFactor = 5`
**File**: `architecture/asset-standards/3d-model-standards.md` (§Center-line strip) and `architecture/graphics-architecture/procedural-terrain.md` (§Z-fighting)
**Type**: [INCONSISTENCY]
**Description**: `procedural-terrain.md` specifies `PolygonOffsetFactor = 1` for road tiles. `3d-model-standards.md` specifies that the center-line strip uses `PolygonOffsetFactor = 5` (one step above the carriageway's `factor = 4`). But `procedural-terrain.md` says the carriageway itself uses `factor = 1`. If the carriageway is at `factor = 1` and the center-line is at `factor = 5`, the carriageway and center-line are 4 steps apart. The `3d-model-standards.md` comment "one step above the carriageway's `factor = 4`" implies the carriageway uses `factor = 4`, contradicting `procedural-terrain.md`'s value of `factor = 1`. One of these values is wrong.
**Proposed resolution**: Reconcile the two documents. Decide on the carriageway polygon offset factor and propagate it consistently. If the carriageway is `factor = 1` (as in `procedural-terrain.md`), the center-line strip should be `factor = 2`. If the carriageway is `factor = 4` (as implied by `3d-model-standards.md`), update `procedural-terrain.md`. Remove the contradiction.

---

#### MED-06 — No spec for how `IrrlichtRenderer::renderer->update(realDeltaSeconds)` is sequenced relative to terrain LOD and the cloud UV scroll
**File**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (§mandatory 11-step sequence)
**Type**: [INCONSISTENCY]
**Description**: The render loop spec lists the mandatory 11-step sequence and includes `renderer->update(realDeltaSeconds)` (cloud UV scroll) as step 5, between `terrainSystem->update()` (step 4) and `driver->beginScene()` (step 6). The code block at the top of the file does NOT include `renderer->update()` in the inline code sample — it jumps directly from `terrainSystem->update()` to `driver->beginScene()`. An implementer following the code sample will skip `renderer->update()` and the cloud UV offset will never scroll.
**Proposed resolution**: Add `renderer->update(realDeltaSeconds)` to the code sample immediately after `terrainSystem->update()` with a comment `// cloud UV scroll and per-frame renderer state`.

---

#### MED-07 — Building atlas `buildings_atlas_d.png` PNG workaround: spec does not define the sRGB upload path for the PNG fallback
**File**: `architecture/asset-standards/building-atlas-layout.md` (§V1 implementation exception)
**Type**: [GAP]
**Description**: The spec says the V1 atlas is loaded as a PNG via `IVideoDriver::getTexture()` and assigned to material slots via `node->getMaterial(m).setTexture(0, atlas)`. PNG loaded through `IVideoDriver::getTexture()` is decoded to `ECF_A8R8G8B8` (linear RGBA) by Irrlicht's PNG loader. There is no sRGB decoding on this path. The spec does not state whether the PNG version of the atlas is authored in linear or sRGB space, or whether the building shader must apply a manual `pow(color.rgb, 2.2)` gamma correction on the PNG path analogous to the terrain shader's `u_srgbLinear` uniform. If the PNG is authored as sRGB but uploaded as linear, all building facades will appear visually washed out (linear-decoded diffuse). This is a production rendering correctness issue for the PNG fallback path.
**Proposed resolution**: Add an explicit statement: "The V1 PNG atlas (`buildings_atlas_d.png`) must be authored in linear color space (not sRGB) to match the linear upload path via `IVideoDriver::getTexture()`, OR the building shader must apply `pow(color.rgb, 2.2)` gamma correction when sampling the PNG atlas. Document the chosen approach in `shader-loading.md` and cross-reference from `building-atlas-layout.md`."

---

#### MED-08 — LOD Distance Thresholds table in `3d-model-standards.md` says "switch-out" / "switch-in" but `LODNode::swapMesh()` comment only references "setMesh" — hysteresis implementation responsibility unclear
**File**: `architecture/asset-standards/3d-model-standards.md` and `architecture/graphics-architecture/scene-graph-ownership.md`
**Type**: [GAP]
**Description**: The LOD Threshold table specifies four distances per asset category (switch-out and switch-in for both LOD0↔LOD1 and LOD1↔LOD2). `scene-graph-ownership.md` says "Canonical implementation: This sequence is encapsulated in `LODNode::swapMesh()`." But `LODNode::swapMesh()` performs the mesh swap — it does not contain the distance comparison or hysteresis logic. The spec never identifies where the per-frame distance check and hysteresis comparison live: is it in `LODNode::update(float cameraDistance)` (which is not mentioned anywhere as a method), in `SceneEntityManager::update()`, in `CitySimulation`, or somewhere else? No component is designated as responsible for calling `swapMesh()` at the right time.
**Proposed resolution**: Add a `LODNode::update(float cameraDistanceSq)` method description to `scene-graph-ownership.md` that documents the hysteresis state machine (current LOD, switch-in threshold, switch-out threshold) and specifies that `SceneEntityManager::update()` calls `lodNode->update(distSq)` per frame for all live entities.

---

#### MED-09 — `buildingAtlasLayout.md` references `buildings_atlas_d.dds` mip count via `GL_TEXTURE_MAX_LEVEL = 4`, but `texture-cache.md` dispatch table shows `GL_TEXTURE_MAX_LEVEL = 4` for the primary atlas and `GL_TEXTURE_MAX_LEVEL = 3` for fallback; the `TextureCache::loadSRGB()` path must select between them at runtime
**File**: `architecture/graphics-architecture/texture-cache.md` (§GL_TEXTURE_MAX_LEVEL Dispatch Table)
**Type**: [GAP]
**Description**: The dispatch table has two rows for the building atlas (primary 4096×4096 with `GL_TEXTURE_MAX_LEVEL = 4`, and fallback 2048×2048 with `GL_TEXTURE_MAX_LEVEL = 3`). The runtime selection between primary and fallback atlas is triggered by `GL_MAX_TEXTURE_SIZE < 4096`. However, the spec does not document where this selection logic lives — in `TextureCache::loadSRGB()`? In `BuildingAssetLoader`? In `IrrlichtRenderer`? And which component holds the reference to `m_maxTextureSize` to make the comparison? If `TextureCache` does not have access to `m_maxTextureSize` from `RenderSystem`, the dispatch cannot be performed.
**Proposed resolution**: Specify: (1) which class performs the atlas selection (primary vs fallback); (2) that `TextureCache` receives `maxTextureSize` at construction from `RenderSystem::getMaxTextureSize()`, or that `BuildingAssetLoader` queries `RenderSystem::getMaxTextureSize()` and passes the correct atlas path to `loadSRGB()`; (3) the exact condition (`m_maxTextureSize >= 4096` → primary; else → fallback).

---

#### MED-10 — No spec for what happens to `m_cloudNode`'s texture when `IrrlichtRenderer` is destroyed — `device->drop()` releases the scene node but the cloud PNG `ITexture*` may remain in the linear pool
**File**: `architecture/graphics-architecture/sky-clouds.md` (§Headless / EDT_NULL Guard) and `architecture/graphics-architecture/scene-graph-ownership.md` (§Renderer-Internal Permanent Scene Nodes)
**Type**: [GAP]
**Description**: `scene-graph-ownership.md` says renderer-internal nodes (sky dome, cloud plane) are "released automatically by `device->drop()`." Irrlicht's scene node destructor calls `drop()` on the node's materials' textures — but only for `ITexture*` objects stored via `setTexture()`. The cloud PNG was loaded via `IVideoDriver::getTexture()` (linear pool, NOT `TextureCache`). When `device->drop()` is called, Irrlicht's video driver drops all textures it owns (those loaded via `getTexture()`) as part of its own cleanup. This is correct and no action is required from `IrrlichtRenderer`. However, the spec never states this — implementers may add an unnecessary explicit `driver->removeTexture()` call in `IrrlichtRenderer::~IrrlichtRenderer()` that would double-free the texture. The spec should state explicitly that the cloud texture is released by `device->drop()` and requires no manual removal.
**Proposed resolution**: Add a sentence in `sky-clouds.md` (§Headless Guard) and `scene-graph-ownership.md` (§Renderer-Internal Permanent Scene Nodes): "The cloud texture (`clouds.png`, loaded via `IVideoDriver::getTexture()`) is released by Irrlicht's video driver as part of `device->drop()`. Do NOT call `driver->removeTexture()` on this texture in `IrrlichtRenderer`'s destructor — it would double-free the texture object."

---

### LOW

---

#### LOW-01 — `irrlicht-device-lifecycle.md` construction sequence shows `IrrlichtUIBackend` as step 2, before `glewInit()` which is inside `RenderSystem` constructor body — but step 2 is listed after step 1 (RenderSystem) implying glewInit already ran
**File**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (§IrrlichtRenderer Late-Binding Pattern, Construction Sequence)
**Type**: [INCONSISTENCY]
**Description**: The construction sequence table in `irrlicht-device-lifecycle.md` lists step 1 as `RenderSystem` and step 2 as `IrrlichtUIBackend`. A separate section ("IrrlichtUIBackend construction ordering constraint") says `IrrlichtUIBackend` must be constructed AFTER `glewInit()` returns. This is correct if `glewInit()` is called inside the `RenderSystem` constructor body. However, the sequence table implies `IrrlichtUIBackend` is constructed in `main.cpp` after `RenderSystem` (i.e., after `RenderSystem`'s constructor has completed, including `glewInit()`). This ordering is correct but the table does not note the dependency. An implementer who changes the construction order (e.g., tries to construct `IrrlichtUIBackend` as a member of `RenderSystem`) will silently break the GLEW initialization ordering.
**Proposed resolution**: Add a note to step 2 in the construction sequence table: "`IrrlichtUIBackend` must be constructed in `main.cpp` AFTER `RenderSystem`'s constructor completes (which includes `glewInit()`). Do NOT construct it as a member of `RenderSystem` or before `RenderSystem` returns from its constructor."

---

#### LOW-02 — `benchmark-tool.md` Scene 1 ground plane uses a "checkerboard texture" that is never defined or cross-referenced
**File**: `architecture/graphics-architecture/benchmark-tool.md` (§Scene 1)
**Type**: [GAP]
**Description**: Scene 1 uses a "checkerboard texture … with 4x UV repetition per tile." This texture has no defined path, resolution, format, or generation method. It is not in `2d-texture-standards.md` or `building-atlas-layout.md`. If this texture must be a file on disk, it needs a spec entry. If it is procedurally generated (e.g., via `IVideoDriver::createImageFromData()`), the generation code must be documented.
**Proposed resolution**: Add a note specifying either: "Checkerboard texture is generated procedurally at runtime using `IVideoDriver::createImageFromData()` with an 8×8 black/white pattern uploaded via `addTexture()`," or define a `assets/textures/benchmark/checkerboard.png` file at a specified resolution in the asset directory.

---

#### LOW-03 — `scene-graph-ownership.md` mentions `IMeshSceneNode*` for `LODNode::m_node` but `addMeshSceneNode()` returns `IMeshSceneNode*` while `getNode()` returns `ISceneNode*` — the cast is implicit and undocumented
**File**: `architecture/graphics-architecture/scene-graph-ownership.md` (§B3D Building Assets — LOD swap contract)
**Type**: [GAP]
**Description**: The spec says "`LODNode` stores `IMeshSceneNode*` for `m_node` (to call `setMesh(IMesh*)`)". It also says "`getNode()` returns `ISceneNode*` via implicit upcast." The implicit upcast from `IMeshSceneNode*` to `ISceneNode*` is safe (public inheritance), but the spec does not document the `ISceneNode*` return type of `getNode()` or why it is `ISceneNode*` rather than `IMeshSceneNode*`. If callers need to call `setMesh()` on the returned node, they will need to cast back to `IMeshSceneNode*` — but the WARNING section says downcasts are dangerous. The spec should clarify that `LODNode::setMesh()` (the public method) internally calls `m_node->setMesh()` (on `IMeshSceneNode*`) so that external callers NEVER need to downcast the return of `getNode()`.
**Proposed resolution**: Add one sentence: "`LODNode` exposes `void swapMesh(IMesh*)` as the public LOD swap entry point so that callers never need to access `m_node` directly or downcast the `ISceneNode*` returned by `getNode()`. `getNode()` returning `ISceneNode*` is intentional — callers that need scene-node operations (transform, visibility) use the base-class interface."

---

#### LOW-04 — `billboard imposter atlas` listed in `building-atlas-layout.md` references `2d-texture-standards.md` for the mip chain spec, but `2d-texture-standards.md` has no heading "Billboard Imposter Atlas"
**File**: `architecture/asset-standards/building-atlas-layout.md` (§Billboard Imposter Atlas) and `architecture/asset-standards/2d-texture-standards.md`
**Type**: [GAP]
**Description**: `building-atlas-layout.md` says "See `architecture/asset-standards/2d-texture-standards.md` 'Billboard Imposter Atlas' section for the full authoring spec (bake angles, elevation, lighting, cell padding, usable content area, naming convention)." A search of `2d-texture-standards.md` finds no heading with this exact title. The billboard-related content is present (references to 1024×128 DXT5 sRGB, 4-level mip chain, `_billboard` suffix) but not under a dedicated heading. Cross-references that use heading names that do not exist produce documentation dead-ends.
**Proposed resolution**: Add a `## Billboard Imposter Atlas` section heading to `2d-texture-standards.md` that aggregates all billboard-specific requirements (currently scattered in several places). Update `building-atlas-layout.md`'s cross-reference to use the correct heading anchor.

---

#### LOW-05 — `irrlicht-device-lifecycle.md` and `procedural-terrain.md` both describe the hover highlight bounding box rule but with different scope
**File**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` and `architecture/graphics-architecture/scene-graph-ownership.md`
**Type**: [DUPLICATE]
**Description**: The `irrlicht-device-lifecycle.md` §Hover Highlight section describes the full lifecycle of `m_hoveredTileMesh` including the `setTileHoverHighlight()` update protocol. `scene-graph-ownership.md` §Hover Highlight — Single Buffer, Pre-Allocated also describes the same lifecycle. Both files describe the same allocation, the same update pattern, and the same draw-via-`drawMeshBuffer` rule. While each has slightly different emphasis (lifecycle vs geometry), the two descriptions can diverge if one is updated and the other is not.
**Proposed resolution**: Designate one file as the normative lifecycle spec for `m_hoveredTileMesh` (recommend `scene-graph-ownership.md` since it already covers all scene-graph ownership rules) and reduce the other to a forward-reference. Move all definitive lifecycle text to the designated file.

---

#### LOW-06 — `testability-architecture.md` `IRenderer` interface is referenced by `CameraController testability` but `IRenderer.h` is not listed as the source of `ScreenRect`
**File**: `architecture/testing/testability-architecture.md` (§QueryPanel testability)
**Type**: [GAP]
**Description**: `testability-architecture.md` says "`ScreenRect` is added to `IRenderer.h` by Phase 9b Deliverable B as `struct ScreenRect { int x{0}, y{0}, w{0}, h{0}; }`". However, `IUIBackend.h` already defines `struct Rect { int x{0}, y{0}, w{0}, h{0}; }` (used by `getElementRect()`). Two distinct POD structs with identical fields but different names exist in the public interfaces. An implementer may confuse `Rect` (from `IUIBackend`) with `ScreenRect` (from `IRenderer`) or inadvertently introduce an implicit conversion. The rationale for having two separate structs is not stated.
**Proposed resolution**: Add a note in `testability-architecture.md` (and `IRenderer.h` spec) explaining why `ScreenRect` cannot reuse `IUIBackend::Rect`: "Although the fields are identical, `ScreenRect` lives in `IRenderer.h` (rendering domain) and `Rect` lives in `IUIBackend.h` (UI domain). Including `IUIBackend.h` from `IRenderer.h` would create a cross-domain header dependency. The duplicate definition is intentional."

---

## Summary Table

| ID | File | Type | Severity |
|---|---|---|---|
| CRIT-01 | `procedural-terrain.md` | GAP | CRITICAL |
| CRIT-02 | `texture-cache.md` | GAP | CRITICAL |
| CRIT-03 | `sky-clouds.md` | PROBLEM | CRITICAL |
| CRIT-04 | `texture-cache.md` | INCONSISTENCY | CRITICAL |
| CRIT-05 | `scene-graph-ownership.md` | INCONSISTENCY | CRITICAL |
| HIGH-01 | `procedural-terrain.md` | GAP | HIGH |
| HIGH-02 | `irrlicht-device-lifecycle.md` | GAP | HIGH |
| HIGH-03 | `procedural-terrain.md` | PROBLEM | HIGH |
| HIGH-04 | `scene-graph-ownership.md` | GAP | HIGH |
| HIGH-05 | `texture-cache.md` | GAP | HIGH |
| HIGH-06 | `shader-loading.md` | GAP | HIGH |
| HIGH-07 | `benchmark-tool.md` | PROBLEM | HIGH |
| HIGH-08 | `shader-loading.md` | PROBLEM | HIGH |
| HIGH-09 | `model-validator-tool.md` | INCONSISTENCY | HIGH |
| HIGH-10 | `irrlicht-device-lifecycle.md` | MISSING | HIGH |
| MED-01 | `irrlicht-device-lifecycle.md` / `scene-graph-ownership.md` | GAP | MEDIUM |
| MED-02 | `procedural-terrain.md` / `scene-graph-ownership.md` / `3d-model-standards.md` | DUPLICATE | MEDIUM |
| MED-03 | `sky-clouds.md` | GAP | MEDIUM |
| MED-04 | `benchmark-tool.md` | GAP | MEDIUM |
| MED-05 | `3d-model-standards.md` / `procedural-terrain.md` | INCONSISTENCY | MEDIUM |
| MED-06 | `irrlicht-device-lifecycle.md` | INCONSISTENCY | MEDIUM |
| MED-07 | `building-atlas-layout.md` | GAP | MEDIUM |
| MED-08 | `3d-model-standards.md` / `scene-graph-ownership.md` | GAP | MEDIUM |
| MED-09 | `texture-cache.md` | GAP | MEDIUM |
| MED-10 | `sky-clouds.md` / `scene-graph-ownership.md` | GAP | MEDIUM |
| LOW-01 | `irrlicht-device-lifecycle.md` | INCONSISTENCY | LOW |
| LOW-02 | `benchmark-tool.md` | GAP | LOW |
| LOW-03 | `scene-graph-ownership.md` | GAP | LOW |
| LOW-04 | `building-atlas-layout.md` / `2d-texture-standards.md` | GAP | LOW |
| LOW-05 | `irrlicht-device-lifecycle.md` / `scene-graph-ownership.md` | DUPLICATE | LOW |
| LOW-06 | `testability-architecture.md` | GAP | LOW |

---

## Subsystems With No Spec (MISSING)

The following rendering subsystems are referenced in passing but have no dedicated specification file. These are gaps that will require spec work before implementation:

1. **Shadow system**: `irrlicht-device-lifecycle.md` sets `params.Stencil = true` and the benchmark uses `addShadowVolumeSceneNode()` in Scene 2, but no architecture file specifies shadow volume usage in the game runtime (which buildings cast shadows, update frequency, perf cost, interaction with the LOD system). No `shadow-volumes.md` exists.

2. **Post-processing / render effects pipeline**: No spec for any post-processing pass (bloom, tone-mapping, FXAA). `params.AntiAlias = 4` implies MSAA but there is no spec for how MSAA interacts with the custom sRGB shader path or the raw `drawMeshBuffer()` overlay calls.

3. **Lighting model for buildings**: `shader-loading.md` defines terrain shader constants (units 0–8) and a building shader entry for unit 0 (diffuse) and unit 1 (normal map), but no `lighting.frag` specification exists beyond "constant color fragment shader." The building shader's lighting model (diffuse + specular, PBR roughness/metallic, or Phong) is unspecified. Phase 6 is referenced for lighting, but no architecture file documents what the building and road shaders actually compute.

4. **Road shader full specification**: `shader-loading.md` lists `road.vert` / `road.frag` and mentions a `RoadShaderCallback`, but no document specifies what `road.frag` does beyond sampling `u_diffuseMap` and `u_srgbLinear`. UV tiling factor (2×), road marking decal blending from the marking atlas, and lane color are mentioned in `3d-model-standards.md` and `building-atlas-layout.md` but not in a normative shader specification.

5. **UI rendering pipeline (`IrrlichtUIBackend`)**: The GL_ARRAY_BUFFER VAO/VBO construction in `IrrlichtRenderer.h`'s construction sequence note is the only technical description of `IrrlichtUIBackend`'s rendering mechanism. A dedicated `ui-rendering-pipeline.md` would prevent the `ui_quad.vert/frag` shader contract from being scattered across `shader-loading.md` and `irrlicht-device-lifecycle.md`.


---

## 8. Testing Architecture Review

**Reviewer:** Senior C++ Test Engineer

---

# Test Engineering Spec Review

**Scope**: `/workspace/architecture/testing/` (all files), plus
`/workspace/architecture/game-design/minimum-viable-simulation.md`,
`/workspace/architecture/game-design/save-system.md`,
`/workspace/architecture/ui-ux/ui-manager.md`,
`/workspace/architecture/ui-ux/input-arbitration.md`.

---

## File-by-File Findings

---

### `architecture/testing/framework.md`

---

**[GAP] — MEDIUM**
No test coverage spec for `ManualClock` itself.
`ManualClock` is a critical test double used in `NotificationManager`, `AudioSystem`,
`SettingsPanel`, and `UIManager` fixtures. Its behaviour (advance, nowSeconds) is assumed
correct but no self-test cases are specified the way they are for `ManualRNG`
(which has 6 named self-tests in `manual_rng_test.cpp`).
_Proposed resolution_: Add a `manual_clock_test.cpp` (or append to `manual_rng_test.cpp`)
with cases analogous to the ManualRNG self-tests: monotonicity, accumulation across
multiple advances, and verification that `nowSeconds()` returns the exact accumulated value.

---

**[GAP] — MEDIUM**
`framework.md` specifies `aitown_add_tests()` as the canonical registration helper but does
not specify what the macro must do when `LABEL` is not one of the three allowed values
(`unit`, `integration`, `requires-opengl`). Only the `!AITOWN_TEST_LABEL` (missing) guard
is shown; no guard rejects an invalid label string.
_Proposed resolution_: Add a `cmake_parse_arguments` validation block:
`if(NOT AITOWN_TEST_LABEL MATCHES "^(unit|integration|requires-opengl)$") → message(FATAL_ERROR ...)`.
Document this in the macro spec.

---

**[PROBLEM] — MEDIUM**
The "one-label-per-target" rule and the prohibition on mixing unit and integration tests in
one binary are documented for human readers only. The macro `aitown_add_tests()` does not
mechanically enforce this. A developer who adds an integration test source file to
`simulation_tests` (labelled `unit`) will pass CI without a warning.
_Proposed resolution_: Either enforce via CMake (naming convention guard or a source-file
allowlist property) or document explicitly that no mechanical guard exists and the rule
relies on code review.

---

**[GAP] — LOW**
The note about `audio_tests` Phase 10 extension lists 4 additional files
(`crossfade_interrupted_formula_test.cpp`, `stinger_milestone_test.cpp`,
`audio_stream_bar_boundary_test.cpp`, `notification_sfx_efx_bypass_test.cpp`) but does not
specify a corresponding `DISCOVERY_TIMEOUT` override for the Phase-10 expanded binary. The
Phase-3 `simulation_tests` binary overrides to 60 s with rationale ("8-file binary with
RapidCheck property tests under coverage instrumentation"). The Phase-10 `audio_tests`
binary will also have 8+ source files including RapidCheck tests; whether the default 30 s
discovery timeout is sufficient is unaddressed.
_Proposed resolution_: Explicitly state whether `audio_tests` needs a `DISCOVERY_TIMEOUT`
override after Phase 10, or confirm 30 s is adequate for the audio test binary.

---

### `architecture/testing/coverage.md`

---

**[INCONSISTENCY] — CRITICAL**
The system prompt and `CLAUDE.md` both state the coverage gate is **80%** ("`coverage gate:
lcov 80%`") and `coverage.md` Phase 5 section confirms the Phase 5 gate is 80%. However,
`coverage.md` also states the **target range is 95–98%** at the top (Phase 6+), and
`CLAUDE.md` confirms `make test` enforces **≥95%**. The system prompt in the agent
configuration header says "80% — Linux only". This creates an inconsistency: the agent
system prompt presents 80% as the standing gate, while `coverage.md` + `CLAUDE.md` agree
that 95% is the gate from Phase 6 onward.
_Proposed resolution_: Update the agent system prompt (the project instructions header, not
a spec file) to read "95% (Phase 6+, 80% at Phase 5, informational at Phase 4 and below)".
This is an agent-prompt inconsistency, not a spec-internal inconsistency; all three spec
files agree internally.

---

**[GAP] — HIGH**
The Phase 4 `src/ui/` 25% gate uses `lcov --list` output parsed with `awk -F'|'`.
`coverage.md` documents a preflight check that validates the `'|'` delimiter is present,
but the spec acknowledges: "if the format changes, $NF+0 coercion produces 0 → gate FAILS
with misleading '0% coverage' message." There is no equivalent preflight gate for Phase 5
and Phase 6 total-line coverage (which use the direct `.info` file `LH`/`LF` parser, not
`lcov --list`). If `lcov --capture` produces a `.info` file with zero entries (e.g., due
to a linker issue stripping `.gcda` files), the total coverage awk returns `0` and the gate
correctly fails — but the failure message says "0% < 80%" rather than "no coverage data
found". The Phase 6 spec adds a `src/simulation/ SF preflight` check but there is no
analogous preflight for `src/terrain/` or `src/ui/` SF entries at Phase 5 and 6.
_Proposed resolution_: Add preflight `grep -q "SF:.*src/terrain/"` and
`grep -q "SF:.*src/ui/"` checks before the Phase 5 and Phase 6 awk gates, mirroring the
`src/simulation/` preflight already specified for Phase 6.

---

**[GAP] — MEDIUM**
The coverage gate sequence (`lcov --remove → --list → genhtml → awk gate`) is specified in
`coverage.md` and in the `CLAUDE.md` running-tests section. The CI YAML itself is not
validated against `coverage.md` by any spec check. If the CI job is edited to reorder steps
(e.g., gate before genhtml), the HTML artifact is silently lost. There is no contract test
or CI lint step that verifies this ordering in the spec.
_Proposed resolution_: Document the mandatory step order as a numbered constraint (not just
prose) in `coverage.md`, and reference it from the CI architecture file as a constraint
that any CI job edit must preserve.

---

**[GAP] — MEDIUM**
`coverage.md` excludes `'*/src/rendering/*'`, `'*/src/audio/*'`, and `'*/src/platform/*'`
from the coverage gate but does NOT exclude `src/interfaces/`. The `IUIBackend.h`,
`ICitySimulation.h`, and `ITerrainRNG.h` pure-virtual interface headers live in
`src/interfaces/`. Coverage data attributed to inline virtual destructors, inline constants,
or struct definitions in these headers may appear in the filtered report and affect the
total percentage. `testability-architecture.md` notes "`src/interfaces/` is not excluded
from lcov, so coverage is captured correctly under the 80% gate" — but no spec describes
what happens when interface headers have partially-covered inline code (e.g.,
`kInvalidUIElement`, `Rect` struct members) that the tests do not exercise.
_Proposed resolution_: Add a note in `coverage.md` that `src/interfaces/` headers
contribute to the total gate, and specify whether inline-only headers (pure-virtual
interfaces) should be excluded or accepted as contributing denominator lines.

---

**[GAP] — LOW**
`coverage.md` specifies the `--ignore-errors` flag as `mismatch,inconsistent` in the
Phase 5 section, but `CLAUDE.md` (Running Tests section) adds `version` as a third comma-
separated value: `mismatch,inconsistent,version`. The divergence means a developer
following `coverage.md` exactly will get GCC/gcov version-mismatch stderr noise that
`CLAUDE.md` suppresses.
_Proposed resolution_: Update `coverage.md` to match `CLAUDE.md`: add `version` to the
`--ignore-errors` list and add a comment explaining it suppresses GCC/gcov version-string
differences.

---

**[DUPLICATE] — LOW**
The lcov exclusion patterns (`'/usr/*'`, `"*/.fetchcontent_cache/*"`, `'*/tests/*'`, etc.)
are specified verbatim in three places: `coverage.md` (local developer script),
`CLAUDE.md` (Running Tests section), and referenced from CI YAML. This triplication creates
drift risk — the Phase 10b addition of CamelCase `'*/Mock*.h'` and `'*/Manual*.h'` patterns
is documented in `coverage.md` but requires three synchronised updates. Currently all three
appear consistent, but the spec has no single-source-of-truth mechanism.
_Proposed resolution_: Designate `coverage.md` as the single source of truth for the
exclusion pattern list; `CLAUDE.md` and CI YAML cross-reference it rather than restating it.

---

### `architecture/testing/testability-architecture.md`

---

**[INCONSISTENCY] — HIGH**
`testability-architecture.md` (line 4 approximation) states:
> `IUIBackend.h` lives in `src/interfaces/` (moved from `src/ui/` in Phase 10b Feature 3)

`ui-manager.md` §IUIBackend Header Placement states:
> `IUIBackend.h` is placed in `src/ui/` (not `src/interfaces/`) because it is part of the
> UI subsystem abstraction boundary.

These two authoritative spec files directly contradict each other on the canonical location
of `IUIBackend.h`. The contradiction exists within the current checked-in spec.
_Proposed resolution_: Resolve to a single canonical location. The Phase 10b Feature 3
intent (per `testability-architecture.md`) is to move it to `src/interfaces/`. The
`ui-manager.md` statement predates this plan. Update `ui-manager.md` to remove the
conflicting statement and cross-reference `testability-architecture.md` as the authority.

---

**[GAP] — HIGH**
No mock contract is specified for `ISaveSystem`. `save-system.md` defines `ISaveSystem`
with `loadMostRecentSave()`, `autoSave()`, and `saveToSlot(int)`. Tests that exercise
`UIManager`'s unsaved-changes quit flow, load-game flow, and auto-save tick wiring need a
`MockSaveSystem`. The spec documents the `UIManager`→`SaveSystem` wiring contract but
defines no mock, no test file location, and no named test cases for the save system's
`UIManager` integration paths.
_Proposed resolution_: Add a `MockSaveSystem` contract to `testability-architecture.md`
(source location: `tests/ui/MockSaveSystem.h`); add named test cases for
`UnsavedChanges_QuitToDesktop_ShowsModal`, `SaveSystem_AutoSave_TriggersOnBudgetTick5`,
and `SaveSystem_LoadResult_Corrupted_StaysInMainMenu`.

---

**[GAP] — HIGH**
No test coverage is specified for the `SaveSystem` itself — `save-system.md` defines
detailed serialization requirements (population milestone flags, speed multiplier, building
variant counters, `LoadResult` enum) but `testability-architecture.md` does not document
a `save_system_test.cpp` fixture, mock contracts for `ICitySimulationSerializable`, or the
`save_system_real_test.cpp` companion file.
`coverage.md` §Coverage Test Placement Convention references `save_system_real_test.cpp`
as an example of the stub/real split pattern, but this is only a naming example — no test
cases, fixture setup, or round-trip invariants are specified.
_Proposed resolution_: Add a `SaveSystemTest` section to `testability-architecture.md`
specifying: fixture setup (MockClock, temporary directory injection), named test cases
(`SaveSystem_RoundTrip_PreservesFullCityState`, `SaveSystem_LoadResult_NoSaveFound`,
`SaveSystem_LoadResult_Corrupted`), and the stub/real split pattern.

---

**[GAP] — HIGH**
No test coverage is specified for `EventReceiver` — the class that translates Irrlicht
`SEvent` to `InputEvent` and synthesises `EGET_BUTTON_CLICKED` → `MouseButtonDown` events.
`input-arbitration.md` defines an extremely detailed `EventReceiver` contract (RMB drag
tracking `m_rmbDragActive`/`m_rmbMoved`, button-click synthesis, RMB-up always forwarded
to CameraController). These are pure logic decisions that could be tested without a display
if `EventReceiver` is given a seam to inject a callback instead of holding a raw
`CameraController*` and `UIManager*`.
_Proposed resolution_: Either (a) add `EventReceiver` to the testability architecture with
an injection seam for the callback layer, or (b) explicitly document that `EventReceiver`
is tested via integration tests requiring EDT_NULL with the full stack.

---

**[GAP] — MEDIUM**
`ICitySimulation` exposes `getTrafficDemandFactor(ZoneType)` (added in Phase 11 for save
round-trip tests) but no test fixture or named test case is specified for this method's
contract. The comment says it is exposed "solely for Phase 11 save/load round-trip tests"
but no corresponding test structure is specified.
_Proposed resolution_: Add a named test case `SaveSystem_RoundTrip_PreservesTrafficRollingWindow`
to the save system test spec section.

---

**[GAP] — MEDIUM**
`testability-architecture.md` specifies `MockCitySimulation` lives in
`tests/ui/MockCitySimulation.h`, but `ICitySimulation` also has simulation-domain users
(e.g., `UIManagerDeficitIntegrationTest` uses `MockCitySimulation` from `tests/ui/`). The
spec does NOT address whether simulation tests that need a lightweight `ICitySimulation`
mock (e.g., for testing cross-subsystem paths that originate in `CitySimulation`) should
also use `tests/ui/MockCitySimulation.h` or maintain a separate simulation-domain copy.
This creates ambiguity about whether `simulation_tests` CMake target can include
`tests/ui/MockCitySimulation.h` without violating the include-directory separation.
_Proposed resolution_: Explicitly document whether `MockCitySimulation` is shared across
`simulation_tests` and `ui_tests` targets, and if so, confirm both targets have the
`tests/ui/` path in their `target_include_directories`.

---

**[GAP] — MEDIUM**
The `SettingsPanelTest` fixture specifies `StrictMock<MockAudioSystem>` for the three
volume-control tests but does not specify a mock for `IKeyBindings` or the file I/O seam
(`keybindings.json` read/write). The `KeyBindings` test cases (#1–#5) require file I/O
isolation (the spec says "keybindings.json is NOT written" for conflict cases, and "written
exactly once" for swap). There is no `IKeyBindingsStorage` seam defined, meaning tests
would actually write to the filesystem at test time.
_Proposed resolution_: Define an `IKeyBindingsStorage` seam (or document the
`KeyBindings::setStorageRoot(path)` pattern) to allow tests to redirect writes to a
temporary directory. Specify the seam in `testability-architecture.md`.

---

**[GAP] — MEDIUM**
No property-based tests are specified for the zoning desirability invariant beyond the
single bullet: "Desirability scores must remain in [0, 100] for any valid zone + adjacency
configuration." No generator strategy, no GTest fixture class name, no file location
(`tests/simulation/zoning_test.cpp`?), and no RapidCheck generator for `ZoneAdjacencyConfig`
are documented.
_Proposed resolution_: Expand the zoning invariant in `property-based-tests.md` to include
fixture name, generator strategy, and specific adjacency edge cases (all-same zone, mixed
R/C/I, zone with no neighbours).

---

**[PROBLEM] — MEDIUM**
The `UIManagerDeficitIntegrationTest` fixture mandates `TearDown()` resets `ui_` before
mock destruction. The eight test cases in this fixture all use `NiceMock<MockAudioSystem>`
and `NiceMock<MockUIBackend>`. However, the spec does not specify the `TearDown()` reset
order when BOTH `MockUIBackend` AND `MockAudioSystem` are held by the fixture. The comment
says "MockUIBackend destruction while UIManager holds a pointer causes use-after-free" but
does not address whether `MockAudioSystem` destruction order matters. If `UIManager` also
calls audio methods during its destructor (e.g., stopping a playing sound on teardown),
`MockAudioSystem` being destroyed before `UIManager` is reset could produce a dangling
pointer.
_Proposed resolution_: Explicitly document the required TearDown order for all multi-mock
fixtures: reset `ui_` (or the class-under-test) to `nullptr` first, then let mocks destruct
in declaration-reverse order.

---

**[GAP] — LOW**
The `QueryPanel` test cases (#1–#4 in `testability-architecture.md`) test
`computePanelPosition()` as a pure function. But `InspectorPanel::populate()` — which
creates UI elements via `IUIBackend` — has no named test cases specified for it, other than
being mentioned as a call site. `populate()` exercises `addStaticText`, `setElementText`,
`setElementMonoFont`, `setElementTextColor` — high-priority coverage paths for the 95% gate.
_Proposed resolution_: Add named test cases for `InspectorPanel::populate()` covering
road tile, zone tile, empty tile, and service building results.

---

**[GAP] — LOW**
`testability-architecture.md` does not specify test cases for `UIManager::setLoadingTerrain(bool)`.
This method gates the entire `update()` dispatch loop. A missing test means the loading
guard could be inadvertently removed without breaking any test.
_Proposed resolution_: Add a named test case:
`UIManager_LoadingTerrainGate_UpdateReturnsEarlyWithoutPolling` — verify that when
`setLoadingTerrain(true)`, a subsequent `ui_.update(dt)` call does NOT invoke
`sim_.pollPendingNotification()` or `sim_.getConsecutiveDeficitMonths()`.

---

### `architecture/testing/headless-ci-testing.md`

---

**[GAP] — MEDIUM**
`headless-ci-testing.md` specifies the three CTest filter commands for CI but does not
document the expected minimum test count for each label bucket. If label routing is broken
(e.g., all tests end up unlabelled), `ctest -LE "integration|requires-opengl"` still passes
(it runs everything). The Phase 1 integration routing verification step ("at least 1 test")
is mentioned in `framework.md` but not in this file, and no equivalent minimum-count
requirement is stated for the `unit` and `requires-opengl` labels.
_Proposed resolution_: Add a `Minimum test count verification` section that documents the
expected minimum count per label and the CI step that enforces it (or cross-reference the
label routing verification step from `framework.md` and CI YAML).

---

**[GAP] — LOW**
The containerised CI section (Phase 11b) describes the temporary `test-container-xvfb` job
but does not specify what happens if the Phase 11b spike PR fails that job. There is no
documented rollback plan or fallback to non-container CI for the `requires-opengl` tests.
_Proposed resolution_: Add a one-line note: "If `test-container-xvfb` fails, the Phase 11b
container migration is blocked; `build-linux` and `coverage-linux` remain on non-container
mode until the xvfb issue is resolved."

---

### `architecture/testing/property-based-tests.md`

---

**[GAP] — HIGH**
The traffic invariant is one bullet: "For any connected road graph, pathfinding must return
a path of finite length for any source/destination pair." No generator strategy is
specified, no fixture class name, no test file (`tests/simulation/traffic_test.cpp`?), no
RapidCheck generator for road graph topologies (grid, ring, tree, disconnected), and no
treatment of disconnected source/destination pairs (which should presumably return "no path"
rather than "finite path"). The invariant as written is incomplete — it does not distinguish
reachability from connectivity.
_Proposed resolution_: Expand the traffic invariant to specify: graph topology generator,
expected return type when no path exists, whether the invariant tests only connected graphs
or also disconnected cases, and the GTest fixture name.

---

**[GAP] — HIGH**
No property-based tests are specified for population growth or density unlocking. The
population growth invariant should cover: population is monotonically non-decreasing while
demand is positive; population does not grow beyond map capacity; density tier unlocks fire
at the correct consecutive-month threshold. These are simulation invariants with combinatorial
input spaces that are well-suited to RapidCheck.
_Proposed resolution_: Add a "Population growth invariants" subsection to
`property-based-tests.md` with at least: (a) a monotonicity invariant and (b) a density
unlock firing-threshold invariant.

---

**[GAP] — MEDIUM**
The economy property-based tests are extremely detailed (correct, good coverage), but the
`BudgetDeficitWarn` notification dispatching is not covered by any property test. The
dispatch condition (`budget_surplus_pct ≤ −0.25`) is a threshold gate that interacts with
RNG (service degradation), loan issuance, and tick timing — a property test verifying that
the warn notification is enqueued exactly once per qualifying tick (not zero times, not
twice) would improve confidence.
_Proposed resolution_: Add a `BudgetWarn_QueuedExactlyOncePerQualifyingTick` RapidCheck
property to `property-based-tests.md` and assign it to `tests/simulation/economy_test.cpp`.

---

**[GAP] — LOW**
The `LoanGate_FiresAtExactly120Seconds` fixed-seed boundary test specifies IEEE 754 integer
advances (`119.0 + 1.0`) to avoid floating-point precision issues. However, the spec does
not address what happens at the boundary of `ManualClock` overflow: if a test advances the
clock by a very large double value (e.g., `1e308`), `nowSeconds()` would return a `double`
near infinity, and the `>= 120.0` check still passes (infinity >= 120.0 is true). This is
an unlikely but latent edge case for clock tests that use large advances.
_Proposed resolution_: Add a `ManualClock` overflow / saturation policy note (or confirm
that `ManualClock::nowSeconds()` is expected to return IEEE 754 infinity for very large
cumulative advances, and that this is acceptable in test contexts).

---

### `architecture/testing/procedural-generation-seeds.md`

---

**[MISSING] — HIGH**
`procedural-generation-seeds.md` is 5 lines and functions only as a summary pointer to
`property-based-tests.md`. It does not document: the seed registration table (which fixed
seeds are pinned and why), the process for pinning a new seed after a RapidCheck failure,
whether seeds are shared across terrain, simulation, and audio tests or kept per-domain,
or the relationship between the seed used for `std::mt19937_64` initialization and the
`uint64_t` parameter accepted by `TerrainGenerator`.
_Proposed resolution_: Expand this file significantly. Add: (a) a seed registry table with
at least the primary terrain regression seed `0xDEADBEEF00000001` documented with the
reason it was chosen; (b) the workflow for adding a new regression seed after a CI failure;
(c) the policy on seed reuse across test domains.

---

**[GAP] — MEDIUM**
There is no spec for how RapidCheck's own seed is controlled in CI. RapidCheck by default
uses a time-based seed, which means property test failures are non-reproducible without the
printed hex seed. The spec says "print `// Reproduce with seed: 0x<hex>`" but does not
document where this output appears in CI logs, whether GTEST_OUTPUT XML captures it, or
whether CI should use a fixed RapidCheck seed (via `RC_PARAMS=seed=<value>`) for
reproducible CI runs.
_Proposed resolution_: Add a policy on whether CI uses a fixed or random RapidCheck seed
and document how to extract the failing seed from CI logs (grep pattern, log line format).

---

## Cross-File Findings

---

**[INCONSISTENCY] — HIGH**
`testability-architecture.md` states `IUIBackend.h` moved to `src/interfaces/` in Phase
10b Feature 3. The same file states `MockUIBackend` lives in `tests/ui/MockUIBackend.h`
(renamed from lowercase in Phase 10b). `ui-manager.md` §IUIBackend Header Placement
states `IUIBackend.h` is in `src/ui/`. These two files give different answers for the same
file. This is distinct from issue TA-01 above: it also impacts `coverage.md` which notes
"`src/interfaces/` is not excluded from lcov, so coverage is captured correctly under the
80% gate" — if `IUIBackend.h` were in `src/ui/` instead, it would be in the gate scope
under `src/ui/` rather than `src/interfaces/`, changing which exclusion pattern applies.
_See_ "testability-architecture.md [INCONSISTENCY] — HIGH" above for the primary entry.

---

**[MISSING] — HIGH**
No integration test scope is specified for the full `UIManager` → `CitySimulation` →
`SaveSystem` round-trip. `testability-architecture.md` defines unit-level fixtures for each
subsystem in isolation, but the integration test spec (`tests/integration/`) only covers
the Phase 1 compile-check. No integration test is specified that exercises the complete
"place zone → budget tick → deficit notification → modal → dismiss → save" flow using the
EDT_NULL Irrlicht device. This is the most important end-to-end path for city simulation
correctness, and it has no spec-level integration test requirement.
_Proposed resolution_: Add an integration test spec section to either
`testability-architecture.md` or `headless-ci-testing.md` defining at least one full-stack
integration test fixture (`CitySimulationIntegrationTest`) with EDT_NULL Irrlicht, null
audio, and real `CitySimulation` + `UIManager` instances (no mocks for the domain logic).

---

**[MISSING] — HIGH**
No test coverage is specified for the `SaveSystem` ↔ `UIManager` wiring described in
`save-system.md`. Specifically:
- Auto-save on budget tick 5 (`consumeBudgetTicks()` return value forwarded to
  `SaveSystem::onBudgetTick()`)
- Auto-save on forced loan dialog activation (before modal is shown)
- "Load Last Save" button grayed when `LoadResult::NoSaveFound`
- Unsaved changes indicator (amber dot) toggled by `setUnsavedChanges()`

None of these paths have named test cases. All four are exercised by `UIManager::update()`
and can be tested with `MockSaveSystem` + `NiceMock<MockCitySimulation>` without a display.
_Proposed resolution_: Add a `UIManagerSaveIntegrationTest` fixture in
`testability-architecture.md` with the four named test cases above.

---

**[MISSING] — HIGH**
`save-system.md` specifies three serialized fields that are Phase 11 scope:
`population_milestone_fired`, `speed_multiplier`, and `building_variant_counters`. A single
named test is referenced: `SaveSystem_RoundTrip_PreservesFullCityState`. However:
1. No fixture setup is documented (which class provides the simulation state? how is a
   temporary save directory injected?).
2. No failure mode test is documented (what if the JSON is missing the
   `building_variant_counters` key — backward compatibility from Phase 10 saves?).
3. No checksum/corruption test is documented (for `LoadResult::Corrupted`).

All three gaps exist at the spec level.
_Proposed resolution_: Expand the save system test spec to cover backward-compatibility
deserialization (missing optional keys use defaults) and corruption detection.

---

**[INCONSISTENCY] — MEDIUM**
`testability-architecture.md` says `NotificationManager::dismissCriticalToast(UIElementHandle)`
"is the production API called by the UI event handler when the player clicks, presses Enter,
or presses Delete on a CRITICAL toast; it is not a test-only backdoor."
`input-arbitration.md` §Priority 2 states CRITICAL toast dismiss fires "click, Enter, or
Delete" but does not say who calls `dismissCriticalToast`. The event routing from
`UIManager::onEvent()` at Priority 2 to `NotificationManager::dismissCriticalToast()` is
unspecified in `input-arbitration.md` — it is only documented in `testability-architecture.md`.
Tests that verify the Priority 2 dismiss path (as an event-routing test) would need to
exercise `UIManager::onEvent()` with Enter/Delete input events and verify
`dismissCriticalToast()` is called. No such test is specified.
_Proposed resolution_: Add named test cases for Priority 2 event routing:
`Priority2_EnterKey_DismissesCriticalToast` and
`Priority2_ModalActive_EnterKey_DoesNotDismissCriticalToast` to
`testability-architecture.md` (or the world_interaction_test.cpp mapping section).

---

**[MISSING] — MEDIUM**
No test coverage spec exists for the `input-arbitration.md` §RMB drag suppression in
non-gameplay states. The spec says `EventReceiver` guards RMB down with
`UIManager::isGameplayOrPaused()` — but `UIManager::isGameplayOrPaused()` is not listed
as a method on `ICitySimulation` or `UIManager` in `testability-architecture.md`. It is
unclear whether this method is on `UIManager` directly (concrete, not behind an interface)
or needs to be tested via `EventReceiver` integration.
_Proposed resolution_: Document whether `isGameplayOrPaused()` is tested via a unit test
on `UIManager` or only via integration. If unit-testable, add it to the world_interaction
test mapping.

---

**[GAP] — MEDIUM**
`minimum-viable-simulation.md` specifies three map sizes (Small 128×128, Medium 512×512,
Large 1024×1024). No test coverage spec addresses map-size parameterisation. The terrain
generator tests use fixed seeds but do not parameterise over map size. A Large 1024×1024
map may exercise different code paths (chunk boundaries, BFS distance limits for service
coverage) than the default Medium size.
_Proposed resolution_: Add a note in `property-based-tests.md` or the terrain generator
section of `testability-architecture.md` specifying which tests are parameterised over
map size vs. which assume the default Medium size.

---

**[INCONSISTENCY] — MEDIUM**
`testability-architecture.md` specifies `MockAudioSystem` lives in
`tests/simulation/MockAudioSystem.h`. The `UIManagerDeficitIntegrationTest` section says:
"Include path: `MockAudioSystem` is in `tests/simulation/MockAudioSystem.h` (NOT
`tests/ui/` — audio mocks live alongside simulation mocks)."
However, the `SettingsPanelTest` fixture section says `StrictMock<MockAudioSystem>` is
used but does not explicitly state the include path. The `audio_tests` target includes
`tests/simulation/` in its include directories (per `framework.md`), but `ui_tests` may
not include `tests/simulation/` by default. If `ui_tests` does not include
`tests/simulation/`, the `SettingsPanelTest` and `NotificationSFX` test files will fail
to find `MockAudioSystem.h`.
_Proposed resolution_: Verify that `tests/simulation/` is listed in
`target_include_directories(ui_tests ...)` (per `framework.md`, it is listed as
`tests/simulation/`). Confirm this explicitly in `testability-architecture.md`'s
`SettingsPanelTest` fixture section.

---

**[MISSING] — MEDIUM**
`input-arbitration.md` documents the Hover State Switching section (IGUIButton Image Swap
via `EGET_ELEMENT_HOVERED`/`EGET_ELEMENT_LEFT`). This logic lives in `IrrlichtUIBackend`
(a rendering-layer class). No test coverage is specified for it. Hover state switching is
an `IrrlichtUIBackend`-internal concern and cannot be tested through `IUIBackend` (which
has no hover methods). This path would only be exercised by integration or OpenGL tests.
_Proposed resolution_: Either add a `requires-opengl` integration test for hover state
switching to the `opengl_tests` target, or explicitly document that this is untestable
headlessly and accepted as an untested rendering detail.

---

**[GAP] — LOW**
`save-system.md` documents "Quit to Desktop / Quit to Main Menu safety" — a blocking
`ModalDialog::showUnsavedQuit()` with three options (Save and Quit, Quit Without Saving,
Cancel). None of the three action paths have named test cases in `testability-architecture.md`.
The `UnsavedChanges_QuitToDesktop_ShowsModal` gap is identified above as a missing
`MockSaveSystem` test, but the `Quit Without Saving` (no modal for no unsaved changes) and
`Cancel` (modal dismissed, game continues) paths are also unspecified.
_Proposed resolution_: Add three named test cases for the quit-safety modal paths to the
`UIManager` test coverage section.

---

**[GAP] — LOW**
The `UIManager::transitionToMainMenu()` call order is specified in `ui-manager.md`:
(1) `m_audio->transitionToMainMenu()`, (2) `onNewGame()`, (3) save-state refresh,
(4) `m_mainMenu->show()`. No test verifies this ordering. A test using `::testing::InSequence`
on `MockAudioSystem` and `MockUIBackend` would catch regressions where, e.g., `m_mainMenu->show()`
is called before `m_audio->transitionToMainMenu()`.
_Proposed resolution_: Add a named test case `UIManager_TransitionToMainMenu_CallOrder`
using `InSequence` to enforce the four-step order.

---

**[DUPLICATE] — LOW**
The `IUIBackend` interface definition (all 21 methods with doc comments) appears verbatim
in both `testability-architecture.md` (test-facing authority) and `ui-manager.md`
(production-facing authority). Both files state they "must remain consistent". The duplicate
creates a maintenance obligation with no mechanical enforcement. The Phase 10b addition of
method 21 required updating both files simultaneously — any future method addition carries
the same dual-update burden.
_Proposed resolution_: Consider moving the canonical interface definition to a single file
(e.g., a dedicated `src/interfaces/IUIBackend.md`) with `testability-architecture.md` and
`ui-manager.md` cross-referencing it. If duplication must be maintained, add a CI lint step
that counts virtual methods in both spec blocks and fails if they diverge.

---

## Summary Table

| # | File | Category | Severity | Title |
|---|---|---|---|---|
| 1 | framework.md | GAP | MEDIUM | No self-tests specified for ManualClock |
| 2 | framework.md | GAP | MEDIUM | aitown_add_tests() does not validate label value |
| 3 | framework.md | PROBLEM | MEDIUM | One-label-per-target rule has no mechanical enforcement |
| 4 | framework.md | GAP | LOW | audio_tests Phase 10 binary DISCOVERY_TIMEOUT unaddressed |
| 5 | coverage.md | INCONSISTENCY | CRITICAL | Agent system prompt states 80% gate; spec states 95% from Phase 6 |
| 6 | coverage.md | GAP | HIGH | No src/terrain/ and src/ui/ SF preflight for Phase 5/6 gates |
| 7 | coverage.md | GAP | MEDIUM | Step ordering constraint not specified as a numbered rule |
| 8 | coverage.md | GAP | MEDIUM | src/interfaces/ coverage contribution not documented |
| 9 | coverage.md | GAP | LOW | --ignore-errors missing 'version' flag vs CLAUDE.md |
| 10 | coverage.md | DUPLICATE | LOW | lcov exclusion patterns tripled across coverage.md/CLAUDE.md/CI |
| 11 | testability-architecture.md | INCONSISTENCY | HIGH | IUIBackend.h location contradicts ui-manager.md |
| 12 | testability-architecture.md | GAP | HIGH | No MockSaveSystem contract specified |
| 13 | testability-architecture.md | GAP | HIGH | No SaveSystemTest fixture, mock contracts, or round-trip test spec |
| 14 | testability-architecture.md | GAP | HIGH | No EventReceiver testability seam or test spec |
| 15 | testability-architecture.md | GAP | MEDIUM | getTrafficDemandFactor test case not specified |
| 16 | testability-architecture.md | GAP | MEDIUM | MockCitySimulation cross-target usage not clarified |
| 17 | testability-architecture.md | GAP | MEDIUM | No IKeyBindingsStorage seam for KeyBindings file-I/O isolation |
| 18 | testability-architecture.md | GAP | MEDIUM | Zoning invariant has no generator strategy or fixture name |
| 19 | testability-architecture.md | PROBLEM | MEDIUM | Multi-mock TearDown order not fully specified for audio+UI fixtures |
| 20 | testability-architecture.md | GAP | LOW | InspectorPanel::populate() has no named test cases |
| 21 | testability-architecture.md | GAP | LOW | setLoadingTerrain() gate has no named test case |
| 22 | headless-ci-testing.md | GAP | MEDIUM | Minimum test count per label bucket not specified |
| 23 | headless-ci-testing.md | GAP | LOW | Phase 11b xvfb container spike has no rollback plan |
| 24 | property-based-tests.md | GAP | HIGH | Traffic invariant incomplete (no generator, no disconnected-graph case) |
| 25 | property-based-tests.md | GAP | HIGH | No population growth or density-unlock property tests |
| 26 | property-based-tests.md | GAP | MEDIUM | BudgetDeficitWarn dispatch not covered by any property test |
| 27 | property-based-tests.md | GAP | LOW | ManualClock overflow/saturation policy not documented |
| 28 | procedural-generation-seeds.md | MISSING | HIGH | File is 5 lines; no seed registry, pinning workflow, or domain policy |
| 29 | procedural-generation-seeds.md | GAP | MEDIUM | RapidCheck CI seed policy not documented |
| 30 | Cross-file | INCONSISTENCY | HIGH | IUIBackend.h location (src/ui/ vs src/interfaces/) — see items 11/5 |
| 31 | Cross-file | MISSING | HIGH | No integration test spec for full UIManager→CitySimulation→SaveSystem round-trip |
| 32 | Cross-file | MISSING | HIGH | SaveSystem↔UIManager wiring paths have no named test cases |
| 33 | Cross-file | MISSING | HIGH | Phase 11 save fields (milestone flags, speed multiplier, variant counters): no fixture spec |
| 34 | Cross-file | INCONSISTENCY | MEDIUM | Priority 2 CRITICAL dismiss routing to NotificationManager not tested |
| 35 | Cross-file | MISSING | MEDIUM | RMB drag suppression guard (isGameplayOrPaused) not test-specified |
| 36 | Cross-file | GAP | MEDIUM | Map size not parameterised in terrain/service-coverage tests |
| 37 | Cross-file | INCONSISTENCY | MEDIUM | MockAudioSystem include path not confirmed in ui_tests target_include_directories |
| 38 | Cross-file | MISSING | MEDIUM | Hover State Switching (IrrlichtUIBackend) has no test plan |
| 39 | Cross-file | GAP | LOW | Quit-safety modal three paths not specified as named test cases |
| 40 | Cross-file | GAP | LOW | transitionToMainMenu() call order not verified with InSequence test |
| 41 | Cross-file | DUPLICATE | LOW | IUIBackend 21-method definition duplicated in testability-architecture.md and ui-manager.md |


---

## 9. CI/CD Pipeline Review

**Reviewer:** Senior GitHub Pipeline Engineer

---

# CI/CD Review: AI Town Pipeline

**Reviewed files:**
- `/workspace/architecture/ci-cd/github-actions-workflow.md`
- `/workspace/architecture/ci-cd/dependency-management.md`
- `/workspace/architecture/ci-cd/caching.md`
- `/workspace/architecture/ci-cd/branch-protection.md`
- `/workspace/architecture/testing/headless-ci-testing.md`
- `/workspace/architecture/testing/coverage.md`
- `/workspace/.github/workflows/ci.yml`
- `/workspace/.github/workflows/_build-linux.yml`
- `/workspace/.github/workflows/_build-windows.yml`
- `/workspace/.github/workflows/_coverage-linux.yml`
- `/workspace/.github/workflows/_supply-chain-lint.yml`
- `/workspace/.github/workflows/_validate-assets.yml`
- `/workspace/.github/workflows/_markdown-lint.yml`
- `/workspace/.github/workflows/_package-windows.yml`
- `/workspace/.github/workflows/_package-linux-deb.yml`
- `/workspace/.github/workflows/docker-ci-image.yml`

---

## CRITICAL Issues

---

### ISSUE-1 [INCONSISTENCY] [CRITICAL] — `_build-linux.yml` and `_coverage-linux.yml` skip the vcpkg `actions/cache` step entirely

**Severity:** CRITICAL

**Description:**
`caching.md` (line 3–27) and `dependency-management.md` (line 26–31) both mandate an explicit `actions/cache` step for vcpkg packages in every build job, with a four-component cache key (`runner.os`, `COMPILER_VERSION`, `hashFiles('vcpkg.json')`, `vcpkg_commit_id`). The spec also explicitly states this step must come after the compiler-detect step.

`_build-linux.yml` and `_coverage-linux.yml` have no `actions/cache` step at all. Both configure CMake with `-DVCPKG_MANIFEST_INSTALL=OFF` and `-DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed`, delegating the entire vcpkg install to the pre-baked Docker image. There is also no `lukka/run-vcpkg` step in either file.

This means:
- The `actions/cache` architecture described in `caching.md` is completely absent from the two Linux jobs.
- The FetchContent cache key format specified in `caching.md` (line 28) is also never used.
- `caching.md` step 3 in the mandated order reads `COMPILER_VERSION` from step 2 — but on Linux the only purpose of the detect step in the actual workflow is to key the ccache action. The spec's four-component vcpkg key is irrelevant to the actual workflow, yet the spec documents it as required.

**Root cause:** The spec was written assuming `lukka/run-vcpkg` + `actions/cache` for Linux, but the implementation switched to a containerized GHCR image with pre-installed vcpkg packages (`/opt/vcpkg_installed`). The spec was never updated to reflect this architectural shift.

**Proposed resolution:** Update `caching.md` and `dependency-management.md` to document the containerized approach: when `container: image: ghcr.io/...` is in use with `VCPKG_MANIFEST_INSTALL=OFF`, no `actions/cache` vcpkg step and no `lukka/run-vcpkg` step are used in `build-linux` / `coverage-linux`. The cache strategy shifts to the Docker layer cache in `docker-ci-image.yml` (via `cache-from/cache-to: type=gha`). Add a note that `caching.md`'s four-component vcpkg key applies to the Windows job only. Consolidate this in a "Platform-specific caching summary" table.

---

### ISSUE-2 [INCONSISTENCY] [CRITICAL] — `dependency-management.md` mandates `lukka/run-vcpkg` for Linux; actual workflow uses none

**Severity:** CRITICAL

**Description:**
`dependency-management.md` line 26 states: "CI uses `lukka/run-vcpkg@5e0cab206a5ea620130caf672fce3e4a6b5666a1` with a pinned vcpkg commit hash stored as `env.VCPKG_COMMIT_ID`", and this is presented as applying to all CI jobs. The spec further states `VCPKG_COMMIT_ID` must be declared at the workflow level "so it is available to all jobs and steps."

`_build-linux.yml` has zero references to `lukka/run-vcpkg`. The vcpkg commit ID is passed as an input (`vcpkg_commit_id`) but is only used in the `ci.yml` orchestration layer; `_build-linux.yml` and `_coverage-linux.yml` never consume it. The baseline consistency check in `_validate-assets.yml` does consume it correctly, but `build-linux` and `coverage-linux` bypass the entire install step via the pre-baked image.

This creates a dangerous gap: if the `docker-ci-image.yml` image is rebuilt at a different vcpkg baseline than `VCPKG_COMMIT_ID`, the installed packages in the image will silently mismatch the validated baseline. The `validate-assets` job's baseline consistency check verifies `vcpkg.json`'s baseline matches `VCPKG_COMMIT_ID`, but never verifies that the image was built with the same commit.

**Proposed resolution:** The spec must document that Linux builds derive their vcpkg installation from the Docker image rather than a live `lukka/run-vcpkg` invocation, and explain the atomicity contract: image rebuild (`docker-ci-image.yml`) must be triggered and completed before the updated baseline can be used in `build-linux`. Add a CI step or check (in `docker-ci-image.yml` or as a new `validate-baseline-image` step) that verifies the image's embedded vcpkg commit matches the workflow-level `VCPKG_COMMIT_ID`.

---

### ISSUE-3 [INCONSISTENCY] [CRITICAL] — `coverage.md` specifies `--ignore-errors mismatch,inconsistent,version` but actual workflow omits `version`

**Severity:** CRITICAL

**Description:**
`coverage.md` lines 12–19 show the canonical `lcov --capture` invocation with `--ignore-errors mismatch,inconsistent,version`. The `version` error suppresses "GCC/gcov version-string mismatch when build and capture gcov versions differ." The `CLAUDE.md` notes section also specifies `mismatch,inconsistent,version` (comma-separated single flag).

`_coverage-linux.yml` line 201 uses `--ignore-errors mismatch,inconsistent` — omitting `version`. Since the Dockerfile installs gcc-13 but the comment in `_coverage-linux.yml` itself (lines 186–188) documents that "gcov-14 cannot read .gcda files produced by GCC 13," this is a concern: although the `--gcov-tool gcov-13` flag routes to the correct gcov binary, a version-string mismatch warning from an lcov internal consistency check can still cause a non-zero exit when `version` is absent. Under lcov 2.x this may not be a hard failure in the specific scenario with `--gcov-tool gcov-13`, but the spec and the implementation disagree.

**Proposed resolution:** Add `version` to the `--ignore-errors` flag in `_coverage-linux.yml`'s `lcov --capture` invocation to match `coverage.md` exactly. Alternatively, if the implementation team has confirmed `version` is not needed with `--gcov-tool gcov-13`, update `coverage.md` and `CLAUDE.md` to remove `version` from the documented set.

---

### ISSUE-4 [GAP] [CRITICAL] — `_build-linux.yml` missing `actions/cache` step for vcpkg, but spec step ordering comment is orphaned

**Severity:** CRITICAL

**Description:**
`_build-linux.yml` step numbering comments reference steps that do not exist in the file. The file comments say "Step 1: checkout", "Step 4: Detect GCC version", "Step 8: Set up ccache", "Step 9: Configure CMake". Steps 2, 3, 5, 6, 7 are not present and are not explained. The gaps correspond to the removed `actions/cache` (vcpkg), `lukka/run-vcpkg`, and possibly system-dependency install steps. The same issue is present in `_coverage-linux.yml` which uses "Step 4", "Step 8", "Step 9" with identical gaps.

This creates maintenance confusion: a developer following the spec's step ordering (which explicitly numbers steps 1–17+ in `github-actions-workflow.md` §coverage-linux) cannot reconcile the spec's step list with the file's step comments.

**Proposed resolution:** Renumber the step comments in `_build-linux.yml` and `_coverage-linux.yml` to be sequential (1, 2, 3 ...) reflecting only steps that actually exist. Alternatively, retain the spec numbering but add an explanatory comment block at the top of each file stating which spec steps are handled by the Docker image and therefore absent.

---

### ISSUE-5 [PROBLEM] [CRITICAL] — `_coverage-linux.yml` `Preflight src/simulation/ coverage entries` step exits 0 on failure (warning-only) but spec mandates hard-fail at Phase 6

**Severity:** CRITICAL

**Description:**
`coverage.md` §Phase 6 (lines 183–192) specifies that the `src/simulation/` SF preflight step must `exit 1` if no `src/simulation/` SF entries are present in `coverage_filtered.info`. The step in `github-actions-workflow.md` (step 16b) also states it is a "Phase 6 deliverable" that runs before the 95% gate.

`_coverage-linux.yml` lines 248–256 implement this step as warning-only: it prints a `WARNING:` message and does NOT exit non-zero when simulation entries are absent. The comment says "change this step to exit 1 on missing simulation entries" — indicating Phase 6 work was completed but this step was never hardened. The project is well past Phase 6 (the Phase 11 per-file 85% floor step at line 258 is already implemented as hard-fail).

This means a broken `simulation_tests` registration would cause the entire 95% gate to silently compute coverage across only `src/terrain/` and `src/ui/`, potentially showing green while simulation code is 0% covered.

**Proposed resolution:** Remove the warning-only logic and replace with the hard-fail form from `coverage.md` §Phase 6:
```bash
if ! grep -q "SF:.*src/simulation/" coverage_filtered.info; then
  echo "PREFLIGHT FAIL: No src/simulation/ SF entries in coverage_filtered.info."
  exit 1
fi
```

---

## HIGH Issues

---

### ISSUE-6 [INCONSISTENCY] [HIGH] — `dependency-management.md` specifies `glew.lib` library name but actual workflow verifies `glew32.lib`

**Severity:** HIGH

**Description:**
`dependency-management.md` §Step B "Verify GLEW vcpkg install" (lines 230–239) shows the canonical YAML checking for `build/vcpkg_installed/x64-windows/lib/glew.lib`. However, `_build-windows.yml` step 10 (line 105) correctly checks `glew32.lib` and includes a comment: "On Windows, vcpkg GLEW port installs glew32.lib (not glew.lib) per portfile libname override."

The spec document is wrong: it specifies `glew.lib` in the canonical YAML. Any developer implementing from the spec will write a check that never fails (glew.lib does not exist) rather than actually verifying the real file `glew32.lib`.

**Proposed resolution:** Update `dependency-management.md` Step B canonical YAML to reference `glew32.lib` not `glew.lib`, and add a note explaining the Windows portfile libname override.

---

### ISSUE-7 [INCONSISTENCY] [HIGH] — `caching.md` FetchContent cache section references `.fetchcontent_cache` which is unused in actual Linux jobs

**Severity:** HIGH

**Description:**
`caching.md` lines 28–29 mandate caching `.fetchcontent_cache` with a key including `COMPILER_VERSION` and `hashFiles('CMakeLists.txt', 'cmake/**')`. This key format is documented as "MUST be identical between dependency-management.md and this file."

Neither `_build-linux.yml` nor `_coverage-linux.yml` have a FetchContent cache step. Because vcpkg manages all test dependencies (gtest, rapidcheck), FetchContent is not used in the Linux container build path. The `.fetchcontent_cache` directory never exists.

`dependency-management.md` line 172 explicitly states: "Why vcpkg (not FetchContent): Using vcpkg for all C++ dependencies — including test dependencies — eliminates git clone overhead at CMake configure time." This directly contradicts the need for the FetchContent caching section in `caching.md`.

**Proposed resolution:** Mark the FetchContent caching section in `caching.md` as "applicable only if FetchContent is used" with a note that the current implementation uses vcpkg for all dependencies. Alternatively, remove it entirely since the current and planned architecture uses vcpkg exclusively. The "MUST be identical" cross-reference to `dependency-management.md` should also be removed or qualified.

---

### ISSUE-8 [INCONSISTENCY] [HIGH] — `dependency-management.md` lists `libxxf86vm-dev` as a required system package but it is absent from `github-actions-workflow.md` and both actual workflows

**Severity:** HIGH

**Description:**
`headless-ci-testing.md` line 5–6 lists `libxxf86vm-dev` as a required system package: "provides the `xf86vmode` library required by Irrlicht's X11 display mode enumeration." The note explicitly says without it, Irrlicht fails to build with `cannot find -lXxf86vm`.

`dependency-management.md` lines 53–58 list required apt packages for `build-linux` and `coverage-linux` but do NOT include `libxxf86vm-dev`. `github-actions-workflow.md` also does not include it in the system dependency install step.

`_build-linux.yml` and `_coverage-linux.yml` have no apt-get install step at all (dependencies are in the Docker image). However, the spec documents an explicit `apt-get install` step with a list that is missing `libxxf86vm-dev`.

**Proposed resolution:** Add `libxxf86vm-dev` to the apt package list in `dependency-management.md`. Verify the Docker image includes it (check `docker/ci-linux/Dockerfile`). Also align `headless-ci-testing.md`'s package list with `dependency-management.md` since they describe the same requirement.

---

### ISSUE-9 [GAP] [HIGH] — No spec coverage for `bump-version` and `release` jobs

**Severity:** HIGH

**Description:**
`github-actions-workflow.md` contains a `## 'bump-version' Job` section (referenced at line 969+) and a `## 'release' Job` section, and both jobs are implemented in `ci.yml` (lines 163–253). However, neither `branch-protection.md` nor `caching.md` mention these jobs, and neither section describes:

- What permissions `bump-version` needs to push to `main` and the implications for branch protection rules that require PRs. The job uses `contents: write` and force-pushes a tag — bypassing branch protection for the commit.
- What `release` retention policy applies for GitHub release assets (as opposed to artifact retention).
- Whether `bump-version` should be gated on `all-checks-pass` (currently it is not in `all-checks-pass.needs`).
- The race condition: `bump-version` runs on every push to `main` in parallel with `release`. If `bump-version` fails, the tag is not pushed, but `release` may still attempt to create a release with the wrong version.

The `bump-version` job's `git push --follow-tags` will fail on protected branches because it is a direct push (not a PR). The job sets `contents: write` but branch protection rules require PRs with approvals. This is a latent failure mode.

**Proposed resolution:** Add a `## Auto-versioning` section to `github-actions-workflow.md` explaining the `bump-version` / `release` lifecycle, including: (1) why `bump-version` must use a branch protection bypass token or `permissions: contents: write` with an allowed bypass actor; (2) the sequencing dependency between `bump-version` and `release`; (3) an explicit note that `release` is outside the `all-checks-pass` gate by design; (4) retention policy for GitHub release assets (separate from artifact retention).

---

### ISSUE-10 [PROBLEM] [HIGH] — `_package-windows.yml` re-runs vcpkg install without MSVC environment setup for the cache step

**Severity:** HIGH

**Description:**
`_package-windows.yml` runs `ilammy/msvc-dev-cmd` and `lukka/run-vcpkg` but does NOT include the MSVC version detect step (`vswhere.exe`) or an `actions/cache` step for vcpkg packages before running vcpkg. This means:
1. On a cold cache the package job rebuilds all vcpkg dependencies from scratch — adding 30–40 minutes to every `main`/`develop` push.
2. The `caching.md` four-component key requirement is not implemented here.

Additionally, `_package-windows.yml` does not add `build\vcpkg_installed\x64-windows\bin` to PATH before running CPack (only appends it via `Out-File -FilePath $env:GITHUB_PATH` which takes effect for subsequent steps — this is correct, but no step actually uses it in the package job since tests are disabled). This is not a bug but is undocumented inconsistency.

**Proposed resolution:** Add a vcpkg cache step to `_package-windows.yml` mirroring `_build-windows.yml` steps 3–4, or add a note to `github-actions-workflow.md` explicitly acknowledging that packaging jobs accept the cold-cache rebuild cost and explaining why (packaging is release-only and infrequent).

---

### ISSUE-11 [GAP] [HIGH] — `_package-linux-deb.yml` installs vcpkg from source on every run with no caching; containers are not digest-pinned

**Severity:** HIGH

**Description:**
`_package-linux-deb.yml` runs on raw distro container images (`debian:bookworm`, `ubuntu:22.04`, etc.) and clones the entire vcpkg repo, builds the vcpkg tool from source, and runs `vcpkg install` on every single run. This is:
1. Extremely slow (15–30+ minutes per matrix leg), and
2. Uses floating tags (`debian:bookworm`, `ubuntu:22.04`) with no digest pin, which the supply-chain lint (`_supply-chain-lint.yml` lines 49–55) would flag if those lines were `container: image:` entries in a CI workflow. They are matrix-injected values so the lint regex (`^\s+image:\s+`) would match the final resolved `container: ${{ matrix.container }}` line.

**Check:** `_supply-chain-lint.yml` checks `container: image:` lines but does NOT check `container: ${{ matrix.container }}` because the value is a context variable, not a literal. The lint regex at line 50 matches literal image lines. This means the four matrix containers bypass the digest-pin lint entirely.

This creates a supply-chain gap: a tag like `debian:bookworm` can be updated silently to point to a different image digest, and no CI check would catch it.

**Proposed resolution:**
1. Add a note to `dependency-management.md` documenting that packaging containers use floating tags by design (cross-distro compatibility testing requires tracking moving distro images), but acknowledge the supply-chain trade-off.
2. Add a caching strategy for the vcpkg build in `_package-linux-deb.yml` (cache `/opt/vcpkg/installed` keyed on `inputs.vcpkg_commit_id`).
3. Alternatively, build distro-specific CI images similar to the main `ghcr.io/m0wa/aitown-ci-linux` image.

---

### ISSUE-12 [INCONSISTENCY] [HIGH] — `github-actions-workflow.md` documents `markdown-lint` step with unpinned `npm install -g markdownlint-cli` but actual workflow pins `@0.47.0`

**Severity:** HIGH

**Description:**
`github-actions-workflow.md` §markdown-lint job (line 583) shows: `run: npm install -g markdownlint-cli` (no version pin). The spec note at line 594 says "To pin to a specific version use `npm install -g markdownlint-cli@0.47.0`" as an option, not a requirement.

`_markdown-lint.yml` line 26 correctly pins `npm install -g markdownlint-cli@0.47.0`.

The spec presents pinning as optional ("to pin... use...") while the actual implementation correctly enforces it. A future developer reading the spec may implement an unpinned version, causing non-reproducible lint results.

**Proposed resolution:** Update `github-actions-workflow.md` §markdown-lint to change the job definition snippet to use `markdownlint-cli@0.47.0` and reword the note from "to pin... use" to "MUST pin to a specific version; current pin: `@0.47.0`."

---

### ISSUE-13 [INCONSISTENCY] [HIGH] — `coverage.md` Phase 4 src/ui/ gate uses `lcov --list` parsing but `_coverage-linux.yml` uses direct `.info` file parsing

**Severity:** HIGH

**Description:**
`coverage.md` §Phase 4 src/ui/ Coverage Baseline (lines 244–276) documents a gate using `lcov --list coverage_filtered.info | grep -E "src/ui/" | awk -F'|' '{print $NF+0}'` to extract coverage percentages from column-delimited `--list` output. The same section includes a preflight check for the `|` column delimiter.

`_coverage-linux.yml` step 20 (lines 309–334) uses a completely different approach: it parses `coverage_filtered.info` directly via SF/LH/LF records in a single awk pass. This approach is described in `coverage.md` as the Phase 6 method and labeled "version-agnostic; does NOT use `lcov --list` output."

Both approaches produce the same result when correctly implemented, but the spec and implementation disagree about which method to use for the Phase 4 gate. Future maintainers reading the spec will implement the `lcov --list` parsing approach and get a different implementation from what is deployed.

**Proposed resolution:** Update `coverage.md` §Phase 4 to replace the `lcov --list` parsing approach with the direct `.info` parsing approach (awk SF/LH/LF), since that is what is actually deployed and is acknowledged as superior (version-agnostic). Remove the lcov 2.x `|` delimiter preflight check from the spec since it is no longer needed.

---

### ISSUE-14 [PROBLEM] [HIGH] — `_build-linux.yml` step comments reference out-of-order step numbers that are never resolved

**Severity:** HIGH

**Description:**
`_build-linux.yml` step comments use non-sequential numbering (Steps 1, 4, 8, 9, 10, 10b, 11, 12, 12b, 12c, 13, 14, 15, 16, 17, 18, 19). Steps 2, 3, 5, 6, 7 are absent with no explanation. The same pattern appears in `_coverage-linux.yml` (Steps 1, 4, 8, 9, 10, 11, 12, 12b, 12c, 13–21).

This numbering originates from `github-actions-workflow.md` §coverage-linux ordered step list (lines 317–336), where some steps correspond to operations now embedded in the Docker image. However, the spec's step list (1–17) in that section also skips the system-dependency install step that was removed from the container-based workflow.

The skipped step numbers cause confusion when cross-referencing spec to implementation. Anyone adding a new step using the spec's numbering will produce a step number collision or further gaps.

**Proposed resolution:** Either (a) renumber steps in both workflow files to be fully sequential, or (b) add a comment at the top of each workflow file listing which spec steps are handled by the Docker image ("Steps 2, 3, 5, 6, 7 are handled by the GHCR container image and are not present as explicit steps"). The spec's ordered step list in `github-actions-workflow.md` §coverage-linux should be updated to mark container-handled steps.

---

## MEDIUM Issues

---

### ISSUE-15 [DUPLICATE] [MEDIUM] — Supply-chain SHA lint logic is duplicated between `_supply-chain-lint.yml` and `docker-ci-image.yml`

**Severity:** MEDIUM

**Description:**
The SHA lint logic (checking for angle-bracket placeholders and short SHAs) appears in three places:
1. `_supply-chain-lint.yml` lines 27–47 (canonical, checks all workflow files)
2. `docker-ci-image.yml` lines 83–101 (self-check, scoped to `docker-ci-image.yml` only)
3. `github-actions-workflow.md` lines 33–56 (documented as a step inside `build-linux`)

The spec (`github-actions-workflow.md` line 27–58) describes this as "the first named step in `build-linux`", but the actual implementation delegates it to `_supply-chain-lint.yml` as a separate reusable workflow. `build-linux` no longer contains the lint step inline. The spec is outdated.

**Proposed resolution:** Update `github-actions-workflow.md` to describe the supply-chain lint as a dedicated reusable workflow job (`_supply-chain-lint.yml`) rather than an inline step in `build-linux`. Note that `docker-ci-image.yml` carries its own self-check for defense-in-depth. Keep one authoritative source for the lint logic description.

---

### ISSUE-16 [GAP] [MEDIUM] — `branch-protection.md` does not address `bump-version` direct-push exception

**Severity:** MEDIUM

**Description:**
`branch-protection.md` §5 states: "Do not allow bypass by administrators" and "all contributors including admins must merge through PRs." The `bump-version` job in `ci.yml` (lines 176–188) directly pushes a commit and tag to `main` via `git push --follow-tags` using the `GITHUB_TOKEN` with `contents: write`. This is a bot-originated direct push that bypasses the PR requirement.

GitHub branch protection rules can be configured to allow `github-actions[bot]` as a bypass actor even when administrator bypass is disabled. The spec does not document this exception, leaving implementers with no guidance on how to make the `bump-version` job work without disabling the branch protection rule that the spec mandates.

**Proposed resolution:** Add a sub-section to `branch-protection.md` titled "Bot push bypass for auto-versioning" that:
1. Explains that `github-actions[bot]` must be added to the "Allow specified actors to bypass required pull requests" list for both `main` and `develop`.
2. Notes this is a narrowly scoped exception for the `bump-version` bot commit only.
3. Clarifies that human contributors and administrator accounts remain subject to the full PR requirement.

---

### ISSUE-17 [GAP] [MEDIUM] — No spec for `actions/download-artifact` SHA pin in `release` job

**Severity:** MEDIUM

**Description:**
`ci.yml` `release` job (lines 217–244) uses `actions/download-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08` — the same SHA as `actions/upload-artifact`. This is correct by coincidence (both are v4.6.0), but `caching.md` only lists `actions/upload-artifact` in its SHA registry (line 35). `actions/download-artifact` is not listed in `caching.md` even though it requires a SHA pin equally.

`_supply-chain-lint.yml` would catch a missing pin at CI time, but the spec has a gap: maintainers updating the SHA registry in `caching.md` may not update `actions/download-artifact` since it is not listed.

**Proposed resolution:** Add `actions/download-artifact@65c4c4a1ddee5b72f698fdd19549f0f0fb45cf08 # v4.6.0` to the SHA registry in `caching.md`. Note that upload and download share the same v4.6.0 SHA and must be updated together.

---

### ISSUE-18 [GAP] [MEDIUM] — `docker-ci-image.yml` digest update is manual; no automated reminder or enforcement

**Severity:** MEDIUM

**Description:**
After `docker-ci-image.yml` builds and pushes a new image, step 9 (lines 140–159) prints instructions to manually update `ci.yml` and `.devcontainer/Dockerfile` with the new digest. This is a purely manual step. There is no CI check that verifies the digest in `_build-linux.yml` / `_coverage-linux.yml` matches the most recently pushed image digest.

The five-item vcpkg baseline atomicity contract (`CLAUDE.md` §vcpkg Baseline Atomicity) documents this requirement, but nothing in the CI pipeline enforces that an image rebuild is followed by a digest pin update in `ci.yml`. A developer could rebuild the image, forget to update the digest, and CI would keep using the old image silently.

**Proposed resolution:** Add a step to `docker-ci-image.yml` that uses `gh` CLI to create a draft PR (or open an issue) with the exact diff needed for `ci.yml` and `.devcontainer/Dockerfile`. Alternatively, document a `validate-image-digest` step in `_build-linux.yml` or `_supply-chain-lint.yml` that queries the GHCR image manifest and verifies the pinned digest matches the tag used in `container: image:`.

---

### ISSUE-19 [INCONSISTENCY] [MEDIUM] — `github-actions-workflow.md` `lcov --capture` uses `${{ github.workspace }}` for `--base-directory` but `_coverage-linux.yml` uses `.` (dot)

**Severity:** MEDIUM

**Description:**
`github-actions-workflow.md` lines 423–431 explain at length why `${{ github.workspace }}` is preferred over `.` for the `--base-directory` argument: "it works correctly on self-hosted runners where the runner's working directory convention may differ from `$GITHUB_WORKSPACE`."

`_coverage-linux.yml` line 199 uses `--base-directory .` (dot), contradicting the spec's preferred form.

This is not a bug in the standard GitHub-hosted runner case because the shell CWD is always `$GITHUB_WORKSPACE` at step start. However, the spec explicitly documents the reason for preferring the absolute path form, and the implementation silently ignores it.

**Proposed resolution:** Update `_coverage-linux.yml` line 199 to use `--base-directory "${GITHUB_WORKSPACE}"` (environment variable form, safe inside a container where `${{ github.workspace }}` may resolve to the host path while the shell CWD is the container path `/__w/...`). Note: inside the container, `$GITHUB_WORKSPACE` resolves to the container path `/__w/ai-town/ai-town`, making `"${GITHUB_WORKSPACE}"` more correct than `${{ github.workspace }}` which resolves to the host path at YAML evaluation time. Update the spec to acknowledge this container-path distinction.

---

### ISSUE-20 [MISSING] [MEDIUM] — No spec for Dependabot or automated dependency update automation

**Severity:** MEDIUM

**Description:**
No spec file in `architecture/ci-cd/` addresses automated dependency update tooling. The pipeline currently has:
- Pinned action SHAs that go stale silently (no automated PR when a new version is released)
- A pinned vcpkg baseline that must be manually updated
- A pinned `markdownlint-cli@0.47.0` that goes stale silently
- Pinned `mutagen` without version pin (contradicting the reproducibility goals stated in the spec)

`dependency-management.md` says "Do NOT pin `mutagen` to a specific version" (line 658) which is inconsistent with the reproducibility principles applied to all other pinned dependencies.

**Proposed resolution:** Add a `## Automated Dependency Updates` section to `dependency-management.md` addressing: (1) whether Dependabot is configured for GitHub Actions; (2) the process for updating pinned action SHAs; (3) an explicit policy on Python pip dependency pinning (either pin all pip packages with a `requirements-validate-assets.txt` or acknowledge the non-reproducibility trade-off with a rationale).

---

### ISSUE-21 [GAP] [MEDIUM] — Windows `_package-windows.yml` does not run tests before packaging; spec has no test gate on packaging

**Severity:** MEDIUM

**Description:**
`_package-windows.yml` configures CMake with `-DBUILD_TESTING=OFF` and never runs CTest. The CI wiring in `ci.yml` does gate `package-windows` on `needs: [build-windows]` (line 141), meaning the quality gate is enforced. However:

1. `github-actions-workflow.md` has no spec section for the packaging jobs at all (the spec ends at the `release` job with minimal description).
2. The `needs: [build-windows]` dependency means a successful `build-windows` (which does run tests) gates the packaging job. But `package-windows` rebuilds from source independently — it does not reuse `build-windows` artifacts. A fluke in the second build that produces a different binary than the tested one is theoretically possible.

**Proposed resolution:** Add a `## Packaging Jobs` section to `github-actions-workflow.md` explaining: (1) why packaging rebuilds from source rather than reusing `build-windows` artifacts (artifact transfer size, cache alignment); (2) how the `needs: [build-windows]` dependency provides the quality gate; (3) why `BUILD_TESTING=OFF` is correct for packaging builds; (4) what retention policy applies to packaging artifacts.

---

### ISSUE-22 [INCONSISTENCY] [MEDIUM] — `caching.md` mentions `softprops/action-gh-release` with a placeholder SHA but `ci.yml` uses a real SHA that is undocumented in the spec

**Severity:** MEDIUM

**Description:**
`caching.md` line 37 lists: `softprops/action-gh-release@<40-CHAR-SHA>  # resolve at implementation time`. The implementation in `ci.yml` line 247 uses `softprops/action-gh-release@9d7c94cfd0a1f3ed45544c887983e9fa900f0564  # v2.1.0`.

The `<40-CHAR-SHA>` placeholder in `caching.md` was never updated to the real resolved SHA. The supply-chain lint in `_supply-chain-lint.yml` does NOT check spec markdown files — only `.github/workflows/*.yml` files. So the placeholder in the spec goes undetected.

**Proposed resolution:** Update `caching.md` line 37 to replace the placeholder with `softprops/action-gh-release@9d7c94cfd0a1f3ed45544c887983e9fa900f0564 # v2.1.0`.

---

### ISSUE-23 [PROBLEM] [MEDIUM] — `_coverage-linux.yml` lcov `--ignore-errors` on `--remove` step uses `unused,inconsistent` but spec specifies only `unused`

**Severity:** MEDIUM

**Description:**
`coverage.md` lines 30–32 specify `--ignore-errors unused` for the `lcov --remove` step (not `unused,inconsistent`). `_coverage-linux.yml` line 216 uses `--ignore-errors unused,inconsistent` on the `--remove` step. The additional `inconsistent` suppressor is not documented in `coverage.md` and its rationale is not captured anywhere in the spec.

This is a low-risk discrepancy (suppressing additional warnings is not harmful) but it means the spec does not explain why `inconsistent` is needed on the `--remove` step in addition to the `--capture` step. If someone follows the spec and omits `inconsistent` from `--remove`, they may encounter unexpected non-zero exits.

**Proposed resolution:** Add `inconsistent` to the `--ignore-errors` on the `lcov --remove` step in `coverage.md` and add a comment explaining: "lcov 2.x may also emit inconsistent data errors during --remove when processing coverage data with lambda inlining; include inconsistent here for the same reason as --capture."

---

### ISSUE-24 [GAP] [MEDIUM] — `branch-protection.md` does not address `develop` branch protection for `package-linux-deb` and `package-windows` jobs

**Severity:** MEDIUM

**Description:**
`ci.yml` lines 141–155 trigger `package-windows` and `package-linux-deb` on both `main` and `develop` pushes (`github.ref == 'refs/heads/main' || github.ref == 'refs/heads/develop'`). `branch-protection.md` requires protected `develop` to pass `all-checks-pass`, which is correct. But there is no discussion of the packaging jobs' relationship to branch protection: these jobs are not in `all-checks-pass.needs`, so a failing packaging job does not block `develop` merges.

This means a broken CPack configuration or NSIS packaging script can merge to `develop` silently through PRs (PRs only run CI, not packaging — packaging only triggers on push). The first signal of a broken installer is when the package job runs post-merge.

**Proposed resolution:** Add a note to `branch-protection.md` explaining that packaging jobs run post-merge and are intentionally outside the PR gate, along with the rationale (packaging is release infrastructure, not build quality). Document the remediation path when a packaging job fails on `develop` (immediate follow-up PR to fix, not a rollback).

---

## LOW Issues

---

### ISSUE-25 [DUPLICATE] [LOW] — vcpkg baseline atomicity requirements are documented in three separate places with slight wording differences

**Severity:** LOW

**Description:**
The five-item vcpkg baseline atomicity commit requirement is documented in:
1. `dependency-management.md` lines 3–4 (brief mention)
2. `CLAUDE.md` §Build & Toolchain "vcpkg Baseline Atomicity" (five numbered items)
3. `MEMORY.md` §vcpkg Baseline Staleness (separate memory note with different framing)

The three descriptions have slightly different item counts and wording. `CLAUDE.md` lists five items. `dependency-management.md` only mentions `builtin-baseline` and `VCPKG_COMMIT_ID` without the full five-item list. A developer reading only `dependency-management.md` would miss items 3–5 (Dockerfile, devcontainer, digest pin).

**Proposed resolution:** Add the full five-item atomicity list to `dependency-management.md` as the canonical location, and change `CLAUDE.md` to reference `dependency-management.md` for the authoritative list rather than duplicating it.

---

### ISSUE-26 [MISSING] [LOW] — No spec for what happens when `docker-ci-image.yml` monthly schedule triggers but the image digest is already current

**Severity:** LOW

**Description:**
`docker-ci-image.yml` has a monthly schedule trigger (`cron: '0 2 1 * *'`). On a scheduled run, if no files changed, Docker BuildKit will produce the same image layers (assuming the base OS packages are unchanged). But it will push a new image tag with the same digest, and the digest in `ci.yml` / `.devcontainer/Dockerfile` may not need updating.

There is no documented policy on:
- Whether the monthly rebuild always produces a new digest (OS security updates usually do)
- Whether the digest update PR should be created automatically or manually reviewed
- What to do if the scheduled rebuild fails (no notification mechanism is documented)

**Proposed resolution:** Add a brief spec note to `github-actions-workflow.md` in the `docker-ci-image.yml` section explaining: (1) the monthly rebuild purpose (pick up OS security patches from the base image); (2) that a digest change from a scheduled rebuild requires the same atomicity commit as a vcpkg baseline update (items 4 and 5 of the five-item contract); (3) recommended alerting if the scheduled rebuild fails (GitHub Actions notification to repo admins).

---

### ISSUE-27 [GAP] [LOW] — `caching.md` does not specify artifact retention for `_package-linux-deb.yml` or `_package-windows.yml` packaging artifacts

**Severity:** LOW

**Description:**
`caching.md` (and `github-actions-workflow.md` §Artifact retention) specify retention for test XML (14 days), coverage HTML (14 days), and Windows binary (30 days). The packaging artifacts in `_package-windows.yml` (line 58: `retention-days: 30`) and `_package-linux-deb.yml` (line 108: `retention-days: 30`) both use 30 days — matching the Windows binary.

This is consistent, but the 30-day retention for packaging artifacts is not explicitly stated in the spec. A future change to artifact retention policy might update the spec without updating the packaging workflows.

**Proposed resolution:** Add packaging artifact retention (30 days, same as release binaries) to the retention policy table in `github-actions-workflow.md`.

---

### ISSUE-28 [PROBLEM] [LOW] — `_package-windows.yml` uses `Out-File -FilePath $env:GITHUB_PATH` PATH append idiom but `_build-windows.yml` uses `>> $env:GITHUB_PATH`

**Severity:** LOW

**Description:**
`_build-windows.yml` line 125 appends to `GITHUB_PATH` with `"..." >> $env:GITHUB_PATH`. `_package-windows.yml` line 47 uses `echo "$vcpkgBin" | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append`. Both accomplish the same result but inconsistently.

Neither `caching.md` nor `dependency-management.md` document the canonical PS 5.1 GITHUB_PATH append idiom. `caching.md` only documents the `$GITHUB_ENV` append idiom for compiler version detection.

**Proposed resolution:** Add the canonical PS 5.1 `GITHUB_PATH` append idiom to `caching.md` alongside the `GITHUB_ENV` idiom. Standardize both workflows to use `"$vcpkgBin" >> $env:GITHUB_PATH` form (the simpler form already used in `_build-windows.yml`).

---

### ISSUE-29 [MISSING] [LOW] — No spec for `ilammy/msvc-dev-cmd` SHA in the SHA registry

**Severity:** LOW

**Description:**
`caching.md` lines 30–37 document the SHA registry for all pinned actions, but `ilammy/msvc-dev-cmd@a102174a2b586eec2ea151a69e6fd14404a8ce7c` (v1.13.0) is not included in the `caching.md` SHA list. It appears only in `github-actions-workflow.md` §Windows job (line 180) and in the actual workflow files.

If the SHA registry in `caching.md` is intended to be the authoritative list for supply-chain management, this omission is a gap.

**Proposed resolution:** Add `ilammy/msvc-dev-cmd@a102174a2b586eec2ea151a69e6fd14404a8ce7c # v1.13.0` and `docker/login-action@c94ce9fb468520275223c153574b00df6fe4bcc9 # v3`, `docker/setup-buildx-action@8d2750c68a42422c14e847fe6c8ac0403b4cbd6f # v3`, and `docker/build-push-action@10e90e3645eae34f1e60eeb005ba3a3d33f178e8 # v6` to the SHA registry in `caching.md`.

---

### ISSUE-30 [GAP] [LOW] — `headless-ci-testing.md` §Containerised CI section lacks spec for ccache key behavior inside containers

**Severity:** LOW

**Description:**
`headless-ci-testing.md` lines 14–29 describe the Phase 11b containerized CI transition but do not mention how ccache functions inside the container. `caching.md` documents ccache for native runners but does not address the container case: when the job runs inside a container, `hendrikmuhs/ccache-action` uses the GitHub Actions cache API which works via the `ACTIONS_CACHE_URL` environment variable injected into the container by the runner. This works correctly but is undocumented in the spec.

**Proposed resolution:** Add a note to `caching.md` §compiler output caching confirming that `hendrikmuhs/ccache-action` is compatible with container jobs on GitHub-hosted runners (the action uses the Actions cache service API, not the local filesystem, so it functions identically inside and outside containers).

---

## Summary Table

| Issue | Category | Severity | Spec File(s) | Workflow File(s) |
|-------|----------|----------|--------------|-----------------|
| ISSUE-1 | INCONSISTENCY | CRITICAL | `caching.md`, `dependency-management.md` | `_build-linux.yml`, `_coverage-linux.yml` |
| ISSUE-2 | INCONSISTENCY | CRITICAL | `dependency-management.md` | `_build-linux.yml`, `_coverage-linux.yml` |
| ISSUE-3 | INCONSISTENCY | CRITICAL | `coverage.md`, `CLAUDE.md` | `_coverage-linux.yml` |
| ISSUE-4 | GAP | CRITICAL | `github-actions-workflow.md` | `_build-linux.yml`, `_coverage-linux.yml` |
| ISSUE-5 | PROBLEM | CRITICAL | `coverage.md` | `_coverage-linux.yml` |
| ISSUE-6 | INCONSISTENCY | HIGH | `dependency-management.md` | `_build-windows.yml` |
| ISSUE-7 | INCONSISTENCY | HIGH | `caching.md`, `dependency-management.md` | `_build-linux.yml`, `_coverage-linux.yml` |
| ISSUE-8 | INCONSISTENCY | HIGH | `dependency-management.md`, `headless-ci-testing.md` | `_build-linux.yml` |
| ISSUE-9 | GAP | HIGH | `github-actions-workflow.md`, `branch-protection.md` | `ci.yml` |
| ISSUE-10 | PROBLEM | HIGH | `caching.md` | `_package-windows.yml` |
| ISSUE-11 | GAP | HIGH | `dependency-management.md` | `_package-linux-deb.yml` |
| ISSUE-12 | INCONSISTENCY | HIGH | `github-actions-workflow.md` | `_markdown-lint.yml` |
| ISSUE-13 | INCONSISTENCY | HIGH | `coverage.md` | `_coverage-linux.yml` |
| ISSUE-14 | PROBLEM | HIGH | `github-actions-workflow.md` | `_build-linux.yml`, `_coverage-linux.yml` |
| ISSUE-15 | DUPLICATE | MEDIUM | `github-actions-workflow.md` | `_supply-chain-lint.yml`, `docker-ci-image.yml` |
| ISSUE-16 | GAP | MEDIUM | `branch-protection.md` | `ci.yml` |
| ISSUE-17 | GAP | MEDIUM | `caching.md` | `ci.yml` |
| ISSUE-18 | GAP | MEDIUM | `github-actions-workflow.md` | `docker-ci-image.yml` |
| ISSUE-19 | INCONSISTENCY | MEDIUM | `github-actions-workflow.md` | `_coverage-linux.yml` |
| ISSUE-20 | MISSING | MEDIUM | `dependency-management.md` | — |
| ISSUE-21 | GAP | MEDIUM | `github-actions-workflow.md` | `_package-windows.yml` |
| ISSUE-22 | INCONSISTENCY | MEDIUM | `caching.md` | `ci.yml` |
| ISSUE-23 | PROBLEM | MEDIUM | `coverage.md` | `_coverage-linux.yml` |
| ISSUE-24 | GAP | MEDIUM | `branch-protection.md` | `ci.yml` |
| ISSUE-25 | DUPLICATE | LOW | `dependency-management.md`, `CLAUDE.md` | — |
| ISSUE-26 | MISSING | LOW | `github-actions-workflow.md` | `docker-ci-image.yml` |
| ISSUE-27 | GAP | LOW | `caching.md`, `github-actions-workflow.md` | `_package-linux-deb.yml`, `_package-windows.yml` |
| ISSUE-28 | PROBLEM | LOW | `caching.md` | `_build-windows.yml`, `_package-windows.yml` |
| ISSUE-29 | MISSING | LOW | `caching.md` | `_build-windows.yml`, `_package-windows.yml`, `docker-ci-image.yml` |
| ISSUE-30 | GAP | LOW | `headless-ci-testing.md`, `caching.md` | `_build-linux.yml`, `_coverage-linux.yml` |

**Counts:** 5 CRITICAL, 9 HIGH, 10 MEDIUM, 6 LOW = **30 total issues**


---

## 10. Cross-Domain Technical Review

**Reviewer:** Technical Squad (All Disciplines)

---

# AI Town — Technical Squad Architecture Review

**Reviewers**: Senior C++ Developer (Irrlicht), Senior C++ Developer (OpenAL Soft),
Senior C++ Test Engineer, Senior GitHub Pipeline Engineer

**Date**: 2026-03-29

**Scope**: All architecture spec files in `/workspace/architecture/` reviewed for
gaps, problems, inconsistencies, duplicates, and missing content.
No source files were modified.

---

## Table of Contents

1. [Graphics Architecture](#1-graphics-architecture)
2. [Audio Architecture](#2-audio-architecture)
3. [Testing Architecture](#3-testing-architecture)
4. [CI/CD Architecture](#4-cicd-architecture)
5. [Game Design — Technical Feasibility](#5-game-design--technical-feasibility)
6. [UI/UX — Technical Feasibility](#6-uiux--technical-feasibility)
7. [Asset Standards — Technical Feasibility](#7-asset-standards--technical-feasibility)
8. [Cross-Domain Issues](#8-cross-domain-issues)

---

## 1. Graphics Architecture

### 1.1 Irrlicht Device Lifecycle (`irrlicht-device-lifecycle.md`)

**[GAP] MEDIUM — Frame-rate cap implementation detail missing**
The spec mandates a 60 FPS cap via `std::this_thread::sleep_for` but does not specify
how `realDelta` is computed when sleep causes the frame to run longer than 1/60 s
(e.g. under load). If `realDelta` is capped at 1/60 s (ignoring catch-up), fast
simulation speeds may fall behind. If it is not capped, a sleep overshoot accumulates
into the simulation. The spec should explicitly state: "realDelta is the raw wall-clock
delta even if it exceeds 1/60 s — no clamping is applied."

**[GAP] MEDIUM — Construction sequence step 16 (setUIManager) is described as "late-binding"
but no thread-safety guarantee is given**
Step 16 calls `renderer->setUIManager(uiManager)` after the Irrlicht device is fully
constructed. If any background thread (e.g. the audio thread spawned at step 14) could
call a renderer accessor between steps 14 and 16, a data race exists. The spec should
either state that the audio thread never touches the renderer, or add an explicit
contract that `setUIManager` is called before the audio thread is started.

**[GAP] LOW — Missing specification for what happens if `beginScene` fails**
The spec lists the mandatory 11-step per-frame loop but does not specify the recovery
path if `beginScene()` returns `false` (e.g. window minimised on Windows, driver lost).
The implementation will need to handle this; a note should be added.

**[INCONSISTENCY] MEDIUM — The spec says the loading screen does not call
`guiEnvironment->drawAll()`** but the same 11-step loop (which includes drawAll at step 6)
is described as the canonical frame loop. The exception for the loading screen carves
out a separate code path that is not fully documented — it is unclear whether steps 5
(AudioSystem::update), 4a (syncListenerToCamera), and 3b (UIManager::update) also fire
during the loading screen. The spec should enumerate which steps are active/inactive
during the loading-screen state.

---

### 1.2 Procedural Terrain (`procedural-terrain.md`)

**[GAP] HIGH — kCardinalFalloff / kDiagonalFalloff values are "not yet signed off"**
The spec explicitly notes that neighbour-blending falloff constants have not received
final sign-off. These values directly affect terrain visual quality and the
`setTileHeight` earthworks mesh outcome. Until signed off, any property-based test that
verifies blending correctness cannot be written against a stable spec. The sign-off
must happen before Phase 10b implementation begins, or the spec must mark these as
"subject to tuning" with explicit provisional values and a Phase 10b acceptance test
that validates the final values.

**[GAP] MEDIUM — DDA pickTerrainTile ray-terrain miss path**
The spec describes the DDA algorithm for ray-terrain intersection but does not define
what `pickTerrainTile()` returns when the ray leaves the terrain bounds without
intersecting any chunk (the "miss" path). The caller in `UIManager` (Priority 3 inspector
open path) checks the return value, but the return type and miss sentinel are not
specified in this file. Cross-reference to a definition in `IRenderer.h` is needed.

**[GAP] MEDIUM — flushPendingRebuilds 100 ms budget: wall-clock or sim-clock?**
`flushPendingRebuilds()` is specified to run with a 100 ms time budget. However, the
spec does not state whether this budget is measured against `IClock::nowSeconds()`
(wall-clock) or simulation time. For deterministic CI tests the distinction matters.
The IClock injection seam is mentioned, but the contract (wall-clock seconds) should
be made explicit.

**[MISSING] MEDIUM — No spec for how terrain chunks handle the boundary between the
map edge and "out of bounds"**
The DDA algorithm will eventually step off the edge of the map. The spec does not
define whether out-of-bounds tiles are clamped, wrapped, or treated as a miss. This
omission will result in implementation guesswork at boundary conditions.

---

### 1.3 Scene Graph Ownership (`scene-graph-ownership.md`)

**[PROBLEM] HIGH — Irrlicht compiled with `-fno-rtti`**
The spec correctly warns that `dynamic_cast` on Irrlicht types will SIGSEGV. However,
there is no corresponding note in `IrrlichtRenderer.h` or `SceneEntityManager.h`
directing developers to avoid `dynamic_cast` within those files. A `// WARNING:
dynamic_cast on Irrlicht types is UNDEFINED BEHAVIOR (-fno-rtti)` comment should be
mandated in the spec as a required header-level comment in those files.

**[GAP] MEDIUM — Zone overlay u16 batching: no spec for overflow handling**
The spec caps zone overlay SMeshBuffer at 10,922 quads (u16 limit). However, it does
not define what happens when more than 10,922 zone tiles are active simultaneously
(a 512×512 map has 262,144 tiles). The overflow path — allocate a second
SMeshBuffer, create a second scene node — is not specified. Without this, a large
city will produce rendering corruption (truncated overlay) or a crash from integer
overflow in the index buffer.

**[GAP] MEDIUM — Permanent renderer-internal nodes (sky dome, cloud plane) exempt from
SceneEntityManager: no cleanup contract**
The spec states these nodes are exempt from `SceneEntityManager`. However, it does not
specify when and how they are cleaned up on renderer shutdown. The Irrlicht device
`drop()` will remove all remaining scene nodes, but if the sky dome holds extra
references (e.g. via the cloud dome shader callback `grab()`), a reference leak may
occur. The shutdown sequence for renderer-internal nodes should be documented.

**[INCONSISTENCY] LOW — B3D asset pattern "borrowed from Irrlicht mesh cache, no drop after
setMesh"**
This is correct per Irrlicht's `setMesh()` contract, but the spec in `texture-cache.md`
describes a separate eviction path for textures. A note should clarify that B3D mesh
eviction (from the Irrlicht mesh cache) is NOT covered by `TextureCache` and follows
Irrlicht's internal cache management.

---

### 1.4 Texture Cache (`texture-cache.md`)

**[PROBLEM] HIGH — EDT_NULL guard for raw OpenGL calls is described in
`2d-texture-standards.md` but not in `texture-cache.md`**
`TextureCache::loadSRGB()` calls `glGenTextures` / `glCompressedTexImage2D`. The spec
in `2d-texture-standards.md` mandates an `EDT_NULL` guard before any raw GL call, but
`texture-cache.md` does not repeat or reference this guard. A developer reading only
`texture-cache.md` (the canonical texture loading spec) will miss the required guard,
leading to UB in integration tests. The guard must be documented in `texture-cache.md`
or a cross-reference to `2d-texture-standards.md` must be made explicit.

**[GAP] MEDIUM — LRU eviction policy: no maximum cache size specified**
`texture-cache.md` documents three pools and an eviction path, but never specifies the
maximum memory budget (in MB) before LRU eviction fires. Without a budget the
`evictUnreferenced()` decision point is implementation-defined. This is a functional gap
— the spec should state a nominal VRAM budget (e.g. 256 MB) or admit that eviction is
demand-driven and bounded by GPU VRAM.

**[GAP] LOW — Mip-map generation not specified for the raw-GL upload path**
`TextureCache::loadSRGB()` uploads via `glCompressedTexImage2D` but the spec does not
state whether `glGenerateMipmap` is called or whether the DDS file is expected to
contain all mip levels. For city assets at distance, missing mipmaps will produce
aliasing. The spec should either mandate full mip chains in DDS files or specify
`glGenerateMipmap` after upload.

---

### 1.5 Shader Loading (`shader-loading.md`)

**[PROBLEM] HIGH — "Failure path safety: cb is destroyed on matType==-1"**
The spec states that when `addHighLevelShaderMaterialFromFiles` returns -1, the
callback `cb` must not be accessed after `drop()`. However, the spec does not address
the case where the failure occurs mid-frame when other systems may already hold a
pointer to the callback (e.g. stored in a renderer member). If `drop()` deletes the
callback and a renderer member still holds the raw pointer, a use-after-free results.
The spec should mandate that all renderer members pointing to a callback are nulled
immediately on failure before calling `drop()`.

**[GAP] MEDIUM — Save/restore GL_ACTIVE_TEXTURE: no spec for what unit is restored**
The spec mandates save/restore of `GL_ACTIVE_TEXTURE` inside `OnSetConstants()` to
prevent Irrlicht state corruption, but does not specify which unit Irrlicht expects to
be active after `OnSetConstants()` returns. If Irrlicht assumes GL_TEXTURE0 is active
but the shader callback restore-saves a non-0 unit, subsequent Irrlicht texture binds
may go to the wrong unit. The spec should state: "restore to `GL_TEXTURE0` after
terrain splat shader setup, regardless of which unit was active on entry."

**[GAP] LOW — ui_quad raw GL path: shader compile error handling not specified**
The `ui_quad` shader uses `glCreateShader`/`glCompileShader` directly. The spec
documents the normal path but not what happens if shader compilation fails (e.g.
driver version incompatibility). The error path should at minimum log the GLSL
info log and fall back to a solid-color material.

---

### 1.6 Sky and Clouds (`sky-clouds.md`)

**[PROBLEM] HIGH — kCloudAltitude = -1000m is below ground for all terrain**
The spec places the cloud plane at Y = -1000 m (kCloudAltitude). With the terrain
starting near Y = 0, this means the cloud plane renders below all terrain. The spec
says the camera is above the dome looking down at pitch [-70°, -20°], which means
the cloud plane at -1000 m would appear at the horizon or below it. The rationale
for a negative kCloudAltitude must be explained; if this is intentional (placing clouds
below the horizon as atmospheric haze), it should be called out explicitly. If this is
a typo and the intended value is +1000 m, it is a CRITICAL bug in the spec.

**[GAP] MEDIUM — CloudDomeShaderCallback keeps its own reference via `void* m_cloudShaderCbRaw`**
The spec documents this pattern to avoid the raw pointer being deleted while in use, but
does not specify when this self-reference is released. If the renderer is destroyed
while the callback is still alive (e.g. due to a refcount bug), memory leaks. The
shutdown sequence for the cloud dome callback should explicitly state that
`m_cloudShaderCbRaw->drop()` is called in the renderer's destructor after the scene
manager is cleaned up.

**[INCONSISTENCY] MEDIUM — farClip must be 15000m but irrlicht-device-lifecycle.md
does not mention this**
`sky-clouds.md` requires `farClip >= 15000 m` to prevent hard-clipping of dome
vertices. The Irrlicht device lifecycle spec does not list camera farClip as a
construction parameter. If the farClip is set in `IrrlichtRenderer::init()` and later
overwritten by any camera-construction code that uses a default farClip, the cloud dome
will clip. The device lifecycle spec should reference the farClip requirement or mandate
that farClip is set in the same step that creates the camera scene node.

---

### 1.7 Benchmark Tool (`benchmark-tool.md`)

**[GAP] LOW — Second ISceneManager lifetime not specified**
The benchmark tool creates a second `ISceneManager*` via `smgr->createNewSceneManager(false)`.
The spec does not state when this scene manager is dropped. If the benchmark tool exits
before calling `drop()`, Irrlicht will leak the scene manager and all its nodes. The spec
should mandate `drop()` on the second scene manager in the benchmark tool's destructor.

---

### 1.8 Model Validator Tool (`model-validator-tool.md`)

**[GAP] LOW — Road tiles under Vehicles category**
The spec states road tiles use "the identical code path as `IrrlichtRenderer`" in the
model validator. However, `3d-model-standards.md` now specifies that road tile geometry
is procedurally generated at runtime (no `.b3d` file). The model validator must
therefore also construct road tile meshes procedurally rather than loading from disk.
The spec should note this distinction and confirm the validator calls the same
`buildTileRoadMesh()` function.

---

## 2. Audio Architecture

### 2.1 AudioSystem (`audio-system.md`)

**[GAP] HIGH — `setGameOverState()` is specified as a V1 no-op with LOG_WARNING**
but no test verifies this no-op contract. If a future implementer accidentally
makes `setGameOverState()` functional (e.g. modifying music state), existing tests
will not catch the regression. A test `AudioSystem_SetGameOverState_IsNoOp` should
be mandated in the testing spec.

**[GAP] MEDIUM — IAudioSystem interface evolution tracking**
The spec notes the interface evolved across Phase 7 → Phase 10 → Phase 11d → Phase 11m.
There is no single authoritative method count or interface version marker. If a test
mock or a future platform backend is built from an older spec snapshot, it will silently
miss methods. A comment block in `IAudioSystem.h` with a method count assertion (similar
to the 21-method comment in `IUIBackend`) should be mandated.

**[GAP] MEDIUM — transitionToGameplay / transitionToMainMenu symmetry not fully specified**
The spec lists both methods as Phase 11m additions but does not document their full
contracts (e.g., does `transitionToMainMenu()` stop all vehicle engine pairs? does it
reset the stems? does it wait for crossfade completion?). This is an implementation
contract gap that will cause divergent behavior across implementations.

---

### 2.2 Audio Thread Shutdown (`audio-thread-shutdown.md`)

**[PROBLEM] HIGH — Step 3.5 requires rebinding the context to the main thread after join**
The spec requires `alcMakeContextCurrent(m_context)` on the main thread after the audio
thread joins, before AL object cleanup. However, `alcMakeContextCurrent` is a
process-wide operation (not thread-local for the main context), and the spec also
mandates `alcSetThreadContext` for the audio thread. If both are in use simultaneously
during teardown, the order dependency is fragile. The spec should clarify: does the
audio thread call `alcSetThreadContext(nullptr)` before it exits, and does the main
thread then call `alcMakeContextCurrent(m_context)` to re-establish ownership? The
current wording implies both but does not sequence them explicitly.

**[INCONSISTENCY] MEDIUM — kEvictableSFXCount (55) as shutdown loop guard vs.
kSFXPoolSize (58)**
The spec uses `kEvictableSFXCount = 55` as the loop bound for EFX filter cleanup
because stingers and reserved sources do not have EFX filters. This is correct, but
`source-pool.md` defines the pool boundaries differently. The spec should add a
cross-reference: "kEvictableSFXCount = 55 = kTotalSources(62) − kStingerCount(2) −
kReservedCount(1) − kStreamSourceCount(4)" to prevent the constant from diverging
from the pool layout spec.

---

### 2.3 Error Checking (`error-checking.md`)

**[GAP] LOW — `alcCheckError` uses `void*` parameter to avoid AL/alc.h in header,
but the production call sites cast back to `ALCdevice*`**
The `void*` signature means type-checking is lost at the call site. If a caller
accidentally passes a non-device pointer (e.g. a `void*` handle to a different object),
the resulting `alcGetError()` call will operate on garbage. The spec should mandate a
comment at every call site: `// alcCheckError(device, ...)` with the actual type, and
optionally a `static_assert(sizeof(ALCdevice*) == sizeof(void*))` guard.

---

### 2.4 HRTF Initialization (`hrtf-initialization.md`)

**[GAP] MEDIUM — `default.mhr` copy via POST_BUILD: no fallback if file is absent**
The spec mandates a CMake POST_BUILD rule to copy `default.mhr` to the build output
directory. If the file is absent (e.g. a clean checkout with missing LFS content or
a vcpkg version that no longer ships HRTF data), the HRTF init will silently fall back
to stereo panning without warning. The spec should mandate a `file(COPY ... DESTINATION
... RESULT_VARIABLE rv)` check in CMakeLists.txt and a build error if the file is
missing.

**[INCONSISTENCY] MEDIUM — `OpenAL32.dll` renamed to `soft_oal.dll` via POST_BUILD**
The spec says this is a "hard-fail" in Phase 7. However, `ci-cd/github-actions-workflow.md`
does not list a verification step that confirms `soft_oal.dll` is present in the Windows
build artifacts before running tests. If the POST_BUILD rename step is skipped or fails
silently (e.g. `file(RENAME ...)` with no error check), Windows CI tests will fail
with a cryptic DLL-load error rather than a meaningful build error.

---

### 2.5 Source Pool (`source-pool.md`)

**[GAP] HIGH — kMaxVehiclePairs = 12 (24 pool slots / 2 per vehicle) is derived
from kEvictableSFXCount = 55, but the vehicle pair slots are carved out of the
evictable SFX pool**
The spec states vehicle engine sources are `sources[kVehiclePairStart..kVehiclePairEnd]`
within the evictable SFX range but does not define `kVehiclePairStart` or `kVehiclePairEnd`
as named constants. These values will be hardcoded implicitly. The spec should define
them as `constexpr` values in `audio_constants.h` with a static_assert confirming
`kVehiclePairEnd − kVehiclePairStart + 1 == kMaxVehiclePairs * 2`.

**[INCONSISTENCY] LOW — Post-V1 4-step GAME_OVER stinger promotion sequence is
documented in `source-pool.md` but the stinger types spec in `audio-system.md`
does not cross-reference this post-V1 behavior**
Future implementers extending the stinger system may not find the promotion sequence
spec. A cross-reference should be added.

---

### 2.6 Streaming Architecture (`streaming-architecture.md`)

**[PROBLEM] HIGH — Pattern A (SPSC tryPop) vs Pattern B (std::vector push_back): must
pick one**
The spec explicitly states "must pick one" for the pre-load queue drain pattern but
leaves the choice open. This is an unresolved design decision that will produce
inconsistent implementations if two developers implement different audio subsystems.
The decision must be made and the spec updated before implementation.

**[GAP] MEDIUM — `alGetError()` before `alBufferData` to clear stale errors**
The spec correctly mandates this, but does not state what action to take if the
pre-clear reveals a genuinely stale error (i.e. a non-AL_NO_ERROR state before
`alBufferData` is called). The spec should clarify: "log the stale error via
`alCheckError()` before clearing, then proceed; do not abort the buffer upload on
stale errors since they reflect prior frames."

**[GAP] MEDIUM — kSamplesPerBuffer = 16384 frames and buffer count**
The spec defines the per-buffer frame count but does not state how many buffers are
allocated per stream source. The circular buffer queue depth (typically 3–4 buffers
for OGG streaming) is not specified. Without this, the decode loop's queue-depth
invariants are implementation-defined.

---

### 2.7 Spatial Audio (`spatial-audio.md`)

**[GAP] MEDIUM — Z-negation for AL_ORIENTATION is specified but only for the
`syncListenerToCamera` path. The spec does not state whether positional SFX sources
also require Z-negation when setting `AL_POSITION`**
If `playPositionalSound()` receives an Irrlicht world-space position `(x, y, z)` and
sets `alSource3f(source, AL_POSITION, x, y, z)` without negating Z, all positional
audio will be spatially mirrored on the Z axis. The spec should explicitly state:
"all AL_POSITION calls on SFX sources must also negate Z to match the listener
coordinate convention."

**[PROBLEM] MEDIUM — `alcMakeContextCurrent` process-wide binding "permanent at runtime"**
The spec states the process-wide context binding is never cleared while
`syncListenerToCamera` is active. However, if the audio system is destroyed while a
modal dialog is on screen (e.g. the game-over path), `syncListenerToCamera` may no
longer be called. The spec should clarify: "after `AudioSystem::shutdown()` is called,
`syncListenerToCamera` must not be called; the renderer must null the audio system
pointer before entering the shutdown path."

---

### 2.8 Audio Occlusion (`audio-occlusion.md`)

**[GAP] MEDIUM — Mid-loop EFX filter allocation failure: "must delete the partially-
allocated filter immediately"**
The spec mandates immediate cleanup on partial allocation failure but does not specify
the AL call sequence: should `alDeleteFilters(1, &filter)` be called even if
`alGenFilters` returned successfully but `alFilterf` failed? The spec should show the
exact cleanup sequence.

**[INCONSISTENCY] LOW — `m_efxAllocationAttempted` vs `m_efxAvailable` in shutdown vs.
occlusion paths**
`audio-thread-shutdown.md` uses `m_efxAllocationAttempted` as the shutdown loop guard.
`audio-occlusion.md` uses `m_efxAvailable` as the guard for applying filter values.
These are documented as two separate booleans with distinct roles, which is correct.
However, neither file cross-references the other, making it easy for an implementer
to conflate them. Both files should add: "See `audio-occlusion.md`/`audio-thread-shutdown.md`
for the complementary boolean."

---

### 2.9 Dynamic Soundscape (`dynamic-soundscape.md`)

**[PROBLEM] HIGH — `m_lastDuckWakeTime` must be initialized BEFORE `notify_one()`**
The spec explicitly states this constraint. However, there is no corresponding test
that verifies initialization order — the constraint is impossible to test without
inspecting thread interleaving. At minimum, a code-review checklist item should
be added to the spec: "Reviewer must verify `m_lastDuckWakeTime` initialization
appears before `m_initCV.notify_one()` in the audio thread startup sequence."

**[GAP] MEDIUM — In V1 DUCKED state, only sources[55] and sources[56] are checked
(NOT sources[57])**
The spec states this exclusion but does not explain why sources[57] (the reserved
source) is excluded. The rationale (sources[57] is a reserved non-stinger source that
should not be ducked) should be documented to prevent a future implementer from
"fixing" this apparent off-by-one.

**[GAP] LOW — Stinger loudness target -18 LUFS / -1 dBTP**
This loudness target is specified in `dynamic-soundscape.md` but is not cross-referenced
in `v1-audio-asset-manifest.md`. The artist spec and the technical spec may diverge
if one is updated without the other.

---

### 2.10 V1 Audio Asset Manifest (`v1-audio-asset-manifest.md`)

**[INCONSISTENCY] MEDIUM — Vehicle engine minimum duration: spec says ≥ 6 s,
but `audio-asset-formats.md` tier boundary is "5–19.999 s for OGG pre-loaded"**
If a vehicle engine OGG is exactly 5.0 s, it falls in the "5–19.999 s" pre-load
tier per `audio-asset-formats.md` but violates the "≥ 6 s" minimum in the manifest.
The manifest should be the binding contract; `audio-asset-formats.md` should note
the vehicle engine exception: "Vehicle engine loops: minimum 6 s per `v1-audio-asset-manifest.md`."

**[INCONSISTENCY] LOW — `sfx_vehicle_horn` priority: manifest says HIGH priority;
`source-pool.md` does not call out horn as a HIGH-priority sound in its pool pressure
examples**
This is a minor documentation inconsistency. `source-pool.md` should add sfx_vehicle_horn
to its HIGH-priority example list.

---

## 3. Testing Architecture

### 3.1 Framework (`testing/framework.md`)

**[GAP] MEDIUM — `aitown_add_tests()` macro is documented as the canonical helper
but its implementation in `AitownTestHelpers.cmake` is not cross-referenced in this file**
A developer reading `framework.md` must know to look in `AitownTestHelpers.cmake` for
the macro definition. This should be an explicit cross-reference: "Defined in
`cmake/AitownTestHelpers.cmake`; see also `ci-cd/dependency-management.md` for the
vcpkg discovery mode requirement."

**[GAP] MEDIUM — terrain_tests TIMEOUT 300 / DISCOVERY_TIMEOUT 60 are specified
but no rationale is given**
Without a rationale, future maintainers may reduce these timeouts to match other
test targets, causing intermittent CI failures on slow machines. The spec should
note: "300 s timeout is required because terrain mesh rebuild tests iterate over
large chunk arrays at multiple LOD levels and may take 200+ s on CI-class hardware."

---

### 3.2 Coverage (`testing/coverage.md`)

**[PROBLEM] HIGH — lcov `--ignore-errors mismatch,inconsistent,version` is specified
as a comma-separated single flag, but CLAUDE.md says only `mismatch,inconsistent`
(not `version`)**
`coverage.md` lists three error codes; `CLAUDE.md` (Notes for AI Assistants) lists
only two. If the `version` suppressor is needed (GCC/gcov version-string mismatch)
and is missing from the CLAUDE.md guidance, any agent or developer following CLAUDE.md
for local builds will see spurious failures. Either CLAUDE.md must be updated to
add `version`, or `coverage.md` should be the sole authority and CLAUDE.md should
reference it rather than repeating the flag.

**[GAP] MEDIUM — No spec for how coverage_filtered.info is verified to be non-empty**
After `lcov --remove`, if all paths are excluded by mistake (e.g. a broken glob
pattern), `coverage_filtered.info` will be empty and `lcov --summary` will report 0%
coverage, which the awk gate will fail. The CI spec should add a step that checks the
filtered info contains at least one source file before running the coverage gate.

**[GAP] LOW — Four exclusion prefixes (`mock_*`, `manual_*`, `Mock*`, `Manual*`) but
no exclusion for auto-generated files from vcpkg**
vcpkg-managed headers may be included inline in test builds via template instantiation,
adding them to the coverage data. The `${BUILD_DIR}/_deps/*` pattern is mentioned but
CLAUDE.md warns it "never exists". The exclusion for vcpkg headers should use
`"*/.fetchcontent_cache/*"` AND `"*/vcpkg_installed/*"` to be safe.

---

### 3.3 Testability Architecture (`testing/testability-architecture.md`)

**[GAP] HIGH — `NotificationManager` constructor signature is
`(IUIBackend*, ICitySimulation*, IClock*, IAudioSystem*)` but no test verifies
the constructor succeeds when `IAudioSystem*` is nullptr (for tests that do not
need audio)**
The fourth parameter was added late (Phase 11m). If any existing test constructs
`NotificationManager` with 3 arguments (pre-Phase-11m signature), it will fail to
compile. The spec should explicitly state: "all four parameters are mandatory; nullptr
is NOT a valid value for any parameter in V1 tests."

**[GAP] MEDIUM — `UIManager` 8 integration test cases are enumerated but no test
specifies the teardown order**
The spec says "add `TearDown()` to explicitly reset `sim_` and document
destructor-path contract" but does not specify what "reset" means — whether it is
`sim_.reset()` (for `unique_ptr`) or `sim_ = nullptr` or `sim_.release()`. The
teardown contract must be unambiguous to avoid ASAN failures on test shutdown.

**[INCONSISTENCY] MEDIUM — `MockUIBackend` is described as the test-facing authority
for `IUIBackend` (21 methods), but `hud-layout.md` references `setMouseCursor()` as
deferred to Phase 12 — implying a 22nd method will be added post-V1**
If `setMouseCursor()` is added to `IUIBackend` in Phase 12, the method count increases
to 22. The spec should note: "Phase 12 will add `setMouseCursor()` as method 22; at
that time both `ui-manager.md` and `testability-architecture.md` must be updated
simultaneously." Without this note, the Phase 12 implementer may add the method to
`IrrlichtUIBackend` without updating `MockUIBackend`, breaking test builds.

---

### 3.4 Headless CI Testing (`testing/headless-ci-testing.md`)

**[GAP] MEDIUM — Phase 11b: container mode / xvfb pre-installed noted, but no spec
for what happens if xvfb fails to start**
The CI workflow runs `xvfb-run --auto-servernum` before OpenGL tests. If xvfb fails
to start (e.g. display server conflict, missing DISPLAY socket), the test binary will
receive a DISPLAY that has no OpenGL server and will exit with a connection error.
The spec should mandate a `xvfb-run` health check step before the test step, or
specify that `--auto-servernum` is sufficient.

**[GAP] LOW — No spec for memory or resource limits during headless OpenGL tests**
Headless Irrlicht with a software renderer (or mesa `llvmpipe`) may use significant
RAM. CI runners with 7 GB RAM may OOM-kill the test process. The spec should note
whether `LIBGL_ALWAYS_SOFTWARE=1` or `MESA_GL_VERSION_OVERRIDE` is expected, and
whether a RAM limit exists.

---

### 3.5 Property-Based Tests (`testing/property-based-tests.md`)

**[GAP] MEDIUM — Economy invariant: interest computed on outstanding balance BEFORE
repayment that tick**
The spec defines the order explicitly. However, the RapidCheck property does not
specify how to handle the edge case where `outstanding_balance < repayment_this_tick`
(final repayment tick where the remainder is absorbed). If the test uses generic
`rc::gen::arbitrary<int>()` for the balance, it may generate negative balances that
cause the invariant to fail for the wrong reason. The spec should mandate that the
generator is constrained to positive balances.

**[GAP] LOW — ManualClock `advance(121.0)` before debt cap rc::check**
The spec requires `advance(121.0)` to clear the 120 s grace period gate. However,
it does not state whether `advance()` is cumulative or absolute. If a test calls
`advance(60.0)` earlier in setup and then `advance(121.0)` again, the total may be
181 s, which clears the gate twice. The spec should use absolute timestamps or
document that `advance()` is additive.

---

### 3.6 Procedural Generation Seeds (`testing/procedural-generation-seeds.md`)

**[GAP] MEDIUM — "All generators accept uint64_t seed; log seed on RapidCheck failure"**
The spec does not define which C++ logging function to use for seed logging on failure.
If each generator implements its own logging, the output format will differ, making
seed-reproduction from CI logs difficult. A canonical format
(`SEED: <decimal uint64>`) should be mandated.

---

## 4. CI/CD Architecture

### 4.1 GitHub Actions Workflow (`ci-cd/github-actions-workflow.md`)

**[PROBLEM] HIGH — Supply-chain SHA lint is the first step after checkout in
build-linux, but AITOWN_HEADLESS is set as an env var on the unit and integration
test steps and NOT on the requires-opengl step**
This is correctly documented, but the spec does not include a step that VERIFIES
`AITOWN_HEADLESS` is absent from the requires-opengl step environment. If a developer
copy-pastes the env block from the unit test step, the OpenGL test will silently
run headlessly and produce incorrect coverage. A lint or diff-based check should
be mandated.

**[GAP] HIGH — No spec for handling CI runner out-of-disk-space**
vcpkg builds can consume 5–15 GB of disk space. GitHub-hosted Linux runners have
approximately 14 GB available. If a vcpkg port build fails mid-way due to disk
exhaustion, the error message is a generic CMake failure with no disk-space context.
The workflow spec should add a disk-space check step early in build-linux/build-windows.

**[GAP] MEDIUM — job timeout-minutes: build-linux=30, build-windows=40, coverage-linux=60**
The spec sets these timeouts but does not document how they were derived. If a
dependency is added that increases build time by 10 minutes, the timeout will be
silently violated. The spec should note that these timeouts include vcpkg build time
and should be increased if new vcpkg ports are added.

**[INCONSISTENCY] MEDIUM — AITOWN_HEADLESS=1 is set on the unit and integration test
steps but the spec says `ALSOFT_DRIVERS=null` is also required on both steps**
`ci-cd/github-actions-workflow.md` is the canonical workflow spec, but CLAUDE.md
(Notes for AI Assistants / Windows CI section) says both env vars are needed.
The Linux section should be audited to confirm `ALSOFT_DRIVERS=null` is also set
for the Linux unit/integration test steps, not just Windows.

---

### 4.2 Dependency Management (`ci-cd/dependency-management.md`)

**[GAP] HIGH — `fmt` must be an explicit dependency because openal-soft vcpkg
portfile devendors it**
This is correctly documented. However, there is no automated check that `fmt` appears
in `vcpkg.json` — a vcpkg baseline bump that silently removes `fmt` (if it becomes
a transitive dependency again in a future baseline) would break builds. The supply-chain
lint step should add a check: "confirm `fmt` is in `vcpkg.json` features list."

**[GAP] MEDIUM — `VCPKG_COMMIT_ID` at workflow level (not job level) is a policy
decision that is documented but not enforced**
If a developer adds a new job and accidentally sets `VCPKG_COMMIT_ID` at the job level
(overriding the workflow-level value), the build will use a different vcpkg baseline
for that job. The spec should mandate a supply-chain lint step that checks all job-level
env blocks for `VCPKG_COMMIT_ID` and fails if found.

---

### 4.3 Caching (`ci-cd/caching.md`)

**[GAP] MEDIUM — coverage-linux must use a distinct ccache key (-coverage suffix)**
This is documented, but the spec does not state what happens if the non-coverage cache
is populated but the coverage cache is empty (cold start). The coverage build may
spuriously use the non-coverage ccache hit (if the key structure allows fallback via
`restore-keys`). The spec should document whether `restore-keys` is used for the
ccache step and, if so, whether cross-contamination between coverage and non-coverage
caches is acceptable.

**[GAP] MEDIUM — BOTH build-linux AND coverage-linux need independent compiler-detect steps**
This is documented in CLAUDE.md but not in `caching.md`. `caching.md` should
explicitly state: "the compiler-detect step must be present in BOTH the build-linux
and coverage-linux jobs — do not share a single step output across jobs."

**[INCONSISTENCY] LOW — Compiler cache key includes 4 components but the spec does not
state whether the 4-component key format produces cache invalidation when moving
between GitHub-hosted runners (e.g. ubuntu-22.04 vs ubuntu-24.04)**
If the runner OS version changes, the "OS" component of the key changes, correctly
invalidating the cache. However, the spec should explicitly name the OS component as
`${{ runner.os }}-${{ matrix.os }}` rather than just "OS" to make the implementation
unambiguous.

---

### 4.4 Branch Protection (`ci-cd/branch-protection.md`)

**[PROBLEM] HIGH — `all-checks-pass` must have `if: always()` to prevent skip-on-failure**
This is documented but is a common implementation mistake. The spec should include
a YAML snippet showing the exact `if: always()` placement, not just a prose description.
Without the snippet, developers may place `if: always()` at the wrong level
(e.g., on a step within the job rather than on the job itself).

**[GAP] LOW — Protection on both `main` and `develop` is documented but the spec does
not state whether `force-push` is disabled**
GitHub branch protection has a "Restrict force pushes" option that is distinct from
the required-status-checks rule. The spec should explicitly state: "force pushes
are disabled on both `main` and `develop`."

---

## 5. Game Design — Technical Feasibility

### 5.1 Simulation Time (`game-design/simulation-time.md`)

**[GAP] MEDIUM — Frame-loop step 3b `UIManager::update()` and step 3 `CameraController::update()`
are given identical step numbers (3 and 3b) in the canonical 8-step loop**
Steps 3 and 3b appear to be sequential sub-steps. However, the spec does not clarify
whether 3 and 3b must be sequential (no other code between them) or whether they can
be interleaved with other operations. The numbering should be made unambiguous.

**[INCONSISTENCY] MEDIUM — `SaveSystem::update(realDeltaSeconds)` is at step 3c but is
not listed in the 8-step frame loop in `simulation-time.md`**
`save-system.md` references a "step 3c" in the main loop for `SaveSystem::update()`.
`simulation-time.md`'s canonical frame loop only shows 7 distinct steps and does not
include step 3c. The two files are inconsistent on the canonical frame loop
structure.

---

### 5.2 Economy Model / Zoning / Population

**[GAP] MEDIUM — Density unlock thresholds reference `economy-model.md` as the
authoritative source, but `population-density-growth.md` partially reproduces them**
`population-density-growth.md` lists unlock thresholds with the caveat "Economy Model
is the authoritative source." If the two files diverge (e.g. during a rebalance),
the implementation will use one and tests will verify the other. The reproduction in
`population-density-growth.md` should either be removed or replaced with an explicit
XREF-only note.

**[MISSING] MEDIUM — No spec for `getDemandPressurePct()` return type precision**
`hud-layout.md` notes the return type is `float` in `[0.0, 1.0]` and warns the HUD
must multiply by 100. However, `zoning-system.md` and `simulation-time.md` do not
define the interface method at all. `ICitySimulation` method signatures (beyond what
is in `testability-architecture.md`) are scattered across multiple spec files with
no single master list.

---

### 5.3 Terrain Interaction (`game-design/terrain-interaction.md`)

**[GAP] MEDIUM — Phase 10b terrain mesh modification: `setTileHeight()` is called for
all 4 corners but neighbour chunk boundaries are not addressed**
When a tile at the edge of a chunk has its corners flattened, the adjacent chunk's
heightmap is not updated. The spec describes the flattening of all 4 corners of the
placed tile, but a corner vertex is shared between up to 4 chunks. Not rebuilding
adjacent chunks will produce visible seams at chunk boundaries after earthworks.
`procedural-terrain.md` addresses the neighbour sync for the LOD rebuild deque but
does not specifically mandate it for the earthworks path.

---

### 5.4 Game Over Flow (`game-design/game-over-flow.md`)

**[GAP] MEDIUM — "Load Last Save" from the game-over modal uses the loading-screen
path, but the spec does not specify what happens if no save exists**
`save-system.md` states the Load Last Save button is grayed out with a tooltip
if no save exists. However, `game-over-flow.md` describes the transition path without
addressing the no-save case. If the game-over modal's "Load Last Save" button is
activated with no save files (e.g. the player never saved), the spec's behavior is
undefined.

---

### 5.5 Save System (`game-design/save-system.md`)

**[GAP] MEDIUM — `ISaveSystem::loadMostRecentSave()` returns a `LoadResult` enum,
but the enum is not defined in `save-system.md`**
The spec defines `LoadResult::Ok`, `LoadResult::NoSaveFound`, and
`LoadResult::Corrupted` inline in the spec. However, the canonical location for this
enum (simulation_types.h? save_system_types.h?) is not specified. Without a canonical
header location, implementations will put the enum in different places, causing ODR
issues.

**[GAP] LOW — `building_variant_counters` array: index formula is `zone * 3 + tier`**
The spec states the array has exactly 9 elements. However, it does not specify the
`zone` and `tier` enum values or their ordering. If `ZoneType::Residential = 0`,
`Commercial = 1`, `Industrial = 2` and `DensityTier::Low = 0`, `Medium = 1`, `High = 2`,
the formula works. But if the enum values change or are non-contiguous, the formula
breaks. The spec should mandate that the enum values are 0/1/2 contiguous and add
a static_assert.

---

## 6. UI/UX — Technical Feasibility

### 6.1 Input Arbitration (`ui-ux/input-arbitration.md`)

**[PROBLEM] HIGH — Priority 2 dual-guard (criticalVisible && !modalActive) is extensively
documented but the spec does not mandate a unit test for the same-frame race
condition (modal + CRITICAL toast activate on the same tick)**
`testability-architecture.md` includes 8 UIManager integration tests but none of
them covers the same-frame modal + CRITICAL toast race. Without a test, the dual-guard
may be silently removed by a refactor. A test `UIManager_SameFrameModalAndCriticalToast_ModalTakesPrecedence`
should be mandated.

**[GAP] MEDIUM — CameraController receives `EMIE_RMOUSE_LEFT_UP` unconditionally,
even after UIManager consumes the event**
The spec explicitly states this requirement to prevent the drag flag from getting
stuck. However, `EventReceiver` is not described in detail in any spec file — the
contract is stated in `input-arbitration.md` but the implementation location
(`src/platform/EventReceiver.cpp`) is not referenced. A developer may implement
the UIManager Ctrl+Z handler without knowing that `CameraController` must also
receive RMB up. A reference to `EventReceiver.cpp` should be added.

**[GAP] MEDIUM — `WindowFocusGained`/`WindowFocusLost` must NOT be consumed by
UIManager (any priority), but there is no test for this**
The spec mandates these events pass through unconditionally. Without a test that
verifies UIManager returns `false` for focus events, a refactor that accidentally
consumes them will break edge-scroll suppression on Alt+Tab silently.

---

### 6.2 HUD Layout (`ui-ux/hud-layout.md`)

**[INCONSISTENCY] HIGH — `kToolbarBottom = 784` is the input gate, but
`input-arbitration.md` Priority 3 toolbar carve-out also states `virtual x: 8–72 px,
y: 64–784 px`**
Both files agree on the value, but `hud-layout.md` explicitly warns "DO NOT use y:600
as an input gate threshold" while `input-arbitration.md` lists the correct 784 value
in its dispatch table. The consistency is good, but `hud-layout.md` should add a
cross-reference: "See `input-arbitration.md` Priority 5 toolbar dispatch table for
the enforcement point — both files must agree on `kToolbarBottom = 784`."

**[GAP] MEDIUM — Demand pressure bar inverse semantics warning**
`hud-layout.md` warns: "Do NOT use `QueryResult::demandPressurePct` directly to fill
the HUD demand bar — it uses the complementary definition." However,
`query-inspector-panel.md` (not yet reviewed in full but referenced) defines
`demandPressurePct` as `(1.0f − effective_demand_factor) × 100`. This inversion is
a known footgun. A `static_assert` or a type alias (`InverseDemandPct` vs
`DemandFillPct`) should be considered to make the inversion compile-time-visible
rather than runtime-detectable.

---

### 6.3 Notification System (`ui-ux/notification-system.md`)

**[GAP] MEDIUM — CRITICAL toast keyboard navigation: "first CRITICAL toast receives
keyboard focus automatically when it becomes visible"**
The spec does not define how keyboard focus is transferred back to the underlying
HUD after the last CRITICAL toast is dismissed. If focus remains on a now-removed
UI element, subsequent keyboard input may be lost. The spec should state:
"after the last CRITICAL toast is dismissed, keyboard focus returns to the
previously-focused HUD element (or the speed selector if no element was focused)."

**[GAP] LOW — Notification log stores full text of truncated toasts but no spec
for log size limit**
Toasts beyond depth 10 are logged to the notification log. However, the log has no
specified maximum depth. Over a long play session the log may grow unboundedly.
A maximum log depth (e.g. 200 entries) and a FIFO eviction policy should be specified.

---

### 6.4 UIManager (`ui-ux/ui-manager.md`)

**[GAP] MEDIUM — `onGameLoaded()` is referenced in `main-menu-new-game-flow.md` as
a required post-load call but its full contract is not specified in `ui-manager.md`**
`main-menu-new-game-flow.md` states: "The loading controller must call
`UIManager::onGameLoaded()` after deserialization completes and before the first
`UIManager::update()` tick." `ui-manager.md` does not document `onGameLoaded()` in
its method list. The method's contract (what state it resets, which panels it hides/shows)
must be specified.

---

## 7. Asset Standards — Technical Feasibility

### 7.1 3D Model Standards (`asset-standards/3d-model-standards.md`)

**[INCONSISTENCY] HIGH — Road tile mesh is "procedurally generated in C++ at runtime"
but the model validator spec (`model-validator-tool.md`) uses the same code path**
The model validator is defined as a tool that validates authored B3D assets.
If road tiles are code-generated, the validator's road tile test is validating code
behavior, not an asset. The model validator spec should explicitly call out which
asset categories it covers (skipping road tiles) and which are code-generated.

**[GAP] MEDIUM — LOD distance thresholds for road tiles: "same as small buildings/props"**
Road tiles use LOD0→LOD1 at 30 m / 25 m and LOD1→LOD2 at 100 m / 90 m per
the spec text. However, the LOD distance threshold table in the spec does not have a
row for "Road tiles" — they are only mentioned in the narrative. The table should have
an explicit road tile row to prevent confusion with the procedural terrain chunk row
(which uses entirely different distances: 100 m / 300 m).

**[GAP] MEDIUM — `validate_assets.py` must NOT look for road tile `.b3d` files**
This rule is stated but the spec does not define what `validate_assets.py` should do
if it encounters a road tile asset path in a manifest. Should it error? Warn? Skip?
The policy should be explicit.

---

### 7.2 2D Texture Standards (`asset-standards/2d-texture-standards.md`)

**[PROBLEM] HIGH — `GL_EXT_texture_sRGB` extension check at `RenderSystem::init()`:
`glewIsExtensionSupported` is called after `createDevice()` returns, but the spec does
not state whether GLEW is initialized before this call**
`glewInit()` must be called after an OpenGL context is created and made current.
The spec notes the check happens "after `createDevice()`", but GLEW initialization
is not mentioned in the Irrlicht device lifecycle spec. If GLEW is not initialized
before the extension check, `glewIsExtensionSupported` will return `GL_FALSE` (or
crash) even on a capable GPU. The lifecycle spec should mandate:
"call `glewInit()` immediately after `createDevice()` and before any `glew*` calls."

**[GAP] MEDIUM — `GL_MAX_TEXTURE_SIZE` initialized to 2048 under EDT_NULL**
The EDT_NULL fallback of 2048 is reasonable for most textures but may be too small
for the 1024×1024 building atlas. If any code uses `m_maxTextureSize` to gate atlas
dimension selection, and tests run under EDT_NULL, the atlas may be sized to 2048
rather than the actual asset dimension. Tests that verify atlas layout must either
mock the `getMaxTextureSize()` call or explicitly set the EDT_NULL fallback to a
value large enough for all test cases.

---

### 7.3 Building Atlas Layout (`asset-standards/building-atlas-layout.md`)

(Not read in full — only referenced indirectly. Flagging for completeness.)

**[MISSING] LOW — Atlas layout spec cross-reference in `texture-cache.md`**
`texture-cache.md` mentions atlas mip chains and DDS uploads but does not
cross-reference `building-atlas-layout.md`. A developer implementing `TextureCache`
may not know the atlas layout constraints (clamped at 4 mip levels) apply
specifically to the building atlas. A cross-reference should be added.

---

## 8. Cross-Domain Issues

**[INCONSISTENCY] HIGH — IClock injection is specified for AudioSystem, CitySimulation,
UndoSystem/HUD, and SaveSystem, but the canonical IClock definition (header location,
methods) is only specified in `testability-architecture.md`**
Four different subsystems depend on `IClock`, but the interface is only formally
defined in the testing spec. The interface definition should live in
`src/interfaces/IClock.h` with a cross-reference from every spec that uses it.
Currently a developer reading `economy-model.md` or `save-system.md` has no pointer
to the IClock contract.

**[INCONSISTENCY] HIGH — ISimulationRNG injection is required in `CitySimulation`
constructor but the interface definition is only in CLAUDE.md (Notes for AI Assistants)**
`service-coverage.md` references `ISimulationRNG::nextFloat()` but neither the
interface header location (`src/interfaces/ISimulationRNG.h`?) nor its method list
is documented in any architecture spec file. The interface must have its own spec
entry in `architecture/testing/testability-architecture.md` or a new file under
`src/interfaces/`.

**[INCONSISTENCY] HIGH — Frame loop canonical definition exists in both
`simulation-time.md` (8 steps, step-2 CitySimulation::tick) and
`irrlicht-device-lifecycle.md` (11-step render loop starting with beginScene)**
These are two partially overlapping descriptions of the same per-frame loop.
`simulation-time.md` covers the pre-render simulation steps (1–4b + SaveSystem at 3c);
`irrlicht-device-lifecycle.md` covers the render steps (beginScene through drawAll).
Neither file cross-references the other to establish the unified 8-step frame loop.
A single canonical frame loop should be defined in one place (e.g.,
`irrlicht-device-lifecycle.md`) with cross-references from all other specs.

**[GAP] HIGH — `ICitySimulation` interface has no canonical method list spec file**
`ICitySimulation` methods are referenced across at least 8 spec files
(`testability-architecture.md`, `game-over-flow.md`, `zoning-system.md`,
`save-system.md`, `service-coverage.md`, `hud-layout.md`, `traffic-system.md`,
`simulation-time.md`). There is no single file that lists all required methods.
An `ICitySimulation` interface spec (or at minimum a method count comment in the
header similar to `IUIBackend`'s "21 methods" contract) is needed to prevent
implementations from missing methods that are only mentioned in distant spec files.

**[GAP] HIGH — `IRenderer` interface has no canonical method list spec file**
Similar to `ICitySimulation`, `IRenderer` methods are referenced across:
`testability-architecture.md`, `traffic-system.md` (getListenerPosition()),
`hud-layout.md` (setZoneOverlay, setTileHoverHighlight, setTilePlacementPreview,
getTileScreenBounds), `input-arbitration.md` (pickTerrainTile). There is no spec
file that enumerates all `IRenderer` pure-virtual methods with their signatures.
`src/interfaces/IRenderer.h` must be the authoritative source, but the spec files
should cross-reference it.

**[PROBLEM] HIGH — The earthworks path in Phase 10b (`terrain-interaction.md`)
calls `setTileHeight()` on all 4 corners of a placed tile but does NOT address the
chunk-boundary seam problem**
A tile at chunk boundary (e.g. tileX = 63, tileZ = 0 on a 64-tile-wide chunk) has
its northeast corner shared with the adjacent chunk. Calling `setTileHeight` on that
corner updates the shared heightmap, but the adjacent chunk's mesh is not queued for
rebuild. The `procedural-terrain.md` LOD rebuild spec covers general heightmap changes
but does not specifically mandate neighbour-chunk invalidation for the earthworks path.
The Phase 10b spec in `terrain-interaction.md` should explicitly state:
"for each corner vertex modified by `setTileHeight()`, all chunks that share that
vertex must be queued for rebuild."

**[INCONSISTENCY] MEDIUM — `vec3` type: used throughout audio and simulation specs as
`vec3{x, y, z}` but the concrete type (`irr::core::vector3df` alias? custom struct?)
is not specified in any architecture file**
`traffic-system.md`, `audio-system.md`, `spatial-audio.md`, and `terrain-interaction.md`
all use `vec3` as if it is a well-known type. If it is an alias for
`irr::core::vector3df`, including it in `IAudioSystem.h` (which must not include
Irrlicht headers per the architecture) creates a dependency problem. The `vec3`
type alias must be defined in a shared header (`src/interfaces/vec3.h` or similar)
that does NOT include any Irrlicht headers.

**[INCONSISTENCY] MEDIUM — `ServiceBuildingType` enum is defined in `simulation_types.h`
per `service-coverage.md`, but `ZoneType`, `DensityTier`, `SpeedMultiplier`, and
`CityRatingTier` are also described as living in `simulation_types.h` or
`src/interfaces/simulation_types.h` (referenced across multiple specs)**
The spec does not consistently name the header — some files say `simulation_types.h`
and some say `src/interfaces/simulation_types.h`. The fully qualified path must be
uniform across all spec files to prevent header-include divergence.

**[GAP] MEDIUM — No spec for how `GameMode` enum (Sandbox/Scenario) is communicated
from UIManager to CitySimulation**
`game-over-flow.md` explicitly states `CitySimulation` must NOT reference `GameMode`.
`UIManager` checks `GameMode` before calling `transitionToGameOver()`. But the spec
does not define where `GameMode` is stored, how it is set at game start, or which
header it lives in. The `main-menu-new-game-flow.md` spec passes `GameMode::Sandbox`
to `transitionToGameplay()` but this method is not defined in any interface spec.

**[GAP] MEDIUM — `ISaveSystem` interface is referenced but never formally spec'd**
`save-system.md` describes `ISaveSystem::loadMostRecentSave()` and `saveToSlot()` but
there is no spec file that enumerates all `ISaveSystem` pure-virtual methods, their
signatures, or the header location. Like `ICitySimulation`, this interface needs a
canonical method list.

**[GAP] LOW — No spec for the `vec3 distance()` free function used in
`traffic-system.md` (signal cull)**
The 80 m pre-acquisition cull calls `distance(listenerPos, signalPos)`. This free
function must be defined somewhere, but no spec file specifies where. If it is the
Irrlicht `irr::core::vector3df::getDistanceTo()` method, then `CitySimulation` would
need to include Irrlicht headers — which conflicts with the testability architecture
that keeps simulation logic Irrlicht-free. A `distance()` free function taking two
`vec3` arguments must be defined in the `vec3` header or a `simulation_math.h` header
that is Irrlicht-free.

---

*End of AI Town Technical Squad Architecture Review*


---
