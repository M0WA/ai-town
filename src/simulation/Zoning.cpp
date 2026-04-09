// Zoning.cpp — tile/map sub-system for CitySimulation.
// Extracted verbatim from CitySimulation.cpp (Phase 11q1 decomposition).

#include "Zoning.h"
#include "Economy.h"
#include "Traffic.h"
#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/ISimulationRNG.h"
#include "src/interfaces/sound_ids.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <numeric>
#include <queue>

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

/*static*/ int64_t Zoning::tileKey(int x, int z) {
    return (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(z);
}

/*static*/ int Zoning::footprintSize(DensityTier tier) {
    switch (tier) {
        case DensityTier::Low:    return 1;
        case DensityTier::Medium: return 2;
        case DensityTier::High:   return 3;
    }
    return 1;
}

/*static*/ int Zoning::serviceFootprintSize() { return 2; }

/*static*/ int Zoning::nearestRoadFromCell(
    const std::unordered_map<int64_t, TileData>& tiles,
    int fx, int fz, int curMin)
{
    int minDist = curMin;
    for (int rx = -3; rx <= 3; ++rx) {
        for (int rz = -3; rz <= 3; ++rz) {
            int chebyshev = std::max(std::abs(rx), std::abs(rz));
            if (chebyshev == 0 || chebyshev >= minDist) continue;
            int64_t key = tileKey(fx + rx, fz + rz);
            auto it = tiles.find(key);
            if (it != tiles.end() && it->second.isRoad) {
                minDist = chebyshev;
            }
        }
    }
    return minDist;
}

/*static*/ int Zoning::nearestRoadDistance(
    const std::unordered_map<int64_t, TileData>& tiles,
    int tileX, int tileZ, int footprintN)
{
    int minDist = INT_MAX;
    for (int dx = 0; dx < footprintN; ++dx) {
        for (int dz = 0; dz < footprintN; ++dz) {
            minDist = nearestRoadFromCell(tiles, tileX + dx, tileZ + dz, minDist);
        }
    }
    return minDist;
}

/*static*/ std::string Zoning::zoneAssetBaseName(ZoneType zone, DensityTier density) {
    static constexpr const char* kZonePrefix[3][3] = {
        { "res_low",  "res_med",  "res_high"  },
        { "com_low",  "com_med",  "com_high"  },
        { "ind_low",  "ind_med",  "ind_high"  },
    };
    int zi = static_cast<int>(zone);
    int di = static_cast<int>(density);
    if (zi < 0 || zi > 2 || di < 0 || di > 2) return {};
    return std::string(kZonePrefix[zi][di]) + "_01";
}

/*static*/ int Zoning::maxPopulationForTile(ZoneType zone, DensityTier density) {
    switch (zone) {
        case ZoneType::Residential:
            switch (density) {
                case DensityTier::Low:    return SimulationConstants::max_pop_residential_low;
                case DensityTier::Medium: return SimulationConstants::max_pop_residential_medium;
                case DensityTier::High:   return SimulationConstants::max_pop_residential_high;
            }
            break;
        case ZoneType::Commercial:
            switch (density) {
                case DensityTier::Low:    return SimulationConstants::max_pop_commercial_low;
                case DensityTier::Medium: return SimulationConstants::max_pop_commercial_medium;
                case DensityTier::High:   return SimulationConstants::max_pop_commercial_high;
            }
            break;
        case ZoneType::Industrial:
            switch (density) {
                case DensityTier::Low:    return SimulationConstants::max_pop_industrial_low;
                case DensityTier::Medium: return SimulationConstants::max_pop_industrial_medium;
                case DensityTier::High:   return SimulationConstants::max_pop_industrial_high;
            }
            break;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

const TileData* Zoning::findTile(int x, int z) const {
    auto it = m_tiles.find(tileKey(x, z));
    return (it != m_tiles.end()) ? &it->second : nullptr;
}

TileData* Zoning::findTile(int x, int z) {
    auto it = m_tiles.find(tileKey(x, z));
    return (it != m_tiles.end()) ? &it->second : nullptr;
}

int Zoning::roadTileCount() const {
    return m_roadTileCount;
}

int Zoning::getBuildingVariantCounter(int zone, int tier) const {
    int idx = zone * 3 + tier;
    if (idx < 0 || idx >= static_cast<int>(m_buildingVariantCounters.size())) return 0;
    return m_buildingVariantCounters[idx];
}

const std::unordered_map<int64_t, TileData>& Zoning::tiles() const {
    return m_tiles;
}

std::unordered_map<int64_t, TileData>& Zoning::tilesRef() {
    return m_tiles;
}

const std::vector<ServiceBuilding>& Zoning::serviceBuildings() const {
    return m_serviceBuildings;
}

std::vector<ServiceBuilding>& Zoning::serviceBuildingsRef() {
    return m_serviceBuildings;
}

bool Zoning::isPowerCovered(int x, int z) const {
    return m_powerCoverageCache.count(tileKey(x, z)) > 0;
}

bool Zoning::isWithinRoadRange(int x, int z, DensityTier tier) const {
    return nearestRoadDistance(m_tiles, x, z, footprintSize(tier)) <= 3;
}

bool Zoning::isBuildableTile(int x, int z) const {
    const TileData* tile = findTile(x, z);
    return (tile != nullptr && (tile->isZoned || tile->isRoad));
}

// ---------------------------------------------------------------------------
// queryTile
// ---------------------------------------------------------------------------

QueryResult Zoning::queryTile(int tileX, int tileZ) const {
    QueryResult result;
    result.tileX  = tileX;
    result.tileZ  = tileZ;
    result.isZoned = false;

    const TileData* tile = findTile(tileX, tileZ);
    if (!tile) {
        const int sN = serviceFootprintSize();
        for (const ServiceBuilding& sb : m_serviceBuildings) {
            if (tileX >= sb.x && tileX < sb.x + sN &&
                tileZ >= sb.z && tileZ < sb.z + sN) {
                result.serviceType = sb.type;
                result.degraded = sb.degraded;
                return result;
            }
        }
        return result;
    }

    if (tile->isRoad) {
        result.isRoad = true;
        return result;
    }

    if (!tile->isZoned) {
        return result;
    }

    result.isZoned     = true;
    result.zoneType    = tile->zone;
    result.densityTier = tile->density;
    result.population  = static_cast<int>(tile->population);
    result.desirability = tile->desirability;

    // Per-tile effective demand factor — uses the cached demand from Traffic
    // Note: effectiveDemandForTile is inlined here since Zoning doesn't own Traffic data
    // The caller (CitySimulation) will handle this. For now, return 0.
    // Actually, this method is called through CitySimulation which will delegate properly.
    // We set demandPressurePct to 0 here; CitySimulation::queryTile overrides if needed.
    result.demandPressurePct = 0.0f;

    bool hasFireStation = false, hasPolice = false, hasWater = false, hasPower = false;
    for (const ServiceBuilding& sb : m_serviceBuildings) {
        switch (sb.type) {
            case ServiceBuildingType::FireStation:   hasFireStation = true; break;
            case ServiceBuildingType::PoliceStation: hasPolice      = true; break;
            case ServiceBuildingType::WaterTower:    hasWater       = true; break;
            case ServiceBuildingType::PowerPlant:    hasPower       = true; break;
            default: break;
        }
    }

    result.coverage.fire   = hasFireStation ? computeRadialCoverage(tileX, tileZ, ServiceBuildingType::FireStation)   : -1.0f;
    result.coverage.police = hasPolice      ? computeRadialCoverage(tileX, tileZ, ServiceBuildingType::PoliceStation) : -1.0f;
    result.coverage.water  = hasWater       ? computeRadialCoverage(tileX, tileZ, ServiceBuildingType::WaterTower)    : -1.0f;
    result.coverage.power  = hasPower       ? computePowerCoverage(tileX, tileZ)                                     : -1.0f;

    result.isAbandoned = tile->isAbandoned;
    result.underConstruction = tile->underConstruction;
    result.footprintOriginX = tile->footprintOriginX;
    result.footprintOriginZ = tile->footprintOriginZ;
    result.buildingVariantNum = tile->buildingVariantNum;

    return result;
}

// ---------------------------------------------------------------------------
// getServiceCoverage
// ---------------------------------------------------------------------------

/*static*/ std::vector<ServiceCoverageTile> Zoning::collectCoverageTiles(
    const ServiceBuilding& sb, float radius)
{
    std::vector<ServiceCoverageTile> result;
    int radiusTiles = static_cast<int>(radius / kTileSizeMeters) + 1;

    for (int dz = -radiusTiles; dz <= radiusTiles; ++dz) {
        for (int dx = -radiusTiles; dx <= radiusTiles; ++dx) {
            float dist = std::sqrt(static_cast<float>(dx*dx + dz*dz)) * kTileSizeMeters;
            if (dist > radius) continue;
            ServiceCoverageTile sct;
            sct.tileX     = sb.x + dx;
            sct.tileZ     = sb.z + dz;
            sct.coveredBy = sb.type;
            sct.degraded  = sb.degraded;
            result.push_back(sct);
        }
    }
    return result;
}

std::vector<ServiceCoverageTile> Zoning::getServiceCoverage() const {
    std::vector<ServiceCoverageTile> coverage;

    for (const ServiceBuilding& sb : m_serviceBuildings) {
        if (sb.type == ServiceBuildingType::None) continue;

        float radius = computeServiceCoverageRadius(sb.type, sb.degraded);
        auto tiles = collectCoverageTiles(sb, radius);
        coverage.insert(coverage.end(), tiles.begin(), tiles.end());
    }
    return coverage;
}

// ---------------------------------------------------------------------------
// Service coverage helpers
// ---------------------------------------------------------------------------

float Zoning::computeServiceCoverageRadius(ServiceBuildingType type, bool degraded) const {
    float radius;
    switch (type) {
        case ServiceBuildingType::FireStation:
            radius = static_cast<float>(SimulationConstants::fire_station_coverage_radius_m);
            break;
        case ServiceBuildingType::PoliceStation:
            radius = static_cast<float>(SimulationConstants::police_station_coverage_radius_m);
            break;
        case ServiceBuildingType::WaterTower:
            radius = static_cast<float>(SimulationConstants::water_tower_coverage_radius_m);
            break;
        case ServiceBuildingType::PowerPlant:
            radius = static_cast<float>(SimulationConstants::fire_station_coverage_radius_m);
            break;
        default:
            radius = 0.0f;
            break;
    }
    if (degraded) radius *= 0.5f;
    return radius;
}

float Zoning::computeRadialCoverage(int tileX, int tileZ, ServiceBuildingType type) const {
    for (const ServiceBuilding& sb : m_serviceBuildings) {
        if (sb.type != type) continue;
        float radius = computeServiceCoverageRadius(type, sb.degraded);
        float radiusTiles = radius / kTileSizeMeters;
        float dx = static_cast<float>(sb.x - tileX);
        float dz = static_cast<float>(sb.z - tileZ);
        float dist = std::sqrt(dx * dx + dz * dz);
        if (dist <= radiusTiles) {
            return 1.0f;
        }
    }
    return 0.0f;
}

void Zoning::runPowerBfs(const ServiceBuilding& sb,
                         std::unordered_map<int64_t, int>& bfsDepth,
                         int& maxDepth) const {
    const int dx4[] = {0, 0, -1, 1};
    const int dz4[] = {-1, 1, 0, 0};

    const int sN = serviceFootprintSize();
    for (int fdx = 0; fdx < sN; ++fdx) {
        for (int fdz = 0; fdz < sN; ++fdz) {
            int64_t fpKey = tileKey(sb.x + fdx, sb.z + fdz);
            bfsDepth[fpKey] = 0;
        }
    }

    std::queue<std::pair<int,int>> bfsQueue;
    for (int fdx = 0; fdx < sN; ++fdx) {
        for (int fdz = 0; fdz < sN; ++fdz) {
            bfsQueue.push({sb.x + fdx, sb.z + fdz});
        }
    }

    maxDepth = 0;

    while (!bfsQueue.empty()) {
        auto [cx, cz] = bfsQueue.front();
        bfsQueue.pop();
        int depth = bfsDepth[tileKey(cx, cz)];

        for (int d = 0; d < 4; ++d) {
            int nx = cx + dx4[d];
            int nz = cz + dz4[d];
            int64_t nkey = tileKey(nx, nz);
            if (bfsDepth.count(nkey)) continue;
            if (!isBuildableTile(nx, nz)) continue;

            int newDepth = depth + 1;
            bfsDepth[nkey] = newDepth;
            if (newDepth > maxDepth) maxDepth = newDepth;
            bfsQueue.push({nx, nz});
        }
    }
}

float Zoning::computePowerCoverage(int tileX, int tileZ) const {
    for (const ServiceBuilding& sb : m_serviceBuildings) {
        if (sb.type != ServiceBuildingType::PowerPlant) continue;

        std::unordered_map<int64_t, int> bfsDepth;
        int maxDepth = 0;
        runPowerBfs(sb, bfsDepth, maxDepth);

        int64_t targetKey = tileKey(tileX, tileZ);
        auto it = bfsDepth.find(targetKey);
        if (it == bfsDepth.end()) {
            float radiusTiles = computeServiceCoverageRadius(ServiceBuildingType::PowerPlant, sb.degraded)
                                / kTileSizeMeters;
            float fdx = static_cast<float>(tileX - sb.x);
            float fdz = static_cast<float>(tileZ - sb.z);
            float dist = std::sqrt(fdx * fdx + fdz * fdz);
            if (dist <= radiusTiles) return 1.0f;
            continue;
        }

        int targetDepth = it->second;

        if (m_budgetSurplusPctRef <=
            SimulationConstants::service_deficit_radius_halving_threshold) {
            int coverDepth = static_cast<int>(
                std::floor(static_cast<float>(maxDepth) * 0.70f));
            if (targetDepth > coverDepth) continue;
        }

        return 1.0f;
    }
    return 0.0f;
}

// ---------------------------------------------------------------------------
// buildPowerCoverageCache
// ---------------------------------------------------------------------------

void Zoning::addRadialFallbackCoverage(const ServiceBuilding& sb, float radiusTiles,
                                        const std::unordered_map<int64_t, int>& bfsDepth) {
    int r = static_cast<int>(std::ceil(radiusTiles)) + 1;
    for (int dx = -r; dx <= r; ++dx) {
        for (int dz = -r; dz <= r; ++dz) {
            int nx = sb.x + dx;
            int nz = sb.z + dz;
            int64_t nkey = tileKey(nx, nz);
            if (bfsDepth.count(nkey)) continue;
            float fdx = static_cast<float>(dx);
            float fdz = static_cast<float>(dz);
            if (std::sqrt(fdx * fdx + fdz * fdz) <= radiusTiles) {
                m_powerCoverageCache.insert(nkey);
            }
        }
    }
}

void Zoning::buildPowerCoverageCache() {
    m_powerCoverageCache.clear();

    for (const ServiceBuilding& sb : m_serviceBuildings) {
        if (sb.type != ServiceBuildingType::PowerPlant) continue;

        std::unordered_map<int64_t, int> bfsDepth;
        int maxDepth = 0;
        runPowerBfs(sb, bfsDepth, maxDepth);

        int coverDepth = maxDepth;
        if (m_budgetSurplusPctRef <=
            SimulationConstants::service_deficit_radius_halving_threshold) {
            coverDepth = static_cast<int>(
                std::floor(static_cast<float>(maxDepth) * 0.70f));
        }

        float radiusTiles = computeServiceCoverageRadius(ServiceBuildingType::PowerPlant, sb.degraded)
                            / kTileSizeMeters;

        for (const auto& [key, depth] : bfsDepth) {
            if (depth <= coverDepth && depth > 0) {
                m_powerCoverageCache.insert(key);
            } else if (depth == 0) {
                m_powerCoverageCache.insert(key);
            }
        }

        addRadialFallbackCoverage(sb, radiusTiles, bfsDepth);
    }
}

// ---------------------------------------------------------------------------
// buildServiceCoverageMap
// ---------------------------------------------------------------------------

void Zoning::buildServiceCoverageMap(bool& outHasFireStation, bool& outHasPolice,
                                      bool& outHasWater, bool& outHasPower) const {
    outHasFireStation = false;
    outHasPolice      = false;
    outHasWater       = false;
    outHasPower       = false;

    for (const ServiceBuilding& sb : m_serviceBuildings) {
        switch (sb.type) {
            case ServiceBuildingType::FireStation:   outHasFireStation = true; break;
            case ServiceBuildingType::PoliceStation: outHasPolice      = true; break;
            case ServiceBuildingType::WaterTower:    outHasWater       = true; break;
            case ServiceBuildingType::PowerPlant:    outHasPower       = true; break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// doDesirabilityTick — thin orchestrator
// ---------------------------------------------------------------------------

void Zoning::doDesirabilityTick(const Economy& economy, const Traffic& /*traffic*/,
                                IAudioSystem* audio,
                                std::queue<SimulationNotification>& notifications) {
    buildPowerCoverageCache();

    bool hasFireStation = false, hasPolice = false, hasWater = false, hasPower = false;
    buildServiceCoverageMap(hasFireStation, hasPolice, hasWater, hasPower);

    applyDesirabilityScores(hasFireStation, hasPolice, hasWater, hasPower,
                            economy, audio, notifications);
}

// ---------------------------------------------------------------------------
// applyDesirabilityScores
// ---------------------------------------------------------------------------

float Zoning::computeNeighborDesirabilityDelta(int x, int z) const {
    float delta = 0.0f;
    for (int dz = -5; dz <= 5; ++dz) {
        for (int dx = -5; dx <= 5; ++dx) {
            if (dx == 0 && dz == 0) continue;
            int chebyshevDist = std::max(std::abs(dx), std::abs(dz));
            if (chebyshevDist > 5) continue;

            const TileData* neighbor = findTile(x + dx, z + dz);
            if (!neighbor || !neighbor->isZoned) continue;

            if (neighbor->zone == ZoneType::Industrial) {
                float falloff = 1.0f - static_cast<float>(chebyshevDist - 1) / 4.0f;
                delta -= SimulationConstants::adjacency_industrial_residential_base_penalty
                         * falloff;
            } else if (neighbor->zone == ZoneType::Commercial && chebyshevDist == 1) {
                delta += static_cast<float>(SimulationConstants::adjacency_commercial_residential_bonus);
            }
        }
    }
    return delta;
}

bool Zoning::computeFirePoliceCoverageGap(int x, int z,
                                           bool hasFireStation, bool hasPolice) const {
    if (hasFireStation) {
        float cov = computeRadialCoverage(x, z, ServiceBuildingType::FireStation);
        if (cov == 0.0f) return true;
    }
    if (hasPolice) {
        float cov = computeRadialCoverage(x, z, ServiceBuildingType::PoliceStation);
        if (cov == 0.0f) return true;
    }
    return false;
}

void Zoning::updateWaterState(TileData& tile, int x, int z, bool hasWater,
                               bool& anyUncovered, IAudioSystem* audio) {
    bool currentlyWaterCovered = false;
    if (hasWater) {
        float cov = computeRadialCoverage(x, z, ServiceBuildingType::WaterTower);
        if (cov == 0.0f) {
            anyUncovered = true;
        } else {
            currentlyWaterCovered = true;
        }
    }
    if (tile.wasWaterCovered && !currentlyWaterCovered && hasWater) {
        if (audio) {
            audio->playSound(SFX_WATER_OUT, SoundPriority::NORMAL, 1.0f);
        }
        tile.wasWaterCovered = false;
    } else if (currentlyWaterCovered) {
        tile.wasWaterCovered = true;
    }
}

void Zoning::updatePowerState(TileData& tile, int x, int z, bool hasPower,
                               bool& anyUncovered, IAudioSystem* audio) {
    bool currentlyPowered = false;
    if (hasPower) {
        if (!m_powerCoverageCache.count(tileKey(x, z))) {
            anyUncovered = true;
        } else {
            currentlyPowered = true;
        }
    }
    if (tile.wasPowered && !currentlyPowered && hasPower) {
        if (audio) {
            audio->playSound(SFX_POWER_OUT, SoundPriority::NORMAL, 1.0f);
        }
        tile.wasPowered = false;
    } else if (currentlyPowered) {
        tile.wasPowered = true;
    }
}

void Zoning::fireDesirabilityAlert(TileData& tile, int x, int z,
                                    bool hasFireStation, bool hasPolice,
                                    IAudioSystem* audio) {
    if (!tile.alertFired) {
        if (hasFireStation) {
            audio->playPositionalSound(SFX_FIRE_ALERT,
                vec3{static_cast<float>(x), 0.0f, static_cast<float>(z)},
                SoundPriority::CRITICAL, 1.0f);
        } else if (hasPolice) {
            audio->playPositionalSound(SFX_POLICE_ALERT,
                vec3{static_cast<float>(x), 0.0f, static_cast<float>(z)},
                SoundPriority::CRITICAL, 1.0f);
        }
        tile.alertFired = true;
    }
}

void Zoning::applyDesirabilityScores(bool hasFireStation, bool hasPolice,
                                      bool hasWater, bool hasPower,
                                      const Economy& /*economy*/, IAudioSystem* audio,
                                      std::queue<SimulationNotification>& /*notifications*/) {
    for (auto& [key, tile] : m_tiles) {
        if (!tile.isZoned) continue;

        int x = static_cast<int>(key >> 32);
        int z = static_cast<int>(static_cast<uint32_t>(key));

        float desirability = tile.desirability;

        if (tile.zone == ZoneType::Residential) {
            desirability += computeNeighborDesirabilityDelta(x, z);

            bool anyUncovered = false;

            if (!hasFireStation && !hasPolice && !hasWater && !hasPower) {
                anyUncovered = true;
            } else {
                if (computeFirePoliceCoverageGap(x, z, hasFireStation, hasPolice)) {
                    anyUncovered = true;
                }
                updateWaterState(tile, x, z, hasWater, anyUncovered, audio);
                updatePowerState(tile, x, z, hasPower, anyUncovered, audio);
            }

            if (anyUncovered) {
                if (!tile.firstDesirabilityTick) {
                    desirability -= static_cast<float>(SimulationConstants::service_uncovered_desirability_penalty_per_tick);
                }
            } else if (hasFireStation || hasPolice || hasWater || hasPower) {
                desirability += static_cast<float>(SimulationConstants::service_recovery_desirability_per_tick);
            }
        }

        tile.firstDesirabilityTick = false;

        tile.desirability = std::min(100.0f, std::max(0.0f, desirability));

        if (tile.isZoned && tile.zone == ZoneType::Residential && audio) {
            if (tile.desirability <= static_cast<float>(SimulationConstants::service_alert_desirability_threshold)) {
                fireDesirabilityAlert(tile, x, z, hasFireStation, hasPolice, audio);
            } else {
                tile.alertFired = false;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// doServiceDegradationTick
// ---------------------------------------------------------------------------

void Zoning::tryDegradeService(ServiceBuilding& sb, ISimulationRNG& rng,
                                IAudioSystem* audio,
                                std::queue<SimulationNotification>& notifications) {
    if (!sb.degraded) {
        float roll = rng.nextFloat();
        if (roll < SimulationConstants::service_degradation_probability_per_tick) {
            sb.degraded = true;
            if (audio) {
                audio->playSound(SFX_SERVICE_DEGRADE, SoundPriority::NORMAL, 1.0f);
            }
            notifications.push({NotificationType::ServiceDegraded, 0, 0, 0});
        }
    }
}

void Zoning::doServiceDegradationTick(const Economy& economy, ISimulationRNG& rng,
                                       IAudioSystem* audio,
                                       std::queue<SimulationNotification>& notifications) {
    if (economy.getBudgetSurplusPct() <= SimulationConstants::service_deficit_radius_halving_threshold) {
        auto typeOrder = [](ServiceBuildingType t) -> int {
            switch (t) {
                case ServiceBuildingType::FireStation:   return 0;
                case ServiceBuildingType::PoliceStation: return 1;
                case ServiceBuildingType::WaterTower:    return 2;
                case ServiceBuildingType::PowerPlant:    return 3;
            }
            return 4;
        };

        std::vector<size_t> indices(m_serviceBuildings.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            return typeOrder(m_serviceBuildings[a].type) < typeOrder(m_serviceBuildings[b].type);
        });

        for (size_t idx : indices) {
            ServiceBuilding& sb = m_serviceBuildings[idx];
            if (sb.type == ServiceBuildingType::PowerPlant) {
                sb.degraded = true;
                continue;
            }
            tryDegradeService(sb, rng, audio, notifications);
        }
    } else {
        for (ServiceBuilding& sb : m_serviceBuildings) {
            sb.degraded = false;
        }
    }
}

// ---------------------------------------------------------------------------
// doProximityTick
// ---------------------------------------------------------------------------

void Zoning::doProximityTick(std::queue<SimulationNotification>& notifications) {
    for (auto& [key, tile] : m_tiles) {
        if (!tile.isZoned || tile.footprintOriginX != -1) continue;

        int ox = static_cast<int>(static_cast<int64_t>(key) >> 32);
        int oz = static_cast<int>(static_cast<uint32_t>(key & 0xFFFFFFFFLL));
        int fp = footprintSize(tile.density);
        int dist = nearestRoadDistance(m_tiles, ox, oz, fp);

        if (dist > 3 && !tile.isAbandoned) {
            tile.isAbandoned  = true;
            tile.population   = 0.0f;
            notifications.push({NotificationType::BuildingAbandoned, ox, oz, 0});
        } else if (dist <= 3 && tile.isAbandoned) {
            tile.isAbandoned = false;
            float maxPop = static_cast<float>(maxPopulationForTile(tile.zone, tile.density));
            tile.population  = maxPop * 0.5f;
            notifications.push({NotificationType::BuildingRecovered, ox, oz, 0});
        }
    }
}

// ---------------------------------------------------------------------------
// addServiceBuilding — test-only injection (not via ICitySimulation)
// ---------------------------------------------------------------------------

void Zoning::addServiceBuilding(int x, int z, int serviceTypeInt) {
    ServiceBuildingType mappedType = ServiceBuildingType::None;
    switch (serviceTypeInt) {
        case 0: mappedType = ServiceBuildingType::FireStation;   break;
        case 1: mappedType = ServiceBuildingType::PoliceStation; break;
        case 2: mappedType = ServiceBuildingType::WaterTower;    break;
        case 3: mappedType = ServiceBuildingType::PowerPlant;    break;
        default: return;
    }
    ServiceBuilding sb;
    sb.x       = x;
    sb.z       = z;
    sb.type    = mappedType;
    sb.degraded = false;
    m_serviceBuildings.push_back(sb);
}
