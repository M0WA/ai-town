#pragma once

// Canonical definitions for shared simulation-domain types that appear in
// multiple interface headers. Both ICitySimulation.h and IAudioSystem.h
// may #include this file.

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
// Counter range: 0-3 (0=none; 1=first month; 2=second month; fires and resets on third month).
// The unlock fires when the counter would increment to 3: counter reaches 2, next tick above threshold
// triggers the unlock and resets to 0. Range is [0, 2] in persisted state (3 is transient).
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

// NotificationType — category of simulation event notification.
// Used by ICitySimulation::pollPendingNotifications() to let UIManager
// post the appropriate toast via NotificationManager.
enum class NotificationType {
    ForcedLoanIssued,    // mandatory loan; amount = principal, repaymentTicks = loan_repayment_ticks
    BondIssued,          // emergency municipal bond; amount = principal, repaymentTicks = bond_repayment_ticks
    ServiceDegraded,     // service building entered reduced-coverage state; amount unused
    BudgetDeficitWarn    // city crossed the -25% deficit threshold; amount unused
};

// SimulationNotification — one queued event from CitySimulation for UIManager to process.
// Dequeued via ICitySimulation::pollPendingNotifications() (FIFO, one per call).
struct SimulationNotification {
    NotificationType type{NotificationType::ForcedLoanIssued};
    float  amount{0.0f};         // loan principal for loan types; 0 for others
    int    repaymentTicks{0};    // repayment period for loan types; 0 for others
};

// ServiceCoverage — per-service coverage percentage for a tile.
// A value of -1.0f means N/A (zero buildable tiles in range — do not display a percentage).
struct ServiceCoverage {
    float fire{-1.0f};
    float police{-1.0f};
    float water{-1.0f};
    float power{-1.0f};
};

// QueryResult — per-tile data returned by ICitySimulation::queryTile().
// Consumed by the Query/Inspector Panel (Phase 8).
struct QueryResult {
    int         tileX{};
    int         tileZ{};
    bool        isZoned{false};
    ZoneType    zoneType{ZoneType::Residential};
    DensityTier densityTier{DensityTier::Low};
    int         population{};
    float       desirability{};      // [0, 100]
    float       demandPressurePct{}; // per-tile unmet demand [0, 100]; NOT getDemandPressurePct (city-wide)
    ServiceCoverage coverage;        // per-service; -1.0f = N/A
};
