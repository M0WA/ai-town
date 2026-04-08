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
    CityRatingTransition, // city rating tier changed; milestoneValue = new CityRatingTier as int
                          //   (Village=0, Town=1, City=2, Metropolis=3, Megalopolis=4)
                          //   fires stinger_milestone at tier transitions ONLY; NOT at 100K raw population
    NeighbourCleared,    // same-zone lower-density neighbour auto-demolished during density upgrade
    UpgradeBlocked,      // density upgrade cancelled after 12 deferred retries (CRITICAL toast)
    PlacementBlocked,    // zone/service placement rejected (footprint occupied, OOB, road too far,
                         //   or no adjacent road); tileX/tileZ hold the attempted origin tile
    BuildingAbandoned,   // zone building abandoned — nearest road > 3 tiles away
    BuildingRecovered    // previously-abandoned zone building recovered — road within 3 tiles again
};

// SimulationNotification — one queued event from CitySimulation for UIManager to process.
// Dequeued via ICitySimulation::pollPendingNotification() (singular, FIFO, one per call).
//
// Field usage by NotificationType:
//   ForcedLoanIssued    — loanPrincipal = loan amount; loanRepaymentTicks = repayment period
//   BondIssued          — loanPrincipal = bond amount; loanRepaymentTicks = repayment period
//   ServiceDegraded     — loanPrincipal unused (0);    loanRepaymentTicks unused (0)
//   BudgetDeficitWarn   — loanPrincipal unused (0);    loanRepaymentTicks unused (0)
//   PopulationMilestone — loanPrincipal unused (0);    loanRepaymentTicks unused (0);
//                         milestoneValue = population count at milestone
//   CityRatingTransition— loanPrincipal unused (0);    loanRepaymentTicks unused (0);
//                         milestoneValue = new CityRatingTier cast to int
//   NeighbourCleared    — all fields unused (0)
//   UpgradeBlocked      — all fields unused (0)
//   PlacementBlocked    — all fields unused (0); tileX/tileZ stored separately in notification queue
//   BuildingAbandoned   — all fields unused (0)
//   BuildingRecovered   — all fields unused (0)
struct SimulationNotification {
    NotificationType type{NotificationType::ForcedLoanIssued};

    // Loan principal (currency units, integer to avoid float precision loss for large values).
    // Used by: ForcedLoanIssued (mandatory loan amount), BondIssued (bond amount).
    // Unused (0) for all other notification types.
    int loanPrincipal{0};

    // Loan repayment period in simulation ticks.
    // Used by: ForcedLoanIssued (loan_repayment_ticks), BondIssued (bond_repayment_ticks).
    // Unused (0) for all other notification types.
    int loanRepaymentTicks{0};

    // Population count (PopulationMilestone) or CityRatingTier as int (CityRatingTransition).
    // Unused (0) for all other notification types.
    int milestoneValue{0};
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
// Used by ICitySimulation::placeServiceBuilding() and QueryResult::serviceType.
// None is the sentinel value for non-service-building tiles (used in QueryResult).
// IMPORTANT: None is placed last to preserve the existing ordinals of the four
// placeable types (PowerPlant=0..PoliceStation=3); UIManager casts an int index
// 0-3 directly to ServiceBuildingType — inserting None before them would break that mapping.
// All four non-None types are mandatory in V1; see architecture/game-design/service-coverage.md.
enum class ServiceBuildingType {
    PowerPlant,
    WaterTower,
    FireStation,
    PoliceStation,
    None
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
                                      // *** INVERSE SEMANTICS vs ISimulationState::getZoneDemandFactor(ZoneType) ***
                                      // getZoneDemandFactor(ZoneType) returns EFFECTIVE demand in [0.0, 1.0]
                                      // (1.0 = maximum demand). QueryResult::demandPressurePct is its complement
                                      // multiplied by 100. Formula: queryResult.demandPressurePct =
                                      // (1.0f - tileEffectiveDemandFactor) * 100.0f — NOT getZoneDemandFactor() * 100.
                                      // NOT the same as getZoneDemandFactor(ZoneType) which returns city-wide aggregate.
    ServiceCoverage     coverage;                              // per-service; -1.0f = N/A
    ServiceBuildingType serviceType{ServiceBuildingType::None}; // ServiceBuildingType::None for non-service tiles
    bool                degraded{false};         // true if covering service building is in degraded state
    // Phase 11h: multi-tile footprint fields.
    bool isAbandoned{false};  // true when building is abandoned due to road proximity > 3 tiles
    // true when the zone tile has been placed but the building mesh has not yet spawned
    // (demand below SimulationConstants::construction_delay_demand_threshold).
    // False for road tiles, unzoned tiles, and tiles whose building has already spawned.
    bool underConstruction{false};
    // footprintOriginX/Z: -1,-1 means this tile IS the building origin (or 1×1 building).
    // Any other value means this tile is a non-origin part of a multi-tile footprint —
    // its visual mesh was placed at the origin tile, not here.
    int footprintOriginX{-1};
    int footprintOriginZ{-1};
    int buildingVariantNum{0};  // variant assigned at placeZone() (1–4); 0 = not yet assigned or under construction
};

// -----------------------------------------------------------------------
// Phase 11d — Types for traffic-agent and service-coverage query methods
// added to ICitySimulation (getAgentPositions, getIntersectionSignalStates,
// getRoadSegmentSpeeds, getServiceCoverage).
// -----------------------------------------------------------------------

// AgentHandle — opaque stable ID for a traffic agent, valid for the agent's
// lifetime. Defined once here to avoid ODR violations when both simulation_types.h
// and IRenderer.h appear in the same translation unit.
using AgentHandle = uint32_t;

// SignalPhase — current traffic-signal colour at an intersection tile.
enum class SignalPhase {
    Green,
    Red
};

// AgentState — per-agent snapshot returned by ICitySimulation::getAgentPositions().
// worldX/worldZ hold the sub-tile-interpolated world-space position (metres).
// When non-zero they are used directly by the renderer for smooth movement;
// tileX/tileZ retain the integer tile for audio distance culling.
struct AgentState {
    uint32_t agentId{0};
    int      tileX{0};
    int      tileZ{0};
    float    headingDeg{0.0f};
    ZoneType zone{ZoneType::Residential};
    float    worldX{0.0f};   // sub-tile interpolated world X (m); 0 = use tile centre
    float    worldZ{0.0f};   // sub-tile interpolated world Z (m); 0 = use tile centre
};

// IntersectionSignalState — signal-phase snapshot for one intersection tile.
struct IntersectionSignalState {
    int         tileX{0};
    int         tileZ{0};
    SignalPhase phase{SignalPhase::Green};
};

// RoadSegmentSpeed — fractional speed on one road tile (0.0 = stopped, 1.0 = free-flow).
struct RoadSegmentSpeed {
    int   tileX{0};
    int   tileZ{0};
    float speedFraction{1.0f};
};

// ServiceCoverageTile — coverage state for one tile.
struct ServiceCoverageTile {
    int                 tileX{0};
    int                 tileZ{0};
    ServiceBuildingType coveredBy{ServiceBuildingType::None};
    bool                degraded{false};
};
