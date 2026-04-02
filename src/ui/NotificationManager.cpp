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
#include <cmath>
#include <cstddef>

// Key codes — must match the platform adapter's InputEvent keyCode mapping.
// Update these if the platform adapter uses different values.
namespace {
    constexpr int kKeyReturn = 13;   // Enter/Return
    constexpr int kKeyDelete = 46;   // Delete key (Irrlicht KEY_DELETE)
    constexpr int kKeyTab    = 9;    // Tab

    // --- Toast layout constants (virtual 1920x1080 space) ---
    // D-9 / UI-10: moved from NotificationManager.h to keep the header clean.
    constexpr int kToastWidth           = 500;
    constexpr int kCriticalToastHeight  = 48;
    constexpr int kNormalToastMaxHeight = 63;
    constexpr int kCriticalBandY        = 20;
    constexpr int kNormalBandY          = 130;
    constexpr int kMaxCriticalVisible   = 2;
    constexpr int kMaxNormalBase        = 3;
    constexpr int kMaxNormalQueueDepth  = 10;
    constexpr int kMaxLogEntries        = 50;
    constexpr int kMaxBodyChars         = 80;

    // --- Log panel layout constants (virtual 1920x1080 space) ---
    constexpr int kLogRowHeightPx = 20;
    constexpr int kLogTrackX      = 1856;
    constexpr int kLogTrackY      = 56;
    constexpr int kLogTrackW      = 12;
    constexpr int kLogTrackH      = 500;
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

    // Update scrollbar if the log panel is currently open (new entry added).
    if (m_logOpen) {
        updateScrollThumb();
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

    // Update scrollbar if the log panel is currently open (new entry added).
    if (m_logOpen) {
        updateScrollThumb();
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
    // --- Mouse wheel: scroll the log panel if it is open ---
    if (m_logOpen && event.type == InputEvent::Type::MouseWheel) {
        int totalRows   = static_cast<int>(m_logEntries.size());
        int maxOffset   = std::max(0, totalRows - m_logVisibleRows);
        if (event.wheelDelta < 0.f) {
            // Scroll down (towards older entries).
            m_logScrollOffset = std::min(m_logScrollOffset + 1, maxOffset);
        } else if (event.wheelDelta > 0.f) {
            // Scroll up (towards newer entries).
            m_logScrollOffset = std::max(m_logScrollOffset - 1, 0);
        }
        updateScrollThumb();
        return true;
    }

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
            // Content area width is 388 px (400 - 12 px scrollbar on right edge).
            m_logPanelHandle = m_backend->addStaticText("Notification Log",
                                                        1468, 56, 388, 500);
            // Dark navy semi-opaque background. Phase 10c Glass City Colour Pass.
            // Signature: setElementBackground(handle, r, g, b, a)
            m_backend->setElementBackground(m_logPanelHandle, 13, 27, 42, 209);
        }

        // Create scrollbar track (12 px wide, right edge of panel: x:1856-1868).
        if (m_logScrollTrack == kInvalidUIElement && m_backend) {
            m_logScrollTrack = m_backend->addStaticText("",
                kLogTrackX, kLogTrackY, kLogTrackW, kLogTrackH);
            // Track colour: rgba(255, 255, 255, 0.08) — approx 20/255.
            m_backend->setElementBackground(m_logScrollTrack, 255, 255, 255, 20);
        }

        // Create scrollbar thumb (12 px wide, height and position computed below).
        if (m_logScrollThumb == kInvalidUIElement && m_backend) {
            m_logScrollThumb = m_backend->addStaticText("",
                kLogTrackX, kLogTrackY, kLogTrackW, kLogTrackH);
            // Thumb colour at rest: rgba(255, 255, 255, 0.25) — approx 64/255.
            m_backend->setElementBackground(m_logScrollThumb, 255, 255, 255, 64);
        }

        // Compute visible rows from panel height and row height.
        m_logVisibleRows = kLogTrackH / kLogRowHeightPx;

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

        // Initialise scrollbar position and visibility.
        updateScrollThumb();
    } else {
        if (m_logPanelHandle != kInvalidUIElement && m_backend) {
            m_backend->setElementVisible(m_logPanelHandle, false);
        }
        if (m_logScrollTrack != kInvalidUIElement && m_backend) {
            m_backend->setElementVisible(m_logScrollTrack, false);
        }
        if (m_logScrollThumb != kInvalidUIElement && m_backend) {
            m_backend->setElementVisible(m_logScrollThumb, false);
        }
    }
}

// ----------------------------------------------------------------
// updateScrollThumb — recompute thumb geometry and update visibility
// ----------------------------------------------------------------
void NotificationManager::updateScrollThumb() {
    if (!m_backend) return;

    int totalRows   = static_cast<int>(m_logEntries.size());
    int visibleRows = m_logVisibleRows;

    // Hide scrollbar when all entries fit without scrolling.
    bool needsScrollbar = (totalRows > visibleRows);

    if (m_logScrollTrack != kInvalidUIElement) {
        m_backend->setElementVisible(m_logScrollTrack, needsScrollbar);
    }
    if (m_logScrollThumb != kInvalidUIElement) {
        m_backend->setElementVisible(m_logScrollThumb, needsScrollbar);
    }

    if (!needsScrollbar) return;

    // thumbH = max(20, floor(visibleRows / totalRows * trackH))
    int thumbH = std::max(20,
        static_cast<int>(std::floor(
            static_cast<float>(visibleRows) / static_cast<float>(std::max(1, totalRows))
            * static_cast<float>(kLogTrackH))));

    // thumbY = trackTop + floor(scrollOffset / max(1, totalRows - visibleRows) * (trackH - thumbH))
    int denom  = std::max(1, totalRows - visibleRows);
    int thumbY = kLogTrackY + static_cast<int>(std::floor(
        static_cast<float>(m_logScrollOffset) / static_cast<float>(denom)
        * static_cast<float>(kLogTrackH - thumbH)));

    if (m_logScrollThumb != kInvalidUIElement) {
        m_backend->setElementRect(m_logScrollThumb, kLogTrackX, thumbY, kLogTrackW, thumbH);
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
