#pragma once
#include "src/platform/input_event.h"      // InputEvent definition
#include "src/interfaces/camera_state.h"   // CameraState struct — minimal header, no audio-domain types
#include "src/ui/key_bindings.h"           // KeyBindings — hotkey config accepted in constructor
// Do NOT include audio_types.h here: including it leaks SoundPriority, StingerType, SoundId,
// and other audio-domain types into every UI translation unit that includes CameraController.h.

// Forward-declare ICameraSceneNode to avoid pulling Irrlicht headers into every
// translation unit that includes CameraController.h. This keeps test files clean.
// CameraController.cpp includes <irrlicht.h> for the full type.
namespace irr { namespace scene { class ICameraSceneNode; } }

// CameraController — processes pan/zoom/rotate input and maintains camera state.
// Source location: src/ui/ (locked per testability-architecture.md).
// Moving to src/platform/ would break the src/ui/ coverage gate.
//
// Pan speed formula (mandatory):
//   panSpeed = kBasePanSpeed * (currentZoomDistance / kDefaultZoomDistance)
// Arrow-key pan uses the same formula * kKeyboardPanRate.
// sensitivityMultiplier applies ONLY to MMB drag and edge-scroll — NOT keyboard pan.
//
// Phase 3: constructor extended with optional KeyBindings parameter (default: KeyBindings{}).
// camera parameter MAY be nullptr in unit tests (null-camera test seam).
class CameraController {
public:
    // Pan speed constants — constexpr per architecture/ui-ux/camera-controls.md
    static constexpr float kBasePanSpeed       = 50.0f;   // world units/second at default zoom
    static constexpr float kDefaultZoomDistance = 200.0f;  // default camera-to-target distance
    static constexpr float kMinZoomDistance     = 30.0f;   // minimum zoom (closest)
    static constexpr float kMaxZoomDistance     = 800.0f;  // maximum zoom (farthest)
    static constexpr float kKeyboardPanRate     = 1.0f;    // Arrow-key pan scale relative to mouse pan
    static constexpr float kZoomSpeed           = 20.0f;   // zoom units per wheel click
    static constexpr float kRotateSpeed         = 0.3f;    // degrees per physical pixel
    static constexpr int   kEdgeScrollBand      = 20;      // px from virtual edge (1920x1080 space)

    // Constructor: camera may be nullptr (unit-test seam — getCameraState() uses m_pitch/m_yaw formula).
    // startInFullscreen: true → m_edgeScrollEnabled=true (fullscreen default: ON)
    //                    false → m_edgeScrollEnabled=false (windowed default: OFF)
    // bindings: hotkey configuration; defaults to KeyBindings{} (default hotkeys).
    //           Phase 3 adds KeyBindings parameter; existing tests pass default value.
    // Production instance constructed with startInFullscreen=false (1280x720 windowed window).
    CameraController(irr::scene::ICameraSceneNode* camera, bool startInFullscreen,
                     const KeyBindings& bindings = KeyBindings{});

    // Process an input event. Returns true if consumed (stops further propagation).
    // Takes const InputEvent& — uses physX/physY for drag-delta (UX-1 requirement).
    bool OnInputEvent(const InputEvent& event);

    // Per-frame update: applies accumulated state to the live camera node.
    // Must be called every frame BEFORE sceneManager->drawAll().
    // Null-safe when camera == nullptr.
    void update(float dt);

    // Returns current camera state (position, forward, up vectors).
    // When camera == nullptr: derives forward from m_pitch/m_yaw using Irrlicht left-handed Y-up formula.
    // When camera != nullptr: reads live camera node values (uses getUpVector() for up — NOT hardcoded).
    CameraState getCameraState() const;

    // Edge-scroll accessors. Required by test cases 6 and 8.
    bool isEdgeScrollEnabled() const { return m_edgeScrollEnabled; }

    // Setter required by Phase 8 Settings panel wiring.
    // Persists the player's explicit on/off choice (distinct from m_appHasFocus suppression).
    void setEdgeScrollEnabled(bool enabled) { m_edgeScrollEnabled = enabled; }

    // Sensitivity multiplier accessor/setter (Phase 8 sensitivity slider).
    // Applies ONLY to MMB drag and edge-scroll — NOT keyboard pan.
    float getSensitivityMultiplier() const { return m_sensitivityMultiplier; }
    void  setSensitivityMultiplier(float s) { m_sensitivityMultiplier = s; }

    // Set the camera look-at target in world space (XZ plane, Y=0).
    // Used to position the camera over the terrain center after generation.
    void setTarget(float worldX, float worldZ) { m_targetX = worldX; m_targetZ = worldZ; }

    // Reset orbit parameters (pitch, yaw, zoom) to their defaults.
    // Call after setTarget() when starting a new game so the player gets a
    // clean top-down overview regardless of the previous session's camera state.
    void resetOrbit();

private:
    irr::scene::ICameraSceneNode* m_camera;
    const KeyBindings m_bindings;  // set once in constructor initialiser list, never reassigned

    // Camera spherical coordinates
    float m_pitch{-45.0f};   // degrees, clamped to [-70, -20]
    float m_yaw{0.0f};       // degrees, unbounded (wraps naturally via trig)
    float m_zoomDistance{kDefaultZoomDistance};

    // Camera look-at target in world space (pan moves this)
    float m_targetX{0.0f};
    float m_targetZ{0.0f};

    // Input state
    bool  m_rmbDragActive{false};  // RMB (button=1) drag in progress
    bool  m_mmbDragActive{false};  // MMB (button=2) drag in progress
    int   m_prevPhysX{0};          // previous physical x for drag-delta
    int   m_prevPhysY{0};          // previous physical y for drag-delta

    // Arrow-key held state (continuous pan applied in update(dt))
    bool  m_panLeft{false};
    bool  m_panRight{false};
    bool  m_panForward{false};
    bool  m_panBackward{false};

    // Edge scroll & focus
    bool  m_edgeScrollEnabled;     // set from startInFullscreen in constructor
    bool  m_appHasFocus{true};     // false on WindowFocusLost — suppresses edge-scroll
                                   // but does NOT change m_edgeScrollEnabled

    // Sensitivity (Phase 8 slider multiplier — applies to MMB drag and edge-scroll only)
    float m_sensitivityMultiplier{1.0f};

    // Phase 11q3 — extracted helpers for OnInputEvent (S3776)
    void applyMouseDrag(float dx, float dy);
    void applyScrollZoom(float delta);

    // Phase 11q5 — extracted per-case helpers for OnInputEvent (S3776 CC=33 → ≤15)
    bool handleRMBDown(const InputEvent& e);
    bool handleMMBDown(const InputEvent& e);
    bool handleRMBUp(const InputEvent& e);
    bool handleMMBUp(const InputEvent& e);
    bool handleMouseMoved(const InputEvent& e);
};
