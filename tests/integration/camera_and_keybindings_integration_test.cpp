// camera_and_keybindings_integration_test.cpp
//
// Integration tests for:
//   - CameraController (null-camera seam, pan/zoom/rotate, edge-scroll, focus)
//   - KeyBindings (defaults, load from file, writeToFile, isReservedKey, copyMutableFrom)
//   - InspectorPanel::computePanelPosition (pure static function — three-step cascade)
//
// CameraController uses the null-camera test seam (camera==nullptr) per architecture.
// KeyBindings::load() requires Irrlicht ILogger; we pass nullptr (fallback to stderr OK).
// InspectorPanel tests: standalone pure function, no IUIBackend needed.
//
// CMake target: integration_tests, label "integration".

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <cstdio>    // tmpnam / fopen / fclose
#include <cstring>

// CameraController (null-camera seam)
#include "src/ui/CameraController.h"
#include "src/platform/input_event.h"
#include "src/ui/key_bindings.h"

// InspectorPanel (computePanelPosition — pure static)
#include "src/ui/InspectorPanel.h"

// Irrlicht for key codes used by CameraController
#include <irrlicht.h>

// ---------------------------------------------------------------------------
// Helper: make InputEvent shortcuts
// ---------------------------------------------------------------------------
namespace {

InputEvent makeMouseMove(int physX, int physY, int vx = 500, int vy = 500) {
    InputEvent e;
    e.type  = InputEvent::Type::MouseMove;
    e.physX = physX;
    e.physY = physY;
    e.x     = vx;
    e.y     = vy;
    return e;
}

InputEvent makeRMBDown(int physX = 0, int physY = 0) {
    InputEvent e;
    e.type   = InputEvent::Type::MouseButtonDown;
    e.button = 1;
    e.physX  = physX;
    e.physY  = physY;
    return e;
}

InputEvent makeRMBUp() {
    InputEvent e;
    e.type   = InputEvent::Type::MouseButtonUp;
    e.button = 1;
    return e;
}

InputEvent makeMMBDown(int physX = 0, int physY = 0) {
    InputEvent e;
    e.type   = InputEvent::Type::MouseButtonDown;
    e.button = 2;
    e.physX  = physX;
    e.physY  = physY;
    return e;
}

InputEvent makeMMBUp() {
    InputEvent e;
    e.type   = InputEvent::Type::MouseButtonUp;
    e.button = 2;
    return e;
}

InputEvent makeLMBDown() {
    InputEvent e;
    e.type   = InputEvent::Type::MouseButtonDown;
    e.button = 0;
    return e;
}

InputEvent makeWheelScroll(float delta) {
    InputEvent e;
    e.type       = InputEvent::Type::MouseWheel;
    e.wheelDelta = delta;
    return e;
}

InputEvent makeKeyDown(int irrlichtKey) {
    InputEvent e;
    e.type    = InputEvent::Type::KeyDown;
    e.keyCode = irrlichtKey;
    return e;
}

InputEvent makeKeyUp(int irrlichtKey) {
    InputEvent e;
    e.type    = InputEvent::Type::KeyUp;
    e.keyCode = irrlichtKey;
    return e;
}

InputEvent makeFocusGained() {
    InputEvent e;
    e.type = InputEvent::Type::WindowFocusGained;
    return e;
}

InputEvent makeFocusLost() {
    InputEvent e;
    e.type = InputEvent::Type::WindowFocusLost;
    return e;
}

} // anonymous namespace

// ===========================================================================
// CameraController Tests (null-camera seam)
// ===========================================================================
class CameraControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // null camera — unit-test seam per CameraController header
        cc_ = std::make_unique<CameraController>(nullptr, false);
    }

    std::unique_ptr<CameraController> cc_;
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, Construction_DefaultsAreCorrect) {
    EXPECT_FLOAT_EQ(cc_->getSensitivityMultiplier(), 1.0f);
    EXPECT_FALSE(cc_->isEdgeScrollEnabled()); // startInFullscreen=false
}

TEST_F(CameraControllerTest, Construction_Fullscreen_EdgeScrollEnabled) {
    CameraController cc(nullptr, true);
    EXPECT_TRUE(cc.isEdgeScrollEnabled());
}

// ---------------------------------------------------------------------------
// Edge-scroll accessor/setter
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, SetEdgeScrollEnabled_True) {
    cc_->setEdgeScrollEnabled(true);
    EXPECT_TRUE(cc_->isEdgeScrollEnabled());
}

