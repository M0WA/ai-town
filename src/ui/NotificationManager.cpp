// src/ui/NotificationManager.cpp
//
// NotificationManager — full Phase 8 implementation.
// Manages CRITICAL and Normal toast queues, auto-pause injection,
// player dismissal, modal-aware visibility, and the notification log panel.
//
// CRITICAL toasts: 48 px, y:20-116 band, max 2 visible, player-dismissed.
// Normal toasts:   40-63 px, y:130+ band, max 3 visible (reduced by CRITICAL count),
//                  auto-dismiss after timeout.
//
// Deficit-streak polling is handled by UIManager::update(), NOT here.
// NotificationManager only provides postCritical/postNormal for UIManager to call.

#include "src/ui/NotificationManager.h"
#include "src/platform/input_event.h"  // InputEvent — full include here, not in the header
#include "src/interfaces/IAudioSystem.h"  // full include in .cpp for m_audio->playSound() calls
#include "src/interfaces/sound_ids.h"     // UI_TOAST = SoundId 23

#include <algorithm>
#include <cstddef>

// Key codes — must match the platform adapter's InputEvent keyCode mapping.
// Update these if the platform adapter uses different values.
namespace {
    constexpr int kKeyReturn = 13;   // Enter/Return
    constexpr int kKeyDelete = 46;   // Delete key (Irrlicht KEY_DELETE)
    constexpr int kKeyTab    = 9;    // Tab
} // namespace

// ----------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------
NotificationManager::NotificationManager(IUIBackend* backend, ICitySimulation* sim, IClock* clock,
                                         IAudioSystem* audio)
    : m_backend(backend)
    , m_sim(sim)
    , m_clock(clock)
    , m_audio(audio)
{}

// ----------------------------------------------------------------
// Static helper: truncate body text to kMaxBodyChars with ellipsis
// ----------------------------------------------------------------
std::string NotificationManager::truncateBody(const std::string& body) {
    if (body.size() > static_cast<std::size_t>(kMaxBodyChars)) {
        return body.substr(0, kMaxBodyChars - 3) + "...";
    }
    return body;
}

// ----------------------------------------------------------------
// addLogEntry — insert at front (most-recent first), cap at max
// ----------------------------------------------------------------
void NotificationManager::addLogEntry(const std::string& title, const std::string& body,
                                      bool isCritical) {
    double now = m_clock ? m_clock->nowSeconds() : 0.0;
    m_logEntries.insert(m_logEntries.begin(), LogEntry{title, body, isCritical, now});
    if (static_cast<int>(m_logEntries.size()) > kMaxLogEntries) {
        m_logEntries.resize(static_cast<std::size_t>(kMaxLogEntries));
    }
}

// ----------------------------------------------------------------
// postCritical — enqueue a CRITICAL toast, auto-pause if first arrival
// ----------------------------------------------------------------
void NotificationManager::postCritical(const std::string& title, const std::string& body) {
    // Log the full body (not truncated) to the notification log.
    addLogEntry(title, body, /*isCritical=*/true);

    // Enqueue the toast with truncated body for display.
    m_criticalQueue.push_back(CriticalToast{title, truncateBody(body), kInvalidUIElement});

    // Auto-pause: if this is the FIRST CRITICAL toast and no modal is blocking.
    // Size == 1 means we just added the first one (queue was empty before).
    bool wasEmpty = (m_criticalQueue.size() == 1);
    if (wasEmpty && !m_modalActive && m_sim) {
        m_sim->setPaused(true);
        m_hasPausedForCritical = true;
    }

    // Refresh UI elements to show the new toast if room is available.
    refreshCriticalVisibility();
    refreshNormalVisibility();

    // Phase 10: fire ui_toast SFX when the new CRITICAL toast became visible on screen.
    // Fires once per appearance — not per enqueue — only when a UI element was created.
    // Guard: !m_criticalQueue.empty() (defensive; we just pushed_back above).
    if (m_audio && !m_criticalQueue.empty() &&
        m_criticalQueue.back().handle != kInvalidUIElement) {
        m_audio->playSound(UI_TOAST, SoundPriority::NORMAL, 1.0f);
    }
}

