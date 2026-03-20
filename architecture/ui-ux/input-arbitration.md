# Input Arbitration (Focus Management)

Events flow through `IEventReceiver` in this priority order — each handler returns `true` (consumed) or `false` (pass-through):

1. `ModalDialog` (highest priority — blocks all others while active, including Ctrl+Z and Escape). **Camera input exception**: `ModalDialog` does NOT consume camera movement events (middle-mouse-button drag, right-mouse-button drag, scroll-wheel zoom). These pass through to `CameraController` at Priority 6 regardless of modal state. **Rationale**: camera movement is non-destructive; the player benefits from being able to zoom and pan while reading a blocking modal. All keyboard events, left-mouse clicks, and right-click context events ARE consumed by the modal. **Scrim event consumption**: When a blocking modal is active, the full-screen scrim element rendered by `UIManager` must consume left-click and right-click context events in `UIManager::onEvent()` at Priority 1. Camera pass-through events (scroll wheel, middle-mouse-button drag, right-mouse-button drag for pan) must NOT be consumed by the scrim. The scrim is not merely a visual overlay — it must be an event-consuming element that prevents zone placement and road tools from receiving clicks while a modal is shown. Window focus events (`WindowFocusGained`, `WindowFocusLost`) are additionally exempt from the short-circuit rule — they must always reach `CameraController` so `m_appHasFocus` is kept accurate regardless of modal state. `UIManager` consuming a `WindowFocusLost` event would prevent `CameraController` from suppressing edge-scroll on Alt+Tab. **RMB drag initiation forwarding contract**: The initial `MouseButtonDown button=1` (right-mouse-button press) is dispatched to `UIManager::onEvent()` first. If `UIManager` returns `false` (scrim not active AND no tool is active), the platform adapter sets `m_rmbDragActive = true` AND forwards the event to `CameraController::onInputEvent()`. If `UIManager` returns `true` — either because the scrim consumed it (modal active) OR because the tool-deselect handler (Priority 6b/7e) consumed it (a tool was active and has now been deselected) — `m_rmbDragActive` is NOT set and the event does not reach `CameraController`. In the tool-deselect case this is intentional: the right-click that cancels the active tool should not also begin a camera drag. Subsequent `MouseMove` events while `m_rmbDragActive == true` always pass through to `CameraController` unconditionally. Without forwarding the `MouseButtonDown button=1` to `CameraController` when the scrim is inactive, `CameraController::m_rmbDragActive` is never set to `true` and RMB drag is permanently non-functional.
2. **CRITICAL toast dismiss** — click, Enter, or Delete while a CRITICAL toast is in the foreground are consumed by `NotificationManager` before any other handler sees them. This prevents an Enter key meant to dismiss a CRITICAL toast from accidentally activating a toolbar button behind it. **Priority 2 is entirely skipped when: (a) no CRITICAL toast is currently visible, OR (b) a blocking modal is currently active** (Priority 1 holds full input control; CRITICAL toasts are hidden while a modal is active per Notification System spec, so this condition naturally holds — but must be enforced explicitly to avoid Priority 2 consuming events that the modal should see if toast visibility state is transiently inconsistent). In both cases, events fall through to lower priorities normally. **Implementation contract**: Both conditions MUST be evaluated independently as a compound guard. The correct guard is:

```cpp
// Priority 2 dual-guard — BOTH conditions required
bool criticalVisible = m_notifications->hasCriticalToastVisible();
bool modalActive     = m_modal && m_modal->isActive();
if (criticalVisible && !modalActive) {
    // dispatch to NotificationManager for CRITICAL toast dismiss
}
```

