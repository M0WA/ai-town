#pragma once
#include "ICitySimulation.h"
#include "IClock.h"
#include "IRenderer.h"
#include "IAudioSystem.h"
#include "ISimulationRNG.h"
#include "ITerrainQuery.h"
#include "simulation_constants.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <queue>
#include <optional>

// CitySimulation — concrete implementation of ICitySimulation.
// Full V1 simulation engine: economy, traffic, zoning, population, service coverage, undo.
//
// All six constructor parameters are mandatory; pass &realSystem in production and
// test doubles in unit tests. Never pass nullptr for m_terrain — earthworks cost
// silently returns 0 for all tiles (breaks slope-cost tests).
//
// Default starting speed is x3 (kDefaultSimSpeed), NOT x1. Verified by:
//   StartingFunds_Easy_1M, StartingFunds_Normal_500K, StartingFunds_Hard_200K
//   (all pause the sim before querying, confirming x3 at construction).
//
// Source: src/simulation/CitySimulation.h
// Implementation: src/simulation/CitySimulation.cpp

class CitySimulation : public ICitySimulation {
public:
    CitySimulation(IRenderer*      renderer,
                   IAudioSystem*   audio,
                   ISimulationRNG* rng,
                   IClock*         clock,
                   ITerrainQuery*  terrain,
                   Difficulty      difficulty);
    ~CitySimulation() override = default;

    // Non-copyable, non-movable (owns simulation state)
    CitySimulation(const CitySimulation&)            = delete;
    CitySimulation& operator=(const CitySimulation&) = delete;

    // ---- ISimulationPauser (inherited via ICitySimulation) ----
    void setPaused(bool paused) override;

    // ---- Speed control ----
    void           setSpeed(SpeedMultiplier speed) override;
    bool           isPaused()          const override;
    SpeedMultiplier getSpeedMultiplier() const override;

    // ---- Main simulation step ----
    // Called once per frame from main.cpp with real elapsed seconds (never pre-multiplied).
    // Accumulates sim seconds and fires budget ticks when >= SECONDS_PER_BUDGET_TICK.
    void tick(float realDeltaSeconds);

    // ---- Economy / treasury ----
    float getTreasuryBalance()       const override;
    float getCurrentMonthlyRevenue() const override;
    float getOutstandingDebt()       const override;
    float estimateMonthlyUpkeep()    const override;
    float getNextUnlockThreshold(Difficulty d) const override;

    // ---- City rating ----
    CityRatingTier getCityRating() const override;

    // ---- Demand ----
    float getDemandPressurePct(ZoneType zone) const override;
    float getTrafficDemandFactor(ZoneType zone) const override;

    // ---- Population ----
    int getTotalPopulation() const override;

    // ---- Undo state ----
    bool   hasUndoPendingAction()    const override;
    double getUndoExpiryTimeSeconds() const override;

    // ---- Game-over ----
    int getConsecutiveDeficitMonths() const override;

    // ---- Density-unlock ----
    DensityUnlockState getDensityUnlockState() const override;

    // ---- Simulation time ----
    SimulationTime getSimulationTime() const override;

    // ---- Notification queue ----
    bool pollPendingNotification(SimulationNotification& out) override;

    // ---- Tax rates ----
    void  setTaxRate(ZoneType zone, float rate) override;
    float getTaxRate(ZoneType zone) const override;

    // ---- Budget line items ----
    float getTaxRevenue(ZoneType zone)   const override;
    float getWagesCost()                 const override;
    float getRoadMaintenanceCost()       const override;
    float getServiceUpkeepCost()         const override;
    float getUtilityFeeRevenue()         const override;

    // ---- Zone / road actions ----
    void placeZone(int tileX, int tileZ, ZoneType type, DensityTier tier,
                   int earthworksCostOverride = 0) override;
    void placeRoad(int tileX, int tileZ, int earthworksCostOverride = 0) override;
    void demolishTile(int tileX, int tileZ) override;
    void undoLastAction() override;
    void placeServiceBuilding(int tileX, int tileZ,
                              ServiceBuildingType type,
                              int earthworksCostOverride = 0) override;