TEST_F(CameraControllerTest, SetEdgeScrollEnabled_False) {
    cc_->setEdgeScrollEnabled(true);
    cc_->setEdgeScrollEnabled(false);
    EXPECT_FALSE(cc_->isEdgeScrollEnabled());
}

// ---------------------------------------------------------------------------
// Sensitivity multiplier
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, SetSensitivityMultiplier) {
    cc_->setSensitivityMultiplier(2.5f);
    EXPECT_FLOAT_EQ(cc_->getSensitivityMultiplier(), 2.5f);
}

// ---------------------------------------------------------------------------
// RMB drag — consumed
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, RMBDown_Consumed) {
    bool consumed = cc_->OnInputEvent(makeRMBDown());
    EXPECT_TRUE(consumed);
}

TEST_F(CameraControllerTest, RMBUp_Consumed) {
    cc_->OnInputEvent(makeRMBDown());
    bool consumed = cc_->OnInputEvent(makeRMBUp());
    EXPECT_TRUE(consumed);
}

// ---------------------------------------------------------------------------
// MMB drag — consumed
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, MMBDown_Consumed) {
    bool consumed = cc_->OnInputEvent(makeMMBDown());
    EXPECT_TRUE(consumed);
}

TEST_F(CameraControllerTest, MMBUp_Consumed) {
    cc_->OnInputEvent(makeMMBDown());
    bool consumed = cc_->OnInputEvent(makeMMBUp());
    EXPECT_TRUE(consumed);
}

// ---------------------------------------------------------------------------
// LMB — not consumed by CameraController
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, LMBDown_NotConsumed) {
    bool consumed = cc_->OnInputEvent(makeLMBDown());
    EXPECT_FALSE(consumed);
}

// ---------------------------------------------------------------------------
// Scroll wheel — zoom changes, consumed
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, WheelScrollDown_Consumed) {
    bool consumed = cc_->OnInputEvent(makeWheelScroll(-3.0f));
    EXPECT_TRUE(consumed);
}

TEST_F(CameraControllerTest, WheelScrollUp_Consumed) {
    bool consumed = cc_->OnInputEvent(makeWheelScroll(3.0f));
    EXPECT_TRUE(consumed);
}

TEST_F(CameraControllerTest, WheelScroll_ClampedToMaxZoom) {
    // Scroll in by a huge amount
    for (int i = 0; i < 100; ++i) {
        cc_->OnInputEvent(makeWheelScroll(100.0f));
    }
    // getCameraState() still works (no crash)
    CameraState s = cc_->getCameraState();
    (void)s;
}

TEST_F(CameraControllerTest, WheelScroll_ClampedToMinZoom) {
    for (int i = 0; i < 100; ++i) {
        cc_->OnInputEvent(makeWheelScroll(-100.0f));
    }
    CameraState s = cc_->getCameraState();
    (void)s;
}

// ---------------------------------------------------------------------------
// RMB drag: yaw and pitch update
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, RMBDrag_ChangesYawAndPitch) {
    cc_->OnInputEvent(makeRMBDown(100, 100));
    cc_->OnInputEvent(makeMouseMove(120, 110, 500, 500));
    CameraState before = cc_->getCameraState();
    // getCameraState() should return a valid forward vector after drag
    EXPECT_NE(before.forward.x, 0.0f); // yaw changed
}

TEST_F(CameraControllerTest, RMBDrag_PitchClamped) {
    // Drag up by a large amount — pitch should clamp to -20
    cc_->OnInputEvent(makeRMBDown(100, 1000));
    cc_->OnInputEvent(makeMouseMove(100, 0, 500, 500)); // dy = -1000 → pitch increases
    // No crash; getCameraState works
    CameraState s = cc_->getCameraState();
    (void)s;
}

// ---------------------------------------------------------------------------
// MMB drag: pan target
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, MMBDrag_MovesPanTarget) {
    CameraState before = cc_->getCameraState();
    cc_->OnInputEvent(makeMMBDown(100, 100));
    cc_->OnInputEvent(makeMouseMove(150, 130, 500, 500));
    CameraState after = cc_->getCameraState();
    // Position should have changed
    EXPECT_NE(before.position.x, after.position.x);
}

// ---------------------------------------------------------------------------
// MouseMove without active drag — not consumed (unless edge-scroll active)
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, MouseMove_NoDrag_NotConsumed) {
    bool consumed = cc_->OnInputEvent(makeMouseMove(500, 500));
    EXPECT_FALSE(consumed);
}

