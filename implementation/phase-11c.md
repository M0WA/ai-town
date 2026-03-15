## Phase 11c: Settings, Controls & Save/Load Dialog Polish

**Status: Planned**

### Goal

Fix three areas of the UI that were specified in Phase 8 / Phase 11 but whose visual
and functional completeness was deferred: (1) the Settings / Pause Menu panel background
is missing the Glass City deep-navy treatment; (2) the Controls tab in Settings has no
keybinding table — the `keybindings.json` back-end from Phase 11 exists but the rebinding
UI was never wired; (3) the save-slot picker and Load Game dialogs are incomplete.

---

### Deliverables

#### 1. Settings Dialog — Glass City Background

- [ ] **`SettingsPanel` background**: Apply the Glass City deep-navy panel style to
  `SettingsPanel`:
  - Background fill: `rgba(13, 27, 42, 0.88)` deep navy; **8 px corner radius** on all
    four edges (the panel is a floating centred overlay — never flush with the screen edge)
  - Background is drawn by `IrrlichtUIBackend` as a rounded-rect fill via the UI sprite-sheet
    panel tile (rows 16+ of `hud_sprites_ui.dds`, 2048×2048 RGBA8 UNORM, per
    `architecture/asset-standards/2d-texture-standards.md` §UI Sprite Sheet — the
    building-atlas-layout.md is the building facade atlas and does not contain panel tiles) or
    the equivalent `IVideoDriver::draw2DRectangle` path if the tile is not yet available
  - Full-screen scrim: solid `rgba(0, 0, 0, 0.50)` drawn beneath the panel (same scrim
    rule as modal dialogs — see `architecture/ui-ux/modal-dialog-system.md`)

- [ ] **`PauseMenuPanel` background**: Same Glass City treatment as `SettingsPanel`
  (the two panels share identical panel style per `architecture/ui-ux/settings-pause-menu.md`):
  - Background: `rgba(13, 27, 42, 0.88)`, 8 px corner radius, floating centred
  - Scrim: `rgba(0, 0, 0, 0.50)` full-screen layer beneath the panel

- [ ] **Tab strip visual state**: Settings tab headers use Glass City button tiles:
  - **Active tab**: `rgba(0, 201, 200, 0.22)` teal wash + 2 px `rgba(0, 201, 200, 0.75)`
    teal border + 4 px baked glow
  - **Inactive tab**: `rgba(255, 255, 255, 0.08)` fill + 1 px `rgba(255, 255, 255, 0.18)`
    border
  - **Hover**: `rgba(255, 255, 255, 0.15)` fill + 1 px `rgba(255, 255, 255, 0.35)` border

- [ ] **Text colour pass** — Settings panel text must match the Glass City palette:
  - Panel title: `#EBF4F6` near-white
  - Active tab label: `#EBF4F6`; inactive tab label: `#4A7FA5` mid-blue
  - Field labels: `#4A7FA5`; field value readouts: `#F0B429` amber
  - Disabled control labels (e.g. "Post-launch" disaster toggle): `#4A7FA5` at 50% opacity
    (implemented via `IUIBackend::setElementAlpha` + `setElementEnabled(..., false)`)

- [ ] **Apply / Cancel / Restore Defaults buttons** on all tabs: Glass City button tile
  (inactive default style; hover and focus states per tab-strip rules above)

- [ ] **`SettingsPanelBackgroundTest`** (label `unit`, CMake target `ui_tests`): uses
  `NiceMock<MockUIBackend>` to verify that `SettingsPanel::show()` calls
  `setElementVisible(scrımHandle, true)` and `setElementImage(bgHandle, kPanelTileId)` at
  the expected handles. Scrim handle and background handle are stored as `UIElementHandle`
  members of `SettingsPanel` (consistent with the `ModalDialog` element-repositioning
  pattern — elements created once at construction, repositioned on each `show()` call via
  `setElementRect()`).

---

#### 2. Controls Tab — Keybinding Table

