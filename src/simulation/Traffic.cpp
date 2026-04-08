// Traffic.cpp — traffic sub-system for CitySimulation.
// Extracted verbatim from CitySimulation.cpp (Phase 11q1 decomposition).

#include "Traffic.h"
#include "Zoning.h"
#include "src/interfaces/IRenderer.h"
#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/IClock.h"
#include "src/interfaces/ISimulationRNG.h"
#include "src/interfaces/sound_ids.h"

#include <algorithm>
#include <cmath>
#include <vector>

// kPiF — pi as float (C++17 compatible).
static constexpr float kPiF = 3.14159265f;

// kDespawnedVehicleTile — sentinel tile coordinate for despawned vehicles.
static constexpr int kDespawnedVehicleTile = -9999;

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

float Traffic::getZoneDemandFactor(ZoneType zone) const {
    return m_demandPressurePct[static_cast<int>(zone)];
}

float Traffic::getTrafficDemandFactor(ZoneType zone) const {
    switch (zone) {
        case ZoneType::Residential: return m_trafficDemandFactorR;
        case ZoneType::Commercial:  return m_trafficDemandFactorC;
        case ZoneType::Industrial:  return m_trafficDemandFactorI;
    }
    return SimulationConstants::null_path_demand_default;
}

float Traffic::getRoadSpeedFraction() const {
    return m_roadSpeedFraction;
}

std::vector<AgentState> Traffic::getAgentPositions() const {
    std::vector<AgentState> agents;
    agents.reserve(m_trafficVehicles.size());
    for (const TrafficVehicle& v : m_trafficVehicles) {
        AgentState state;
        state.agentId    = v.id;
        state.tileX      = v.srcX;
        state.tileZ      = v.srcZ;
        state.headingDeg = v.headingDeg;
        state.zone       = v.zone;
        state.worldX     = v.worldX;
        state.worldZ     = v.worldZ;
        agents.push_back(state);
    }
    return agents;
}

std::vector<IntersectionSignalState> Traffic::getIntersectionSignalStates() const {
    std::vector<IntersectionSignalState> states;
    states.reserve(m_trafficSignals.size());
    for (const TrafficSignal& sig : m_trafficSignals) {
        IntersectionSignalState iss;
        iss.tileX = sig.tileX;
        iss.tileZ = sig.tileZ;
        iss.phase = (sig.phaseTimer < sig.phaseSeconds * 0.5f)
                    ? SignalPhase::Green
                    : SignalPhase::Red;
        states.push_back(iss);
    }
    return states;
}

