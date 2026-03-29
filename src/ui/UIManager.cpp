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
#include "src/ui/MainMenuPanel.h"
#include "src/ui/HUD.h"
#include "src/ui/FinancesPanel.h"
#include "src/ui/Minimap.h"
#include "src/ui/InspectorPanel.h"
#include "src/ui/PauseMenuPanel.h"
#include "src/ui/SettingsPanel.h"
#include "src/ui/ModalDialog.h"

// Explicit interface includes for method calls on forward-declared pointers.
#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/sound_ids.h"       // UI_CLICK, UI_MENU_OPEN, UI_MENU_CLOSE — Phase 10 wiring
#include <cstdio>    // fopen/fclose/fprintf — loadKeybindings file probe
#include <cstdlib>   // getenv — HOME / APPDATA resolution in loadKeybindings
#if defined(_WIN32)
#  include <cstring> // snprintf on MSVC
#endif
#include "src/interfaces/audio_types.h"     // SoundPriority::NORMAL — Phase 10 wiring
#include "src/interfaces/ICitySimulation.h"
#include "src/interfaces/IRenderer.h"       // IRenderer — for setZoneOverlay, pickTerrainTile
#include "src/interfaces/ITerrainQuery.h"   // ITerrainQuery — for earthworks cost computation
#include "src/interfaces/simulation_types.h"  // ZoneType, DensityTier, ServiceBuildingType
#include "src/interfaces/ISaveSystem.h"        // ISaveSystem — Phase 11c interface abstraction
#include "src/simulation/SaveSystem.h"         // SaveResult/LoadResult types used in save wiring

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
    constexpr int kKeyS      = 83;   // Irrlicht KEY_KEY_S
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
// computeZoneOverlayColor — 3×3 lookup table for zone overlay ARGB colors.
// Rows: ZoneType (Residential=0, Commercial=1, Industrial=2)
// Cols: DensityTier (Low=0, Medium=1, High=2)
// Alpha 0xB4 = ~71% opacity for the under-construction overlay.
// ----------------------------------------------------------------
/*static*/ uint32_t UIManager::computeZoneOverlayColor(ZoneType zone, DensityTier density) {
    static constexpr uint32_t kTable[3][3] = {
        // Low,         Medium,       High
        { 0xB480CC80u, 0xB400AA00u, 0xB4005500u }, // Residential (green)
        { 0xB48080CCu, 0xB40000AAu, 0xB4000055u }, // Commercial  (blue)
        { 0xB4CCCC80u, 0xB4AAAA00u, 0xB4555500u }, // Industrial  (yellow)
    };
    return kTable[static_cast<int>(zone)][static_cast<int>(density)];
}

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
    m_notifications = new NotificationManager(m_backend, m_sim, m_clock, m_audio);
    m_mainMenu      = new MainMenuPanel(m_backend);   // calls show() in its constructor
    m_hud           = new HUD(m_backend, m_audio, m_sim, m_clock);
    m_minimap       = new Minimap(m_backend);
    m_inspector     = new InspectorPanel(m_backend, m_sim);
    m_pauseMenu     = new PauseMenuPanel(m_backend);
    m_settings      = new SettingsPanel(m_backend, m_audio, m_clock);
    m_modal         = new ModalDialog(m_backend, m_sim);

    // Wire settings panel into pause menu so Pause > Settings button works.
    m_pauseMenu->setSettingsPanel(m_settings);

    // Wire pause menu into settings panel for back-navigation (Settings > back to Pause).
    m_settings->setPauseMenu(m_pauseMenu);

    // Wire modal dialog into settings panel (for WASD preset + Restore Defaults modals).
    // m_modal is constructed immediately before this call (see line above).
    m_settings->setModal(m_modal);

    // Wire keybindings apply callback: SettingsPanel calls this on Controls Apply.
    m_settings->setKeybindingsApplyFn([this](const KeyBindings& b){ applyKeybindings(b); });

    // Seed SettingsPanel with the current (default) keybindings so the Controls tab
    // opens with the right state. (loadKeybindings() may not have been called yet —
    // setCurrentBindings is re-called after loadKeybindings() in main.cpp. The seeding
    // here ensures the Controls tab is never open with uninitialised bindings.)
    m_settings->setCurrentBindings(m_keyBindings);

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

        // Create all 9 buttons; set active sprite for each so icons are always visible.
        // The selected button (default: idx 0, Residential Low) shows its active sprite.
        // Non-selected buttons also start with their active sprites — they transition to
        // inactive (outline) only when the player selects a different button, providing a
        // clear selection indicator while keeping icons visible in the initial open state.
        // Empty string label — sprite is the sole visual encoding (hud_sprites_ui.dds is committed).
        for (int densityRow = 0; densityRow < 3; ++densityRow) {
            for (int zoneCol = 0; zoneCol < 3; ++zoneCol) {
                int idx = densityRow * 3 + zoneCol;
                int bx  = zoneLeft + zoneCol  * (zoneBtnW + zoneGap);
                int by  = zoneTop  + densityRow * (zoneBtnH + zoneGap);

                m_zoneSubPanelBtns[idx] = m_backend->addButton("", bx, by, zoneBtnW, zoneBtnH);

                // Set active sprite so the filled icon is visible from the start.
                // Sprite IDs: kSpriteZoneResLowActive(64) + zoneCol + densityRow*3.
                uint32_t activeSprite = kSpriteZoneResLowActive
                                        + static_cast<uint32_t>(zoneCol)
                                        + static_cast<uint32_t>(densityRow) * 3u;
                m_backend->setElementImage(m_zoneSubPanelBtns[idx], activeSprite);

                // Initially hidden — only shown when Zone tool is active.
                m_backend->setElementVisible(m_zoneSubPanelBtns[idx], false);
            }
        }

    }

    // --- Phase 9b: Utilities sub-panel buttons (4×1 single-row grid) ---
    // Top-left anchor: virtual (x:80, y:64). Each button 64×40 px with 4 px gap.
    // Grid layout: 4 columns × 1 row (single horizontal strip).
    //   col0=PowerPlant, col1=WaterTower, col2=FireStation, col3=PoliceStation
    // Total width: 4*64 + 3*4 = 268 px (x:80..348). Height: 40 px (y:64..104).
    // All buttons initially hidden (only shown when ActiveTool::Utilities is active).
    // Anchored at y:64 (same as Zone sub-panel) — sub-panels are mutually exclusive.
    if (m_backend) {
        const int utilBtnW  = 64;
        const int utilBtnH  = 40;
        const int utilGap   = 4;
        const int utilLeft  = 80;
        const int utilTop   = 64;

        // ServiceBuildingType ordinals: PowerPlant=0, WaterTower=1, FireStation=2, PoliceStation=3.
        // Single-row layout: typeIdx == column index.
        //   col0->0=PowerPlant, col1->1=WaterTower, col2->2=FireStation, col3->3=PoliceStation.
        // Create all 4 buttons; set active sprite for each so icons are always visible.
        // Same pattern as Zone sub-panel: icons visible from the start; transition to
        // inactive (outline) only for non-selected buttons when a different button is clicked.
        // Empty string label — sprite is the sole visual encoding (hud_sprites_ui.dds is committed).

        for (int typeIdx = 0; typeIdx < 4; ++typeIdx) {
            int bx = utilLeft + typeIdx * (utilBtnW + utilGap);
            int by = utilTop;

            m_utilSubPanelBtns[typeIdx] = m_backend->addButton(
                "", bx, by, utilBtnW, utilBtnH);

            // Set active sprite so the filled icon is visible from the start.
            uint32_t activeSprite = kSpriteUtilPowerActive
                                    + static_cast<uint32_t>(typeIdx);
            m_backend->setElementImage(m_utilSubPanelBtns[typeIdx], activeSprite);

            // Initially hidden.
            m_backend->setElementVisible(m_utilSubPanelBtns[typeIdx], false);
        }
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
            // Phase 11d Deliverable 4a: hide service coverage overlay on inspector close.
            if (m_renderer) m_renderer->hideServiceCoverageOverlay();
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
                    // Phase 11d Deliverable 4a: hide service coverage overlay on outside click.
                    if (m_renderer) m_renderer->hideServiceCoverageOverlay();
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
        // Use physical pixel coords for ray-casting — getRayFromScreenCoordinates()
        // requires physical (not virtual/scaled) screen coordinates.
        if (m_renderer->pickTerrainTile(event.physX, event.physY, hitX, hitZ)) {
            if (m_sim) {
                QueryResult result = m_sim->queryTile(hitX, hitZ);
                // Compute virtual-space cursor and tile bounds for panel positioning.
                ScreenRect tileBounds{};
                tileBounds = m_renderer->getTileScreenBounds(hitX, hitZ);
                // Open inspector at computed position.
                // event.x/y are virtual coords (1920×1080); populate uses them for
                // panel cascade positioning in virtual space.
                m_inspector->populate(result, hitX, hitZ,
                                      event.x, event.y, tileBounds);
                m_inspectorOpen = true;
                // Phase 11d Deliverable 4a: show service coverage overlay when
                // the queried tile holds a service building.
                if (m_renderer &&
                    result.serviceType != ServiceBuildingType::None) {
                    m_renderer->showServiceCoverageOverlay(
                        hitX, hitZ, result.serviceType, result.degraded);
                }
            }
            return true;
        }
        // No terrain hit (ray missed all geometry) — do NOT return here.
        // Fall through to Priority 5 so toolbar buttons and other HUD regions
        // remain responsive when Query tool is active.
    }

    // ============================================================
    // Priority 4: FinancesPanel (when open) — Phase 11l.
    // Forwards events to FinancesPanel; panel handles Escape and
    // outside-click dismiss internally. Outside-click is NOT consumed
    // so the underlying game-world handlers still receive the event.
    // ============================================================
    if (m_financesPanelOpen) {
        if (m_hud && m_hud->getFinances()) {
            bool consumed = m_hud->getFinances()->onEvent(event);
            // Sync open state in case FinancesPanel closed itself
            m_financesPanelOpen = m_hud->getFinances()->isOpen();
            if (consumed) return true;
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

        // --- Ctrl+S: Manual save (Gameplay only) ---
        if (event.keyCode == kKeyS && m_ctrlDown && m_state == GameState::Gameplay) {
            if (m_saveSystem) {
                // Post-V1: slot picker dialog
                SaveResult result = m_saveSystem->saveToSlot(1);
                if (result.ok) {
                    setUnsavedChanges(false);
                } else {
                    // Manual save failure — show blocking Retry/Cancel modal.
                    if (m_modal) {
                        m_pendingSaveFailure = true;
                        m_modal->showSaveFailure(result.error);
                    }
                }
            }
            return true;
        }

        // --- Hotkeys (only during Gameplay) ---
        if (m_state == GameState::Gameplay) {
            // B: toggle notification log / bell
            if (event.keyCode == kKeyB) {
                m_notifications->toggleLog();
                return true;
            }

            // T: toggle finances panel
            if (event.keyCode == kKeyT) {
                if (m_hud) {
                    m_hud->toggleFinancesPanel();
                    m_financesPanelOpen = m_hud->getFinances()
                                         ? m_hud->getFinances()->isOpen()
                                         : false;
                }
                return true;
            }

            // Z: Zone tool hotkey
            if (event.keyCode == kKeyZ && !m_ctrlDown) {
                m_activeTool = ActiveTool::Zone;
                if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
                if (m_hud) m_hud->setActiveToolLabel("Zone");
                updateSubPanelVisibility();
                return true;
            }

            // R: Road tool hotkey
            if (event.keyCode == kKeyR) {
                m_activeTool = ActiveTool::Road;
                if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
                if (m_hud) m_hud->setActiveToolLabel("Road");
                updateSubPanelVisibility();
                return true;
            }

            // U: Utilities tool hotkey
            if (event.keyCode == kKeyU) {
                m_activeTool = ActiveTool::Utilities;
                if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
                if (m_hud) m_hud->setActiveToolLabel("Utilities");
                updateSubPanelVisibility();
                return true;
            }

            // D: Demolish tool hotkey
            if (event.keyCode == kKeyD) {
                m_activeTool = ActiveTool::Demolish;
                if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
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
                    if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
                    if (m_hud) m_hud->setActiveToolLabel("No tool");
                    if (m_inspectorOpen) {
                        m_inspector->hide();
                        m_inspectorOpen = false;
                    }
                } else {
                    m_activeTool = ActiveTool::Query;
                    if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
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
                        // Update selection state. All buttons keep their active sprite —
                        // icons remain visible for all zone types at all times.
                        m_selectedZoneType    = zoneCol;
                        m_selectedDensityTier = densityRow;
                        // Phase 10: ui_click SFX on zone sub-panel button press.
                        if (m_audio) m_audio->playSound(UI_CLICK, SoundPriority::NORMAL, 1.0f);
                        return true;
                    }
                }
            }
        }

        // --- Utilities sub-panel button hit-test ---
        // Uses hardcoded positions (same constants as construction).
        // kInvalidUIElement guard omitted from hit-test loop (preserved only for backend calls).
        if (m_activeTool == ActiveTool::Utilities && m_backend) {
            const int utilBtnW = 64, utilBtnH = 40, utilGap = 4;
            const int utilLeft = 80, utilTop = 64;
            for (int typeIdx = 0; typeIdx < 4; ++typeIdx) {
                int bx = utilLeft + typeIdx * (utilBtnW + utilGap);
                int by = utilTop;
                if (inRect(event.x, event.y, bx, by, utilBtnW, utilBtnH)) {
                    // Update selection. All buttons keep their active sprite —
                    // icons remain visible for all utility types at all times.
                    m_selectedServiceBuilding = typeIdx;
                    // Phase 10: ui_click SFX on utilities sub-panel button press.
                    if (m_audio) m_audio->playSound(UI_CLICK, SoundPriority::NORMAL, 1.0f);
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
                if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
                if (m_hud) m_hud->setActiveToolLabel("Zone");
                updateSubPanelVisibility();
                // Phase 10: ui_click SFX on toolbar button press.
                if (m_audio) m_audio->playSound(UI_CLICK, SoundPriority::NORMAL, 1.0f);
            } else if (y >= 120 && y < 168) {  // Road
                m_activeTool = ActiveTool::Road;
                if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
                if (m_hud) m_hud->setActiveToolLabel("Road");
                updateSubPanelVisibility();
                if (m_audio) m_audio->playSound(UI_CLICK, SoundPriority::NORMAL, 1.0f);
            } else if (y >= 176 && y < 224) {  // Utilities
                m_activeTool = ActiveTool::Utilities;
                if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
                if (m_hud) m_hud->setActiveToolLabel("Utilities");
                updateSubPanelVisibility();
                if (m_audio) m_audio->playSound(UI_CLICK, SoundPriority::NORMAL, 1.0f);
            } else if (y >= 232 && y < 280) {  // Demolish
                m_activeTool = ActiveTool::Demolish;
                if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
                if (m_hud) m_hud->setActiveToolLabel("Demolish");
                updateSubPanelVisibility();
                if (m_audio) m_audio->playSound(UI_CLICK, SoundPriority::NORMAL, 1.0f);
            } else if (y >= 288 && y < 336) {  // Query — toggle between Query and None
                // Toolbar button activates Query tool only; inspector is opened by
                // subsequent tile-click (Priority-3 QueryTool open path).
                // Contrast with I hotkey which opens the inspector immediately.
                if (m_activeTool == ActiveTool::Query) {
                    m_activeTool = ActiveTool::None;
                    if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
                    if (m_hud) m_hud->setActiveToolLabel("No tool");
                    if (m_inspectorOpen) {
                        m_inspector->hide();
                        m_inspectorOpen = false;
                    }
                } else {
                    m_activeTool = ActiveTool::Query;
                    if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
                    if (m_hud) m_hud->setActiveToolLabel("Query");
                }
                updateSubPanelVisibility();
                if (m_audio) m_audio->playSound(UI_CLICK, SoundPriority::NORMAL, 1.0f);
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

        // Resource/budget bar click (top area: x=8-1912, y=0-56) → toggle FinancesPanel.
        // Phase 11l: replaces the old treasury-hover BudgetDetailPanel.
        // The FinancesPanel open() fires UI_MENU_OPEN via IAudioSystem internally.
        if (inRect(event.x, event.y, 8, 0, 1904, 56)) {
            if (m_hud) {
                // If panel is already open, a resource-bar click dismisses it only
                // (spec: dismiss handler at panel priority level consumes the click first).
                if (m_financesPanelOpen && m_hud->getFinances()) {
                    m_hud->getFinances()->close();
                    m_financesPanelOpen = false;
                } else {
                    m_hud->toggleFinancesPanel();
                    m_financesPanelOpen = m_hud->getFinances()
                                         ? m_hud->getFinances()->isOpen()
                                         : false;
                }
            }
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
    // Priority 6b: RMB *click* (release without drag) cancels the active tool.
    // Only during Gameplay when a non-None tool is active.
    // EventReceiver only delivers RMB-up here when no camera drag occurred
    // (m_rmbMoved == false in EventReceiver), so this handler fires exclusively
    // on short right-clicks.  RMB drag always starts the camera and never
    // reaches this branch.
    // ============================================================
    if (m_state == GameState::Gameplay &&
        event.type == InputEvent::Type::MouseButtonUp &&
        event.button == 1 &&
        m_activeTool != ActiveTool::None) {
        // Close any open sub-panels and clear the active tool.
        m_activeTool = ActiveTool::None;
        if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
        if (m_hud) m_hud->setActiveToolLabel("No tool");
        updateSubPanelVisibility();
        // Also clear zone/road anchor and placement preview in case a drag was in progress.
        m_zoneAnchorX = -1;
        m_zoneAnchorZ = -1;
        m_lmbHeld = false;
        if (m_renderer) m_renderer->setTilePlacementPreview({}, 0u, {});
        // Clear the hover highlight so the last-hovered tile quad is not frozen
        // on screen after the tool is deselected.  The MouseMove handler at Priority 7
        // is gated on m_activeTool != None, so without this call m_hoverVisible
        // stays true and the quad persists until the next MouseMove.
        if (m_renderer) m_renderer->clearDemolishHighlight();
        m_demolishPendingTileX = -1;
        m_demolishPendingTileZ = -1;
        if (m_renderer) m_renderer->setTileHoverHighlight(-1, -1);
        m_hoveredTileX = -1;
        m_hoveredTileZ = -1;
        return true;  // Consumed: tool deselected.
    }

    // ============================================================
    // Priority 7: World-interaction layer.
    // Only active when in Gameplay state with a non-None tool selected.
    //
    // Zone tool: immediate placement on LMB click (single tile per click).
    //   LMB press  → place at hovered tile immediately via doTerrainPlacement().
    //   MouseMove  → update hover highlight only.
    //   LMB release → clears m_lmbHeld; drag-painting places on each new tile.
    //
    // Road tool: placement deferred to LMB release (commit on mouse-up).
    //   LMB press  → record anchor tile; update hover highlight; do NOT place.
    //   MouseMove  → update hover highlight only (no placement during drag).
    //   LMB release → place at current hovered tile (single commit).
    //
    // Utilities and Demolish retain tile-by-tile drag behavior:
    //   LMB press  → placement dispatch (consumes when terrain hit).
    //   MouseMove  → drag-to-place when LMB held and tile changes.
    //   LMB release → clears m_lmbHeld (never consumes).
    //
    // Query tool LMB: handled at Priority 3 above — must not reach here.
    // MouseMove: never consumes — returns false (edge-scroll must still reach
    //            CameraController).
    // ============================================================
    if (m_state == GameState::Gameplay && m_activeTool != ActiveTool::None) {
        // Guard: renderer must be set before any world-interaction can occur.
        if (!m_renderer) return false;

        if (event.type == InputEvent::Type::MouseButtonUp && event.button == 0) {
            // Zone tool: commit rectangular fill on mouse-up.
            if (m_activeTool == ActiveTool::Zone && m_zoneAnchorX != -1) {
                int releaseX = m_hoveredTileX;
                int releaseZ = m_hoveredTileZ;
                if (releaseX != -1 && releaseZ != -1) {
                    int x0 = std::min(m_zoneAnchorX, releaseX);
                    int x1 = std::max(m_zoneAnchorX, releaseX);
                    int z0 = std::min(m_zoneAnchorZ, releaseZ);
                    int z1 = std::max(m_zoneAnchorZ, releaseZ);
                    for (int tz = z0; tz <= z1; ++tz) {
                        for (int tx = x0; tx <= x1; ++tx) {
                            // Phase 11d Deliverable 5d: skip tiles already occupied
                            // (isRoad || isZoned) — sim guard already rejects them, but
                            // skipping here avoids triggering the placeZone early-return
                            // for every blocked tile in the rect.
                            if (m_sim) {
                                QueryResult q = m_sim->queryTile(tx, tz);
                                if (q.isRoad || q.isZoned) continue;
                            }
                            doTerrainPlacement(tx, tz);
                        }
                    }
                }
                m_zoneAnchorX = -1;
                m_zoneAnchorZ = -1;
                // Clear placement preview.
                m_renderer->setTilePlacementPreview({}, 0u, {});
            }

            // Road tool: commit straight-line placement on mouse-up.
            if (m_activeTool == ActiveTool::Road && m_zoneAnchorX != -1) {
                int releaseX = m_hoveredTileX;
                int releaseZ = m_hoveredTileZ;
                if (releaseX != -1 && releaseZ != -1) {
                    int dX = std::abs(releaseX - m_zoneAnchorX);
                    int dZ = std::abs(releaseZ - m_zoneAnchorZ);
                    if (dX >= dZ) {
                        // Dominant axis: X — place along X at anchor Z.
                        int x0 = std::min(m_zoneAnchorX, releaseX);
                        int x1 = std::max(m_zoneAnchorX, releaseX);
                        for (int tx = x0; tx <= x1; ++tx) {
                            // Phase 11d Deliverable 5d: skip occupied tiles.
                            if (m_sim) {
                                QueryResult q = m_sim->queryTile(tx, m_zoneAnchorZ);
                                if (q.isRoad || q.isZoned) continue;
                            }
                            doTerrainPlacement(tx, m_zoneAnchorZ);
                        }
                    } else {
                        // Dominant axis: Z — place along Z at anchor X.
                        int z0 = std::min(m_zoneAnchorZ, releaseZ);
                        int z1 = std::max(m_zoneAnchorZ, releaseZ);
                        for (int tz = z0; tz <= z1; ++tz) {
                            // Phase 11d Deliverable 5d: skip occupied tiles.
                            if (m_sim) {
                                QueryResult q = m_sim->queryTile(m_zoneAnchorX, tz);
                                if (q.isRoad || q.isZoned) continue;
                            }
                            doTerrainPlacement(m_zoneAnchorX, tz);
                        }
                    }
                }
                m_zoneAnchorX = -1;
                m_zoneAnchorZ = -1;
                // Clear placement preview.
                m_renderer->setTilePlacementPreview({}, 0u, {});
            }

            // Phase 11h: Demolish tool — mouse-up triggers confirmation modal or immediate demolish.
            if (m_activeTool == ActiveTool::Demolish && m_demolishPendingTileX != -1) {
                int upHitX = -1, upHitZ = -1;
                bool hitTerrain = m_renderer->pickTerrainTile(event.physX, event.physY,
                                                               upHitX, upHitZ);
                if (hitTerrain &&
                    upHitX == m_demolishPendingTileX && upHitZ == m_demolishPendingTileZ) {
                    // Mouse released on same tile as press — confirm demolish.
                    if (m_demolishConfirmEnabled && m_modal) {
                        m_modal->showDemolishConfirm(1);
                        m_demolishModalPending = true;
                    } else {
                        // Immediate demolish (confirm disabled).
                        if (m_sim) {
                            m_sim->demolishTile(m_demolishPendingTileX, m_demolishPendingTileZ);
                            if (m_mapTilesX > 0 && m_mapTilesZ > 0) {
                                uint64_t key = static_cast<uint64_t>(m_demolishPendingTileZ)
                                               * static_cast<uint64_t>(m_mapTilesX)
                                               + static_cast<uint64_t>(m_demolishPendingTileX);
                                m_overlayMap.erase(key);
                                if (m_renderer) {
                                    m_renderer->setZoneOverlay(m_mapTilesX, m_mapTilesZ,
                                                               m_overlayMap);
                                }
                            }
                            setUnsavedChanges(true);
                        }
                        m_renderer->clearDemolishHighlight();
                        m_demolishPendingTileX = -1;
                        m_demolishPendingTileZ = -1;
                    }
                } else {
                    // Mouse released on a different tile — cancel.
                    m_renderer->clearDemolishHighlight();
                    m_demolishPendingTileX = -1;
                    m_demolishPendingTileZ = -1;
                }
                m_lmbHeld = false;
                return true;  // Consumed.
            }

            m_lmbHeld = false;
            return false;  // Up-events are never consumed by world-interaction.
        }

        if (event.type == InputEvent::Type::MouseMove) {
            int hitX = -1, hitZ = -1;
            if (m_renderer->pickTerrainTile(event.physX, event.physY, hitX, hitZ)) {
                // Determine hover colour from active tool.
                // Zone tool: colour depends on selected zone type (res/com/ind).
                uint32_t colour = 0u;
                switch (m_activeTool) {
                    case ActiveTool::Zone:
                        switch (m_selectedZoneType) {
                            case 1:  colour = kHoverArgbZoneCom; break;  // Commercial
                            case 2:  colour = kHoverArgbZoneInd; break;  // Industrial
                            default: colour = kHoverArgbZoneRes; break;  // Residential
                        }
                        if (m_renderer) m_renderer->setZoneHoverColour(colour);
                        break;
                    case ActiveTool::Road:      colour = kHoverArgbRoad;      break;
                    case ActiveTool::Utilities: colour = kHoverArgbUtilities; break;
                    case ActiveTool::Demolish:  colour = kHoverArgbDemolish;  break;
                    case ActiveTool::Query:     colour = kHoverArgbQuery;     break;
                    default:                    colour = kHoverArgbClear;     break;
                }

                // Phase 11d Deliverable 5d: override hover colour to blocked red when
                // the Zone or Road tool is hovering over an already-occupied tile
                // (isRoad || isZoned).  Only applied to single-tile hover; drag rect
                // preview partitioning handles the multi-tile case below.
                //
                // Phase 11h: blocked state is signalled to the renderer via
                // setTileHoverHighlight footprintSize=0 (sentinel for "blocked").
                bool tileIsBlocked = false;
                if (m_sim &&
                    (m_activeTool == ActiveTool::Zone || m_activeTool == ActiveTool::Road) &&
                    !(m_lmbHeld && m_zoneAnchorX != -1)) {
                    QueryResult hoverQ = m_sim->queryTile(hitX, hitZ);
                    if (hoverQ.isRoad || hoverQ.isZoned) {
                        colour = kHoverArgbBlocked;
                        tileIsBlocked = true;
                    }
                }

                // Zone tool with anchor active: show rect preview instead of single-tile hover.
                if (m_lmbHeld && m_activeTool == ActiveTool::Zone && m_zoneAnchorX != -1) {
                    int x0 = std::min(m_zoneAnchorX, hitX);
                    int x1 = std::max(m_zoneAnchorX, hitX);
                    int z0 = std::min(m_zoneAnchorZ, hitZ);
                    int z1 = std::max(m_zoneAnchorZ, hitZ);
                    // Phase 11d Deliverable 5d: partition preview tiles into free vs blocked
                    // so the renderer shows free tiles in zone colour and blocked tiles in red.
                    std::vector<std::pair<int,int>> freeTiles;
                    std::vector<std::pair<int,int>> blockedTiles;
                    const size_t total = static_cast<size_t>((x1 - x0 + 1) * (z1 - z0 + 1));
                    freeTiles.reserve(total);
                    blockedTiles.reserve(total);
                    for (int tz = z0; tz <= z1; ++tz) {
                        for (int tx = x0; tx <= x1; ++tx) {
                            if (m_sim) {
                                QueryResult q = m_sim->queryTile(tx, tz);
                                if (q.isRoad || q.isZoned) {
                                    blockedTiles.push_back({tx, tz});
                                } else {
                                    freeTiles.push_back({tx, tz});
                                }
                            } else {
                                freeTiles.push_back({tx, tz});
                            }
                        }
                    }
                    m_renderer->setTileHoverHighlight(-1, -1);
                    m_renderer->setTilePlacementPreview(freeTiles, colour, blockedTiles);
                }
                // Road tool with anchor active: show axis-snapped line preview.
                else if (m_lmbHeld && m_activeTool == ActiveTool::Road && m_zoneAnchorX != -1) {
                    int dX = std::abs(hitX - m_zoneAnchorX);
                    int dZ = std::abs(hitZ - m_zoneAnchorZ);
                    // Phase 11d Deliverable 5d: partition road preview into free vs blocked.
                    std::vector<std::pair<int,int>> freeTiles;
                    std::vector<std::pair<int,int>> blockedTiles;
                    if (dX >= dZ) {
                        int x0 = std::min(m_zoneAnchorX, hitX);
                        int x1 = std::max(m_zoneAnchorX, hitX);
                        freeTiles.reserve(static_cast<size_t>(x1 - x0 + 1));
                        blockedTiles.reserve(static_cast<size_t>(x1 - x0 + 1));
                        for (int tx = x0; tx <= x1; ++tx) {
                            if (m_sim) {
                                QueryResult q = m_sim->queryTile(tx, m_zoneAnchorZ);
                                if (q.isRoad || q.isZoned) {
                                    blockedTiles.push_back({tx, m_zoneAnchorZ});
                                } else {
                                    freeTiles.push_back({tx, m_zoneAnchorZ});
                                }
                            } else {
                                freeTiles.push_back({tx, m_zoneAnchorZ});
                            }
                        }
                    } else {
                        int z0 = std::min(m_zoneAnchorZ, hitZ);
                        int z1 = std::max(m_zoneAnchorZ, hitZ);
                        freeTiles.reserve(static_cast<size_t>(z1 - z0 + 1));
                        blockedTiles.reserve(static_cast<size_t>(z1 - z0 + 1));
                        for (int tz = z0; tz <= z1; ++tz) {
                            if (m_sim) {
                                QueryResult q = m_sim->queryTile(m_zoneAnchorX, tz);
                                if (q.isRoad || q.isZoned) {
                                    blockedTiles.push_back({m_zoneAnchorX, tz});
                                } else {
                                    freeTiles.push_back({m_zoneAnchorX, tz});
                                }
                            } else {
                                freeTiles.push_back({m_zoneAnchorX, tz});
                            }
                        }
                    }
                    m_renderer->setTileHoverHighlight(-1, -1);
                    m_renderer->setTilePlacementPreview(freeTiles, colour, blockedTiles);
                }
                // All other cases: single-tile hover highlight (no drag preview).
                else {
                    m_renderer->setTilePlacementPreview({}, 0u, {});
                    // footprintSize: Zone tool uses density-tier footprint (1/2/3),
                    // all other tools use 1×1.
                    // footprintSize=0 is a sentinel meaning "blocked" (red highlight).
                    int hoverFootprint = 1;
                    if (tileIsBlocked) {
                        hoverFootprint = 0;  // Phase 11h: 0 = blocked sentinel → red highlight
                    } else if (m_activeTool == ActiveTool::Zone) {
                        // m_selectedDensityTier: 0=Low→1, 1=Med→2, 2=High→3
                        hoverFootprint = m_selectedDensityTier + 1;
                    } else if (m_activeTool == ActiveTool::Utilities) {
                        hoverFootprint = 2;  // Service buildings always have a 2×2 footprint
                    }
                    m_renderer->setTileHoverHighlight(hitX, hitZ, hoverFootprint);
                }

                // Zone, Road, Utilities, and Demolish all use click-only or deferred placement.
                // No tool places on drag (MouseMove while LMB held).

                m_hoveredTileX = hitX;
                m_hoveredTileZ = hitZ;
            } else {
                m_renderer->setTileHoverHighlight(-1, -1);
                m_renderer->setTilePlacementPreview({}, 0u, {});
                m_hoveredTileX = -1;
                m_hoveredTileZ = -1;
            }
            return false;  // MouseMove is never consumed by world-interaction.
        }

        if (event.type == InputEvent::Type::MouseButtonDown && event.button == 0) {
            m_lmbHeld = true;

            // Query tool left-click is handled at Priority 3 — it must not reach here.
            if (m_activeTool == ActiveTool::Query) {
                return false;
            }

            // Ray-cast from click position to find the terrain tile.
            // Use physical pixel coords — getRayFromScreenCoordinates() requires physical pixels.
            // Do not require a prior MouseMove — the click itself provides the screen coords.
            int hitX = -1, hitZ = -1;
            if (!m_renderer->pickTerrainTile(event.physX, event.physY, hitX, hitZ)) {
                return false;
            }

            // Update hover tile to the clicked position so that the drag throttle in the
            // MouseMove handler starts from the correct tile.  Without this, m_hoveredTileX
            // remains -1 after the initial click, causing the first MouseMove to the same
            // tile to evaluate the tile-change condition as true and double-place it.
            m_hoveredTileX = hitX;
            m_hoveredTileZ = hitZ;

            // Zone tool: record anchor on press — placement is deferred to LMB release
            // (rectangular fill of all tiles in [anchor, release] bounding box).
            // Clear any stale preview from a previous drag.
            if (m_activeTool == ActiveTool::Zone) {
                m_zoneAnchorX = hitX;
                m_zoneAnchorZ = hitZ;
                m_renderer->setTilePlacementPreview({}, 0u, {});
                return true;  // Consumed: anchor recorded; no placement yet.
            }

            // Road tool: record anchor on press — placement is deferred to LMB release
            // (straight-line along the dominant axis from anchor to release tile).
            // Clear any stale preview from a previous drag.
            if (m_activeTool == ActiveTool::Road) {
                m_zoneAnchorX = hitX;
                m_zoneAnchorZ = hitZ;
                m_renderer->setTilePlacementPreview({}, 0u, {});
                return true;  // Consumed: anchor recorded; no placement yet.
            }

            // Phase 11h: Demolish tool — record pending tile on mouse-down.
            // The demolish confirmation modal (or immediate demolish) is triggered on mouse-up.
            if (m_activeTool == ActiveTool::Demolish) {
                m_demolishPendingTileX = hitX;
                m_demolishPendingTileZ = hitZ;
                // The renderer will use demolish red color (ToolMode::Demolish is already set).
                m_renderer->setTileHoverHighlight(hitX, hitZ, 1);
                return true;  // Consumed: pending tile recorded; no placement yet.
            }

            return doTerrainPlacement(hitX, hitZ);
        }
    }

    return false;
}

// ----------------------------------------------------------------
// doTerrainPlacement — execute placement at (hitX, hitZ) for the
// current active tool.  Called from both the MouseButtonDown handler
// and the MouseMove drag handler (when LMB is held and tile changes).
// Performs earthworks cost computation, slope guard, sim dispatch,
// and overlay update.  Returns true when the event should be consumed.
// ----------------------------------------------------------------
bool UIManager::doTerrainPlacement(int hitX, int hitZ) {
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
            // Phase 11m: rebuild road mesh scene nodes adjacent to the zone footprint.
            // placeZone() flattened their terrain heights; placeRoadMesh() rebakes
            // road vertex Y positions from the updated terrain.
            if (m_renderer && m_sim) {
                const int N = m_selectedDensityTier + 1;  // footprint: Low=1, Med=2, High=3
                for (int dx = -1; dx <= N; ++dx) {
                    for (int dz = -1; dz <= N; ++dz) {
                        // Skip the zone footprint itself — only the border ring.
                        if (dx >= 0 && dx < N && dz >= 0 && dz < N) continue;
                        int nx = hitX + dx;
                        int nz = hitZ + dz;
                        QueryResult q = m_sim->queryTile(nx, nz);
                        if (q.isRoad) {
                            m_renderer->placeRoadMesh(nx, nz);
                        }
                    }
                }
            }
            if (m_mapTilesX > 0 && m_mapTilesZ > 0) {
                // Phase 11m: insert overlay entry showing the zone color for this
                // under-construction tile. The tile starts in underConstruction state;
                // the periodic overlay refresh in update() will remove it once the
                // building has spawned (underConstruction becomes false).
                uint64_t key = static_cast<uint64_t>(hitZ) * static_cast<uint64_t>(m_mapTilesX)
                               + static_cast<uint64_t>(hitX);
                ZoneType  zoneType   = static_cast<ZoneType>(m_selectedZoneType);
                DensityTier densityTier = static_cast<DensityTier>(m_selectedDensityTier);
                static constexpr size_t kOverlayCap = 100000u;
                if (m_overlayMap.size() < kOverlayCap) {
                    m_overlayMap[key] = computeZoneOverlayColor(zoneType, densityTier);
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
            // Phase 11h: Demolish is handled at mouse-up (not mouse-down).
            // doTerrainPlacement() is no longer called for Demolish — this case
            // should not be reached in normal flow. No-op defensively.
            break;
        }
        default:
            break;
    }
    return true;
}

// ----------------------------------------------------------------
// updateSubPanelVisibility — show/hide Zone and Utilities sub-panels
// based on the current active tool.  Called whenever m_activeTool changes.
//
// Phase 10 audio: fire ui_menu_open/ui_menu_close when a sub-panel transitions.
// Close sounds precede open sounds in the same call (per phase-10.md).
// Transition detection uses isElementVisible() on the first valid handle before
// the setElementVisible() loop, so both Zone and Utilities are checked before
// any visibility is changed — ensuring close fires before open on the same frame.
// ----------------------------------------------------------------
void UIManager::updateSubPanelVisibility() {
    if (!m_backend) return;

    bool showZone = (m_activeTool == ActiveTool::Zone);
    bool showUtil = (m_activeTool == ActiveTool::Utilities);

    // Phase 10: sample current visibility BEFORE making any changes, so that
    // close/open transitions can be detected correctly.
    bool zoneWasVisible = false;
    for (int i = 0; i < 9; ++i) {
        if (m_zoneSubPanelBtns[i] != kInvalidUIElement) {
            zoneWasVisible = m_backend->isElementVisible(m_zoneSubPanelBtns[i]);
            break;
        }
    }
    bool utilWasVisible = false;
    for (int i = 0; i < 4; ++i) {
        if (m_utilSubPanelBtns[i] != kInvalidUIElement) {
            utilWasVisible = m_backend->isElementVisible(m_utilSubPanelBtns[i]);
            break;
        }
    }

    // Phase 10: fire close sounds FIRST (before any setElementVisible calls).
    // Guard: m_audio != nullptr; only fire when the panel is transitioning.
    if (m_audio) {
        if (zoneWasVisible && !showZone) {
            m_audio->playSound(UI_MENU_CLOSE, SoundPriority::NORMAL, 1.0f);
        }
        if (utilWasVisible && !showUtil) {
            m_audio->playSound(UI_MENU_CLOSE, SoundPriority::NORMAL, 1.0f);
        }
    }

    // Apply visibility to all sub-panel elements.
    for (int i = 0; i < 9; ++i) {
        if (m_zoneSubPanelBtns[i] != kInvalidUIElement) {
            m_backend->setElementVisible(m_zoneSubPanelBtns[i], showZone);
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (m_utilSubPanelBtns[i] != kInvalidUIElement) {
            m_backend->setElementVisible(m_utilSubPanelBtns[i], showUtil);
        }
    }

    // Phase 10: fire open sounds AFTER setElementVisible (per spec: "immediately after").
    if (m_audio) {
        if (!zoneWasVisible && showZone) {
            m_audio->playSound(UI_MENU_OPEN, SoundPriority::NORMAL, 1.0f);
        }
        if (!utilWasVisible && showUtil) {
            m_audio->playSound(UI_MENU_OPEN, SoundPriority::NORMAL, 1.0f);
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
    if (m_hud && m_hud->getFinances()) m_hud->getFinances()->draw();  // slot 4 — finances panel (toggled by T)
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
            if (!m_gameSessionActive) {
                // First game: transition directly to gameplay.
                transitionToGameplay(GameMode::Sandbox);
            } else {
                // Subsequent game: defer until main.cpp polls consumeNewGameRequest().
                // Store parameters from the MainMenuPanel selection.
                m_newGameParams.mapSize   = m_mainMenu ? m_mainMenu->getSelectedMapSize()
                                                       : MapSize::kMedium;
                m_newGameParams.seed      = 0;  // V1: fixed seed; may be extended later
                m_newGameParams.difficulty = 1; // V1: Normal hardcoded; difficulty UI is future
                m_newGamePending = true;
                if (m_mainMenu) m_mainMenu->showLoadingScreen();
            }
            return; // Skip the rest of this frame's update — state just changed.
        }
        if (m_mainMenu->consumeLoadGameRequest()) {
            if (m_saveSystem) {
                LoadResult result = m_saveSystem->loadMostRecentSave();
                if (result.ok) {
                    m_pendingLoadJson  = result.jsonData;
                    m_loadGamePending  = true;
                    if (m_mainMenu) m_mainMenu->showLoadingScreen();
                }
            }
            return;
        }
        if (m_mainMenu->consumeSettingsRequest()) {
            showSettings();
        }
        if (m_mainMenu->consumeQuitRequest()) {
            m_quitRequested = true;
            return;
        }
    }

    // 1c. Poll PauseMenuPanel for save/quit requests.
    if (m_pauseMenu) {
        // Manual save: call saveToSlot(1) if SaveSystem is available.
        // Post-V1: slot picker dialog
        if (m_pauseMenu->consumeSaveRequest()) {
            if (m_saveSystem) {
                SaveResult result = m_saveSystem->saveToSlot(1);
                if (result.ok) {
                    setUnsavedChanges(false);
                } else {
                    // Manual save failure: show blocking Retry/Cancel modal.
                    if (m_modal) {
                        m_pendingSaveFailure = true;
                        m_modal->showSaveFailure(result.error);
                    }
                }
            }
        }

        // Quit to Desktop: check unsaved-changes guard.
        if (m_pauseMenu->consumeQuitDesktopRequest()) {
            if (m_hasUnsavedChanges && m_modal) {
                m_pendingQuit = PendingQuitAction::Desktop;
                m_modal->showUnsavedQuit(true);
            } else {
                m_quitRequested = true;
            }
            return;
        }

        // Quit to Main Menu: check unsaved-changes guard.
        if (m_pauseMenu->consumeQuitToMenuRequest()) {
            if (m_hasUnsavedChanges && m_modal) {
                m_pendingQuit = PendingQuitAction::ToMenu;
                m_modal->showUnsavedQuit(false);
            } else {
                transitionToMainMenu();
            }
            return;
        }
    }

    // 1d. Handle pending quit action after unsaved-changes modal closes.
    if (m_pendingQuit != PendingQuitAction::None && m_modal && !m_modal->isActive()) {
        auto result  = m_modal->getLastResult();
        auto pending = m_pendingQuit;
        m_pendingQuit = PendingQuitAction::None;

        if (result == ModalDialog::DialogResult::Accept) {
            // "Save and Quit" — save first then dispatch.
            if (m_saveSystem) {
                m_saveSystem->autoSave();
                setUnsavedChanges(false);
            }
            if (pending == PendingQuitAction::Desktop) {
                m_quitRequested = true;
            } else {
                transitionToMainMenu();
            }
            return;
        }
        if (result == ModalDialog::DialogResult::Decline) {
            // "Quit Without Saving" — dispatch immediately.
            if (pending == PendingQuitAction::Desktop) {
                m_quitRequested = true;
            } else {
                transitionToMainMenu();
            }
            return;
        }
        // DialogResult::Cancel — do nothing, user stays in the game.
    }

    // 1d2. Handle save-failure modal result (Retry / Cancel).
    if (m_pendingSaveFailure && m_modal && !m_modal->isActive()) {
        m_pendingSaveFailure = false;
        auto result = m_modal->getLastResult();
        if (result == ModalDialog::DialogResult::Accept && m_saveSystem) {
            // Retry: call saveToSlot(1) again.
            SaveResult retryResult = m_saveSystem->saveToSlot(1);
            if (retryResult.ok) {
                setUnsavedChanges(false);
            } else {
                // Second failure: show modal again.
                m_pendingSaveFailure = true;
                m_modal->showSaveFailure(retryResult.error);
            }
        }
        // Cancel: modal dismissed; unsaved-changes dot remains visible.
    }

    // 1d3. Poll demolish confirmation modal result.
    if (m_demolishModalPending && m_modal && !m_modal->isActive()) {
        m_demolishModalPending = false;
        auto result = m_modal->pollResult();
        if (result == ModalDialog::DialogResult::Accept) {
            if (m_sim && m_demolishPendingTileX != -1) {
                m_sim->demolishTile(m_demolishPendingTileX, m_demolishPendingTileZ);
                // Remove from overlay map and update zone overlay.
                if (m_mapTilesX > 0 && m_mapTilesZ > 0 && m_renderer) {
                    uint64_t key = static_cast<uint64_t>(m_demolishPendingTileZ)
                                   * static_cast<uint64_t>(m_mapTilesX)
                                   + static_cast<uint64_t>(m_demolishPendingTileX);
                    m_overlayMap.erase(key);
                    m_renderer->setZoneOverlay(m_mapTilesX, m_mapTilesZ, m_overlayMap);
                }
                setUnsavedChanges(true);
            }
            if (m_renderer) m_renderer->clearDemolishHighlight();
        } else {
            // No or Cancel — clear highlight only.
            if (m_renderer) m_renderer->clearDemolishHighlight();
        }
        m_demolishPendingTileX = -1;
        m_demolishPendingTileZ = -1;
    }

    // 1e. Forward budget ticks from CitySimulation to SaveSystem (5-tick auto-save gate).
    if (m_sim && m_saveSystem) {
        int ticks = m_sim->consumeBudgetTicks();
        for (int i = 0; i < ticks; ++i) {
            m_saveSystem->onBudgetTick();
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

    // Month 1 edge: post CRITICAL "2 months to bankruptcy" toast and auto-slow to 1×
    // (player retains speed selector control — per architecture/game-design/game-over-flow.md).
    if (currentMonths == 1 && m_lastDeficitMonths < 1) {
        m_notifications->postCritical(
            "City Finances Critical",
            "2 months to bankruptcy if deficit continues");
        if (m_sim) {
            m_sim->setSpeed(SpeedMultiplier::x1);
            if (m_audio) m_audio->setSpeed(SimSpeed::x1);
        }
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

    // Month 3+ edge: transition to game-over (Scenario mode only — guard at call site
    // per phase-11.md spec; transitionToGameOver() contains no internal GameMode check).
    if (currentMonths >= 3 && m_lastDeficitMonths < 3 && m_gameMode == GameMode::Scenario) {
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
    // 4b. STINGER_MILESTONE POLLING (Phase 11 per-frame dispatch)
    // Per-frame getCityRating() edge-detect replaces the Phase 10
    // notification-based dispatch to avoid double-fire on load.
    // m_previousCityRating is seeded by onGameLoaded() so no spurious
    // stinger fires when a saved game is first loaded.
    // =============================================================
    {
        CityRatingTier currentRating = m_sim->getCityRating();
        if (currentRating > m_previousCityRating) {
            if (m_audio && m_clock) {
                double now = m_clock->nowSeconds();
                if ((now - m_lastMilestoneStingerFireTime) >= kStingerCooldownSeconds) {
                    m_audio->triggerStinger(StingerType::MILESTONE);
                    m_lastMilestoneStingerFireTime = now;
                }
            }
        }
        m_previousCityRating = currentRating;
    }

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
                // Phase 11m: populationTick triggers an immediate overlay refresh
                // so that buildings that just spawned are removed from the overlay
                // without waiting for the 60-frame counter.
                if (!m_overlayMap.empty() && m_renderer && m_mapTilesX > 0 && m_mapTilesZ > 0) {
                    bool anyRemoved = false;
                    for (auto it = m_overlayMap.begin(); it != m_overlayMap.end(); ) {
                        int tileX = static_cast<int>(it->first % static_cast<uint64_t>(m_mapTilesX));
                        int tileZ = static_cast<int>(it->first / static_cast<uint64_t>(m_mapTilesX));
                        QueryResult qr = m_sim->queryTile(tileX, tileZ);
                        if (!qr.isZoned || !qr.underConstruction) {
                            it = m_overlayMap.erase(it);
                            anyRemoved = true;
                        } else {
                            ++it;
                        }
                    }
                    if (anyRemoved) {
                        m_renderer->setZoneOverlay(m_mapTilesX, m_mapTilesZ, m_overlayMap);
                    }
                }
                m_overlayRefreshCounter = 0;
                break;

            case NotificationType::CityRatingTransition:
                m_notifications->postNormal(
                    "City Rating Changed",
                    "City rating tier has changed!");
                // Phase 11: stinger_milestone is now dispatched via per-frame
                // getCityRating() polling in section 4b above — NOT from this
                // notification handler.  Removing the stinger call here prevents
                // double-fire: both paths would otherwise fire on the same transition.
                break;

            case NotificationType::NeighbourCleared:
                m_notifications->postNormal(
                    "Neighbour Cleared",
                    "Neighbouring building cleared for density upgrade.");
                break;

            case NotificationType::UpgradeBlocked:
                m_notifications->postCritical(
                    "Upgrade Blocked",
                    "Upgrade blocked — clear surrounding tiles.");
                break;

            case NotificationType::PlacementBlocked:
                m_notifications->postNormal(
                    "Placement Blocked",
                    "Cannot place here — check for occupied tiles or road proximity.");
                break;

            case NotificationType::BuildingAbandoned:
                m_notifications->postNormal(
                    "Building Abandoned",
                    "Building abandoned — too far from road.");
                break;

            case NotificationType::BuildingRecovered:
                m_notifications->postNormal(
                    "Building Recovered",
                    "Building recovered — road reconnected.");
                break;
        }
    }

    // =============================================================
    // 7. OVERLAY REFRESH (Phase 11m)
    // Periodic removal of construction overlay entries whose buildings
    // have already spawned (underConstruction == false).
    // Counter increments each frame; refresh runs at 60 frames.
    // PopulationMilestone notification (section 6) also triggers an
    // immediate refresh and resets this counter.
    // =============================================================
    if (!m_overlayMap.empty() && m_renderer && m_mapTilesX > 0 && m_mapTilesZ > 0) {
        ++m_overlayRefreshCounter;
        if (m_overlayRefreshCounter >= 60) {
            m_overlayRefreshCounter = 0;
            bool anyRemoved = false;
            for (auto it = m_overlayMap.begin(); it != m_overlayMap.end(); ) {
                int tileX = static_cast<int>(it->first % static_cast<uint64_t>(m_mapTilesX));
                int tileZ = static_cast<int>(it->first / static_cast<uint64_t>(m_mapTilesX));
                QueryResult qr = m_sim->queryTile(tileX, tileZ);
                if (!qr.isZoned || !qr.underConstruction) {
                    it = m_overlayMap.erase(it);
                    anyRemoved = true;
                } else {
                    ++it;
                }
            }
            if (anyRemoved) {
                m_renderer->setZoneOverlay(m_mapTilesX, m_mapTilesZ, m_overlayMap);
            }
        }
    }
}

// ----------------------------------------------------------------
// State-transition methods — full Phase 8 implementation.
// ----------------------------------------------------------------

void UIManager::transitionToPaused() {
    m_state = GameState::Paused;
    // Auto-save before displaying the pause menu (per save-system.md).
    if (m_saveSystem) {
        m_saveSystem->onPauseMenuOpened();
    }
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
    // Primary guard is at the call site (UIManager::update() deficit-streak section)
    // per phase-11.md spec. Belt-and-suspenders guard here as well so that any
    // direct call (e.g. from test fixtures or future code paths) is also safe in
    // Sandbox mode.
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
    // Auto-save immediately before showing the forced-loan modal (per save-system.md).
    if (m_saveSystem) {
        m_saveSystem->onForcedLoanDialogActive();
    }

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

    // Hide main menu, show HUD and minimap.
    m_mainMenu->hide();
    m_hud->show();
    m_minimap->show();

    // Phase 11m: notify HUD that a new game session has started so the grace period
    // indicator resets correctly for both first-game and subsequent new games.
    if (m_hud) m_hud->notifyGameStarted();

    // Transition audio from menu to gameplay.
    // setTimeOfDay MUST be called before transitionToGameplay() so the ambient bed
    // selection reads the correct time period. New games always start at DAY.
    if (m_audio) {
        m_audio->setTimeOfDay(TimeOfDay::DAY);
        m_audio->transitionToGameplay();
    }

    // Phase 11m: mark the game session as active so subsequent new-game requests
    // take the pending-flag path (loading screen) instead of transitioning directly.
    m_gameSessionActive = true;
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
    // -----------------------------------------------------------------------
    // MANDATORY CALL ORDER (Phase 11m spec):
    //   1. m_audio->transitionToMainMenu() — MUST BE FIRST: stops all gameplay
    //      audio before any UI state changes occur.
    //   2. onNewGame()                     — clear overlay + tool state.
    //   3. Save-state refresh              — re-query SaveFileState for the
    //      Load Game button.
    //   4. m_mainMenu->show()              — reveal the main menu.
    //
    // NOTE: m_gameSessionActive is NOT reset here. It remains true so that the
    // next handleNewGameRequest() / consumeStartGameRequest() correctly takes
    // the subsequent-game path (loading screen + pending flag).
    // -----------------------------------------------------------------------

    // Step 1: Stop all gameplay audio FIRST.
    if (m_audio) {
        m_audio->transitionToMainMenu();
    }

    // Hide gameplay panels.
    m_hud->hide();
    m_minimap->hide();
    m_pauseMenu->hide();
    if (m_hud && m_hud->getFinances() && m_hud->getFinances()->isOpen()) {
        m_hud->getFinances()->close();
    }
    m_financesPanelOpen = false;
    m_inspector->hide();
    m_inspectorOpen = false;
    m_state = GameState::MainMenu;

    // Step 2: Clear overlay and tool state.
    onNewGame();

    // Step 3: Re-query save state so the Load Game button reflects any saves
    // made this session.
    if (m_saveSystem && m_mainMenu) {
        SaveFileState state = m_saveSystem->getSaveFileState();
        m_mainMenu->setSaveAvailable(state == SaveFileState::Valid);
        switch (state) {
            case SaveFileState::NoSaves:
                m_mainMenu->setSaveStatusText("No saves found.");
                break;
            case SaveFileState::AllCorrupt:
                m_mainMenu->setSaveStatusText(
                    "Save data is corrupted — cannot load. Check "
                    + m_saveSystem->getSaveDirectoryPath() + " for recovery.");
                break;
            case SaveFileState::Valid:
                m_mainMenu->setSaveStatusText("");
                break;
        }
    }

    // Step 4: Show main menu.
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
    if (m_renderer) m_renderer->setActiveTool(static_cast<ToolMode>(m_activeTool));
    m_hoveredTileX = -1;
    m_hoveredTileZ = -1;
    m_zoneAnchorX  = -1;
    m_zoneAnchorZ  = -1;
    m_lmbHeld      = false;
    if (m_renderer) m_renderer->setTilePlacementPreview({}, 0u, {});
    if (m_renderer) m_renderer->setTileHoverHighlight(-1, -1);
    updateSubPanelVisibility();
}

// ----------------------------------------------------------------
// Phase 11: onGameLoaded — seed stinger and deficit caches after load.
// Called by the loading controller (main.cpp) once per load path
// (New Game / Load Last Save) before the first update() tick.
// ----------------------------------------------------------------
void UIManager::onGameLoaded() {
    if (m_sim) {
        // Seed m_previousCityRating so the stinger_milestone per-frame
        // detector does not fire a spurious MILESTONE on the first tick.
        m_previousCityRating = m_sim->getCityRating();
        // Seed m_lastDeficitMonths so deficit-streak warnings do not
        // retroactively fire for months already counted before load.
        m_lastDeficitMonths = m_sim->getConsecutiveDeficitMonths();
    }
}

// zoneAssetNameForLoad — map (ZoneType, DensityTier) to the _01 asset base name.
// Mirrors CitySimulation::zoneAssetBaseName() (Phase 10 policy: always _01 suffix).
static std::string zoneAssetNameForLoad(ZoneType zone, DensityTier density) {
    const char* zoneStr = nullptr;
    const char* densStr = nullptr;
    switch (zone) {
        case ZoneType::Residential: zoneStr = "res"; break;
        case ZoneType::Commercial:  zoneStr = "com"; break;
        case ZoneType::Industrial:  zoneStr = "ind"; break;
    }
    switch (density) {
        case DensityTier::Low:    densStr = "low";  break;
        case DensityTier::Medium: densStr = "med";  break;
        case DensityTier::High:   densStr = "high"; break;
    }
    if (!zoneStr || !densStr) return {};
    return std::string(zoneStr) + "_" + densStr + "_01";
}

// rebuildCityFromSim — recreate all renderer scene nodes from current sim state.
// Called from main.cpp after applyLoadedJson() and terrain regeneration complete.
void UIManager::rebuildCityFromSim() {
    if (!m_renderer || !m_sim || m_mapTilesX <= 0 || m_mapTilesZ <= 0) return;
    m_renderer->clearCity();
    m_overlayMap.clear();
    static constexpr size_t kOverlayCapLoad = 100000u;
    for (int z = 0; z < m_mapTilesZ; ++z) {
        for (int x = 0; x < m_mapTilesX; ++x) {
            QueryResult q = m_sim->queryTile(x, z);
            if (q.isRoad) {
                m_renderer->placeRoadMesh(x, z);
            } else if (q.isZoned && !q.underConstruction && !q.isAbandoned
                       && q.footprintOriginX == -1) {
                // This tile is a building origin (or 1×1) — place its mesh.
                std::string asset = zoneAssetNameForLoad(q.zoneType, q.densityTier);
                if (!asset.empty()) {
                    int varNum = q.buildingVariantNum;
                    if (varNum < 1 || varNum > 4) {
                        // Old save: no variant stored. Use position-based fallback for visual variety.
                        varNum = ((x * 7 + z * 13) % 4) + 1;  // deterministic 1-4
                    }
                    if (asset.size() >= 2) {
                        asset[asset.size() - 2] = '0';
                        asset[asset.size() - 1] = static_cast<char>('0' + varNum);
                    }
                    m_renderer->placeBuildingMesh(x, z, asset);
                }
            } else if (q.isZoned && q.underConstruction
                       && m_overlayMap.size() < kOverlayCapLoad) {
                // Restore zone overlay for unbuilt tiles.
                uint64_t key = static_cast<uint64_t>(z)
                             * static_cast<uint64_t>(m_mapTilesX) + x;
                m_overlayMap[key] = computeZoneOverlayColor(q.zoneType, q.densityTier);
            }
        }
    }
    if (m_renderer && m_mapTilesX > 0 && m_mapTilesZ > 0) {
        m_renderer->setZoneOverlay(m_mapTilesX, m_mapTilesZ, m_overlayMap);
    }
}

// ----------------------------------------------------------------
// Phase 11: loadKeybindings — probe and (future) parse keybindings.json.
// ----------------------------------------------------------------
void UIManager::loadKeybindings() {
    char path[512] = {};
#if defined(_WIN32)
    const char* appdata = getenv("APPDATA");
    if (!appdata || appdata[0] == '\0') {
        // APPDATA unset (service account, etc.): silently use defaults.
        return;
    }
    snprintf(path, sizeof(path), "%s\\aitown\\keybindings.json", appdata);
#else
    const char* home = getenv("HOME");
    if (!home || home[0] == '\0') {
        // HOME unset (container without user env): silently use defaults.
        return;
    }
    snprintf(path, sizeof(path), "%s/.config/aitown/keybindings.json", home);
#endif

    FILE* f = fopen(path, "r");
    if (!f) {
        // Absent on first run — silently use defaults, do NOT warn.
        return;
    }
    fclose(f);

    // File exists: parse and apply overrides.
    m_keyBindings.load(path);

    // Propagate loaded bindings to SettingsPanel so Controls tab reopens correctly.
    if (m_settings) m_settings->setCurrentBindings(m_keyBindings);
}

// ----------------------------------------------------------------
// Phase 11c: saveKeybindings — write m_keyBindings to disk.
// ----------------------------------------------------------------
void UIManager::saveKeybindings(const KeyBindings& b) {
    m_keyBindings.copyMutableFrom(b);

    char path[512] = {};
#if defined(_WIN32)
    const char* appdata = getenv("APPDATA");
    if (!appdata || appdata[0] == '\0') return;
    snprintf(path, sizeof(path), "%s\\aitown\\keybindings.json", appdata);
#else
    const char* home = getenv("HOME");
    if (!home || home[0] == '\0') return;
    snprintf(path, sizeof(path), "%s/.config/aitown/keybindings.json", home);
#endif

    m_keyBindings.writeToFile(path);
}

// ----------------------------------------------------------------
// Phase 11c: applyKeybindings — update in-memory bindings, persist, update panel.
// ----------------------------------------------------------------
void UIManager::applyKeybindings(const KeyBindings& b) {
    saveKeybindings(b);
    // Inform SettingsPanel of the newly-applied bindings so the Controls tab
    // reopens with the correct state next time.
    if (m_settings) m_settings->setCurrentBindings(b);
}

// ----------------------------------------------------------------
// Phase 11: setSaveSystem — bind the SaveSystem for manual save and quit-guard.
// ----------------------------------------------------------------
void UIManager::setSaveSystem(ISaveSystem* saveSystem) {
    m_saveSystem = saveSystem;
}

// ----------------------------------------------------------------
// Phase 11: setSaveAvailable — forward to MainMenuPanel.
// ----------------------------------------------------------------
void UIManager::setSaveAvailable(bool available) {
    if (m_mainMenu) {
        m_mainMenu->setSaveAvailable(available);
    }
}

// ----------------------------------------------------------------
// Phase 11: setSaveStatusText — forward status label text to MainMenuPanel.
// ----------------------------------------------------------------
void UIManager::setSaveStatusText(const std::string& text) {
    if (m_mainMenu) {
        m_mainMenu->setSaveStatusText(text);
    }
}

// ----------------------------------------------------------------
// Phase 11l: getPendingMapTiles — return selected map size as tile count.
// Returns the integer value of MapSize enum (128, 512, or 1024).
// main.cpp passes this to TerrainSystem::generate() when starting a new game.
// ----------------------------------------------------------------
int UIManager::getPendingMapTiles() const {
    if (m_mainMenu) {
        return static_cast<int>(m_mainMenu->getSelectedMapSize());
    }
    return static_cast<int>(MapSize::kMedium);  // default
}

// ----------------------------------------------------------------
// Phase 11m: consumeNewGameRequest — return m_newGamePending and reset it.
// ----------------------------------------------------------------
bool UIManager::consumeNewGameRequest() {
    if (m_newGamePending) {
        m_newGamePending = false;
        return true;
    }
    return false;
}

// ----------------------------------------------------------------
// Phase 11m: getNewGameParams — return stored new-game parameters.
// ----------------------------------------------------------------
NewGameParams UIManager::getNewGameParams() {
    return m_newGameParams;
}

// ----------------------------------------------------------------
// Phase 11m: consumeLoadGameRequest — atomically consume pending load-game JSON.
// ----------------------------------------------------------------
bool UIManager::consumeLoadGameRequest(std::string& jsonOut) {
    if (!m_loadGamePending) return false;
    m_loadGamePending = false;
    jsonOut = std::move(m_pendingLoadJson);
    m_pendingLoadJson.clear();
    return true;
}

#ifdef AITOWN_TESTING_ENABLED
// ----------------------------------------------------------------
// Phase 11m: handleNewGameRequest — test seam to inject new-game params.
// ----------------------------------------------------------------
void UIManager::handleNewGameRequest(const NewGameParams& params) {
    m_newGameParams = params;
    if (m_gameSessionActive) {
        m_newGamePending = true;
        if (m_mainMenu) m_mainMenu->showLoadingScreen();
    } else {
        transitionToGameplay(GameMode::Sandbox);
    }
}

// ----------------------------------------------------------------
// Phase 11m: setGameSessionActiveForTest — directly set m_gameSessionActive.
// ----------------------------------------------------------------
void UIManager::setGameSessionActiveForTest(bool value) {
    m_gameSessionActive = value;
}
#endif  // AITOWN_TESTING_ENABLED
