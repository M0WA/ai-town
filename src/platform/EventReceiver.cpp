// EventReceiver.cpp — Irrlicht SEvent → InputEvent translation and dispatch.
#include "EventReceiver.h"

#include "src/ui/UIScaler.h"          // UIScaler::unproject(), getViewportRect()
#include "src/ui/UIManager.h"         // UIManager::onEvent()
#include "src/ui/CameraController.h"  // CameraController::OnInputEvent()

EventReceiver::EventReceiver(UIScaler* scaler, UIManager* uiManager, CameraController* camera)
    : m_scaler(scaler)
    , m_uiManager(uiManager)
    , m_camera(camera)
{
}

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
    if (event.EventType == irr::EET_GUI_EVENT &&
        event.GUIEvent.EventType == irr::gui::EGET_BUTTON_CLICKED) {
        irr::gui::IGUIElement* btn = event.GUIEvent.Caller;
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

    // -------------------------------------------------------------------------
    // Mouse events
    // -------------------------------------------------------------------------
    if (event.EventType == irr::EET_MOUSE_INPUT_EVENT) {
        // Raw physical coordinates from SEvent.
        const int physX = event.MouseInput.X;
        const int physY = event.MouseInput.Y;

        // Virtual coordinates via UIScaler::unproject().
        int virtX = 0, virtY = 0;
        if (m_scaler) {
            UIScaler::VirtualPoint vp = m_scaler->unproject(physX, physY);
            virtX = vp.x;
            virtY = vp.y;
        }

        ev.physX = physX;
        ev.physY = physY;
        ev.x     = virtX;
        ev.y     = virtY;

        switch (event.MouseInput.Event) {
        case irr::EMIE_LMOUSE_PRESSED_DOWN:
            ev.type   = InputEvent::Type::MouseButtonDown;
            ev.button = 0;  // LMB
            // UIManager first; if consumed, do not forward to CameraController.
            if (m_uiManager && m_uiManager->onEvent(ev)) return true;
            if (m_camera) m_camera->OnInputEvent(ev);
            return false;

        case irr::EMIE_RMOUSE_PRESSED_DOWN:
            ev.type   = InputEvent::Type::MouseButtonDown;
            ev.button = 1;  // RMB
            // RMB atomicity: UIManager first; if NOT consumed, set m_rmbDragActive AND
            // forward to CameraController IN THE SAME BRANCH (atomicity requirement).
            // Splitting into separate conditions is a bug (CameraController never starts drag).
            if (m_uiManager && m_uiManager->onEvent(ev)) {
                // Scrim consumed RMB — do NOT start RMB drag.
                // NOTE: UIManager returning true here is CORRECT (scrim at Priority 1).
                return true;
            }
            // UIManager did not consume — start RMB drag and forward to CameraController.
            m_rmbDragActive = true;
            if (m_camera) m_camera->OnInputEvent(ev);
            return false;

        case irr::EMIE_MMOUSE_PRESSED_DOWN:
            ev.type   = InputEvent::Type::MouseButtonDown;
            ev.button = 2;  // MMB — camera pass-through, no UIManager call
            m_mmbDragActive = true;
            if (m_camera) m_camera->OnInputEvent(ev);
            return false;

        case irr::EMIE_LMOUSE_LEFT_UP:
            ev.type   = InputEvent::Type::MouseButtonUp;
            ev.button = 0;
            if (m_uiManager && m_uiManager->onEvent(ev)) return true;
            if (m_camera) m_camera->OnInputEvent(ev);
            return false;

        case irr::EMIE_RMOUSE_LEFT_UP:
            ev.type   = InputEvent::Type::MouseButtonUp;
            ev.button = 1;
            m_rmbDragActive = false;
            if (m_uiManager && m_uiManager->onEvent(ev)) return true;
            if (m_camera) m_camera->OnInputEvent(ev);
            return false;

        case irr::EMIE_MMOUSE_LEFT_UP:
            ev.type   = InputEvent::Type::MouseButtonUp;
            ev.button = 2;
            m_mmbDragActive = false;
            // MMB pass-through — no UIManager call.
            if (m_camera) m_camera->OnInputEvent(ev);
            return false;

        case irr::EMIE_MOUSE_MOVED:
            ev.type = InputEvent::Type::MouseMove;
            // Camera pass-through during MMB or RMB drag (Priority 1 — bypasses UIManager).
            if (m_mmbDragActive || m_rmbDragActive) {
                if (m_camera) m_camera->OnInputEvent(ev);
                return false;
            }
            // Normal mouse move: UIManager first, then CameraController if not consumed.
            if (m_uiManager && m_uiManager->onEvent(ev)) return true;
            if (m_camera) m_camera->OnInputEvent(ev);
            return false;

        case irr::EMIE_MOUSE_WHEEL:
            ev.type       = InputEvent::Type::MouseWheel;
            ev.wheelDelta = event.MouseInput.Wheel;
            // Scroll wheel: camera pass-through, UIManager NOT called.
            if (m_camera) m_camera->OnInputEvent(ev);
            return false;

        default:
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // Keyboard events
    // -------------------------------------------------------------------------
    if (event.EventType == irr::EET_KEY_INPUT_EVENT) {
        ev.keyCode = static_cast<int>(event.KeyInput.Key);
        if (event.KeyInput.PressedDown) {
            ev.type = InputEvent::Type::KeyDown;
        } else {
            ev.type = InputEvent::Type::KeyUp;
        }
        if (m_uiManager && m_uiManager->onEvent(ev)) return true;
        if (m_camera) m_camera->OnInputEvent(ev);
        return false;
    }

    return false;
}

void EventReceiver::dispatchFocusEvent(bool gained) {
    InputEvent ev{};
    ev.type = gained ? InputEvent::Type::WindowFocusGained : InputEvent::Type::WindowFocusLost;
    if (m_uiManager) m_uiManager->onEvent(ev);   // step 1 — result discarded
    if (m_camera)    m_camera->OnInputEvent(ev);  // step 2 — unconditional
}
