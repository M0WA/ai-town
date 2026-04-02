// src/ui/KeyBindingsPanel.cpp
//
// KeyBindingsPanel — Controls tab keybinding table with capture-mode state machine.
//
// Row layout (top to bottom):
//   Rows  0–10 : capturable (11 rebindable actions)
//   Rows 11–12 : informational (Ctrl+Z, Ctrl+S — not rebindable in V1)
//   Rows 13–14 : reserved (Q, E — reserved for future camera controls)
//
// Capture state machine: Idle → Capturing → ConflictPending → Idle
//   Idle           : normal display; clicking a chip enters Capturing
//   Capturing      : chip shows teal wash; any key press (except Escape) is a candidate
//   ConflictPending: conflict text shown; Swap/Cancel buttons visible
//
// Escape during capture: cancels the capture and returns false so SettingsPanel's
// Escape handler fires and closes Settings in the same event frame.

#include "src/ui/KeyBindingsPanel.h"
#include "src/platform/input_event.h"

#include <string>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// copyMutableBindings -- copy only the rebindable (non-const) fields.
// KeyBindings::undo and KeyBindings::save are const and cannot be assigned;
// the copy-assignment operator is therefore implicitly deleted for the whole
// struct.  This helper copies only the 11 capturable fields individually.
static void copyMutableBindings(KeyBindings& dst, const KeyBindings& src) {
    dst.camPanUp       = src.camPanUp;
    dst.camPanDown     = src.camPanDown;
    dst.camPanLeft     = src.camPanLeft;
    dst.camPanRight    = src.camPanRight;
    dst.toolZone       = src.toolZone;
    dst.toolRoad       = src.toolRoad;
    dst.toolUtilities  = src.toolUtilities;
    dst.toolDemolish   = src.toolDemolish;
    dst.toolInspector  = src.toolInspector;
    dst.toggleTaxPanel = src.toggleTaxPanel;
    dst.toggleNotifLog = src.toggleNotifLog;
}

// ---------------------------------------------------------------------------
// Static tables
// ---------------------------------------------------------------------------

const char* KeyBindingsPanel::kActionName[KeyBindingsPanel::kNumCapturable] = {
    "Zone tool",           // 0  camPanUp     — wait, reorder per KeyBindings struct
    "Road tool",           // 1
    "Utilities tool",      // 2
    "Demolish tool",       // 3
    "Inspector/Query tool",// 4
    "Toggle Tax Rate Panel",// 5
    "Toggle Notification Log",// 6
    "Pan Up",              // 7
    "Pan Down",            // 8
    "Pan Left",            // 9
    "Pan Right"            // 10
};

