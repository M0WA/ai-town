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

#include <algorithm>
#include <cassert>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
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

/*static*/ int CitySimulation::bondMaxUsesForDifficulty(Difficulty d) {
    switch (d) {
        case Difficulty::Easy:   return SimulationConstants::bond_max_uses_easy;
        case Difficulty::Normal: return SimulationConstants::bond_max_uses_normal;
        case Difficulty::Hard:   return SimulationConstants::bond_max_uses_hard;
    }
    return SimulationConstants::bond_max_uses_normal;
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
// placeZone
// ---------------------------------------------------------------------------

void CitySimulation::placeZone(int tileX, int tileZ, ZoneType type, DensityTier tier,
                               int earthworksCostOverride) {
    const int N = Zoning::footprintSize(tier);

    // Multi-tile footprint guard — check all N*N tiles empty and in-bounds.
    for (int dx = 0; dx < N; ++dx) {
        for (int dz = 0; dz < N; ++dz) {
            int fx = tileX + dx, fz = tileZ + dz;
            if (fx < 0 || fz < 0) {
                m_notifications.push({NotificationType::PlacementBlocked, tileX, tileZ, 0});
                return;
            }
            int64_t fkey = Zoning::tileKey(fx, fz);
            auto fit = m_zoning.m_tiles.find(fkey);
            if (fit != m_zoning.m_tiles.end() && (fit->second.isRoad || fit->second.isZoned)) {
                m_notifications.push({NotificationType::PlacementBlocked, tileX, tileZ, 0});
                return;
            }
        }
    }

    // Service building overlap guard.
    for (const ServiceBuilding& sb : m_zoning.m_serviceBuildings) {
        for (int sdx = 0; sdx < 2; ++sdx) {
            for (int sdz = 0; sdz < 2; ++sdz) {
                int sx = sb.x + sdx, sz = sb.z + sdz;
                for (int dx = 0; dx < N; ++dx) {
                    for (int dz = 0; dz < N; ++dz) {
                        if (tileX + dx == sx && tileZ + dz == sz) {
                            m_notifications.push({NotificationType::PlacementBlocked, tileX, tileZ, 0});
                            return;
                        }
                    }
                }
            }
        }
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

    // Border-ring terrain flattening.
    if (m_terrain) {
        for (int dx = -1; dx <= N; ++dx) {
            for (int dz = -1; dz <= N; ++dz) {
                if (dx >= 0 && dx < N && dz >= 0 && dz < N) continue;
                int bx = tileX + dx, bz = tileZ + dz;
                if (bx < 0 || bx >= m_mapWidth || bz < 0 || bz >= m_mapHeight) continue;
                int64_t bkey = Zoning::tileKey(bx, bz);
                auto bit = m_zoning.m_tiles.find(bkey);
                if (bit != m_zoning.m_tiles.end() && bit->second.isRoad) {
                    m_terrain->setTileHeight(bx, bz, flatHeight);
                }
            }
        }
        m_terrain->flushTerrainRebuilds();
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
// demolishTile
// ---------------------------------------------------------------------------

void CitySimulation::demolishTile(int tileX, int tileZ) {
    int64_t key = Zoning::tileKey(tileX, tileZ);
    auto it = m_zoning.m_tiles.find(key);

    // Redirect non-origin footprint tiles to origin.
    if (it != m_zoning.m_tiles.end() && it->second.isZoned &&
        it->second.footprintOriginX != -1) {
        demolishTile(it->second.footprintOriginX, it->second.footprintOriginZ);
        return;
    }

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

    if (m_renderer) {
        if (wasRoad) {
            m_renderer->removeRoadMesh(tileX, tileZ);
        } else if (hadServiceBuilding) {
            m_renderer->removeServiceBuildingMesh(tileX, tileZ);
        } else if (undoAction.previousState.isZoned) {
            m_renderer->removeBuildingMesh(tileX, tileZ);
        }
    }

    m_zoning.m_serviceBuildings.erase(
        std::remove_if(m_zoning.m_serviceBuildings.begin(), m_zoning.m_serviceBuildings.end(),
            [tileX, tileZ](const ServiceBuilding& sb) {
                return sb.x == tileX && sb.z == tileZ;
            }),
        m_zoning.m_serviceBuildings.end());

    recordUndoAction(undoAction);
}

// ---------------------------------------------------------------------------
// placeServiceBuilding
// ---------------------------------------------------------------------------

void CitySimulation::placeServiceBuilding(int tileX, int tileZ,
                                          ServiceBuildingType type,
                                          int earthworksCostOverride) {
    {
        const int sN = Zoning::serviceFootprintSize();
        for (int dx = 0; dx < sN; ++dx) {
            for (int dz = 0; dz < sN; ++dz) {
                int fx = tileX + dx, fz = tileZ + dz;
                const TileData* ft = m_zoning.findTile(fx, fz);
                if (ft) return;
                for (const ServiceBuilding& sb : m_zoning.m_serviceBuildings) {
                    if (fx >= sb.x && fx < sb.x + sN &&
                        fz >= sb.z && fz < sb.z + sN) {
                        return;
                    }
                }
            }
        }
    }

    {
        const int sN = Zoning::serviceFootprintSize();
        bool hasRoadAdjacent = false;
        const int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
        for (int dx = 0; dx < sN && !hasRoadAdjacent; ++dx) {
            for (int dz = 0; dz < sN && !hasRoadAdjacent; ++dz) {
                for (auto& d : dirs) {
                    int nx = tileX + dx + d[0];
                    int nz = tileZ + dz + d[1];
                    const TileData* nd = m_zoning.findTile(nx, nz);
                    if (nd && nd->isRoad) { hasRoadAdjacent = true; break; }
                }
            }
        }
        if (!hasRoadAdjacent) {
            m_notifications.push({NotificationType::PlacementBlocked, tileX, tileZ, 0});
            return;
        }
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
#endif

// ===========================================================================
// Serialization helpers — hand-written minimal JSON (no external library).
// ===========================================================================

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

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

    std::string j;
    j.reserve(4096);

    j += "{\n";
    j += "  \"schema_version\": 1,\n";
    j += "  \"map_tiles_x\": " + std::to_string(m_mapWidth) + ",\n";
    j += "  \"map_tiles_z\": " + std::to_string(m_mapHeight) + ",\n";
    j += "  \"treasury_balance\": " + std::to_string(m_economy.m_treasury) + ",\n";

    j += "  \"tax_rates\": [";
    for (int i = 0; i < 3; ++i) {
        if (i > 0) j += ", ";
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6f", m_economy.m_taxRates[i]);
        j += buf;
    }
    j += "],\n";

    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6f", outstandingDebt);
        j += "  \"outstanding_debt\": ";
        j += buf;
        j += ",\n";
    }
    j += "  \"outstanding_bond_uses\": " + std::to_string(m_economy.m_outstandingBondUses) + ",\n";
    j += "  \"consecutive_deficit_months\": " + std::to_string(m_population.m_consecutiveDeficitMonths) + ",\n";
    j += "  \"speed_multiplier\": " + std::to_string(speedInt) + ",\n";

    j += "  \"population_milestone_fired\": [";
    for (int i = 0; i < 5; ++i) {
        if (i > 0) j += ", ";
        j += (m_population.m_milestoneFired[i] ? "true" : "false");
    }
    j += "],\n";

    j += "  \"building_variant_counters\": [";
    for (int i = 0; i < 9; ++i) {
        if (i > 0) j += ", ";
        j += std::to_string(m_zoning.m_buildingVariantCounters[i]);
    }
    j += "],\n";

    j += "  \"tiles\": [\n";
    {
        bool first = true;
        for (const auto& [key, tile] : m_zoning.m_tiles) {
            int tx = static_cast<int>(key >> 32);
            int tz = static_cast<int>(static_cast<uint32_t>(key & 0xFFFFFFFFLL));
            if (!first) j += ",\n";
            first = false;
            char popBuf[32];
            std::snprintf(popBuf, sizeof(popBuf), "%.6f", tile.population);
            j += "    {\"x\": " + std::to_string(tx)
               + ", \"z\": " + std::to_string(tz)
               + ", \"zone\": " + std::to_string(static_cast<int>(tile.zone))
               + ", \"tier\": " + std::to_string(static_cast<int>(tile.density))
               + ", \"is_zoned\": " + (tile.isZoned ? "true" : "false")
               + ", \"is_road\": " + (tile.isRoad ? "true" : "false")
               + ", \"population\": " + popBuf
               + ", \"alert_fired\": " + (tile.alertFired ? "true" : "false")
               + ", \"under_construction\": " + (tile.underConstruction ? "true" : "false")
               + ", \"variant_num\": " + std::to_string(tile.buildingVariantNum)
               + ", \"fp_origin_x\": " + std::to_string(tile.footprintOriginX)
               + ", \"fp_origin_z\": " + std::to_string(tile.footprintOriginZ)
               + "}";
        }
    }
    j += "\n  ],\n";

    j += "  \"service_buildings\": [\n";
    {
        bool first = true;
        for (const auto& sb : m_zoning.m_serviceBuildings) {
            if (!first) j += ",\n";
            first = false;
            j += "    {\"x\": " + std::to_string(sb.x)
               + ", \"z\": " + std::to_string(sb.z)
               + ", \"type\": " + std::to_string(static_cast<int>(sb.type))
               + ", \"degraded\": " + (sb.degraded ? "true" : "false")
               + "}";
        }
    }
    j += "\n  ],\n";

    j += "  \"density_unlock_flags\": [";
    for (int i = 0; i < 6; ++i) {
        if (i > 0) j += ", ";
        j += (m_population.m_densityUnlockState.unlock_flags[i] ? "true" : "false");
    }
    j += "],\n";

    j += "  \"density_unlock_revenue_counter\": [";
    for (int i = 0; i < 6; ++i) {
        if (i > 0) j += ", ";
        j += std::to_string(m_population.m_densityUnlockState.consecutive_months_above_threshold[i]);
    }
    j += "],\n";

    j += "  \"total_ticks\": " + std::to_string(m_timing.getTotalTicks()) + ",\n";
    j += "  \"month\": " + std::to_string(m_timing.getSimulationTime().month) + ",\n";
    j += "  \"year\": " + std::to_string(m_timing.getSimulationTime().year) + ",\n";

    j += "  \"scenario_state\": {";
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6f", m_scenarioState.win_condition_progress);
        j += "\"win_condition_progress\": ";
        j += buf;
    }
    j += ", \"elapsed_ticks\": " + std::to_string(m_scenarioState.elapsed_ticks);
    j += ", \"scenario_id\": \"" + jsonEscape(m_scenarioState.scenario_id) + "\"";
    j += "}\n";

    j += "}\n";
    return j;
}

// ===========================================================================
// Minimal JSON parser helpers for deserializeFromJson
// ===========================================================================

namespace {

static void skipWs(const std::string& s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n'))
        ++pos;
}

static bool expect(const std::string& s, size_t& pos, const char* expected, std::string& err) {
    size_t len = std::strlen(expected);
    if (pos + len > s.size() || s.substr(pos, len) != expected) {
        err = std::string("expected '") + expected + "' at position " + std::to_string(pos);
        return false;
    }
    pos += len;
    return true;
}

static bool parseString(const std::string& s, size_t& pos, std::string& out, std::string& err) {
    skipWs(s, pos);
    if (pos >= s.size() || s[pos] != '"') {
        err = "expected '\"' at position " + std::to_string(pos);
        return false;
    }
    ++pos;
    out.clear();
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\') {
            ++pos;
            if (pos >= s.size()) { err = "unexpected end in string escape"; return false; }
            switch (s[pos]) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                default:   out += s[pos]; break;
            }
        } else {
            out += s[pos];
        }
        ++pos;
    }
    if (pos >= s.size()) { err = "unterminated string"; return false; }
    ++pos;
    return true;
}

static bool parseInt64(const std::string& s, size_t& pos, int64_t& out, std::string& err) {
    skipWs(s, pos);
    if (pos >= s.size()) { err = "unexpected end of input parsing integer"; return false; }
    bool neg = false;
    if (s[pos] == '-') { neg = true; ++pos; }
    if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) {
        err = "expected digit at position " + std::to_string(pos);
        return false;
    }
    int64_t v = 0;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
        v = v * 10 + (s[pos] - '0');
        ++pos;
    }
    out = neg ? -v : v;
    return true;
}