The Controls tab in `SettingsPanel` currently has placeholder stubs. This deliverable
wires the full rebinding table specified in `architecture/ui-ux/hotkey-scheme.md`.

- [ ] **`KeyBindingsPanel`** — new class `src/ui/KeyBindingsPanel.h` /
  `src/ui/KeyBindingsPanel.cpp` owned by `SettingsPanel`:
  - Renders the rebinding table within the Controls tab content area
  - Owns the capture-mode state machine (Idle → Capturing → ConflictPending → Idle)

- [ ] **Rebinding table rows** — one capturable row per V1 rebindable action (per
  `architecture/ui-ux/hotkey-scheme.md` §V1 rebindable actions): Zone tool (Z), Road tool
  (R), Utilities tool (U), Demolish tool (D), Inspector/Query tool (I), Toggle Tax Rate
  Panel (T), Toggle Notification Log (B), Pan Up (ArrowUp), Pan Down (ArrowDown), Pan Left
  (ArrowLeft), Pan Right (ArrowRight) — 11 capturable rows total. Space (Pause), Escape,
  +/= and - (speed controls) are system-reserved and do NOT appear in the table:
  - Row layout: action-name label (left) + key-chip button (right)
  - **Bindable row colours**: action label `#EBF4F6` near-white; key chip uses Glass City
    inactive tile (`rgba(255, 255, 255, 0.08)` fill + 1 px `rgba(255, 255, 255, 0.18)`
    border)
  - **Capture state**: key chip switches to teal wash (`rgba(0, 201, 200, 0.22)` fill +
    2 px `rgba(0, 201, 200, 0.75)` border) — signals "recording"; row label changes to
    "Press a key…"

- [ ] **Non-rebindable informational rows** (Ctrl+Z — Undo, Ctrl+S — Save):
  - Label: `#4A7FA5` mid-blue; chip: `#4A7FA5` mid-blue background (no capture on click)
  - Displayed as "Ctrl+Z — Undo (not rebindable in V1)" and
    "Ctrl+S — Save (not rebindable in V1)"

- [ ] **Reserved-key rows** (Q and E):
  - Displayed as "Q — Reserved for future camera controls — unavailable" and
    "E — Reserved for future camera controls — unavailable"; label colour `#4A7FA5` at 50%
    opacity; hover tooltip: "This key is reserved and cannot be assigned"
  - No capture state; clicking the row chip has no effect

- [ ] **Capture flow**:
  1. Player clicks a bindable key chip → row enters Capturing state
  2. Next key press (excluding Escape) is read as the candidate new key
  3. **Q/E guard**: if the candidate is Q or E, immediately display inline red text
     `#F04E37` "This key is reserved and cannot be assigned" beneath the row; remain in
     Capturing state so the player can retry — do NOT advance to conflict detection
  4. **Conflict detection**: if the candidate is already bound to another action, display
     inline `#F04E37` text "Key already used by: [Action Name]" with two buttons
     **Swap bindings** / **Cancel** (Glass City button tile)
  5. **Swap**: atomically exchange the two bindings in the in-memory `KeyBindings` struct;
     both rows update their chip labels; state returns to Idle
  6. **Cancel** (conflict): revert the candidate; row returns to Idle with original chip
  7. **Escape during capture**: cancels capture (row returns to Idle, no change) before
     the tab-level Escape handler fires (per
     `architecture/ui-ux/settings-pause-menu.md` §Controls tab)

- [ ] **WASD preset button**: a "WASD" preset button at the bottom of the Controls tab
  content area (above Apply/Cancel/Restore Defaults). Clicking it opens the WASD preset
  confirmation modal (Small, 480×240 px) per
  `architecture/ui-ux/modal-dialog-system.md` §WASD camera preset confirmation modal:
  - Body shows current binding of W, A, S, D (so the player sees what will be overwritten)
  - Buttons: **Apply** (primary) / **Cancel** (default focus — least destructive)
  - On Apply: atomically rebind PanUp=W, PanDown=S, PanLeft=A, PanRight=D, Demolish=X;
    write `keybindings.json`; close modal; refresh all chip labels in the table

