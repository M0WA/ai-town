#pragma once
#include "src/ui/IUIBackend.h"                // UIElementHandle, IUIBackend
#include "src/interfaces/IClock.h"            // IClock
#include "src/interfaces/ICitySimulation.h"   // ICitySimulation

// NotificationManager — manages toast notifications with auto-dismiss timing
// and CRITICAL-toast auto-pause injection.
// Constructor signature: (backend, sim, clock) — ICitySimulation* is the second
// parameter so NotificationManager can call sim->setPaused(true) for CRITICAL-toast
// auto-pause. Phase 3 corrects the Phase 0 stub which mistakenly used ISimulationPauser*.
// Full implementation in Phase 8.
class NotificationManager {
public:
    NotificationManager(IUIBackend* backend, ICitySimulation* sim, IClock* clock);

    // Production API for player dismissal of CRITICAL toasts.
    // Called by the UI event handler when the player clicks, presses Enter,
    // or presses Delete on a CRITICAL toast. Not a test-only backdoor.
    void dismissCriticalToast(UIElementHandle handle);
};