Checking only `!criticalVisible` is insufficient: the `modalActive` guard MUST also be present because same-frame state transitions can leave `criticalVisible == true` while a modal is active (e.g., the game-over modal is shown on the same frame that a deficit CRITICAL toast happens to be visible). Without the `!modalActive` check, Priority 2 would consume the event before Priority 1's modal handler sees it, bypassing the modal's input lock.
3. `QueryPanel` / Inspector panel (**does NOT consume Ctrl+Z** — passes it through to priority 4). **Escape is consumed by QueryPanel** when it is open: pressing Escape dismisses the QueryPanel (identical to pressing I again or clicking elsewhere) and does NOT open the Pause Menu. If the QueryPanel is not open, Escape passes through. **Dismiss-click consumption**: when the QueryPanel is open, a click **anywhere outside the panel bounds** closes the panel AND the click is **consumed** (not passed to the city grid or toolbar below). This prevents accidental zone/road placement on the click that dismisses the panel. The consumed dismiss-click applies only when the QueryPanel is open; if the player wishes to place after dismissal, they must click a second time. **Toolbar carve-out exception**: clicks within the **primary toolbar bounds** (virtual x: 8–72 px, y: 64–784 px — covering the tool icon group, undo button, demand bar, and active tool indicator) are **NOT consumed** by the QueryPanel dismiss. **Note**: these coordinate values are defined as named constants in `src/ui/ui_constants.h` per `ui-manager.md`. Any future update to the carve-out bounds MUST update both this file AND the corresponding constants in `ui_constants.h` — do not let raw numbers in the spec diverge from the implementation constants. A click on a toolbar button **MUST close the inspector panel** (`m_inspector->hide()`, `m_inspectorOpen = false`) AND be passed through to the toolbar handler, activating the selected tool in a single click. **Implementation contract**: the Priority 3 toolbar carve-out branch MUST explicitly close the inspector before falling through — it must NOT merely fall through without closing. If the inspector is left open while `m_activeTool` changes to a non-Query tool (e.g. Zone), subsequent terrain clicks will be mis-routed: Priority 3 will intercept them as "inspector dismiss-clicks" (consuming them) before Priority 7 can handle zone placement, making the terrain non-interactive. The correct sequence for a toolbar click while the inspector is open is: (1) call `m_inspector->hide()`, (2) set `m_inspectorOpen = false`, (3) fall through to Priority 5 toolbar dispatch. Step (1) and (2) must occur at Priority 3, not deferred to Priority 5. **Minimap carve-out exception**: clicks within the **full minimap widget footprint** (see `minimap.md` for canonical dimensions; implementation must use `UIManager`-level named constants from `src/ui/ui_constants.h` — do NOT use `Minimap::getBounds()` alone and do NOT hardcode the coordinates; `Minimap::getBounds()` returns the render area only (y:880–1080 px) and is insufficient — the full widget includes the legend panel (y:732–832 px) and label strip (y:832–848 px) when a service overlay is active; use `kMinimapWidgetTopOverlayActive` for the top-y when an overlay is active and `kMinimapWidgetTop` for the top-y when no overlay is active, both defined in `src/ui/ui_constants.h`) are **NOT consumed** by the QueryPanel dismiss. A minimap click closes the QueryPanel AND is passed through to the minimap handler, panning the camera in a single click. **Rationale**: minimap clicks are non-destructive navigation actions — consuming them would force a double-click to navigate after dismissing the panel, creating an inconsistent experience with camera navigation in general. Implementation: in `QueryPanel::handleDismissClick(int x, int y)`, check both toolbar AND minimap bounds; if either matches, dismiss the panel but return `false` (pass-through). The city tile grid (everything outside the toolbar, minimap, and panel bounds) still requires a second click after panel dismissal. **Mutual exclusion with Tax Rate Panel**: QueryPanel and Tax Rate Panel must NOT be simultaneously open. Opening the QueryPanel (pressing I or clicking a tile) must close the Tax Rate Panel if it is open; opening the Tax Rate Panel must close the QueryPanel if it is open. Only one floating panel is active at any time.

   **QueryTool open path (tile left-click when QueryPanel is closed)**: When `m_activeTool == ActiveTool::Query` AND the QueryPanel is NOT currently open AND the event is `MouseButtonDown button=0` (left-click), Priority 3 handles the inspector open path as follows: call `m_renderer->pickTerrainTile(event.screenX, event.screenY, tileX, tileZ)`. If the ray-cast returns `true` (valid terrain hit): call `m_sim->queryTile(tileX, tileZ)` to obtain a `QueryResult`; obtain the tile's physical screen bounding box via `m_renderer->getTileScreenBounds(tileX, tileZ)` and un-project all four corners to virtual space via `UIScaler::unproject()`; call `InspectorPanel::computePanelPosition(cursorX_virtual, cursorY_virtual, tileBounds_virtual)` for the three-step position cascade; destroy any existing inspector elements and recreate them at the computed position via `InspectorPanel::populate(result, tileX, tileZ)`; return `true` (event consumed). If the ray-cast returns `false` (no terrain hit): return `false` (pass-through — the player may have clicked a non-terrain area). This open path runs BEFORE the dismiss-click path within Priority 3 — a left-click when the QueryPanel is closed and the Query tool is active can only be the open path. The dismiss-click path only runs when the QueryPanel is already open. The two paths are mutually exclusive by panel state and MUST be evaluated in open-first, dismiss-second order within the Priority 3 block.
