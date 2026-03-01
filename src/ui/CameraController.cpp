#include "CameraController.h"

#include <irrlicht.h>
#include <cmath>
#include <algorithm>  // std::clamp

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper: convert Irrlicht vector3df to our minimal vec3
static vec3 toVec3(const irr::core::vector3df& v) {
    return vec3{v.X, v.Y, v.Z};
}

CameraController::CameraController(irr::scene::ICameraSceneNode* camera, bool startInFullscreen,
                                   const KeyBindings& bindings)
    : m_camera(camera)
    , m_bindings(bindings)
    , m_edgeScrollEnabled(startInFullscreen)
{
    // startInFullscreen=true → edge-scroll ON (fullscreen default)
    // startInFullscreen=false → edge-scroll OFF (windowed default)
    // Production instance uses startInFullscreen=false (1280x720 windowed window)
    // per architecture/ui-ux/camera-controls.md windowed-default rule.
}

bool CameraController::OnInputEvent(const InputEvent& event) {
    using Type = InputEvent::Type;

    switch (event.type) {
    case Type::WindowFocusGained:
        // Restore focus: enable edge-scroll processing if m_edgeScrollEnabled is true.
        // Does NOT modify m_edgeScrollEnabled — that is the player's explicit preference.
        m_appHasFocus = true;
        return false; // never consumed by CameraController

    case Type::WindowFocusLost:
        // Suppress edge-scroll processing. Does NOT change m_edgeScrollEnabled.
        m_appHasFocus = false;
        // Also reset drag state on focus loss to avoid stuck drag
        m_rmbDragActive = false;
        m_mmbDragActive = false;
        return false; // never consumed by CameraController

    case Type::MouseButtonDown:
        if (event.button == 1) {
            // RMB down: start rotate/pan drag
            m_rmbDragActive = true;
            m_prevPhysX = event.physX;
            m_prevPhysY = event.physY;
            return true;
        } else if (event.button == 2) {
            // MMB down: start pan drag
            m_mmbDragActive = true;
            m_prevPhysX = event.physX;
            m_prevPhysY = event.physY;
            return true;
        }
        return false;

    case Type::MouseButtonUp:
        if (event.button == 1) {
            m_rmbDragActive = false;
            return true;
        } else if (event.button == 2) {
            m_mmbDragActive = false;
            return true;
        }
        return false;

    case Type::MouseMove:
        {
            const int dx = event.physX - m_prevPhysX;
            const int dy = event.physY - m_prevPhysY;
            bool consumed = false;

            if (m_rmbDragActive) {
                // RMB drag: rotate (yaw and pitch)
                // UX-1: drag-delta MUST use physX/physY, NOT virtual x/y.
                m_yaw   += static_cast<float>(dx) * kRotateSpeed;
                m_pitch -= static_cast<float>(dy) * kRotateSpeed;
                // Clamp pitch to [-70, -20] INCLUSIVE (std::clamp semantics)
                m_pitch = std::clamp(m_pitch, -70.0f, -20.0f);
                consumed = true;
            }

            if (m_mmbDragActive) {
                // MMB drag: pan (move target in world XZ plane)
                // UX-1: drag-delta MUST use physX/physY, NOT virtual x/y.
                // Pan speed scales with zoom distance: panSpeed = kBasePanSpeed * (zoom / defaultZoom)
                const float panSpeed = kBasePanSpeed * (m_zoomDistance / kDefaultZoomDistance)
                                     * m_sensitivityMultiplier;
                // Negate dx for camera-right to world-right mapping
                // Negate dy for screen-down to world-back mapping
                const float yaw_rad = m_yaw * static_cast<float>(M_PI / 180.0);
                // Right vector in XZ plane: perpendicular to forward at yaw
                const float rightX =  std::cos(yaw_rad);
                const float rightZ = -std::sin(yaw_rad);
                // Forward vector in XZ plane
                const float fwdX   = std::sin(yaw_rad);
                const float fwdZ   = std::cos(yaw_rad);

                const float scale = panSpeed * 0.01f; // tune: pixels to world units
                m_targetX -= static_cast<float>(dx) * rightX * scale;
                m_targetZ -= static_cast<float>(dx) * rightZ * scale;
                m_targetX += static_cast<float>(dy) * fwdX  * scale;
                m_targetZ += static_cast<float>(dy) * fwdZ  * scale;
                consumed = true;
            }

            // Edge scroll (virtual coordinate space — InputEvent.x)
            // Only active when m_appHasFocus AND m_edgeScrollEnabled
            if (m_appHasFocus && m_edgeScrollEnabled) {
                // Edge-scroll activation band: 20 px from each edge in 1920x1080 virtual space
                const float panSpeed = kBasePanSpeed * (m_zoomDistance / kDefaultZoomDistance)
                                     * m_sensitivityMultiplier;
                const float scale = panSpeed * 0.01f;
                const float yaw_rad = m_yaw * static_cast<float>(M_PI / 180.0);
                const float rightX =  std::cos(yaw_rad);
                const float rightZ = -std::sin(yaw_rad);
                const float fwdX   = std::sin(yaw_rad);
                const float fwdZ   = std::cos(yaw_rad);

                if (event.x < kEdgeScrollBand) {
                    // Left edge: pan left (negative right direction)
                    m_targetX -= rightX * scale;
                    m_targetZ -= rightZ * scale;
                } else if (event.x > (1920 - kEdgeScrollBand)) {
                    // Right edge: pan right
                    m_targetX += rightX * scale;
                    m_targetZ += rightZ * scale;
                }
                if (event.y < kEdgeScrollBand) {
                    // Top edge: pan forward
                    m_targetX += fwdX * scale;
                    m_targetZ += fwdZ * scale;
                } else if (event.y > (1080 - kEdgeScrollBand)) {
                    // Bottom edge: pan backward
                    m_targetX -= fwdX * scale;
                    m_targetZ -= fwdZ * scale;
                }
            }

            m_prevPhysX = event.physX;
            m_prevPhysY = event.physY;
            return consumed;
        }

    case Type::MouseWheel:
        // Scroll wheel controls zoom distance ONLY — NOT pitch angle.
        // (Pitch clamp tests use RMB vertical drag, not scroll wheel.)
        m_zoomDistance -= event.wheelDelta * kZoomSpeed;
        m_zoomDistance = std::clamp(m_zoomDistance, kMinZoomDistance, kMaxZoomDistance);
        return true;

    case Type::KeyDown:
        {
            bool consumed = true;
            // Irrlicht key codes — set held-state flags for continuous pan in update(dt)
            if (event.keyCode == irr::KEY_LEFT) {
                m_panLeft = true;
            } else if (event.keyCode == irr::KEY_RIGHT) {
                m_panRight = true;
            } else if (event.keyCode == irr::KEY_UP) {
                m_panForward = true;
            } else if (event.keyCode == irr::KEY_DOWN) {
                m_panBackward = true;
            } else {
                consumed = false;
            }
            return consumed;
        }

    case Type::KeyUp:
        {
            if (event.keyCode == irr::KEY_LEFT) {
                m_panLeft = false;
            } else if (event.keyCode == irr::KEY_RIGHT) {
                m_panRight = false;
            } else if (event.keyCode == irr::KEY_UP) {
                m_panForward = false;
            } else if (event.keyCode == irr::KEY_DOWN) {
                m_panBackward = false;
            }
            return false;
        }

    default:
        return false;
    }
}

