#pragma once

// Injectable pause interface for NotificationManager auto-pause injection.
// Source location: src/interfaces/ (NOT src/simulation/) to avoid the
// circular dependency src/ui/ -> src/simulation/ which is prohibited.
// MockSimulationPauser lives in tests/ui/mock_simulation_pauser.h.
class ISimulationPauser {
public:
    virtual ~ISimulationPauser() = default;
    virtual void setPaused(bool paused) = 0;
};
