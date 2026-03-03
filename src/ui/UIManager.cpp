// src/ui/UIManager.cpp
//
// UIManager — Phase 8 full implementation.
// Orchestrates the 6-priority input arbitration chain, 10-slot draw order,
// deficit-streak polling (GD-H3 bridge), notification polling, modal-pause
// coordination, and all state transitions.
//
// Draw order: 10 named slots in explicit Z-order (no drawAll()).
// Input arbitration: 6-priority chain per architecture/ui-ux/input-arbitration.md.
// Deficit-streak polling: edge-detect on getConsecutiveDeficitMonths() each frame.

#include "src/ui/UIManager.h"
#include "src/platform/input_event.h"     // Full include here (NOT in UIManager.h — see prohibition comment)
#include "src/ui/ui_constants.h"          // kToolbarLeft, kToolbarRight, kToolbarTop, kToolbarBottom
#include "src/ui/hud_sprite_ids.h"        // kSpriteZone*, kSpriteUtil* sprite handle constants

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

// Explicit interface includes for method calls on forward-declared pointers.
#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/interfaces/IRenderer.h"       // IRenderer — for setZoneOverlay, pickTerrainTile
#include "src/interfaces/ITerrainQuery.h"   // ITerrainQuery — for earthworks cost computation
#include "src/interfaces/simulation_types.h"  // ZoneType, DensityTier, ServiceBuildingType

#include <algorithm>
#include <string>

// Key codes — must match the platform adapter's InputEvent keyCode mapping.
// Irrlicht EKEY_CODE values are used as the baseline. Update these if the
// platform adapter maps differently.
namespace {
    constexpr int kKeyEscape = 27;   // Irrlicht KEY_ESCAPE / SDL2 SDLK_ESCAPE
    constexpr int kKeyReturn = 13;   // Irrlicht KEY_RETURN  / SDL2 SDLK_RETURN
    constexpr int kKeyTab    = 9;    // Irrlicht KEY_TAB     / SDL2 SDLK_TAB
    constexpr int kKeyB      = 66;   // Irrlicht KEY_KEY_B
    constexpr int kKeyD      = 68;   // Irrlicht KEY_KEY_D
    constexpr int kKeyI      = 73;   // Irrlicht KEY_KEY_I
    constexpr int kKeyR      = 82;   // Irrlicht KEY_KEY_R
    constexpr int kKeyT      = 84;   // Irrlicht KEY_KEY_T
    constexpr int kKeyU      = 85;   // Irrlicht KEY_KEY_U
    constexpr int kKeyZ      = 90;   // Irrlicht KEY_KEY_Z
    constexpr int kKeyLCtrl  = 162;  // Irrlicht KEY_LCONTROL
    constexpr int kKeyRCtrl  = 163;  // Irrlicht KEY_RCONTROL

    // Speed selector button bounds (virtual 1920x1080 space).
    // These match HUD.cpp button positions exactly.
    constexpr int kSpeedSelectorLeft   = 1600;
    constexpr int kSpeedSelectorRight  = 1796;
    constexpr int kSpeedSelectorTop    = 8;
    constexpr int kSpeedSelectorBottom = 56;

    // Notification bell bounds (virtual 1920x1080 space).
    constexpr int kBellLeft   = 1820;
    constexpr int kBellRight  = 1868;
    constexpr int kBellTop    = 8;
    constexpr int kBellBottom = 56;

    // Stinger cooldown in seconds.
    constexpr double kStingerCooldownSeconds = 5.0;

    // Hit test helper: returns true if (px, py) is within [x, x+w) x [y, y+h).
    bool inRect(int px, int py, int x, int y, int w, int h) {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
} // namespace

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
    m_taxPanel      = new TaxRatePanel(m_backend, m_sim);
    m_minimap       = new Minimap(m_backend);
    m_inspector     = new InspectorPanel(m_backend, m_sim);
    m_pauseMenu     = new PauseMenuPanel(m_backend);
    m_settings      = new SettingsPanel(m_backend, m_audio, m_clock);
    m_modal         = new ModalDialog(m_backend, m_sim);

    // Wire settings panel into pause menu so Pause > Settings button works.
    m_pauseMenu->setSettingsPanel(m_settings);

    // Wire pause menu into settings panel for back-navigation (Settings > back to Pause).
    m_settings->setPauseMenu(m_pauseMenu);

    // Create the background scrim element (50% opacity, hidden initially).
    // The scrim sits at Z-slot 9 between the settings panel (slot 8) and
    // the modal dialog (slot 10).
    if (m_backend) {
        m_scrimHandle = m_backend->addStaticText("",
            0, 0, m_backend->getVirtualWidth(), m_backend->getVirtualHeight());
        m_backend->setElementAlpha(m_scrimHandle, 0.5f);
        m_backend->setElementVisible(m_scrimHandle, false);
    }

    // --- Phase 9b: Zone sub-panel buttons (3×3 grid: R/C/I × Low/Med/High) ---
    // Top-left anchor: virtual (x:80, y:64). Each button 64×40 px with 4 px gap.
    // Grid layout: 3 columns (zone type) × 3 rows (density tier).
    // All buttons initially hidden (only shown when ActiveTool::Zone is active).
    // Sprite initialization: all inactive first, then active on default selection (ResLow).
    if (m_backend) {
        const int zoneBtnW  = 64;
        const int zoneBtnH  = 40;
        const int zoneGap   = 4;
        const int zoneLeft  = 80;
        const int zoneTop   = 64;

        // Zone type short names (column) and density tier short names (row).
        // Used as text-label fallback when the sprite atlas is not yet loaded.
        // Displayed as "Res\nLow", "Com\nMed", etc.  (two-line abbreviation)
        static const char* kZoneTypeAbbrev[3]    = { "Res", "Com", "Ind" };
        static const char* kDensityTierAbbrev[3] = { "Low", "Med", "High" };

        // Create all 9 buttons; set inactive sprite for each (including the default);
        // then override the default (idx 0, Residential Low) with the active sprite.
        // Tests assert: all 9 inactive calls + 1 active call on the default button.
        for (int densityRow = 0; densityRow < 3; ++densityRow) {
            for (int zoneCol = 0; zoneCol < 3; ++zoneCol) {
                int idx = densityRow * 3 + zoneCol;
                int bx  = zoneLeft + zoneCol  * (zoneBtnW + zoneGap);
                int by  = zoneTop  + densityRow * (zoneBtnH + zoneGap);

                // Build a short text label so the button is readable even when the
                // sprite atlas (hud_sprites_ui.dds) has not yet loaded.
                std::string label = std::string(kZoneTypeAbbrev[zoneCol])
                                    + " " + kDensityTierAbbrev[densityRow];

                m_zoneSubPanelBtns[idx] = m_backend->addButton(label.c_str(), bx, by, zoneBtnW, zoneBtnH);

                // Set inactive sprite for every button (including the default).
                uint32_t inactiveSprite = kSpriteZoneResLowInactive
                                          + static_cast<uint32_t>(zoneCol)
                                          + static_cast<uint32_t>(densityRow) * 3u;
                m_backend->setElementImage(m_zoneSubPanelBtns[idx], inactiveSprite);

                // Initially hidden — only shown when Zone tool is active.
                m_backend->setElementVisible(m_zoneSubPanelBtns[idx], false);
            }
        }

        // Override default selection (Residential Low = idx 0) with the active sprite.
        m_backend->setElementImage(m_zoneSubPanelBtns[0], kSpriteZoneResLowActive);
    }