std::vector<RoadSegmentSpeed> Traffic::getRoadSegmentSpeeds(const Zoning& zoning) const {
    std::vector<RoadSegmentSpeed> speeds;
    speeds.reserve(m_trafficSignals.size() + 8);
    for (const auto& kv : zoning.tiles()) {
        if (!kv.second.isRoad) continue;
        int tx = static_cast<int>(static_cast<int64_t>(kv.first) >> 32);
        int tz = static_cast<int>(static_cast<uint32_t>(kv.first & 0xFFFFFFFFLL));
        RoadSegmentSpeed rss;
        rss.tileX = tx;
        rss.tileZ = tz;
        rss.speedFraction = 1.0f;
        for (const TrafficSignal& sig : m_trafficSignals) {
            if (sig.tileX == tx && sig.tileZ == tz) {
                bool isGreen = (sig.phaseTimer < sig.phaseSeconds * 0.5f);
                rss.speedFraction = isGreen ? 1.0f : 0.35f;
                break;
            }
        }
        speeds.push_back(rss);
    }
    return speeds;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

/*static*/ float Traffic::smoothstep(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/*static*/ float Traffic::travelTimeDemand(float t, float fullTime, float zeroTime) {
    if (t <= fullTime) return 1.0f;
    if (t >= zeroTime) return 0.0f;
    float x = (zeroTime - t) / (zeroTime - fullTime);
    return smoothstep(x);
}

/*static*/ int Traffic::maxPopulationForTile(ZoneType zone, DensityTier density) {
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
// resetTrafficWindows
// ---------------------------------------------------------------------------

void Traffic::resetTrafficWindows() {
    for (int i = 0; i < SimulationConstants::traffic_rolling_window_r_c; ++i) {
        m_trafficWindowR[i] = 0.0f;
        m_trafficWindowC[i] = 0.0f;
    }
    for (int i = 0; i < SimulationConstants::traffic_rolling_window_i; ++i) {
        m_trafficWindowI[i] = 0.0f;
    }
    m_trafficDemandFactorR = SimulationConstants::null_path_demand_default;
    m_trafficDemandFactorC = SimulationConstants::null_path_demand_default;
    m_trafficDemandFactorI = SimulationConstants::null_path_demand_default;
}

// ---------------------------------------------------------------------------
// reset
// ---------------------------------------------------------------------------

void Traffic::reset(IAudioSystem* audio) {
    for (auto& agent : m_trafficVehicles) {
        if (audio) {
            audio->releaseVehicleEnginePair(agent.idleIdx, agent.moveIdx);
        }
    }
    m_trafficVehicles.clear();
    m_trafficSignals.clear();
    m_nextVehicleId = 1;
    m_trafficWindowIdxRC = 0;
    m_trafficWindowIdxI  = 0;
    resetTrafficWindows();
    m_roadSpeedFraction = 1.0f;
    m_demandPressurePct.fill(0.0f);
}

// ---------------------------------------------------------------------------
// addSignalForTile — extracted from CitySimulation::placeRoad()
// ---------------------------------------------------------------------------

void Traffic::addSignalForTile(int tileX, int tileZ, const Zoning& zoning) {
    const int neighbors[4][2] = {{tileX-1,tileZ},{tileX+1,tileZ},{tileX,tileZ-1},{tileX,tileZ+1}};

    auto countRoadNeighbors = [&](int cx, int cz) -> int {
        const int nbs[4][2] = {{cx-1,cz},{cx+1,cz},{cx,cz-1},{cx,cz+1}};
        int count = 0;
        for (auto& nb2 : nbs) {
            const auto* td = zoning.findTile(nb2[0], nb2[1]);
            if (td && td->isRoad) ++count;
        }
        return count;
    };

    auto hasSignal = [&](int cx, int cz) -> bool {
        for (const TrafficSignal& sig : m_trafficSignals) {
            if (sig.tileX == cx && sig.tileZ == cz) return true;
        }
        return false;
    };

    auto phaseOffset = [&](int cx, int cz) -> float {
        unsigned int seed = (static_cast<unsigned int>(cx) * 73856093u)
                          ^ (static_cast<unsigned int>(cz) * 19349663u);
        return (static_cast<float>(seed & 0xFFFFu) / 65535.0f) *
               SimulationConstants::traffic_signal_phase_seconds;
    };

    if (countRoadNeighbors(tileX, tileZ) >= 2 && !hasSignal(tileX, tileZ)) {
        TrafficSignal sig;
        sig.tileX      = tileX;
        sig.tileZ      = tileZ;
        sig.phaseTimer = phaseOffset(tileX, tileZ);
        m_trafficSignals.push_back(sig);
    }

    for (auto& nb : neighbors) {
        const auto* td = zoning.findTile(nb[0], nb[1]);
        if (!td || !td->isRoad) continue;
        if (countRoadNeighbors(nb[0], nb[1]) >= 2 && !hasSignal(nb[0], nb[1])) {
            TrafficSignal sig;
            sig.tileX      = nb[0];
            sig.tileZ      = nb[1];
            sig.phaseTimer = phaseOffset(nb[0], nb[1]);
            m_trafficSignals.push_back(sig);
        }
    }
}

// ---------------------------------------------------------------------------
// removeSignalForTile — extracted from CitySimulation::demolishTile()
// ---------------------------------------------------------------------------

void Traffic::removeSignalForTile(int tileX, int tileZ) {
    m_trafficSignals.erase(
        std::remove_if(m_trafficSignals.begin(), m_trafficSignals.end(),
            [tileX, tileZ](const TrafficSignal& sig) {
                return sig.tileX == tileX && sig.tileZ == tileZ;
            }),
        m_trafficSignals.end());
}

// ---------------------------------------------------------------------------
// spawnVehiclesForRoad — extracted from CitySimulation::placeRoad()
// ---------------------------------------------------------------------------

void Traffic::spawnVehiclesForRoad(int tileX, int tileZ, int roadTileCount,
                                   const Zoning& zoning,
                                   IAudioSystem* /*audio*/, ISimulationRNG* rng) {
    if (roadTileCount % SimulationConstants::vehicle_spawn_interval != 0) return;

    const int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    int dstX = tileX, dstZ = tileZ;
    for (auto& d : dirs) {
        int nx = tileX + d[0];
        int nz = tileZ + d[1];
        const auto* nd = zoning.findTile(nx, nz);
        if (nd && nd->isRoad) { dstX = nx; dstZ = nz; break; }
    }
    if (dstX == tileX && dstZ == tileZ) return;

    TrafficVehicle v;
    v.id = m_nextVehicleId++;
    v.srcX = tileX; v.srcZ = tileZ;
    v.dstX = dstX;  v.dstZ = dstZ;
    v.progress = 0.0f;
    float dx = static_cast<float>(dstX - tileX);
    float dz = static_cast<float>(dstZ - tileZ);
    v.headingDeg = std::atan2(dx, dz) * (180.0f / kPiF);
    v.worldX = (static_cast<float>(tileX) + 0.5f) * kTileSizeMeters;
    v.worldZ = (static_cast<float>(tileZ) + 0.5f) * kTileSizeMeters;

    ZoneType spawnZone = ZoneType::Residential;
    const auto* dst = zoning.findTile(v.dstX, v.dstZ);
    if (dst && dst->isZoned) {
        spawnZone = dst->zone;
    } else {
        const int roll = rng->nextInt(0, 99);
        if (roll < 70)       spawnZone = ZoneType::Residential;
        else if (roll < 90)  spawnZone = ZoneType::Commercial;
        else                 spawnZone = ZoneType::Industrial;
    }
    v.zone = spawnZone;
    m_trafficVehicles.push_back(v);
}

// ---------------------------------------------------------------------------
// removeVehiclesForRoad — not currently used (vehicles despawn lazily)
// but provided for symmetry with addSignalForTile/removeSignalForTile.
// ---------------------------------------------------------------------------

void Traffic::removeVehiclesForRoad(int /*x*/, int /*z*/, IAudioSystem* /*audio*/) {
    // Vehicles are despawned lazily in doTrafficVehicleTick() when their
    // destination tile is no longer a road. No immediate removal needed.
}

// ---------------------------------------------------------------------------
// computeTrafficDemand
// ---------------------------------------------------------------------------

void Traffic::computeTrafficDemand(const Zoning& zoning, int totalTicks) {
    int rAdjacentCount = 0;
    int cAdjacentCount = 0;
    int iAdjacentCount = 0;

    const int dx[] = {0, 0, -1, 1};
    const int dz[] = {-1, 1, 0, 0};

    for (auto& [key, tile] : zoning.tiles()) {
        if (!tile.isZoned) continue;

        int x = static_cast<int>(key >> 32);
        int z = static_cast<int>(static_cast<uint32_t>(key));

        bool hasRoadAdjacentTile = false;
        for (int d = 0; d < 4; ++d) {
            int nx = x + dx[d];
            int nz = z + dz[d];
            const auto* neighbor = zoning.findTile(nx, nz);
            if (neighbor && neighbor->isRoad) {
                hasRoadAdjacentTile = true;
                break;
            }
        }

        if (hasRoadAdjacentTile) {
            switch (tile.zone) {
                case ZoneType::Residential:  rAdjacentCount++; break;
                case ZoneType::Commercial:   cAdjacentCount++; break;
                case ZoneType::Industrial:   iAdjacentCount++; break;
            }
        }
    }

    int totalZonedTiles = 0;
    for (auto& [zkey, ztile] : zoning.tiles()) {
        if (ztile.isZoned) totalZonedTiles++;
    }

    int totalRoadTiles = zoning.roadTileCount();
    int roadCapacity = std::max(1, totalRoadTiles) * SimulationConstants::road_segment_capacity_per_tile;
    float trafficLoad = static_cast<float>(totalZonedTiles) / static_cast<float>(roadCapacity);

    float speedFraction = std::max(SimulationConstants::min_speed_fraction,
                                   1.0f - trafficLoad);
    speedFraction = std::min(1.0f, speedFraction);
    m_roadSpeedFraction = speedFraction;

    const float travelTime = kTileSizeMeters /
                             (SimulationConstants::road_max_speed_mps * speedFraction);

    auto computeZoneSample = [&](int adjacentCount, float fullTime, float zeroTime) -> float {
        if (adjacentCount == 0) return SimulationConstants::null_path_demand_default;
        if (trafficLoad > 1.0f) return 0.0f;
        return travelTimeDemand(travelTime, fullTime, zeroTime);
    };

    const float sampleR = computeZoneSample(rAdjacentCount, 25.0f, 60.0f);
    const float sampleC = computeZoneSample(cAdjacentCount, 30.0f, 65.0f);
    const float sampleI = computeZoneSample(iAdjacentCount, 40.0f, 80.0f);

    m_trafficWindowR[m_trafficWindowIdxRC] = sampleR;
    m_trafficWindowC[m_trafficWindowIdxRC] = sampleC;
    m_trafficWindowIdxRC = (m_trafficWindowIdxRC + 1) % SimulationConstants::traffic_rolling_window_r_c;

    m_trafficWindowI[m_trafficWindowIdxI] = sampleI;
    m_trafficWindowIdxI = (m_trafficWindowIdxI + 1) % SimulationConstants::traffic_rolling_window_i;

    int samplesRC = std::min(totalTicks, SimulationConstants::traffic_rolling_window_r_c);
    int samplesI  = std::min(totalTicks, SimulationConstants::traffic_rolling_window_i);
    samplesRC = std::max(1, samplesRC);
    samplesI  = std::max(1, samplesI);

    float sumR = 0.0f;
    for (int i = 0; i < SimulationConstants::traffic_rolling_window_r_c; ++i) sumR += m_trafficWindowR[i];
    m_trafficDemandFactorR = sumR / static_cast<float>(samplesRC);

    float sumC = 0.0f;
    for (int i = 0; i < SimulationConstants::traffic_rolling_window_r_c; ++i) sumC += m_trafficWindowC[i];
    m_trafficDemandFactorC = sumC / static_cast<float>(samplesRC);

    float sumI = 0.0f;
    for (int i = 0; i < SimulationConstants::traffic_rolling_window_i; ++i) sumI += m_trafficWindowI[i];
    m_trafficDemandFactorI = sumI / static_cast<float>(samplesI);
}

// ---------------------------------------------------------------------------
// computeEffectiveDemand
// ---------------------------------------------------------------------------

void Traffic::computeEffectiveDemand(const Zoning& zoning, int totalTicks) {
    float bootstrapR = 0.0f, bootstrapC = 0.0f, bootstrapI = 0.0f;
    if (totalTicks < SimulationConstants::demand_bootstrapping_ticks) {
        float progress = static_cast<float>(totalTicks) /
                         static_cast<float>(SimulationConstants::demand_bootstrapping_ticks);
        bootstrapR = 0.50f * (1.0f - progress);
        bootstrapC = 0.25f * (1.0f - progress);
        bootstrapI = 0.15f * (1.0f - progress);
    }

    float totalRPop = 0.0f;
    float totalCPop = 0.0f;
    float totalIPop = 0.0f;
    float totalCCapacity = 0.0f;
    float totalICapacity = 0.0f;

    for (auto& [key, tile] : zoning.tiles()) {
        if (!tile.isZoned) continue;
        switch (tile.zone) {
            case ZoneType::Residential:
                totalRPop += tile.population;
                break;
            case ZoneType::Commercial:
                totalCPop += tile.population;
                totalCCapacity += static_cast<float>(maxPopulationForTile(tile.zone, tile.density));
                break;
            case ZoneType::Industrial:
                totalIPop += tile.population;
                totalICapacity += static_cast<float>(maxPopulationForTile(tile.zone, tile.density));
                break;
        }
    }

    float totalCIWorkerCapacity = totalCCapacity + totalICapacity;
    float R_demand = std::min(1.0f, totalCIWorkerCapacity / std::max(1.0f, totalRPop));
    float C_demand = std::min(1.0f, totalRPop / std::max(1.0f, totalCCapacity));

    float I_demand;
    if (totalICapacity == 0.0f) {
        I_demand = 1.0f;
    } else {
        float rRawDemand = totalRPop * SimulationConstants::R_raw_material_rate;
        float cGoodsDemand = totalCPop * SimulationConstants::C_goods_consumption_rate;
        I_demand = std::min(1.0f, (rRawDemand + cGoodsDemand) / std::max(1.0f, totalICapacity));
    }

    float effectiveR = std::min(1.0f, std::max(0.0f,
        m_trafficDemandFactorR + bootstrapR));
    float effectiveC = std::min(1.0f, std::max(0.0f,
        m_trafficDemandFactorC * C_demand + bootstrapC));
    float effectiveI;
    if (totalICapacity == 0.0f) {
        effectiveI = 1.0f;
    } else {
        effectiveI = std::min(1.0f, std::max(0.0f,
            m_trafficDemandFactorI * I_demand + bootstrapI));
    }

    if (zoning.roadTileCount() > 0) {
        effectiveR = std::max(SimulationConstants::demand_floor_residential, effectiveR);
        effectiveC = std::max(SimulationConstants::demand_floor_commercial, effectiveC);
        effectiveI = std::max(SimulationConstants::demand_floor_industrial, effectiveI);
    }

    if (totalCIWorkerCapacity == 0.0f) {
        effectiveR = 0.0f;
    }

    m_demandPressurePct[static_cast<int>(ZoneType::Residential)] = effectiveR;
    m_demandPressurePct[static_cast<int>(ZoneType::Commercial)]  = effectiveC;
    m_demandPressurePct[static_cast<int>(ZoneType::Industrial)]  = effectiveI;
}

// ---------------------------------------------------------------------------
// doTrafficSignalTick
// ---------------------------------------------------------------------------

void Traffic::doTrafficSignalTick(float realDeltaSeconds, IRenderer* renderer,
                                  IAudioSystem* audio, IClock* /*clock*/) {
    if (!audio || m_trafficSignals.empty()) return;

    vec3 listenerPos{0.0f, 0.0f, 0.0f};
    if (renderer) {
        listenerPos = renderer->getListenerPosition();
    }

    const float cullDistSq =
        SimulationConstants::traffic_signal_cull_distance_meters *
        SimulationConstants::traffic_signal_cull_distance_meters;

    for (TrafficSignal& sig : m_trafficSignals) {
        sig.phaseTimer += realDeltaSeconds;
        if (sig.phaseTimer < sig.phaseSeconds) continue;

        sig.phaseTimer -= sig.phaseSeconds;

        vec3 signalPos{static_cast<float>(sig.tileX), 0.0f, static_cast<float>(sig.tileZ)};
        float dx = signalPos.x - listenerPos.x;
        float dz = signalPos.z - listenerPos.z;
        float distSq = dx * dx + dz * dz;
        if (distSq > cullDistSq) continue;

        audio->playPositionalSound(SFX_INTERSECTION_TICK, signalPos,
                                   SoundPriority::LOW, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// doTrafficVehicleTick
// ---------------------------------------------------------------------------

void Traffic::doTrafficVehicleTick(float realDeltaSeconds, Zoning& zoning,
                                   IRenderer* /*renderer*/, IAudioSystem* /*audio*/) {
    if (m_trafficVehicles.empty()) return;

    const float step = SimulationConstants::vehicle_tile_per_second * realDeltaSeconds;

    for (TrafficVehicle& v : m_trafficVehicles) {
        v.progress += step;
        if (v.progress >= 1.0f) {
            const auto* dst = zoning.findTile(v.dstX, v.dstZ);
            if (!dst || !dst->isRoad) {
                v.srcX = v.dstX = kDespawnedVehicleTile;
                continue;
            }
            int nextX, nextZ;
            if (!pickNextRoadTile(zoning, v.dstX, v.dstZ, v.srcX, v.srcZ, nextX, nextZ)) {
                v.srcX = v.dstX = kDespawnedVehicleTile;
                continue;
            }
            const auto* newDst = zoning.findTile(v.dstX, v.dstZ);
            if (newDst && newDst->isZoned) {
                v.zone = newDst->zone;
            }
            v.srcX = v.dstX;
            v.srcZ = v.dstZ;
            v.dstX = nextX;
            v.dstZ = nextZ;
            v.progress -= 1.0f;
            float dx = static_cast<float>(v.dstX - v.srcX);
            float dz = static_cast<float>(v.dstZ - v.srcZ);
            v.headingDeg = std::atan2(dx, dz) * (180.0f / kPiF);
        }
        float t = std::max(0.0f, std::min(1.0f, v.progress));
        v.worldX = (static_cast<float>(v.srcX) + (v.dstX - v.srcX) * t + 0.5f) * kTileSizeMeters;
        v.worldZ = (static_cast<float>(v.srcZ) + (v.dstZ - v.srcZ) * t + 0.5f) * kTileSizeMeters;
    }

    m_trafficVehicles.erase(
        std::remove_if(m_trafficVehicles.begin(), m_trafficVehicles.end(),
            [](const TrafficVehicle& v){ return v.srcX == kDespawnedVehicleTile; }),
        m_trafficVehicles.end());
}

// ---------------------------------------------------------------------------
// pickNextRoadTile
// ---------------------------------------------------------------------------

bool Traffic::pickNextRoadTile(const Zoning& zoning, int curX, int curZ, int prevX, int prevZ,
                               int& outX, int& outZ) {
    const int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    std::vector<std::pair<int,int>> candidates;
    for (const auto& d : dirs) {
        int nx = curX + d[0];
        int nz = curZ + d[1];
        if (nx == prevX && nz == prevZ) continue;
        const auto* td = zoning.findTile(nx, nz);
        if (td && td->isRoad) candidates.push_back({nx, nz});
    }
    if (candidates.empty()) {
        const auto* td = zoning.findTile(prevX, prevZ);
        if (td && td->isRoad) { outX = prevX; outZ = prevZ; return true; }
        return false;
    }
    size_t idx = static_cast<size_t>(curX * 7 + curZ * 13) % candidates.size();
    outX = candidates[idx].first;
    outZ = candidates[idx].second;
    return true;
}
