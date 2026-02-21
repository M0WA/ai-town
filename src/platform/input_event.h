#pragma once

// InputEvent — platform-abstracted input event struct.
// Replaces Irrlicht's SEvent in CameraController and UIManager interfaces
// so that test files in tests/ui/ can construct InputEvent directly without
// pulling in Irrlicht headers.
// The concrete IEventReceiver in src/platform/ translates SEvent to InputEvent
// before forwarding to CameraController.
struct InputEvent {
    enum class Type {
        MouseMove,
        MouseButtonDown,
        MouseButtonUp,
        MouseWheel,
        KeyDown,
        KeyUp,
        WindowFocusGained,   // required for UIManager pause-on-alt-tab
        WindowFocusLost      // required for UIManager input arbitration focus tracking
    };
    Type  type{Type::MouseMove};
    int   x{0};              // cursor position in virtual 1920x1080 space (for UI hit-testing)
    int   y{0};              // cursor position in virtual 1920x1080 space (for UI hit-testing)
    int   physX{0};          // cursor position in physical pixels (for drag-delta in CameraController)
    int   physY{0};          // cursor position in physical pixels (for drag-delta in CameraController)
                             // UX-1: drag-delta MUST use physX/physY, NOT x/y — see camera-controls.md.
                             // The platform IEventReceiver sets physX/physY from raw SEvent coordinates
                             // before UIScaler::unproject() is applied. x/y are the unprojected values.
    int   button{0};         // 0=left, 1=right, 2=middle (for mouse button events)
    float wheelDelta{0.f};   // for MouseWheel events
    int   keyCode{0};        // SDL2-style key code (for key events)
};
