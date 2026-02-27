#include "src/ui/UIManager.h"
#include "src/platform/input_event.h"     // Full include here (NOT in UIManager.h — see prohibition comment)
#include "src/ui/ui_constants.h"          // kToolbarLeft, kToolbarRight, kToolbarTop, kToolbarBottom

// Panel includes — full headers included only in the .cpp to avoid transitive
// header dependencies for callers that only #include UIManager.h.
#include "src/ui/NotificationManager.h"
#include "src/ui/main_menu_panel.h"
#include "src/ui/hud.h"
#include "src/ui/tax_rate_panel.h"
#include "src/ui/minimap.h"
#include "src/ui/inspector_panel.h"
#include "src/ui/pause_menu_panel.h"
#include "src/ui/settings_panel.h"
#include "src/ui/modal_dialog.h"

// ----------------------------------------------------------------
// Constructor
// Panel construction order is INVARIANT:
//   NotificationManager FIRST — it has no dependencies on other panels.
//   MainMenuPanel second — calls show() in its own constructor.
//   Remaining panels in declared order.
// ----------------------------------------------------------------
UIManager::UIManager(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock)
    : m_backend(backend)
    , m_audio(audio)
    , m_sim(sim)
    , m_clock(clock)
{
    m_notifications = new NotificationManager(m_backend, m_sim, m_clock);
    m_mainMenu      = new MainMenuPanel(m_backend);   // calls show() in its constructor
    m_hud           = new HUD(m_backend, m_audio, m_sim, m_clock);
    m_taxPanel      = new TaxRatePanel(m_backend);
    m_minimap       = new Minimap(m_backend);
    m_inspector     = new InspectorPanel(m_backend);
    m_pauseMenu     = new PauseMenuPanel(m_backend);
    m_settings      = new SettingsPanel(m_backend);
    m_modal         = new ModalDialog(m_backend, m_sim);

    // Wire settings panel into pause menu so Pause > Settings button works.
    m_pauseMenu->setSettingsPanel(m_settings);
}

// ----------------------------------------------------------------
// Destructor — reverse construction order (INVARIANT).
// NotificationManager is destroyed LAST.
// ----------------------------------------------------------------
UIManager::~UIManager() {
    delete m_modal;
    delete m_settings;
    delete m_pauseMenu;
    delete m_inspector;
    delete m_minimap;
    delete m_taxPanel;
    delete m_hud;
    delete m_mainMenu;
    delete m_notifications;  // last
}

// ----------------------------------------------------------------
// onEvent — 6-priority input arbitration chain.
// Per input-arbitration.md: WindowFocusGained/Lost MUST pass through
// at Priority 1 — never consumed by any level, including modal-active.
// ----------------------------------------------------------------
bool UIManager::onEvent(const InputEvent& event) {
    // Priority 1: Window focus events — always pass-through.
    // This guard MUST be preserved verbatim.
    if (event.type == InputEvent::Type::WindowFocusGained ||
        event.type == InputEvent::Type::WindowFocusLost) {
        return false;
    }

    // Priority 2: CRITICAL toast input capture.
    // Dual-guard compound condition: fires ONLY when a CRITICAL toast is visible
    // AND no modal is active. Both conditions must hold simultaneously.
    bool criticalVisible = m_notifications->hasCriticalToastVisible();
    bool modalActive     = hasActiveModal();
    if (criticalVisible && !modalActive) {
        if (m_notifications->onEvent(event)) return true;
    }

    // Priority 3: Minimap carve-out.
    // UX-4: call getBounds() now so the interface contract is structurally verified
    // at compile time. Phase 6 uses the result for hit-testing.
    (void)m_minimap->getBounds();  // Phase 6: if (minimapBounds.contains(event.x, event.y)) route to minimap

    // Priority 4: Modal dialog (when active).
    // When a blocking modal is active, all non-focus input is captured here.
    if (modalActive) {
        // Phase 6: route event to m_modal's internal button handling.
        return true;
    }

    // Priority 5: Pause menu / settings.
    // Phase 6: if m_state == GameState::Paused, route to pause menu / settings panels.

    // Priority 6: HUD and game world input.
    // Phase 6: route to HUD (toolbar carve-out uses kToolbarLeft/Right/Top/Bottom).

    return false;
}