- [ ] **Apply button (Controls tab)**: writes `keybindings.json` only when the in-memory
  `KeyBindings` state is conflict-free (no two actions share a key). If the panel somehow
  reaches Apply with a conflict still present, Apply is grayed out and a summary line
  shows `#F04E37` "Resolve all key conflicts before saving"

- [ ] **Cancel button (Controls tab)**: reverts all in-session rebinding changes in the
  `KeyBindings` struct to the state at the time the Controls tab was last opened (or the
  persisted state if never modified this session); closes Settings (returns to Pause Menu
  or Main Menu, matching the Escape-from-tab behaviour)

- [ ] **Restore Defaults (Controls tab)**: shows "Reset all Controls settings to defaults?
  Yes / Cancel." confirmation; on Yes, resets the in-memory `KeyBindings` struct to
  defaults and refreshes all chip labels (does NOT write to disk until Apply)

- [ ] **`KeyBindingsPanelTest`** (label `unit`, CMake target `ui_tests`):
  - `KeyBindingsPanel_CaptureMode_TogglesOnClick`: clicking a bindable chip enters Capture
    state; clicking again (second key chip while first is active) cancels first capture and
    begins new one
  - `KeyBindingsPanel_ConflictDetected_ShowsSwapOption`: binding a key already used by
    another action shows inline conflict text and Swap/Cancel buttons
  - `KeyBindingsPanel_Swap_ExchangesBothRows`: pressing Swap correctly exchanges bindings
    in the in-memory struct
  - `KeyBindingsPanel_ReservedKey_ShowsReservedError`: pressing Q or E during capture
    displays reserved-key error and does NOT advance to conflict detection
  - `KeyBindingsPanel_Escape_CancelsCapture`: pressing Escape during capture exits Capture
    state without changing the binding; Settings panel does NOT close on this Escape
  - `KeyBindingsPanel_ApplyGrayed_WhenConflictExists`: Apply button is disabled when any
    conflict is unresolved in the in-memory struct
  - All tests use `NiceMock<MockUIBackend>` and `StrictMock` for calls under assertion;
    `KeyBindings` is injected as a value type (copied on tab open, written on Apply)

---

#### 3. Save / Load Dialog Fixes

Phase 11 delivered `SaveSystem` backend plumbing. This deliverable completes the
player-facing save/load UI that was left as stubs.

##### 3-pre. ISaveSystem Interface (prerequisite for MockSaveSystem)

Phase 11 delivered `SaveSystem` as a concrete class without a pure-virtual interface.
`MockSaveSystem` (used by the tests below) requires a pure-virtual `ISaveSystem` to
generate via GMock — following the testability architecture pattern for all injectable
components (see `architecture/testing/testability-architecture.md`).

- [ ] **`src/interfaces/ISaveSystem.h`** — new pure-virtual interface:
  - Methods mirroring the `SaveSystem` public API used by `UIManager`:
    `virtual bool saveToSlot(int slot) = 0;`
    `virtual bool autoSave() = 0;`
    `virtual LoadResult loadLastSave() = 0;`
    `virtual bool hasSaveFile() const = 0;`
    `virtual SaveFileState getSaveFileState() const = 0;`
    `virtual std::string getSaveDirectoryPath() const = 0;`
    `virtual ~ISaveSystem() = default;`
  - Enum `SaveFileState { NoSaves, AllCorrupt, Valid }` defined in `src/interfaces/ISaveSystem.h`

- [ ] **`SaveSystem` updated** to `class SaveSystem : public ISaveSystem` — all public methods
  declared `override`; no other behaviour changes

- [ ] **`MockSaveSystem`** in `tests/ui/MockSaveSystem.h` — standard `NiceMock`/`StrictMock`-
  compatible GMock stub for all `ISaveSystem` methods; used by `UIManagerUnsavedQuitTest`,
  `UIManagerSaveFailureTest`, and `MainMenuSaveStateTest`