    // --- Phase 9b: Utilities sub-panel buttons (2×2 grid) ---
    // Top-left anchor: virtual (x:80, y:176). Each button 96×48 px with 4 px gap.
    // Grid layout: 2 columns × 2 rows.
    //   (col0,row0)=PowerPlant, (col1,row0)=WaterTower,
    //   (col0,row1)=FireStation, (col1,row1)=PoliceStation
    // All buttons initially hidden (only shown when ActiveTool::Utilities is active).
    if (m_backend) {
        const int utilBtnW  = 96;
        const int utilBtnH  = 48;
        const int utilGap   = 4;
        const int utilLeft  = 80;
        const int utilTop   = 176;

        // ServiceBuildingType ordinals: PowerPlant=0, WaterTower=1, FireStation=2, PoliceStation=3.
        // Grid positions: (col, row) -> type index:
        //   (0,0)->0=PowerPlant, (1,0)->1=WaterTower,
        //   (0,1)->2=FireStation, (1,1)->3=PoliceStation.
        // Create all 4 buttons; set inactive sprite for each (including the default);
        // then override the default (typeIdx 0, PowerPlant) with the active sprite.
        // Tests assert: all 4 inactive calls + 1 active call on the default button.
        for (int utilRow = 0; utilRow < 2; ++utilRow) {
            for (int utilCol = 0; utilCol < 2; ++utilCol) {
                int typeIdx = utilRow * 2 + utilCol;
                int bx  = utilLeft + utilCol * (utilBtnW + utilGap);
                int by  = utilTop  + utilRow * (utilBtnH + utilGap);

                m_utilSubPanelBtns[typeIdx] = m_backend->addButton("", bx, by, utilBtnW, utilBtnH);

                // Set inactive sprite for every button (including the default).
                uint32_t inactiveSprite = kSpriteUtilPowerInactive
                                          + static_cast<uint32_t>(typeIdx);
                m_backend->setElementImage(m_utilSubPanelBtns[typeIdx], inactiveSprite);

                // Initially hidden.
                m_backend->setElementVisible(m_utilSubPanelBtns[typeIdx], false);
            }
        }

        // Override default selection (PowerPlant = typeIdx 0) with the active sprite.
        m_backend->setElementImage(m_utilSubPanelBtns[0], kSpriteUtilPowerActive);
    }
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
    // --- Ctrl key state tracking (needed for Ctrl+Z at Priority 5) ---
    if (event.type == InputEvent::Type::KeyDown) {
        if (event.keyCode == kKeyLCtrl || event.keyCode == kKeyRCtrl) {
            m_ctrlDown = true;
        }
    }
    if (event.type == InputEvent::Type::KeyUp) {
        if (event.keyCode == kKeyLCtrl || event.keyCode == kKeyRCtrl) {
            m_ctrlDown = false;
        }
    }

    // ============================================================
    // Pre-Priority-1: Window focus events — always pass-through.
    // This guard MUST be preserved verbatim.
    // ============================================================
    if (event.type == InputEvent::Type::WindowFocusGained ||
        event.type == InputEvent::Type::WindowFocusLost) {
        return false;
    }

    // ============================================================
    // Priority 1: Modal dialog (when active).
    // Camera pass-through: scroll wheel, MMB, and RMB pass through
    // to CameraController (Priority 6) regardless of modal state.
    // All other events (left click, keyboard) are consumed by the modal.
    // ============================================================
    bool modalActive = hasActiveModal();
    if (modalActive) {
        // Camera-passthrough events: scroll, MMB (button=2), RMB (button=1).
        if (event.type == InputEvent::Type::MouseWheel) {
            return false;
        }
        if (event.type == InputEvent::Type::MouseMove) {
            return false;
        }
        if ((event.type == InputEvent::Type::MouseButtonDown ||
             event.type == InputEvent::Type::MouseButtonUp) &&
            (event.button == 1 || event.button == 2)) {
            return false;  // RMB and MMB pass through for camera pan/orbit
        }

        // Route to modal for internal button handling.
        m_modal->onEvent(event);
        return true;  // All other events consumed when modal active.
    }

    // ============================================================
    // Priority 2: CRITICAL toast input capture.
    // Dual-guard compound condition: fires ONLY when a CRITICAL toast
    // is visible AND no modal is active. Both conditions evaluated independently.
    // ============================================================
    bool criticalVisible = m_notifications->hasCriticalToastVisible();
    if (criticalVisible && !modalActive) {
        if (m_notifications->onEvent(event)) return true;
    }

