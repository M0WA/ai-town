// CitySimulation.cpp — Full V1 simulation engine implementation for AI Town.
//
// Phase 11q1: CitySimulation is now a thin coordinator that delegates to five
// sub-systems: Economy, Traffic, Zoning, Population, SimTiming.
// All migrated method bodies live in their respective sub-system .cpp files.
//
// Source: src/simulation/CitySimulation.cpp

#include "CitySimulation.h"
#include "simulation_constants.h"
#include "src/interfaces/sound_ids.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <queue>

// ---------------------------------------------------------------------------
// Static helpers (kept in CitySimulation — used by constructor/reset/undo)
// ---------------------------------------------------------------------------

/*static*/ float CitySimulation::speedValue(SpeedMultiplier s) {
    return SimTiming::speedValue(s);
}

/*static*/ int64_t CitySimulation::startingFundsForDifficulty(Difficulty d) {
    switch (d) {
        case Difficulty::Easy:   return SimulationConstants::starting_funds_easy;
        case Difficulty::Normal: return SimulationConstants::starting_funds_normal;
        case Difficulty::Hard:   return SimulationConstants::starting_funds_hard;
    }
    return SimulationConstants::starting_funds_normal;
}

// ---------------------------------------------------------------------------
// Map dimensions
// ---------------------------------------------------------------------------

