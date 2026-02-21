#pragma once

#include "src/interfaces/IRenderer.h"  // IRenderer, TextureHandle, CameraParams
#include <irrlicht.h>
#include <unordered_map>

// Forward-declare UIManager — must NOT #include "UIManager.h" in this header.
// Violation breaks headless testability: any ui_tests binary that includes UIManager.h
// without linking aitown_render will fail to compile.
// Rule: IrrlichtRenderer.h forward-declares UIManager; IrrlichtRenderer.cpp #includes it.
// Per architecture/graphics-architecture/irrlicht-device-lifecycle.md Header Dependency Rule.
class UIManager;

// IrrlichtRenderer — concrete implementation of IRenderer backed by Irrlicht.
//
// Per-frame sequence enforced by drawScene():
//   1. sceneManager->drawAll()     (3D scene)
//   2. uiManager->draw()           (2D HUD, explicit Z-order per ui-manager.md)
//   NOTE: m_gui->drawAll() is NOT called — calling it would bypass the explicit Z-order
//   layering required for the background scrim and modal overlay.
//   Both calls happen inside the single beginScene/endScene pair.
//
// Constructor signature LOCKED at Phase 1:
//   IrrlichtRenderer(irr::IrrlichtDevice* device, UIManager* uiManager)
// Phase 3 wires the real UIManager to this already-existing member — does NOT restructure.
class IrrlichtRenderer : public IRenderer {
public:
    // LOCKED constructor signature (Phase 1).
    // device must be non-null. uiManager may be null (draws nothing in that case).
    IrrlichtRenderer(irr::IrrlichtDevice* device, UIManager* uiManager);
    ~IrrlichtRenderer() override = default;

    // IRenderer interface — main-thread-only
    void          beginFrame() override;  // driver->beginScene(true, true, SColor(255,0,0,0))
    void          drawScene()  override;  // smgr->drawAll() + uiManager->draw() inside begin/end pair
    void          endFrame()   override;  // driver->endScene()
    TextureHandle loadTexture(const std::string& path) override;
    void          setCamera(const CameraParams& p) override;

private:
    irr::IrrlichtDevice*        m_device;
    UIManager*                  m_uiManager;
    irr::video::IVideoDriver*   m_driver;
    irr::scene::ISceneManager*  m_smgr;
    irr::scene::ICameraSceneNode* m_camera{nullptr};

    // Texture handle map: TextureHandle → ITexture*
    TextureHandle                                      m_nextHandle{1};
    std::unordered_map<TextureHandle, irr::video::ITexture*> m_textures;
};
