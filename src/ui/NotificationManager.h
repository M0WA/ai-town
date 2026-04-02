#pragma once
#include <string>
#include <vector>
#include <deque>
#include "src/interfaces/IUIBackend.h"                // UIElementHandle, IUIBackend
#include "src/interfaces/IClock.h"            // IClock
#include "src/interfaces/ICitySimulation.h"   // ICitySimulation

// Forward declaration for InputEvent — avoid pulling platform headers into every
// translation unit that includes NotificationManager.h.
struct InputEvent;

// Forward declaration — IAudioSystem is an interface; full include not needed in header.
class IAudioSystem;

// NotificationManager — manages toast notifications with auto-dismiss timing
// and CRITICAL-toast auto-pause injection.
//
// Constructor signature: (backend, sim, clock, audio) — ICitySimulation* is the second
// parameter so NotificationManager can call sim->setPaused(true) for CRITICAL-toast
// auto-pause. Phase 3 corrects the Phase 0 stub which mistakenly used ISimulationPauser*.
// Phase 10 adds IAudioSystem* as the fourth parameter so postCritical()/postNormal()
// can call m_audio->playSound(UI_TOAST, ...) when a toast becomes visible.
// Pass nullptr for audio before Phase 10 — all audio call sites are null-guarded.
//
// Toast sizes (virtual 1920x1080 space):
//   CRITICAL toasts: 48 px fixed height; auto-pause game; player-dismissed via
//                    dismissCriticalToast(UIElementHandle).
//   Normal toasts:   40-63 px height; auto-dismiss after a timed duration.
//
// Log panel: 400x500 px, anchored to bell icon (bottom-right of screen).
class NotificationManager {
public:
    // Phase 10 signature: IAudioSystem* is the fourth parameter.
    // Callers that pre-date Phase 10 may pass nullptr for audio — every audio
    // call site inside NotificationManager is guarded by (if m_audio).
    NotificationManager(IUIBackend* backend, ICitySimulation* sim, IClock* clock,
                        IAudioSystem* audio = nullptr);

    // Production API for player dismissal of CRITICAL toasts.
    // Called by the UI event handler when the player clicks, presses Enter,
    // or presses Delete on a CRITICAL toast. Not a test-only backdoor.
    // NOTE: does NOT call setPaused(false) — see notification-system.md.
    void dismissCriticalToast(UIElementHandle handle);

    // Post a CRITICAL toast (48 px, auto-pauses game, player-dismissed).
    // title and body are displayed in the toast element.
    void postCritical(const std::string& title, const std::string& body);

    // Post a normal toast (40-63 px, auto-dismiss after timed duration).
    // timeoutSeconds: auto-dismiss delay (default 5 s; QueryPanel Escape
    // feedback toast uses 1.5 s per architecture/ui-ux/notification-system.md).
    void postNormal(const std::string& title, const std::string& body,
                    float timeoutSeconds = 5.0f);

    // Handle an input event routed from UIManager's Priority-2 guard.
    // Returns true if the event was consumed by the notification system
    // (e.g. player dismissed a CRITICAL toast via click or key press).
    // Only called when hasCriticalToastVisible() && !modalActive (dual-guard).
    bool onEvent(const InputEvent& event);

    // Returns true when at least one CRITICAL toast is queued
    // and not suppressed by an active modal.
    bool hasCriticalToastVisible() const;

    // Per-frame update: advance auto-dismiss timers for normal toasts.
    // Uses m_clock->nowSeconds() for timing — NOT accumulated deltas.
    void update();

    // Issue draw calls for all active toast elements and the log panel (if open).
    // Called from UIManager::draw() at slot 6.
    void draw();

    // Notify NotificationManager when a modal becomes active or inactive.
    // When m_modalActive is true, Priority-2 input routing is suppressed even
    // if a CRITICAL toast is visible (part of the dual-guard compound condition).
    // When becoming inactive, re-evaluates auto-pause synchronously.
    void setModalActive(bool active);

    // Toggle the notification log panel (B key / bell icon).
    void toggleLog();

    // Returns true if the notification log panel is currently open.
    bool isLogOpen() const;

private:
    // --- Internal toast structures ---

    struct CriticalToast {
        std::string title;
        std::string body;
        UIElementHandle handle{kInvalidUIElement};
    };

    struct NormalToast {
        std::string title;
        std::string body;
        double expiryTime{0.0};
        UIElementHandle handle{kInvalidUIElement};
    };

    struct LogEntry {
        std::string title;
        std::string body;
        bool isCritical{false};
        double timestamp{0.0};
    };

    // --- Helper methods ---

    // Recreate/remove UI elements for CRITICAL toasts based on queue state
    // and modal visibility. Repositions all visible toasts correctly.
    void refreshCriticalVisibility();

    // Recreate/remove UI elements for Normal toasts based on queue state
    // and visible CRITICAL toast count.
    void refreshNormalVisibility();

    // Add an entry to the notification log (capped at kMaxLogEntries).
    void addLogEntry(const std::string& title, const std::string& body, bool isCritical);

    // Truncate body text to kMaxBodyChars with trailing ellipsis.
    static std::string truncateBody(const std::string& body);

    // --- Data members ---

    IUIBackend*       m_backend{nullptr};
    ICitySimulation*  m_sim{nullptr};
    IClock*           m_clock{nullptr};
    IAudioSystem*     m_audio{nullptr};
    bool              m_modalActive{false};

    // Toast queues — front of deque is the oldest (displayed first).
    std::deque<CriticalToast> m_criticalQueue;
    std::deque<NormalToast>   m_normalQueue;

    // Auto-pause tracking: true if NotificationManager called setPaused(true)
    // due to a CRITICAL toast arriving while the queue was empty.
    bool m_hasPausedForCritical{false};

    // Notification log (most-recent-first, max 50 entries).
    std::vector<LogEntry> m_logEntries;
    bool m_logOpen{false};
    UIElementHandle m_logPanelHandle{kInvalidUIElement};

    // CRITICAL toast keyboard focus (Tab-navigable, oldest gets focus first).
    int m_focusedCriticalIndex{0};

    // --- Helper methods (scrollbar) ---

    // Recompute scrollbar thumb position/size and update element visibility.
    // Called after any change to m_logScrollOffset or m_logEntries size while
    // the log panel is open.
    void updateScrollThumb();

    // --- Scrollbar element handles ---
    UIElementHandle m_logScrollTrack{kInvalidUIElement};
    UIElementHandle m_logScrollThumb{kInvalidUIElement};
    int             m_logVisibleRows{0};  // computed at panel open: floor(500 / kLogRowHeightPx)
    int             m_logScrollOffset{0}; // first visible row index (0 = top)

    // Layout constants are defined as anonymous-namespace constexpr values in
    // NotificationManager.cpp (D-9 / UI-10 — not repeated here to keep the header clean).
};
