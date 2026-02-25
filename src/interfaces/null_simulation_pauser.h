#pragma once
#include "src/interfaces/ISimulationPauser.h"

// No-op ISimulationPauser for contexts that don't need pause control.
class NullSimulationPauser : public ISimulationPauser {
public:
    void setPaused(bool /*paused*/) override {}
};