void CameraController::update(float dt) {
    // --- Continuous keyboard pan (arrow-key held state) ---
    // Applied before the null-camera guard because getCameraState() reads
    // m_targetX/m_targetZ even for null cameras (unit-test path).
    // KEYBOARD PAN MUST NOT apply sensitivityMultiplier — only MMB drag and edge-scroll
    // apply it per architecture/ui-ux/camera-controls.md.
    if (m_panLeft || m_panRight || m_panForward || m_panBackward) {
        const float panSpeed = kBasePanSpeed * (m_zoomDistance / kDefaultZoomDistance)
                             * kKeyboardPanRate;
        const float scale = panSpeed * dt;
        const float yaw_rad_pan = m_yaw * static_cast<float>(M_PI / 180.0);
        const float rightX =  std::cos(yaw_rad_pan);
        const float rightZ = -std::sin(yaw_rad_pan);
        const float fwdX   = std::sin(yaw_rad_pan);
        const float fwdZ   = std::cos(yaw_rad_pan);

        if (m_panLeft) {
            m_targetX -= rightX * scale;
            m_targetZ -= rightZ * scale;
        }
        if (m_panRight) {
            m_targetX += rightX * scale;
            m_targetZ += rightZ * scale;
        }
        if (m_panForward) {
            m_targetX -= fwdX * scale;
            m_targetZ -= fwdZ * scale;
        }
        if (m_panBackward) {
            m_targetX += fwdX * scale;
            m_targetZ += fwdZ * scale;
        }
    }

    if (!m_camera) {
        return; // null-camera test seam: no live scene node to update
    }

    // Compute camera position from spherical coordinates (pitch/yaw/zoom)
    const float pitch_rad = m_pitch * static_cast<float>(M_PI / 180.0);
    const float yaw_rad   = m_yaw   * static_cast<float>(M_PI / 180.0);

    // Offset from target to camera eye (spherical to Cartesian in Irrlicht left-handed Y-up)
    const float cosP = std::cos(pitch_rad);
    const float sinP = std::sin(pitch_rad);
    const float cosY = std::cos(yaw_rad);
    const float sinY = std::sin(yaw_rad);

    // At pitch=-45, the camera is above and behind the target.
    // Offset: right = sinY, up = -sinP (negative pitch → camera above), forward = cosY
    const float offX = cosP * sinY * m_zoomDistance;
    const float offY = -sinP * m_zoomDistance;   // pitch is negative → camera above target
    const float offZ = cosP * cosY * m_zoomDistance;

    const irr::core::vector3df target(m_targetX, 0.0f, m_targetZ);
    const irr::core::vector3df eye(m_targetX + offX, offY, m_targetZ + offZ);

    m_camera->setPosition(eye);
    m_camera->setTarget(target);
    // Flush the absolute-position cache so getCameraState() live path and any
    // headless callers see the correct value from getAbsolutePosition() without
    // requiring a full scene draw (ISceneManager::drawAll).
    m_camera->updateAbsolutePosition();
}

