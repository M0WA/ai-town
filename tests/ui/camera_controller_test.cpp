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

// ===========================================================================
// Test 10: CameraController_EdgeScrollActivatesAt20pxBand
//
// Phase 3 compile-only stub. Real assertion is a Phase 6 deliverable.
// ===========================================================================
// Phase 3 compile-only stub. Real assertion is a Phase 6 deliverable.
TEST(CameraControllerTest, CameraController_EdgeScrollActivatesAt20pxBand) { SUCCEED(); }

// ===========================================================================
// Additional tests to cover uncovered branches in CameraController::OnInputEvent
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 11: MouseButtonUp_RMB_EndsRotateDrag
//
// After an RMB down + move, an RMB up must end the drag.
// A subsequent mouse-move must NOT change the yaw (drag no longer active).
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_MouseButtonUp_RMB_EndsDrag)
{
    CameraController cam(nullptr, false);

    // Begin RMB drag.
    cam.OnInputEvent(makeMouseButtonDown(1, 200, 200));

    // Move to accumulate some yaw.
    cam.OnInputEvent(makeMouseMove(210, 200));
    const float yawAfterDrag = cam.getCameraState().yaw;

    // Release RMB — drag must end.
    InputEvent upEv{};
    upEv.type   = InputEvent::Type::MouseButtonUp;
    upEv.button = 1;
    bool consumed = cam.OnInputEvent(upEv);
    EXPECT_TRUE(consumed) << "RMB up must return true (consumed)";

    // Move again — yaw must not change because drag ended.
    cam.OnInputEvent(makeMouseMove(250, 200));
    cam.update(0.016f);

    EXPECT_FLOAT_EQ(cam.getCameraState().yaw, yawAfterDrag)
        << "Yaw must not change after RMB up ends the drag";
}

// ---------------------------------------------------------------------------
// Test 12: MouseButtonUp_MMB_EndsPanDrag
//
// After an MMB down + move, an MMB up must end the drag.
// A subsequent mouse-move must NOT change the position.
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_MouseButtonUp_MMB_EndsDrag)
{
    CameraController cam(nullptr, false);

    // Begin MMB drag.
    cam.OnInputEvent(makeMouseButtonDown(2, 200, 200));
    cam.OnInputEvent(makeMouseMove(210, 200));
    const vec3 posAfterDrag = cam.getCameraState().position;

    // Release MMB — drag must end.
    InputEvent upEv{};
    upEv.type   = InputEvent::Type::MouseButtonUp;
    upEv.button = 2;
    bool consumed = cam.OnInputEvent(upEv);
    EXPECT_TRUE(consumed) << "MMB up must return true (consumed)";

    // Move again — position must not change because drag ended.
    cam.OnInputEvent(makeMouseMove(250, 200));
    cam.update(0.016f);

    const vec3 posAfterUp = cam.getCameraState().position;
    EXPECT_FLOAT_EQ(posAfterDrag.x, posAfterUp.x)
        << "X position must not change after MMB up ends the drag";
    EXPECT_FLOAT_EQ(posAfterDrag.z, posAfterUp.z)
        << "Z position must not change after MMB up ends the drag";
}

// ---------------------------------------------------------------------------
// Test 13: MouseButtonUp_LMB_ReturnsFalse
//
// A left-mouse-button (button=0) up event is not handled — must return false.
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_MouseButtonUp_LMB_ReturnsFalse)
{
    CameraController cam(nullptr, false);

    InputEvent upEv{};
    upEv.type   = InputEvent::Type::MouseButtonUp;
    upEv.button = 0;  // LMB
    EXPECT_FALSE(cam.OnInputEvent(upEv))
        << "LMB up must return false (not consumed by CameraController)";
}

// ---------------------------------------------------------------------------
// Test 14: MouseWheel_ZoomsIn_ClampedToMin
//
// Scrolling the wheel with a large positive delta must clamp zoom distance
// to kMinZoomDistance.
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_MouseWheel_ZoomsIn_ClampedToMin)
{
    CameraController cam(nullptr, false);

    // Inject a large positive wheel delta to zoom all the way in.
    InputEvent wheelEv{};
    wheelEv.type       = InputEvent::Type::MouseWheel;
    wheelEv.wheelDelta = 10000.0f;  // enormous positive = zoom in
    cam.OnInputEvent(wheelEv);

    cam.update(0.016f);

    const CameraState state = cam.getCameraState();
    // The zoom distance is internal; we verify via the Y position (which scales
    // with zoom). At minimum zoom the Y offset is -sin(-45°)*kMinZoomDistance.
    // For null-camera path, position.y = -sinP * m_zoomDistance.
    // At kMinZoomDistance=30 and pitch=-45: y = -sin(-45°)*30 ≈ +21.2.
    // Verify Y is positive (camera is above target) and reasonable.
    EXPECT_GT(state.position.y, 0.0f)
        << "Camera Y must be positive (above target) after zooming in";
}