    // ============================================================
    // Priority 3: QueryPanel / Inspector (when open) + QueryTool open path.
    // Consumes events within panel bounds.
    // Outside-click closes panel (unless on toolbar or minimap).
    // Escape closes the panel.
    // QueryTool open path: if Query tool active and panel NOT open and LMB click,
    //   ray-cast and open inspector at hit tile.
    // ============================================================
    if (m_inspectorOpen) {
        Rect inspBounds = m_inspector->getBounds();

        // Escape closes the inspector.
        if (event.type == InputEvent::Type::KeyDown && event.keyCode == kKeyEscape) {
            m_inspector->hide();
            m_inspectorOpen = false;
            return true;
        }

        // Click inside inspector bounds — consumed.
        if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
            if (inRect(event.x, event.y, inspBounds.x, inspBounds.y,
                       inspBounds.w, inspBounds.h)) {
                return true;
            }

            // Toolbar carve-out: clicks in toolbar pass through to Priority 5.
            if (inRect(event.x, event.y,
                       kToolbarLeft, kToolbarTop,
                       kToolbarRight - kToolbarLeft,
                       kToolbarBottom - kToolbarTop)) {
                // Fall through to lower priorities.
            }
            // Minimap carve-out: clicks in minimap bounds pass through.
            else {
                Rect minimapBounds = m_minimap->getBounds();
                if (inRect(event.x, event.y, minimapBounds.x, minimapBounds.y,
                           minimapBounds.w, minimapBounds.h)) {
                    // Fall through to lower priorities.
                } else {
                    // Outside click — close inspector, consume event.
                    m_inspector->hide();
                    m_inspectorOpen = false;
                    return true;
                }
            }
        }
    }

    // Priority 3 — QueryTool open path.
    // When Query tool is active, the inspector is NOT yet open, and this is a LMB click
    // on the world (not on a toolbar button or other UI region): ray-cast to find the
    // hovered tile and open the inspector panel at that tile.
    //
    // Toolbar carve-out: clicks inside the left toolbar [kToolbarLeft, kToolbarRight) must
    // reach Priority 5 (toolbar handler) regardless of active tool, so they are excluded
    // from this block entirely.  Without this guard, a toolbar button click while Query is
    // active would fire pickTerrainTile(), fail to hit terrain, and return — preventing
    // Priority 5 from switching the active tool (root cause of Bugs 3 and 4).
    //
    // No-terrain-hit behaviour: when the ray misses all terrain (e.g. click in empty sky),
    // do NOT return — fall through to Priority 5/7 so toolbar clicks and other interactions
    // still process correctly.
    if (!m_inspectorOpen &&
        m_activeTool == ActiveTool::Query &&
        m_state == GameState::Gameplay &&
        event.type == InputEvent::Type::MouseButtonDown &&
        event.button == 0 &&
        m_renderer &&
        // Toolbar carve-out: skip query open path for toolbar clicks so Priority 5 handles them.
        !inRect(event.x, event.y,
                kToolbarLeft, kToolbarTop,
                kToolbarRight - kToolbarLeft,
                kToolbarBottom - kToolbarTop)) {

        int hitX = -1, hitZ = -1;
        if (m_renderer->pickTerrainTile(event.x, event.y, hitX, hitZ)) {
            if (m_sim) {
                QueryResult result = m_sim->queryTile(hitX, hitZ);
                // Compute virtual-space cursor and tile bounds for panel positioning.
                ScreenRect tileBounds{};
                tileBounds = m_renderer->getTileScreenBounds(hitX, hitZ);
                // Open inspector at computed position.
                m_inspector->populate(result, hitX, hitZ,
                                      event.x, event.y, tileBounds);
                m_inspectorOpen = true;
            }
            return true;
        }
        // No terrain hit (ray missed all geometry) — do NOT return here.
        // Fall through to Priority 5 so toolbar buttons and other HUD regions
        // remain responsive when Query tool is active.
    }

    // ============================================================
    // Priority 4: TaxRatePanel (when open).
    // Clicks within bounds are consumed.
    // Escape closes the panel.
    // Outside clicks pass through (unlike inspector, which closes).
    // ============================================================
    if (m_taxPanelOpen) {
        Rect taxBounds = m_taxPanel->getBounds();

        // Escape closes the tax panel.
        if (event.type == InputEvent::Type::KeyDown && event.keyCode == kKeyEscape) {
            m_taxPanel->hide();
            m_taxPanelOpen = false;
            return true;
        }

        // Click inside tax panel bounds — consumed.
        if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
            if (inRect(event.x, event.y, taxBounds.x, taxBounds.y,
                       taxBounds.w, taxBounds.h)) {
                return true;
            }
            // Outside clicks pass through to lower priorities.
        }
    }

    // ============================================================
    // MainMenu state: route all input to MainMenuPanel.
    // The main menu is a full-screen overlay that consumes all input.
    // Must be checked before Priority 5 (HUD controls).
    // When SettingsPanel is visible (opened from main menu), route
    // events there first so Escape/Cancel can close it.
    // ============================================================
    if (m_state == GameState::MainMenu) {
        if (m_settings && m_settings->isVisible()) {
            bool wasVisible = true;
            bool consumed = m_settings->onEvent(event);
            // If settings just closed, re-show the main menu.
            if (wasVisible && !m_settings->isVisible()) {
                m_mainMenu->show();
            }
            if (consumed) return true;
        }
        return m_mainMenu->onEvent(event);
    }

    // ============================================================
    // Paused state: route input to PauseMenuPanel (or SettingsPanel
    // if opened from pause menu). Must be checked before Priority 5
    // so that Escape/clicks reach the panels instead of falling through.
    // ============================================================
    if (m_state == GameState::Paused) {
        // SettingsPanel takes priority when visible (opened from pause menu).
        if (m_settings && m_settings->isVisible()) {
            bool consumed = m_settings->onEvent(event);
            // If settings just closed, re-show the pause menu.
            if (!m_settings->isVisible()) {
                m_pauseMenu->show();
            }
            if (consumed) return true;
        }
        // Route to PauseMenuPanel when visible.
        if (m_pauseMenu && m_pauseMenu->isVisible()) {
            bool consumed = m_pauseMenu->onEvent(event);
            // PauseMenuPanel hides itself on Resume (Escape/Enter/click).
            // If it just closed and settings did NOT open, transition to gameplay.
            if (!m_pauseMenu->isVisible()) {
                if (!m_settings || !m_settings->isVisible()) {
                    transitionToGameplay_fromPaused();
                }
            }
            if (consumed) return true;
        }
    }

    // ============================================================
    // Priority 5: HUD controls.
    // Toolbar clicks, speed selector, undo (Ctrl+Z), minimap,
    // bell icon, hotkeys (B/T/I/Escape).
    // Only active when in Gameplay or Paused state.
    // ============================================================

    if (event.type == InputEvent::Type::KeyDown) {
        // --- Escape: context-dependent ---
        if (event.keyCode == kKeyEscape) {
            if (m_state == GameState::Paused) {
                transitionToGameplay_fromPaused();
                return true;
            }
            if (m_state == GameState::Gameplay) {
                transitionToPaused();
                return true;
            }
            // In MainMenu/GameOver: ignore Escape.
            return false;
        }

        // --- Ctrl+Z: Undo last action ---
        if (event.keyCode == kKeyZ && m_ctrlDown) {
            if (m_sim && m_sim->hasUndoPendingAction() && m_state == GameState::Gameplay) {
                m_sim->undoLastAction();
                return true;
            }
        }

        // --- Hotkeys (only during Gameplay) ---
        if (m_state == GameState::Gameplay) {
            // B: toggle notification log / bell
            if (event.keyCode == kKeyB) {
                m_notifications->toggleLog();
                return true;
            }

            // T: toggle tax rate panel
            if (event.keyCode == kKeyT) {
                m_taxPanelOpen = !m_taxPanelOpen;
                if (m_taxPanelOpen) {
                    m_taxPanel->show();
                } else {
                    m_taxPanel->hide();
                }
                return true;
            }

            // Z: Zone tool hotkey
            if (event.keyCode == kKeyZ && !m_ctrlDown) {
                m_activeTool = ActiveTool::Zone;
                if (m_hud) m_hud->setActiveToolLabel("Zone");
                updateSubPanelVisibility();
                return true;
            }

            // R: Road tool hotkey
            if (event.keyCode == kKeyR) {
                m_activeTool = ActiveTool::Road;
                if (m_hud) m_hud->setActiveToolLabel("Road");
                updateSubPanelVisibility();
                return true;
            }

            // U: Utilities tool hotkey
            if (event.keyCode == kKeyU) {
                m_activeTool = ActiveTool::Utilities;
                if (m_hud) m_hud->setActiveToolLabel("Utilities");
                updateSubPanelVisibility();
                return true;
            }

            // D: Demolish tool hotkey
            if (event.keyCode == kKeyD) {
                m_activeTool = ActiveTool::Demolish;
                if (m_hud) m_hud->setActiveToolLabel("Demolish");
                updateSubPanelVisibility();
                return true;
            }

            // I: Query tool hotkey — toggles between Query and None.
            // Per spec line 120: "existing inspector open/close logic is unchanged."
            // Pressing I activates Query tool AND opens the inspector panel directly
            // (same as pre-Phase-9b behaviour).  Pressing I again closes the inspector
            // and deactivates the Query tool.
            if (event.keyCode == kKeyI) {
                if (m_activeTool == ActiveTool::Query) {
                    m_activeTool = ActiveTool::None;
                    if (m_hud) m_hud->setActiveToolLabel("No tool");
                    if (m_inspectorOpen) {
                        m_inspector->hide();
                        m_inspectorOpen = false;
                    }
                } else {
                    m_activeTool = ActiveTool::Query;
                    if (m_hud) m_hud->setActiveToolLabel("Query");
                    // Open inspector directly (existing behaviour preserved).
                    if (!m_inspectorOpen) {
                        m_inspector->show();
                        m_inspectorOpen = true;
                    }
                }
                updateSubPanelVisibility();
                return true;
            }
        }
    }

    // --- Mouse clicks: HUD regions (only during Gameplay) ---
    if (m_state == GameState::Gameplay &&
        event.type == InputEvent::Type::MouseButtonDown &&
        event.button == 0) {

        // --- Zone sub-panel button hit-test (before toolbar, so sub-panel area is checked first) ---
        // Uses hardcoded positions (same constants as construction) to avoid relying on
        // m_backend->getElementRect() which returns {0,0,0,0} on mock backends.
        // The kInvalidUIElement guard is omitted from the hit-test loop (but preserved
        // for backend calls) so that mock backends returning handle=0 still route correctly.
        if (m_activeTool == ActiveTool::Zone && m_backend) {
            const int zoneBtnW = 64, zoneBtnH = 40, zoneGap = 4;
            const int zoneLeft = 80, zoneTop = 64;
            for (int densityRow = 0; densityRow < 3; ++densityRow) {
                for (int zoneCol = 0; zoneCol < 3; ++zoneCol) {
                    int idx = densityRow * 3 + zoneCol;
                    int bx = zoneLeft + zoneCol * (zoneBtnW + zoneGap);
                    int by = zoneTop  + densityRow * (zoneBtnH + zoneGap);
                    if (inRect(event.x, event.y, bx, by, zoneBtnW, zoneBtnH)) {
                        // Update selection state.
                        m_selectedZoneType    = zoneCol;
                        m_selectedDensityTier = densityRow;
                        // Swap sprites: set all to inactive, then clicked to active.
                        for (int dr2 = 0; dr2 < 3; ++dr2) {
                            for (int zc2 = 0; zc2 < 3; ++zc2) {
                                int idx2 = dr2 * 3 + zc2;
                                if (m_zoneSubPanelBtns[idx2] == kInvalidUIElement) continue;
                                uint32_t sprite = kSpriteZoneResLowInactive
                                                  + static_cast<uint32_t>(zc2)
                                                  + static_cast<uint32_t>(dr2) * 3u;
                                m_backend->setElementImage(m_zoneSubPanelBtns[idx2], sprite);
                            }
                        }
                        if (m_zoneSubPanelBtns[idx] != kInvalidUIElement) {
                            uint32_t activeSprite = kSpriteZoneResLowActive
                                                    + static_cast<uint32_t>(zoneCol)
                                                    + static_cast<uint32_t>(densityRow) * 3u;
                            m_backend->setElementImage(m_zoneSubPanelBtns[idx], activeSprite);
                        }
                        return true;
                    }
                }
            }
        }

        // --- Utilities sub-panel button hit-test ---
        // Uses hardcoded positions (same constants as construction).
        // kInvalidUIElement guard omitted from hit-test loop (preserved only for backend calls).
        if (m_activeTool == ActiveTool::Utilities && m_backend) {
            const int utilBtnW = 96, utilBtnH = 48, utilGap = 4;
            const int utilLeft = 80, utilTop = 176;
            for (int typeIdx = 0; typeIdx < 4; ++typeIdx) {
                int utilCol = typeIdx % 2;
                int utilRow = typeIdx / 2;
                int bx = utilLeft + utilCol * (utilBtnW + utilGap);
                int by = utilTop  + utilRow * (utilBtnH + utilGap);
                if (inRect(event.x, event.y, bx, by, utilBtnW, utilBtnH)) {
                    // Update selection.
                    m_selectedServiceBuilding = typeIdx;
                    // Swap sprites (guard handle before backend call).
                    for (int t2 = 0; t2 < 4; ++t2) {
                        if (m_utilSubPanelBtns[t2] == kInvalidUIElement) continue;
                        uint32_t sprite = kSpriteUtilPowerInactive
                                          + static_cast<uint32_t>(t2);
                        m_backend->setElementImage(m_utilSubPanelBtns[t2], sprite);
                    }
                    if (m_utilSubPanelBtns[typeIdx] != kInvalidUIElement) {
                        uint32_t activeSprite = kSpriteUtilPowerActive
                                                + static_cast<uint32_t>(typeIdx);
                        m_backend->setElementImage(m_utilSubPanelBtns[typeIdx], activeSprite);
                    }
                    return true;
                }
            }
        }

        // Toolbar carve-out (left-side toolbar: zone/road/utility/demolish/query + undo)
        if (inRect(event.x, event.y,
                   kToolbarLeft, kToolbarTop,
                   kToolbarRight - kToolbarLeft,
                   kToolbarBottom - kToolbarTop)) {
            // Per-button hit-test. Button positions match HUD.cpp exactly
            // (kToolBtnSize=48, kToolPad=8, starting y=64).
            const int y = event.y;

            if (y >= 64 && y < 112) {          // Zone
                m_activeTool = ActiveTool::Zone;
                if (m_hud) m_hud->setActiveToolLabel("Zone");
                updateSubPanelVisibility();
            } else if (y >= 120 && y < 168) {  // Road
                m_activeTool = ActiveTool::Road;
                if (m_hud) m_hud->setActiveToolLabel("Road");
                updateSubPanelVisibility();
            } else if (y >= 176 && y < 224) {  // Utilities
                m_activeTool = ActiveTool::Utilities;
                if (m_hud) m_hud->setActiveToolLabel("Utilities");
                updateSubPanelVisibility();
            } else if (y >= 232 && y < 280) {  // Demolish
                m_activeTool = ActiveTool::Demolish;
                if (m_hud) m_hud->setActiveToolLabel("Demolish");
                updateSubPanelVisibility();
            } else if (y >= 288 && y < 336) {  // Query — toggle between Query and None
                // Toolbar button activates Query tool only; inspector is opened by
                // subsequent tile-click (Priority-3 QueryTool open path).
                // Contrast with I hotkey which opens the inspector immediately.
                if (m_activeTool == ActiveTool::Query) {
                    m_activeTool = ActiveTool::None;
                    if (m_hud) m_hud->setActiveToolLabel("No tool");
                    if (m_inspectorOpen) {
                        m_inspector->hide();
                        m_inspectorOpen = false;
                    }
                } else {
                    m_activeTool = ActiveTool::Query;
                    if (m_hud) m_hud->setActiveToolLabel("Query");
                }
                updateSubPanelVisibility();
            } else if (y >= 608 && y < 656) {  // Undo
                if (m_sim && m_sim->hasUndoPendingAction()) {
                    m_sim->undoLastAction();
                }
            }
            return true;
        }

        // Speed selector buttons (top-right region).
        if (inRect(event.x, event.y,
                   kSpeedSelectorLeft, kSpeedSelectorTop,
                   kSpeedSelectorRight - kSpeedSelectorLeft,
                   kSpeedSelectorBottom - kSpeedSelectorTop)) {
            if (m_sim) {
                // Determine which speed button was clicked.
                int relX = event.x - kSpeedSelectorLeft;
                if (relX < 48) {
                    m_sim->setPaused(true);
                } else if (relX < 100) {
                    m_sim->setPaused(false);
                    m_sim->setSpeed(SpeedMultiplier::x1);
                    if (m_audio) m_audio->setSpeed(SimSpeed::x1);
                } else if (relX < 152) {
                    m_sim->setPaused(false);
                    m_sim->setSpeed(SpeedMultiplier::x3);
                    if (m_audio) m_audio->setSpeed(SimSpeed::x3);
                } else {
                    m_sim->setPaused(false);
                    m_sim->setSpeed(SpeedMultiplier::x10);
                    if (m_audio) m_audio->setSpeed(SimSpeed::x10);
                }
            }
            return true;
        }

        // Notification bell icon (top-right).
        if (inRect(event.x, event.y,
                   kBellLeft, kBellTop,
                   kBellRight - kBellLeft,
                   kBellBottom - kBellTop)) {
            m_notifications->toggleLog();
            return true;
        }

        // Minimap (bottom-right).
        Rect minimapBounds = m_minimap->getBounds();
        if (inRect(event.x, event.y, minimapBounds.x, minimapBounds.y,
                   minimapBounds.w, minimapBounds.h)) {
            // Minimap click — consumed. Full click-to-move-camera logic is
            // wired when the minimap is interactable.
            return true;
        }
    }

    // ============================================================
    // Priority 6: CameraController — handled externally by platform adapter.
    // ============================================================

    // ============================================================
    // Priority 7: World-interaction layer.
    // Only active when in Gameplay state with a non-None tool selected.
    // MouseMove: hover highlight update (never consumes — returns false).
    // LMB click: placement dispatch (consumes when a non-Query tool hits terrain).
    // Query tool LMB: handled at Priority 3 above — must not reach here.
    // ============================================================
    if (m_state == GameState::Gameplay && m_activeTool != ActiveTool::None) {
        // Guard: renderer must be set before any world-interaction can occur.
        if (!m_renderer) return false;

        if (event.type == InputEvent::Type::MouseMove) {
            int hitX = -1, hitZ = -1;
            if (m_renderer->pickTerrainTile(event.x, event.y, hitX, hitZ)) {
                // Determine hover colour from active tool.
                uint32_t colour = 0u;
                switch (m_activeTool) {
                    case ActiveTool::Zone:      colour = kHoverArgbZone;      break;
                    case ActiveTool::Road:      colour = kHoverArgbRoad;      break;
                    case ActiveTool::Utilities: colour = kHoverArgbUtilities; break;
                    case ActiveTool::Demolish:  colour = kHoverArgbDemolish;  break;
                    case ActiveTool::Query:     colour = kHoverArgbQuery;     break;
                    default:                    colour = kHoverArgbClear;     break;
                }
                m_renderer->setTileHoverHighlight(hitX, hitZ, colour);
                m_hoveredTileX = hitX;
                m_hoveredTileZ = hitZ;
            } else {
                m_renderer->setTileHoverHighlight(-1, -1, kHoverArgbClear);
                m_hoveredTileX = -1;
                m_hoveredTileZ = -1;
            }
            return false;  // MouseMove is never consumed by world-interaction.
        }

        if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
            // Query tool left-click is handled at Priority 3 — it must not reach here.
            if (m_activeTool == ActiveTool::Query) {
                return false;
            }

            // Ray-cast from click position to find the terrain tile.
            // Do not require a prior MouseMove — the click itself provides the screen coords.
            int hitX = -1, hitZ = -1;
            if (!m_renderer->pickTerrainTile(event.x, event.y, hitX, hitZ)) {
                return false;
            }

            // Earthworks cost computation.
            float slope = 0.0f;
            if (m_terrain) {
                slope = m_terrain->getSlopeDegrees(hitX, hitZ);
            }
            float factor = std::clamp((slope - 15.0f) / 30.0f, 0.0f, 2.0f);
            int   earthworksCost = (slope > 15.0f)
                                   ? static_cast<int>(500.0f * factor)
                                   : 0;

            // Slope guard: insufficient funds for earthworks.
            if (slope > 15.0f && m_sim) {
                float balance = m_sim->getTreasuryBalance();
                if (static_cast<float>(earthworksCost) > balance) {
                    m_notifications->postNormal(
                        "Earthworks Required",
                        "Earthworks required — insufficient funds (cost: $"
                            + std::to_string(earthworksCost) + ").");
                    return true;
                }
            }

            if (!m_sim) return false;

            switch (m_activeTool) {
                case ActiveTool::Zone: {
                    // Guard: skip overlay writes until map dimensions are set.
                    m_sim->placeZone(hitX, hitZ,
                                     static_cast<ZoneType>(m_selectedZoneType),
                                     static_cast<DensityTier>(m_selectedDensityTier),
                                     earthworksCost);
                    if (m_mapTilesX > 0 && m_mapTilesZ > 0) {
                        // Determine overlay colour from zone type.
                        uint32_t overlayColour = 0u;
                        switch (static_cast<ZoneType>(m_selectedZoneType)) {
                            case ZoneType::Residential: overlayColour = kOverlayArgbResidential; break;
                            case ZoneType::Commercial:  overlayColour = kOverlayArgbCommercial;  break;
                            case ZoneType::Industrial:  overlayColour = kOverlayArgbIndustrial;  break;
                        }
                        // Cap overlay map at 100K entries.
                        static constexpr size_t kOverlayCap = 100000u;
                        uint64_t key = static_cast<uint64_t>(hitZ) * static_cast<uint64_t>(m_mapTilesX)
                                       + static_cast<uint64_t>(hitX);
                        if (m_overlayMap.size() < kOverlayCap || m_overlayMap.count(key)) {
                            m_overlayMap[key] = overlayColour;
                        }
                        if (m_renderer) {
                            m_renderer->setZoneOverlay(m_mapTilesX, m_mapTilesZ, m_overlayMap);
                        }
                    }
                    setUnsavedChanges(true);
                    break;
                }
                case ActiveTool::Road: {
                    m_sim->placeRoad(hitX, hitZ, earthworksCost);
                    setUnsavedChanges(true);
                    break;
                }
                case ActiveTool::Utilities: {
                    m_sim->placeServiceBuilding(hitX, hitZ,
                        static_cast<ServiceBuildingType>(m_selectedServiceBuilding),
                        earthworksCost);
                    setUnsavedChanges(true);
                    break;
                }
                case ActiveTool::Demolish: {
                    if (m_demolishConfirmEnabled) {
                        // Show demolish confirmation modal (Phase 8 code path).
                        // The actual demolishTile() call is deferred until the modal confirms.
                        // For Phase 9b, show the modal and return — tile destruction occurs
                        // when the modal's Accept result is polled.
                        m_modal->showDemolishConfirm(1);
                        // Note: demolishTile is NOT called here; the modal must confirm first.
                        // In the current Phase 9b architecture, UIManager polls modal result
                        // via m_modal->getLastResult() during update(). This wiring is deferred
                        // to the full modal integration — for now the modal is shown and the
                        // event is consumed.
                        setUnsavedChanges(true);
                    } else {
                        // Confirmation suppressed — demolish immediately.
                        m_sim->demolishTile(hitX, hitZ);
                        if (m_mapTilesX > 0 && m_mapTilesZ > 0) {
                            uint64_t key = static_cast<uint64_t>(hitZ)
                                           * static_cast<uint64_t>(m_mapTilesX)
                                           + static_cast<uint64_t>(hitX);
                            m_overlayMap.erase(key);
                            if (m_renderer) {
                                m_renderer->setZoneOverlay(m_mapTilesX, m_mapTilesZ, m_overlayMap);
                            }
                        }
                        setUnsavedChanges(true);
                    }
                    break;
                }
                default:
                    break;
            }
            return true;
        }
    }

    return false;
}