    // ---- Per-tile query ----
    QueryResult queryTile(int tileX, int tileZ) const override;

    // ---- Bond use count ----
    int getOutstandingBondUses() const override;

    // ---- Time of day ----
    TimeOfDay getTimeOfDay() const override;

    // ---- Test / internal API (not in ICitySimulation) ----
    // addServiceBuilding: inject a service building directly for unit tests.
    // serviceTypeInt: 0=FireStation, 1=PoliceStation, 2=WaterTower, 3=PowerPlant
    // Tests downcast ICitySimulation* to CitySimulation* to reach this method.
    // Not virtual — test-only seam; never called from production paths.
    void addServiceBuilding(int x, int z, int serviceTypeInt);

    // setModalOpen: allow tests to simulate a blocking modal dialog being active
    // so undoLastAction() no-ops as specified in architecture/game-design/undo-system.md.
    // Tests downcast ICitySimulation* to CitySimulation* to reach this method.
    void setModalOpen(bool open);

#ifdef AITOWN_TESTING_ENABLED
    // testForceUnlockDensityTier — test-friend method for Phase 10 unit tests.
    //
    // Directly sets the density unlock flag for the given (ZoneType, DensityTier) pair,
    // bypassing the 3-consecutive-month revenue-streak counter. This allows tests to
    // drive doDensityUnlockTick() into the upgrade-wave state without running multiple
    // budget ticks above the revenue threshold.
    //
    // Index mapping (matches doDensityUnlockTick tier numbering):
    //   Residential/Medium = 0, Commercial/Medium = 1, Industrial/Medium = 2,
    //   Residential/High   = 3, Commercial/High   = 4, Industrial/High   = 5.
    //
    // DensityTier::Low has no unlock gate (always available) and is ignored.
    //
    // This method is compiled ONLY when AITOWN_TESTING_ENABLED=1 is defined.
    // It MUST NOT appear in production builds (aitown target never defines this macro).
    // (ref: implementation/phase-10.md — §testForceUnlockDensityTier decision 2026-03-04)
    void testForceUnlockDensityTier(ZoneType zone, DensityTier tier);
#endif  // AITOWN_TESTING_ENABLED

private:
    // ------------------------------------------------------------------
    // Private nested types
    // ------------------------------------------------------------------
    enum class ServiceType { FireStation, PoliceStation, WaterTower, PowerPlant };

    struct TileData {
        ZoneType    zone{ZoneType::Residential};
        DensityTier density{DensityTier::Low};
        bool        isZoned{false};
        bool        isRoad{false};
        float       population{0.0f};    // actual pop (float for smooth growth)
        float       desirability{static_cast<float>(SimulationConstants::desirability_base_value)};
        bool        firstDesirabilityTick{true};  // grace: skip service penalty on first tick

        // Phase 10 per-tile audio transition flags.
        // Each flag fires its corresponding SFX exactly once per coverage-loss event
        // (not once per tick while uncovered) and resets silently when coverage is restored.
        //
        // wasPowered: true when this tile was power-covered on the previous tick.
        //   CitySimulation::tick() fires SFX_POWER_OUT when wasPowered==true and the
        //   current BFS pass finds the tile unpowered; then sets wasPowered=false.
        //   Restored to true (no SFX) when power coverage returns.
        //   Initialized true so that the very first brownout on a powered tile is audible.
        bool wasPowered{true};

        // wasWaterCovered: true when this tile was water-covered on the previous tick.
        //   CitySimulation::tick() fires SFX_WATER_OUT when wasWaterCovered==true and
        //   the current water-coverage pass finds the tile uncovered; then sets
        //   wasWaterCovered=false. Restored to true (no SFX) when coverage returns.
        //   Initialized true so that the first water-loss event on a covered tile is audible.
        bool wasWaterCovered{true};

