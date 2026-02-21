// WallClock.cpp — implements WallClock::nowSeconds() using std::chrono::steady_clock.
//
// CMake target ownership: WallClock.cpp MUST be added to the aitown EXECUTABLE target,
// NOT to aitown_render, aitown_audio, or aitown_ui. Rationale: WallClock is consumed by
// multiple domains (audio, simulation) — placing it in a single-domain library creates
// an incorrect cross-domain dependency. Production main.cpp owns the WallClock instance
// and injects it as IClock* into AudioSystem and CitySimulation constructors. Test targets
// use ManualClock (header-only in src/interfaces/) and never link WallClock.cpp.
//
// IMPORTANT: The raw delta from nowSeconds() MUST NEVER be pre-multiplied by a speed
// multiplier at the call site or main loop level. CitySimulation::tick() owns all
// speed-scaling internally per architecture/game-design/simulation-time.md.
#include "src/interfaces/WallClock.h"
#include <chrono>

double WallClock::nowSeconds() const {
    using clock = std::chrono::steady_clock;
    using duration = std::chrono::duration<double>;
    static const clock::time_point s_epoch = clock::now();
    return std::chrono::duration_cast<duration>(clock::now() - s_epoch).count();
}