4. **Tax Rate Panel** — when the Tax Rate Panel is open (floating panel triggered from the resource/budget bar), it consumes all clicks within its bounds and consumes Escape to close itself. Escape closes the Tax Rate Panel without opening the Pause Menu. Clicks outside the panel bounds are NOT consumed (pass-through, allowing camera pan while the panel is open). Ctrl+Z passes through to priority 5. **Mutual exclusion**: see QueryPanel note above — opening the Tax Rate Panel closes the QueryPanel. **Intentional asymmetry with QueryPanel dismiss behavior**: The QueryPanel (Priority 3) consumes the dismiss-click when clicking outside (preventing accidental zone placement). The Tax Rate Panel does NOT consume outside clicks — this is a deliberate design difference because the Tax Rate Panel is a non-destructive floating panel (viewing/adjusting tax rates) that players may want to keep open while panning the camera. Clicks outside the Tax Rate Panel are never irreversible, so consuming them provides no safety benefit. This asymmetry is intentional and must not be "fixed" to match QueryPanel behavior. See `tax-rate-panel.md — Dismiss click event consumption` for the full implementation spec.
5. `UIManager` HUD controls (toolbar, minimap, buttons); **Ctrl+Z (undo) is processed at this priority level** — `UIManager` intercepts the Ctrl+Z chord and triggers the undo action if one is available. **Keybinding capture intercept**: When the Settings panel's Controls tab has a keybinding chip in Capturing state (`SettingsPanel::isCapturingKeybinding()` returns `true`), all key-down events are intercepted at Priority 5 BEFORE toolbar and minimap dispatch. The capture state machine consumes the key event for all keys EXCEPT Escape. Escape during capture is NOT consumed by the capture state machine — it must be allowed to fall through to the tab-level Escape handler at Priority 5 (which closes Settings). This means capture is cancelled and Settings closes in the same event frame: the chip exits Capturing state (no binding is recorded) and the Settings panel closes. See the **Escape routing rules** section below for the full capture-Escape sequence. **Toolbar per-button dispatch**: when a left-click falls within the toolbar input carve-out (x:8–72, y:64–784 in virtual 1920×1080 space), `UIManager::onEvent()` performs a secondary y-range hit-test to identify the specific button and dispatches accordingly:

  | Button | Virtual y-range | Action |
  |---|---|---|
  | Zone | 64–112 | `HUD::setActiveToolLabel("Zone")` |
  | Road | 120–168 | `HUD::setActiveToolLabel("Road")` |
  | Utilities | 176–224 | `HUD::setActiveToolLabel("Utilities")` |
  | Demolish | 232–280 | `HUD::setActiveToolLabel("Demolish")` |
  | Query | 288–336 | Toggle inspector panel (same path as `I` key); label → `"Query"` or `"No tool"` |
  | Undo | 608–656 | `m_sim->undoLastAction()` when `hasUndoPendingAction()` (same path as Ctrl+Z) |

  All toolbar y-ranges are derived from `HUD.cpp` button layout constants (`kToolBtnSize=48`, `kToolPad=8`, starting y:64). Any change to HUD button positions MUST update both `HUD.cpp` and the dispatch table in `UIManager::onEvent()`.