// ----------------------------------------------------------------
// updateSubPanelVisibility — show/hide Zone and Utilities sub-panels
// based on the current active tool.  Called whenever m_activeTool changes.
// ----------------------------------------------------------------
void UIManager::updateSubPanelVisibility() {
    if (!m_backend) return;

    bool showZone = (m_activeTool == ActiveTool::Zone);
    for (int i = 0; i < 9; ++i) {
        if (m_zoneSubPanelBtns[i] != kInvalidUIElement) {
            m_backend->setElementVisible(m_zoneSubPanelBtns[i], showZone);
        }
    }

    bool showUtil = (m_activeTool == ActiveTool::Utilities);
    for (int i = 0; i < 4; ++i) {
        if (m_utilSubPanelBtns[i] != kInvalidUIElement) {
            m_backend->setElementVisible(m_utilSubPanelBtns[i], showUtil);
        }
    }
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
    if (m_scrimHandle != kInvalidUIElement && m_backend) {
        m_backend->setElementVisible(m_scrimHandle, hasActiveModal());
    }

    m_modal->draw();          // slot 10 — modal dialog (topmost)
}

// ----------------------------------------------------------------
// update — per-frame UI state update.
//
// 1. Loading gate — early return during terrain generation.
// 2. Notification timer advancement.
// 3. HUD per-frame update.
// 4. Deficit-streak polling (GD-H3 bridge) — CRITICAL Phase 8 logic.
// 5. Speed selector display polling.
// 6. Simulation notification queue polling.
// ----------------------------------------------------------------
void UIManager::update(float realDeltaSeconds) {
    // 1. Loading gate: skip all UI updates during terrain generation.
    if (m_loadingTerrain) return;

    // 1b. Poll MainMenuPanel for user requests (start game, settings, quit).
    if (m_state == GameState::MainMenu && m_mainMenu) {
        if (m_mainMenu->consumeStartGameRequest()) {
            // Per spec: call m_sim->start() before transitionToGameplay() — but
            // start() doesn't exist on ICitySimulation; the sim is already ticking.
            transitionToGameplay(GameMode::Sandbox);
            return; // Skip the rest of this frame's update — state just changed.
        }
        if (m_mainMenu->consumeSettingsRequest()) {
            showSettings();
        }
        if (m_mainMenu->consumeQuitRequest()) {
            m_quitRequested = true;
            return;
        }
    }

    // 1c. Poll PauseMenuPanel for quit requests (Quit to Desktop, Quit to Main Menu).
    if (m_pauseMenu) {
        if (m_pauseMenu->consumeQuitDesktopRequest()) {
            m_quitRequested = true;
            return;
        }
        if (m_pauseMenu->consumeQuitToMenuRequest()) {
            transitionToMainMenu();
            return;
        }
    }

    // 2. Notification manager: auto-dismiss expired normal toasts.
    m_notifications->update();

    // 3. HUD per-frame update (grace period, budget flash, undo countdown).
    m_hud->update(realDeltaSeconds);

    // Null guard: constructor comment says m_sim "may be null" (test seam).
    if (!m_sim) return;

    // =============================================================
    // 4. DEFICIT-STREAK POLLING (GD-H3 bridge)
    // This is THE most critical Phase 8 logic. Edge-detect on
    // getConsecutiveDeficitMonths() fires progressive warnings.
    // =============================================================
    int currentMonths = m_sim->getConsecutiveDeficitMonths();

    // Month 1 edge: post CRITICAL "2 months to bankruptcy" toast.
    if (currentMonths == 1 && m_lastDeficitMonths < 1) {
        m_notifications->postCritical(
            "City Finances Critical",
            "2 months to bankruptcy if deficit continues");
    }

    // Month 2 edge: post CRITICAL "1 month to bankruptcy" toast AND fire stinger.
    if (currentMonths == 2 && m_lastDeficitMonths < 2) {
        m_notifications->postCritical(
            "City Finances Critical",
            "1 month to bankruptcy");

        // Fire CRISIS stinger with 5 s cooldown.
        if (m_audio && m_clock) {
            double now = m_clock->nowSeconds();
            if ((now - m_lastCrisisStingerFireTime) >= kStingerCooldownSeconds) {
                m_audio->triggerStinger(StingerType::CRISIS);
                m_lastCrisisStingerFireTime = now;
            }
        }
    }

    // Month 3+ edge: transition to game-over (Sandbox guard is inside transitionToGameOver).
    if (currentMonths >= 3 && m_lastDeficitMonths < 3) {
        transitionToGameOver();
    }

    // Streak break: deficit cleared after being in deficit.
    if (currentMonths == 0 && m_lastDeficitMonths > 0) {
        m_notifications->postNormal(
            "Finances Stabilizing",
            "Deficit streak broken — finances stabilizing.");
    }

    m_lastDeficitMonths = currentMonths;

    // =============================================================
    // 5. SPEED SELECTOR POLLING
    // Poll sim speed every frame and update display if the handle
    // is valid (created by HUD). This is a no-op until HUD wires
    // the speed selector handle into UIManager.
    // =============================================================
    if (m_speedSelectorHandle != kInvalidUIElement && m_backend) {
        SpeedMultiplier speed = m_sim->getSpeedMultiplier();
        const char* label = "1x";
        switch (speed) {
            case SpeedMultiplier::Paused: label = "||"; break;
            case SpeedMultiplier::x1:     label = "1x"; break;
            case SpeedMultiplier::x3:     label = "3x"; break;
            case SpeedMultiplier::x10:    label = "10x"; break;
        }
        m_backend->setElementText(m_speedSelectorHandle, label);
    }

    // =============================================================
    // 6. NOTIFICATION POLLING
    // Drain the simulation event queue and post the appropriate
    // toasts via NotificationManager.
    // =============================================================
    SimulationNotification notif;
    while (m_sim->pollPendingNotification(notif)) {
        switch (notif.type) {
            case NotificationType::ForcedLoanIssued:
                showForcedLoanDialog(LoanTerms{
                    static_cast<float>(notif.amount),
                    notif.repaymentTicks,
                    0.05f  // unified interest rate
                });
                break;

            case NotificationType::BondIssued:
                m_notifications->postNormal(
                    "Bond Issued",
                    "Emergency Municipal Bond issued: $"
                        + std::to_string(notif.amount));
                break;

            case NotificationType::ServiceDegraded:
                m_notifications->postNormal(
                    "Service Degraded",
                    "A service building has entered reduced coverage.");
                break;

            case NotificationType::BudgetDeficitWarn:
                // Logged to notification log. The CRITICAL deficit-streak
                // toasts are driven by direct polling of
                // getConsecutiveDeficitMonths() above — NOT by this event.
                m_notifications->postNormal(
                    "Budget Warning",
                    "City budget deficit threshold exceeded.");
                break;

            case NotificationType::PopulationMilestone:
                m_notifications->postNormal(
                    "Population Milestone",
                    "Population reached "
                        + std::to_string(notif.milestoneValue) + "!");
                break;

            case NotificationType::CityRatingTransition:
                m_notifications->postNormal(
                    "City Rating Changed",
                    "City rating tier has changed!");
                break;
        }
    }
}

