// camera_controller_live_test.cpp — Integration test for CameraController live-camera path.
//
// Verifies that CameraController::update() and getCameraState() operate correctly
// when constructed with a real Irrlicht ICameraSceneNode (not the null-camera seam
// used by unit tests in camera_controller_test.cpp).
//
// Coverage target:
//   CameraController.cpp lines 12-14  (toVec3() helper — only reachable via live path)
//   CameraController.cpp lines 200-226 (update() live-camera path)
//   CameraController.cpp lines 276-288 (getCameraState() live-camera path)
//
// Uses EDT_NULL device — no display required; safe on headless CI runners.
// Label: "integration" (no display required — EDT_NULL only)
//
// Spec ref: architecture/ui-ux/camera-controls.md
//           architecture/testing/testability-architecture.md §CameraController

#include "src/ui/CameraController.h"
#include "src/platform/input_event.h"
#include "src/interfaces/camera_state.h"

#include <irrlicht.h>
#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Helper: build a MouseButtonDown event
// ---------------------------------------------------------------------------
static InputEvent makeButtonDown(int button, int physX, int physY)
{
    InputEvent ev{};
    ev.type   = InputEvent::Type::MouseButtonDown;
    ev.button = button;
    ev.physX  = physX;
    ev.physY  = physY;
    return ev;
}

// ---------------------------------------------------------------------------
// CameraControllerLiveTest
//
// Each test creates a fresh EDT_NULL device and camera scene node, constructs
// a CameraController against the live camera, exercises update() and
// getCameraState(), then drops the device in teardown.
// ---------------------------------------------------------------------------
class CameraControllerLiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_device = irr::createDevice(
            irr::video::EDT_NULL,
            irr::core::dimension2d<irr::u32>(640, 480));
        ASSERT_NE(m_device, nullptr) << "EDT_NULL device creation failed";

        irr::scene::ISceneManager* smgr = m_device->getSceneManager();
        ASSERT_NE(smgr, nullptr);

        m_camera = smgr->addCameraSceneNode();
        ASSERT_NE(m_camera, nullptr) << "Failed to add camera scene node";
    }

    void TearDown() override {
        if (m_device) {
            m_device->drop();
            m_device = nullptr;
        }
    }

    irr::IrrlichtDevice*            m_device{nullptr};
    irr::scene::ICameraSceneNode*   m_camera{nullptr};
};

// ---------------------------------------------------------------------------
// TEST: LiveCamera_Update_SetsPositionAboveTarget
//
// Calls update() with a live camera (non-null m_camera) and verifies that
// getCameraState() returns a physically plausible position.
//
// At the default initial state (yaw=0, pitch=-45, zoom=200, target=(0,0,0)):
//   eye.y = -sin(-45°) * 200 ≈ +141.4  (camera is above the target — positive Y)
//   getCameraState() live path reads getAbsolutePosition() from Irrlicht.
//   toVec3() helper converts the Irrlicht vector3df to our minimal vec3.
// ---------------------------------------------------------------------------
TEST_F(CameraControllerLiveTest, LiveCamera_Update_SetsPositionAboveTarget) {
    CameraController cam(m_camera, /*startInFullscreen=*/false);

    // update() live path: calls m_camera->setPosition() and m_camera->setTarget().
    cam.update(0.016f);

    // getCameraState() live path: reads m_camera->getAbsolutePosition(),
    // getTarget(), and getUpVector() via toVec3() helper.
    CameraState state = cam.getCameraState();

    // Camera must be above the target plane (positive Y offset) at default pitch=-45.
    EXPECT_GT(state.position.y, 0.0f)
        << "Live-camera getCameraState() must return positive Y "
           "(camera is above target at default pitch=-45)";
}

// ---------------------------------------------------------------------------
// TEST: LiveCamera_GetCameraState_ForwardVector_NonZero
//
// After update(), getCameraState() must return a non-zero forward vector.
// The live path derives forward from (target - position).normalize() via
// toVec3(), which is only reachable when m_camera != nullptr.
// ---------------------------------------------------------------------------
TEST_F(CameraControllerLiveTest, LiveCamera_GetCameraState_ForwardVector_NonZero) {
    CameraController cam(m_camera, false);

    cam.update(0.016f);

    CameraState state = cam.getCameraState();

    // Forward vector magnitude must be non-trivially positive.
    // At yaw=0: forward.z = cos(-45°)*cos(0) ≈ 0.707.
    // At any yaw the magnitude of (target - pos).normalize() is 1.0.
    const float fwdMag = std::sqrt(state.forward.x * state.forward.x
                                 + state.forward.y * state.forward.y
                                 + state.forward.z * state.forward.z);
    EXPECT_GT(fwdMag, 0.5f)
        << "Live-camera forward vector must have non-trivial magnitude after update()";
}

// ---------------------------------------------------------------------------
// TEST: LiveCamera_GetCameraState_UpVector_NonZero
//
// The live path reads m_camera->getUpVector() (not hardcoded (0,1,0)).
// After update() sets position and target, the scene node's up vector is the
// Irrlicht default (0,1,0) for a newly added camera.
// ---------------------------------------------------------------------------
TEST_F(CameraControllerLiveTest, LiveCamera_GetCameraState_UpVector_NonZero) {
    CameraController cam(m_camera, false);

    cam.update(0.016f);

    CameraState state = cam.getCameraState();

    // Irrlicht default up vector for a newly-created camera is (0, 1, 0).
    // The live path MUST use getUpVector() — not hardcode (0,1,0).
    // We verify the Y component is dominant (the camera is upright).
    EXPECT_GT(state.up.y, 0.5f)
        << "Live-camera up vector Y must be > 0.5 (Irrlicht default up is (0,1,0))";
}

// ---------------------------------------------------------------------------
// TEST: LiveCamera_Update_AfterYawChange_PositionChanges
//
// Simulates an RMB drag to change yaw, then calls update() on the live camera.
// getCameraState() must return a position that differs from the pre-drag position,
// confirming that the live-camera path reflects the internal state changes.
// ---------------------------------------------------------------------------
TEST_F(CameraControllerLiveTest, LiveCamera_Update_AfterYawChange_PositionChanges) {
    CameraController cam(m_camera, false);

    // Capture initial live-camera position.
    cam.update(0.016f);
    CameraState stateBefore = cam.getCameraState();

    // Begin RMB drag at (100, 100) and move 100 px right → yaw changes.
    cam.OnInputEvent(makeButtonDown(1, 100, 100));
    InputEvent moveEv{};
    moveEv.type  = InputEvent::Type::MouseMove;
    moveEv.physX = 200;
    moveEv.physY = 100;
    cam.OnInputEvent(moveEv);

    // Call update() again — live path with changed yaw.
    cam.update(0.016f);
    CameraState stateAfter = cam.getCameraState();

    // X or Z position must differ after yaw rotation.
    const bool xDiffers = (stateAfter.position.x != stateBefore.position.x);
    const bool zDiffers = (stateAfter.position.z != stateBefore.position.z);
    EXPECT_TRUE(xDiffers || zDiffers)
        << "Live-camera position must change after RMB yaw rotation";
}
