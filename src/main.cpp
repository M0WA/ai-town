// src/main.cpp — AI Town application entry point.
// Implements the 8-step frame loop per architecture/graphics-architecture/irrlicht-device-lifecycle.md.
//
// 8-step frame sequence:
//   1. Poll events (device->run() + SEvent dispatch via EventReceiver)
//   2. CitySimulation::tick(realDeltaSeconds) [STUB — Phase 6]
//   3. CameraController::update(dt)
//   3b. UIManager::update(realDeltaSeconds)
//   4a. AudioSystem::syncListenerToCamera(cameraState)
//   4b. AudioSystem::update(realDeltaSeconds)
//   5. RenderSystem::beginFrame()
//   6. RenderSystem::drawScene()
//   7. RenderSystem::endFrame()
//
// ALL simulation logic updates and audio updates MUST execute BEFORE beginFrame().

#include "src/rendering/RenderSystem.h"
#include "src/rendering/IrrlichtRenderer.h"
#include "src/rendering/IrrlichtUIBackend.h"
#include "src/ui/UIManager.h"
#include "src/ui/UIScaler.h"
#include "src/ui/CameraController.h"
#include "src/platform/EventReceiver.h"
#include "src/interfaces/WallClock.h"
#include "src/interfaces/camera_state.h"

#include <irrlicht.h>
#include <cstdio>
#include <cmath>

