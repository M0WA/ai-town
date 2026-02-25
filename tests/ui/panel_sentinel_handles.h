#pragma once
#include "src/ui/IUIBackend.h"

// Test-only sentinel handles for UIManagerDrawOrderTest.
// Production code MUST NOT include this header.
//
// Values mirror the kSentinel constants embedded in each panel's draw() method.
// Verified against panel header files and NotificationManager.cpp:
//   main_menu_panel.h   -> 0xDEAD0101u
//   minimap.h           -> 0xDEAD0102u
//   tax_rate_panel.h    -> 0xDEAD0103u
//   inspector_panel.h   -> 0xDEAD0104u
//   NotificationManager.cpp (kNotifSentinel) -> 0xDEAD0105u
//   hud.h               -> 0xDEAD0106u
//   pause_menu_panel.h  -> 0xDEAD0107u
//   settings_panel.h    -> 0xDEAD0108u
//   modal_dialog.h      -> 0xDEAD0109u
//
// Draw slot order (verified against UIManager.cpp::draw()):
//   slot 1:  m_mainMenu->draw()       -> kMainMenuSentinel
//   slot 2:  m_minimap->draw()        -> kMinimapSentinel
//   slot 3:  m_hud->draw()            -> kHudSentinel
//   slot 4:  m_taxPanel->draw()       -> kTaxPanelSentinel
//   slot 5:  m_inspector->draw()      -> kInspectorSentinel
//   slot 6:  m_notifications->draw()  -> kNotificationSentinel
//   slot 7:  m_pauseMenu->draw()      -> kPauseMenuSentinel
//   slot 8:  m_settings->draw()       -> kSettingsSentinel
//   slot 9:  scrim (m_scrimHandle=0)  -> fires only when modal isActive()
//   slot 10: m_modal->draw()          -> kModalSentinel (visible=m_active)
//
// NOTE: kHudSentinel is 0xDEAD0106u (slot 3, not slot 6).
// The draw order is NOT numerically sequential by sentinel value.
constexpr UIElementHandle kMainMenuSentinel     = 0xDEAD0101u;  // MainMenuPanel   — slot 1
constexpr UIElementHandle kMinimapSentinel      = 0xDEAD0102u;  // Minimap         — slot 2
constexpr UIElementHandle kTaxPanelSentinel     = 0xDEAD0103u;  // TaxRatePanel    — slot 4
constexpr UIElementHandle kInspectorSentinel    = 0xDEAD0104u;  // InspectorPanel  — slot 5
constexpr UIElementHandle kNotificationSentinel = 0xDEAD0105u;  // NotificationMgr — slot 6
constexpr UIElementHandle kHudSentinel          = 0xDEAD0106u;  // HUD             — slot 3
constexpr UIElementHandle kPauseMenuSentinel    = 0xDEAD0107u;  // PauseMenuPanel  — slot 7
constexpr UIElementHandle kSettingsSentinel     = 0xDEAD0108u;  // SettingsPanel   — slot 8
constexpr UIElementHandle kModalSentinel        = 0xDEAD0109u;  // ModalDialog     — slot 10