6. `CameraController` — receives unconsumed events after all UI layers. Camera movement events (scroll-wheel zoom, middle-mouse-button drag, right-mouse-button drag, edge-scroll `MouseMove`) that were not consumed by Priorities 1–5 are processed here. Priority 6 is NOT the terminal layer — world interaction (Priority 7) executes after it for events that `CameraController` does not consume.
7. **World Interaction layer** — the terminal layer. Processes mouse events that were not consumed by any higher-priority handler (Priorities 1–6). Rules:

   (a) **`MouseMove` does NOT consume the event** (always returns `false`). Edge-scroll logic in `CameraController` (Priority 6) depends on every `MouseMove` reaching it; the World Interaction layer must not break this by returning `true` on `MouseMove` events. Hover highlight or tile-cursor update may occur as a side-effect, but event consumption is forbidden.

   (b) **`MouseButtonDown button=0` (left-click) consumes the event** (`return true`) only when ALL of the following conditions hold: (1) a non-Query placement tool is active (i.e., the active tool is Zone, Road, Utilities, or Demolish — NOT QueryTool and NOT "No tool"), AND (2) `pickTerrainTile()` ray-cast returns `true` (the ray hit valid terrain). If either condition is false, the event is NOT consumed (returns `false`). In particular, a `QueryTool` left-click does NOT consume the event — it passes through so the QueryPanel open/inspect path (Priority 3) can handle it. **Hover-tile update on click (drag throttle invariant)**: after a successful `pickTerrainTile()` hit in the `MouseButtonDown` handler, `m_hoveredTileX` and `m_hoveredTileZ` MUST be updated to the clicked tile coordinates before any placement occurs. Without this update, the drag throttle condition `(hitX != m_hoveredTileX || hitZ != m_hoveredTileZ)` in the subsequent `MouseMove` handler evaluates `true` for the same tile (because `m_hoveredTileX` still holds the stale pre-click value, often −1), causing the initial tile to be double-placed on the first drag step. **Zone tool — rectangular drag-select**: `MouseButtonDown` records the anchor tile in `m_zoneAnchorX`/`m_zoneAnchorZ` and returns `true` (consumed) without placing anything. `MouseMove` while LMB is held computes the axis-aligned rectangle from anchor to current hover tile, **partitions the selected tiles into `freeTiles` (tiles where placement would succeed — unoccupied) and `blockedTiles` (tiles already occupied by an existing zone)**, and calls `IRenderer::setTilePlacementPreview(freeTiles, kHoverArgb, blockedTiles)`. Free tiles display in the normal green/teal preview colour (`kHoverArgb`); blocked tiles display in red (`kHoverArgbBlocked` — a fixed renderer constant, not caller-supplied). This gives the player immediate visual feedback about which tiles will be skipped at placement time. `setTileHoverHighlight(-1,-1)` is called to clear the single-tile hover cursor while the preview is active. `MouseButtonUp button=0` fills all tiles in `[min(anchor,hover), max(anchor,hover)]` by calling `doTerrainPlacement(tx, tz)` for each tile (blocked tiles are naturally skipped by the placement logic), then clears the anchor and calls `setTilePlacementPreview({}, 0)` to remove the preview. A single click-and-release (no drag) fills a 1×1 rectangle — exactly one tile. **Road tool — straight-line drag-select**: `MouseButtonDown` records the anchor tile in `m_zoneAnchorX`/`m_zoneAnchorZ` and returns `true` (consumed) without placing anything. `MouseMove` while LMB is held snaps to the dominant axis (whichever of |dX| vs |dZ| is larger), **partitions the straight-line tiles into `freeTiles` (unoccupied) and `blockedTiles` (tiles already occupied by an existing road)**, and calls `IRenderer::setTilePlacementPreview(freeTiles, kHoverArgb, blockedTiles)`. As with the Zone tool, free tiles render in the normal preview colour and blocked tiles render in red via `kHoverArgbBlocked`. `setTileHoverHighlight(-1,-1)` clears the single-tile hover. `MouseButtonUp button=0` places all tiles along the snapped line by calling `doTerrainPlacement()` for each (blocked tiles are skipped), then clears the anchor and preview. A single click-and-release places exactly one road tile. **Utilities tool**: `MouseButtonDown` calls `doTerrainPlacement()` immediately, and `MouseMove` while LMB is held calls `doTerrainPlacement()` for each new tile entered (tile-by-tile drag behavior). The tool does not use `setTilePlacementPreview()`.

   **Demolition Tool**: The demolish tool is activated via the Demolish button in the toolbar (or hotkey `X`). While demolish is NOT the active tool, left-mouse events must NEVER trigger demolition logic. Tool-mode is checked before any tile-interaction handler fires, ensuring the Zone tool (or any other tool) cannot accidentally enter the demolition code path. **Mouse-down** (LMB press) while Demolish is active: calls `pickTerrainTile()` to identify the hovered tile; if the ray-cast succeeds, calls `IRenderer::setTileHoverHighlight(tileX, tileZ)` to display a demolition-colour highlight quad as visual feedback (no dialog shown yet). The handler stores the highlighted tile coordinates in `m_demolishAnchorX`/`m_demolishAnchorZ` and returns `true` (consumed). If the ray-cast fails (miss), no highlight is set and the event is NOT consumed. **Mouse-up** (LMB release): if the current mouse position ray-casts to the same tile that was highlighted on mouse-down (matching `m_demolishAnchorX`/`m_demolishAnchorZ`), the confirmation modal "Demolish [tile type]? [Yes] [No]" is opened. If the cursor has moved to a different tile between down and up (hit test differs from anchor), the highlight is cleared via `IRenderer::setTileHoverHighlight(-1, -1)` and no modal is shown. **Yes button** in the modal: calls `ICitySimulation::demolishTile(tileX, tileZ)` with the tile coordinates stored in the modal and dismisses the modal. **No button** or modal dismissed (Esc): dismisses the modal, calls `IRenderer::setTileHoverHighlight(-1, -1)` to clear the highlight, clears the anchor state, and remains in Demolish tool mode ready for the next demolition attempt. **Tool switching**: when the player clicks a toolbar button to switch from Demolish to Zone, Road, Utilities, or Query, or presses Escape to deselect the tool entirely, any pending demolition highlight is cleared synchronously via `IRenderer::setTileHoverHighlight(-1, -1)` as part of the tool-deselect handler (Priority 7e / tool-deselect path), and the anchor state is reset to `{-1, -1}`.

   (c) **Modal and CRITICAL-toast suppression**: if a blocking modal (`ModalDialog`, Priority 1) or a CRITICAL toast (`NotificationManager`, Priority 2) consumed the event at a higher priority, the event never reaches Priority 7. No additional guard is required at Priority 7 for this case — the chain short-circuits naturally. However, if Priority 7 code is ever invoked speculatively (e.g., via a direct call bypassing the chain), it MUST check `m_modal->isActive() || m_notifications->hasCriticalToastVisible()` and return `false` immediately. This mirrors the identical suppression logic used at Priority 6 (`CameraController` suppresses input when a blocking modal is active).

   (d) **QueryTool left-click does NOT consume the event**: when the active tool is `QueryTool`, `MouseButtonDown button=0` must return `false`. The QueryPanel open path is handled at Priority 3; the World Interaction layer must not intercept it.

   (e) **Right-click (`MouseButtonDown button=1`) — tool deselect**: When a non-None tool is active (`m_activeTool != ActiveTool::None`), `UIManager::onEvent()` consumes the RMB press at this layer: it resets `m_activeTool` to `None`, clears `m_lmbHeld`, clears any in-progress anchor state (`m_zoneAnchorX`/`m_zoneAnchorZ`), calls `IRenderer::setTilePlacementPreview({}, 0)` to clear any active drag preview, calls `IRenderer::setTileHoverHighlight(-1, -1)` to clear the hover quad, resets `m_hoveredTileX`/`m_hoveredTileZ` to `{-1,-1}`, calls `updateSubPanelVisibility()`, and returns `true` (consumed). **Hover-clear invariant**: the hover highlight MUST be cleared synchronously in this handler. Without it, `m_hoverVisible` stays `true` in the renderer and the last-hovered tile quad remains frozen on screen — the MouseMove handler at Priority 7 is gated on `m_activeTool != None` and is therefore never reached after the tool is deselected, so no subsequent frame will correct the stale highlight. Returning `true` also prevents `EventReceiver` from setting `m_rmbDragActive` — so no camera RMB-drag starts on the same press. When no tool is active (`m_activeTool == ActiveTool::None`), the RMB event is NOT consumed here and falls through to `CameraController` (Priority 6) for RMB-drag initiation per the forwarding contract defined at Priority 1. This handler logically sits between Priority 6 and 7 (after CameraController receives scroll/MMB events, but before the terminal world-interaction layer). Priority 7 itself never consumes right-click events — only this layer does when a tool is active.