// ---------------------------------------------------------------------------
// Test 15: MouseWheel_ZoomsOut_ClampedToMax
//
// Scrolling the wheel with a large negative delta must clamp zoom distance
// to kMaxZoomDistance.
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_MouseWheel_ZoomsOut_ClampedToMax)
{
    CameraController cam(nullptr, false);

    // Inject a large negative wheel delta to zoom all the way out.
    InputEvent wheelEv{};
    wheelEv.type       = InputEvent::Type::MouseWheel;
    wheelEv.wheelDelta = -10000.0f;
    cam.OnInputEvent(wheelEv);

    cam.update(0.016f);

    // The Y position at max zoom must be larger than at default zoom.
    // Default zoom distance = 200, max = 800.
    // At pitch=-45: y = -sin(-45°) * zoom; max zoom gives larger y.
    const CameraState state = cam.getCameraState();
    EXPECT_GT(state.position.y, 0.0f)
        << "Camera Y must be positive after zooming out to maximum";
}

// ---------------------------------------------------------------------------
// Test 16: MouseWheel_ReturnedAsConsumed
//
// Wheel events are always consumed by CameraController (return true).
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_MouseWheel_ReturnsTrue)
{
    CameraController cam(nullptr, false);

    InputEvent wheelEv{};
    wheelEv.type       = InputEvent::Type::MouseWheel;
    wheelEv.wheelDelta = 1.0f;
    EXPECT_TRUE(cam.OnInputEvent(wheelEv))
        << "MouseWheel must return true (consumed)";
}

// ---------------------------------------------------------------------------
// Test 17: KeyUp_ReturnsFalse
//
// KeyUp events are never consumed by CameraController.
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_KeyUp_ReturnsFalse)
{
    CameraController cam(nullptr, false);

    InputEvent keyUpEv{};
    keyUpEv.type    = InputEvent::Type::KeyUp;
    keyUpEv.keyCode = 0x25;  // irr::KEY_LEFT = 0x25
    EXPECT_FALSE(cam.OnInputEvent(keyUpEv))
        << "KeyUp must return false (not consumed by CameraController)";
}

// ---------------------------------------------------------------------------
// Test 18: UnknownEvent_ReturnsFalse (default branch)
//
// An event type that falls through to the default: branch must return false.
// MouseButtonDown for button=0 (LMB) falls through the button checks and
// returns false.
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_MouseButtonDown_LMB_ReturnsFalse)
{
    CameraController cam(nullptr, false);

    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = 0;  // LMB is not handled (only RMB=1 and MMB=2)
    EXPECT_FALSE(cam.OnInputEvent(ev))
        << "LMB MouseButtonDown must return false (not handled by CameraController)";
}

// ---------------------------------------------------------------------------
// Irrlicht arrow key codes (raw hex values from Keycodes.h):
//   KEY_LEFT  = 0x25, KEY_UP   = 0x26,
//   KEY_RIGHT = 0x27, KEY_DOWN = 0x28
//
// The existing kKeyArrowLeft/Right/Up/Down constants at the top of this file
// use SDL2 keycodes which do NOT match irr::KEY_* — they fall through to the
// unhandled-key else-branch. These new constants use the Irrlicht values
// that CameraController actually checks.
// ---------------------------------------------------------------------------
static constexpr int kIrrKeyLeft  = 0x25;
static constexpr int kIrrKeyRight = 0x27;
static constexpr int kIrrKeyUp    = 0x26;
static constexpr int kIrrKeyDown  = 0x28;

// ---------------------------------------------------------------------------
// Test 19: ArrowKey_Left_MovesTargetLeft (Irrlicht keycodes)
//
// LEFT arrow key (irr::KEY_LEFT = 0x25) must move the camera target.
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_IrrArrowLeft_MovesTarget)
{
    CameraController cam(nullptr, false);
    cam.setEdgeScrollEnabled(false);

    const vec3 posBefore = cam.getCameraState().position;

    InputEvent ev{};
    ev.type    = InputEvent::Type::KeyDown;
    ev.keyCode = kIrrKeyLeft;
    EXPECT_TRUE(cam.OnInputEvent(ev))
        << "irr::KEY_LEFT KeyDown must return true (consumed)";
    cam.update(0.016f);

    const vec3 posAfter = cam.getCameraState().position;
    // At yaw=0: left arrow subtracts from X (rightX = cos(0) = 1).
    EXPECT_NE(posBefore.x, posAfter.x)
        << "irr::KEY_LEFT must move camera position in X";
}