// ----------------------------------------------------------------
// State-transition methods — full Phase 8 implementation.
// ----------------------------------------------------------------

void UIManager::transitionToPaused() {
    m_state = GameState::Paused;
    m_pauseMenu->show();
    if (m_sim) {
        m_sim->setPaused(true);
    }
    // HUD remains visible behind the pause menu overlay.
}

void UIManager::transitionToGameplay_fromPaused() {
    m_state = GameState::Gameplay;
    m_pauseMenu->hide();
    if (m_sim) {
        m_sim->setPaused(false);
    }
}

void UIManager::transitionToGameOver() {
    // SANDBOX GUARD — must not fire in Sandbox mode (Scenario-only in V1).
    if (m_gameMode != GameMode::Scenario) return;

    m_state = GameState::GameOver;

    // Show the game-over modal with current debt and deficit streak.
    int64_t debt = 0;
    int months = 0;
    if (m_sim) {
        debt = static_cast<int64_t>(m_sim->getOutstandingDebt());
        months = m_sim->getConsecutiveDeficitMonths();
    }
    showGameOverModal(debt, months);
}

void UIManager::showForcedLoanDialog(const LoanTerms& terms) {
    // Modal open sequence (notification-system.md):
    // 1. Set modal active on notifications FIRST (hides CRITICAL toasts).
    m_notifications->setModalActive(true);

    // 2. Track whether we paused the sim for this modal.
    bool wasPaused = m_sim && m_sim->isPaused();
    if (!wasPaused && m_sim) {
        m_sim->setPaused(true);
        m_didPauseSim = true;
    } else {
        m_didPauseSim = false;
    }

    // 3. Show the forced-loan dialog via ModalDialog.
    m_modal->showForcedLoan(terms);
}