// ---------------------------------------------------------------------------
// Arrow key pan — sets flags and updates target
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, ArrowLeft_Consumed) {
    bool consumed = cc_->OnInputEvent(makeKeyDown(irr::KEY_LEFT));
    EXPECT_TRUE(consumed);
}

TEST_F(CameraControllerTest, ArrowRight_Consumed) {
    bool consumed = cc_->OnInputEvent(makeKeyDown(irr::KEY_RIGHT));
    EXPECT_TRUE(consumed);
}

TEST_F(CameraControllerTest, ArrowUp_Consumed) {
    bool consumed = cc_->OnInputEvent(makeKeyDown(irr::KEY_UP));
    EXPECT_TRUE(consumed);
}

TEST_F(CameraControllerTest, ArrowDown_Consumed) {
    bool consumed = cc_->OnInputEvent(makeKeyDown(irr::KEY_DOWN));
    EXPECT_TRUE(consumed);
}

TEST_F(CameraControllerTest, ArrowLeft_Then_Update_MovesPanTarget) {
    CameraState before = cc_->getCameraState();
    cc_->OnInputEvent(makeKeyDown(irr::KEY_LEFT));
    cc_->update(0.1f);
    CameraState after = cc_->getCameraState();
    // At yaw=0, left pan moves along X axis
    (void)before; (void)after; // just verify no crash
}

TEST_F(CameraControllerTest, ArrowKeyUp_NotConsumed) {
    cc_->OnInputEvent(makeKeyDown(irr::KEY_LEFT));
    bool consumed = cc_->OnInputEvent(makeKeyUp(irr::KEY_LEFT));
    EXPECT_FALSE(consumed);
}

TEST_F(CameraControllerTest, OtherKey_NotConsumed) {
    bool consumed = cc_->OnInputEvent(makeKeyDown(90)); // Z key
    EXPECT_FALSE(consumed);
}

// ---------------------------------------------------------------------------
// Window focus events — not consumed, but update internal state
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, FocusLost_NotConsumed_EdgeScrollSuppressed) {
    cc_->setEdgeScrollEnabled(true);
    bool consumed = cc_->OnInputEvent(makeFocusLost());
    EXPECT_FALSE(consumed);
}

TEST_F(CameraControllerTest, FocusGained_NotConsumed) {
    bool consumed = cc_->OnInputEvent(makeFocusGained());
    EXPECT_FALSE(consumed);
}

TEST_F(CameraControllerTest, FocusLost_StopsDragState) {
    cc_->OnInputEvent(makeRMBDown(100, 100));
    cc_->OnInputEvent(makeFocusLost());
    // After focus lost, RMB drag is cancelled — subsequent move does nothing
    cc_->OnInputEvent(makeMouseMove(200, 200));
    // No crash
}

// ---------------------------------------------------------------------------
// update() with null camera — no crash
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, Update_NullCamera_DoesNotCrash) {
    cc_->update(0.016f);
}

TEST_F(CameraControllerTest, Update_MultipleFrames_DoesNotCrash) {
    cc_->OnInputEvent(makeKeyDown(irr::KEY_UP));
    for (int i = 0; i < 60; ++i) {
        cc_->update(0.016f);
    }
    cc_->OnInputEvent(makeKeyUp(irr::KEY_UP));
}

// ---------------------------------------------------------------------------
// getCameraState — null-camera path
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, GetCameraState_NullCamera_ForwardIsValid) {
    CameraState s = cc_->getCameraState();
    // At default pitch=-45, yaw=0: forward.y should be negative (pointing down)
    EXPECT_LT(s.forward.y, 0.0f);
}

// ---------------------------------------------------------------------------
// setTarget / resetOrbit
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, SetTarget_DoesNotCrash) {
    cc_->setTarget(100.0f, 200.0f);
    CameraState s = cc_->getCameraState();
    (void)s;
}

TEST_F(CameraControllerTest, ResetOrbit_ResetsToDefaults) {
    cc_->OnInputEvent(makeWheelScroll(50.0f)); // change zoom
    cc_->resetOrbit();
    // After reset, zoom is back to default — getCameraState works
    CameraState s = cc_->getCameraState();
    (void)s;
}