        // alertFired: true while this tile is in an active service-alert episode
        //   (desirability <= SimulationConstants::service_alert_desirability_threshold).
        //   CitySimulation::tick() fires SFX_FIRE_ALERT or SFX_POLICE_ALERT (Fire takes
        //   priority when both stations cover the tile) when alertFired==false and
        //   desirability drops to or below the threshold; then sets alertFired=true.
        //   Reset to false when desirability recovers above the threshold, allowing
        //   the alert to re-fire in a future crisis episode.
        //   Must be serialised in the save file (Phase 11) to prevent spurious re-fire on load.
        bool alertFired{false};
    };

    struct LoanEntry {
        int64_t principal{0};           // original principal
        int64_t remainingPrincipal{0};  // decrements each repayment tick
        int     ticksRemaining{0};      // countdown to payoff
        bool    isBond{false};          // true if Emergency Municipal Bond
    };

    struct ServiceBuilding {
        int         x{0}, z{0};
        ServiceType type{ServiceType::FireStation};
        bool        degraded{false};    // true if in reduced-coverage state
    };

    struct UndoAction {
        enum class Type { PlaceZone, PlaceRoad, Demolish };
        Type     actionType{Type::PlaceZone};
        int      tileX{0}, tileZ{0};
        TileData previousState{};       // tile state before action
        int64_t  costPaid{0};           // treasury deduction to reverse on undo
    };

    // TrafficSignal — Phase 10 data structure for sfx_intersection_tick wiring.
    // Maintained by placeRoad() (add signal at any road tile adjacent to 2+ roads)
    // and demolishTile() (remove signal when road tile is demolished).
    //
    // A signal "fires" every phaseSeconds real-time seconds. Each fire represents
    // one green→red or red→green phase change and triggers sfx_intersection_tick
    // (pre-culled at > traffic_signal_cull_distance_meters from listener).
    //
    // phaseTimer: elapsed real-time seconds since last phase change.
    //             Initialised to a random offset in [0, phaseSeconds) to prevent
    //             all signals firing simultaneously on first frame.
    struct TrafficSignal {
        int   tileX{0};
        int   tileZ{0};
        float phaseTimer{0.0f};   // elapsed seconds since last phase change
        float phaseSeconds{SimulationConstants::traffic_signal_phase_seconds};
    };

    // ------------------------------------------------------------------
    // Injected dependencies
    // ------------------------------------------------------------------
    IRenderer*      m_renderer;   // not used in Phase 6; forward ref for Phase 9
    IAudioSystem*   m_audio;
    ISimulationRNG* m_rng;
    IClock*         m_clock;
    ITerrainQuery*  m_terrain;
    Difficulty      m_difficulty;

    // ------------------------------------------------------------------
    // Time tracking
    // ------------------------------------------------------------------
    float           m_accumulatedSimSeconds{0.0f};   // sub-tick accumulator
    double          m_constructionTimeSeconds{0.0};  // clock at construction (grace period base)
    int             m_totalTicks{0};                 // budget ticks fired so far
    int             m_month{1};                      // 1–12
    int             m_year{1};
    SpeedMultiplier m_speed{kDefaultSimSpeed};       // starts at x3

    // In-game hour tracking (for getTimeOfDay)
    float     m_hoursAccumulator{0.0f};    // in-game hours accumulated since last TimeOfDay update
    TimeOfDay m_timeOfDay{TimeOfDay::DAY};

    // ------------------------------------------------------------------
    // Economy / treasury
    // ------------------------------------------------------------------
    int64_t             m_treasury{0};
    float               m_taxRates[3]{0.05f, 0.05f, 0.05f};  // indexed by (int)ZoneType
    std::vector<LoanEntry> m_loans;
    int                 m_outstandingBondUses{0};
    int                 m_loanCooldownTicks{0};    // ticks until next forced loan allowed
    bool                m_firstRevenueTicked{false}; // gate: first non-zero revenue tick seen
    float               m_budgetSurplusPct{0.0f};   // from last tick

