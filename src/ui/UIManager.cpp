#include "UIManager.h"
#include "src/platform/input_event.h"  // Full include here (NOT in UIManager.h — see prohibition comment)
#include "src/ui/ui_constants.h"       // kToolbarLeft, kToolbarRight, kToolbarTop, kToolbarBottom

UIManager::UIManager(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock)
    : m_backend(backend)
    , m_audio(audio)
    , m_sim(sim)
    , m_clock(clock)
{}

bool UIManager::onEvent(const InputEvent& event) {
    // Per input-arbitration.md: WindowFocusGained/Lost MUST pass through before
    // Priority 1 — never consumed by any priority level, including modal-active state.
    // This guard MUST be preserved verbatim in the Phase 3 shell.
    if (event.type == InputEvent::Type::WindowFocusGained ||
        event.type == InputEvent::Type::WindowFocusLost) {
        return false;
    }
    return false; // Phase 1 stub: pass-through for all other events
}

void UIManager::draw() {
    // Phase 1 stub: no-op.
    // NOTE: All panel stub draw() bodies added in Phases 1-3 MUST call at least one
    // IUIBackend method — empty {} bodies will cause the Phase 4 25% src/ui/ coverage
    // gate to fail. See architecture/testing/coverage.md IMPLEMENTER CONSTRAINT section.
    // UIManager draw() itself is a legitimate no-op at Phase 1 (no panels registered yet).
}

void UIManager::update(float /*realDeltaSeconds*/) {
    // Phase 1 stub: no-op.
    // Phase 8 implementation MUST call m_clock->nowSeconds() for undo expiry countdown
    // (NOT accumulate realDeltaSeconds — accumulated deltas drift over time).
    // Phase 6 forward-reference: remainingSeconds = ICitySimulation::getUndoExpiryTimeSeconds() - m_clock->nowSeconds()
}
