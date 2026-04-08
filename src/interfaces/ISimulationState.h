#pragma once
#include "simulation_types.h"
#include <vector>

class ISimulationState {
public:
    virtual ~ISimulationState() = default;
    virtual std::vector<AgentState>
        getAgentPositions()           const = 0;
    virtual std::vector<IntersectionSignalState>
        getIntersectionSignalStates() const = 0;
    virtual std::vector<RoadSegmentSpeed>
        getRoadSegmentSpeeds()        const = 0;
    virtual std::vector<ServiceCoverageTile>
        getServiceCoverage()          const = 0;
    virtual int       getMapTilesX()  const = 0;
    virtual int       getMapTilesZ()  const = 0;
    virtual SimulationTime getSimulationTime() const = 0;
    virtual TimeOfDay getTimeOfDay()  const = 0;
    virtual float getNextUnlockThreshold(Difficulty d) const = 0;

    // SEMANTIC DISTINCTION — getZoneDemandFactor vs getTrafficDemandFactor:
    //   getZoneDemandFactor(ZoneType)  -> float [0.0, 1.0]
    //     The post-combination, post-floor, post-bootstrap AGGREGATE effective demand for the
    //     given zone type across all tiles. This is what the HUD demand bars display.
    //
    // INVERSE SEMANTICS WARNING vs QueryResult::demandPressurePct:
    //   getZoneDemandFactor(zone)           -> [0.0, 1.0]  1.0 = maximum EFFECTIVE demand
    //   QueryResult::demandPressurePct       -> [0, 100]    100 = ZERO effective demand (fully unmet)
    // See simulation_types.h QueryResult::demandPressurePct for the canonical formula.
    virtual float getZoneDemandFactor(ZoneType zone) const = 0;

    virtual float       getTrafficDemandFactor(ZoneType zone) const = 0;
    virtual int         getTotalPopulation()         const = 0;
    virtual CityRatingTier getCityRating()           const = 0;
    virtual int         getConsecutiveDeficitMonths() const = 0;
    virtual DensityUnlockState getDensityUnlockState() const = 0;
};
