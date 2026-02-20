#pragma once

#include "src/interfaces/ISimulationPauser.h"
#include "gmock/gmock.h"

// MockSimulationPauser — GMock implementation of ISimulationPauser.
// Used by UI tests that need to verify pause/resume calls without pulling in CitySimulation.
// Source location: tests/ui/mock_simulation_pauser.h
class MockSimulationPauser : public ISimulationPauser {
public:
    MOCK_METHOD(void, setPaused, (bool paused), (override));
};