// ----------------------------------------------------------------
// postNormal — enqueue a Normal toast with auto-dismiss timer
// ----------------------------------------------------------------
void NotificationManager::postNormal(const std::string& title, const std::string& body,
                                     float timeoutSeconds) {
    // Always log (full body, not truncated).
    addLogEntry(title, body, /*isCritical=*/false);

    // Check queue depth — beyond kMaxNormalQueueDepth, log only.
    if (static_cast<int>(m_normalQueue.size()) >= kMaxNormalQueueDepth) {
        return;
    }

    double expiryTime = (m_clock ? m_clock->nowSeconds() : 0.0)
                        + static_cast<double>(timeoutSeconds);
    m_normalQueue.push_back(NormalToast{title, truncateBody(body), expiryTime, kInvalidUIElement});

    refreshNormalVisibility();

    // Phase 10: fire ui_toast SFX when the new Normal toast became visible on screen.
    // Fires once per appearance — not per enqueue — only when a UI element was created.
    // Guard: !m_normalQueue.empty() (defensive; we just pushed_back above).
    if (m_audio && !m_normalQueue.empty() &&
        m_normalQueue.back().handle != kInvalidUIElement) {
        m_audio->playSound(UI_TOAST, SoundPriority::NORMAL, 1.0f);
    }
}

// ----------------------------------------------------------------
// dismissCriticalToast — remove one CRITICAL toast by handle
// NOTE: does NOT call setPaused(false) — player must explicitly unpause
//       or the UIManager closeModal path handles it.
// ----------------------------------------------------------------
void NotificationManager::dismissCriticalToast(UIElementHandle handle) {
    auto it = std::find_if(m_criticalQueue.begin(), m_criticalQueue.end(),
        [handle](const CriticalToast& t) { return t.handle == handle; });

    if (it != m_criticalQueue.end()) {
        // Remove the UI element.
        if (it->handle != kInvalidUIElement && m_backend) {
            m_backend->removeElement(it->handle);
        }
        m_criticalQueue.erase(it);

        // Clamp focus index.
        if (m_focusedCriticalIndex >= static_cast<int>(m_criticalQueue.size())) {
            m_focusedCriticalIndex = 0;
        }

        // Refresh: next queued toast becomes visible if any.
        refreshCriticalVisibility();
        refreshNormalVisibility();
    }
    // Do NOT call setPaused(false) here.
}

// ----------------------------------------------------------------
// hasCriticalToastVisible — dual-guard component
// ----------------------------------------------------------------
bool NotificationManager::hasCriticalToastVisible() const {
    return !m_criticalQueue.empty() && !m_modalActive;
}

