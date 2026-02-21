// camera_controller_test.cpp — Phase 1 CameraController unit tests.
// All 9 named test cases registered under the ui_tests CMake target (label "unit").
// Tests use the null-camera seam: CameraController(nullptr, startInFullscreen) allows
// headless testing without a live Irrlicht device or scene node.
//
// Include path strategy: project-root-relative includes are used throughout.
// ${CMAKE_SOURCE_DIR} is present in ui_tests target_include_directories, so
// #include "src/platform/input_event.h" resolves correctly.
// Do NOT use unqualified #include "input_event.h" — src/platform/ is not a
// bare include path for ui_tests.
#include "src/ui/CameraController.h"
#include "src/platform/input_event.h"
#include "src/interfaces/camera_state.h"
#include "src/ui/UIScaler.h"
#include <gtest/gtest.h>
#include <cmath>

// ---------------------------------------------------------------------------
// Helper: SDL2-style key codes for arrow keys used in Test 9.
// The InputEvent spec documents keyCode as "SDL2-style key code".
// SDLK_RIGHT = 1073741903, SDLK_LEFT = 1073741904, per SDL2 reference.
// These constants avoid magic numbers in the test body.
// ---------------------------------------------------------------------------
static constexpr int kKeyArrowRight = 1073741903;
static constexpr int kKeyArrowLeft  = 1073741904;
static constexpr int kKeyArrowUp    = 1073741906;
static constexpr int kKeyArrowDown  = 1073741905;

// ---------------------------------------------------------------------------
// Helper: build InputEvent structs using aggregate initialization per spec.
// The InputEvent struct has all fields defaulted so partial initialization
// via member assignment is safe and explicit.
// ---------------------------------------------------------------------------
static InputEvent makeMouseButtonDown(int button, int physX = 100, int physY = 100)
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = button;
    ev.physX  = physX;
    ev.physY  = physY;
    return ev;
}

static InputEvent makeMouseMove(int physX, int physY, int virtX = 0, int virtY = 540)
{
    InputEvent ev{};
    ev.type  = InputEvent::Type::MouseMove;
    ev.physX = physX;
    ev.physY = physY;
    ev.x     = virtX;
    ev.y     = virtY;
    return ev;
}

static InputEvent makeKeyDown(int keyCode)
{
    InputEvent ev{};
    ev.type    = InputEvent::Type::KeyDown;
    ev.keyCode = keyCode;
    return ev;
}

static InputEvent makeWindowFocusLost()
{
    InputEvent ev{};
    ev.type = InputEvent::Type::WindowFocusLost;
    return ev;
}

static InputEvent makeWindowFocusGained()
{
    InputEvent ev{};
    ev.type = InputEvent::Type::WindowFocusGained;
    return ev;
}

