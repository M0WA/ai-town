// SimTiming.cpp — time-tracking sub-system for CitySimulation.
// Extracted verbatim from CitySimulation.cpp (Phase 11q1 decomposition).

#include "SimTiming.h"

#include <cmath>

// ---------------------------------------------------------------------------
// speedValue
// ---------------------------------------------------------------------------

/*static*/ float SimTiming::speedValue(SpeedMultiplier s) {
    switch (s) {
        case SpeedMultiplier::Paused: return 0.0f;
        case SpeedMultiplier::x1:    return 1.0f;
        case SpeedMultiplier::x3:    return 3.0f;
        case SpeedMultiplier::x10:   return 10.0f;
    }
    return 0.0f;
}

// ---------------------------------------------------------------------------
// tick — accumulate sim seconds, fire budget ticks, advance time-of-day.
// Returns the number of budget ticks that fired this frame.
// ---------------------------------------------------------------------------

int SimTiming::tick(float realDt, SpeedMultiplier speed) {
    m_timeOfDayChanged = false;

    if (speed == SpeedMultiplier::Paused) {
        return 0;
    }

    float sv = speedValue(speed);
    m_accumulatedSimSeconds += realDt * sv;

    int budgetTicks = 0;

    while (m_accumulatedSimSeconds >= SimulationConstants::SECONDS_PER_BUDGET_TICK) {
        m_accumulatedSimSeconds -= SimulationConstants::SECONDS_PER_BUDGET_TICK;
        m_totalTicks++;
        m_pendingBudgetTicks++;
        budgetTicks++;

        // Advance in-game month/year
        m_month++;
        if (m_month > 12) {
            m_month = 1;
            m_year++;
        }

        // Advance in-game hours accumulator
        m_hoursAccumulator += 720.0f;
        float dayHours = std::fmod(m_hoursAccumulator, 24.0f);

        struct TimeWindow { float upperHour; TimeOfDay period; };
        static constexpr TimeWindow kTimeWindows[] = {
            {  4.0f, TimeOfDay::NIGHT },
            {  6.0f, TimeOfDay::DAWN  },
            { 18.0f, TimeOfDay::DAY   },
            { 20.0f, TimeOfDay::DUSK  },
            { 24.0f, TimeOfDay::NIGHT },
        };
        TimeOfDay prevTimeOfDay = m_timeOfDay;
        for (const TimeWindow& tw : kTimeWindows) {
            if (dayHours < tw.upperHour) {
                m_timeOfDay = tw.period;
                break;
            }
        }

        if (m_timeOfDay != prevTimeOfDay) {
            m_timeOfDayChanged = true;
        }
    }

    return budgetTicks;
}

// ---------------------------------------------------------------------------
// consumeBudgetTicks
// ---------------------------------------------------------------------------

int SimTiming::consumeBudgetTicks() {
    int n = m_pendingBudgetTicks;
    m_pendingBudgetTicks = 0;
    return n;
}

// ---------------------------------------------------------------------------
// Trivial accessors
// ---------------------------------------------------------------------------

SimulationTime SimTiming::getSimulationTime() const {
    return SimulationTime{m_year, m_month};
}

TimeOfDay SimTiming::getTimeOfDay() const {
    return m_timeOfDay;
}

void SimTiming::setSpeed(SpeedMultiplier s) {
    m_speed = s;
}

SpeedMultiplier SimTiming::getSpeedMultiplier() const {
    return m_speed;
}

bool SimTiming::isPaused() const {
    return m_speed == SpeedMultiplier::Paused;
}

void SimTiming::setPaused(bool p) {
    if (p) {
        m_speed = SpeedMultiplier::Paused;
    } else {
        if (m_speed == SpeedMultiplier::Paused) {
            m_speed = SpeedMultiplier::x1;
        }
    }
}

double SimTiming::getConstructionTimeSeconds() const {
    return m_constructionTimeSeconds;
}

int SimTiming::getTotalTicks() const {
    return m_totalTicks;
}

bool SimTiming::hasTimeOfDayChanged() const {
    return m_timeOfDayChanged;
}