// ----------------------------------------------------------------
// update — auto-dismiss expired Normal toasts
// ----------------------------------------------------------------
void NotificationManager::update() {
    if (!m_clock) return;

    double now = m_clock->nowSeconds();

    // Remove expired Normal toasts.
    bool removed = false;
    auto it = m_normalQueue.begin();
    while (it != m_normalQueue.end()) {
        if (now >= it->expiryTime) {
            if (it->handle != kInvalidUIElement && m_backend) {
                m_backend->removeElement(it->handle);
            }
            it = m_normalQueue.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }

    if (removed) {
        refreshNormalVisibility();
    }
}

// ----------------------------------------------------------------
// draw — ensure all active toast elements and the log panel are visible
// ----------------------------------------------------------------
void NotificationManager::draw() {
    if (!m_backend) return;

    // CRITICAL toasts — visible elements should already be shown via refresh,
    // but confirm visibility each frame for correctness.
    for (const auto& toast : m_criticalQueue) {
        if (toast.handle != kInvalidUIElement) {
            m_backend->setElementVisible(toast.handle, true);
        }
    }

    // Normal toasts — same confirmation.
    for (const auto& toast : m_normalQueue) {
        if (toast.handle != kInvalidUIElement) {
            m_backend->setElementVisible(toast.handle, true);
        }
    }

    // Log panel visibility.
    if (m_logPanelHandle != kInvalidUIElement) {
        m_backend->setElementVisible(m_logPanelHandle, m_logOpen);
    }
}

// ----------------------------------------------------------------
// onEvent — handle click/Enter/Delete on CRITICAL toasts
// Only called when hasCriticalToastVisible() && !modalActive (dual-guard
// verified by UIManager::onEvent at Priority 2).
// ----------------------------------------------------------------
bool NotificationManager::onEvent(const InputEvent& event) {
    if (m_criticalQueue.empty()) return false;

    // --- Click on a CRITICAL toast ---
    if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
        int vw = m_backend ? m_backend->getVirtualWidth() : 1920;
        int toastX = (vw - kToastWidth) / 2;

        int visibleCount = std::min(static_cast<int>(m_criticalQueue.size()),
                                    kMaxCriticalVisible);
        for (int i = 0; i < visibleCount; ++i) {
            int ty = kCriticalBandY + i * kCriticalToastHeight;
            if (event.x >= toastX && event.x < toastX + kToastWidth &&
                event.y >= ty && event.y < ty + kCriticalToastHeight) {
                if (m_criticalQueue[static_cast<std::size_t>(i)].handle != kInvalidUIElement) {
                    dismissCriticalToast(
                        m_criticalQueue[static_cast<std::size_t>(i)].handle);
                    return true;
                }
            }
        }
    }

    // --- Enter or Delete key: dismiss the focused CRITICAL toast ---
    if (event.type == InputEvent::Type::KeyDown) {
        if (event.keyCode == kKeyReturn || event.keyCode == kKeyDelete) {
            int visibleCount = std::min(static_cast<int>(m_criticalQueue.size()),
                                        kMaxCriticalVisible);
            if (m_focusedCriticalIndex < visibleCount) {
                auto& toast = m_criticalQueue[static_cast<std::size_t>(m_focusedCriticalIndex)];
                if (toast.handle != kInvalidUIElement) {
                    dismissCriticalToast(toast.handle);
                    return true;
                }
            }
        }

        // Tab cycles focus between visible CRITICAL toasts.
        if (event.keyCode == kKeyTab) {
            int visibleCount = std::min(static_cast<int>(m_criticalQueue.size()),
                                        kMaxCriticalVisible);
            if (visibleCount > 1) {
                m_focusedCriticalIndex = (m_focusedCriticalIndex + 1) % visibleCount;
            }
            return true;
        }
    }

    return false;
}

// ----------------------------------------------------------------
// setModalActive — hide/show CRITICAL toasts and re-evaluate auto-pause
// ----------------------------------------------------------------
void NotificationManager::setModalActive(bool active) {
    m_modalActive = active;

    if (active) {
        // Modal becoming active: hide all visible CRITICAL toasts.
        // They return to the front of the queue (handles cleared, not erased).
        for (auto& toast : m_criticalQueue) {
            if (toast.handle != kInvalidUIElement && m_backend) {
                m_backend->removeElement(toast.handle);
                toast.handle = kInvalidUIElement;
            }
        }
    } else {
        // Modal dismissed: re-show CRITICAL toasts (up to 2).
        refreshCriticalVisibility();

        // Re-evaluate auto-pause synchronously (per notification-system.md):
        // if CRITICAL queue is non-empty and sim is not currently paused,
        // pause it now.
        if (!m_criticalQueue.empty() && m_sim && !m_sim->isPaused()) {
            m_sim->setPaused(true);
            m_hasPausedForCritical = true;
        }
    }

    refreshNormalVisibility();
}