// ---------------------------------------------------------------------------
// Helper: compare two vec3 positions for exact float equality.
// Used to assert camera position is UNCHANGED across a sequence of events.
// ---------------------------------------------------------------------------
static bool vec3Equal(const vec3& a, const vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// ===========================================================================
// Test 1: CameraController_PitchClamp_AtUpperBound_ExactlyMinus20
//
// Spec ref: architecture/testing/testability-architecture.md §CameraController
//           Required Named Test Cases item 1.
//           architecture/ui-ux/camera-controls.md §Pitch clamp boundary semantics.
//
// Inject MouseButtonDown(button=1) to begin RMB drag, then inject many
// MouseMove events with physY DECREASING (upward drag — pitch increases toward
// the -20 degree upper bound). After update(), assert getCameraState().pitch
// equals exactly -20.0f via EXPECT_FLOAT_EQ (inclusive bound, std::clamp
// semantics — NOT EXPECT_LT).
//
// NOTE: MouseWheel drives zoom distance (dolly) ONLY — it MUST NOT be used in
// pitch clamp tests, as scroll wheel events do not affect pitch and produce a
// test that never reaches the pitch clamp boundary.
// ===========================================================================
TEST(CameraControllerTest, CameraController_PitchClamp_AtUpperBound_ExactlyMinus20)
{
    CameraController cam(nullptr, false);

    // Start RMB drag at an arbitrary physical position.
    cam.OnInputEvent(makeMouseButtonDown(1, 200, 200));

    // Drive physY sharply upward (visual upward drag = pitch less negative =
    // towards -20 upper bound). 100 iterations x 100 px = 10000 px delta,
    // far exceeding any sensitivity value. std::clamp will stop at -20.
    // physY DECREASING = upward drag = pitch increases toward 0 (less negative).
    int physY = 200;
    for (int i = 0; i < 100; ++i) {
        physY -= 100;
        cam.OnInputEvent(makeMouseMove(200, physY));
    }

    cam.update(0.016f);

    // Pitch must be clamped to exactly -20.0f (upper bound, inclusive).
    // Use EXPECT_FLOAT_EQ (not EXPECT_LT) — the bound is inclusive per spec.
    EXPECT_FLOAT_EQ(cam.getCameraState().pitch, -20.0f);
}

// ===========================================================================
// Test 2: CameraController_PitchClamp_AtLowerBound_ExactlyMinus70
//
// Spec ref: architecture/testing/testability-architecture.md §CameraController
//           Required Named Test Cases item 2.
//
// Inject MouseButtonDown(button=1) + MouseMove events with physY INCREASING
// (downward visual drag) to saturate the -70 degree lower bound.
// Assert getCameraState().pitch == -70.0f via EXPECT_FLOAT_EQ (not EXPECT_GT).
//
// NOTE: MouseWheel drives zoom distance ONLY — not pitch. Use RMB vertical drag.
// ===========================================================================
TEST(CameraControllerTest, CameraController_PitchClamp_AtLowerBound_ExactlyMinus70)
{
    CameraController cam(nullptr, false);

    // Start RMB drag.
    cam.OnInputEvent(makeMouseButtonDown(1, 200, 200));

    // Drive physY sharply downward (visual downward drag = pitch more negative =
    // towards -70 lower bound). physY INCREASING = downward drag.
    int physY = 200;
    for (int i = 0; i < 100; ++i) {
        physY += 100;
        cam.OnInputEvent(makeMouseMove(200, physY));
    }

    cam.update(0.016f);

    // Pitch must be clamped to exactly -70.0f (lower bound, inclusive).
    // Use EXPECT_FLOAT_EQ (not EXPECT_GT) — the bound is inclusive per spec.
    EXPECT_FLOAT_EQ(cam.getCameraState().pitch, -70.0f);
}

// ===========================================================================
// Test 3: CameraController_EdgeScroll_DisabledOnFocusLoss
//
// Spec ref: architecture/testing/testability-architecture.md §CameraController
//           Required Named Test Cases item 3.
//           architecture/ui-ux/camera-controls.md §Edge scrolling.
//
// Confirms that m_appHasFocus = false (set by WindowFocusLost) suppresses
// edge-scroll even when m_edgeScrollEnabled = true.
//
// COORDINATE SPACE NOTE (MANDATORY per phase-1.md §CameraController tests):
//   Edge-scroll activation reads InputEvent.x (virtual 1920x1080 space),
//   NOT InputEvent.physX (physical pixels). The left-edge activation band is
//   x < 20 in virtual space (architecture/ui-ux/camera-controls.md).
//   To obtain a correct virtual-space x=0, construct a UIScaler and call
//   UIScaler::unproject() on a physical coordinate at the left edge.
//   Do NOT hardcode x=0 without going through UIScaler::unproject() — doing so
//   bypasses the coordinate transformation entirely and may produce a vacuously
//   correct-looking test. Setting only physX=0 without x=0 does NOT trigger
//   edge-scroll (physX is for drag-delta only, per UX-1).
// ===========================================================================
TEST(CameraControllerTest, CameraController_EdgeScroll_DisabledOnFocusLoss)
{
    // Construct in fullscreen so that edge scroll is enabled by default.
    CameraController cam(nullptr, /*startInFullscreen=*/true);

    // Record camera position before any events.
    const vec3 initialPos = cam.getCameraState().position;

    // Lose OS focus — sets m_appHasFocus = false inside CameraController.
    // Does NOT change m_edgeScrollEnabled.
    cam.OnInputEvent(makeWindowFocusLost());

    // Build a MouseMove event at the left virtual edge (x=0) that would
    // normally trigger left-edge scroll when m_appHasFocus=true.
    //
    // UIScaler::unproject() path (required — do NOT hardcode x=0 without this):
    // For a 1920x1080 virtual / 1920x1080 viewport with no offset, physical (0, 540)
    // unprojects to virtual (0, 540). vp.x will be 0 (clamped to [0, 1919]).
    UIScaler scaler(1920, 1080, 1920, 1080, 0, 0);
    UIScaler::VirtualPoint vp = scaler.unproject(0, 540);

    InputEvent edgeEv{};
    edgeEv.type  = InputEvent::Type::MouseMove;
    edgeEv.physX = 0;
    edgeEv.physY = 540;
    edgeEv.x     = vp.x;   // virtual-space coordinate — what CameraController reads
    edgeEv.y     = vp.y;

    cam.OnInputEvent(edgeEv);
    cam.update(0.1f);

    // Position must be UNCHANGED — m_appHasFocus=false suppresses edge-scroll.
    const vec3 posAfter = cam.getCameraState().position;
    EXPECT_TRUE(vec3Equal(initialPos, posAfter))
        << "Edge-scroll must be suppressed when m_appHasFocus=false (WindowFocusLost)";
}

// ===========================================================================
// Test 4: CameraController_RightMouseRotate_MovesYaw
//
// Spec ref: architecture/testing/testability-architecture.md §CameraController
//           Required Named Test Cases item 4.
//
// Inject RMB drag (button=1) with a horizontal physX delta of 10 physical
// pixels. Assert getCameraState().yaw changed from the initial value.
//
// CameraController reads physX/physY for drag-delta per UX-1
// (architecture/ui-ux/camera-controls.md §Drag-delta coordinate space).
// physX/physY are physical pixel coordinates — NOT virtual-space x/y.
// ===========================================================================
TEST(CameraControllerTest, CameraController_RightMouseRotate_MovesYaw)
{
    CameraController cam(nullptr, false);

    // Record initial yaw before any input.
    const float initialYaw = cam.getCameraState().yaw;

    // Start RMB drag at physX=100, physY=100.
    // physX/physY are physical pixel coordinates for drag-delta per UX-1.
    cam.OnInputEvent(makeMouseButtonDown(1, 100, 100));

    // Move 10 physical pixels to the right (horizontal drag — no physY delta).
    // CameraController reads physX/physY per UX-1; test comment must state this.
    cam.OnInputEvent(makeMouseMove(110, 100));

    cam.update(0.016f);

    // Yaw must have changed. Exact delta is implementation-defined; this test
    // verifies directional sensitivity (RMB horizontal drag drives yaw rotation).
    EXPECT_NE(cam.getCameraState().yaw, initialYaw)
        << "RMB horizontal drag of 10 physical pixels must change yaw";
}

// ===========================================================================
// Test 5: CameraController_MiddleMousePan_MovesPosition
//
// Spec ref: architecture/testing/testability-architecture.md §CameraController
//           Required Named Test Cases item 5.
//
// Inject MMB drag (button=2) with a horizontal physX delta of 5 physical
// pixels. Assert camera position changed (at least one component differs).
//
// CameraController reads physX/physY for drag-delta per UX-1
// (architecture/ui-ux/camera-controls.md §Drag-delta coordinate space).
// physX/physY are physical pixel coordinates — NOT virtual-space x/y.
// ===========================================================================
TEST(CameraControllerTest, CameraController_MiddleMousePan_MovesPosition)
{
    CameraController cam(nullptr, false);

    // Record initial position before any input.
    const vec3 initialPos = cam.getCameraState().position;

    // Start MMB drag at physX=100, physY=100.
    // physX/physY are physical pixel coordinates for drag-delta per UX-1.
    cam.OnInputEvent(makeMouseButtonDown(2, 100, 100));

    // Move 5 physical pixels to the right (horizontal MMB drag).
    // CameraController reads physX/physY per UX-1; test comment must state this.
    cam.OnInputEvent(makeMouseMove(105, 100));

    cam.update(0.016f);

    // At least one position component must have changed.
    const vec3 newPos = cam.getCameraState().position;
    EXPECT_FALSE(vec3Equal(initialPos, newPos))
        << "MMB horizontal drag of 5 physical pixels must move camera position";
}

// ===========================================================================
// Test 6: CameraController_EdgeScroll_EnabledByDefaultInFullscreen
//
// Spec ref: architecture/testing/testability-architecture.md §CameraController
//           Required Named Test Cases item 6.
//           architecture/ui-ux/camera-controls.md §Edge scrolling.
//
// Construct with startInFullscreen=true. Immediately call isEdgeScrollEnabled()
// without any setEdgeScrollEnabled() call. Assert true.
// ===========================================================================
TEST(CameraControllerTest, CameraController_EdgeScroll_EnabledByDefaultInFullscreen)
{
    CameraController cam(nullptr, /*startInFullscreen=*/true);

    // No setEdgeScrollEnabled() call — testing constructor initial state only.
    EXPECT_TRUE(cam.isEdgeScrollEnabled())
        << "Edge scroll must be enabled by default when startInFullscreen=true";
}

// ===========================================================================
// Test 7: CameraController_SetEdgeScroll_Enabled_Then_FocusLost_DoesNotClearEnabled
//
// Spec ref: architecture/testing/testability-architecture.md §CameraController
//           Required Named Test Cases item 7 (MANDATORY final assertion).
//           architecture/ui-ux/camera-controls.md §Edge scrolling.
//
// Verifies:
//   (a) WindowFocusLost sets m_appHasFocus=false but does NOT mutate
//       m_edgeScrollEnabled.
//   (b) WindowFocusGained restores m_appHasFocus=true, re-enabling edge-scroll
//       for a controller with m_edgeScrollEnabled=true.
//
// The MANDATORY final WindowFocusGained + position-change assertion is required
// per testability-architecture.md. Without it, a broken implementation that
// permanently disables edge scroll after WindowFocusLost would still pass all
// prior assertions.
// ===========================================================================
TEST(CameraControllerTest,
     CameraController_SetEdgeScroll_Enabled_Then_FocusLost_DoesNotClearEnabled)
{
    // Construct in windowed mode (edge scroll OFF by default).
    CameraController cam(nullptr, /*startInFullscreen=*/false);

    // Explicitly enable edge scroll.
    cam.setEdgeScrollEnabled(true);
    EXPECT_EQ(cam.isEdgeScrollEnabled(), true)
        << "setEdgeScrollEnabled(true) must set m_edgeScrollEnabled=true";

    // Lose OS focus — sets m_appHasFocus=false; MUST NOT change m_edgeScrollEnabled.
    cam.OnInputEvent(makeWindowFocusLost());

    // m_edgeScrollEnabled must still be true after focus loss.
    EXPECT_EQ(cam.isEdgeScrollEnabled(), true)
        << "WindowFocusLost must NOT mutate m_edgeScrollEnabled "
           "(only m_appHasFocus=false is set)";

    // Build a left-edge MouseMove event. Use UIScaler::unproject() to derive the
    // virtual-space x coordinate — required, not a hardcoded constant.
    // (See coordinate space note in Test 3 above.)
    auto makeLeftEdgeEvent = []() -> InputEvent {
        UIScaler scaler(1920, 1080, 1920, 1080, 0, 0);
        UIScaler::VirtualPoint vp = scaler.unproject(0, 540);
        InputEvent ev{};
        ev.type  = InputEvent::Type::MouseMove;
        ev.physX = 0;
        ev.physY = 540;
        ev.x     = vp.x;   // virtual x=0 — within 20px left-edge band
        ev.y     = vp.y;
        return ev;
    };

    // Inject edge event while focus is lost — position must NOT change.
    cam.OnInputEvent(makeLeftEdgeEvent());
    cam.update(0.1f);
    const vec3 posBeforeRefocus = cam.getCameraState().position;

    // MANDATORY FINAL STEP per testability-architecture.md:
    // Restore focus and verify that edge-scroll activates again.
    cam.OnInputEvent(makeWindowFocusGained());

    // isEdgeScrollEnabled() must remain true after focus is restored.
    EXPECT_EQ(cam.isEdgeScrollEnabled(), true)
        << "isEdgeScrollEnabled() must remain true after WindowFocusGained";

    // Inject the same left-edge event now that focus is restored.
    // m_edgeScrollEnabled=true AND m_appHasFocus=true -> edge-scroll active.
    cam.OnInputEvent(makeLeftEdgeEvent());
    cam.update(0.1f);

    const vec3 posAfterRefocus = cam.getCameraState().position;

    // Camera position MUST have changed — edge-scroll is active post-refocus.
    // This assertion distinguishes a correct implementation from one that
    // permanently disables edge-scroll after WindowFocusLost.
    EXPECT_NE(posBeforeRefocus.x, posAfterRefocus.x)
        << "Edge-scroll must be active after WindowFocusGained "
           "(m_edgeScrollEnabled=true AND m_appHasFocus=true)";
}

// ===========================================================================
// Test 8: CameraController_EdgeScroll_DisabledByDefaultInWindowed
//
// Spec ref: architecture/testing/testability-architecture.md §CameraController
//           Required Named Test Cases item 8.
//           architecture/ui-ux/camera-controls.md §Edge scrolling.
//
// Symmetric counterpart to Test 6. Construct with startInFullscreen=false.
// Immediately call isEdgeScrollEnabled() without any setEdgeScrollEnabled() call.
// Assert false — windowed mode must have edge scroll OFF by default.
// ===========================================================================
TEST(CameraControllerTest, CameraController_EdgeScroll_DisabledByDefaultInWindowed)
{
    CameraController cam(nullptr, /*startInFullscreen=*/false);

    // No setEdgeScrollEnabled() call — testing constructor initial state only.
    EXPECT_FALSE(cam.isEdgeScrollEnabled())
        << "Edge scroll must be disabled by default when startInFullscreen=false";
}

// ===========================================================================
// Test 9: CameraController_KeyboardPanIgnoresSensitivity  (ISSUE-E regression)
//
// Spec ref: phase-1.md §CameraController tests item 9.
//           architecture/ui-ux/camera-controls.md §Pan Speed Specification.
//
// Keyboard pan MUST NOT apply sensitivityMultiplier — only MMB drag and
// edge-scroll apply it per architecture/ui-ux/camera-controls.md.
// Phase 8 sensitivity slider MUST NOT regress this rule.
//
// Test: two identical Arrow key + update() sequences must produce equal position
// deltas, regardless of sensitivityMultiplier setting.
//
// If CameraController exposes setSensitivityMultiplier(float), the test uses it
// to drive sensitivity=1.0 vs sensitivity=2.0 and asserts equal keyboard deltas.
// If the method is not yet present at Phase 1, the test verifies two identical
// key inputs produce identical deltas (same assertion, same Phase 8 gate).
// The mandatory comment below is the Phase 8 enforcement point.
// ===========================================================================
TEST(CameraControllerTest, CameraController_KeyboardPanIgnoresSensitivity)
{
    CameraController cam(nullptr, /*startInFullscreen=*/false);

    // Disable edge-scroll to isolate keyboard pan from any edge-scroll influence.
    cam.setEdgeScrollEnabled(false);

    // --- Sequence A: default sensitivity (sensitivityMultiplier = 1.0f) ---
    const vec3 posA_before = cam.getCameraState().position;

    cam.OnInputEvent(makeKeyDown(kKeyArrowRight));
    cam.update(0.016f);

    const vec3  posA_after = cam.getCameraState().position;
    const float deltaA_x   = posA_after.x - posA_before.x;
    const float deltaA_z   = posA_after.z - posA_before.z;

    // --- Sequence B: elevated sensitivity (sensitivityMultiplier = 2.0f) ---
    // If CameraController exposes setSensitivityMultiplier(), call it here.
    // The method is defined when AITOWN_CAMERA_HAS_SENSITIVITY_MULTIPLIER is set
    // (wired at Phase 8). Phase 1 tests compile correctly without it.
#ifdef AITOWN_CAMERA_HAS_SENSITIVITY_MULTIPLIER
    cam.setSensitivityMultiplier(2.0f);
#endif

    const vec3 posB_before = cam.getCameraState().position;

    cam.OnInputEvent(makeKeyDown(kKeyArrowRight));
    cam.update(0.016f);

    const vec3  posB_after = cam.getCameraState().position;
    const float deltaB_x   = posB_after.x - posB_before.x;
    const float deltaB_z   = posB_after.z - posB_before.z;

    // Keyboard pan MUST NOT apply sensitivityMultiplier — only MMB drag and
    // edge-scroll apply it per architecture/ui-ux/camera-controls.md.
    // Phase 8 sensitivity slider MUST NOT regress this rule.
    EXPECT_FLOAT_EQ(deltaA_x, deltaB_x)
        << "Keyboard pan X delta must be identical regardless of sensitivityMultiplier";
    EXPECT_FLOAT_EQ(deltaA_z, deltaB_z)
        << "Keyboard pan Z delta must be identical regardless of sensitivityMultiplier";
}
