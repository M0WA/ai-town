#pragma once

#include "src/ui/IUIBackend.h"      // UIElementHandle, kInvalidUIElement, Rect
#include "src/interfaces/IClock.h"  // IClock — full include (available at Phase 0)

// Forward declarations — do NOT #include these headers in UIManager.h.
// IAudioSystem and ICitySimulation are declared as pointers only.
// InputEvent is used as a const-reference parameter — forward declaration is valid in C++.
class IAudioSystem;
class ICitySimulation;

// INCLUDE PROHIBITION: Do NOT replace this forward declaration with
// #include "src/platform/input_event.h". The platform header must not
// be pulled into every translation unit that includes UIManager.h —
// doing so creates circular-include ambiguity at Phase 3 when UIManager
// is included by simulation and audio components that must not depend on
// the platform layer. The full include belongs in UIManager.cpp ONLY.
struct InputEvent;

// UIManager — Phase 1 compile-target stub.
// Phase 3 replaces the stub bodies with the full shell implementation without
// changing any of the 4 Phase 1 method signatures (constructor, onEvent, draw, update).
// Phase 3 WILL add new method declarations (transitionToGameplay, showForcedLoanDialog,
// etc.) — this is expected and does not violate the "no Phase 1 method signature changes"
// guarantee.
//
// Source location: src/ui/ (IUIBackend.h lives here; UIManager depends on it).
// IrrlichtUIBackend lives in src/rendering/ (Irrlicht headers).
//
// NOTE — Phase 8 header additions: Phase 8 will add #include 'src/ui/ui_types.h' and
// #include 'src/interfaces/LoanTerms.h' to UIManager.h — these are expected additions
// and do not violate the Phase 1 four-method signature lock.
class UIManager {
public:
    // 4-parameter constructor from architecture/ui-ux/ui-manager.md.
    // Stores all pointers (may be null). Returns immediately.
    // Signature is LOCKED at Phase 1 — Phase 3 does not change it.
    UIManager(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock);

    // Handle an input event. Returns true if consumed (event should not propagate).
    // WindowFocusGained/Lost events: always return false (pass-through at Priority 1).
    // Phase 1 stub: returns false for all events.
    bool onEvent(const InputEvent& event);

    // Issue explicit per-panel draw calls in Z-order via IUIBackend.
    // Called by IrrlichtRenderer::drawScene() INSIDE the beginScene/endScene pair.
    // m_gui->drawAll() is NOT called — that would bypass the explicit Z-order layering
    // required for the background scrim and modal overlay.
    // Phase 1 stub: no-op.
    void draw();

    // Per-frame UI state update (undo countdown, grace-period indicator, notification timers).
    // Called BEFORE beginFrame() per architecture/ui-ux/ui-manager.md.
    // Uses m_clock->nowSeconds() for undo expiry — NOT accumulated realDeltaSeconds.
    // Phase 1 stub: no-op.
    void update(float realDeltaSeconds);

private:
    IUIBackend*     m_backend;
    IAudioSystem*   m_audio;
    ICitySimulation* m_sim;
    IClock*         m_clock;
};