- [ ] **`UIManager` constructor** updated to accept `ISaveSystem*` instead of `SaveSystem*`
  (if `UIManager` currently holds a concrete pointer); production `main.cpp` passes the real
  `SaveSystem` instance; all existing `UIManager` tests pass `MockSaveSystem` or `nullptr`

##### 3a. Unsaved-Changes Modal

- [ ] **`ModalDialog::showUnsavedQuit()`** properly wired in `UIManager`:
  - Called when `m_pendingQuit != None` and `m_hasUnsavedChanges == true` (see
    `architecture/game-design/save-system.md` §Quit-to-Desktop/Quit-to-Main-Menu safety)
  - Three buttons: **Save and Quit** / **Quit Without Saving** / **Cancel**
  - Default Tab focus: **Cancel** (least destructive, per global modal rule)
  - On **Save and Quit**: `SaveSystem::autoSave()` → dispatch quit via `m_pendingQuit`
  - On **Quit Without Saving**: dispatch quit immediately
  - On **Cancel** / Escape: clear `m_pendingQuit`; return to game/pause menu
  - Size: Small (480×240 px); Glass City modal style with scrim
  - **Unsaved Changes dot** (`m_hasUnsavedChanges` dirty flag): verify that
    `setElementVisible(unsavedDotHandle, m_hasUnsavedChanges)` is called correctly on
    every state transition (action placed → dot shown; manual/auto-save → dot cleared;
    failed auto-save → dot remains shown)

- [ ] **`UIManagerUnsavedQuitTest`** (label `unit`, CMake target `ui_tests`):
  - `UIManager_QuitToDesktop_WithUnsavedChanges_ShowsModal`
  - `UIManager_QuitToDesktop_NoUnsavedChanges_ExitsImmediately`
  - `UIManager_UnsavedQuit_SaveAndQuit_CallsAutoSave`
  - `UIManager_UnsavedQuit_Cancel_ClearsPendingQuit`
  - Uses `StrictMock<MockCitySimulation>`, `NiceMock<MockUIBackend>`,
    `StrictMock<MockSaveSystem>` (new mock); `ManualClock`

##### 3b. Manual Save Failure Modal

- [ ] **`ModalDialog::showSaveFailure()`** — blocking error modal for manual save
  failures (Ctrl+S path): title "Save Failed", body "[reason]", buttons **Retry** /
  **Cancel** (per `architecture/ui-ux/settings-pause-menu.md` §Auto-save)
  - Dismissible via Escape (activates Cancel)
  - Size: Small (480×240 px); Glass City modal style
  - On **Retry**: calls `SaveSystem::saveToSlot(m_lastSaveSlot)` again; on success
    clears unsaved-changes dot; on second failure shows modal again
  - On **Cancel**: modal dismissed; unsaved-changes dot remains visible
  - **Auto-save failure** (distinct path): shows CRITICAL toast "Auto-save failed — [reason].
    Press Ctrl+S to save manually." (2 s display, non-dismissible until expiry per
    `NotificationManager::postCritical()`) — this path does NOT use the blocking modal

- [ ] **`UIManagerSaveFailureTest`** (label `unit`, CMake target `ui_tests`):
  - `UIManager_ManualSave_Failure_ShowsBlockingModal`
  - `UIManager_ManualSave_RetrySuccess_ClearsUnsavedDot`
  - `UIManager_AutoSave_Failure_PostsCriticalToast_NotModal`

##### 3c. Load Game Dialogs — Main Menu State

- [ ] **Main Menu "Load Game" button state** correctly reflects save file state at
  startup and after save/load operations (three states per
  `architecture/ui-ux/main-menu-new-game-flow.md`):
  - **No saves**: grayed, tooltip "No saves found"
  - **All corrupt**: grayed, tooltip "Save data is corrupted — cannot load. Check
    [path] for recovery." (path from `SaveSystem::getSaveDirectoryPath()`)
  - **At least one valid save**: enabled; on click → loading screen → terrain rebuild →
    gameplay; loading controller calls `UIManager::onGameLoaded()` after deserialisation

