#pragma once
#include <string>
#include "src/ui/IUIBackend.h"                // UIElementHandle, IUIBackend
#include "src/interfaces/IClock.h"            // IClock
#include "src/interfaces/ICitySimulation.h"   // ICitySimulation

// Forward declaration for InputEvent — avoid pulling platform headers into every
// translation unit that includes NotificationManager.h.
struct InputEvent;

// NotificationManager — manages toast notifications with auto-dismiss timing
// and CRITICAL-toast auto-pause injection.
//
// Constructor signature: (backend, sim, clock) — ICitySimulation* is the second
// parameter so NotificationManager can call sim->setPaused(true) for CRITICAL-toast
// auto-pause. Phase 3 corrects the Phase 0 stub which mistakenly used ISimulationPauser*.
//
// Toast sizes (virtual 1920x1080 space):
//   CRITICAL toasts: 48 px fixed height; auto-pause game; player-dismissed via
//                    dismissCriticalToast(UIElementHandle).
//   Normal toasts:   40-63 px height; auto-dismiss after a timed duration.
//
// Log panel: 400x500 px, anchored to bell icon (bottom-right of screen).
// Full implementation in Phase 8; stub bodies in Phase 3.
class NotificationManager {
public:
    NotificationManager(IUIBackend* backend, ICitySimulation* sim, IClock* clock);

    // Production API for player dismissal of CRITICAL toasts.
    // Called by the UI event handler when the player clicks, presses Enter,
    // or presses Delete on a CRITICAL toast. Not a test-only backdoor.
    void dismissCriticalToast(UIElementHandle handle);

    // Post a CRITICAL toast (48 px, auto-pauses game, player-dismissed).
    // title and body are displayed in the toast element.
    void postCritical(const std::string& title, const std::string& body);

    // Post a normal toast (40-63 px, auto-dismiss after timed duration).
    void postNormal(const std::string& title, const std::string& body);

    // Handle an input event routed from UIManager's Priority-2 guard.
    // Returns true if the event was consumed by the notification system
    // (e.g. player dismissed a CRITICAL toast via click or key press).
    // Only called when hasCriticalToastVisible() && !modalActive (dual-guard).
    bool onEvent(const InputEvent& event);

    // Returns true when at least one CRITICAL toast is currently visible
    // and waiting for player dismissal.
    bool hasCriticalToastVisible() const;

    // Per-frame update: advance auto-dismiss timers for normal toasts,
    // check deficit streak via m_sim->getConsecutiveDeficitMonths(),
    // and fire progressive warning toasts as required by game-over-flow.md.
    // Uses m_clock->nowSeconds() for timing — NOT accumulated deltas.
    void update();

    // Issue draw calls for all active toast elements and the log panel (if open).
    // Called from UIManager::draw() at slot 6.
    void draw();

    // Notify NotificationManager when a modal becomes active or inactive.
    // When m_modalActive is true, Priority-2 input routing is suppressed even
    // if a CRITICAL toast is visible (part of the dual-guard compound condition).
    void setModalActive(bool active);

private:
    IUIBackend*       m_backend{nullptr};
    ICitySimulation*  m_sim{nullptr};
    IClock*           m_clock{nullptr};
    bool              m_modalActive{false};
};