static bool parseFloat(const std::string& s, size_t& pos, float& out, std::string& err) {
    skipWs(s, pos);
    size_t start = pos;
    if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) ++pos;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    if (pos < s.size() && s[pos] == '.') {
        ++pos;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    }
    if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
        ++pos;
        if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    }
    if (start == pos) { err = "expected float at position " + std::to_string(pos); return false; }
    try {
        out = std::stof(s.substr(start, pos - start));
    } catch (...) {
        err = "invalid float at position " + std::to_string(start);
        return false;
    }
    return true;
}

static bool parseBool(const std::string& s, size_t& pos, bool& out, std::string& err) {
    skipWs(s, pos);
    if (pos + 4 <= s.size() && s.substr(pos, 4) == "true") {
        out = true; pos += 4; return true;
    }
    if (pos + 5 <= s.size() && s.substr(pos, 5) == "false") {
        out = false; pos += 5; return true;
    }
    err = "expected 'true' or 'false' at position " + std::to_string(pos);
    return false;
}

static bool parseKey(const std::string& s, size_t& pos, std::string& key, std::string& err) {
    skipWs(s, pos);
    if (!parseString(s, pos, key, err)) return false;
    skipWs(s, pos);
    return expect(s, pos, ":", err);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// deserializeFromJson
// ---------------------------------------------------------------------------
bool CitySimulation::deserializeFromJson(const std::string& json, std::string& errorOut) {
    size_t pos = 0;
    skipWs(json, pos);
    if (!expect(json, pos, "{", errorOut)) return false;

    bool gotVersion = false;
    bool gotTreasury = false;
    bool gotTaxRates = false;
    bool gotDebt = false;
    bool gotBondUses = false;
    bool gotDeficitMonths = false;
    bool gotSpeed = false;
    bool gotMilestones = false;
    bool gotVariantCounters = false;
    bool gotTiles = false;
    bool gotServiceBuildings = false;
    bool gotUnlockFlags = false;
    bool gotUnlockCounter = false;
    bool gotTotalTicks = false;
    bool gotMonth = false;
    bool gotYear = false;
    bool gotScenario = false;

    int newMapTilesX = m_mapWidth;
    int newMapTilesZ = m_mapHeight;
    int64_t newTreasury = 0;
    float   newTaxRates[3] = {0.05f, 0.05f, 0.05f};
    int     newOutstandingBondUses = 0;
    int     newConsecutiveDeficitMonths = 0;
    SpeedMultiplier newSpeed = SpeedMultiplier::x3;
    bool    newMilestoneFired[5] = {};
    std::array<int,9> newVariantCounters{};
    std::vector<std::pair<int64_t, TileData>> newTiles;
    std::vector<ServiceBuilding> newServiceBuildings;
    DensityUnlockState newDensityUnlock{};
    int     newTotalTicks = 0;
    int     newMonth = 1;
    int     newYear = 1;
    ScenarioState newScenario{};

    skipWs(json, pos);
    while (pos < json.size() && json[pos] != '}') {
        std::string key;
        if (!parseKey(json, pos, key, errorOut)) return false;
        skipWs(json, pos);

        if (key == "schema_version") {
            int64_t v = 0;
            if (!parseInt64(json, pos, v, errorOut)) return false;
            if (v != 1) { errorOut = "unsupported schema_version: " + std::to_string(v); return false; }
            gotVersion = true;

        } else if (key == "map_tiles_x") {
            int64_t v = 0;
            if (!parseInt64(json, pos, v, errorOut)) return false;
            newMapTilesX = static_cast<int>(v);

        } else if (key == "map_tiles_z") {
            int64_t v = 0;
            if (!parseInt64(json, pos, v, errorOut)) return false;
            newMapTilesZ = static_cast<int>(v);

        } else if (key == "treasury_balance") {
            if (!parseInt64(json, pos, newTreasury, errorOut)) return false;
            gotTreasury = true;

        } else if (key == "tax_rates") {
            skipWs(json, pos);
            if (!expect(json, pos, "[", errorOut)) return false;
            for (int i = 0; i < 3; ++i) {
                skipWs(json, pos);
                if (!parseFloat(json, pos, newTaxRates[i], errorOut)) return false;
                skipWs(json, pos);
                if (i < 2) { if (!expect(json, pos, ",", errorOut)) return false; }
            }
            skipWs(json, pos);
            if (!expect(json, pos, "]", errorOut)) return false;
            gotTaxRates = true;

        } else if (key == "outstanding_debt") {
            float dummy = 0.0f;
            if (!parseFloat(json, pos, dummy, errorOut)) return false;
            gotDebt = true;

        } else if (key == "outstanding_bond_uses") {
            int64_t v = 0;
            if (!parseInt64(json, pos, v, errorOut)) return false;
            newOutstandingBondUses = static_cast<int>(v);
            gotBondUses = true;

        } else if (key == "consecutive_deficit_months") {
            int64_t v = 0;
            if (!parseInt64(json, pos, v, errorOut)) return false;
            newConsecutiveDeficitMonths = static_cast<int>(v);
            gotDeficitMonths = true;

        } else if (key == "speed_multiplier") {
            int64_t v = 0;
            if (!parseInt64(json, pos, v, errorOut)) return false;
            switch (v) {
                case 0: newSpeed = SpeedMultiplier::Paused; break;
                case 1: newSpeed = SpeedMultiplier::x1;    break;
                case 2: newSpeed = SpeedMultiplier::x3;    break;
                case 3: newSpeed = SpeedMultiplier::x10;   break;
                default:
                    errorOut = "invalid speed_multiplier value: " + std::to_string(v);
                    return false;
            }
            gotSpeed = true;

        } else if (key == "population_milestone_fired") {
            skipWs(json, pos);
            if (!expect(json, pos, "[", errorOut)) return false;
            for (int i = 0; i < 5; ++i) {
                skipWs(json, pos);
                if (!parseBool(json, pos, newMilestoneFired[i], errorOut)) return false;
                skipWs(json, pos);
                if (i < 4) { if (!expect(json, pos, ",", errorOut)) return false; }
            }
            skipWs(json, pos);
            if (!expect(json, pos, "]", errorOut)) return false;
            gotMilestones = true;

        } else if (key == "building_variant_counters") {
            skipWs(json, pos);
            if (!expect(json, pos, "[", errorOut)) return false;
            for (int i = 0; i < 9; ++i) {
                skipWs(json, pos);
                int64_t v = 0;
                if (!parseInt64(json, pos, v, errorOut)) return false;
                newVariantCounters[i] = static_cast<int>(v);
                skipWs(json, pos);
                if (i < 8) { if (!expect(json, pos, ",", errorOut)) return false; }
            }
            skipWs(json, pos);
            if (!expect(json, pos, "]", errorOut)) return false;
            gotVariantCounters = true;

        } else if (key == "tiles") {
            skipWs(json, pos);
            if (!expect(json, pos, "[", errorOut)) return false;
            skipWs(json, pos);
            while (pos < json.size() && json[pos] != ']') {
                skipWs(json, pos);
                if (!expect(json, pos, "{", errorOut)) return false;
                int tileX = 0, tileZ = 0;
                TileData td{};
                bool first = true;
                skipWs(json, pos);
                while (pos < json.size() && json[pos] != '}') {
                    if (!first) {
                        skipWs(json, pos);
                        if (json[pos] == ',') { ++pos; skipWs(json, pos); }
                    }
                    first = false;
                    std::string tk;
                    if (!parseKey(json, pos, tk, errorOut)) return false;
                    skipWs(json, pos);
                    if (tk == "x") {
                        int64_t v = 0; if (!parseInt64(json, pos, v, errorOut)) return false; tileX = static_cast<int>(v);
                    } else if (tk == "z") {
                        int64_t v = 0; if (!parseInt64(json, pos, v, errorOut)) return false; tileZ = static_cast<int>(v);
                    } else if (tk == "zone") {
                        int64_t v = 0; if (!parseInt64(json, pos, v, errorOut)) return false;
                        if (v < 0 || v > 2) { errorOut = "invalid zone value"; return false; }
                        td.zone = static_cast<ZoneType>(v);
                    } else if (tk == "tier") {
                        int64_t v = 0; if (!parseInt64(json, pos, v, errorOut)) return false;
                        if (v < 0 || v > 2) { errorOut = "invalid tier value"; return false; }
                        td.density = static_cast<DensityTier>(v);
                    } else if (tk == "is_zoned") {
                        if (!parseBool(json, pos, td.isZoned, errorOut)) return false;
                    } else if (tk == "is_road") {
                        if (!parseBool(json, pos, td.isRoad, errorOut)) return false;
                    } else if (tk == "population") {
                        if (!parseFloat(json, pos, td.population, errorOut)) return false;
                    } else if (tk == "alert_fired") {
                        if (!parseBool(json, pos, td.alertFired, errorOut)) return false;
                    } else if (tk == "under_construction") {
                        if (!parseBool(json, pos, td.underConstruction, errorOut)) return false;
                    } else if (tk == "variant_num") {
                        int64_t v = 0;
                        if (!parseInt64(json, pos, v, errorOut)) return false;
                        td.buildingVariantNum = static_cast<int>(v);
                    } else if (tk == "fp_origin_x") {
                        int64_t v = 0;
                        if (!parseInt64(json, pos, v, errorOut)) return false;
                        td.footprintOriginX = static_cast<int>(v);
                    } else if (tk == "fp_origin_z") {
                        int64_t v = 0;
                        if (!parseInt64(json, pos, v, errorOut)) return false;
                        td.footprintOriginZ = static_cast<int>(v);
                    } else {
                        skipWs(json, pos);
                        if (json[pos] == '"') {
                            std::string dummy; if (!parseString(json, pos, dummy, errorOut)) return false;
                        } else {
                            while (pos < json.size() && json[pos] != ',' && json[pos] != '}') ++pos;
                        }
                    }
                    skipWs(json, pos);
                }
                if (!expect(json, pos, "}", errorOut)) return false;
                newTiles.emplace_back(Zoning::tileKey(tileX, tileZ), td);
                skipWs(json, pos);
                if (pos < json.size() && json[pos] == ',') { ++pos; skipWs(json, pos); }
            }
            if (!expect(json, pos, "]", errorOut)) return false;
            gotTiles = true;

        } else if (key == "service_buildings") {
            skipWs(json, pos);
            if (!expect(json, pos, "[", errorOut)) return false;
            skipWs(json, pos);
            while (pos < json.size() && json[pos] != ']') {
                skipWs(json, pos);
                if (!expect(json, pos, "{", errorOut)) return false;
                ServiceBuilding sb{};
                bool first = true;
                skipWs(json, pos);
                while (pos < json.size() && json[pos] != '}') {
                    if (!first) {
                        skipWs(json, pos);
                        if (json[pos] == ',') { ++pos; skipWs(json, pos); }
                    }
                    first = false;
                    std::string sk;
                    if (!parseKey(json, pos, sk, errorOut)) return false;
                    skipWs(json, pos);
                    if (sk == "x") {
                        int64_t v = 0; if (!parseInt64(json, pos, v, errorOut)) return false; sb.x = static_cast<int>(v);
                    } else if (sk == "z") {
                        int64_t v = 0; if (!parseInt64(json, pos, v, errorOut)) return false; sb.z = static_cast<int>(v);
                    } else if (sk == "type") {
                        int64_t v = 0; if (!parseInt64(json, pos, v, errorOut)) return false;
                        if (v < 0 || v > 3) { errorOut = "invalid service building type"; return false; }
                        sb.type = static_cast<ServiceBuildingType>(v);
                    } else if (sk == "degraded") {
                        if (!parseBool(json, pos, sb.degraded, errorOut)) return false;
                    } else {
                        skipWs(json, pos);
                        if (json[pos] == '"') {
                            std::string dummy; if (!parseString(json, pos, dummy, errorOut)) return false;
                        } else {
                            while (pos < json.size() && json[pos] != ',' && json[pos] != '}') ++pos;
                        }
                    }
                    skipWs(json, pos);
                }
                if (!expect(json, pos, "}", errorOut)) return false;
                newServiceBuildings.push_back(sb);
                skipWs(json, pos);
                if (pos < json.size() && json[pos] == ',') { ++pos; skipWs(json, pos); }
            }
            if (!expect(json, pos, "]", errorOut)) return false;
            gotServiceBuildings = true;

        } else if (key == "density_unlock_flags") {
            skipWs(json, pos);
            if (!expect(json, pos, "[", errorOut)) return false;
            for (int i = 0; i < 6; ++i) {
                skipWs(json, pos);
                if (!parseBool(json, pos, newDensityUnlock.unlock_flags[i], errorOut)) return false;
                skipWs(json, pos);
                if (i < 5) { if (!expect(json, pos, ",", errorOut)) return false; }
            }
            skipWs(json, pos);
            if (!expect(json, pos, "]", errorOut)) return false;
            gotUnlockFlags = true;

        } else if (key == "density_unlock_revenue_counter") {
            skipWs(json, pos);
            if (!expect(json, pos, "[", errorOut)) return false;
            for (int i = 0; i < 6; ++i) {
                skipWs(json, pos);
                int64_t v = 0;
                if (!parseInt64(json, pos, v, errorOut)) return false;
                newDensityUnlock.consecutive_months_above_threshold[i] = static_cast<int>(v);
                skipWs(json, pos);
                if (i < 5) { if (!expect(json, pos, ",", errorOut)) return false; }
            }
            skipWs(json, pos);
            if (!expect(json, pos, "]", errorOut)) return false;
            gotUnlockCounter = true;

        } else if (key == "total_ticks") {
            int64_t v = 0; if (!parseInt64(json, pos, v, errorOut)) return false;
            newTotalTicks = static_cast<int>(v);
            gotTotalTicks = true;

        } else if (key == "month") {
            int64_t v = 0; if (!parseInt64(json, pos, v, errorOut)) return false;
            if (v < 1 || v > 12) { errorOut = "month out of range: " + std::to_string(v); return false; }
            newMonth = static_cast<int>(v);
            gotMonth = true;

        } else if (key == "year") {
            int64_t v = 0; if (!parseInt64(json, pos, v, errorOut)) return false;
            newYear = static_cast<int>(v);
            gotYear = true;

        } else if (key == "scenario_state") {
            skipWs(json, pos);
            if (!expect(json, pos, "{", errorOut)) return false;
            bool first = true;
            skipWs(json, pos);
            while (pos < json.size() && json[pos] != '}') {
                if (!first) {
                    skipWs(json, pos);
                    if (json[pos] == ',') { ++pos; skipWs(json, pos); }
                }
                first = false;
                std::string sk;
                if (!parseKey(json, pos, sk, errorOut)) return false;
                skipWs(json, pos);
                if (sk == "win_condition_progress") {
                    if (!parseFloat(json, pos, newScenario.win_condition_progress, errorOut)) return false;
                } else if (sk == "elapsed_ticks") {
                    int64_t v = 0; if (!parseInt64(json, pos, v, errorOut)) return false;
                    newScenario.elapsed_ticks = static_cast<int>(v);
                } else if (sk == "scenario_id") {
                    if (!parseString(json, pos, newScenario.scenario_id, errorOut)) return false;
                } else {
                    skipWs(json, pos);
                    if (json[pos] == '"') {
                        std::string dummy; if (!parseString(json, pos, dummy, errorOut)) return false;
                    } else {
                        while (pos < json.size() && json[pos] != ',' && json[pos] != '}') ++pos;
                    }
                }
                skipWs(json, pos);
            }
            if (!expect(json, pos, "}", errorOut)) return false;
            gotScenario = true;

        } else {
            skipWs(json, pos);
            char c = json[pos];
            if (c == '"') {
                std::string dummy; if (!parseString(json, pos, dummy, errorOut)) return false;
            } else if (c == '[' || c == '{') {
                int depth = 0;
                while (pos < json.size()) {
                    char ch = json[pos++];
                    if (ch == '[' || ch == '{') ++depth;
                    else if (ch == ']' || ch == '}') { --depth; if (depth <= 0) break; }
                    else if (ch == '"') {
                        while (pos < json.size() && json[pos] != '"') {
                            if (json[pos] == '\\') ++pos;
                            ++pos;
                        }
                        if (pos < json.size()) ++pos;
                    }
                }
            } else {
                while (pos < json.size() && json[pos] != ',' && json[pos] != '}') ++pos;
            }
        }

        skipWs(json, pos);
        if (pos < json.size() && json[pos] == ',') { ++pos; }
        skipWs(json, pos);
    }

    if (!expect(json, pos, "}", errorOut)) return false;

    if (!gotVersion)         { errorOut = "missing schema_version";               return false; }
    if (!gotTreasury)        { errorOut = "missing treasury_balance";             return false; }
    if (!gotTaxRates)        { errorOut = "missing tax_rates";                    return false; }
    if (!gotDebt)            { errorOut = "missing outstanding_debt";             return false; }
    if (!gotBondUses)        { errorOut = "missing outstanding_bond_uses";        return false; }
    if (!gotDeficitMonths)   { errorOut = "missing consecutive_deficit_months";   return false; }
    if (!gotSpeed)           { errorOut = "missing speed_multiplier";             return false; }
    if (!gotMilestones)      { errorOut = "missing population_milestone_fired";   return false; }
    if (!gotVariantCounters) { errorOut = "missing building_variant_counters";    return false; }
    if (!gotTiles)           { errorOut = "missing tiles";                        return false; }
    if (!gotServiceBuildings){ errorOut = "missing service_buildings";            return false; }
    if (!gotUnlockFlags)     { errorOut = "missing density_unlock_flags";         return false; }
    if (!gotUnlockCounter)   { errorOut = "missing density_unlock_revenue_counter"; return false; }
    if (!gotTotalTicks)      { errorOut = "missing total_ticks";                  return false; }
    if (!gotMonth)           { errorOut = "missing month";                        return false; }
    if (!gotYear)            { errorOut = "missing year";                         return false; }
    if (!gotScenario)        { errorOut = "missing scenario_state";               return false; }

    // ---- Atomically apply the deserialized state ----
    m_mapWidth               = newMapTilesX;
    m_mapHeight              = newMapTilesZ;
    m_economy.m_treasury     = newTreasury;
    m_economy.m_taxRates[0]  = newTaxRates[0];
    m_economy.m_taxRates[1]  = newTaxRates[1];
    m_economy.m_taxRates[2]  = newTaxRates[2];
    m_economy.m_outstandingBondUses = newOutstandingBondUses;
    m_population.m_consecutiveDeficitMonths = newConsecutiveDeficitMonths;
    m_timing.setSpeed(newSpeed);
    for (int i = 0; i < 5; ++i) m_population.m_milestoneFired[i] = newMilestoneFired[i];
    m_zoning.m_buildingVariantCounters = newVariantCounters;

    m_zoning.m_tiles.clear();
    m_zoning.m_roadTileCount = 0;
    for (auto& [k, td] : newTiles) {
        m_zoning.m_tiles[k] = td;
        if (td.isRoad) ++m_zoning.m_roadTileCount;
    }

    m_zoning.m_serviceBuildings = std::move(newServiceBuildings);
    m_population.m_densityUnlockState = newDensityUnlock;
    m_timing.m_totalTicks     = newTotalTicks;
    m_timing.m_month          = newMonth;
    m_timing.m_year           = newYear;
    m_scenarioState           = std::move(newScenario);

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
