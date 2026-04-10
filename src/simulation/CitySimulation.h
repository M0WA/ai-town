#pragma once
#include "ICitySimulation.h"
#include "IClock.h"
#include "IRenderer.h"
#include "IAudioSystem.h"
#include "ISimulationRNG.h"
#include "ITerrainQuery.h"
#include "Economy.h"
#include "Traffic.h"
#include "Zoning.h"
#include "Population.h"
#include "SimTiming.h"

#include <nlohmann/json_fwd.hpp>

#include <string>
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
// NOSONAR cpp:S1448 — thin coordinator; 44 overrides are delegation boilerplate
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
    void tick(float realDeltaSeconds);

    // ---- New-game reset (Phase 11m) ----
    void reset(int64_t startingFunds) override;

    bool applyLoadedJson(const std::string& json) override;

    // ---- Economy / treasury (IEconomyQuery) ----
    float getTreasuryBalance()       const override { return m_economy.getTreasuryBalance(); }
    float getCurrentMonthlyRevenue() const override { return m_economy.getCurrentMonthlyRevenue(); }
    float getOutstandingDebt()       const override { return m_economy.getOutstandingDebt(); }
    float estimateMonthlyUpkeep()    const override { return m_economy.estimateMonthlyUpkeep(m_zoning, m_clock->nowSeconds(), m_timing.getConstructionTimeSeconds()); }
    void  setTaxRate(ZoneType zone, float rate) override { m_economy.setTaxRate(zone, rate); }
    float getTaxRate(ZoneType zone)  const override { return m_economy.getTaxRate(zone); }
    float getTaxRevenue(ZoneType zone) const override { return m_economy.getTaxRevenue(zone); }
    float getWagesCost()             const override { return m_economy.getWagesCost(); }
    float getRoadMaintenanceCost()   const override { return m_economy.getRoadMaintenanceCost(); }
    float getServiceUpkeepCost()     const override { return m_economy.getServiceUpkeepCost(); }
    float getUtilityFeeRevenue()     const override { return m_economy.getUtilityFeeRevenue(); }
    int   getOutstandingBondUses()   const override { return m_economy.getOutstandingBondUses(); }

    // ---- ISimulationState ----
    float getNextUnlockThreshold(Difficulty d) const override { return m_population.getNextUnlockThreshold(d); }
    CityRatingTier getCityRating()           const override { return m_population.getCityRating(); }
    float getZoneDemandFactor(ZoneType zone) const override { return m_traffic.getZoneDemandFactor(zone); }
    float getTrafficDemandFactor(ZoneType zone) const override { return m_traffic.getTrafficDemandFactor(zone); }
    int   getTotalPopulation()               const override { return m_population.getTotalPopulation(); }
    int   getConsecutiveDeficitMonths()      const override { return m_population.getConsecutiveDeficitMonths(); }
    DensityUnlockState getDensityUnlockState() const override { return m_population.getDensityUnlockState(); }
    SimulationTime getSimulationTime()       const override { return m_timing.getSimulationTime(); }
    TimeOfDay getTimeOfDay()                 const override { return m_timing.getTimeOfDay(); }
    int getMapTilesX() const override { return m_mapWidth; }
    int getMapTilesZ() const override { return m_mapHeight; }
    std::vector<AgentState>              getAgentPositions()           const override { return m_traffic.getAgentPositions(); }
    std::vector<IntersectionSignalState> getIntersectionSignalStates() const override { return m_traffic.getIntersectionSignalStates(); }
    std::vector<RoadSegmentSpeed>        getRoadSegmentSpeeds()       const override { return m_traffic.getRoadSegmentSpeeds(m_zoning); }
    std::vector<ServiceCoverageTile>     getServiceCoverage()         const override { return m_zoning.getServiceCoverage(); }

    // ---- IZoningActions ----
    void placeZone(int tileX, int tileZ, ZoneType type, DensityTier tier,
                   int earthworksCostOverride = 0) override;
    void placeRoad(int tileX, int tileZ, int earthworksCostOverride = 0) override;
    void demolishTile(int tileX, int tileZ) override;
    void undoLastAction() override;
    void placeServiceBuilding(int tileX, int tileZ,
                              ServiceBuildingType type,
                              int earthworksCostOverride = 0) override;
    QueryResult queryTile(int tileX, int tileZ) const override;
    bool isWithinRoadRange(int x, int z, DensityTier tier) const override { return m_zoning.isWithinRoadRange(x, z, tier); }
    bool   hasUndoPendingAction()    const override;
    double getUndoExpiryTimeSeconds() const override;

    // ---- Notification queue ----
    bool pollPendingNotification(SimulationNotification& out) override;

    // ---- consumeBudgetTicks ----
    int consumeBudgetTicks() override;

    // ---- Serialization (Phase 11) ----
    std::string serializeToJson() const;
    bool deserializeFromJson(const std::string& json, std::string& errorOut);

    // ---- Test / save seam (not in ICitySimulation) ----
    int getBuildingVariantCounter(int zone, int tier) const;
    void addServiceBuilding(int x, int z, int serviceTypeInt);
    void setModalOpen(bool open);
    void setMapDimensions(int mapWidth, int mapHeight);