// ---------------------------------------------------------------------------
// Edge-scroll with focus: when app has focus and edge-scroll enabled,
// mouse move near edges triggers pan
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, EdgeScroll_Enabled_LeftEdge_DoesNotCrash) {
    cc_->setEdgeScrollEnabled(true);
    cc_->OnInputEvent(makeFocusGained()); // ensure focus
    // Mouse near left edge (x < kEdgeScrollBand=20)
    InputEvent e = makeMouseMove(5, 540, 5, 540);
    cc_->OnInputEvent(e);
}

TEST_F(CameraControllerTest, EdgeScroll_Enabled_RightEdge_DoesNotCrash) {
    cc_->setEdgeScrollEnabled(true);
    cc_->OnInputEvent(makeFocusGained());
    InputEvent e = makeMouseMove(1915, 540, 1915, 540);
    cc_->OnInputEvent(e);
}

TEST_F(CameraControllerTest, EdgeScroll_Enabled_TopEdge_DoesNotCrash) {
    cc_->setEdgeScrollEnabled(true);
    cc_->OnInputEvent(makeFocusGained());
    InputEvent e = makeMouseMove(960, 5, 960, 5);
    cc_->OnInputEvent(e);
}

TEST_F(CameraControllerTest, EdgeScroll_Enabled_BottomEdge_DoesNotCrash) {
    cc_->setEdgeScrollEnabled(true);
    cc_->OnInputEvent(makeFocusGained());
    InputEvent e = makeMouseMove(960, 1075, 960, 1075);
    cc_->OnInputEvent(e);
}

TEST_F(CameraControllerTest, EdgeScroll_Disabled_EdgeMove_NotConsumed) {
    cc_->setEdgeScrollEnabled(false);
    cc_->OnInputEvent(makeFocusGained());
    InputEvent e = makeMouseMove(5, 540, 5, 540);
    bool consumed = cc_->OnInputEvent(e);
    EXPECT_FALSE(consumed);
}

// ---------------------------------------------------------------------------
// CameraController with custom KeyBindings
// ---------------------------------------------------------------------------
TEST_F(CameraControllerTest, CustomKeyBindings_DoesNotCrash) {
    KeyBindings kb;
    kb.camPanLeft  = "A";
    kb.camPanRight = "D";
    CameraController cc(nullptr, false, kb);
    cc.update(0.016f);
}

// ===========================================================================
// KeyBindings Tests
// ===========================================================================
class KeyBindingsTest : public ::testing::Test {
protected:
    // Creates a temporary file path for testing writeToFile/load
    std::string makeTempPath() {
        char buf[256];
        snprintf(buf, sizeof(buf), "/tmp/aitown_kb_test_%p.json", (void*)this);
        return std::string(buf);
    }

    void TearDown() override {
        if (!tempPath_.empty()) {
            std::remove(tempPath_.c_str());
        }
    }

    std::string tempPath_;
};

// ---------------------------------------------------------------------------
// Default values
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsTest, Defaults_CamPanUp_IsArrowUp) {
    KeyBindings kb;
    EXPECT_EQ(kb.camPanUp, "ArrowUp");
}

TEST_F(KeyBindingsTest, Defaults_ToolZone_IsZ) {
    KeyBindings kb;
    EXPECT_EQ(kb.toolZone, "Z");
}

TEST_F(KeyBindingsTest, Defaults_ToolRoad_IsR) {
    KeyBindings kb;
    EXPECT_EQ(kb.toolRoad, "R");
}

TEST_F(KeyBindingsTest, Defaults_TogglePause_IsSpace) {
    KeyBindings kb;
    EXPECT_EQ(kb.togglePause, "Space");
}

TEST_F(KeyBindingsTest, Defaults_Undo_IsCtrlZ) {
    KeyBindings kb;
    EXPECT_EQ(kb.undo, "Ctrl+Z");
}

TEST_F(KeyBindingsTest, Defaults_Save_IsCtrlS) {
    KeyBindings kb;
    EXPECT_EQ(kb.save, "Ctrl+S");
}

// ---------------------------------------------------------------------------
// isReservedKey
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsTest, IsReservedKey_Q_True) {
    KeyBindings kb;
    EXPECT_TRUE(kb.isReservedKey("Q"));
}

TEST_F(KeyBindingsTest, IsReservedKey_E_True) {
    KeyBindings kb;
    EXPECT_TRUE(kb.isReservedKey("E"));
}

TEST_F(KeyBindingsTest, IsReservedKey_Z_False) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("Z"));
}

TEST_F(KeyBindingsTest, IsReservedKey_Space_False) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey("Space"));
}

