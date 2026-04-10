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
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

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

// =========================================================================
// AppSystems — holds all sub-systems constructed during initSystems().
// Defined at file scope so both initSystems() and runFrameLoop() can use it.
// =========================================================================
struct AppSystems {
    RenderSystem      renderSystem;
    std::unique_ptr<IrrlichtUIBackend> uiBackend;
    std::unique_ptr<UIScaler>          uiScaler;
    irr::scene::ICameraSceneNode* cameraNode{nullptr};
    std::unique_ptr<CameraController>  cameraController;
    std::unique_ptr<IrrlichtRenderer>  renderer;
    WallClock          wallClock;
    std::unique_ptr<AudioSystem>       audioSystem;
    StdSimulationRNG   simRng;
    std::unique_ptr<TerrainSystem>     terrainSystem;
    std::unique_ptr<CitySimulation>    citySimulation;
    std::unique_ptr<UIManager>         uiManager;
    std::unique_ptr<SaveSystem>        saveSystem;
    std::unique_ptr<EventReceiver>     eventReceiver;

    double prevTime{0.0};
    int    maxFrames{0};
    std::unordered_map<AgentHandle, AgentAudioState> activeAgents;
};

// =========================================================================
// initSystems — construct and wire all sub-systems; returns false on fatal error.
// Phase 11q3: extracted from main() to reduce cognitive complexity.
// =========================================================================
static bool initSystems(AppSystems& sys, int argc, char** argv) {
    // Resolve assets directory once at startup (exe-relative on Windows, compiled-in on Linux).
    setAssetsDir(resolveAssetsDir());

    // --frames N : auto-exit after N frames (used for profiling / benchmarking)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            sys.maxFrames = std::atoi(argv[i + 1]);
            ++i;
        }
    }

    // -------------------------------------------------------------------------
    // Phase 1: Create the Irrlicht device via RenderSystem (RAII owner).
    // -------------------------------------------------------------------------
    irr::IrrlichtDevice* device = sys.renderSystem.getDevice();

    if (!device) {
        fprintf(stderr, "[main] FATAL: RenderSystem failed to create a valid device.\n");
        return false;
    }

    device->setWindowCaption(L"AI Town");

    // -------------------------------------------------------------------------
    // IrrlichtUIBackend — Phase 1 compile stub (17-method stubs; Phase 8 full impl).
    // -------------------------------------------------------------------------
    sys.uiBackend = std::make_unique<IrrlichtUIBackend>(device);

    // -------------------------------------------------------------------------
    // UIScaler — virtual 1920x1080 coordinate space.
    // -------------------------------------------------------------------------
    sys.uiScaler = std::make_unique<UIScaler>(
        /*virtualW=*/  1920,
        /*virtualH=*/  1080,
        /*viewportW=*/ sys.uiBackend->getScreenWidth(),
        /*viewportH=*/ sys.uiBackend->getScreenHeight(),
        /*offsetX=*/   0,
        /*offsetY=*/   0
    );

    // -------------------------------------------------------------------------
    // Camera scene node — addCameraSceneNode() only (never FPS/Maya variants).
    // Post-creation: grab/drop-guarded animator removal loop per scene-graph-ownership.md.
    // -------------------------------------------------------------------------
    irr::scene::ISceneManager* smgr = device->getSceneManager();
    sys.cameraNode = smgr->addCameraSceneNode();

    if (sys.cameraNode) {
#ifndef NDEBUG
        if (sys.cameraNode->getAnimators().size() > 0) {
            device->getLogger()->log(
                "[main] WARNING: unexpected animators on addCameraSceneNode() result — "
                "removing animator(s)",
                irr::ELL_WARNING);
        }
#endif
        while (sys.cameraNode->getAnimators().size() > 0) {
            irr::scene::ISceneNodeAnimator* anim = *sys.cameraNode->getAnimators().begin();
            anim->grab();
            sys.cameraNode->removeAnimator(anim);
            anim->drop();
        }
    }

    // -------------------------------------------------------------------------
    // CameraController — windowed default: startInFullscreen=false (edge-scroll OFF).
    // -------------------------------------------------------------------------
    sys.cameraController = std::make_unique<CameraController>(sys.cameraNode, /*startInFullscreen=*/false);

    // -------------------------------------------------------------------------
    // IrrlichtRenderer — owns the rendering interface.
    // -------------------------------------------------------------------------
    sys.renderer = std::make_unique<IrrlichtRenderer>(device, /*uiManager=*/nullptr);
    sys.renderer->setRenderSystem(&sys.renderSystem);

    // -------------------------------------------------------------------------
    // WallClock — production IClock.
    // -------------------------------------------------------------------------
    sys.prevTime = sys.wallClock.nowSeconds();

    // -------------------------------------------------------------------------
    // Phase 7: AudioSystem — full OpenAL Soft implementation.
    // -------------------------------------------------------------------------
    sys.audioSystem = std::make_unique<AudioSystem>(device->getLogger(), &sys.wallClock);

    // -------------------------------------------------------------------------
    // Phase 6: Simulation engine.
    // -------------------------------------------------------------------------
    sys.terrainSystem = std::make_unique<TerrainSystem>(sys.renderer.get(), &sys.wallClock);

    sys.citySimulation = std::make_unique<CitySimulation>(
        sys.renderer.get(), /*audio=*/sys.audioSystem.get(), &sys.simRng, &sys.wallClock,
        sys.terrainSystem.get(), Difficulty::Normal);

    // -------------------------------------------------------------------------
    // UIManager — Phase 8 full implementation.
    // -------------------------------------------------------------------------
    sys.uiManager = std::make_unique<UIManager>(sys.uiBackend.get(), sys.audioSystem.get(),
                                                 sys.citySimulation.get(), &sys.wallClock);

    // Late-bind UIManager to renderer (breaks circular construction dependency).
    sys.renderer->setUIManager(sys.uiManager.get());
    sys.uiManager->setCameraController(sys.cameraController.get());
    sys.uiManager->setLogger(device->getLogger());

    // -------------------------------------------------------------------------
    // Phase 9b: wire renderer terrain query with defaults (dims 0x0 at startup).
    // -------------------------------------------------------------------------
    sys.renderer->setTerrainQuery(sys.terrainSystem.get());
    sys.renderer->setCellSize(sys.terrainSystem->getCellSize());
    sys.renderer->setRendererMapDimensions(sys.terrainSystem->getMapTilesX(),
                                           sys.terrainSystem->getMapTilesZ());

    {
        const float halfWorld = sys.terrainSystem->getMapTilesX()
                                * sys.terrainSystem->getCellSize() * 0.5f;
        sys.cameraController->setTarget(halfWorld, halfWorld);
    }

    // -------------------------------------------------------------------------
    // Phase 9b: wire UIManager terrain/renderer/map-dimensions.
    // -------------------------------------------------------------------------
    sys.uiManager->setRenderer(sys.renderer.get());
    sys.renderer->setSceneBackground(getAssetsDir() + "/textures/ui/loading_screen.png");
    sys.uiManager->setTerrainQuery(sys.terrainSystem.get());
    sys.uiManager->setMapDimensions(sys.terrainSystem->getMapTilesX(),
                                     sys.terrainSystem->getMapTilesZ());

    sys.citySimulation->setMapDimensions(sys.terrainSystem->getMapTilesX(),
                                          sys.terrainSystem->getMapTilesZ());

    // -------------------------------------------------------------------------
    // Phase 10: start main menu music.
    // -------------------------------------------------------------------------
    sys.audioSystem->setMusicTrack(MUSIC_MAIN_MENU_01);

    // -------------------------------------------------------------------------
    // Phase 11: keybindings — load from platform config path at startup.
    // -------------------------------------------------------------------------
    sys.uiManager->loadKeybindings();

    // -------------------------------------------------------------------------
    // Phase 11: SaveSystem — auto-save and manual slot management.
    // -------------------------------------------------------------------------
    sys.saveSystem = std::make_unique<SaveSystem>(&sys.wallClock);
    sys.saveSystem->setSimulation(sys.citySimulation.get());
    sys.uiManager->setSaveSystem(sys.saveSystem.get());

    // Show loading screen for one frame while save file state is checked.
    sys.renderer->beginFrame();
    sys.renderer->drawFullscreenTexture(getAssetsDir() + "/textures/ui/loading_screen.png");
    sys.renderer->endFrame();

    // Update Load Game button state.
    {
        SaveFileState saveState = sys.saveSystem->getSaveFileState();
        sys.uiManager->setSaveAvailable(saveState == SaveFileState::Valid);
        switch (saveState) {
            case SaveFileState::NoSaves:
                sys.uiManager->setSaveStatusText("No saves found.");
                break;
            case SaveFileState::AllCorrupt:
                sys.uiManager->setSaveStatusText(
                    "Save data is corrupted — cannot load. Check "
                    + sys.saveSystem->getSaveDirectoryPath() + " for recovery.");
                break;
            case SaveFileState::Valid:
                sys.uiManager->setSaveStatusText("");
                break;
        }
    }

    // -------------------------------------------------------------------------
    // Phase 11: Notify UIManager that all startup wiring is complete.
    // -------------------------------------------------------------------------
    sys.uiManager->onGameLoaded();

    // -------------------------------------------------------------------------
    // EventReceiver — translates SEvent → InputEvent.
    // -------------------------------------------------------------------------
    sys.eventReceiver = std::make_unique<EventReceiver>(sys.uiScaler.get(), sys.uiManager.get(),
                                                         sys.cameraController.get(),
                                                         sys.uiBackend.get());
    device->setEventReceiver(sys.eventReceiver.get());

    return true;
}

