# Game Progression & Modes

- **Sandbox mode** (ships first): Configurable starting funds per difficulty tier (Easy=$1M / Normal=$500K / Hard=$200K); disasters: post-V1 scope (not available in V1 Sandbox); milestone notifications at 1K / 10K / 50K / 100K / 500K population
- **City Rating tiers** (post-density-unlock progression layer for Sandbox): After all density tiers are unlocked, the player's city advances through City Rating tiers based on population. Tiers are displayed prominently in the HUD and serve as visible long-term goals in open-ended Sandbox play:

| City Rating | Population threshold |
|---|---|
| Village | 0–999 |
| Town | 1,000–9,999 |
| City | 10,000–49,999 |
| Metropolis | 50,000–499,999 |
| Megalopolis | 500,000+ |

  Each tier transition triggers a milestone toast and a brief fanfare stinger. Rating is displayed in the resource bar alongside population count. This replaces the need for a formal win condition in Sandbox mode.

  **Population milestone toasts and City Rating stingers — co-fire behavior (intentional)**: Population milestone notifications (toasts at 1K / 10K / 50K / 100K / 500K residents) and City Rating stinger audio (fires on City Rating transitions at 1K / 10K / 50K / 500K) fire simultaneously for their shared thresholds. This is intentional — the toast provides visual feedback and the stinger provides audio punctuation. Note that 100K is a population milestone (toast only) but NOT a City Rating transition threshold — no stinger fires at exactly 100K population. The stinger fires at RATING TRANSITIONS only (Village→Town at 1K, Town→City at 10K, City→Metropolis at 50K, Metropolis→Megalopolis at 500K), not at raw population milestones that do not coincide with a transition. The `StingerMilestone_OnlyAtCityRatingTransition_NotRawPopulation` test verifies that re-crossing a population threshold after a Rating has already advanced does not re-fire the stinger.

- **Scenario mode** (post-launch): Fixed starting conditions, explicit win conditions (e.g. reach 50,000 population within 10 in-game years with budget surplus), 3–5 launch scenarios. **Win condition definition** for budget-surplus clauses: "budget surplus" at win check time means `budget_surplus_pct > 0` on the budget tick that crosses the win threshold (point-in-time check, not average over multiple ticks). **Win screen flow**: When the win condition is met, the simulation pauses automatically and displays a non-skippable **win modal** (title: "[Scenario Name] — Objective Complete!", body: final city stats — population, monthly revenue, elapsed in-game years; buttons: "Continue Building" (resumes simulation in free-play mode — win condition is cleared, game-over remains active) / "Return to Main Menu"). **HUD goal tracker**: A compact goal-progress bar is displayed in the resource bar area for the duration of a Scenario mode session, showing: current value vs. win-condition target (e.g., "Population: 34,250 / 50,000") and elapsed vs. maximum in-game time (e.g., "Year 7 / 10"). The goal tracker is absent in Sandbox mode. **Win condition failure (timeout)**: If the time limit expires without meeting the win condition, the game transitions to a loss modal (title: "[Scenario Name] — Objective Failed", same buttons as win modal). This uses the existing `ModalDialog` system.