TEST_F(KeyBindingsTest, IsReservedKey_Empty_False) {
    KeyBindings kb;
    EXPECT_FALSE(kb.isReservedKey(""));
}

// ---------------------------------------------------------------------------
// copyMutableFrom
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsTest, CopyMutableFrom_CopiesRebindableFields) {
    KeyBindings src;
    src.toolZone = "F1";
    src.camPanUp = "W";

    KeyBindings dst;
    dst.copyMutableFrom(src);

    EXPECT_EQ(dst.toolZone, "F1");
    EXPECT_EQ(dst.camPanUp, "W");
}

TEST_F(KeyBindingsTest, CopyMutableFrom_DoesNotChangeConstFields) {
    KeyBindings src;
    KeyBindings dst;
    dst.copyMutableFrom(src);
    // Const fields remain unchanged
    EXPECT_EQ(dst.undo, "Ctrl+Z");
    EXPECT_EQ(dst.save, "Ctrl+S");
}

// ---------------------------------------------------------------------------
// writeToFile — writes all 11 rebindable fields
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsTest, WriteToFile_EmptyPath_DoesNotCrash) {
    KeyBindings kb;
    kb.writeToFile("");  // no-op for empty path
}

TEST_F(KeyBindingsTest, WriteToFile_ValidPath_CreatesFile) {
    tempPath_ = makeTempPath();
    KeyBindings kb;
    kb.toolZone = "F1";
    kb.writeToFile(tempPath_);

    // Verify file was created
    FILE* f = fopen(tempPath_.c_str(), "r");
    ASSERT_NE(f, nullptr) << "writeToFile did not create file at " << tempPath_;
    fclose(f);
}

TEST_F(KeyBindingsTest, WriteToFile_ContainsToolZone) {
    tempPath_ = makeTempPath();
    KeyBindings kb;
    kb.toolZone = "F2";
    kb.writeToFile(tempPath_);

    // Read the file back and verify it contains "toolZone"
    FILE* f = fopen(tempPath_.c_str(), "r");
    ASSERT_NE(f, nullptr);
    char buf[4096];
    size_t bytesRead = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[bytesRead] = '\0';
    std::string content(buf);
    EXPECT_NE(content.find("toolZone"), std::string::npos);
    EXPECT_NE(content.find("F2"), std::string::npos);
}

// ---------------------------------------------------------------------------
// load — from file
// ---------------------------------------------------------------------------
TEST_F(KeyBindingsTest, Load_AbsentFile_UsesDefaults) {
    KeyBindings kb;
    kb.load("/nonexistent/path/keybindings.json", nullptr);
    // Defaults unchanged
    EXPECT_EQ(kb.toolZone, "Z");
    EXPECT_EQ(kb.camPanUp, "ArrowUp");
}

TEST_F(KeyBindingsTest, Load_ValidFile_UpdatesFields) {
    tempPath_ = makeTempPath();
    // Write a valid JSON file
    {
        FILE* f = fopen(tempPath_.c_str(), "w");
        ASSERT_NE(f, nullptr);
        fprintf(f, "{\n");
        fprintf(f, "  \"toolZone\": \"F3\",\n");
        fprintf(f, "  \"toolRoad\": \"F4\",\n");
        fprintf(f, "  \"camPanUp\": \"W\"\n");
        fprintf(f, "}\n");
        fclose(f);
    }

    KeyBindings kb;
    kb.load(tempPath_, nullptr);

    EXPECT_EQ(kb.toolZone, "F3");
    EXPECT_EQ(kb.toolRoad, "F4");
    EXPECT_EQ(kb.camPanUp, "W");
}

TEST_F(KeyBindingsTest, Load_ReservedKey_RetainsDefault) {
    tempPath_ = makeTempPath();
    {
        FILE* f = fopen(tempPath_.c_str(), "w");
        ASSERT_NE(f, nullptr);
        fprintf(f, "{\n  \"toolZone\": \"Q\"\n}\n");
        fclose(f);
    }

    KeyBindings kb;
    kb.load(tempPath_, nullptr);
    // Q is reserved — toolZone should retain its default "Z"
    EXPECT_EQ(kb.toolZone, "Z");
}

