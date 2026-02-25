#pragma once
#include "src/ui/IUIBackend.h"            // UIElementHandle, IUIBackend
#include "src/interfaces/IAudioSystem.h"   // IAudioSystem
#include "src/interfaces/ICitySimulation.h"// ICitySimulation
#include "src/interfaces/IClock.h"         // IClock
#include "src/ui/budget_detail_panel.h"    // BudgetDetailPanel (owned by HUD)

// HUD — the in-gameplay heads-up display.
// Owns BudgetDetailPanel as a sub-panel.
// Parameter order (backend, audio, sim, clock) matches the UIManager canonical order.
// Stub for Phase 3. Full implementation in Phase 6.
//
// m_unsavedDotHandle: handle for the unsaved-changes indicator dot shown in the HUD
// toolbar. kInvalidUIElement (0) means not yet created. Phase 6 creates the real element.
class HUD {
public:
    HUD(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock)
        : m_backend(backend)
        , m_audio(audio)
        , m_sim(sim)
        , m_clock(clock)
        , m_budgetDetail(new BudgetDetailPanel(backend))
    {}

    ~HUD() { delete m_budgetDetail; }

    void show() { m_backend->setElementVisible(0xDEAD0106u, true);  }
    void hide() { m_backend->setElementVisible(0xDEAD0106u, false); }
    void draw() { m_backend->setElementVisible(0xDEAD0106u, true);  }

private:
    IUIBackend*        m_backend{nullptr};
    IAudioSystem*      m_audio{nullptr};
    ICitySimulation*   m_sim{nullptr};
    IClock*            m_clock{nullptr};
    BudgetDetailPanel* m_budgetDetail{nullptr};
    UIElementHandle    m_unsavedDotHandle{kInvalidUIElement};  // Phase 6 creates element
};
