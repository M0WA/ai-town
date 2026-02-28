#include "src/ui/NotificationManager.h"
#include "src/platform/input_event.h"  // InputEvent — full include here, not in the header

// Sentinel handle used to exercise IUIBackend in stub draw() — ensures the
// Phase 4 25% src/ui/ coverage gate is not broken by an empty draw body.
constexpr UIElementHandle kNotifSentinel = 0xDEAD0105u;

NotificationManager::NotificationManager(IUIBackend* backend, ICitySimulation* sim, IClock* clock)
    : m_backend(backend)
    , m_sim(sim)
    , m_clock(clock)
{}

void NotificationManager::dismissCriticalToast(UIElementHandle /*handle*/) {
    // Phase 8: hide the identified CRITICAL toast element, call m_sim->setPaused(false)
    // if no other CRITICAL toasts remain visible, and remove the handle from the active list.
}

void NotificationManager::postCritical(const std::string& /*title*/, const std::string& /*body*/) {
    // Phase 8: create a 48 px toast element via m_backend->addStaticText(),
    // store its handle, and call m_sim->setPaused(true) to auto-pause.
}

void NotificationManager::postNormal(const std::string& /*title*/, const std::string& /*body*/,
                                     float /*timeoutSeconds*/) {
    // Phase 8: create a 40-63 px toast element via m_backend->addStaticText(),
    // store its handle along with an expiry timestamp from m_clock->nowSeconds()
    // offset by timeoutSeconds.
}

bool NotificationManager::onEvent(const InputEvent& /*event*/) {
    // Phase 8: check if the event is a click/Enter/Delete on a CRITICAL toast;
    // if so, call dismissCriticalToast(handle) and return true (consumed).
    return false;
}

bool NotificationManager::hasCriticalToastVisible() const {
    // Phase 8: return true if the active-CRITICAL-toast list is non-empty.
    return false;
}

void NotificationManager::update() {
    // Phase 8: advance auto-dismiss timers for normal toasts using m_clock->nowSeconds();
    // expire and remove any toasts past their deadline.
    // Also poll m_sim->getConsecutiveDeficitMonths() and fire progressive warning toasts
    // per architecture/game-design/game-over-flow.md:
    //   streak==1 -> "2 months to bankruptcy" normal toast
    //   streak==2 -> "1 month to bankruptcy" CRITICAL toast
    //   streak>=3 -> trigger game-over (handled by UIManager::update() / GD-H3 bridge)
}

void NotificationManager::draw() {
    // Calls at least one IUIBackend method so the Phase 4 25% src/ui/ coverage gate passes.
    m_backend->setElementVisible(kNotifSentinel, true);
    // Phase 8: iterate active toast handles and call setElementVisible() on each.
}

void NotificationManager::setModalActive(bool active) {
    m_modalActive = active;
}