int main() {
    // -------------------------------------------------------------------------
    // Phase 1: Create the Irrlicht device via RenderSystem (RAII owner).
    // -------------------------------------------------------------------------
    RenderSystem renderSystem;
    irr::IrrlichtDevice* device = renderSystem.getDevice();

    if (!device) {
        fprintf(stderr, "[main] FATAL: RenderSystem failed to create a valid device.\n");
        return 1;
    }

    device->setWindowCaption(L"AI Town");

    // -------------------------------------------------------------------------
    // IrrlichtUIBackend — Phase 1 compile stub (17-method stubs; Phase 8 full impl).
    // -------------------------------------------------------------------------
    IrrlichtUIBackend uiBackend(device);

    // -------------------------------------------------------------------------
    // UIScaler — virtual 1920x1080 coordinate space.
    // virtualW=1920, virtualH=1080 are FIXED design constants (NOT derived from screen dims).
    // Physical dims come from IrrlichtUIBackend::getScreenWidth()/getScreenHeight().
    // ISSUE-Q: virtual dimensions MUST be 1920/1080, NOT getScreenWidth()/getScreenHeight().
    // -------------------------------------------------------------------------
    UIScaler uiScaler(
        /*virtualW=*/  1920,
        /*virtualH=*/  1080,
        /*viewportW=*/ uiBackend.getScreenWidth(),
        /*viewportH=*/ uiBackend.getScreenHeight(),
        /*offsetX=*/   0,
        /*offsetY=*/   0
    );

    // -------------------------------------------------------------------------
    // UIManager stub (Phase 3 wires full implementation).
    // Null audio/sim/clock pointers — Phase 1 stub does not use them.
    // -------------------------------------------------------------------------
    UIManager uiManager(&uiBackend, nullptr, nullptr, nullptr);

    // -------------------------------------------------------------------------
    // Camera scene node — addCameraSceneNode() only (never FPS/Maya variants).
    // Post-creation: grab/drop-guarded animator removal loop per scene-graph-ownership.md.
    // -------------------------------------------------------------------------
    irr::scene::ISceneManager* smgr = device->getSceneManager();
    irr::scene::ICameraSceneNode* cameraNode = smgr->addCameraSceneNode();

    if (cameraNode) {
#ifndef NDEBUG
        if (cameraNode->getAnimators().size() > 0) {
            fprintf(stderr,
                "[main] WARNING: unexpected animators on addCameraSceneNode() result "
                "— removing %zu animator(s)\n",
                static_cast<size_t>(cameraNode->getAnimators().size()));
        }
#endif
        while (cameraNode->getAnimators().size() > 0) {
            irr::scene::ISceneNodeAnimator* anim = *cameraNode->getAnimators().begin();
            anim->grab();
            cameraNode->removeAnimator(anim);
            anim->drop();
        }
    }

    // -------------------------------------------------------------------------
    // CameraController — windowed default: startInFullscreen=false (edge-scroll OFF).
    // Per architecture/ui-ux/camera-controls.md windowed-default rule:
    //   production always starts windowed (1280x720), so startInFullscreen=false.
    // -------------------------------------------------------------------------
    CameraController cameraController(cameraNode, /*startInFullscreen=*/false);

    // -------------------------------------------------------------------------
    // IrrlichtRenderer — owns the rendering interface.
    // Constructor signature LOCKED at Phase 1.
    // -------------------------------------------------------------------------
    IrrlichtRenderer renderer(device, &uiManager);

    // -------------------------------------------------------------------------
    // EventReceiver — translates SEvent → InputEvent, dispatches per input-arbitration.md.
    // -------------------------------------------------------------------------
    EventReceiver eventReceiver(&uiScaler, &uiManager, &cameraController);
    device->setEventReceiver(&eventReceiver);

    // -------------------------------------------------------------------------
    // WallClock — production IClock; injects into AudioSystem/CitySimulation at Phase 4/6.
    // Raw delta MUST NEVER be pre-multiplied by speed multiplier here.
    // CitySimulation owns all speed-scaling internally (architecture/game-design/simulation-time.md).
    // -------------------------------------------------------------------------
    WallClock wallClock;
    double prevTime = wallClock.nowSeconds();

    // =========================================================================
    // 8-STEP FRAME LOOP
    // =========================================================================
    while (device->run()) {
        // Compute real delta — computed ONCE per frame, BEFORE step 2.
        // MUST use steady_clock (raw wall time) — NEVER pre-multiply by speed.
        const double currentTime  = wallClock.nowSeconds();
        const float  realDeltaSeconds = static_cast<float>(currentTime - prevTime);
        prevTime = currentTime;

        // Step 1: Poll events — handled by EventReceiver::OnEvent() via device->run().
        // No explicit poll call needed — device->run() drives EventReceiver.

        // Step 2: CitySimulation::tick(realDeltaSeconds) — STUB (Phase 6 deliverable).
        // DEFAULT SPEED CONTRACT: CitySimulation must be constructed or initialized at
        // SpeedMultiplier::x3 (not x1 or Paused) — see architecture/game-design/simulation-time.md.
        // Phase 6 MUST verify setSpeed(SpeedMultiplier::x3) or equivalent initialization is
        // called; initializing at x1 silently breaks the default starting speed contract.
        // FRAME-LOOP POSITION CONSTRAINT: The tick call MUST remain at step 2 in the 8-step
        // sequence — moving it to any later step violates the Frame-Loop Position Constraint
        // in `architecture/game-design/simulation-time.md` and causes rendered frames to
        // reflect an inconsistent simulation state.
        // TODO Phase 6: citySimulation.tick(realDeltaSeconds);

        // Step 3: CameraController::update(dt).
        // OAL-2 ordering rule: CameraController::update() MUST execute BEFORE
        // AudioSystem::syncListenerToCamera() so the listener reads the updated position.
        cameraController.update(realDeltaSeconds);

        // Step 3b: UIManager::update(realDeltaSeconds) — per-frame UI state update.
        // MUST execute BEFORE beginFrame() per architecture/ui-ux/ui-manager.md.
        uiManager.update(realDeltaSeconds);

        // Steps 4a/4b: AudioSystem stubs — Phase 4 deliverable.
        // Step 4a: AudioSystem::syncListenerToCamera(cameraState) — commits AL_POSITION,
        //   AL_VELOCITY=0, AL_ORIENTATION to OpenAL context BEFORE update() reads them.
        //   (OAL-2: listener must be committed before update() advances occlusion budget.)
        //   COORDINATE CONVERSION: the Z component of BOTH cam.forward AND cam.up MUST be
        //   negated when constructing the AL_ORIENTATION 6-float array:
        //     { cam.forward.x, cam.forward.y, -cam.forward.z,
        //       cam.up.x,      cam.up.y,      -cam.up.z }
        //   Irrlicht uses a LEFT-HANDED coordinate system (Z forward into screen);
        //   OpenAL uses a RIGHT-HANDED coordinate system (Z backward out of screen).
        //   Omitting this Z-negation inverts depth perception for all spatial audio.
        //   Per architecture/audio-architecture/spatial-audio.md — Z-negation required.
        // Step 4b: AudioSystem::update(realDeltaSeconds) — THREE main-thread responsibilities:
        //   (1) advance occlusion raycast budget + per-source distance cull checks
        //       (depends on listener position committed by step 4a);
        //       NOTE: per-source GAINHF state-change writes MUST hold m_occlusionMutex.
        //   (2) process queued time-of-day transition consequences posted by CitySimulation;
        //   (3) queue crossfade commands to audio thread via mutex-protected command queue —
        //       MUST NOT call alSourcef(AL_GAIN) directly on streaming sources from main thread
        //       (per architecture/audio-architecture/dynamic-soundscape.md).
        //   NOTE: alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED) MUST be called in the AudioSystem
        //   constructor immediately after alcMakeContextCurrent succeeds, before the audio thread
        //   is launched — this is NOT a Phase 7 deferral item; Phase 7 cannot defer this call
        //   past context creation. Required for physically correct distance attenuation of
        //   positional SFX; prevents near-field gain clipping (unclamped AL_INVERSE_DISTANCE
        //   allows gain > 1.0 for sources below reference distance);
        //   cross-reference: architecture/audio-architecture/hrtf-initialization.md
        //   NOTE: TODO Phase 7: verify alcSetThreadContext(m_context) on the audio thread does
        //   NOT displace the process-wide context set by alcMakeContextCurrent(m_context) on the
        //   main thread — syncListenerToCamera() calls alListenerfv() from the main thread and
        //   requires the process-wide context to remain valid; ALC_EXT_thread_local_context
        //   semantics guarantee thread-local context isolation but this MUST be verified at
        //   Phase 7 implementation; cross-reference: architecture/audio-architecture/audio-system.md
        // TODO Phase 4:
        //   CameraState cameraState = cameraController.getCameraState();
        //   audioSystem.syncListenerToCamera(cameraState);
        //   audioSystem.update(realDeltaSeconds);

        // Step 5: beginFrame (driver->beginScene).
        renderer.beginFrame();

        // Step 6: drawScene (sceneManager->drawAll() + uiManager->draw()).
        renderer.drawScene();

        // Step 7: endFrame (driver->endScene).
        renderer.endFrame();
    }

    return 0;
}
