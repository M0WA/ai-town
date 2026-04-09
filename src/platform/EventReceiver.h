#pragma once
#include <irrlicht.h>
#include "src/platform/input_event.h"   // InputEvent

// Forward declarations — full includes are in EventReceiver.cpp
class UIScaler;
class UIManager;
class CameraController;
class IrrlichtUIBackend;  // for handleGuiHoverEvent() hover sprite swapping

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
//     Always set m_rmbDragActive=true AND forward to CameraController — UIManager is NOT
//     called on RMB down. Tool cancel is deferred to RMB up (click detection).
//
//   RMB MouseButtonUp (button=1):
//     Clear m_rmbDragActive. If m_rmbMoved is false (no movement = short click), call
//     UIManager::onEvent() so it can cancel the active tool (Priority 6b). If m_rmbMoved
//     is true (drag), skip UIManager — camera was panning, do not cancel tool.
//     ALWAYS forward to CameraController regardless of UIManager result — CameraController
//     MUST receive RMB up to clear its own drag state; omitting this call leaves its
//     m_rmbDragActive stuck true and mouse moves rotate the camera with no button held.
//
//   All other events: UIManager::onEvent() first; forward to CameraController if not consumed.
//
// m_rmbDragActive and m_mmbDragActive MUST be initialized to false (declared with initializer).
class EventReceiver : public irr::IEventReceiver {
public:
    // Constructor — all pointers may be null (null-safe dispatch).
    // uiBackend: may be null; when non-null, EET_GUI_EVENT hover events are
    // forwarded to IrrlichtUIBackend::handleGuiHoverEvent() for sprite swapping.
    EventReceiver(UIScaler* scaler, UIManager* uiManager, CameraController* camera,
                  IrrlichtUIBackend* uiBackend = nullptr);

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
    UIScaler*          m_scaler;
    UIManager*         m_uiManager;
    CameraController*  m_camera;
    IrrlichtUIBackend* m_uiBackend{nullptr};  // for hover sprite swapping (may be null)

    // Drag state — MUST be initialized to false.
    bool m_rmbDragActive{false};  // true while RMB (button=1) is held down
    bool m_rmbMoved{false};       // true if mouse moved during current RMB press (drag vs click)
    bool m_mmbDragActive{false};  // true while MMB (button=2) is held down

    // Phase 11q3 — extracted helpers for OnEvent (S3776 + S134)
    bool handleGuiEvent(const irr::SEvent::SGUIEvent& guiEvt, InputEvent& out);
    bool handleKeyboardEvent(const irr::SEvent::SKeyInput& key, InputEvent& out);
    bool handleMouseEvent(const irr::SEvent::SMouseInput& mouse, InputEvent& out);
};
