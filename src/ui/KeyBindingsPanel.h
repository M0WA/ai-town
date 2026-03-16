#pragma once
#include "src/interfaces/IUIBackend.h"  // UIElementHandle, IUIBackend
#include "src/ui/key_bindings.h"        // KeyBindings

// Forward declarations
struct InputEvent;
class ModalDialog;

// KeyBindingsPanel — Controls tab content panel owned by SettingsPanel.
//
// Renders the full rebinding table within the Controls tab content area and
// owns the capture-mode state machine.
//
// Row types:
//   Capturable     — 11 actions with clickable key chips (Z, R, U, D, I, T, B,
//                    ArrowUp, ArrowDown, ArrowLeft, ArrowRight)
//   Informational  — 2 non-rebindable rows (Ctrl+Z, Ctrl+S)
//   Reserved       — 2 reserved rows (Q, E) — not Tab-navigable, no click effect
//
// Capture state machine: Idle → Capturing → ConflictPending → Idle
//
// Input interception priority: KeyBindingsPanel::onEvent() is called BEFORE
// SettingsPanel::onEvent() for all key events when m_state != Idle, so that
// Escape during capture cancels the capture first; the Escape then falls
// through to SettingsPanel's Escape handler which closes Settings.
// Both effects occur in the same event frame per input-arbitration.md Priority 5.
class KeyBindingsPanel {
public:
    KeyBindingsPanel(IUIBackend* backend, ModalDialog* modal);

    // Refresh the panel for the Controls tab.
    // Called by SettingsPanel::showControlsTab().
    // Takes a snapshot of bindings at tab-open time (for Cancel revert).
    void openTab(const KeyBindings& currentBindings);

    void show();
    void hide();
    void draw();

    // Returns true if the event was consumed by capture-mode logic.
    // Escape during capture: cancels capture and returns FALSE so SettingsPanel's
    // Escape handler fires and closes Settings in the same frame.
    bool onEvent(const InputEvent& event);

    // Apply current in-memory bindings to the output struct and persist via the
    // write callback.  Returns false if a conflict exists (Apply is grayed).
    bool hasConflict() const;

    // Returns a copy of the current in-memory bindings (applied or pending).
    const KeyBindings& getCurrentBindings() const { return m_bindings; }
    // Alias for test convenience.
    const KeyBindings& currentBindings() const { return m_bindings; }

    // Revert to the snapshot taken at openTab() time (Cancel button).
    void revertToSnapshot();

    // Reset all bindings to KeyBindings defaults (Restore Defaults button).
    void resetToDefaults();

    // Refresh all chip labels from m_bindings (called after WASD preset apply).
    void refreshChipLabels();

    // Confirm the pending swap (public for direct test invocation).
    void applySwap();

    // --- Testability accessors ---
    bool isVisible()          const { return m_visible; }
    bool isCapturing()        const { return m_captureState == CaptureState::Capturing; }
    bool isConflictPending()  const { return m_captureState == CaptureState::ConflictPending; }
    int  capturingRowIndex()  const { return m_capturingRow; }
    bool hasReservedKeyError()const { return m_hasReservedKeyError; }
    bool isApplyEnabled()     const { return !hasConflict(); }

private:
    IUIBackend* m_backend{nullptr};
    ModalDialog* m_modal{nullptr};
    bool m_visible{false};

    // In-memory bindings (may differ from persisted state during an open session).
    KeyBindings m_bindings;

    // Snapshot taken at openTab() — used by Cancel to revert.
    KeyBindings m_snapshot;

    // --- Capture state machine ---
    enum class CaptureState { Idle, Capturing, ConflictPending };
    CaptureState m_captureState{CaptureState::Idle};

    // Index of the row currently in Capturing or ConflictPending state.
    // -1 when Idle.
    int m_capturingRow{-1};

    // Candidate key string during ConflictPending state.
    std::string m_candidateKey;

    // Index of the conflicting row (used for swap).
    int m_conflictRow{-1};

    // Content area origin passed from SettingsPanel layout constants.
    // Matches kContentX / kContentY / kContentW / kLineH from SettingsPanel.cpp.
    static constexpr int kContentX = 360 + 16;  // kSettingsX + 16
    static constexpr int kContentY = 140 + 50;  // kSettingsY + 50
    static constexpr int kContentW = 1200 - 32; // kSettingsW - 32
    static constexpr int kLineH    = 32;
    static constexpr int kChipW    = 180;
    static constexpr int kRowGap   = 4;

    // Row count constants
    static constexpr int kNumCapturable    = 11;
    static constexpr int kNumInfoRows      = 2;
    static constexpr int kNumReservedRows  = 2;
    static constexpr int kTotalRows = kNumCapturable + kNumInfoRows + kNumReservedRows;

    // Per-row UI element handles
    UIElementHandle m_rowLabel[kTotalRows]{};
    UIElementHandle m_rowChip[kTotalRows]{};
    // Inline message label per capturable row (conflict text, reserved error)
    UIElementHandle m_rowMsg[kNumCapturable]{};

    // Conflict swap/cancel buttons (shared, repositioned per active row)
    UIElementHandle m_btnSwap{kInvalidUIElement};
    UIElementHandle m_btnConflictCancel{kInvalidUIElement};

    // Reserved-error inline label (shared across Q and E rows — only one active at a time)
    // Displayed inline in the row, always visible for reserved rows.
    // Q row = index kNumCapturable + kNumInfoRows + 0 = 13
    // E row = index kNumCapturable + kNumInfoRows + 1 = 14

    // Action name strings for capturable rows (index 0..10)
    static const char* kActionName[kNumCapturable];

    // Maps capturable row index → pointer-to-member in KeyBindings
    using BindingField = std::string KeyBindings::*;
    static BindingField kBindingField[kNumCapturable];

    // Helper: return the current key string for capturable row i.
    std::string keyForRow(int row) const;

    // Helper: find capturable row bound to the given key string.
    // Returns -1 if not found.
    int findRowWithKey(const std::string& key) const;

    // Helper: set a row to the Capturing visual state.
    void enterCapture(int row);

    // Helper: cancel capture and return to Idle.
    void cancelCapture();

    // Helper: show/hide conflict buttons near the given row.
    void showConflictButtons(int row);
    void hideConflictButtons();

    // Helper: clear the inline message for row i.
    void clearRowMsg(int row);

    // Helper: refresh a single chip label from current bindings.
    void refreshChip(int row);

    bool m_hasReservedKeyError{false};
};
