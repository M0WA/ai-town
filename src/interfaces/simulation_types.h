#pragma once
#include "audio_types.h"

// Canonical definitions for shared simulation-domain types that appear in
// multiple interface headers. Both ICitySimulation.h and IAudioSystem.h
// may #include this file.
// audio_types.h is included here to provide TimeOfDay (defined there as the
// canonical location) to all code that includes simulation_types.h.

enum class ZoneType {
    Residential,
    Commercial,
    Industrial
};

enum class Difficulty {
    Easy,
    Normal,
    Hard
};

// SpeedMultiplier is the canonical enum.
// SimSpeed is a type alias that resolves the name mismatch between
// ICitySimulation.h (uses SpeedMultiplier) and IAudioSystem.h (uses SimSpeed).
enum class SpeedMultiplier {
    Paused = 0,
    x1     = 1,
    x3     = 2,
    x10    = 3
};

using SimSpeed = SpeedMultiplier;

// Default starting simulation speed — 3× (fast enough for early feedback; not so fast it punishes
// new players). See architecture/game-design/simulation-time.md for the design rationale.
constexpr SpeedMultiplier kDefaultSimSpeed = SpeedMultiplier::x3;

// CityRatingTier — ordered tier enum used as the return type of ICitySimulation::getCityRating().
// Tier transitions (not raw population milestones) drive the stinger_milestone audio event.
// See architecture/game-design/game-progression-modes.md for tier definitions.
enum class CityRatingTier {
    Village,
    Town,
    City,
    Metropolis,
    Megalopolis
};

// DensityTier — density tier for a zoned tile.
// Used by ICitySimulation::placeZone() and QueryResult.
enum class DensityTier {
    Low,
    Medium,
    High
};

// DensityUnlockState — snapshot of all density-unlock counters and flags.
// Referenced by ICitySimulation::getDensityUnlockState().
// Phase 1 stub returns a default-constructed DensityUnlockState{}.
// Phase 3 fills in real implementation.
// Density tiers 0-5 (6 total): one counter + one flag per tier.
// Counter range: 0-2 in returned/persisted state. Value 3 is transient — within a single
// tick's execution the counter briefly reaches 3, triggers the unlock, and resets to 0
// before getDensityUnlockState() returns. The observable range is always [0, 2].
// The unlock fires when the counter would increment to 3: counter reaches 2, next tick above
// threshold triggers the unlock and resets to 0.
struct DensityUnlockState {
    int  consecutive_months_above_threshold[6]{};  // 0-2 range in save state; one counter per density tier
    bool unlock_flags[6]{};                        // true if the corresponding tier is unlocked
};

// SimulationTime — current in-game date returned by ICitySimulation::getSimulationTime().
// Used by HUD resource bar to display month/year.
struct SimulationTime {
    int year{1};   // starts at year 1
    int month{1};  // 1-12
};

// TimeOfDay is defined in audio_types.h (included above) and forwarded here
// for ICitySimulation::getTimeOfDay(). Phase 10 wires CitySimulation's accessor
// to IAudioSystem::setTimeOfDay() transitions.

// NotificationType — category of simulation event notification.
// Used by ICitySimulation::pollPendingNotification() (singular) to let UIManager
// post the appropriate toast via NotificationManager.
enum class NotificationType {
    ForcedLoanIssued,    // mandatory loan; amount = principal, repaymentTicks = loan_repayment_ticks
    BondIssued,          // emergency municipal bond; amount = principal, repaymentTicks = bond_repayment_ticks
    ServiceDegraded,     // service building entered reduced-coverage state; amount unused
    BudgetDeficitWarn,   // city crossed the -25% deficit threshold; amount unused
    PopulationMilestone, // population crossed a milestone (1K/10K/50K/100K/500K); milestoneValue = pop count
    CityRatingTransition // city rating tier changed; milestoneValue = new CityRatingTier as int
                         //   (Village=0, Town=1, City=2, Metropolis=3, Megalopolis=4)
                         //   fires stinger_milestone at tier transitions ONLY; NOT at 100K raw population
};

// SimulationNotification — one queued event from CitySimulation for UIManager to process.
// Dequeued via ICitySimulation::pollPendingNotification() (singular, FIFO, one per call).
struct SimulationNotification {
    NotificationType type{NotificationType::ForcedLoanIssued};
    int    amount{0};            // loan principal for loan types; 0 for others (int: all treasury values are integers; avoids float precision loss for large principals)
    int    repaymentTicks{0};    // repayment period for loan types; 0 for others
    int    milestoneValue{0};    // population count for PopulationMilestone;
                                 // CityRatingTier as int for CityRatingTransition; 0 for others
};

// ServiceCoverage — per-service coverage percentage for a tile.
// A value of -1.0f means N/A (zero buildable tiles in range — do not display a percentage).
struct ServiceCoverage {
    float fire{-1.0f};
    float police{-1.0f};
    float water{-1.0f};
    float power{-1.0f};
};

// ServiceBuildingType — the four placeable service infrastructure buildings.
// Used by ICitySimulation::placeServiceBuilding().
// All four types are mandatory in V1; see architecture/game-design/service-coverage.md.
enum class ServiceBuildingType {
    PowerPlant,
    WaterTower,
    FireStation,
    PoliceStation
};

// QueryResult — per-tile data returned by ICitySimulation::queryTile().
// Consumed by the Query/Inspector Panel (Phase 8).
struct QueryResult {
    int         tileX{};
    int         tileZ{};
    bool        isZoned{false};
    bool        isRoad{false};
    ZoneType    zoneType{ZoneType::Residential};
    DensityTier densityTier{DensityTier::Low};
    int         population{};
    float       desirability{};      // [0, 100]
    float       demandPressurePct{}; // Per-tile UNMET demand percentage: (1.0f - effective_demand_factor) * 100.
                                      // Range [0, 100]: 0 = fully satisfied demand, 100 = zero demand.
                                      // *** INVERSE SEMANTICS vs ICitySimulation::getDemandPressurePct(ZoneType) ***
                                      // getDemandPressurePct(ZoneType) returns EFFECTIVE demand in [0.0, 1.0]
                                      // (1.0 = maximum demand). QueryResult::demandPressurePct is its complement
                                      // multiplied by 100. Formula: queryResult.demandPressurePct =
                                      // (1.0f - tileEffectiveDemandFactor) * 100.0f — NOT getDemandPressurePct() * 100.
                                      // NOT the same as getDemandPressurePct(ZoneType) which returns city-wide aggregate.
    ServiceCoverage coverage;        // per-service; -1.0f = N/A
};
