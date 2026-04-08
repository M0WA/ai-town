#pragma once
#include "simulation_constants.h"
#include "src/interfaces/simulation_types.h"

// SimTiming — sub-system owning all time-tracking state for CitySimulation.
// Extracted from CitySimulation as part of Phase 11q1 decomposition.

struct SimTiming {
    // ---- Fields ----
    float           m_accumulatedSimSeconds{0.0f};
    double          m_constructionTimeSeconds{0.0};
    int             m_totalTicks{0};
    int             m_pendingBudgetTicks{0};
    int             m_month{1};
    int             m_year{1};
    SpeedMultiplier m_speed{kDefaultSimSpeed};
    float           m_hoursAccumulator{0.0f};
    TimeOfDay       m_timeOfDay{TimeOfDay::DAY};
    bool            m_timeOfDayChanged{false};

    // ---- Methods ----
    // Returns number of budget ticks to fire this frame
    int  tick(float realDt, SpeedMultiplier speed);
    int  consumeBudgetTicks();
    SimulationTime getSimulationTime() const;
    TimeOfDay      getTimeOfDay()       const;
    void           setSpeed(SpeedMultiplier s);
    SpeedMultiplier getSpeedMultiplier() const;
    bool           isPaused()           const;
    void           setPaused(bool p);
    double         getConstructionTimeSeconds() const;
    int            getTotalTicks()      const;
    bool           hasTimeOfDayChanged() const;

    // Speed value as real multiplier (Paused=0, x1=1, x3=3, x10=10)
    static float speedValue(SpeedMultiplier s);
};