**Ctrl+Z routing rules**: `ModalDialog` (priority 1) blocks Ctrl+Z when any blocking modal is active — undo is unavailable while a modal is open, consistent with the Undo System spec. `QueryPanel` (priority 3) does not intercept Ctrl+Z; when the QueryPanel is open and the player presses Ctrl+Z, the event passes through QueryPanel to `UIManager` (priority 5) which handles undo normally. This is consistent with QueryPanel not being a destructive action.

**Escape routing rules**: `ModalDialog` (priority 1) consumes Escape only for undismissable modals (forced loan dialog); dismissable modals also consume Escape to close themselves. `QueryPanel` (priority 3) consumes Escape to close itself. `TaxRatePanel` (priority 4) consumes Escape to close itself. When `SettingsPanel` is visible: Escape is consumed at Priority 5 by `SettingsPanel`. The behavior depends on the current game state:

- If `m_state == GameState::Paused`: close Settings and call `PauseMenuPanel::show()`
- If `m_state == GameState::MainMenu`: close Settings and restore `MainMenuPanel` focus — call `MainMenuPanel::show()` (NOT `PauseMenuPanel::show()`; calling PauseMenuPanel during MainMenu state would incorrectly surface the pause overlay)

**Escape during keybinding capture**: If a keybinding chip is in Capturing state when Escape is pressed, the capture state machine MUST NOT consume the Escape event. The correct sequence within the Priority 5 handler is: (1) check `SettingsPanel::isCapturingKeybinding()`; (2) if true, call `SettingsPanel::cancelKeybindingCapture()` to exit Capturing state without recording any binding; (3) do NOT return `true` — fall through to the normal SettingsPanel Escape handler, which closes Settings per the game-state rules above. The net result is that a single Escape press both cancels the capture and closes Settings. This is intentional: the player pressed Escape, which universally means "cancel/back" — it would be surprising if a first Escape only cancelled the chip while leaving Settings open.

