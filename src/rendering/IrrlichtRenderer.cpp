// IrrlichtRenderer.cpp — IRenderer concrete implementation backed by Irrlicht.
// GLEW must be included BEFORE irrlicht.h (symbol conflict mitigation).
#include <GL/glew.h>

#include <irrlicht.h>

#include "IrrlichtRenderer.h"
#include "src/ui/UIManager.h"  // FULL include here (not in header — per Header Dependency Rule)

#include <cstdio>  // fprintf
#include <cmath>   // M_PI

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace irr;
using namespace irr::video;
using namespace irr::scene;

IrrlichtRenderer::IrrlichtRenderer(irr::IrrlichtDevice* device, UIManager* uiManager)
    : m_device(device)
    , m_uiManager(uiManager)
    , m_driver(device ? device->getVideoDriver() : nullptr)
    , m_smgr(device ? device->getSceneManager() : nullptr)
{
}

void IrrlichtRenderer::beginFrame() {
    if (!m_driver) return;
    m_driver->beginScene(true, true, SColor(255, 0, 0, 0));
}

void IrrlichtRenderer::drawScene() {
    // Per-frame sequence (must be called INSIDE beginScene/endScene pair):
    //   1. sceneManager->drawAll()  — 3D scene
    //   2. uiManager->draw()        — 2D HUD, explicit Z-order
    // NOTE: m_gui->drawAll() is NOT called — it would bypass the explicit Z-order layering
    // required for the background scrim and modal overlay (per architecture/ui-ux/ui-manager.md).
    if (m_smgr) {
        m_smgr->drawAll();
    }
    if (m_uiManager) {
        m_uiManager->draw();
    }
}

void IrrlichtRenderer::endFrame() {
    if (!m_driver) return;
    m_driver->endScene();
}

TextureHandle IrrlichtRenderer::loadTexture(const std::string& path) {
    if (!m_driver) return kInvalidTexture;

    irr::video::ITexture* tex = m_driver->getTexture(path.c_str());
    if (!tex) return kInvalidTexture;

    TextureHandle handle = m_nextHandle++;
    m_textures[handle] = tex;
    return handle;
}

void IrrlichtRenderer::setCamera(const CameraParams& p) {
    if (!m_smgr) return;

    if (!m_camera) {
        m_camera = m_smgr->addCameraSceneNode();
        if (m_camera) {
            // Remove all default animators (prevents FPS/Maya animator interference).
            // Grab each animator before removal, drop after — per scene-graph-ownership.md.
#ifndef NDEBUG
            if (m_camera->getAnimators().size() > 0) {
                fprintf(stderr,
                    "[IrrlichtRenderer] WARNING: unexpected animators on addCameraSceneNode() "
                    "result — removing %zu animator(s)\n",
                    static_cast<size_t>(m_camera->getAnimators().size()));
            }
#endif
            while (m_camera->getAnimators().size() > 0) {
                ISceneNodeAnimator* anim = *m_camera->getAnimators().begin();
                anim->grab();
                m_camera->removeAnimator(anim);
                anim->drop();
            }
        }
    }

    if (!m_camera) return;

    m_camera->setPosition(core::vector3df(p.position.x, p.position.y, p.position.z));
    m_camera->setTarget(core::vector3df(p.target.x, p.target.y, p.target.z));
    m_camera->setFOV(p.fovDegrees * static_cast<float>(M_PI / 180.0));
    m_camera->setNearValue(p.nearClip);
    m_camera->setFarValue(p.farClip);
}
