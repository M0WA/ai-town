#pragma once
#include "src/ui/IUIBackend.h"                // UIElementHandle, IUIBackend
#include "src/interfaces/IClock.h"            // IClock
#include "src/interfaces/ISimulationPauser.h" // ISimulationPauser

// NotificationManager — manages toast notifications with auto-dismiss timing
// and CRITICAL-toast auto-pause injection.
// Constructor signature is locked at Phase 0 to prevent Phase 1 teams from
// defining NotificationManager(IUIBackend*) with only one parameter, breaking
// the IClock*-based auto-dismiss test infrastructure.
// Full implementation in Phase 1.
class NotificationManager {
public:
    NotificationManager(IUIBackend* backend, IClock* clock, ISimulationPauser* pauser);

    // Production API for player dismissal of CRITICAL toasts.
    // Called by the UI event handler when the player clicks, presses Enter,
    // or presses Delete on a CRITICAL toast. Not a test-only backdoor.
    void dismissCriticalToast(UIElementHandle handle);
};
