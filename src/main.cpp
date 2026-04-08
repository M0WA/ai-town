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
#include "src/platform/WallClock.h"
#include "src/platform/PlatformUtils.h"
#include "src/interfaces/camera_state.h"
#include "src/simulation/CitySimulation.h"
#include "src/simulation/simulation_constants.h"  // SimulationConstants::startingFundsForDifficulty
#include "src/simulation/StdSimulationRNG.h"
#include "src/simulation/SaveSystem.h"
#include "src/terrain/TerrainSystem.h"
#include "src/terrain/StdTerrainRNG.h"
#include "src/audio/AudioSystem.h"
#include "src/interfaces/sound_ids.h"   // MUSIC_MAIN_MENU_01 — Phase 10 startup music

#include <irrlicht.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <exception>
#include <string>
#include <unordered_map>
#include <unordered_set>

int main(int argc, char** argv) {
    // Resolve assets directory once at startup (exe-relative on Windows, compiled-in on Linux).
    setAssetsDir(resolveAssetsDir());

    // --frames N : auto-exit after N frames (used for profiling / benchmarking)
    int maxFrames = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            maxFrames = std::atoi(argv[i + 1]);
            ++i;
        }
    }
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
            device->getLogger()->log(
                "[main] WARNING: unexpected animators on addCameraSceneNode() result — "
                "removing animator(s)",
                irr::ELL_WARNING);
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
    renderer.setRenderSystem(&renderSystem);

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
    AudioSystem audioSystem(device->getLogger(), &wallClock);

    // -------------------------------------------------------------------------
    // Phase 6: Simulation engine.
    // StdSimulationRNG — production mt19937-backed ISimulationRNG.
    // TerrainSystem — ITerrainQuery implementation (provides slope data for earthworks cost).
    //   MUST NOT pass nullptr for terrain — earthworks cost silently returns 0 for all tiles.
    // Difficulty::Normal is the production default; Phase 8 wires this to the New Game flow.
    // -------------------------------------------------------------------------
    StdSimulationRNG simRng;
    TerrainSystem terrainSystem(&renderer, &wallClock);

    CitySimulation citySimulation(
        &renderer, /*audio=*/&audioSystem, &simRng, &wallClock, &terrainSystem, Difficulty::Normal);

    // -------------------------------------------------------------------------
    // UIManager — Phase 8 full implementation.
    // Now wired with real audio, simulation, and clock pointers.
    // Panels (HUD, NotificationManager, etc.) receive these pointers at construction.
    // UIManager is constructed BEFORE terrain generation so that getPendingMapTiles()
    // (which reflects the player's Map Size selection in the New Game screen) is
    // available when calling terrainSystem.generate().
    // -------------------------------------------------------------------------
    UIManager uiManager(&uiBackend, &audioSystem, &citySimulation, &wallClock);

    // Late-bind UIManager to renderer (breaks circular construction dependency).
    renderer.setUIManager(&uiManager);
    // Phase 11p: late-bind CameraController for minimap viewport and click-to-pan.
    uiManager.setCameraController(&cameraController);
    // Phase 11l: supply Irrlicht logger so KeyBindings::load() routes warnings through ILogger.
    uiManager.setLogger(device->getLogger());

    // Terrain is generated on-demand when the player starts or loads a game
    // (consumeNewGameRequest / consumeLoadGameRequest polling blocks below).
    // No startup terrain generation — the main menu shows without terrain.

    // -------------------------------------------------------------------------
    // Phase 9b: wire renderer terrain query with defaults (dims 0x0 at startup).
    // Actual terrain is generated when the player starts or loads a game.
    // Step 2:  renderer.setTerrainQuery(&terrainSystem)
    // Step 2a: renderer.setCellSize(terrainSystem.getCellSize())
    // Step 2b: renderer.setRendererMapDimensions(...) — 0x0 until terrain generated
    // -------------------------------------------------------------------------
    renderer.setTerrainQuery(&terrainSystem);
    renderer.setCellSize(terrainSystem.getCellSize());
    renderer.setRendererMapDimensions(terrainSystem.getMapTilesX(), terrainSystem.getMapTilesZ());

    // Camera target is set to origin at startup (no terrain yet).
    // Repositioned to terrain center after each new-game or load-game.
    {
        const float halfWorld = terrainSystem.getMapTilesX()
                                * terrainSystem.getCellSize() * 0.5f;
        cameraController.setTarget(halfWorld, halfWorld);
    }

    // -------------------------------------------------------------------------
    // Phase 9b: wire UIManager terrain/renderer/map-dimensions AFTER UIManager
    // is constructed (and after terrain generation completes).
    // Step 3: uiManager.setRenderer(&renderer)
    // Step 4: uiManager.setTerrainQuery(&terrainSystem)
    // Step 5: uiManager.setMapDimensions(...)
    // -------------------------------------------------------------------------
    uiManager.setRenderer(&renderer);
    // UIManager transitioned to main menu during construction (before setRenderer was
    // wired), so arm the background here now that the renderer is available.
    renderer.setSceneBackground(getAssetsDir() + "/textures/ui/loading_screen.png");
    uiManager.setTerrainQuery(&terrainSystem);
    uiManager.setMapDimensions(terrainSystem.getMapTilesX(), terrainSystem.getMapTilesZ());

    // Phase 11m: supply map dimensions to CitySimulation for border-ring terrain flattening.
    citySimulation.setMapDimensions(terrainSystem.getMapTilesX(), terrainSystem.getMapTilesZ());

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
    // Phase 11: keybindings — load from platform config path at startup.
    // Silently uses defaults if keybindings.json is absent (normal first-run state).
    // -------------------------------------------------------------------------
    uiManager.loadKeybindings();

    // -------------------------------------------------------------------------
    // Phase 11: SaveSystem — auto-save and manual slot management.
    // Constructed with wallClock for deterministic 120 s auto-save gate.
    // setSimulation() binds the CitySimulation to the SaveSystem before any
    // save/load method can be called.
    // -------------------------------------------------------------------------
    SaveSystem saveSystem(&wallClock);
    saveSystem.setSimulation(&citySimulation);

    uiManager.setSaveSystem(&saveSystem);

    // Show loading screen for one frame while save file state is checked.
    renderer.beginFrame();
    renderer.drawFullscreenTexture(getAssetsDir() + "/textures/ui/loading_screen.png");
    renderer.endFrame();

    // Update Load Game button state after loading screen completes (deferred from startup
    // to avoid blocking before the loading screen is visible).
    // Three states per architecture/ui-ux/main-menu-new-game-flow.md:
    //   Valid      — button enabled; click → loading screen
    //   AllCorrupt — button grayed; tooltip shows save directory path for recovery
    //   NoSaves    — button grayed; tooltip "No saves found"
    {
        SaveFileState saveState = saveSystem.getSaveFileState();
        uiManager.setSaveAvailable(saveState == SaveFileState::Valid);
        switch (saveState) {
            case SaveFileState::NoSaves:
                uiManager.setSaveStatusText("No saves found.");
                break;
            case SaveFileState::AllCorrupt:
                uiManager.setSaveStatusText(
                    "Save data is corrupted — cannot load. Check "
                    + saveSystem.getSaveDirectoryPath() + " for recovery.");
                break;
            case SaveFileState::Valid:
                uiManager.setSaveStatusText("");  // hide label when saves are available
                break;
        }
    }

    // -------------------------------------------------------------------------
    // Phase 11: Notify UIManager that all startup wiring is complete.
    // Seeds m_previousCityRating and m_lastDeficitMonths caches from the loaded
    // simulation state so the first update() tick does not fire spurious stingers
    // or deficit warnings.  Must be called before the first UIManager::update().
    // -------------------------------------------------------------------------
    uiManager.onGameLoaded();

    // -------------------------------------------------------------------------
    // EventReceiver — translates SEvent → InputEvent, dispatches per input-arbitration.md.
    // -------------------------------------------------------------------------
    // Pass &uiBackend so hover events are forwarded to IrrlichtUIBackend::handleGuiHoverEvent()
    // for Glass City Colour Pass button sprite swapping (Phase 10c).
    EventReceiver eventReceiver(&uiScaler, &uiManager, &cameraController, &uiBackend);
    device->setEventReceiver(&eventReceiver);

    // =========================================================================
    // Phase 11d Deliverable 3a: per-frame vehicle agent sync state.
    // Persists across frames: maps AgentHandle → acquired audio source pair.
    // Released on despawn; capped to 150 m distance cull in the sync loop below.
    // =========================================================================
    struct AgentAudioState {
        int      idleIdx{-1};
        int      moveIdx{-1};
        ZoneType zone{ZoneType::Residential};
    };
    std::unordered_map<AgentHandle, AgentAudioState> activeAgents;

    // =========================================================================
    // 8-STEP FRAME LOOP
    // =========================================================================
    int frameCount = 0;
    while (device->run()) {
        if (maxFrames > 0 && frameCount >= maxFrames) {
            device->closeDevice();
            break;
        }
        ++frameCount;

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
        try {
            citySimulation.tick(realDeltaSeconds);
        } catch (const std::exception& e) {
            device->getLogger()->log(e.what(), irr::ELL_ERROR);
        }

        // Phase 11d Deliverable 3a: per-frame vehicle agent sync loop.
        // Runs AFTER CitySimulation::tick() so getAgentPositions() reflects the
        // current tick, and BEFORE drawScene() so renderer nodes are updated.
        // Distance cull: agents beyond 150 m from the camera are not rendered.
        // kAgentCullDistSq = 150*150 = 22500 m² (avoids sqrt per agent).
        {
            constexpr float kTileSizeM       = 10.0f;   // metres per tile (V1 map)
            constexpr float kAgentCullDistSq = 22500.0f; // 150 m radius squared

            CameraState camState = cameraController.getCameraState();
            const float camX = camState.position.x;
            const float camZ = camState.position.z;

            const std::vector<AgentState> agentList = citySimulation.getAgentPositions();

            // Build set of handles currently alive in the simulation.
            std::unordered_set<AgentHandle> liveHandles;
            liveHandles.reserve(agentList.size());
            for (const AgentState& a : agentList) {
                liveHandles.insert(static_cast<AgentHandle>(a.agentId));
            }

            // Despawn agents that are no longer alive in the simulation.
            {
                std::vector<AgentHandle> toRemove;
                for (const auto& kv : activeAgents) {
                    if (liveHandles.find(kv.first) == liveHandles.end()) {
                        toRemove.push_back(kv.first);
                    }
                }
                for (AgentHandle h : toRemove) {
                    const AgentAudioState& aud = activeAgents.at(h);
                    audioSystem.releaseVehicleEnginePair(aud.idleIdx, aud.moveIdx);
                    renderer.despawnVehicleAgent(h);
                    activeAgents.erase(h);
                }
            }

            // Spawn / move agents that are alive.
            for (const AgentState& a : agentList) {
                const AgentHandle handle = static_cast<AgentHandle>(a.agentId);

                // Distance cull: skip agents beyond 150 m.
                const float wx = static_cast<float>(a.tileX) * kTileSizeM;
                const float wz = static_cast<float>(a.tileZ) * kTileSizeM;
                const float dx = wx - camX;
                const float dz = wz - camZ;
                const float distSq = dx * dx + dz * dz;
                if (distSq > kAgentCullDistSq) {
                    // If the agent was previously visible, despawn it now.
                    auto it = activeAgents.find(handle);
                    if (it != activeAgents.end()) {
                        audioSystem.releaseVehicleEnginePair(it->second.idleIdx, it->second.moveIdx);
                        renderer.despawnVehicleAgent(handle);
                        activeAgents.erase(it);
                    }
                    continue;
                }

                auto it = activeAgents.find(handle);
                if (it != activeAgents.end() && it->second.zone != a.zone) {
                    // Zone changed: despawn+respawn with correct mesh, re-acquire audio.
                    auto& s = it->second;
                    audioSystem.releaseVehicleEnginePair(s.idleIdx, s.moveIdx);
                    renderer.despawnVehicleAgent(handle);
                    renderer.spawnVehicleAgent(handle, a.tileX, a.tileZ, a.zone);
                    const auto newAud = audioSystem.acquireVehicleEnginePair(a.zone);
                    activeAgents[handle].idleIdx = newAud.first;
                    activeAgents[handle].moveIdx = newAud.second;
                    activeAgents[handle].zone    = a.zone;
                }
                if (it == activeAgents.end()) {
                    // New agent: spawn renderer node and acquire audio pair.
                    renderer.spawnVehicleAgent(handle, a.tileX, a.tileZ, a.zone);
                    std::pair<int,int> audioPair{-1, -1};
                    try {
                        audioPair = audioSystem.acquireVehicleEnginePair(a.zone);
                    } catch (const std::exception& e) {
                        device->getLogger()->log(e.what(), irr::ELL_ERROR);
                    }
                    AgentAudioState aud;
                    aud.idleIdx = audioPair.first;
                    aud.moveIdx = audioPair.second;
                    aud.zone    = a.zone;
                    activeAgents[handle] = aud;
                    it = activeAgents.find(handle);
                }

                // Move existing agent — use sub-tile-interpolated world position when available.
                const float agentWx = (a.worldX != 0.0f || a.worldZ != 0.0f)
                    ? a.worldX
                    : (static_cast<float>(a.tileX) + 0.5f) * kTileSizeM;
                const float agentWz = (a.worldX != 0.0f || a.worldZ != 0.0f)
                    ? a.worldZ
                    : (static_cast<float>(a.tileZ) + 0.5f) * kTileSizeM;
                renderer.moveVehicleAgent(handle, agentWx, agentWz, a.headingDeg);

                // Update vehicle audio (speed fraction derived from agent road data).
                // Use speedFraction = 1.0 (free-flow) as default; traffic signal state
                // modulation is handled by CitySimulation::getRoadSegmentSpeeds() queries
                // from the minimap overlay — agent sync uses a simple motion heuristic.
                const float speedFraction = 1.0f;
                if (it->second.idleIdx >= 0) {
                    // Use the same sub-tile-interpolated position as the renderer
                    // (agentWx/agentWz) so audio remains spatially synchronised with
                    // the visual vehicle position, including the lane offset applied by
                    // IrrlichtRenderer::moveVehicleAgent() (Phase 11h §4e).
                    audioSystem.updateVehicleAudio(
                        it->second.idleIdx, it->second.moveIdx,
                        speedFraction, agentWx, agentWz);
                }
            }
        }

        // Step 3: CameraController::update(dt).
        // OAL-2 ordering rule: CameraController::update() MUST execute BEFORE
        // AudioSystem::syncListenerToCamera() so the listener reads the updated position.
        cameraController.update(realDeltaSeconds);

        // Step 3a: TerrainSystem::update(dt) — process LOD rebuild deque (at most 2 per frame).
        // Runs after camera update so LOD decisions use the current camera position.
        terrainSystem.update(realDeltaSeconds);

        // Step 3b: UIManager::update(realDeltaSeconds) — per-frame UI state update.
        // MUST execute BEFORE beginFrame() per architecture/ui-ux/ui-manager.md.
        try {
            uiManager.update(realDeltaSeconds);
        } catch (const std::exception& e) {
            // AL error thrown by alCheckError_real inside a sim/audio call
            // (e.g. playPositionalSound on SFX_EARTHWORKS after device loss).
            // Log and continue — audio is already degraded.
            device->getLogger()->log(e.what(), irr::ELL_ERROR);
        }

        // Step 3c: SaveSystem::update(realDeltaSeconds) — advance auto-save timer.
        // MUST execute after UIManager::update() so that the save-requested flag
        // set by UIManager (from PauseMenuPanel) is consumed before the timer check.
        saveSystem.update(realDeltaSeconds);

        // Phase 11m: new-game polling block.
        // Polled after UIManager::update() so that consumeNewGameRequest() sees the
        // m_newGamePending flag set by the current frame's consumeStartGameRequest().
        if (uiManager.consumeNewGameRequest()) {
            NewGameParams ngp = uiManager.getNewGameParams();
            int64_t startingFunds = SimulationConstants::startingFundsForDifficulty(
                static_cast<Difficulty>(ngp.difficulty));
            citySimulation.reset(startingFunds);
            renderer.clearCity();
            StdTerrainRNG freshRng;
            freshRng.reseed(ngp.seed);
            terrainSystem.generate(
                static_cast<int>(ngp.mapSize), static_cast<int>(ngp.mapSize),
                10.0f, &freshRng);
            terrainSystem.enqueueAllChunks();
            // Loading loop identical to startup loading loop.
            double loadPrev2 = wallClock.nowSeconds();
            while (device->run() && terrainSystem.pendingRebuildCount() > 0) {
                const double loadNow2  = wallClock.nowSeconds();
                const float  loadDt2   = static_cast<float>(loadNow2 - loadPrev2);
                loadPrev2 = loadNow2;
                uiManager.update(loadDt2);
                terrainSystem.update(loadDt2);
                terrainSystem.flushPendingRebuilds();
                CameraState loadCam = cameraController.getCameraState();
                try {
                    audioSystem.syncListenerToCamera(loadCam);
                    audioSystem.update(loadDt2);
                } catch (const std::exception& e) {
                    device->getLogger()->log(e.what(), irr::ELL_ERROR);
                }
                renderer.beginFrame();
                renderer.drawFullscreenTexture(getAssetsDir() + "/textures/ui/loading_screen.png");
                renderer.endFrame();
            }
            // Update renderer and UI with new terrain dimensions.
            renderer.setRendererMapDimensions(terrainSystem.getMapTilesX(), terrainSystem.getMapTilesZ());
            renderer.setCellSize(terrainSystem.getCellSize());
            // Reposition camera to center of new terrain and reset orbit.
            {
                const float halfWorld = terrainSystem.getMapTilesX()
                                        * terrainSystem.getCellSize() * 0.5f;
                cameraController.setTarget(halfWorld, halfWorld);
                cameraController.resetOrbit();
            }
            uiManager.setMapDimensions(
                static_cast<int>(ngp.mapSize), static_cast<int>(ngp.mapSize));
            citySimulation.setMapDimensions(
                static_cast<int>(ngp.mapSize), static_cast<int>(ngp.mapSize));
            uiManager.transitionToGameplay(GameMode::Sandbox);
            uiManager.onGameLoaded();
            continue;
        }

        // Phase 11m: load-game polling block.
        // Polled after UIManager::update() so that consumeLoadGameRequest() sees the
        // m_loadGamePending flag set by the current frame's consumeLoadGameRequest() handler.
        {
            std::string loadJson;
            if (uiManager.consumeLoadGameRequest(loadJson)) {
                // Apply loaded JSON to sim — restores tile data and map dimensions.
                citySimulation.applyLoadedJson(loadJson);
                int loadedTilesX = citySimulation.getMapTilesX();
                int loadedTilesZ = citySimulation.getMapTilesZ();
                // Guard: if dims are 0 (old save without map_tiles), fall back to current terrain.
                if (loadedTilesX <= 0 || loadedTilesZ <= 0) {
                    loadedTilesX = terrainSystem.getMapTilesX();
                    loadedTilesZ = terrainSystem.getMapTilesZ();
                }
                // Regenerate terrain for the loaded map size (seed 0 = V1 fixed seed).
                renderer.clearCity();
                StdTerrainRNG loadRng;
                loadRng.reseed(0);
                terrainSystem.generate(loadedTilesX, loadedTilesZ, 10.0f, &loadRng);
                terrainSystem.enqueueAllChunks();
                // Loading loop identical to new-game loading loop.
                double loadPrevL = wallClock.nowSeconds();
                while (device->run() && terrainSystem.pendingRebuildCount() > 0) {
                    const double loadNowL  = wallClock.nowSeconds();
                    const float  loadDtL   = static_cast<float>(loadNowL - loadPrevL);
                    loadPrevL = loadNowL;
                    uiManager.update(loadDtL);
                    terrainSystem.update(loadDtL);
                    terrainSystem.flushPendingRebuilds();
                    CameraState loadCamL = cameraController.getCameraState();
                    try {
                        audioSystem.syncListenerToCamera(loadCamL);
                        audioSystem.update(loadDtL);
                    } catch (const std::exception& e) {
                        device->getLogger()->log(e.what(), irr::ELL_ERROR);
                    }
                    renderer.beginFrame();
                    renderer.drawFullscreenTexture(getAssetsDir() + "/textures/ui/loading_screen.png");
                    renderer.endFrame();
                }
                // Wire dimensions and reposition camera.
                renderer.setRendererMapDimensions(loadedTilesX, loadedTilesZ);
                renderer.setCellSize(terrainSystem.getCellSize());
                {
                    const float halfWorld = static_cast<float>(loadedTilesX)
                                            * terrainSystem.getCellSize() * 0.5f;
                    cameraController.setTarget(halfWorld, halfWorld);
                    cameraController.resetOrbit();
                }
                uiManager.setMapDimensions(loadedTilesX, loadedTilesZ);
                citySimulation.setMapDimensions(loadedTilesX, loadedTilesZ);
                // Rebuild city scene nodes from loaded sim state.
                uiManager.rebuildCityFromSim();
                uiManager.transitionToGameplay(GameMode::Sandbox);
                uiManager.onGameLoaded();
                continue;
            }
        }

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
            device->getLogger()->log(e.what(), irr::ELL_ERROR);
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