- [ ] **`MainMenuSaveStateTest`** (label `unit`, CMake target `ui_tests`):
  - `MainMenuPanel_NoSaves_LoadButtonGrayed`
  - `MainMenuPanel_CorruptSaves_LoadButtonGrayed_TooltipShowsPath`
  - `MainMenuPanel_ValidSave_LoadButtonEnabled`
  - Uses `NiceMock<MockUIBackend>` and `StrictMock<MockSaveSystem>`

##### 3d. Post-V1 Slot Picker Stub (informational)

Phase 11 saves to slot 1 directly with no picker dialog — this is correct V1 behaviour.
The slot picker (3 slots with timestamps, overwrite confirmation) is post-V1 scope and
must **not** be implemented in Phase 11c. A code comment `// Post-V1: slot picker dialog`
in `UIManager::update()` at the `consumeSaveRequest()` call site is sufficient.

---

### Exit Criteria

- `SettingsPanel` and `PauseMenuPanel` render with the Glass City deep-navy background
  (`rgba(13, 27, 42, 0.88)`, 8 px corner radius) and full-screen scrim
- Settings > Controls tab shows the full rebinding table with Glass City chip styling;
  capture flow, conflict detection, Swap/Cancel, Q/E guard, and WASD preset modal all work
- `keybindings.json` is written only after conflict-free Apply; Escape during capture
  cancels capture only (does not close Settings)
- Unsaved-changes modal (Save and Quit / Quit Without Saving / Cancel) fires correctly
  for both Quit-to-Desktop and Quit-to-Main-Menu paths
- Manual save failure shows blocking error modal (Retry / Cancel); auto-save failure posts
  CRITICAL toast only
- Main Menu Load Game button correctly reflects no-saves / all-corrupt / valid-save state
- All new unit tests pass; `all-checks-pass` gate green

### Team

| Role | Responsibility |
|---|---|
| `gamedesign-ux` | Glass City background spec for Settings + PauseMenuPanel; Controls tab table layout; unsaved-changes modal UX; Load Game button tooltip copy |
| `graphics-dev-irrlicht` | `SettingsPanel` / `PauseMenuPanel` background rendering; `KeyBindingsPanel` implementation; `ModalDialog::showUnsavedQuit()` / `showSaveFailure()` wiring; Main Menu save-state button wiring; `ISaveSystem` interface + `SaveSystem : public ISaveSystem` refactor + `UIManager` constructor update to accept `ISaveSystem*` |
| `test-dev-cpp` | All new unit tests: `SettingsPanelBackgroundTest`, `KeyBindingsPanelTest`, `UIManagerUnsavedQuitTest`, `UIManagerSaveFailureTest`, `MainMenuSaveStateTest`; `MockSaveSystem` in `tests/ui/` (requires `ISaveSystem` from the deliverable above — mocks must never live under `src/`) |

### Dependencies

- Requires Phase 8 complete (Settings / Pause Menu panels exist as stubs)
- Requires Phase 11 complete (`SaveSystem` backend, `keybindings.json` load/save,
  `UIManager::m_hasUnsavedChanges` dirty flag)
- No audio, terrain, or simulation deliverables — pure UI polish

### Risks & Spikes

- **RISK**: `IUIBackend` does not currently expose a rounded-rect draw call. The Glass City
  panel background must be implemented using the existing panel sprite tile (building-atlas
  rows 16+) — if the tile does not have 8 px pre-baked corners, a fallback using four
  `draw2DRectangle` calls for body + corners is acceptable provided the visual result is
  indistinguishable at 1080p. Spike: render a test panel in the EDT_NULL integration
  test harness and confirm the tile path does not crash before committing the full impl.
- **RISK**: Capture-mode input interception must fire before `UIManager::onEvent()`
  Priority-6 camera dispatch — confirm ordering with the existing `InputEvent` translation
  layer in `IEventReceiver` before implementation begins.