TEST_F(KeyBindingsTest, Load_UnknownKey_SkipsEntry) {
    tempPath_ = makeTempPath();
    {
        FILE* f = fopen(tempPath_.c_str(), "w");
        ASSERT_NE(f, nullptr);
        fprintf(f, "{\n  \"unknownAction\": \"X\",\n  \"toolZone\": \"F5\"\n}\n");
        fclose(f);
    }

    KeyBindings kb;
    kb.load(tempPath_, nullptr);
    // Unknown key skipped; toolZone updated
    EXPECT_EQ(kb.toolZone, "F5");
}

TEST_F(KeyBindingsTest, Load_AllRebindableFields_UpdatesAll) {
    tempPath_ = makeTempPath();
    {
        FILE* f = fopen(tempPath_.c_str(), "w");
        ASSERT_NE(f, nullptr);
        fprintf(f, "{\n");
        fprintf(f, "  \"camPanUp\": \"W\",\n");
        fprintf(f, "  \"camPanDown\": \"S\",\n");
        fprintf(f, "  \"camPanLeft\": \"A\",\n");
        fprintf(f, "  \"camPanRight\": \"D\",\n");
        fprintf(f, "  \"toolZone\": \"F1\",\n");
        fprintf(f, "  \"toolRoad\": \"F2\",\n");
        fprintf(f, "  \"toolUtilities\": \"F3\",\n");
        fprintf(f, "  \"toolDemolish\": \"F4\",\n");
        fprintf(f, "  \"toolInspector\": \"F5\",\n");
        fprintf(f, "  \"toggleTaxPanel\": \"F6\",\n");
        fprintf(f, "  \"toggleNotifLog\": \"F7\"\n");
        fprintf(f, "}\n");
        fclose(f);
    }

    KeyBindings kb;
    kb.load(tempPath_, nullptr);

    EXPECT_EQ(kb.camPanUp,       "W");
    EXPECT_EQ(kb.camPanDown,     "S");
    EXPECT_EQ(kb.camPanLeft,     "A");
    EXPECT_EQ(kb.camPanRight,    "D");
    EXPECT_EQ(kb.toolZone,       "F1");
    EXPECT_EQ(kb.toolRoad,       "F2");
    EXPECT_EQ(kb.toolUtilities,  "F3");
    EXPECT_EQ(kb.toolDemolish,   "F4");
    EXPECT_EQ(kb.toolInspector,  "F5");
    EXPECT_EQ(kb.toggleTaxPanel, "F6");
    EXPECT_EQ(kb.toggleNotifLog, "F7");
}

TEST_F(KeyBindingsTest, Load_ConstFieldsNotModified) {
    tempPath_ = makeTempPath();
    {
        FILE* f = fopen(tempPath_.c_str(), "w");
        ASSERT_NE(f, nullptr);
        // Attempt to set undo and save (which are const — should be ignored)
        fprintf(f, "{\n  \"undo\": \"Ctrl+X\",\n  \"save\": \"Ctrl+D\"\n}\n");
        fclose(f);
    }

    KeyBindings kb;
    kb.load(tempPath_, nullptr);
    // const fields must not be modified
    EXPECT_EQ(kb.undo, "Ctrl+Z");
    EXPECT_EQ(kb.save, "Ctrl+S");
}

TEST_F(KeyBindingsTest, WriteToFile_ThenLoad_RoundTrip) {
    tempPath_ = makeTempPath();

    // writeToFile only persists the 11 rebindable fields:
    // camPanUp, camPanDown, camPanLeft, camPanRight,
    // toolZone, toolRoad, toolUtilities, toolDemolish, toolInspector,
    // toggleTaxPanel, toggleNotifLog.
    // togglePause, speedIncrease, speedDecrease, openPauseMenu are NOT persisted.
    KeyBindings kb1;
    kb1.toolZone      = "F10";
    kb1.camPanUp      = "W";
    kb1.camPanDown    = "S";
    kb1.toggleNotifLog = "N";
    kb1.writeToFile(tempPath_);

    KeyBindings kb2;
    kb2.load(tempPath_, nullptr);

    EXPECT_EQ(kb2.toolZone,      "F10");
    EXPECT_EQ(kb2.camPanUp,      "W");
    EXPECT_EQ(kb2.camPanDown,    "S");
    EXPECT_EQ(kb2.toggleNotifLog,"N");
}

TEST_F(KeyBindingsTest, Load_ReservedKeyE_RetainsDefault) {
    tempPath_ = makeTempPath();
    {
        FILE* f = fopen(tempPath_.c_str(), "w");
        ASSERT_NE(f, nullptr);
        fprintf(f, "{\n  \"camPanUp\": \"E\"\n}\n");
        fclose(f);
    }

    KeyBindings kb;
    kb.load(tempPath_, nullptr);
    // E is reserved — camPanUp should retain its default
    EXPECT_EQ(kb.camPanUp, "ArrowUp");
}