#ifdef AITOWN_TESTING_ENABLED
    void testForceUnlockDensityTier(ZoneType zone, DensityTier tier);
#endif

private:
    // ------------------------------------------------------------------
    // Private nested types
    // ------------------------------------------------------------------

    // ScenarioState — placeholder for V1 scenario mode scaffolding.
    struct ScenarioState {
        float       win_condition_progress{0.0f};
        int         elapsed_ticks{0};
        std::string scenario_id;
    };

    // UndoAction — single-level undo record.
    struct UndoAction {
        enum class Type { PlaceZone, PlaceRoad, Demolish };
        Type     actionType{Type::PlaceZone};
        int      tileX{0}, tileZ{0};
        TileData previousState{};
        int64_t  costPaid{0};
    };

    // ------------------------------------------------------------------
    // Injected dependencies
    // ------------------------------------------------------------------
    IRenderer*      m_renderer;
    IAudioSystem*   m_audio;
    ISimulationRNG* m_rng;
    IClock*         m_clock;
    ITerrainQuery*  m_terrain;
    Difficulty      m_difficulty;

    // ------------------------------------------------------------------
    // Map dimensions
    // ------------------------------------------------------------------
    int m_mapWidth{0};
    int m_mapHeight{0};

    // ------------------------------------------------------------------
    // Notification queue (FIFO, polled by UIManager)
    // ------------------------------------------------------------------
    std::queue<SimulationNotification> m_notifications;

    // ------------------------------------------------------------------
    // Undo (single-level, tick-based expiry)
    // ------------------------------------------------------------------
    std::optional<UndoAction> m_pendingUndo;
    double                    m_undoExpiryWallSeconds{0.0};
    int                       m_undoExpiryTickTarget{-1};
    bool                      m_modalOpen{false};

    // ------------------------------------------------------------------
    // Audio debounce
    // ------------------------------------------------------------------
    double m_lastPlacementSoundTime{-1.0};

    // ------------------------------------------------------------------
    // Scenario state (V1 stub)
    // ------------------------------------------------------------------
    ScenarioState m_scenarioState{};

    // ------------------------------------------------------------------
    // Sub-system members
    // ------------------------------------------------------------------
    Economy    m_economy;
    Traffic    m_traffic;
    Zoning     m_zoning;
    Population m_population;
    SimTiming  m_timing;

    // ------------------------------------------------------------------
    // Private helpers
    // ------------------------------------------------------------------
    static float speedValue(SpeedMultiplier s);
    static int64_t startingFundsForDifficulty(Difficulty d);
    static int     bondMaxUsesForDifficulty(Difficulty d);

    void doBudgetTick();
    void recordUndoAction(const UndoAction& action);

    // Phase 11q3 — extracted helpers for placeZone (S3776 + S134)
    bool checkZoneFootprintClear(int tileX, int tileZ, int N) const;
    void applyZoneFootprint(int tileX, int tileZ, ZoneType type, DensityTier tier, int N);

    // Phase 11q3 — extracted helper for demolishTile (S3776)
    void removeTileFromScene(int tileX, int tileZ, bool wasRoad, bool hadServiceBuilding, const TileData& prev);

    // Phase 11q3 — extracted helper for placeServiceBuilding (S3776 + S134)
    bool checkServiceFootprintClear(int tileX, int tileZ, int sN) const;

    // Phase 11q5 — AABB overlap test for service buildings (S3776 + S134)
    bool checkServiceBuildingOverlap(int tileX, int tileZ, int N) const;

    // Phase 11q5 — border-ring terrain flattening helper (S3776 + S134)
    void flattenBorderRing(int tileX, int tileZ, int N, float flatHeight);

    // Phase 11q5 — road-adjacency helper for placeServiceBuilding (S134)
    bool hasRoadAdjacent(int tileX, int tileZ, int sN) const;

    // Phase 11q3 — extracted helpers for deserializeFromJson (S3776)
    bool parseZoningSection(const nlohmann::json& j, std::string& err);
    bool parseTrafficSection(const nlohmann::json& j, std::string& err);
    bool parseEconomySection(const nlohmann::json& j, int64_t& treasury, float taxRates[3], std::string& err);

    // Phase 11q5 — scalar try-catch helper for deserializeFromJson (S3776)
    template<typename T>
    bool parseJsonField(const nlohmann::json& j, const char* key,
                        T& out, std::string& errorOut) const;

    // Phase 11q5 — optional array helpers for deserializeFromJson (S3776)
    bool parseOptionalBoolArray(const nlohmann::json& j, const char* key,
                                bool* out, int maxCount, std::string& errorOut) const;
    bool parseOptionalIntArray(const nlohmann::json& j, const char* key,
                               int* out, int maxCount, std::string& errorOut) const;

    // Phase 11q5 — density unlock section helper for deserializeFromJson (S3776)
    bool parseDensityUnlockSection(const nlohmann::json& j,
                                    DensityUnlockState& out,
                                    std::string& errorOut) const;
};