void UIManager::showGameOverModal(int64_t totalDebt, int monthsInDeficit) {
    // Modal open sequence.
    m_notifications->setModalActive(true);

    bool wasPaused = m_sim && m_sim->isPaused();
    if (!wasPaused && m_sim) {
        m_sim->setPaused(true);
        m_didPauseSim = true;
    } else {
        m_didPauseSim = false;
    }

    m_modal->showGameOver(totalDebt, monthsInDeficit);
}

void UIManager::closeModal() {
    // CRITICAL ORDERING (per notification-system.md):
    // 1. Hide the modal dialog FIRST.
    m_modal->hide();

    // 2. If UIManager paused the sim for this modal, unpause FIRST.
    if (m_didPauseSim && m_sim) {
        m_sim->setPaused(false);
        m_didPauseSim = false;
    }

    // 3. THEN notify NotificationManager to restore CRITICAL toast visibility.
    // setModalActive(false) may synchronously re-pause if CRITICAL queue is
    // non-empty — this is correct and expected.
    m_notifications->setModalActive(false);
}

void UIManager::showSettings() {
    // Hide the calling panel before showing settings overlay.
    if (m_state == GameState::MainMenu && m_mainMenu) {
        m_mainMenu->hide();
    }
    m_settings->show();
}

void UIManager::transitionToGameplay(GameMode mode) {
    m_gameMode          = mode;
    m_hasUnsavedChanges = false;
    m_state             = GameState::Gameplay;

    // Hide main menu, show HUD.
    m_mainMenu->hide();
    m_hud->show();

    // Transition audio from menu to gameplay.
    // setTimeOfDay must be called before transitionToGameplay() so the
    // ambient bed selection reads the correct time period.
    if (m_audio) {
        m_audio->setTimeOfDay(TimeOfDay::DAY);  // New game starts at DAY
        m_audio->transitionToGameplay();
    }
}

