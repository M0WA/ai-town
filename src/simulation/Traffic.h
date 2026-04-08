#pragma once
#include "simulation_constants.h"
#include "src/interfaces/simulation_types.h"
#include "src/interfaces/vec3.h"

#include <array>
#include <vector>

// Forward declarations for cross-system references
class Zoning;
class IRenderer;
class IAudioSystem;
class IClock;
class ISimulationRNG;

// TrafficSignal — Phase 10 data structure for sfx_intersection_tick wiring.
// Moved from CitySimulation.h (Phase 11q1 decomposition).
struct TrafficSignal {
    int   tileX{0};
    int   tileZ{0};
    float phaseTimer{0.0f};
    float phaseSeconds{SimulationConstants::traffic_signal_phase_seconds};
};

// TrafficVehicle — Phase 11d path-following vehicle agent.
// Moved from CitySimulation.h (Phase 11q1 decomposition).
struct TrafficVehicle {
    uint32_t  id{0};
    int       srcX{0}, srcZ{0};
    int       dstX{0}, dstZ{0};
    float     progress{0.0f};
    float     headingDeg{0.0f};
    ZoneType  zone{ZoneType::Residential};
    float     worldX{0.0f};
    float     worldZ{0.0f};
    int idleIdx{-1};
    int moveIdx{-1};
};

// Traffic — sub-system owning all traffic state for CitySimulation.
// Extracted from CitySimulation as part of Phase 11q1 decomposition.
class Traffic {
public:
    // ---- Fields ----
    std::vector<TrafficVehicle> m_trafficVehicles;
    uint32_t                    m_nextVehicleId{1};
    std::vector<TrafficSignal>  m_trafficSignals;

    float m_trafficWindowR[SimulationConstants::traffic_rolling_window_r_c]{};
    float m_trafficWindowC[SimulationConstants::traffic_rolling_window_r_c]{};
    float m_trafficWindowI[SimulationConstants::traffic_rolling_window_i]{};
    int   m_trafficWindowIdxRC{0};
    int   m_trafficWindowIdxI{0};

    float m_trafficDemandFactorR{SimulationConstants::null_path_demand_default};
    float m_trafficDemandFactorC{SimulationConstants::null_path_demand_default};
    float m_trafficDemandFactorI{SimulationConstants::null_path_demand_default};

    float m_roadSpeedFraction{1.0f};

    std::array<float, 3> m_demandPressurePct{0.0f, 0.0f, 0.0f};

    // ---- Public accessors ----
    float getZoneDemandFactor(ZoneType zone) const;
    float getTrafficDemandFactor(ZoneType zone) const;
    float getRoadSpeedFraction() const;

    std::vector<RoadSegmentSpeed>        getRoadSegmentSpeeds(const Zoning& zoning) const;
    std::vector<AgentState>              getAgentPositions() const;
    std::vector<IntersectionSignalState> getIntersectionSignalStates() const;

    // ---- Tick methods ----
    void computeTrafficDemand(const Zoning& zoning, int totalTicks);
    void computeEffectiveDemand(const Zoning& zoning, int totalTicks);
    void doTrafficSignalTick(float realDt, IRenderer* renderer, IAudioSystem* audio, IClock* clock);
    void doTrafficVehicleTick(float realDt, Zoning& zoning, IRenderer* renderer, IAudioSystem* audio);

    // ---- Mutation methods ----
    void resetTrafficWindows();
    void reset(IAudioSystem* audio);
    void addSignalForTile(int x, int z, const Zoning& zoning);
    void removeSignalForTile(int x, int z);
    void spawnVehiclesForRoad(int x, int z, int roadTileCount, const Zoning& zoning,
                              IAudioSystem* audio, ISimulationRNG* rng);
    void removeVehiclesForRoad(int x, int z, IAudioSystem* audio);

    // ---- Tile size constant (shared with CitySimulation) ----
    static constexpr float kTileSizeMeters = 10.0f;

private:
    bool pickNextRoadTile(const Zoning& zoning, int curX, int curZ, int prevX, int prevZ,
                          int& outX, int& outZ);
    static float smoothstep(float t);
    static float travelTimeDemand(float avgTravelTimeSec, float fullTime, float zeroTime);
    static int maxPopulationForTile(ZoneType zone, DensityTier density);
};