// ----------------------------------------------------------------
// draw — 10 named draw slots in explicit Z-order.
// Called by IrrlichtRenderer::drawScene() INSIDE beginScene/endScene.
// m_gui->drawAll() is NOT called here.
// ----------------------------------------------------------------
void UIManager::draw() {
    m_mainMenu->draw();       // slot 1 — main menu (hidden during gameplay)
    m_minimap->draw();        // slot 2 — minimap (bottom-right in gameplay)
    m_hud->draw();            // slot 3 — HUD toolbar and resource bar
    m_taxPanel->draw();       // slot 4 — tax rate panel (toggled by T hotkey)
    m_inspector->draw();      // slot 5 — query/inspector panel (toggled by I hotkey)
    m_notifications->draw();  // slot 6 — toast stack and notification log
    m_pauseMenu->draw();      // slot 7 — pause menu overlay
    m_settings->draw();       // slot 8 — settings panel
    // slot 9 — background scrim (visible only when a modal is active)
    if (hasActiveModal()) {
        m_backend->setElementVisible(m_scrimHandle, true);
    }
    m_modal->draw();          // slot 10 — modal dialog (topmost)
}

// ----------------------------------------------------------------
// update — per-frame UI state update.
// GD-H3 bridge: polls getConsecutiveDeficitMonths() and forwards to
// notification system. Uses m_clock->nowSeconds() (not accumulated delta)
// for undo expiry timing.
// ----------------------------------------------------------------
void UIManager::update(float /*realDeltaSeconds*/) {
    m_notifications->update();

    // GD-H3 bridge: poll deficit streak for progressive warning toasts.
    // Phase 6 will use the delta to fire notifications at streak transitions.
    // Null guard: constructor comment says "may be null" (test seam).
    if (!m_sim) return;
    int deficitStreak = m_sim->getConsecutiveDeficitMonths();
    // Phase 6: compare deficitStreak vs m_lastKnownDeficitStreak and trigger
    // notifications or transitionToGameOver() at streak >= 3 (Scenario only).
    m_lastKnownDeficitStreak = deficitStreak;

    // Phase 6 forward-reference: undo expiry countdown uses
    //   remainingSeconds = m_sim->getUndoExpiryTimeSeconds() - m_clock->nowSeconds()
}

// ----------------------------------------------------------------
// State-transition method stubs (Phase 3 shells; full logic in Phase 6)
// ----------------------------------------------------------------

void UIManager::transitionToPaused() {
    // Phase 6: set m_state = GameState::Paused; show pause menu; keep HUD.
}

void UIManager::transitionToGameplay_fromPaused() {
    // Phase 6: set m_state = GameState::Gameplay; hide pause menu.
}

void UIManager::transitionToGameOver() {
    // SANDBOX GUARD — must not fire in Sandbox mode (Scenario-only in V1).
    if (m_gameMode != GameMode::Scenario) return;
    // Phase 6: set m_state = GameState::GameOver; show game-over modal.
}

void UIManager::showForcedLoanDialog(const LoanTerms& /*terms*/) {
    // Phase 6: populate the 2-screen forced-loan modal (640x400 px virtual)
    // with terms.amount, terms.repaymentTicks, terms.interestRate and show it.
    m_modal->show();
    m_notifications->setModalActive(true);
}

void UIManager::showGameOverModal(int64_t /*totalDebt*/, int /*monthsInDeficit*/) {
    // Phase 6: populate the game-over modal (560x320 px virtual, stub in V1)
    // with debt and streak data and call m_modal->show().
}

void UIManager::closeModal() {
    // Phase 6: call m_modal->hide() and notify NotificationManager to restore
    // Priority-2 routing if a CRITICAL toast is still visible.
    m_notifications->setModalActive(false);
}

void UIManager::showSettings() {
    // Phase 6: show settings panel (m_settings->show()).
}

void UIManager::transitionToGameplay(GameMode mode) {
    m_gameMode            = mode;
    m_hasUnsavedChanges   = false;
    // Phase 6: hide main menu; show HUD; call m_audio->transitionToGameplay();
    // set m_state = GameState::Gameplay.
}

void UIManager::setUnsavedChanges(bool value) {
    m_hasUnsavedChanges = value;
    // Phase 6: show/hide the unsaved-changes dot in the HUD toolbar via m_hud.
}

bool UIManager::hasActiveModal() const {
    return m_modal && m_modal->isActive();
}
