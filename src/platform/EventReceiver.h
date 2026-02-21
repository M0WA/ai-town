#pragma once
#include <irrlicht.h>
#include "src/platform/input_event.h"   // InputEvent

// Forward declarations — full includes are in EventReceiver.cpp
class UIScaler;
class UIManager;
class CameraController;

// EventReceiver — translates Irrlicht SEvent to InputEvent and dispatches per the
// 6-priority input arbitration chain (architecture/ui-ux/input-arbitration.md).
//
// Dispatch contract (MUST be authored at Phase 1 — Phase 3 integration MUST NOT restructure):
//
//   Focus events (WindowFocusGained/Lost):
//     Step 1: call UIManager::onEvent() — UIManager MUST see these for its dispatch guard.
//     Step 2: unconditionally forward to CameraController (return value from UIManager discarded).
//
//   Camera pass-through (Priority 1):
//     - Scroll wheel: direct to CameraController, UIManager NOT called.
//     - MMB drag (MouseButtonDown/Up button=2, MouseMove while m_mmbDragActive):
//       direct to CameraController (m_mmbDragActive tracks button state).
//     - RMB drag (MouseMove while m_rmbDragActive): direct to CameraController.
//
//   RMB MouseButtonDown (button=1):
//     UIManager::onEvent() called FIRST. If false (not consumed): set m_rmbDragActive=true
//     AND forward to CameraController in the same branch (atomicity — split branches = bug).
//     If UIManager returns true (scrim consumed): m_rmbDragActive NOT set, not forwarded.
//     NOTE: UIManager returning true for LMB/RMB while modal is active is CORRECT behavior
//     (scrim element consuming at Priority 1 per input-arbitration.md). Phase 8 MUST NOT
//     reinterpret UIManager returning true for those events as a bug.
//
//   All other events: UIManager::onEvent() first; forward to CameraController if not consumed.
//
// m_rmbDragActive and m_mmbDragActive MUST be initialized to false (declared with initializer).
class EventReceiver : public irr::IEventReceiver {
public:
    // Constructor — all pointers may be null (null-safe dispatch).
    EventReceiver(UIScaler* scaler, UIManager* uiManager, CameraController* camera);

    // IEventReceiver override — called by Irrlicht for every input event.
    bool OnEvent(const irr::SEvent& event) override;

    // dispatchFocusEvent — delivers WindowFocusGained or WindowFocusLost to the dispatch chain.
    // Called directly from the platform window loop since the Irrlicht vcpkg port (based on
    // Irrlicht 1.8.x) does NOT expose EET_APPLICATION_EVENT / EAET_FOCUS_GAINED / EAET_FOCUS_LOST.
    // Dispatch contract (same as if delivered via EET_APPLICATION_EVENT):
    //   Step 1: UIManager::onEvent() — result DISCARDED (UIManager sees event for guard).
    //   Step 2: CameraController::OnInputEvent() — unconditional.
    void dispatchFocusEvent(bool gained);

private:
    UIScaler*        m_scaler;
    UIManager*       m_uiManager;
    CameraController* m_camera;

    // Drag state — MUST be initialized to false.
    bool m_rmbDragActive{false};  // true while RMB (button=1) is held down
    bool m_mmbDragActive{false};  // true while MMB (button=2) is held down
};