void UIManager::setUnsavedChanges(bool value) {
    m_hasUnsavedChanges = value;
    if (m_hud) {
        m_hud->setUnsavedChanges(value);
    }
}

bool UIManager::hasActiveModal() const {
    return m_modal && m_modal->isActive();
}

void UIManager::setLoadingTerrain(bool loading) {
    m_loadingTerrain = loading;
}

bool UIManager::isQuitRequested() const {
    return m_quitRequested;
}

void UIManager::transitionToMainMenu() {
    // Hide gameplay panels.
    m_hud->hide();
    m_pauseMenu->hide();
    m_taxPanel->hide();
    m_taxPanelOpen = false;
    m_inspector->hide();
    m_inspectorOpen = false;

    // TODO Phase 9+: transition audio back to menu music when
    // IAudioSystem::transitionToMenu() is available.

    // Show main menu.
    m_state = GameState::MainMenu;
    m_mainMenu->show();
}

// ----------------------------------------------------------------
// Phase 9b: late-bind setters
// These are one-time initialization setters called from main.cpp
// after terrain generation completes.  They are NOT on the
// IRenderer interface — they are UIManager-specific wiring steps.
// ----------------------------------------------------------------

void UIManager::setRenderer(IRenderer* renderer) {
    m_renderer = renderer;
}