CameraState CameraController::getCameraState() const {
    CameraState state{};

    if (!m_camera) {
        // Null-camera path (unit-test seam): derive forward from m_pitch/m_yaw.
        // Uses Irrlicht left-handed Y-up spherical-to-Cartesian formula.
        // Yaw convention: yaw=0 looks toward +Z (Irrlicht default forward).
        // Positive yaw rotates clockwise when viewed from above (toward +X).
        // forward.z = cos(pitch)*cos(yaw) is positive at yaw=0 — correct for +Z-forward.
        //
        // At pitch=-45°: forward.y = sin(-45°) ≈ -0.707 (negative — camera points downward).
        // At pitch=-45°, yaw=0: forward.z = cos(-45°)*cos(0) ≈ 0.707 (toward +Z).
        // This formula matches architecture/ui-ux/camera-controls.md and is required
        // by getCameraState() null-camera path to match syncListenerToCamera() convention.
        // Note: Z component of BOTH forward and up MUST be negated when constructing
        // AL_ORIENTATION (Irrlicht left-handed → OpenAL right-handed conversion).
        // See architecture/audio-architecture/spatial-audio.md.
        const float pitch_rad = m_pitch * static_cast<float>(M_PI / 180.0);
        const float yaw_rad   = m_yaw   * static_cast<float>(M_PI / 180.0);

        state.forward.x = std::cos(pitch_rad) * std::sin(yaw_rad);
        state.forward.y = std::sin(pitch_rad);   // negative for pitch in [-70°, -20°] — correct
        state.forward.z = std::cos(pitch_rad) * std::cos(yaw_rad);

        // World-up for null-camera path — approved test-seam value
        state.up = vec3{0.0f, 1.0f, 0.0f};

        // Position from internal state
        const float cosP = std::cos(pitch_rad);
        const float sinP = std::sin(pitch_rad);
        const float cosY = std::cos(yaw_rad);
        const float sinY = std::sin(yaw_rad);
        const float offX = cosP * sinY * m_zoomDistance;
        const float offY = -sinP * m_zoomDistance;
        const float offZ = cosP * cosY * m_zoomDistance;
        state.position = vec3{m_targetX + offX, offY, m_targetZ + offZ};
        state.pitch    = m_pitch;
        state.yaw      = m_yaw;

        return state;
    }

    // Live-camera path (camera != nullptr):
    // toVec3() MUST be applied to ALL THREE CameraState fields (ISSUE-A exit criterion):
    //   - position: camera->getAbsolutePosition()
    //   - forward:  derived from camera->getTarget() and camera->getAbsolutePosition()
    //   - up:       camera->getUpVector() — MUST use getUpVector(), NOT hardcode (0,1,0)
    // Hardcoding (0,1,0) for up fails when camera is pitched (incorrect HRTF spatialization).
    const irr::core::vector3df pos = m_camera->getAbsolutePosition();
    const irr::core::vector3df tgt = m_camera->getTarget();
    irr::core::vector3df fwd = (tgt - pos);
    fwd.normalize();

    state.position = toVec3(pos);
    state.forward  = toVec3(fwd);
    state.up       = toVec3(m_camera->getUpVector());  // MUST use getUpVector(), NOT (0,1,0)
    state.pitch    = m_pitch;
    state.yaw      = m_yaw;

    return state;
}
