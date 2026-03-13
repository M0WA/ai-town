// src/main.cpp — AI Town application entry point.
// Implements the 8-step frame loop per architecture/graphics-architecture/irrlicht-device-lifecycle.md.
//
// 8-step frame sequence:
//   1. Poll events (device->run() + SEvent dispatch via EventReceiver)
//   2. CitySimulation::tick(realDeltaSeconds) [Phase 6]
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
#include "src/simulation/CitySimulation.h"
#include "src/simulation/StdSimulationRNG.h"
#include "src/terrain/TerrainSystem.h"
#include "src/terrain/StdTerrainRNG.h"
#include "src/audio/AudioSystem.h"
#include "src/interfaces/sound_ids.h"   // MUSIC_MAIN_MENU_01 — Phase 10 startup music

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
    // Created with nullptr for UIManager — late-bound below after UIManager is created.
    // -------------------------------------------------------------------------
    IrrlichtRenderer renderer(device, /*uiManager=*/nullptr);

    // -------------------------------------------------------------------------
    // WallClock — production IClock; injects into AudioSystem/CitySimulation at Phase 4/6.
    // Raw delta MUST NEVER be pre-multiplied by speed multiplier here.
    // CitySimulation owns all speed-scaling internally (architecture/game-design/simulation-time.md).
    // -------------------------------------------------------------------------
    WallClock wallClock;
    double prevTime = wallClock.nowSeconds();

    // -------------------------------------------------------------------------
    // Phase 7: AudioSystem — full OpenAL Soft implementation.
    // Injected with wallClock for deterministic timing (crossfade duck timer, etc.).
    // Constructor signature (IClock*) and all IAudioSystem method signatures are frozen.
    // CitySimulation receives &audioSystem for SFX playback during simulation ticks.
    // -------------------------------------------------------------------------
    AudioSystem audioSystem(&wallClock);

    // -------------------------------------------------------------------------
    // Phase 6: Simulation engine.
    // StdSimulationRNG — production mt19937-backed ISimulationRNG.
    // TerrainSystem — ITerrainQuery implementation (provides slope data for earthworks cost).
    //   MUST NOT pass nullptr for terrain — earthworks cost silently returns 0 for all tiles.
    // Difficulty::Normal is the production default; Phase 8 wires this to the New Game flow.
    // -------------------------------------------------------------------------
    StdSimulationRNG simRng;
    TerrainSystem terrainSystem(&renderer, &wallClock);

    // -------------------------------------------------------------------------
    // Terrain generation — generate the procedural heightmap and build all chunks.
    // Uses 128x128 tiles (4x4 = 16 chunks at 32 tiles/chunk), cellSize = 10 m.
    // StdTerrainRNG provides mt19937-backed randomness with reseed() support.
    // generate() enforces playability constraints (20% flat, 50x50 contiguous region).
    // buildAllChunks() synchronously creates all scene nodes via IRenderer.
    // -------------------------------------------------------------------------
    StdTerrainRNG terrainRng;
    terrainSystem.generate(128, 128, 10.0f, &terrainRng);
    terrainSystem.buildAllChunks();

    // -------------------------------------------------------------------------
    // Phase 9b: wire renderer terrain query AFTER terrain generation.
    // Step 2:  renderer.setTerrainQuery(&terrainSystem)
    // Step 2a: renderer.setCellSize(terrainSystem.getCellSize())
    // Step 2b: renderer.setRendererMapDimensions(...)
    // -------------------------------------------------------------------------
    renderer.setTerrainQuery(&terrainSystem);
    renderer.setCellSize(terrainSystem.getCellSize());
    renderer.setRendererMapDimensions(terrainSystem.getMapTilesX(), terrainSystem.getMapTilesZ());

    // Position camera over the terrain center (128 tiles × 10 m / 2 = 640 m per axis).
    cameraController.setTarget(640.0f, 640.0f);

    CitySimulation citySimulation(
        &renderer, /*audio=*/&audioSystem, &simRng, &wallClock, &terrainSystem, Difficulty::Normal);

    // -------------------------------------------------------------------------
    // UIManager — Phase 8 full implementation.
    // Now wired with real audio, simulation, and clock pointers.
    // Panels (HUD, NotificationManager, etc.) receive these pointers at construction.
    // -------------------------------------------------------------------------
    UIManager uiManager(&uiBackend, &audioSystem, &citySimulation, &wallClock);

    // Late-bind UIManager to renderer (breaks circular construction dependency).
    renderer.setUIManager(&uiManager);

    // -------------------------------------------------------------------------
    // Phase 9b: wire UIManager terrain/renderer/map-dimensions AFTER UIManager
    // is constructed (and after terrain generation completes).
    // Step 3: uiManager.setRenderer(&renderer)
    // Step 4: uiManager.setTerrainQuery(&terrainSystem)
    // Step 5: uiManager.setMapDimensions(...)
    // -------------------------------------------------------------------------
    uiManager.setRenderer(&renderer);
    uiManager.setTerrainQuery(&terrainSystem);
    uiManager.setMapDimensions(terrainSystem.getMapTilesX(), terrainSystem.getMapTilesZ());

    // -------------------------------------------------------------------------
    // Phase 10: start main menu music now that the AudioSystem is ready and all
    // wiring is complete.  setMusicTrack() queues MUSIC_MAIN_MENU_01 for streaming
    // via the audio thread; no crossfade is needed because no music is playing yet.
    // This is the only place in the codebase that initiates main menu music at
    // application startup. UIManager::transitionToMainMenu() uses the same call
    // when returning from gameplay.
    // -------------------------------------------------------------------------
    audioSystem.setMusicTrack(MUSIC_MAIN_MENU_01);

    // -------------------------------------------------------------------------
    // EventReceiver — translates SEvent → InputEvent, dispatches per input-arbitration.md.
    // -------------------------------------------------------------------------
    EventReceiver eventReceiver(&uiScaler, &uiManager, &cameraController);
    device->setEventReceiver(&eventReceiver);

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

        // Update UIScaler viewport dimensions each frame so that mouse coordinate
        // unprojection tracks the current window size after a resize.
        uiScaler.setViewportSize(uiBackend.getScreenWidth(), uiBackend.getScreenHeight());

        // Reposition Irrlicht GUI elements when the window is resized so their
        // physical pixel positions match the virtual 1920x1080 layout.
        uiBackend.handleViewportResize();

        // Step 2: CitySimulation::tick(realDeltaSeconds) — Phase 6 wired.
        // DEFAULT SPEED CONTRACT: CitySimulation is constructed at SpeedMultiplier::x3
        // (kDefaultSimSpeed) — see architecture/game-design/simulation-time.md.
        // FRAME-LOOP POSITION CONSTRAINT: This tick call MUST remain at step 2 in the
        // 8-step sequence (before camera, UI, audio, and rendering steps).
        citySimulation.tick(realDeltaSeconds);

        // Step 3: CameraController::update(dt).
        // OAL-2 ordering rule: CameraController::update() MUST execute BEFORE
        // AudioSystem::syncListenerToCamera() so the listener reads the updated position.
        cameraController.update(realDeltaSeconds);

        // Step 3a: TerrainSystem::update(dt) — process LOD rebuild deque (at most 2 per frame).
        // Runs after camera update so LOD decisions use the current camera position.
        terrainSystem.update(realDeltaSeconds);

        // Step 3b: UIManager::update(realDeltaSeconds) — per-frame UI state update.
        // MUST execute BEFORE beginFrame() per architecture/ui-ux/ui-manager.md.
        uiManager.update(realDeltaSeconds);

        // Check for application quit request (Main Menu Quit / Pause Menu Quit to Desktop).
        if (uiManager.isQuitRequested()) {
            device->closeDevice();
            continue;  // Skip rendering; device->run() returns false next iteration.
        }

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
        CameraState cameraState = cameraController.getCameraState();
        try {
            audioSystem.syncListenerToCamera(cameraState);
            audioSystem.update(realDeltaSeconds);
        } catch (const std::exception& e) {
            // AL backend failure (e.g. broken pipe) — audio degraded to silent.
            // The m_deviceLost flag in AudioSystem will suppress further AL calls.
            fprintf(stderr, "[main] Audio error (audio disabled): %s\n", e.what());
        }

        // Phase 10b: update renderer (cloud plane UV scrolling).
        // Runs before beginFrame() so the texture matrix is updated before drawAll().
        renderer.update(realDeltaSeconds);

        // Step 5: beginFrame (driver->beginScene).
        renderer.beginFrame();

        // Step 6: drawScene (sceneManager->drawAll() + uiManager->draw()).
        renderer.drawScene();

        // Step 7: endFrame (driver->endScene).
        renderer.endFrame();
    }

    return 0;
}
