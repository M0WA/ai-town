// EventReceiver.cpp — Irrlicht SEvent → InputEvent translation and dispatch.
#include "EventReceiver.h"

#include "src/ui/UIScaler.h"                    // UIScaler::unproject(), getViewportRect()
#include "src/ui/UIManager.h"                   // UIManager::onEvent()
#include "src/ui/CameraController.h"            // CameraController::OnInputEvent()
#include "src/rendering/IrrlichtUIBackend.h"    // IrrlichtUIBackend::handleGuiHoverEvent()

EventReceiver::EventReceiver(UIScaler* scaler, UIManager* uiManager, CameraController* camera,
                             IrrlichtUIBackend* uiBackend)
    : m_scaler(scaler)
    , m_uiManager(uiManager)
    , m_camera(camera)
    , m_uiBackend(uiBackend)
{
}

// ---------------------------------------------------------------------------
// OnEvent — extracted helpers (Phase 11q3)
// ---------------------------------------------------------------------------

bool EventReceiver::handleGuiEvent(const irr::SEvent::SGUIEvent& guiEvt, InputEvent& /*out*/) {
    // --- Hover sprite swapping (Phase 10c Glass City Colour Pass) ---
    // Forward EGET_ELEMENT_HOVERED / EGET_ELEMENT_LEFT to IrrlichtUIBackend
    // so it can swap button sprite cells for hover visual feedback.
    // Must be called before the EGET_BUTTON_CLICKED handler (order does not
    // matter for correctness, but hover events arrive before click events).
    // Returns false always — hover events must not be consumed here.
    if (m_uiBackend) {
        // handleGuiHoverEvent expects a full SEvent; reconstruct it from the sub-struct.
        // However, this helper receives only the SGUIEvent sub-struct. Since
        // IrrlichtUIBackend::handleGuiHoverEvent() only accesses event.GUIEvent,
        // we call the backend from OnEvent directly before dispatching here.
        // This method handles only EGET_BUTTON_CLICKED.
    }

    if (guiEvt.EventType == irr::gui::EGET_BUTTON_CLICKED) {
        irr::gui::IGUIElement* btn = guiEvt.Caller;
        if (btn && m_scaler) {
            // Physical centre of the button.
            irr::core::rect<irr::s32> r = btn->getAbsolutePosition();
            const int physCx = (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2;
            const int physCy = (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2;
            UIScaler::VirtualPoint vp = m_scaler->unproject(physCx, physCy);
            InputEvent btnEv{};
            btnEv.type   = InputEvent::Type::MouseButtonDown;
            btnEv.button = 0;
            btnEv.x      = vp.x;
            btnEv.y      = vp.y;
            btnEv.physX  = physCx;
            btnEv.physY  = physCy;
            if (m_uiManager) m_uiManager->onEvent(btnEv);
        }
        return false;  // let Irrlicht finish its own GUI handling
    }

    return false;  // never consume other GUI events
}

bool EventReceiver::handleKeyboardEvent(const irr::SEvent::SKeyInput& key, InputEvent& out) {
    out.keyCode = static_cast<int>(key.Key);
    if (key.PressedDown) {
        out.type = InputEvent::Type::KeyDown;
    } else {
        out.type = InputEvent::Type::KeyUp;
    }
    if (m_uiManager && m_uiManager->onEvent(out)) return true;
    if (m_camera) m_camera->OnInputEvent(out);
    return false;
}

bool EventReceiver::handleMouseEvent(const irr::SEvent::SMouseInput& mouse, InputEvent& out) {
    // Raw physical coordinates from SEvent.
    const int physX = mouse.X;
    const int physY = mouse.Y;

    // Virtual coordinates via UIScaler::unproject().
    int virtX = 0, virtY = 0;
    if (m_scaler) {
        UIScaler::VirtualPoint vp = m_scaler->unproject(physX, physY);
        virtX = vp.x;
        virtY = vp.y;
    }

    out.physX = physX;
    out.physY = physY;
    out.x     = virtX;
    out.y     = virtY;

    // Phase 11q5: pure dispatch switch — one helper call per case (S3776 CC ≤ 10).
    switch (mouse.Event) {
    case irr::EMIE_LMOUSE_PRESSED_DOWN: return handleLMBDown(out);
    case irr::EMIE_RMOUSE_PRESSED_DOWN: return handleRMBDown(out);
    case irr::EMIE_MMOUSE_PRESSED_DOWN: return handleMMBDown(out);
    case irr::EMIE_LMOUSE_LEFT_UP:     return handleLMBUp(out);
    case irr::EMIE_RMOUSE_LEFT_UP:     return handleRMBUp(out);
    case irr::EMIE_MMOUSE_LEFT_UP:     return handleMMBUp(out);
    case irr::EMIE_MOUSE_MOVED:        return handleMouseMoved(out);
    case irr::EMIE_MOUSE_WHEEL:
        out.wheelDelta = mouse.Wheel;
        return handleMouseWheel(out);
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Phase 11q5 — per-case mouse-event helpers (extracted from handleMouseEvent)
// ---------------------------------------------------------------------------

bool EventReceiver::handleLMBDown(InputEvent& out) {
    out.type   = InputEvent::Type::MouseButtonDown;
    out.button = 0;  // LMB
    // UIManager first; if consumed, do not forward to CameraController.
    if (m_uiManager && m_uiManager->onEvent(out)) return true;
    if (m_camera) m_camera->OnInputEvent(out);
    return false;
}

bool EventReceiver::handleRMBDown(InputEvent& out) {
    out.type   = InputEvent::Type::MouseButtonDown;
    out.button = 1;  // RMB
    // Suppress camera drag when not in gameplay or paused (e.g., main menu).
    if (!m_uiManager || !m_uiManager->isGameplayOrPaused()) {
        return false;
    }
    // Always start camera drag immediately — tool cancel is deferred to RMB up
    // (only when no movement occurred, i.e. a short click, not a drag).
    // UIManager is NOT called on RMB down; it sees RMB up only for click-cancel.
    m_rmbDragActive = true;
    m_rmbMoved = false;
    if (m_camera) m_camera->OnInputEvent(out);
    return false;
}

bool EventReceiver::handleMMBDown(InputEvent& out) {
    out.type   = InputEvent::Type::MouseButtonDown;
    out.button = 2;  // MMB — camera pass-through, no UIManager call
    m_mmbDragActive = true;
    if (m_camera) m_camera->OnInputEvent(out);
    return false;
}

bool EventReceiver::handleLMBUp(InputEvent& out) {
    out.type   = InputEvent::Type::MouseButtonUp;
    out.button = 0;
    if (m_uiManager && m_uiManager->onEvent(out)) return true;
    if (m_camera) m_camera->OnInputEvent(out);
    return false;
}

bool EventReceiver::handleRMBUp(InputEvent& out) {
    out.type   = InputEvent::Type::MouseButtonUp;
    out.button = 1;
    m_rmbDragActive = false;
    // Only notify UIManager on RMB click (no movement during press).
    // If m_rmbMoved is true the user was dragging the camera — do not cancel tool.
    bool consumed = false;
    if (!m_rmbMoved && m_uiManager) {
        consumed = m_uiManager->onEvent(out);
    }
    // Always forward to CameraController — it MUST receive RMB up to clear
    // its own m_rmbDragActive. Without this, a short RMB click (UIManager
    // consumes the up event) leaves CameraController in drag state and
    // subsequent mouse moves rotate the camera with no button held.
    if (m_camera) m_camera->OnInputEvent(out);
    return consumed;
}

bool EventReceiver::handleMMBUp(InputEvent& out) {
    out.type   = InputEvent::Type::MouseButtonUp;
    out.button = 2;
    m_mmbDragActive = false;
    // MMB pass-through — no UIManager call.
    if (m_camera) m_camera->OnInputEvent(out);
    return false;
}

bool EventReceiver::handleMouseMoved(InputEvent& out) {
    out.type = InputEvent::Type::MouseMove;
    // Camera pass-through during MMB or RMB drag (Priority 1 — bypasses UIManager).
    if (m_mmbDragActive || m_rmbDragActive) {
        if (m_rmbDragActive) m_rmbMoved = true;
        if (m_camera) m_camera->OnInputEvent(out);
        return false;
    }
    // Normal mouse move: UIManager first, then CameraController if not consumed.
    if (m_uiManager && m_uiManager->onEvent(out)) return true;
    if (m_camera) m_camera->OnInputEvent(out);
    return false;
}

bool EventReceiver::handleMouseWheel(InputEvent& out) {
    out.type = InputEvent::Type::MouseWheel;
    // Scroll wheel: camera pass-through, UIManager NOT called.
    if (m_camera) m_camera->OnInputEvent(out);
    return false;
}

// ---------------------------------------------------------------------------
// OnEvent
// ---------------------------------------------------------------------------

bool EventReceiver::OnEvent(const irr::SEvent& event) {
    InputEvent ev{};

    // -------------------------------------------------------------------------
    // GUI button click events — MUST be handled here.
    //
    // Irrlicht's GUI environment processes mouse events BEFORE calling the user
    // EventReceiver (in CIrrDeviceLinux/CIrrDeviceWin32::OnEvent the order is:
    //   1. GUIEnvironment->postEventFromUser(event) — if returns true → STOP
    //   2. Receiver->OnEvent(event)
    // This means any click on an IGUIButton is consumed by the GUI layer and the
    // corresponding EMIE_LMOUSE_PRESSED_DOWN event NEVER reaches OnEvent.
    // Solution: handle EET_GUI_EVENT / EGET_BUTTON_CLICKED here by synthesising a
    // MouseButtonDown InputEvent at the button's centre in virtual coordinates.
    // UIManager's inRect-based hit-tests then handle the event normally.
    // -------------------------------------------------------------------------
    if (event.EventType == irr::EET_GUI_EVENT) {
        // Hover sprite swapping must receive the full SEvent (IrrlichtUIBackend API).
        if (m_uiBackend) {
            m_uiBackend->handleGuiHoverEvent(event);
        }
        return handleGuiEvent(event.GUIEvent, ev);
    }

    // -------------------------------------------------------------------------
    // Focus events: WindowFocusGained / WindowFocusLost
    // Dispatch contract (MANDATORY per architecture/ui-ux/input-arbitration.md):
    //   Step 1: UIManager::onEvent() — UIManager MUST see these for its internal
    //           dispatch chain guard (WindowFocusGained/Lost early-return) to fire.
    //   Step 2: unconditionally forward to CameraController regardless of UIManager result.
    //   Return value from UIManager::onEvent() is DISCARDED for focus events.
    //
    // NOTE: The Irrlicht vcpkg port (adrido/irrlicht-vcpkg, based on Irrlicht 1.8.x)
    // does NOT expose EET_APPLICATION_EVENT / EAET_FOCUS_GAINED / EAET_FOCUS_LOST.
    // Those event types are only available in the irrlicht-mt (Minetest) fork.
    // Window focus events for this build must be delivered by calling
    // dispatchFocusEvent() directly from the platform window loop (Phase 3+).
    // -------------------------------------------------------------------------

    if (event.EventType == irr::EET_MOUSE_INPUT_EVENT) {
        return handleMouseEvent(event.MouseInput, ev);
    }

    if (event.EventType == irr::EET_KEY_INPUT_EVENT) {
        return handleKeyboardEvent(event.KeyInput, ev);
    }

    return false;
}

void EventReceiver::dispatchFocusEvent(bool gained) {
    InputEvent ev{};
    ev.type = gained ? InputEvent::Type::WindowFocusGained : InputEvent::Type::WindowFocusLost;
    if (m_uiManager) m_uiManager->onEvent(ev);   // step 1 — result discarded
    if (m_camera)    m_camera->OnInputEvent(ev);  // step 2 — unconditional
}