The PauseMenu-to-gameplay transition is NOT triggered. When `PauseMenuPanel` is open and `SettingsPanel` is NOT open: Escape is consumed by `PauseMenuPanel` (closes Pause Menu; resumes gameplay via `transitionToGameplay_fromPaused()`). If none of these are open, Escape passes to `UIManager` which opens the Pause Menu. `CameraController` never sees Escape.

**Pre-gameplay Escape routing (MainMenu and NewGame screens)**: When `GameState == MainMenu` or `GameState == NewGame`, Escape is routed to the Back/Main-Menu action — it is NOT routed to the Pause Menu. This routing is enforced in `UIManager::onEvent()` by checking the current `GameState` before any Escape dispatch:

- `GameState::MainMenu`: Escape is a no-op (the player is already at the top-level screen; there is no parent screen to navigate Back to). The event is consumed to prevent fall-through.
- `GameState::NewGame`: Escape maps to the Back button action — it dismisses the New Game screen and returns to Main Menu (identical to clicking the Back button in the New Game flow). No confirmation dialog is shown (no simulation state has been created yet). This is consistent with the Back-button spec in `main-menu-new-game-flow.md`.

This pre-gameplay Escape check MUST appear before the Pause-Menu-open path in `UIManager::onEvent()`. Without it, pressing Escape on the New Game screen would open the Pause Menu overlay on top of a pre-gameplay screen, which has no defined dismiss path and produces an inconsistent state.

**WindowFocusGained / WindowFocusLost pass-through contract**: `WindowFocusGained` and `WindowFocusLost` events MUST always pass through unconditionally to `CameraController` (the application focus handler) — they MUST NOT be consumed by `UIManager::onEvent()` at any priority level, including Priority 1 (modal active) and the scrim event-consumer path. These events are the sole mechanism by which `CameraController` sets `m_appHasFocus`. Consuming either event in `UIManager` would permanently desynchronise `m_appHasFocus` from actual OS window focus: for example, consuming `WindowFocusLost` on Alt+Tab would leave `m_appHasFocus = true`, causing edge-scroll to continue firing while the application window has no focus.

Implementation requirement: `UIManager::onEvent()` must check for `WindowFocusGained` and `WindowFocusLost` event types at the top of the dispatch chain (before Priority 1) and immediately return `false` (pass-through) for both, regardless of modal state, scrim visibility, or any other UI condition. No priority level may consume these events. `CameraController::onEvent()` processes them and updates `m_appHasFocus` accordingly.