// Maps capturable row index → pointer-to-member in KeyBindings struct.
// Order must match kActionName.
KeyBindingsPanel::BindingField KeyBindingsPanel::kBindingField[KeyBindingsPanel::kNumCapturable] = {
    &KeyBindings::toolZone,       // 0
    &KeyBindings::toolRoad,       // 1
    &KeyBindings::toolUtilities,  // 2
    &KeyBindings::toolDemolish,   // 3
    &KeyBindings::toolInspector,  // 4
    &KeyBindings::toggleTaxPanel, // 5
    &KeyBindings::toggleNotifLog, // 6
    &KeyBindings::camPanUp,       // 7
    &KeyBindings::camPanDown,     // 8
    &KeyBindings::camPanLeft,     // 9
    &KeyBindings::camPanRight     // 10
};

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
KeyBindingsPanel::~KeyBindingsPanel() {
    if (!m_backend) return;
    for (int i = 0; i < kTotalRows; ++i) {
        if (m_rowLabel[i] != kInvalidUIElement) m_backend->removeElement(m_rowLabel[i]);
        if (m_rowChip[i]  != kInvalidUIElement) m_backend->removeElement(m_rowChip[i]);
    }
    for (int i = 0; i < kNumCapturable; ++i) {
        if (m_rowMsg[i] != kInvalidUIElement) m_backend->removeElement(m_rowMsg[i]);
    }
    if (m_btnSwap           != kInvalidUIElement) m_backend->removeElement(m_btnSwap);
    if (m_btnConflictCancel != kInvalidUIElement) m_backend->removeElement(m_btnConflictCancel);
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
KeyBindingsPanel::KeyBindingsPanel(IUIBackend* backend, ModalDialog* modal)
    : m_backend(backend)
    , m_modal(modal)
{
    if (!m_backend) return;

    // Initialise all handles to invalid.
    for (int i = 0; i < kTotalRows; ++i) {
        m_rowLabel[i] = kInvalidUIElement;
        m_rowChip[i]  = kInvalidUIElement;
    }
    for (int i = 0; i < kNumCapturable; ++i) {
        m_rowMsg[i] = kInvalidUIElement;
    }

    int y = kContentY;

    // --- Capturable rows (0–10) ---
    for (int i = 0; i < kNumCapturable; ++i) {
        m_rowLabel[i] = m_backend->addStaticText(
            kActionName[i],
            kContentX, y, kContentW - kChipW - 16, kLineH);
        m_rowChip[i] = m_backend->addButton(
            "",
            kContentX + kContentW - kChipW, y, kChipW, kLineH);
        // Chip background: inactive tile rgba(255,255,255,0.08) approximated as alpha=20/255
        m_backend->setElementBackground(m_rowChip[i], 255, 255, 255, 20);
        // Inline message label (initially empty/hidden)
        m_rowMsg[i] = m_backend->addStaticText(
            "",
            kContentX, y + kLineH, kContentW, kLineH);
        m_backend->setElementVisible(m_rowMsg[i], false);

        // Colour: action label near-white #EBF4F6
        m_backend->setElementTextColor(m_rowLabel[i], 0xEB, 0xF4, 0xF6);

        y += kLineH + kRowGap;
    }

    // --- Informational rows (11–12: Ctrl+Z, Ctrl+S) ---
    const char* infoLabels[kNumInfoRows] = {
        "Ctrl+Z — Undo (not rebindable in V1)",
        "Ctrl+S — Save (not rebindable in V1)"
    };
    const char* infoChips[kNumInfoRows] = { "Ctrl+Z", "Ctrl+S" };
    for (int i = 0; i < kNumInfoRows; ++i) {
        int row = kNumCapturable + i;
        m_rowLabel[row] = m_backend->addStaticText(
            infoLabels[i],
            kContentX, y, kContentW - kChipW - 16, kLineH);
        m_rowChip[row] = m_backend->addStaticText(
            infoChips[i],
            kContentX + kContentW - kChipW, y, kChipW, kLineH);
        // Mid-blue #4A7FA5 for non-rebindable rows
        m_backend->setElementTextColor(m_rowLabel[row], 0x4A, 0x7F, 0xA5);
        m_backend->setElementTextColor(m_rowChip[row],  0x4A, 0x7F, 0xA5);
        y += kLineH + kRowGap;
    }

    // --- Reserved rows (13–14: Q, E) ---
    const char* reservedLabels[kNumReservedRows] = {
        "Q — Reserved for future camera controls — unavailable",
        "E — Reserved for future camera controls — unavailable"
    };
    const char* reservedChips[kNumReservedRows] = { "Q", "E" };
    for (int i = 0; i < kNumReservedRows; ++i) {
        int row = kNumCapturable + kNumInfoRows + i;
        m_rowLabel[row] = m_backend->addStaticText(
            reservedLabels[i],
            kContentX, y, kContentW - kChipW - 16, kLineH);
        m_rowChip[row] = m_backend->addStaticText(
            reservedChips[i],
            kContentX + kContentW - kChipW, y, kChipW, kLineH);
        // Mid-blue at 50% opacity for reserved rows
        m_backend->setElementTextColor(m_rowLabel[row], 0x4A, 0x7F, 0xA5);
        m_backend->setElementAlpha(m_rowLabel[row], 0.5f);
        m_backend->setElementTextColor(m_rowChip[row],  0x4A, 0x7F, 0xA5);
        m_backend->setElementAlpha(m_rowChip[row], 0.5f);

        // Inline "reserved" notice always visible in the row
        // (re-use the row label space — it already contains the full reserved text)
        y += kLineH + kRowGap;
    }

    // Conflict swap/cancel buttons — hidden until a conflict is detected
    m_btnSwap = m_backend->addButton("Swap bindings", kContentX, kContentY, 160, kLineH);
    m_btnConflictCancel = m_backend->addButton("Cancel", kContentX + 168, kContentY, 100, kLineH);
    m_backend->setElementVisible(m_btnSwap,           false);
    m_backend->setElementVisible(m_btnConflictCancel, false);

    hide();
}

// ---------------------------------------------------------------------------
// openTab
// ---------------------------------------------------------------------------
void KeyBindingsPanel::openTab(const KeyBindings& currentBindings) {
    copyMutableBindings(m_bindings,  currentBindings);
    copyMutableBindings(m_snapshot, currentBindings);
    m_captureState = CaptureState::Idle;
    m_capturingRow = -1;
    m_candidateKey.clear();
    m_conflictRow = -1;

    // Hide all inline messages
    for (int i = 0; i < kNumCapturable; ++i) {
        if (m_rowMsg[i] != kInvalidUIElement) {
            m_backend->setElementText(m_rowMsg[i], "");
            m_backend->setElementVisible(m_rowMsg[i], false);
        }
    }
    hideConflictButtons();
    refreshChipLabels();
    show();
}

// ---------------------------------------------------------------------------
// show / hide
// ---------------------------------------------------------------------------
void KeyBindingsPanel::show() {
    m_visible = true;
    if (!m_backend) return;
    for (int i = 0; i < kTotalRows; ++i) {
        if (m_rowLabel[i] != kInvalidUIElement) m_backend->setElementVisible(m_rowLabel[i], true);
        if (m_rowChip[i]  != kInvalidUIElement) m_backend->setElementVisible(m_rowChip[i],  true);
    }
    // Inline messages and conflict buttons remain hidden until triggered.
}

void KeyBindingsPanel::hide() {
    m_visible = false;
    if (!m_backend) return;
    for (int i = 0; i < kTotalRows; ++i) {
        if (m_rowLabel[i] != kInvalidUIElement) m_backend->setElementVisible(m_rowLabel[i], false);
        if (m_rowChip[i]  != kInvalidUIElement) m_backend->setElementVisible(m_rowChip[i],  false);
    }
    for (int i = 0; i < kNumCapturable; ++i) {
        if (m_rowMsg[i] != kInvalidUIElement) m_backend->setElementVisible(m_rowMsg[i], false);
    }
    m_backend->setElementVisible(m_btnSwap,           false);
    m_backend->setElementVisible(m_btnConflictCancel, false);

    cancelCapture();
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void KeyBindingsPanel::draw() {
    if (!m_visible || !m_backend) return;
    // Visual state updates happen in onEvent / helper methods.
    // No per-frame work needed beyond what the backend already tracks.
}

// ---------------------------------------------------------------------------
// onEvent
// ---------------------------------------------------------------------------
bool KeyBindingsPanel::onEvent(const InputEvent& event) {
    if (!m_visible || !m_backend) return false;

    // --- Key events during capture ---
    if (event.type == InputEvent::Type::KeyDown) {
        int key = event.keyCode;

        if (m_captureState == CaptureState::Capturing) {
            if (key == 27) { // Escape
                // Cancel capture and return FALSE so SettingsPanel's Escape
                // handler fires and closes Settings in the same event frame.
                cancelCapture();
                return false;
            }

            // Convert key code to a key-string identifier.
            // Use the same SDL2-style identifiers as KeyBindings.
            std::string candidate;
            switch (key) {
                case 38: candidate = "ArrowUp";    break;
                case 40: candidate = "ArrowDown";  break;
                case 37: candidate = "ArrowLeft";  break;
                case 39: candidate = "ArrowRight"; break;
                default:
                    if (key >= 65 && key <= 90) {
                        candidate = std::string(1, static_cast<char>(key));
                    } else if (key == 32) {
                        candidate = "Space";
                    } else if (key == 187 || key == 61) {
                        candidate = "+";
                    } else if (key == 189 || key == 173) {
                        candidate = "-";
                    } else {
                        // Unrecognised key — remain in Capturing state.
                        return true;
                    }
                    break;
            }

            // --- Q/E guard ---
            if (candidate == "Q" || candidate == "E") {
                // Remain in Capturing state; show inline reserved error.
                m_hasReservedKeyError = true;
                if (m_capturingRow >= 0 && m_capturingRow < kNumCapturable) {
                    if (m_rowMsg[m_capturingRow] != kInvalidUIElement) {
                        m_backend->setElementText(m_rowMsg[m_capturingRow],
                            "This key is reserved and cannot be assigned");
                        m_backend->setElementTextColor(m_rowMsg[m_capturingRow], 0xF0, 0x4E, 0x37);
                        m_backend->setElementVisible(m_rowMsg[m_capturingRow], true);
                    }
                }
                return true;
            }

            // --- Conflict detection ---
            int conflictRow = findRowWithKey(candidate);
            if (conflictRow != -1 && conflictRow != m_capturingRow) {
                m_candidateKey = candidate;
                m_conflictRow  = conflictRow;
                m_captureState = CaptureState::ConflictPending;

                // Show inline conflict text
                if (m_capturingRow >= 0 && m_capturingRow < kNumCapturable
                    && m_rowMsg[m_capturingRow] != kInvalidUIElement) {
                    std::string msg = "Key already used by: ";
                    msg += kActionName[conflictRow];
                    m_backend->setElementText(m_rowMsg[m_capturingRow], msg);
                    m_backend->setElementTextColor(m_rowMsg[m_capturingRow], 0xF0, 0x4E, 0x37);
                    m_backend->setElementVisible(m_rowMsg[m_capturingRow], true);
                }

                showConflictButtons(m_capturingRow);
                return true;
            }

            // --- No conflict: apply immediately ---
            if (m_capturingRow >= 0 && m_capturingRow < kNumCapturable) {
                m_bindings.*kBindingField[m_capturingRow] = candidate;
                clearRowMsg(m_capturingRow);
                refreshChip(m_capturingRow);
            }
            m_captureState = CaptureState::Idle;
            m_capturingRow = -1;
            m_candidateKey.clear();
            return true;
        }

        if (m_captureState == CaptureState::ConflictPending) {
            // Consume all key events while conflict dialog is shown.
            if (key == 27) {
                // Escape: cancel conflict, return to Idle.
                cancelCapture();
                return false; // Let SettingsPanel's Escape close Settings too.
            }
            return true;
        }
    }

    // --- Mouse clicks ---
    if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
        int mx = event.x;
        int my = event.y;

        auto hitTest = [this](int mx, int my, UIElementHandle h) -> bool {
            if (h == kInvalidUIElement) return false;
            UIRect r = m_backend->getElementRect(h);
            return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
        };

        // --- Conflict buttons ---
        if (m_captureState == CaptureState::ConflictPending) {
            if (hitTest(mx, my, m_btnSwap)) {
                applySwap();
                return true;
            }
            if (hitTest(mx, my, m_btnConflictCancel)) {
                cancelCapture();
                return true;
            }
            // Consume all other clicks while conflict is pending.
            return true;
        }

        // --- Capturable row chips ---
        for (int i = 0; i < kNumCapturable; ++i) {
            if (hitTest(mx, my, m_rowChip[i])) {
                if (m_captureState == CaptureState::Capturing && m_capturingRow == i) {
                    // Second click on active chip: cancel this capture.
                    cancelCapture();
                } else {
                    if (m_captureState == CaptureState::Capturing && m_capturingRow != i) {
                        // Cancel previous capture before starting a new one.
                        cancelCapture();
                    }
                    enterCapture(i);
                }
                return true;
            }
        }

        // Informational and reserved row chips: no click effect (consume silently).
        for (int i = kNumCapturable; i < kTotalRows; ++i) {
            if (hitTest(mx, my, m_rowChip[i])) {
                return true; // Consumed but no action.
            }
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// hasConflict
// ---------------------------------------------------------------------------
bool KeyBindingsPanel::hasConflict() const {
    return m_captureState == CaptureState::ConflictPending;
}

// ---------------------------------------------------------------------------
// revertToSnapshot
// ---------------------------------------------------------------------------
void KeyBindingsPanel::revertToSnapshot() {
    copyMutableBindings(m_bindings, m_snapshot);
    m_captureState = CaptureState::Idle;
    m_capturingRow = -1;
    m_candidateKey.clear();
    m_conflictRow = -1;
    if (m_backend) {
        for (int i = 0; i < kNumCapturable; ++i) {
            clearRowMsg(i);
        }
        hideConflictButtons();
        refreshChipLabels();
    }
}

// ---------------------------------------------------------------------------
// resetToDefaults
// ---------------------------------------------------------------------------
void KeyBindingsPanel::resetToDefaults() {
    // Default-construct a temporary and copy only the mutable fields.
    // Direct assignment is not possible because KeyBindings::undo and
    // KeyBindings::save are const, which deletes the copy-assignment operator.
    KeyBindings defaults{};
    copyMutableBindings(m_bindings, defaults);
    m_captureState = CaptureState::Idle;
    m_capturingRow = -1;
    m_candidateKey.clear();
    m_conflictRow = -1;
    if (m_backend) {
        for (int i = 0; i < kNumCapturable; ++i) {
            clearRowMsg(i);
        }
        hideConflictButtons();
        refreshChipLabels();
    }
}

// ---------------------------------------------------------------------------
// refreshChipLabels
// ---------------------------------------------------------------------------
void KeyBindingsPanel::refreshChipLabels() {
    if (!m_backend) return;
    for (int i = 0; i < kNumCapturable; ++i) {
        refreshChip(i);
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::string KeyBindingsPanel::keyForRow(int row) const {
    if (row < 0 || row >= kNumCapturable) return "";
    return m_bindings.*kBindingField[row];
}

int KeyBindingsPanel::findRowWithKey(const std::string& key) const {
    for (int i = 0; i < kNumCapturable; ++i) {
        if (keyForRow(i) == key) return i;
    }
    return -1;
}

void KeyBindingsPanel::enterCapture(int row) {
    m_captureState = CaptureState::Capturing;
    m_capturingRow = row;
    m_candidateKey.clear();
    m_conflictRow  = -1;

    if (!m_backend) return;

    // Clear any previous inline message for this row.
    clearRowMsg(row);

    // Update chip label to "Press a key…" (teal wash visual).
    if (m_rowLabel[row] != kInvalidUIElement) {
        m_backend->setElementText(m_rowLabel[row], "Press a key…");
        m_backend->setElementTextColor(m_rowLabel[row], 0x00, 0xC9, 0xC8); // teal
    }
    if (m_rowChip[row] != kInvalidUIElement) {
        m_backend->setElementText(m_rowChip[row], "…");
    }
}

void KeyBindingsPanel::cancelCapture() {
    if (!m_backend) return;

    if (m_capturingRow >= 0 && m_capturingRow < kNumCapturable) {
        // Restore the row label to the action name.
        m_backend->setElementText(m_rowLabel[m_capturingRow], kActionName[m_capturingRow]);
        m_backend->setElementTextColor(m_rowLabel[m_capturingRow], 0xEB, 0xF4, 0xF6);
        clearRowMsg(m_capturingRow);
        refreshChip(m_capturingRow);
    }

    hideConflictButtons();

    m_captureState        = CaptureState::Idle;
    m_capturingRow        = -1;
    m_candidateKey.clear();
    m_conflictRow         = -1;
    m_hasReservedKeyError = false;
}

void KeyBindingsPanel::showConflictButtons(int row) {
    if (!m_backend) return;

    // Position Swap/Cancel buttons on the row below the conflict message.
    UIRect msgRect{kContentX, kContentY + row * (kLineH + kRowGap) + kLineH, kContentW, kLineH};
    if (m_rowMsg[row] != kInvalidUIElement) {
        msgRect = m_backend->getElementRect(m_rowMsg[row]);
    }

    const int btnY = msgRect.y + kLineH + 2;
    m_backend->setElementRect(m_btnSwap,           kContentX,           btnY, 160, kLineH);
    m_backend->setElementRect(m_btnConflictCancel, kContentX + 168,     btnY, 100, kLineH);

    m_backend->setElementVisible(m_btnSwap,           true);
    m_backend->setElementVisible(m_btnConflictCancel, true);
}

void KeyBindingsPanel::hideConflictButtons() {
    if (!m_backend) return;
    if (m_btnSwap           != kInvalidUIElement) m_backend->setElementVisible(m_btnSwap,           false);
    if (m_btnConflictCancel != kInvalidUIElement) m_backend->setElementVisible(m_btnConflictCancel, false);
}

void KeyBindingsPanel::clearRowMsg(int row) {
    if (!m_backend || row < 0 || row >= kNumCapturable) return;
    if (m_rowMsg[row] != kInvalidUIElement) {
        m_backend->setElementText(m_rowMsg[row], "");
        m_backend->setElementVisible(m_rowMsg[row], false);
    }
}

void KeyBindingsPanel::refreshChip(int row) {
    if (!m_backend || row < 0 || row >= kNumCapturable) return;
    if (m_rowChip[row] != kInvalidUIElement) {
        m_backend->setElementText(m_rowChip[row], keyForRow(row));
    }
    // Also restore action name label colour (in case it was changed during capture).
    if (m_rowLabel[row] != kInvalidUIElement) {
        m_backend->setElementText(m_rowLabel[row], kActionName[row]);
        m_backend->setElementTextColor(m_rowLabel[row], 0xEB, 0xF4, 0xF6);
    }
}

void KeyBindingsPanel::applySwap() {
    if (!m_backend) return;
    if (m_capturingRow < 0 || m_capturingRow >= kNumCapturable) return;
    if (m_conflictRow  < 0 || m_conflictRow  >= kNumCapturable) return;

    // Atomically exchange the two bindings.
    std::string oldKey = keyForRow(m_capturingRow);
    std::string newKey = m_candidateKey;

    m_bindings.*kBindingField[m_capturingRow] = newKey;
    m_bindings.*kBindingField[m_conflictRow]  = oldKey;

    // Clear conflict state.
    clearRowMsg(m_capturingRow);
    hideConflictButtons();

    // Refresh both chips.
    refreshChip(m_capturingRow);
    refreshChip(m_conflictRow);

    m_captureState = CaptureState::Idle;
    m_capturingRow = -1;
    m_candidateKey.clear();
    m_conflictRow  = -1;
}