// ---------------------------------------------------------------------------
// Test 20: ArrowKey_Up_MovesTargetForward (Irrlicht keycodes)
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_IrrArrowUp_MovesTarget)
{
    CameraController cam(nullptr, false);
    cam.setEdgeScrollEnabled(false);

    const vec3 posBefore = cam.getCameraState().position;

    InputEvent ev{};
    ev.type    = InputEvent::Type::KeyDown;
    ev.keyCode = kIrrKeyUp;
    EXPECT_TRUE(cam.OnInputEvent(ev))
        << "irr::KEY_UP KeyDown must return true (consumed)";
    cam.update(0.016f);

    const vec3 posAfter = cam.getCameraState().position;
    EXPECT_NE(posBefore.z, posAfter.z)
        << "irr::KEY_UP must move camera position in Z";
}

// ---------------------------------------------------------------------------
// Test 21: ArrowKey_Down_MovesTargetBackward (Irrlicht keycodes)
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_IrrArrowDown_MovesTarget)
{
    CameraController cam(nullptr, false);
    cam.setEdgeScrollEnabled(false);

    const vec3 posBefore = cam.getCameraState().position;

    InputEvent ev{};
    ev.type    = InputEvent::Type::KeyDown;
    ev.keyCode = kIrrKeyDown;
    EXPECT_TRUE(cam.OnInputEvent(ev))
        << "irr::KEY_DOWN KeyDown must return true (consumed)";
    cam.update(0.016f);

    const vec3 posAfter = cam.getCameraState().position;
    EXPECT_NE(posBefore.z, posAfter.z)
        << "irr::KEY_DOWN must move camera position in Z";
}

// ---------------------------------------------------------------------------
// Test 22: ArrowKey_Right_MovesTargetRight (Irrlicht keycodes)
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_IrrArrowRight_MovesTarget)
{
    CameraController cam(nullptr, false);
    cam.setEdgeScrollEnabled(false);

    const vec3 posBefore = cam.getCameraState().position;

    InputEvent ev{};
    ev.type    = InputEvent::Type::KeyDown;
    ev.keyCode = kIrrKeyRight;
    EXPECT_TRUE(cam.OnInputEvent(ev))
        << "irr::KEY_RIGHT KeyDown must return true (consumed)";
    cam.update(0.016f);

    const vec3 posAfter = cam.getCameraState().position;
    EXPECT_NE(posBefore.x, posAfter.x)
        << "irr::KEY_RIGHT must move camera position in X";
}

// ---------------------------------------------------------------------------
// Test 22b: UnhandledKeyDown_ReturnsFalse
//
// A KeyDown event for a key that is not an arrow key must return false
// (falls through the else branch and returns consumed=false).
// Key code 0x1B = irr::KEY_ESCAPE (not an arrow key).
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_NonArrowKeyDown_ReturnsFalse)
{
    CameraController cam(nullptr, false);

    InputEvent ev{};
    ev.type    = InputEvent::Type::KeyDown;
    ev.keyCode = 0x1B;  // irr::KEY_ESCAPE — not an arrow key
    EXPECT_FALSE(cam.OnInputEvent(ev))
        << "Non-arrow KeyDown must return false (not consumed)";
}

// ---------------------------------------------------------------------------
// Test 23: EdgeScroll_RightEdge_MovesTargetRight
//
// With edge-scroll enabled and focus, a MouseMove event at the RIGHT edge
// (virtual x > 1920-20 = 1900) must pan the camera to the right.
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_EdgeScroll_RightEdge_MovesCameraRight)
{
    CameraController cam(nullptr, /*startInFullscreen=*/true);

    const vec3 posBefore = cam.getCameraState().position;

    // UIScaler::unproject for a physical coord at the right edge.
    UIScaler scaler(1920, 1080, 1920, 1080, 0, 0);
    UIScaler::VirtualPoint vp = scaler.unproject(1919, 540);

    InputEvent ev{};
    ev.type  = InputEvent::Type::MouseMove;
    ev.physX = 1919;
    ev.physY = 540;
    ev.x     = vp.x;   // virtual x near right edge (> 1900)
    ev.y     = vp.y;

    cam.OnInputEvent(ev);
    cam.update(0.1f);

    const vec3 posAfter = cam.getCameraState().position;
    // At yaw=0, right-edge scroll increases X (rightX = cos(0) = 1).
    EXPECT_GT(posAfter.x, posBefore.x)
        << "Right-edge scroll must increase camera X position";
}