**Query mode exit — authoritative rules**: The following events all exit Query mode and MUST close the inspector panel (`m_inspector->hide()`, `m_inspectorOpen = false`) as part of the same action:

1. **Toolbar button click** (any tool button: Zone, Road, Utilities, Demolish, or Query-toggle-off): handled at Priority 3 (toolbar carve-out — close inspector, fall through) then Priority 5 (set new `m_activeTool`, call `updateSubPanelVisibility()`).
2. **I hotkey while Query tool is active**: handled at Priority 5 — sets `m_activeTool = None`, closes inspector, calls `updateSubPanelVisibility()`.
3. **Escape while inspector is open**: handled at Priority 3 — closes inspector (`m_inspectorOpen = false`, `m_inspector->hide()`), does NOT change `m_activeTool` (remains `Query`), returns `true` (consumed). The player remains in Query mode but the inspector is closed; the next terrain click will re-open the inspector.
4. **Outside-panel click** (not on toolbar or minimap): handled at Priority 3 — closes inspector, consumes the click. `m_activeTool` remains `Query`.

**After exiting Query mode via paths 1**: `m_activeTool` changes to the newly selected tool; `updateSubPanelVisibility()` immediately shows the appropriate sub-panel (e.g., zone sub-panel for Zone tool). The inspector is closed at Priority 3 before Priority 5 sets the new tool. There must be NO frame where `m_inspectorOpen == true` AND `m_activeTool != ActiveTool::Query` — this inconsistent state causes terrain clicks to be consumed by the inspector dismiss path rather than routed to world interaction.

**After exiting Query mode via paths 3 and 4**: `m_activeTool` remains `ActiveTool::Query`. No sub-panel appears (zone sub-panel is hidden in Query mode). The player must either click a toolbar button (path 1) or press I (path 2) to switch to a placement tool.

## CRITICAL: Irrlicht GUI Event Swallowing — IGUIButton Click Handling

**Root cause**: Irrlicht's device event loop calls `GUIEnvironment->postEventFromUser(event)` BEFORE
calling `Receiver->OnEvent(event)`. When `GUIEnvironment` handles a button click and returns `true`
(button was activated), `Receiver->OnEvent` is **never called** for the underlying
`EMIE_LMOUSE_PRESSED_DOWN` event. This means any `UIManager::onEvent()` handler that relies on
receiving a raw `MouseButtonDown` event (including all `inRect`-based click handlers for the Bell
icon, speed buttons, and toolbar buttons) is **silently never invoked** when the player clicks an
Irrlicht `IGUIButton`.

**Mandatory fix**: `EventReceiver::OnEvent()` (in `src/platform/EventReceiver.cpp`) MUST handle
`EET_GUI_EVENT / EGET_BUTTON_CLICKED` events and synthesise a `MouseButtonDown` `InputEvent` at
the button's physical centre, converted to virtual coordinates via `UIScaler::unproject()`. The
synthesised event is dispatched to `UIManager::onEvent()`. The handler MUST return `false` so
Irrlicht continues its own GUI handling (rendering button active state, etc.).

**Required implementation** (in `EventReceiver::OnEvent()`):

```cpp
if (event.EventType == irr::EET_GUI_EVENT &&
    event.GUIEvent.EventType == irr::gui::EGET_BUTTON_CLICKED) {
    irr::gui::IGUIElement* btn = event.GUIEvent.Caller;
    if (btn && m_scaler) {
        irr::core::rect<irr::s32> r = btn->getAbsolutePosition();
        const int physCx = (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2;
        const int physCy = (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2;
        UIScaler::VirtualPoint vp = m_scaler->unproject(physCx, physCy);
        InputEvent btnEv{};
        btnEv.type = InputEvent::Type::MouseButtonDown;
        btnEv.button = 0;
        btnEv.x = vp.x; btnEv.y = vp.y;
        btnEv.physX = physCx; btnEv.physY = physCy;
        if (m_uiManager) m_uiManager->onEvent(btnEv);
    }
    return false; // MUST return false — let Irrlicht finish its own GUI handling
}
```