    // Last-tick budget line items (returned by budget-panel accessors)
    float m_lastMonthTaxRevenue[3]{};
    float m_lastMonthWagesCost{0.0f};
    float m_lastMonthRoadMaintenanceCost{0.0f};
    float m_lastMonthServiceUpkeepCost{0.0f};
    float m_lastMonthUtilityFeeRevenue{0.0f};
    float m_currentMonthlyRevenue{0.0f};

    // Budget-deficit warning dedup (resets when deficit clears -25%)
    bool m_budgetWarnFired{false};

    // ------------------------------------------------------------------
    // Map state
    // ------------------------------------------------------------------
    std::unordered_map<int64_t, TileData> m_tiles;
    std::vector<ServiceBuilding>           m_serviceBuildings;
    int                                    m_roadTileCount{0};

    // Phase 10: traffic signals — one entry per intersection road tile.
    // Populated by placeRoad() (when the new road tile is adjacent to 2+ existing roads)
    // and pruned by demolishTile() (remove entry when a road tile is demolished).
    // Each signal's phaseTimer is advanced by doTrafficSignalTick(realDeltaSeconds).
    std::vector<TrafficSignal> m_trafficSignals;

    // ------------------------------------------------------------------
    // Traffic — rolling windows (circular buffers, initialized to null_path default)
    // ------------------------------------------------------------------
    float m_trafficWindowR[SimulationConstants::traffic_rolling_window_r_c]{};
    float m_trafficWindowC[SimulationConstants::traffic_rolling_window_r_c]{};
    float m_trafficWindowI[SimulationConstants::traffic_rolling_window_i]{};
    int   m_trafficWindowIdxRC{0};   // circular write index for R/C
    int   m_trafficWindowIdxI{0};    // circular write index for I
    // Cached smoothstep results (updated each tick by computeTrafficDemand)
    float m_trafficDemandFactorR{SimulationConstants::null_path_demand_default};
    float m_trafficDemandFactorC{SimulationConstants::null_path_demand_default};
    float m_trafficDemandFactorI{SimulationConstants::null_path_demand_default};

    // Cached road speed fraction (updated each tick by computeTrafficDemand; used in congestion penalty)
    float m_roadSpeedFraction{1.0f};

    // Cached effective demand (post-floor, post-bootstrap, city-wide aggregate)
    float m_demandPressurePct[3]{0.0f, 0.0f, 0.0f};  // indexed by (int)ZoneType

    // ------------------------------------------------------------------
    // Population & city rating
    // ------------------------------------------------------------------
    int            m_totalPopulation{0};
    int            m_prevPopulation{0};    // Phase 10: snapshot from end of previous tick
                                           // Used to detect net-positive pop delta for GROWTH tier.
                                           // Updated at the END of doPopulationTick() each budget tick.
    CityRatingTier m_cityRating{CityRatingTier::Village};
    bool           m_milestoneFired[5]{};  // 1K/10K/50K/100K/500K (index 0–4)

    // ------------------------------------------------------------------
    // Game-over tracking
    // ------------------------------------------------------------------
    int  m_consecutiveDeficitMonths{0};
    bool m_month1AutoSlowed{false};  // resets when streak breaks

    // ------------------------------------------------------------------
    // Adaptive music intensity (Phase 10)
    // ------------------------------------------------------------------
    // Tracks the MusicIntensity tier that was last sent to IAudioSystem so
    // that setMusicIntensity() is only called when the tier actually changes
    // (edge-detect policy — avoids hammering the audio system every tick).
    // Initialised to CALM: construction sends no setMusicIntensity call because
    // the audio system already defaults to CALM on transitionToGameplay().
    MusicIntensity m_lastSentMusicIntensity{MusicIntensity::CALM};

    // ------------------------------------------------------------------
    // Density unlock
    // ------------------------------------------------------------------
    DensityUnlockState m_densityUnlockState{};