// =========================================================================
// runFrame — execute one iteration of the 8-step frame loop.
// Phase 11q3: extracted from main() to reduce cognitive complexity.
// Returns false when the device should stop running.
// =========================================================================
static bool runFrame(AppSystems& sys) {
    irr::IrrlichtDevice* device = sys.renderSystem.getDevice();

    // Compute real delta — computed ONCE per frame, BEFORE step 2.
    const double currentTime  = sys.wallClock.nowSeconds();
    const float  realDeltaSeconds = static_cast<float>(currentTime - sys.prevTime);
    sys.prevTime = currentTime;

    // Step 1: Poll events — handled by EventReceiver::OnEvent() via device->run().

    // Update UIScaler viewport dimensions each frame.
    sys.uiScaler->setViewportSize(sys.uiBackend->getScreenWidth(),
                                   sys.uiBackend->getScreenHeight());

    sys.uiBackend->handleViewportResize();

    // Step 2: CitySimulation::tick(realDeltaSeconds).
    try {
        sys.citySimulation->tick(realDeltaSeconds);
    } catch (const std::exception& e) {
        device->getLogger()->log(e.what(), irr::ELL_ERROR);
    }

    // Phase 11d Deliverable 3a: per-frame vehicle agent sync loop.
    {
        constexpr float kTileSizeM       = 10.0f;
        constexpr float kAgentCullDistSq = 22500.0f;

        CameraState camState = sys.cameraController->getCameraState();
        const float camX = camState.position.x;
        const float camZ = camState.position.z;

        const std::vector<AgentState> agentList = sys.citySimulation->getAgentPositions();

        std::unordered_set<AgentHandle> liveHandles;
        liveHandles.reserve(agentList.size());
        for (const AgentState& a : agentList) {
            liveHandles.insert(static_cast<AgentHandle>(a.agentId));
        }

        // Despawn agents that are no longer alive.
        {
            std::vector<AgentHandle> toRemove;
            for (const auto& kv : sys.activeAgents) {
                if (liveHandles.find(kv.first) == liveHandles.end()) {
                    toRemove.push_back(kv.first);
                }
            }
            for (AgentHandle h : toRemove) {
                const AgentAudioState& aud = sys.activeAgents.at(h);
                sys.audioSystem->releaseVehicleEnginePair(aud.idleIdx, aud.moveIdx);
                sys.renderer->despawnVehicleAgent(h);
                sys.activeAgents.erase(h);
            }
        }

        // Spawn / move agents that are alive.
        for (const AgentState& a : agentList) {
            const AgentHandle handle = static_cast<AgentHandle>(a.agentId);

            const float wx = static_cast<float>(a.tileX) * kTileSizeM;
            const float wz = static_cast<float>(a.tileZ) * kTileSizeM;
            const float dx = wx - camX;
            const float dz = wz - camZ;
            const float distSq = dx * dx + dz * dz;
            if (distSq > kAgentCullDistSq) {
                auto it = sys.activeAgents.find(handle);
                if (it != sys.activeAgents.end()) {
                    sys.audioSystem->releaseVehicleEnginePair(it->second.idleIdx, it->second.moveIdx);
                    sys.renderer->despawnVehicleAgent(handle);
                    sys.activeAgents.erase(it);
                }
                continue;
            }

            auto it = sys.activeAgents.find(handle);
            if (it != sys.activeAgents.end() && it->second.zone != a.zone) {
                auto& s = it->second;
                sys.audioSystem->releaseVehicleEnginePair(s.idleIdx, s.moveIdx);
                sys.renderer->despawnVehicleAgent(handle);
                sys.renderer->spawnVehicleAgent(handle, a.tileX, a.tileZ, a.zone);
                const auto newAud = sys.audioSystem->acquireVehicleEnginePair(a.zone);
                sys.activeAgents[handle].idleIdx = newAud.first;
                sys.activeAgents[handle].moveIdx = newAud.second;
                sys.activeAgents[handle].zone    = a.zone;
            }
            if (it == sys.activeAgents.end()) {
                sys.renderer->spawnVehicleAgent(handle, a.tileX, a.tileZ, a.zone);
                std::pair<int,int> audioPair{-1, -1};
                try {
                    audioPair = sys.audioSystem->acquireVehicleEnginePair(a.zone);
                } catch (const std::exception& e) {
                    device->getLogger()->log(e.what(), irr::ELL_ERROR);
                }
                AgentAudioState aud;
                aud.idleIdx = audioPair.first;
                aud.moveIdx = audioPair.second;
                aud.zone    = a.zone;
                sys.activeAgents[handle] = aud;
                it = sys.activeAgents.find(handle);
            }

            const float agentWx = (a.worldX != 0.0f || a.worldZ != 0.0f)
                ? a.worldX
                : (static_cast<float>(a.tileX) + 0.5f) * kTileSizeM;
            const float agentWz = (a.worldX != 0.0f || a.worldZ != 0.0f)
                ? a.worldZ
                : (static_cast<float>(a.tileZ) + 0.5f) * kTileSizeM;
            sys.renderer->moveVehicleAgent(handle, agentWx, agentWz, a.headingDeg);

            const float speedFraction = 1.0f;
            if (it->second.idleIdx >= 0) {
                sys.audioSystem->updateVehicleAudio(
                    it->second.idleIdx, it->second.moveIdx,
                    speedFraction, agentWx, agentWz);
            }
        }
    }

    // Step 3: CameraController::update(dt).
    sys.cameraController->update(realDeltaSeconds);

    // Step 3a: TerrainSystem::update(dt).
    sys.terrainSystem->update(realDeltaSeconds);

    // Step 3b: UIManager::update(realDeltaSeconds).
    try {
        sys.uiManager->update(realDeltaSeconds);
    } catch (const std::exception& e) {
        device->getLogger()->log(e.what(), irr::ELL_ERROR);
    }

    // Step 3c: SaveSystem::update(realDeltaSeconds).
    sys.saveSystem->update(realDeltaSeconds);

    // Phase 11m: new-game polling block.
    if (sys.uiManager->consumeNewGameRequest()) {
        NewGameParams ngp = sys.uiManager->getNewGameParams();
        int64_t startingFunds = SimulationConstants::startingFundsForDifficulty(
            static_cast<Difficulty>(ngp.difficulty));
        sys.citySimulation->reset(startingFunds);
        sys.renderer->clearCity();
        StdTerrainRNG freshRng;
        freshRng.reseed(ngp.seed);
        sys.terrainSystem->generate(
            static_cast<int>(ngp.mapSize), static_cast<int>(ngp.mapSize),
            10.0f, &freshRng);
        sys.terrainSystem->enqueueAllChunks();
        double loadPrev2 = sys.wallClock.nowSeconds();
        while (device->run() && sys.terrainSystem->pendingRebuildCount() > 0) {
            const double loadNow2  = sys.wallClock.nowSeconds();
            const float  loadDt2   = static_cast<float>(loadNow2 - loadPrev2);
            loadPrev2 = loadNow2;
            sys.uiManager->update(loadDt2);
            sys.terrainSystem->update(loadDt2);
            sys.terrainSystem->flushPendingRebuilds();
            CameraState loadCam = sys.cameraController->getCameraState();
            try {
                sys.audioSystem->syncListenerToCamera(loadCam);
                sys.audioSystem->update(loadDt2);
            } catch (const std::exception& e) {
                device->getLogger()->log(e.what(), irr::ELL_ERROR);
            }
            sys.renderer->beginFrame();
            sys.renderer->drawFullscreenTexture(getAssetsDir() + "/textures/ui/loading_screen.png");
            sys.renderer->endFrame();
        }
        sys.renderer->setRendererMapDimensions(sys.terrainSystem->getMapTilesX(),
                                                sys.terrainSystem->getMapTilesZ());
        sys.renderer->setCellSize(sys.terrainSystem->getCellSize());
        {
            const float halfWorld = sys.terrainSystem->getMapTilesX()
                                    * sys.terrainSystem->getCellSize() * 0.5f;
            sys.cameraController->setTarget(halfWorld, halfWorld);
            sys.cameraController->resetOrbit();
        }
        sys.uiManager->setMapDimensions(
            static_cast<int>(ngp.mapSize), static_cast<int>(ngp.mapSize));
        sys.citySimulation->setMapDimensions(
            static_cast<int>(ngp.mapSize), static_cast<int>(ngp.mapSize));
        sys.uiManager->transitionToGameplay(GameMode::Sandbox);
        sys.uiManager->onGameLoaded();
        return true;  // skip rendering this frame
    }

    // Phase 11m: load-game polling block.
    {
        std::string loadJson;
        if (sys.uiManager->consumeLoadGameRequest(loadJson)) {
            sys.citySimulation->applyLoadedJson(loadJson);
            int loadedTilesX = sys.citySimulation->getMapTilesX();
            int loadedTilesZ = sys.citySimulation->getMapTilesZ();
            if (loadedTilesX <= 0 || loadedTilesZ <= 0) {
                loadedTilesX = sys.terrainSystem->getMapTilesX();
                loadedTilesZ = sys.terrainSystem->getMapTilesZ();
            }
            sys.renderer->clearCity();
            StdTerrainRNG loadRng;
            loadRng.reseed(0);
            sys.terrainSystem->generate(loadedTilesX, loadedTilesZ, 10.0f, &loadRng);
            sys.terrainSystem->enqueueAllChunks();
            double loadPrevL = sys.wallClock.nowSeconds();
            while (device->run() && sys.terrainSystem->pendingRebuildCount() > 0) {
                const double loadNowL  = sys.wallClock.nowSeconds();
                const float  loadDtL   = static_cast<float>(loadNowL - loadPrevL);
                loadPrevL = loadNowL;
                sys.uiManager->update(loadDtL);
                sys.terrainSystem->update(loadDtL);
                sys.terrainSystem->flushPendingRebuilds();
                CameraState loadCamL = sys.cameraController->getCameraState();
                try {
                    sys.audioSystem->syncListenerToCamera(loadCamL);
                    sys.audioSystem->update(loadDtL);
                } catch (const std::exception& e) {
                    device->getLogger()->log(e.what(), irr::ELL_ERROR);
                }
                sys.renderer->beginFrame();
                sys.renderer->drawFullscreenTexture(getAssetsDir() + "/textures/ui/loading_screen.png");
                sys.renderer->endFrame();
            }
            sys.renderer->setRendererMapDimensions(loadedTilesX, loadedTilesZ);
            sys.renderer->setCellSize(sys.terrainSystem->getCellSize());
            {
                const float halfWorld = static_cast<float>(loadedTilesX)
                                        * sys.terrainSystem->getCellSize() * 0.5f;
                sys.cameraController->setTarget(halfWorld, halfWorld);
                sys.cameraController->resetOrbit();
            }
            sys.uiManager->setMapDimensions(loadedTilesX, loadedTilesZ);
            sys.citySimulation->setMapDimensions(loadedTilesX, loadedTilesZ);
            sys.uiManager->rebuildCityFromSim();
            sys.uiManager->transitionToGameplay(GameMode::Sandbox);
            sys.uiManager->onGameLoaded();
            return true;  // skip rendering this frame
        }
    }

    // Check for application quit request.
    if (sys.uiManager->isQuitRequested()) {
        device->closeDevice();
        return true;  // skip rendering; device->run() returns false next iteration
    }

    // Steps 4a/4b: AudioSystem.
    CameraState cameraState = sys.cameraController->getCameraState();
    try {
        sys.audioSystem->syncListenerToCamera(cameraState);
        sys.audioSystem->update(realDeltaSeconds);
    } catch (const std::exception& e) {
        device->getLogger()->log(e.what(), irr::ELL_ERROR);
    }

    // Phase 10b: update renderer (cloud plane UV scrolling).
    sys.renderer->update(realDeltaSeconds);

    // Step 5: beginFrame (driver->beginScene).
    sys.renderer->beginFrame();

    // Step 6: drawScene (sceneManager->drawAll() + uiManager->draw()).
    sys.renderer->drawScene();

    // Step 7: endFrame (driver->endScene).
    sys.renderer->endFrame();

    return true;
}

int main(int argc, char** argv) {
    AppSystems sys;

    if (!initSystems(sys, argc, argv)) {
        return 1;
    }

    irr::IrrlichtDevice* device = sys.renderSystem.getDevice();

    // =========================================================================
    // 8-STEP FRAME LOOP
    // =========================================================================
    int frameCount = 0;
    while (device->run()) {
        if (sys.maxFrames > 0 && frameCount >= sys.maxFrames) {
            device->closeDevice();
            break;
        }
        ++frameCount;

        runFrame(sys);
    }

    return 0;
}