**This pattern is mandatory for ALL `IGUIButton` interactions in the project.** Any toolbar button,
Bell icon, speed selector button, or other `IGUIButton`-backed UI element that relies on
`UIManager::onEvent()` receiving a `MouseButtonDown` event MUST go through this synthesised-event
path. The raw `EMIE_LMOUSE_PRESSED_DOWN` event is NOT delivered to `EventReceiver::OnEvent()` when
Irrlicht's GUI layer consumes the click; the synthesised `EGET_BUTTON_CLICKED` dispatch is the
only reliable delivery mechanism.

**Why `return false`**: Returning `true` from the `EGET_BUTTON_CLICKED` handler would tell Irrlicht
to stop processing the GUI event, preventing visual button state updates (the pressed/active
appearance). Always return `false` for `EET_GUI_EVENT` handlers in `EventReceiver` so the Irrlicht
GUI environment can complete its own rendering and state management.

## Hover State Switching — IGUIButton Image Swap

Hover visual feedback is implemented entirely inside `IrrlichtUIBackend` by handling
`EGET_ELEMENT_HOVERED` and `EGET_ELEMENT_LEFT` GUI events. No `IUIBackend` interface methods are
added — this is an internal rendering concern.

**Required implementation** (in `IrrlichtUIBackend::OnEvent()` or equivalent GUI event hook):

```cpp
if (event.EventType == irr::EET_GUI_EVENT) {
    irr::gui::IGUIElement* el = event.GUIEvent.Caller;
    // Use getType() instead of dynamic_cast to avoid RTTI issues across shared-library
    // boundaries on some GPU/driver combinations.
    if (el && el->getType() == irr::gui::EGUIET_BUTTON) {
        auto* btn = static_cast<irr::gui::IGUIButton*>(el);
        if (event.GUIEvent.EventType == irr::gui::EGET_ELEMENT_HOVERED) {
            if (!btn->isPressed()) {
                // swap to hover sprite cell (skip if button is in active/pressed state)
                uint32_t hoverId = lookupHoverSpriteId(btn);  // kSpriteXxxHover constant
                if (hoverId != kSpriteInvalid)
                    btn->setImage(getSpriteTexture(hoverId));
            }
        } else if (event.GUIEvent.EventType == irr::gui::EGET_ELEMENT_LEFT) {
            // Restore the registered base sprite (whatever setElementImage last set).
            // This may be an active OR inactive sprite — do NOT force-map to inactive,
            // as buttons are often initialized with active sprites and must remain so.
            uint32_t baseId = lookupRegisteredSpriteId(btn);  // value from m_imageElementMap
            if (baseId != kSpriteInvalid)
                btn->setImage(getSpriteTexture(baseId));
        }
    }
    return false; // never consume hover events — Irrlicht must process them too
}
```

**Rules:**

- `EGET_ELEMENT_HOVERED` → if `btn->isPressed()` is true, skip (active sprite persists);
  otherwise call `IGUIButton::setImage()` with the `kSpriteXxxHover` cell texture (85% opacity,
  outlined 2 px stroke, 1 px white border — see
  `architecture/asset-standards/2d-texture-standards.md` §Icon State Authoring Rules)
- `EGET_ELEMENT_LEFT` → restore the **registered base sprite** — the sprite ID last set via
  `IUIBackend::setElementImage()`, stored in `m_imageElementMap`. This is the sprite that was
  visible before the hover began; it may be an active sprite (filled icon) or an inactive sprite
  (outline), depending on what the caller set. Do NOT force-map to inactive — that incorrectly
  erases icons from buttons initialised with active sprites (e.g. toolbar buttons, which always
  show filled icons regardless of tool selection state).
- Active buttons (`isPressed()` returns true) must NOT have their image overridden on
  `EGET_ELEMENT_HOVERED`; they are skipped entirely so the active sprite persists across
  hover-enter. On `EGET_ELEMENT_LEFT` the registered base sprite is restored unconditionally
  (for a pressed button the base sprite is the active sprite, so the result is the same).
- Always `return false` — consuming hover events blocks Irrlicht's own tooltip and focus logic
- `kSpriteXxxHover` constants live in `src/ui/hud_sprite_ids.h`; the naming convention is
  `kSprite<Name>Hover` (e.g. `kSpriteZoneResidentialHover`, `kSpriteRoadHover`)