    // ------------------------------------------------------------------
    // Notification queue (FIFO, polled by UIManager)
    // ------------------------------------------------------------------
    std::queue<SimulationNotification> m_notifications;

    // ------------------------------------------------------------------
    // Undo (single-level, tick-based expiry)
    // ------------------------------------------------------------------
    std::optional<UndoAction> m_pendingUndo;
    double                    m_undoExpiryWallSeconds{0.0}; // absolute clock time; for display
    int                       m_undoExpiryTickTarget{-1};   // budget tick at which undo expires
    bool                      m_modalOpen{false};

    // ------------------------------------------------------------------
    // Private helpers
    // ------------------------------------------------------------------

    // Speed value as real multiplier (Paused=0, x1=1, x3=3, x10=10)
    static float speedValue(SpeedMultiplier s);

    // Tile map
    static int64_t tileKey(int x, int z);
    TileData*       findTile(int x, int z);
    const TileData* findTile(int x, int z) const;

    // Budget tick sub-steps (called in order from doBudgetTick)
    void doBudgetTick();
    void computeTrafficDemand();          // update rolling windows and traffic demand factors
    void computeEffectiveDemand();        // combine traffic + capacity-ratio + bootstrap + floor
    void doServiceDegradationTick();      // stochastic degradation / recovery of service buildings
    void doDesirabilityTick();            // adjacency & service-coverage desirability updates
    void doPopulationTick();              // grow/decay tile populations; fire milestone notifications
    void doDensityUnlockTick();           // 3-month threshold check + density upgrade wave
    void doEconomyTick();                 // revenue, expenses, loan repayment, deficit checks
    void doGameOverTick();                // deficit streak, auto-slow, game-over counter
    void checkCityRatingTransition();     // tier change notification

    // Phase 10: advance traffic signal timers and fire sfx_intersection_tick.
    // Called once per tick() with real delta seconds (NOT sim-speed-scaled).
    // Signals are independent of budget ticks — they toggle every
    // SimulationConstants::traffic_signal_phase_seconds real-time seconds.
    // Pre-cull: skips playPositionalSound if listener distance > 80 m.
    void doTrafficSignalTick(float realDeltaSeconds);

    // Economy helpers
    int64_t computeTaxRevenue(ZoneType zone) const;
    int64_t computeWagesCost(int64_t totalCIRevenue) const;
    int64_t computeServiceUpkeepCost() const;
    int64_t computeRoadMaintenanceCost() const;
    int64_t computeUtilityFeeRevenue() const;
    void    processLoanRepayments(int64_t& treasury);
    void    checkAndIssueForcedLoan();
    float   getDensityUnlockScale() const;
    float   computeBudgetSurplusPct(int64_t revenue, int64_t expenses) const;

    // Population helpers
    static int   maxPopulationForTile(ZoneType zone, DensityTier density);

    // Phase 10: map (ZoneType, DensityTier) to the _01 asset base name for
    // IRenderer::placeBuildingMesh().  Round-robin variant cycling is Phase 11.
    // Returns empty string on unknown inputs (renderer no-ops on empty baseName).
    static std::string zoneAssetBaseName(ZoneType zone, DensityTier density);
    float        effectiveDemandForTile(const TileData& tile) const;

    // Service coverage helpers (tile-unit coordinates)
    // tile_size_m = 10.0f (implicit: coverage radii in constants are in metres)
    static constexpr float kTileSizeMeters = 10.0f;
    float computeServiceCoverageRadius(ServiceType type, bool degraded) const;
    bool  isBuildableTile(int x, int z) const;
    float computeRadialCoverage(int tileX, int tileZ, ServiceType type) const;
    float computePowerCoverage(int tileX, int tileZ) const;

    // Traffic helpers
    static float smoothstep(float t);                         // [0,1] S-curve
    static float travelTimeDemand(float avgTravelTimeSec,
                                  float fullTime, float zeroTime);

    // Undo helpers
    void recordUndoAction(const UndoAction& action);
};