void CitySimulation::setMapDimensions(int mapWidth, int mapHeight) {
    m_mapWidth  = mapWidth;
    m_mapHeight = mapHeight;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

CitySimulation::CitySimulation(IRenderer*      renderer,
                               IAudioSystem*   audio,
                               ISimulationRNG* rng,
                               IClock*         clock,
                               ITerrainQuery*  terrain,
                               Difficulty      difficulty)
    : m_renderer(renderer)
    , m_audio(audio)
    , m_rng(rng)
    , m_clock(clock)
    , m_terrain(terrain)
    , m_difficulty(difficulty)
{
    m_economy.setInitialFunds(startingFundsForDifficulty(difficulty), difficulty);
    m_timing.m_speed = kDefaultSimSpeed;
    m_traffic.resetTrafficWindows();
    m_timing.m_constructionTimeSeconds = m_clock->nowSeconds();
}

// ---------------------------------------------------------------------------
// Speed / pause control
// ---------------------------------------------------------------------------

void CitySimulation::setPaused(bool paused) {
    m_timing.setPaused(paused);
}

void CitySimulation::setSpeed(SpeedMultiplier speed) {
    m_timing.setSpeed(speed);
}

bool CitySimulation::isPaused() const {
    return m_timing.isPaused();
}

SpeedMultiplier CitySimulation::getSpeedMultiplier() const {
    return m_timing.getSpeedMultiplier();
}

// ---------------------------------------------------------------------------
// reset
// ---------------------------------------------------------------------------

void CitySimulation::reset(int64_t startingFunds) {
    // Release audio sources for traffic vehicles then reset traffic sub-system.
    m_traffic.reset(m_audio);

    // Reset zoning (tiles, service buildings, counters).
    m_zoning = Zoning{};

    // Reset population.
    m_population = Population{};

    // Reset timing.
    m_timing = SimTiming{};
    m_timing.m_constructionTimeSeconds = m_clock->nowSeconds();
    m_timing.m_speed = kDefaultSimSpeed;

    // Reset economy.
    m_economy = Economy{};
    m_economy.setInitialFunds(startingFunds, m_difficulty);

    // Reset notification queue.
    while (!m_notifications.empty()) {
        m_notifications.pop();
    }

    // Reset undo.
    m_pendingUndo.reset();
    m_undoExpiryTickTarget = -1;
    m_undoExpiryWallSeconds = 0.0;
    m_modalOpen = false;

    // Reset placement sound cooldown.
    m_lastPlacementSoundTime = -1.0;

    // Reset scenario.
    m_scenarioState = ScenarioState{};
}

// ---------------------------------------------------------------------------
// Main simulation tick
// ---------------------------------------------------------------------------

void CitySimulation::tick(float realDeltaSeconds) {
    if (m_timing.isPaused()) {
        return;
    }

    int budgetTicks = m_timing.tick(realDeltaSeconds, m_timing.getSpeedMultiplier());

    if (m_timing.hasTimeOfDayChanged()) {
        if (m_audio) {
            m_audio->setTimeOfDay(m_timing.getTimeOfDay());
        }
    }

    for (int i = 0; i < budgetTicks; ++i) {
        doBudgetTick();

        // Phase 10: setMusicIntensity — called after doBudgetTick() so that
        // m_consecutiveDeficitMonths, m_budgetSurplusPct, and m_totalPopulation
        // all reflect the just-completed tick.
        m_population.updateMusicIntensity(m_economy, m_audio);
    }

    // Traffic signal and vehicle ticks run every frame (real-time).
    m_traffic.doTrafficSignalTick(realDeltaSeconds, m_renderer, m_audio, m_clock);
    m_traffic.doTrafficVehicleTick(realDeltaSeconds, m_zoning, m_renderer, m_audio);
}

// ---------------------------------------------------------------------------
// Budget tick orchestration
// ---------------------------------------------------------------------------

int CitySimulation::consumeBudgetTicks() {
    return m_timing.consumeBudgetTicks();
}

void CitySimulation::doBudgetTick() {
    bool inGracePeriod = ((m_clock->nowSeconds() - m_timing.getConstructionTimeSeconds()) <
                          SimulationConstants::grace_period_real_seconds);

    // Sync budget surplus for power BFS brownout.
    m_zoning.m_budgetSurplusPctRef = m_economy.getBudgetSurplusPct();

    // Pre-compute economy snapshot.
    m_economy.computeEconomySnapshot(m_zoning, m_traffic, m_population, inGracePeriod);

    // Sync budget surplus again after snapshot.
    m_zoning.m_budgetSurplusPctRef = m_economy.getBudgetSurplusPct();

    // Sub-methods called in EXACTLY this order (per spec):
    m_traffic.computeTrafficDemand(m_zoning, m_timing.getTotalTicks());
    m_traffic.computeEffectiveDemand(m_zoning, m_timing.getTotalTicks());
    m_zoning.doServiceDegradationTick(m_economy, *m_rng, m_audio, m_notifications);
    m_zoning.doDesirabilityTick(m_economy, m_traffic, m_audio, m_notifications);
    m_population.doPopulationTick(m_zoning, m_traffic, m_economy, m_audio, m_renderer, m_notifications);
    m_population.doDensityUnlockTick(m_zoning, m_traffic, m_economy, m_difficulty, m_renderer, m_audio, m_notifications);
    m_zoning.doProximityTick(m_notifications);
    // Re-compute economy snapshot with post-population-tick values, matching the original
    // CitySimulation::doEconomyTick() which called computeEconomySnapshot() a second time
    // after doPopulationTick() had updated tile populations.
    m_economy.computeEconomySnapshot(m_zoning, m_traffic, m_population, inGracePeriod);
    m_economy.doEconomyTick(m_zoning, m_population, inGracePeriod, m_audio, *m_clock, m_notifications);
    m_population.doGameOverTick(m_economy, m_timing, *m_clock);
    m_population.checkCityRatingTransition(m_notifications);

    // Undo expiry check
    if (m_undoExpiryTickTarget >= 0 && m_timing.getTotalTicks() >= m_undoExpiryTickTarget) {
        m_pendingUndo.reset();
        m_undoExpiryTickTarget = -1;
    }

    // Loan cooldown countdown
    if (m_economy.m_loanCooldownTicks > 0) {
        m_economy.m_loanCooldownTicks--;
    }
}

// ---------------------------------------------------------------------------
// Undo state accessors
// ---------------------------------------------------------------------------

bool CitySimulation::hasUndoPendingAction() const {
    return m_pendingUndo.has_value();
}

double CitySimulation::getUndoExpiryTimeSeconds() const {
    if (!m_pendingUndo.has_value()) return 0.0;
    return m_undoExpiryWallSeconds;
}

// ---------------------------------------------------------------------------
// Notification queue
// ---------------------------------------------------------------------------

bool CitySimulation::pollPendingNotification(SimulationNotification& out) {
    if (m_notifications.empty()) return false;
    out = m_notifications.front();
    m_notifications.pop();
    return true;
}

// ---------------------------------------------------------------------------
// Undo helpers
// ---------------------------------------------------------------------------

void CitySimulation::recordUndoAction(const UndoAction& action) {
    m_pendingUndo = action;
    m_undoExpiryTickTarget = m_timing.getTotalTicks() + 2;

    float sv = speedValue(m_timing.getSpeedMultiplier());
    if (sv > 0.0f) {
        double realSecondsRemaining = static_cast<double>(
            (2.0f * SimulationConstants::SECONDS_PER_BUDGET_TICK - m_timing.m_accumulatedSimSeconds) / sv);
        m_undoExpiryWallSeconds = m_clock->nowSeconds() + realSecondsRemaining;
    } else {
        m_undoExpiryWallSeconds = m_clock->nowSeconds() +
            static_cast<double>(2.0f * SimulationConstants::SECONDS_PER_BUDGET_TICK);
    }
}

// ---------------------------------------------------------------------------
// queryTile — delegates to Zoning but adds demand pressure from Traffic
// ---------------------------------------------------------------------------

QueryResult CitySimulation::queryTile(int tileX, int tileZ) const {
    QueryResult result = m_zoning.queryTile(tileX, tileZ);

    // Fill in per-tile effective demand factor from Traffic
    if (result.isZoned) {
        float effDemand = m_traffic.getZoneDemandFactor(result.zoneType);
        result.demandPressurePct = (1.0f - effDemand) * 100.0f;
    }

    return result;
}

// ---------------------------------------------------------------------------
// placeZone — extracted helpers (Phase 11q3)
// ---------------------------------------------------------------------------

bool CitySimulation::checkZoneFootprintClear(int tileX, int tileZ, int N) const {
    // Multi-tile footprint guard — check all N*N tiles empty and in-bounds.
    for (int dx = 0; dx < N; ++dx) {
        for (int dz = 0; dz < N; ++dz) {
            int fx = tileX + dx, fz = tileZ + dz;
            if (fx < 0 || fz < 0 ||
                (m_mapWidth > 0 && fx >= m_mapWidth) ||
                (m_mapHeight > 0 && fz >= m_mapHeight)) {
                return false;
            }
            int64_t fkey = Zoning::tileKey(fx, fz);
            auto fit = m_zoning.m_tiles.find(fkey);
            if (fit != m_zoning.m_tiles.end() && (fit->second.isRoad || fit->second.isZoned)) {
                return false;
            }
        }
    }

    if (checkServiceBuildingOverlap(tileX, tileZ, N)) return false;

    return true;
}

bool CitySimulation::checkServiceBuildingOverlap(int tileX, int tileZ, int N) const {
    for (const ServiceBuilding& sb : m_zoning.m_serviceBuildings) {
        if (sb.x < tileX + N && sb.x + 2 > tileX &&
            sb.z < tileZ + N && sb.z + 2 > tileZ)
            return true;
    }
    return false;
}

void CitySimulation::applyZoneFootprint(int tileX, int tileZ, ZoneType type, DensityTier tier, int N) {
    // Sample origin tile height BEFORE any setTileHeight calls.
    const float flatHeight = m_terrain ? m_terrain->getHeightAt(tileX, tileZ) : 0.0f;

    // Mark all N*N footprint tiles.
    for (int dx = 0; dx < N; ++dx) {
        for (int dz = 0; dz < N; ++dz) {
            int64_t fkey = Zoning::tileKey(tileX + dx, tileZ + dz);
            TileData& ftile = m_zoning.m_tiles[fkey];
            ftile.isZoned          = true;
            ftile.isRoad           = false;
            ftile.zone             = type;
            ftile.density          = tier;
            ftile.population       = 0.0f;
            ftile.desirability     = static_cast<float>(SimulationConstants::desirability_base_value);
            ftile.isAbandoned      = false;
            ftile.underConstruction = (dx == 0 && dz == 0);
            if (dx == 0 && dz == 0) {
                ftile.footprintOriginX = -1;
                ftile.footprintOriginZ = -1;
            } else {
                ftile.footprintOriginX = tileX;
                ftile.footprintOriginZ = tileZ;
            }
            if (m_terrain) {
                m_terrain->setTileHeight(tileX + dx, tileZ + dz, flatHeight);
            }
        }
    }

    flattenBorderRing(tileX, tileZ, N, flatHeight);
}

void CitySimulation::flattenBorderRing(int tileX, int tileZ, int N, float flatHeight) {
    if (!m_terrain) return;
    auto flattenIfRoad = [&](int bx, int bz) {
        auto bit = m_zoning.m_tiles.find(Zoning::tileKey(bx, bz));
        if (bit != m_zoning.m_tiles.end() && bit->second.isRoad)
            m_terrain->setTileHeight(bx, bz, flatHeight);
    };
    for (int dx = -1; dx <= N; ++dx) {
        for (int dz = -1; dz <= N; ++dz) {
            if (dx >= 0 && dx < N && dz >= 0 && dz < N) continue;
            int bx = tileX + dx, bz = tileZ + dz;
            if (bx < 0 || bx >= m_mapWidth || bz < 0 || bz >= m_mapHeight) continue;
            flattenIfRoad(bx, bz);
        }
    }
    m_terrain->flushTerrainRebuilds();
}

// ---------------------------------------------------------------------------
// placeZone
// ---------------------------------------------------------------------------

void CitySimulation::placeZone(int tileX, int tileZ, ZoneType type, DensityTier tier,
                               int earthworksCostOverride) {
    const int N = Zoning::footprintSize(tier);

    if (!checkZoneFootprintClear(tileX, tileZ, N)) {
        m_notifications.push({NotificationType::PlacementBlocked, tileX, tileZ, 0});
        return;
    }

    // Road proximity check.
    if (Zoning::nearestRoadDistance(m_zoning.m_tiles, tileX, tileZ, N) > 3) {
        m_notifications.push({NotificationType::PlacementBlocked, tileX, tileZ, 0});
        return;
    }

    int64_t key = Zoning::tileKey(tileX, tileZ);

    // Record previous state for undo.
    UndoAction undoAction;
    undoAction.actionType = UndoAction::Type::PlaceZone;
    undoAction.tileX = tileX;
    undoAction.tileZ = tileZ;
    {
        auto it = m_zoning.m_tiles.find(key);
        undoAction.previousState = (it != m_zoning.m_tiles.end()) ? it->second : TileData{};
    }
    undoAction.costPaid = static_cast<int64_t>(earthworksCostOverride);

    // Deduct earthworks cost.
    m_economy.m_treasury -= static_cast<int64_t>(earthworksCostOverride);

    applyZoneFootprint(tileX, tileZ, type, tier, N);

    // Play audio.
    if (m_audio && m_clock) {
        const double now = m_clock->nowSeconds();
        if (now - m_lastPlacementSoundTime >= 0.1) {
            m_lastPlacementSoundTime = now;
            const vec3 pos{static_cast<float>(tileX), 0.0f,
                           static_cast<float>(tileZ)};
            if (earthworksCostOverride > 0)
                m_audio->playPositionalSound(SFX_EARTHWORKS, pos,
                                             SoundPriority::NORMAL, 1.0f);
            m_audio->playPositionalSound(SFX_BUILD_PLACE, pos,
                                         SoundPriority::NORMAL, 1.0f);
        }
    }

    // Variant counter + deferred mesh spawn.
    {
        int zoneIdx = static_cast<int>(type);
        int tierIdx = static_cast<int>(tier);
        int idx     = zoneIdx * 3 + tierIdx;
        m_zoning.m_buildingVariantCounters[idx]++;
        int variantNum = ((m_zoning.m_buildingVariantCounters[idx] - 1) % 4) + 1;

        TileData* originTile = m_zoning.findTile(tileX, tileZ);
        if (originTile) {
            originTile->buildingVariantNum = variantNum;
        }
    }

    recordUndoAction(undoAction);
}

// ---------------------------------------------------------------------------
// placeRoad
// ---------------------------------------------------------------------------

void CitySimulation::placeRoad(int tileX, int tileZ, int earthworksCostOverride) {
    int64_t key = Zoning::tileKey(tileX, tileZ);

    // Early-return guard — tile occupied.
    {
        auto it = m_zoning.m_tiles.find(key);
        if (it != m_zoning.m_tiles.end() && (it->second.isRoad || it->second.isZoned)) {
            return;
        }
    }

    // Record previous state for undo.
    UndoAction undoAction;
    undoAction.actionType = UndoAction::Type::PlaceRoad;
    undoAction.tileX = tileX;
    undoAction.tileZ = tileZ;

    auto it = m_zoning.m_tiles.find(key);
    if (it != m_zoning.m_tiles.end()) {
        undoAction.previousState = it->second;
    } else {
        undoAction.previousState = TileData{};
    }

    int64_t totalCost = static_cast<int64_t>(SimulationConstants::road_placement_cost_per_tile) +
                        static_cast<int64_t>(earthworksCostOverride);
    undoAction.costPaid = totalCost;

    m_economy.m_treasury -= totalCost;

    bool wasRoad = (it != m_zoning.m_tiles.end() && it->second.isRoad);

    TileData& tile = m_zoning.m_tiles[key];
    tile.isRoad      = true;
    tile.isZoned     = false;
    tile.population  = 0.0f;
    tile.desirability = static_cast<float>(SimulationConstants::desirability_base_value);

    if (!wasRoad) {
        m_zoning.m_roadTileCount++;
    }

    // Traffic signal maintenance.
    if (!wasRoad) {
        m_traffic.addSignalForTile(tileX, tileZ, m_zoning);
    }

    // Play audio.
    if (m_audio && m_clock) {
        const double now = m_clock->nowSeconds();
        if (now - m_lastPlacementSoundTime >= 0.1) {
            m_lastPlacementSoundTime = now;
            const vec3 pos{static_cast<float>(tileX), 0.0f,
                           static_cast<float>(tileZ)};
            if (earthworksCostOverride > 0)
                m_audio->playPositionalSound(SFX_EARTHWORKS, pos,
                                             SoundPriority::NORMAL, 1.0f);
            m_audio->playPositionalSound(SFX_ROAD_BUILD, pos,
                                         SoundPriority::NORMAL, 1.0f);
        }
    }

    // Spawn road mesh.
    if (m_renderer) {
        m_renderer->placeRoadMesh(tileX, tileZ);
    }

    // Spawn traffic vehicle.
    if (!wasRoad) {
        m_traffic.spawnVehiclesForRoad(tileX, tileZ, m_zoning.m_roadTileCount,
                                       m_zoning, m_audio, m_rng);
    }

    recordUndoAction(undoAction);
}

// ---------------------------------------------------------------------------
// demolishTile — extracted helper (Phase 11q3)
// ---------------------------------------------------------------------------

void CitySimulation::removeTileFromScene(int tileX, int tileZ, bool wasRoad,
                                         bool hadServiceBuilding, const TileData& prev) {
    if (m_renderer) {
        if (wasRoad) {
            m_renderer->removeRoadMesh(tileX, tileZ);
        } else if (hadServiceBuilding) {
            m_renderer->removeServiceBuildingMesh(tileX, tileZ);
        } else if (prev.isZoned) {
            m_renderer->removeBuildingMesh(tileX, tileZ);
        }
    }

    m_zoning.m_serviceBuildings.erase(
        std::remove_if(m_zoning.m_serviceBuildings.begin(), m_zoning.m_serviceBuildings.end(),
            [tileX, tileZ](const ServiceBuilding& sb) {
                return sb.x == tileX && sb.z == tileZ;
            }),
        m_zoning.m_serviceBuildings.end());
}

// ---------------------------------------------------------------------------
// redirectToFootprintOrigin — redirect non-origin zone or service building
// tiles to their origin tile for demolition. Returns true (and calls
// demolishTile on the origin) if a redirect occurred; false otherwise.
// Phase 11q9: also handles service building footprints.
// ---------------------------------------------------------------------------

bool CitySimulation::redirectToFootprintOrigin(
        int tileX, int tileZ,
        std::unordered_map<int64_t, TileData>::iterator it) {
    // Zone-tile redirect (existing logic, moved here from demolishTile).
    if (it != m_zoning.m_tiles.end() && it->second.isZoned &&
        it->second.footprintOriginX != -1) {
        demolishTile(it->second.footprintOriginX, it->second.footprintOriginZ);
        return true;
    }

    // Service building redirect — service buildings are not stored in m_tiles.
    // Scan for a service building whose footprint covers (tileX, tileZ).
    // Copy origin coords before calling demolishTile because the call erases
    // from m_serviceBuildings, invalidating any live iterator/reference.
    // Use index-based loop to avoid range-for UB on mid-iteration erase.
    const int sN = Zoning::serviceFootprintSize();
    const size_t sbCount = m_zoning.m_serviceBuildings.size();
    for (size_t i = 0; i < sbCount; ++i) {
        const int sbx = m_zoning.m_serviceBuildings[i].x;
        const int sbz = m_zoning.m_serviceBuildings[i].z;
        if (tileX == sbx && tileZ == sbz) return false;  // origin — no redirect
        if (tileX >= sbx && tileX < sbx + sN &&
            tileZ >= sbz && tileZ < sbz + sN) {
            demolishTile(sbx, sbz);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// demolishTile
// ---------------------------------------------------------------------------

void CitySimulation::demolishTile(int tileX, int tileZ) {
    int64_t key = Zoning::tileKey(tileX, tileZ);
    auto it = m_zoning.m_tiles.find(key);

    // Redirect non-origin footprint tiles to origin (zone tiles + service buildings).
    if (redirectToFootprintOrigin(tileX, tileZ, it)) return;

    bool hadServiceBuilding = false;
    for (const ServiceBuilding& sb : m_zoning.m_serviceBuildings) {
        if (sb.x == tileX && sb.z == tileZ) { hadServiceBuilding = true; break; }
    }

    if (it == m_zoning.m_tiles.end() && !hadServiceBuilding) return;

    UndoAction undoAction;
    undoAction.actionType    = UndoAction::Type::Demolish;
    undoAction.tileX         = tileX;
    undoAction.tileZ         = tileZ;
    undoAction.previousState = (it != m_zoning.m_tiles.end()) ? it->second : TileData{};
    undoAction.costPaid      = 0;

    bool wasRoad = (it != m_zoning.m_tiles.end()) && it->second.isRoad;

    int clearN = 1;
    if (it != m_zoning.m_tiles.end() && it->second.isZoned && !it->second.isRoad) {
        clearN = Zoning::footprintSize(it->second.density);
    }

    for (int dx = 0; dx < clearN; ++dx) {
        for (int dz = 0; dz < clearN; ++dz) {
            int64_t fkey = Zoning::tileKey(tileX + dx, tileZ + dz);
            auto fit = m_zoning.m_tiles.find(fkey);
            if (fit != m_zoning.m_tiles.end()) {
                TileData& ftile = fit->second;
                ftile.isZoned         = false;
                ftile.isRoad          = false;
                ftile.population      = 0.0f;
                ftile.footprintOriginX = -1;
                ftile.footprintOriginZ = -1;
                ftile.isAbandoned     = false;
            }
        }
    }

    m_zoning.m_upgradeRetryCount.erase(key);

    if (wasRoad) {
        m_zoning.m_roadTileCount--;
    }

    if (wasRoad) {
        m_traffic.removeSignalForTile(tileX, tileZ);
    }

    if (m_audio) {
        m_audio->playPositionalSound(SFX_BUILD_DEMOLISH,
            vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
            SoundPriority::NORMAL, 1.0f);
    }

    removeTileFromScene(tileX, tileZ, wasRoad, hadServiceBuilding, undoAction.previousState);

    recordUndoAction(undoAction);
}

// ---------------------------------------------------------------------------
// placeServiceBuilding — extracted helper (Phase 11q3)
// ---------------------------------------------------------------------------

bool CitySimulation::checkServiceFootprintClear(int tileX, int tileZ, int sN) const {
    for (int dx = 0; dx < sN; ++dx) {
        for (int dz = 0; dz < sN; ++dz) {
            int fx = tileX + dx, fz = tileZ + dz;
            const TileData* ft = m_zoning.findTile(fx, fz);
            if (ft) return false;
            bool overlapsSb = std::any_of(m_zoning.m_serviceBuildings.begin(),
                                           m_zoning.m_serviceBuildings.end(),
                [fx, fz, sN](const ServiceBuilding& sb) {
                    return fx >= sb.x && fx < sb.x + sN && fz >= sb.z && fz < sb.z + sN;
                });
            if (overlapsSb) return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// placeServiceBuilding
// ---------------------------------------------------------------------------

bool CitySimulation::hasRoadAdjacent(int tileX, int tileZ, int sN) const {
    const int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    for (int dx = 0; dx < sN; ++dx) {
        for (int dz = 0; dz < sN; ++dz) {
            for (auto& d : dirs) {
                int nx = tileX + dx + d[0];
                int nz = tileZ + dz + d[1];
                const TileData* nd = m_zoning.findTile(nx, nz);
                if (nd && nd->isRoad) return true;
            }
        }
    }
    return false;
}

void CitySimulation::placeServiceBuilding(int tileX, int tileZ,
                                          ServiceBuildingType type,
                                          int earthworksCostOverride) {
    const int sN = Zoning::serviceFootprintSize();

    if (!checkServiceFootprintClear(tileX, tileZ, sN)) {
        return;
    }

    if (!hasRoadAdjacent(tileX, tileZ, sN)) {
        m_notifications.push({NotificationType::PlacementBlocked, tileX, tileZ, 0});
        return;
    }

    int placementCost = 0;
    switch (type) {
        case ServiceBuildingType::PowerPlant:
            placementCost = SimulationConstants::service_placement_cost_power_plant; break;
        case ServiceBuildingType::WaterTower:
            placementCost = SimulationConstants::service_placement_cost_water_tower; break;
        case ServiceBuildingType::FireStation:
            placementCost = SimulationConstants::service_placement_cost_fire_station; break;
        case ServiceBuildingType::PoliceStation:
            placementCost = SimulationConstants::service_placement_cost_police_station; break;
        default: return;
    }

    UndoAction undoAction;
    undoAction.actionType = UndoAction::Type::PlaceZone;
    undoAction.tileX = tileX;
    undoAction.tileZ = tileZ;
    int64_t key = Zoning::tileKey(tileX, tileZ);
    auto it = m_zoning.m_tiles.find(key);
    undoAction.previousState = (it != m_zoning.m_tiles.end()) ? it->second : TileData{};

    int64_t totalCost = static_cast<int64_t>(placementCost)
                        + static_cast<int64_t>(earthworksCostOverride);
    undoAction.costPaid = totalCost;

    m_economy.m_treasury -= totalCost;

    ServiceBuilding sb;
    sb.x        = tileX;
    sb.z        = tileZ;
    sb.type     = type;
    sb.degraded = false;
    m_zoning.m_serviceBuildings.push_back(sb);

    if (earthworksCostOverride > 0) {
        if (m_audio) {
            m_audio->playPositionalSound(SFX_EARTHWORKS,
                vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
                SoundPriority::NORMAL, 1.0f);
        }
    }
    if (m_audio) {
        m_audio->playPositionalSound(SFX_BUILD_PLACE,
            vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
            SoundPriority::NORMAL, 1.0f);
    }

    if (m_renderer) {
        m_renderer->placeServiceBuildingMesh(tileX, tileZ, type);
    }

    recordUndoAction(undoAction);
}

// ---------------------------------------------------------------------------
// undoLastAction
// ---------------------------------------------------------------------------

void CitySimulation::undoLastAction() {
    if (!m_pendingUndo.has_value()) return;
    if (m_modalOpen) return;

    const UndoAction& action = m_pendingUndo.value();

    int64_t key = Zoning::tileKey(action.tileX, action.tileZ);

    auto it = m_zoning.m_tiles.find(key);
    bool currentlyRoad  = (it != m_zoning.m_tiles.end() && it->second.isRoad);

    m_zoning.m_tiles[key] = action.previousState;

    bool prevWasRoad = action.previousState.isRoad;
    if (currentlyRoad && !prevWasRoad) {
        m_zoning.m_roadTileCount--;
    } else if (!currentlyRoad && prevWasRoad) {
        m_zoning.m_roadTileCount++;
    }

    int64_t startingFunds = startingFundsForDifficulty(m_difficulty);
    m_economy.m_treasury = std::min(m_economy.m_treasury + action.costPaid, startingFunds);

    m_pendingUndo.reset();
    m_undoExpiryTickTarget = -1;
}

// ---------------------------------------------------------------------------
// Test / internal API
// ---------------------------------------------------------------------------

int CitySimulation::getBuildingVariantCounter(int zone, int tier) const {
    return m_zoning.getBuildingVariantCounter(zone, tier);
}

void CitySimulation::addServiceBuilding(int x, int z, int serviceTypeInt) {
    m_zoning.addServiceBuilding(x, z, serviceTypeInt);
}

void CitySimulation::setModalOpen(bool open) {
    m_modalOpen = open;
}

#ifdef AITOWN_TESTING_ENABLED
void CitySimulation::testForceUnlockDensityTier(ZoneType zone, DensityTier tier) {
    m_population.testForceUnlockDensityTier(zone, tier);
}
void CitySimulation::testSetZoneDemandFactor(ZoneType zone, float value) {
    m_traffic.overrideZoneDemandFactor(zone, value);
}
#endif

// ---------------------------------------------------------------------------
// serializeToJson
// ---------------------------------------------------------------------------
std::string CitySimulation::serializeToJson() const {
    float outstandingDebt = 0.0f;
    for (const auto& loan : m_economy.m_loans) {
        outstandingDebt += static_cast<float>(loan.remainingPrincipal);
    }

    int speedInt = 0;
    switch (m_timing.getSpeedMultiplier()) {
        case SpeedMultiplier::Paused: speedInt = 0; break;
        case SpeedMultiplier::x1:    speedInt = 1; break;
        case SpeedMultiplier::x3:    speedInt = 2; break;
        case SpeedMultiplier::x10:   speedInt = 3; break;
    }

    nlohmann::json j;
    j["schema_version"] = 1;
    j["map_tiles_x"] = m_mapWidth;
    j["map_tiles_z"] = m_mapHeight;
    j["treasury_balance"] = m_economy.m_treasury;

    j["tax_rates"] = nlohmann::json::array({
        m_economy.m_taxRates[0], m_economy.m_taxRates[1], m_economy.m_taxRates[2]
    });

    j["outstanding_debt"] = outstandingDebt;
    j["outstanding_bond_uses"] = m_economy.m_outstandingBondUses;
    j["consecutive_deficit_months"] = m_population.m_consecutiveDeficitMonths;
    j["speed_multiplier"] = speedInt;

    nlohmann::json milestonesArr = nlohmann::json::array();
    for (int i = 0; i < 5; ++i)
        milestonesArr.push_back(m_population.m_milestoneFired[i]);
    j["population_milestone_fired"] = std::move(milestonesArr);

    nlohmann::json variantArr = nlohmann::json::array();
    for (int i = 0; i < 9; ++i)
        variantArr.push_back(m_zoning.m_buildingVariantCounters[i]);
    j["building_variant_counters"] = std::move(variantArr);

    nlohmann::json tilesArr = nlohmann::json::array();
    for (const auto& [key, tile] : m_zoning.m_tiles) {
        int tx = static_cast<int>(key >> 32);
        int tz = static_cast<int>(static_cast<uint32_t>(key & 0xFFFFFFFFLL));
        nlohmann::json tobj;
        tobj["x"] = tx;
        tobj["z"] = tz;
        tobj["zone"] = static_cast<int>(tile.zone);
        tobj["tier"] = static_cast<int>(tile.density);
        tobj["is_zoned"] = tile.isZoned;
        tobj["is_road"] = tile.isRoad;
        tobj["population"] = tile.population;
        tobj["alert_fired"] = tile.alertFired;
        tobj["under_construction"] = tile.underConstruction;
        tobj["variant_num"] = tile.buildingVariantNum;
        tobj["fp_origin_x"] = tile.footprintOriginX;
        tobj["fp_origin_z"] = tile.footprintOriginZ;
        tobj["was_powered"] = tile.wasPowered;
        tobj["was_water_covered"] = tile.wasWaterCovered;
        tilesArr.push_back(std::move(tobj));
    }
    j["tiles"] = std::move(tilesArr);

    nlohmann::json sbArr = nlohmann::json::array();
    for (const auto& sb : m_zoning.m_serviceBuildings) {
        nlohmann::json sobj;
        sobj["x"] = sb.x;
        sobj["z"] = sb.z;
        sobj["type"] = static_cast<int>(sb.type);
        sobj["degraded"] = sb.degraded;
        sbArr.push_back(std::move(sobj));
    }
    j["service_buildings"] = std::move(sbArr);

    nlohmann::json unlockFlagsArr = nlohmann::json::array();
    for (int i = 0; i < 6; ++i)
        unlockFlagsArr.push_back(m_population.m_densityUnlockState.unlock_flags[i]);
    j["density_unlock_flags"] = std::move(unlockFlagsArr);

    nlohmann::json unlockCounterArr = nlohmann::json::array();
    for (int i = 0; i < 6; ++i)
        unlockCounterArr.push_back(m_population.m_densityUnlockState.consecutive_months_above_threshold[i]);
    j["density_unlock_revenue_counter"] = std::move(unlockCounterArr);

    j["total_ticks"] = m_timing.getTotalTicks();
    j["month"] = m_timing.getSimulationTime().month;
    j["year"] = m_timing.getSimulationTime().year;

    nlohmann::json scenarioObj;
    scenarioObj["win_condition_progress"] = m_scenarioState.win_condition_progress;
    scenarioObj["elapsed_ticks"] = m_scenarioState.elapsed_ticks;
    scenarioObj["scenario_id"] = m_scenarioState.scenario_id;
    j["scenario_state"] = std::move(scenarioObj);

    return j.dump(2);
}

// ---------------------------------------------------------------------------
// deserializeFromJson — extracted parse helpers (Phase 11q3)
// ---------------------------------------------------------------------------

bool CitySimulation::parseEconomySection(const nlohmann::json& j, int64_t& treasury,
                                         float taxRates[3], std::string& err) {
    try { treasury = j.at("treasury_balance").get<int64_t>(); }
    catch (...) { err = "missing treasury_balance"; return false; }

    try {
        const auto& taxArr = j.at("tax_rates");
        for (int i = 0; i < 3; ++i)
            taxRates[i] = taxArr.at(i).get<float>();
    } catch (...) {
        err = "missing tax_rates";
        return false;
    }

    try { j.at("outstanding_debt"); }
    catch (...) { err = "missing outstanding_debt"; return false; }

    return true;
}

bool CitySimulation::parseZoningSection(const nlohmann::json& j, std::string& err) {
    try {
        for (const auto& tobj : j.at("tiles")) {
            int tileX = tobj.at("x").get<int>();
            int tileZ = tobj.at("z").get<int>();
            TileData td{};
            int zoneVal = tobj.at("zone").get<int>();
            if (zoneVal < 0 || zoneVal > 2) { err = "invalid zone value"; return false; }
            td.zone = static_cast<ZoneType>(zoneVal);
            int tierVal = tobj.at("tier").get<int>();
            if (tierVal < 0 || tierVal > 2) { err = "invalid tier value"; return false; }
            td.density = static_cast<DensityTier>(tierVal);
            td.isZoned = tobj.at("is_zoned").get<bool>();
            td.isRoad  = tobj.at("is_road").get<bool>();
            td.population = tobj.at("population").get<float>();
            td.footprintOriginX   = tobj.value("fp_origin_x", -1);
            td.footprintOriginZ   = tobj.value("fp_origin_z", -1);
            td.isAbandoned        = tobj.value("is_abandoned", false);
            td.underConstruction  = tobj.value("under_construction", false);
            td.buildingVariantNum = tobj.value("variant_num", 0);
            td.alertFired         = tobj.value("alert_fired", false);
            td.wasPowered         = tobj.value("was_powered", true);
            td.wasWaterCovered    = tobj.value("was_water_covered", true);
            int64_t key = Zoning::tileKey(tileX, tileZ);
            m_zoning.m_tiles[key] = td;
            if (td.isRoad) ++m_zoning.m_roadTileCount;
        }
    } catch (...) {
        if (err.empty()) err = "missing or invalid tiles";
        return false;
    }

    try {
        for (const auto& sobj : j.at("service_buildings")) {
            ServiceBuilding sb{};
            sb.x = sobj.at("x").get<int>();
            sb.z = sobj.at("z").get<int>();
            int typeVal = sobj.at("type").get<int>();
            if (typeVal < 0 || typeVal > 3) { err = "invalid service building type"; return false; }
            sb.type = static_cast<ServiceBuildingType>(typeVal);
            sb.degraded = sobj.at("degraded").get<bool>();
            m_zoning.m_serviceBuildings.push_back(sb);
        }
    } catch (...) {
        if (err.empty()) err = "missing or invalid service_buildings";
        return false;
    }

    return true;
}

bool CitySimulation::parseTrafficSection(const nlohmann::json& j, std::string& err) {
    // V1 save format has no explicit traffic sub-object — speed_multiplier and
    // scenario_state are parsed here as they affect simulation flow control.
    int speedInt = j.value("speed_multiplier", 2);
    switch (speedInt) {
        case 0: m_timing.setSpeed(SpeedMultiplier::Paused); break;
        case 1: m_timing.setSpeed(SpeedMultiplier::x1);     break;
        case 2: m_timing.setSpeed(SpeedMultiplier::x3);     break;
        case 3: m_timing.setSpeed(SpeedMultiplier::x10);    break;
        default:
            err = "invalid speed_multiplier value: " + std::to_string(speedInt);
            return false;
    }

    try { m_timing.m_totalTicks = j.at("total_ticks").get<int>(); }
    catch (...) { err = "missing total_ticks"; return false; }

    try {
        int month = j.at("month").get<int>();
        if (month < 1 || month > 12) {
            err = "month out of range: " + std::to_string(month);
            return false;
        }
        m_timing.m_month = month;
    } catch (...) {
        if (err.empty()) err = "missing month";
        return false;
    }

    try { m_timing.m_year = j.at("year").get<int>(); }
    catch (...) { err = "missing year"; return false; }

    try {
        const auto& ss = j.at("scenario_state");
        m_scenarioState.win_condition_progress = ss.at("win_condition_progress").get<float>();
        m_scenarioState.elapsed_ticks = ss.at("elapsed_ticks").get<int>();
        m_scenarioState.scenario_id = ss.at("scenario_id").get<std::string>();
    } catch (...) {
        err = "missing or invalid scenario_state";
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// deserializeFromJson — Phase 11q5 extracted helpers (S3776)
// ---------------------------------------------------------------------------

template<typename T>
bool CitySimulation::parseJsonField(const nlohmann::json& j, const char* key,
                                     T& out, std::string& errorOut) const {
    try { out = j.at(key).template get<T>(); return true; }
    catch (...) { errorOut = std::string("missing ") + key; return false; }
}

template bool CitySimulation::parseJsonField<int>(const nlohmann::json&, const char*, int&, std::string&) const;
template bool CitySimulation::parseJsonField<int64_t>(const nlohmann::json&, const char*, int64_t&, std::string&) const;

bool CitySimulation::parseOptionalBoolArray(const nlohmann::json& j, const char* key,
                                            bool* out, int maxCount, std::string& errorOut) const {
    try {
        if (j.contains(key)) {
            const auto& arr = j[key];
            for (int i = 0; i < maxCount && i < static_cast<int>(arr.size()); ++i)
                out[i] = arr[i].get<bool>();
        }
    } catch (...) {
        errorOut = std::string("invalid ") + key;
        return false;
    }
    return true;
}

bool CitySimulation::parseOptionalIntArray(const nlohmann::json& j, const char* key,
                                           int* out, int maxCount, std::string& errorOut) const {
    try {
        if (j.contains(key)) {
            const auto& arr = j[key];
            for (int i = 0; i < maxCount && i < static_cast<int>(arr.size()); ++i)
                out[i] = arr[i].get<int>();
        }
    } catch (...) {
        errorOut = std::string("invalid ") + key;
        return false;
    }
    return true;
}

bool CitySimulation::parseDensityUnlockSection(const nlohmann::json& j,
                                                DensityUnlockState& out,
                                                std::string& errorOut) const {
    bool flagsBuf[6] = {};
    if (!parseOptionalBoolArray(j, "density_unlock_flags", flagsBuf, 6, errorOut))
        return false;
    for (int i = 0; i < 6; ++i) out.unlock_flags[i] = flagsBuf[i];

    int counterBuf[6] = {};
    if (!parseOptionalIntArray(j, "density_unlock_revenue_counter", counterBuf, 6, errorOut))
        return false;
    for (int i = 0; i < 6; ++i) out.consecutive_months_above_threshold[i] = counterBuf[i];

    return true;
}

// ---------------------------------------------------------------------------
// deserializeFromJson
// ---------------------------------------------------------------------------
bool CitySimulation::deserializeFromJson(const std::string& json, std::string& errorOut) {
    auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded()) {
        errorOut = "failed to parse JSON";
        return false;
    }

    try {
        int v = j.at("schema_version").get<int>();
        if (v != 1) {
            errorOut = "unsupported schema_version: " + std::to_string(v);
            return false;
        }
    } catch (...) {
        errorOut = "missing schema_version";
        return false;
    }

    int newMapTilesX = m_mapWidth;
    int newMapTilesZ = m_mapHeight;
    if (!parseJsonField(j, "map_tiles_x", newMapTilesX, errorOut)) return false;
    if (!parseJsonField(j, "map_tiles_z", newMapTilesZ, errorOut)) return false;

    int64_t newTreasury = 0;
    float   newTaxRates[3] = {0.05f, 0.05f, 0.05f};
    if (!parseEconomySection(j, newTreasury, newTaxRates, errorOut)) return false;

    int newOutstandingBondUses = 0;
    if (!parseJsonField(j, "outstanding_bond_uses", newOutstandingBondUses, errorOut)) return false;

    int newConsecutiveDeficitMonths = 0;
    if (!parseJsonField(j, "consecutive_deficit_months", newConsecutiveDeficitMonths, errorOut)) return false;

    bool newMilestoneFired[5] = {};
    if (!parseOptionalBoolArray(j, "population_milestone_fired", newMilestoneFired, 5, errorOut)) return false;

    std::array<int,9> newVariantCounters{};
    if (!parseOptionalIntArray(j, "building_variant_counters", newVariantCounters.data(), 9, errorOut)) return false;

    DensityUnlockState newDensityUnlock{};
    if (!parseDensityUnlockSection(j, newDensityUnlock, errorOut)) return false;

    // ---- Atomically apply the deserialized state ----
    m_mapWidth               = newMapTilesX;
    m_mapHeight              = newMapTilesZ;
    m_economy.m_treasury     = newTreasury;
    m_economy.m_taxRates[0]  = newTaxRates[0];
    m_economy.m_taxRates[1]  = newTaxRates[1];
    m_economy.m_taxRates[2]  = newTaxRates[2];
    m_economy.m_outstandingBondUses = newOutstandingBondUses;
    m_population.m_consecutiveDeficitMonths = newConsecutiveDeficitMonths;
    for (int i = 0; i < 5; ++i) m_population.m_milestoneFired[i] = newMilestoneFired[i];
    m_zoning.m_buildingVariantCounters = newVariantCounters;

    m_zoning.m_tiles.clear();
    m_zoning.m_roadTileCount = 0;
    m_zoning.m_serviceBuildings.clear();
    if (!parseZoningSection(j, errorOut)) return false;

    if (!parseTrafficSection(j, errorOut)) return false;

    m_population.m_densityUnlockState = newDensityUnlock;

    m_economy.m_loans.clear();
    m_economy.m_loanCooldownTicks = 0;

    int totalPop = 0;
    for (const auto& [k, td] : m_zoning.m_tiles) {
        totalPop += static_cast<int>(td.population);
    }
    m_population.m_totalPopulation = totalPop;
    m_population.m_prevPopulation  = totalPop;

    m_pendingUndo.reset();
    m_undoExpiryTickTarget = -1;
    m_timing.m_accumulatedSimSeconds = 0.0f;

    return true;
}

bool CitySimulation::applyLoadedJson(const std::string& json) {
    std::string err;
    return deserializeFromJson(json, err);
}