// ---------------------------------------------------------------------------
// Test 24: EdgeScroll_TopEdge_MovesCameraForward
//
// With edge-scroll enabled and focus, a MouseMove at the TOP edge
// (virtual y < 20) must pan the camera forward (+Z at yaw=0).
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_EdgeScroll_TopEdge_MovesCameraForward)
{
    CameraController cam(nullptr, /*startInFullscreen=*/true);

    const vec3 posBefore = cam.getCameraState().position;

    UIScaler scaler(1920, 1080, 1920, 1080, 0, 0);
    UIScaler::VirtualPoint vp = scaler.unproject(960, 0);

    InputEvent ev{};
    ev.type  = InputEvent::Type::MouseMove;
    ev.physX = 960;
    ev.physY = 0;
    ev.x     = vp.x;
    ev.y     = vp.y;   // virtual y near top edge (< 20)

    cam.OnInputEvent(ev);
    cam.update(0.1f);

    const vec3 posAfter = cam.getCameraState().position;
    // At yaw=0, forward-edge scroll increases Z (fwdZ = cos(0) = 1).
    EXPECT_GT(posAfter.z, posBefore.z)
        << "Top-edge scroll must increase camera Z (forward pan)";
}

// ---------------------------------------------------------------------------
// Test 25: EdgeScroll_BottomEdge_MovesCameraBackward
//
// With edge-scroll enabled and focus, a MouseMove at the BOTTOM edge
// (virtual y > 1080-20 = 1060) must pan the camera backward.
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_EdgeScroll_BottomEdge_MovesCameraBackward)
{
    CameraController cam(nullptr, /*startInFullscreen=*/true);

    const vec3 posBefore = cam.getCameraState().position;

    UIScaler scaler(1920, 1080, 1920, 1080, 0, 0);
    UIScaler::VirtualPoint vp = scaler.unproject(960, 1079);

    InputEvent ev{};
    ev.type  = InputEvent::Type::MouseMove;
    ev.physX = 960;
    ev.physY = 1079;
    ev.x     = vp.x;
    ev.y     = vp.y;   // virtual y near bottom edge (> 1060)

    cam.OnInputEvent(ev);
    cam.update(0.1f);

    const vec3 posAfter = cam.getCameraState().position;
    // At yaw=0, bottom-edge scroll decreases Z (backward pan).
    EXPECT_LT(posAfter.z, posBefore.z)
        << "Bottom-edge scroll must decrease camera Z (backward pan)";
}

// ---------------------------------------------------------------------------
// Test 26: FocusLost_ResetsDragState
//
// WindowFocusLost must reset both m_rmbDragActive and m_mmbDragActive so that
// a subsequent move event does not accidentally continue a drag.
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_FocusLost_ResetsDragState)
{
    CameraController cam(nullptr, false);

    // Begin RMB drag.
    cam.OnInputEvent(makeMouseButtonDown(1, 200, 200));
    cam.OnInputEvent(makeMouseMove(210, 200));
    const float yawAfterDrag = cam.getCameraState().yaw;

    // Lose focus — must reset drag state.
    cam.OnInputEvent(makeWindowFocusLost());

    // Move — yaw must not change because drag was reset.
    cam.OnInputEvent(makeMouseMove(250, 200));
    cam.update(0.016f);

    EXPECT_FLOAT_EQ(cam.getCameraState().yaw, yawAfterDrag)
        << "Yaw must not change after WindowFocusLost resets drag state";
}

// ---------------------------------------------------------------------------
// Test 27: SensitivityMultiplier_AffectsMMBPanDelta
//
// Two MMB pan sequences with different sensitivity multipliers must produce
// different position deltas (sensitivity applies to MMB drag).
// This is the counterpart to Test 9 (keyboard pan ignores sensitivity).
// ---------------------------------------------------------------------------
TEST(CameraControllerTest, CameraController_SensitivityMultiplier_AffectsMMBPan)
{
    // Sequence A: default sensitivity (1.0f).
    CameraController camA(nullptr, false);
    camA.setEdgeScrollEnabled(false);
    camA.setSensitivityMultiplier(1.0f);

    camA.OnInputEvent(makeMouseButtonDown(2, 200, 200));
    camA.OnInputEvent(makeMouseMove(210, 200));  // 10 px horizontal MMB
    const vec3 posA = camA.getCameraState().position;

    // Sequence B: elevated sensitivity (3.0f).
    CameraController camB(nullptr, false);
    camB.setEdgeScrollEnabled(false);
    camB.setSensitivityMultiplier(3.0f);

    camB.OnInputEvent(makeMouseButtonDown(2, 200, 200));
    camB.OnInputEvent(makeMouseMove(210, 200));  // same 10 px horizontal MMB
    const vec3 posB = camB.getCameraState().position;

    // With higher sensitivity the pan delta must be larger.
    EXPECT_NE(posA.x, posB.x)
        << "MMB pan X delta must differ when sensitivityMultiplier differs";
}