// ----------------------------------------------------------------
// toggleLog — show/hide the notification log panel
// ----------------------------------------------------------------
void NotificationManager::toggleLog() {
    m_logOpen = !m_logOpen;

    if (m_logOpen) {
        // Create the log panel element if it does not yet exist.
        if (m_logPanelHandle == kInvalidUIElement && m_backend) {
            // Log panel: 400x500 px, aligned to bell icon bottom-right.
            // Virtual bounds: x:1468-1868, y:56-556.
            m_logPanelHandle = m_backend->addStaticText("Notification Log",
                                                        1468, 56, 400, 500);
        }
        if (m_logPanelHandle != kInvalidUIElement && m_backend) {
            // Build log text from entries.
            std::string logText = "Notification Log\n";
            for (const auto& entry : m_logEntries) {
                logText += (entry.isCritical ? "[!] " : "    ");
                logText += entry.title + ": " + entry.body + "\n";
            }
            m_backend->setElementText(m_logPanelHandle, logText);
            m_backend->setElementVisible(m_logPanelHandle, true);
        }
    } else {
        if (m_logPanelHandle != kInvalidUIElement && m_backend) {
            m_backend->setElementVisible(m_logPanelHandle, false);
        }
    }
}

// ----------------------------------------------------------------
// isLogOpen
// ----------------------------------------------------------------
bool NotificationManager::isLogOpen() const {
    return m_logOpen;
}

// ----------------------------------------------------------------
// refreshCriticalVisibility — recreate UI elements at correct positions
// ----------------------------------------------------------------
void NotificationManager::refreshCriticalVisibility() {
    int maxVisible = m_modalActive ? 0 : kMaxCriticalVisible;
    int vw = m_backend ? m_backend->getVirtualWidth() : 1920;
    int toastX = (vw - kToastWidth) / 2;

    for (std::size_t i = 0; i < m_criticalQueue.size(); ++i) {
        auto& toast = m_criticalQueue[i];

        // Remove existing element (will recreate at correct position if needed).
        if (toast.handle != kInvalidUIElement && m_backend) {
            m_backend->removeElement(toast.handle);
            toast.handle = kInvalidUIElement;
        }

        if (static_cast<int>(i) < maxVisible && m_backend) {
            int y = kCriticalBandY + static_cast<int>(i) * kCriticalToastHeight;
            std::string displayText = toast.title + ": " + toast.body;
            toast.handle = m_backend->addStaticText(displayText, toastX, y,
                                                     kToastWidth, kCriticalToastHeight);
        }
    }

    // Reset focus if out of bounds.
    int visibleCount = std::min(static_cast<int>(m_criticalQueue.size()), maxVisible);
    if (m_focusedCriticalIndex >= visibleCount) {
        m_focusedCriticalIndex = 0;
    }
}

// ----------------------------------------------------------------
// refreshNormalVisibility — recreate UI elements at correct positions
// ----------------------------------------------------------------
void NotificationManager::refreshNormalVisibility() {
    // Count visible CRITICAL toasts (those with valid handles).
    int visibleCritical = 0;
    for (const auto& ct : m_criticalQueue) {
        if (ct.handle != kInvalidUIElement) {
            ++visibleCritical;
        }
    }

    // Max visible normal = 3 minus CRITICAL count (clamped).
    int maxVisibleNormal = kMaxNormalBase - std::min(visibleCritical, kMaxCriticalVisible);
    if (maxVisibleNormal < 0) maxVisibleNormal = 0;

    int vw = m_backend ? m_backend->getVirtualWidth() : 1920;
    int toastX = (vw - kToastWidth) / 2;

    for (std::size_t i = 0; i < m_normalQueue.size(); ++i) {
        auto& toast = m_normalQueue[i];

        // Remove existing element (will recreate at correct position if visible).
        if (toast.handle != kInvalidUIElement && m_backend) {
            m_backend->removeElement(toast.handle);
            toast.handle = kInvalidUIElement;
        }

        if (static_cast<int>(i) < maxVisibleNormal && m_backend) {
            int y = kNormalBandY + static_cast<int>(i) * (kNormalToastMaxHeight + 2);
            std::string displayText = toast.title + ": " + toast.body;
            toast.handle = m_backend->addStaticText(displayText, toastX, y,
                                                     kToastWidth, kNormalToastMaxHeight);
        }
    }
}