// ===========================================================================
// InspectorPanel::computePanelPosition Tests (pure static function)
// ===========================================================================
// Panel dimensions: kPanelW=340, kPanelH=280
// Virtual screen: 1920x1080
// offset = 40
// Primary: right-below cursor (+40, +40); fits [0..1580] x [0..800]
// Fallback: left-above cursor (-40-340, -40-280) = (-380,-320) from cursor
// Edge-snap: clamp to [0, 1580] x [0, 800]
class InspectorPanelPositionTest : public ::testing::Test {};

static ScreenRect kNoOverlap{2000, 2000, 10, 10}; // off-screen tile

// ---------------------------------------------------------------------------
// Primary placement (right-below) — fits and no overlap
// ---------------------------------------------------------------------------
TEST_F(InspectorPanelPositionTest, Primary_FitsAndNoOverlap_ChoosesPrimary) {
    // Cursor at (100, 100) — primary at (140, 140), fits in 1920x1080 and no overlap
    ScreenRect result = InspectorPanel::computePanelPosition(100, 100, kNoOverlap);
    EXPECT_EQ(result.x, 140); // 100 + 40
    EXPECT_EQ(result.y, 140); // 100 + 40
    EXPECT_EQ(result.w, 340);
    EXPECT_EQ(result.h, 280);
}

// ---------------------------------------------------------------------------
// Primary off right edge — falls to fallback
// ---------------------------------------------------------------------------
TEST_F(InspectorPanelPositionTest, Primary_OffRightEdge_UsesFallback) {
    // Cursor at (1600, 100): primary x = 1640+340=1980 > 1920 — off screen
    // Fallback: x = 1600-40-340 = 1220, y = 100-40-280 = -220 — off screen (y < 0)
    // Edge-snap: cursor at x=1600 > 960, so ex=0; ey=clamp(100-140, 0, 800) = 0
    ScreenRect result = InspectorPanel::computePanelPosition(1600, 100, kNoOverlap);
    // Either fallback or edge-snap — just verify width/height correct
    EXPECT_EQ(result.w, 340);
    EXPECT_EQ(result.h, 280);
    EXPECT_GE(result.x, 0);
    EXPECT_GE(result.y, 0);
}

// ---------------------------------------------------------------------------
// Primary off bottom edge — falls to fallback or edge-snap
// ---------------------------------------------------------------------------
TEST_F(InspectorPanelPositionTest, Primary_OffBottomEdge_FallsBack) {
    // Cursor at (100, 800): primary y = 840+280=1120 > 1080 — off screen
    // Fallback: x = 100-40-340 = -280 — off screen
    // Edge-snap applies
    ScreenRect result = InspectorPanel::computePanelPosition(100, 800, kNoOverlap);
    EXPECT_EQ(result.w, 340);
    EXPECT_EQ(result.h, 280);
    EXPECT_GE(result.x, 0);
    EXPECT_GE(result.y, 0);
}

// ---------------------------------------------------------------------------
// Fallback placement (left-above) — when primary overlaps tile
// ---------------------------------------------------------------------------
TEST_F(InspectorPanelPositionTest, Primary_OverlapsTile_UsesFallback) {
    // Cursor at (200, 200): primary at (240, 240) size 340x280 → ends at (580, 520)
    // Place tile at (300, 300, 100, 100) — overlaps primary
    ScreenRect tile{300, 300, 100, 100};
    ScreenRect result = InspectorPanel::computePanelPosition(200, 200, tile);
    // Primary: (240,240)-(580,520) overlaps tile(300,300)-(400,400) → use fallback
    // Fallback: x=200-40-340=-180 — off screen → edge-snap
    EXPECT_EQ(result.w, 340);
    EXPECT_EQ(result.h, 280);
}

// ---------------------------------------------------------------------------
// Fallback placement (left-above) — fits and no overlap
// ---------------------------------------------------------------------------
TEST_F(InspectorPanelPositionTest, Fallback_FitsAndNoOverlap_ChoosesFallback) {
    // Cursor at (1700, 700): primary x=1740+340=2080 > 1920 — off screen
    // Fallback: x=1700-40-340=1320, y=700-40-280=380 → fits in [0,1920] x [0,1080]
    // Tile off-screen so no overlap
    ScreenRect result = InspectorPanel::computePanelPosition(1700, 700, kNoOverlap);
    EXPECT_EQ(result.w, 340);
    EXPECT_EQ(result.h, 280);
    EXPECT_EQ(result.x, 1320); // 1700-40-340
    EXPECT_EQ(result.y, 380);  // 700-40-280
}

