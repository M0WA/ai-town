#pragma once
#include "simulation_constants.h"
#include "src/interfaces/simulation_types.h"

#include <array>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declarations for cross-system references
class Economy;
class Traffic;
class IAudioSystem;
class ISimulationRNG;

// TileData — per-tile state for a zoned or road tile.
// Moved from CitySimulation.h (Phase 11q1 decomposition).
struct TileData {
    ZoneType    zone{ZoneType::Residential};
    DensityTier density{DensityTier::Low};
    bool        isZoned{false};
    bool        isRoad{false};
    float       population{0.0f};
    float       desirability{static_cast<float>(SimulationConstants::desirability_base_value)};
    bool        firstDesirabilityTick{true};

    int  footprintOriginX{-1};
    int  footprintOriginZ{-1};
    bool isAbandoned{false};
    bool underConstruction{false};
    int  buildingVariantNum{0};

    bool wasPowered{true};
    bool wasWaterCovered{true};
    bool alertFired{false};
};

// ServiceBuilding — a placed service infrastructure building.
// Moved from CitySimulation.h (Phase 11q1 decomposition).
struct ServiceBuilding {
    int                 x{0}, z{0};
    ServiceBuildingType type{ServiceBuildingType::FireStation};
    bool                degraded{false};
};

// Zoning — sub-system owning all tile/map state for CitySimulation.
// Extracted from CitySimulation as part of Phase 11q1 decomposition.
class Zoning {
public:
    // ---- Public accessors ----
    const TileData*       findTile(int x, int z) const;
    TileData*             findTile(int x, int z);
    int                   roadTileCount() const;
    int                   getBuildingVariantCounter(int zone, int tier) const;
    std::vector<ServiceCoverageTile> getServiceCoverage() const;
    bool                  isWithinRoadRange(int x, int z, DensityTier tier) const;
    QueryResult           queryTile(int x, int z) const;
    bool                  isBuildableTile(int x, int z) const;

    const std::unordered_map<int64_t, TileData>& tiles() const;
    std::unordered_map<int64_t, TileData>&       tilesRef();
    const std::vector<ServiceBuilding>&          serviceBuildings() const;
    std::vector<ServiceBuilding>&                serviceBuildingsRef();

    // Power coverage cache query
    bool isPowerCovered(int x, int z) const;

    // ---- Mutation methods ----
    // These extract the tile-map mutations from CitySimulation's placement methods.
    // IRenderer calls remain in CitySimulation.
    void placeZoneTiles(int x, int z, ZoneType type, DensityTier tier, int earthworksCost);
    void placeRoadTile(int x, int z, int earthworksCost);
    void demolishTiles(int x, int z);
    void placeServiceBuildingRecord(int x, int z, ServiceBuildingType type);
    void addServiceBuilding(int x, int z, int serviceTypeInt);

    // ---- Tick methods ----
    void buildPowerCoverageCache();
    void buildServiceCoverageMap(bool& outHasFireStation, bool& outHasPolice,
                                 bool& outHasWater, bool& outHasPower) const;
    void applyDesirabilityScores(bool hasFireStation, bool hasPolice,
                                 bool hasWater, bool hasPower,
                                 const Economy& economy, IAudioSystem* audio,
                                 std::queue<SimulationNotification>& notifications);
    void doDesirabilityTick(const Economy& economy, const Traffic& traffic,
                            IAudioSystem* audio,
                            std::queue<SimulationNotification>& notifications);
    void doServiceDegradationTick(const Economy& economy, ISimulationRNG& rng,
                                  IAudioSystem* audio,
                                  std::queue<SimulationNotification>& notifications);
    void doProximityTick(std::queue<SimulationNotification>& notifications);

    // ---- Static helpers (public for cross-system use) ----
    static int64_t tileKey(int x, int z);
    static int     footprintSize(DensityTier tier);
    static int     serviceFootprintSize();
    static int     nearestRoadDistance(const std::unordered_map<int64_t, TileData>& tiles,
                                       int tileX, int tileZ, int footprintN);
    static std::string zoneAssetBaseName(ZoneType zone, DensityTier density);
    static int     maxPopulationForTile(ZoneType zone, DensityTier density);

    // Coverage helpers (public for Economy to use)
    float computeRadialCoverage(int tileX, int tileZ, ServiceBuildingType type) const;
    float computeServiceCoverageRadius(ServiceBuildingType type, bool degraded) const;
    float computePowerCoverage(int tileX, int tileZ) const;

    // Tile size constant
    static constexpr float kTileSizeMeters = 10.0f;

    // ---- Fields (public for CitySimulation to access during transition) ----
    std::unordered_map<int64_t, TileData> m_tiles;
    std::vector<ServiceBuilding>           m_serviceBuildings;
    int                                    m_roadTileCount{0};
    std::unordered_set<int64_t>            m_powerCoverageCache;
    std::unordered_map<int64_t, int>       m_upgradeRetryCount;
    std::array<int, 9>                     m_buildingVariantCounters{};
    float                                  m_budgetSurplusPctRef{0.0f};  // cached from Economy for BFS

private:
    static int nearestRoadFromCell(const std::unordered_map<int64_t, TileData>& tiles,
                                   int fx, int fz, int curMin);
    static std::vector<ServiceCoverageTile> collectCoverageTiles(
        const ServiceBuilding& sb, float radius);
    void runPowerBfs(const ServiceBuilding& sb,
                     std::unordered_map<int64_t, int>& bfsDepth,
                     int& maxDepth) const;
    void addRadialFallbackCoverage(const ServiceBuilding& sb, float radiusTiles,
                                   const std::unordered_map<int64_t, int>& bfsDepth);
    float computeNeighborDesirabilityDelta(int x, int z) const;
    bool  computeFirePoliceCoverageGap(int x, int z,
                                       bool hasFireStation, bool hasPolice) const;
    void  updateWaterState(TileData& tile, int x, int z, bool hasWater,
                           bool& anyUncovered, IAudioSystem* audio);
    void  updatePowerState(TileData& tile, int x, int z, bool hasPower,
                           bool& anyUncovered, IAudioSystem* audio);
    void  fireDesirabilityAlert(TileData& tile, int x, int z,
                                bool hasFireStation, bool hasPolice,
                                IAudioSystem* audio);
    void  tryDegradeService(ServiceBuilding& sb, ISimulationRNG& rng,
                            IAudioSystem* audio,
                            std::queue<SimulationNotification>& notifications);

    // Phase 11q3 — extracted per-tile desirability scoring from applyDesirabilityScores (S3776 + S134)
    float computeTileDesirability(TileData& tile, int tileX, int tileZ,
                                 bool hasFireStation, bool hasPolice,
                                 bool hasWaterTower, bool hasPowerPlant,
                                 IAudioSystem* audio);
};