void UIManager::setTerrainQuery(ITerrainQuery* terrain) {
    m_terrain = terrain;
}

void UIManager::setMapDimensions(int mapTilesX, int mapTilesZ) {
    // Re-call safety: if dimensions change AND we already had valid dimensions set,
    // clear stale overlay keys from the previous map so old indices cannot corrupt
    // the new map's overlay. On the FIRST call (m_mapTilesX == 0 && m_mapTilesZ == 0),
    // there is nothing to clear — skip the setZoneOverlay call entirely.
    bool dimensionsChanged = (m_mapTilesX != mapTilesX || m_mapTilesZ != mapTilesZ);
    bool hadValidDimensions = (m_mapTilesX > 0 && m_mapTilesZ > 0);

    if (dimensionsChanged && hadValidDimensions) {
        m_overlayMap.clear();
        // Push an empty setZoneOverlay call to clear the render-layer overlay mesh.
        if (m_renderer) {
            m_renderer->setZoneOverlay(mapTilesX, mapTilesZ, {});
        }
    } else if (dimensionsChanged) {
        // First call (from 0,0 to actual dimensions): just clear overlay map, no renderer call.
        m_overlayMap.clear();
    }
    m_mapTilesX = mapTilesX;
    m_mapTilesZ = mapTilesZ;
}

ActiveTool UIManager::getActiveTool() const {
    return m_activeTool;
}

// ----------------------------------------------------------------
// onNewGame — reset world-interaction state for a new game.
// Clears overlay map, pushes an empty setZoneOverlay() to clear
// the render-layer overlay mesh, resets active tool and hovered tile.
// ----------------------------------------------------------------
void UIManager::onNewGame() {
    m_overlayMap.clear();
    if (m_renderer && m_mapTilesX > 0 && m_mapTilesZ > 0) {
        m_renderer->setZoneOverlay(m_mapTilesX, m_mapTilesZ, {});
    }
    m_activeTool   = ActiveTool::None;
    m_hoveredTileX = -1;
    m_hoveredTileZ = -1;
    updateSubPanelVisibility();
}