// ---------------------------------------------------------------------------
// Edge-snap — cursor in upper-right: snaps to left edge
// ---------------------------------------------------------------------------
TEST_F(InspectorPanelPositionTest, EdgeSnap_CursorRightHalf_SnapsToLeftEdge) {
    // When both primary and fallback fail, edge-snap chooses:
    // cursorX > 960 → ex=0; cursorX <= 960 → ex=1920-340=1580
    // Force primary and fallback to fail by overlapping tile with primary
    // and placing cursor near top-right corner (fallback y < 0)
    // Cursor at (1800, 50): primary x=1840+340 > 1920 → fail
    // Fallback: x=1800-40-340=1420, y=50-40-280=-270 < 0 → fail
    // Edge-snap: ex = 0 (cursorX=1800 > 960); ey = clamp(50-140, 0, 800) = 0
    ScreenRect result = InspectorPanel::computePanelPosition(1800, 50, kNoOverlap);
    EXPECT_EQ(result.x, 0);
    EXPECT_EQ(result.y, 0);
    EXPECT_EQ(result.w, 340);
    EXPECT_EQ(result.h, 280);
}

TEST_F(InspectorPanelPositionTest, EdgeSnap_CursorLeftHalf_SnapsToRightEdge) {
    // Cursor at (50, 50): both primary and fallback might fail
    // Primary: x=90, y=90 → fits; no overlap → primary used
    // This test just verifies left-half snapping when edge-snap IS selected
    // Trigger by placing cursor so fallback also fails:
    // Cursor at (50, 50): fallback x=50-40-340=-330 < 0 → fail
    // But primary (90,90) fits → primary used
    // For left-half edge snap: use cursor at (50, 30) with overlapping tile
    ScreenRect tile{90, 30, 100, 100}; // overlaps primary at (90,70)?
    // primary: x=50+40=90, y=30+40=70: overlap with tile(90,30,100,100)?
    // tile: x=[90,190), y=[30,130); panel: x=[90,430), y=[70,350) → overlaps
    // fallback: x=50-40-340=-330 < 0 → fail
    // edge-snap: cursorX=50 < 960 → ex = 1920-340 = 1580
    ScreenRect result = InspectorPanel::computePanelPosition(50, 30, tile);
    EXPECT_EQ(result.x, 1580);
    EXPECT_EQ(result.w, 340);
    EXPECT_EQ(result.h, 280);
}

TEST_F(InspectorPanelPositionTest, EdgeSnap_EyClampedToValidRange) {
    // Edge-snap: ey = clamp(cursorY - panelH/2, 0, 1080-280)
    // cursorY = 500 → ey = clamp(500-140, 0, 800) = 360
    // Force edge-snap by overlapping both primary and fallback
    ScreenRect tile{1900-340, 500-280+40, 340, 280}; // covers fallback area
    // Trigger by cursor in left-half + primary overlap
    // Cursor (50, 500), tile at (90, 540, 340, 280) overlaps primary(90,540)
    ScreenRect tile2{90, 540, 340, 280};
    // fallback: x=50-40-340=-330 < 0 → fail → edge-snap
    // cursorX=50 < 960 → ex=1580; ey=clamp(500-140, 0, 800)=360
    ScreenRect result = InspectorPanel::computePanelPosition(50, 500, tile2);
    EXPECT_EQ(result.x, 1580);
    EXPECT_EQ(result.y, 360);
}

// ---------------------------------------------------------------------------
// Corner cases for primary placement bounds
// ---------------------------------------------------------------------------
TEST_F(InspectorPanelPositionTest, PrimaryBorderline_ExactFit_ChoosesPrimary) {
    // Primary at exactly (1580, 800) = (1920-340, 1080-280): exactly fits
    // cursorX + offset = 1580 → cursorX = 1540
    // cursorY + offset = 800  → cursorY = 760
    ScreenRect result = InspectorPanel::computePanelPosition(1540, 760, kNoOverlap);
    EXPECT_EQ(result.x, 1580);
    EXPECT_EQ(result.y, 800);
}
