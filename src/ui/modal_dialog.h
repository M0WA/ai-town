#pragma once
#include "src/ui/IUIBackend.h"             // UIElementHandle, IUIBackend
#include "src/interfaces/ICitySimulation.h" // ICitySimulation

// ModalDialog — handles all blocking modal dialogs:
//   - Forced-loan dialog (2-screen flow, 640x400 px virtual)
//   - Demolish confirmation (480x240 px virtual)
//   - WASD preset confirmation (480x240 px virtual)
//   - Game-over modal (560x320 px virtual, stub in V1)
//
// When active, isActive() returns true and UIManager::onEvent() routes all input
// to the modal at Priority 4, blocking the game world and HUD input.
// Stub for Phase 3. Full implementation in Phase 6.
class ModalDialog {
public:
    explicit ModalDialog(IUIBackend* backend, ICitySimulation* sim)
        : m_backend(backend), m_sim(sim)
    {}

    void show() {
        m_active = true;
        m_backend->setElementVisible(0xDEAD0109u, true);
    }
    void hide() {
        m_active = false;
        m_backend->setElementVisible(0xDEAD0109u, false);
    }
    void draw() {
        m_backend->setElementVisible(0xDEAD0109u, m_active);
    }

    // Returns true when a blocking modal is currently displayed.
    // UIManager::hasActiveModal() delegates to this.
    bool isActive() const { return m_active; }

private:
    IUIBackend*      m_backend{nullptr};
    ICitySimulation* m_sim{nullptr};
    bool             m_active{false};
};
