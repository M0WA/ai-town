# Main Menu & New Game Flow

- **Main Menu** (shown on launch and after "Quit to Main Menu"): Full-screen overlay with four options: **New Game**, **Load Game**, **Settings**, **Quit**. Load Game button states:
  - **Grayed out, tooltip "No saves found"**: no save files exist.
  - **Grayed out, tooltip "Save data is corrupted — cannot load. Check [save folder path] for recovery."**: save files exist but all are unreadable (checksum failure, schema version mismatch, truncated file). The tooltip must show the actual save folder path so the player can attempt manual recovery. The path is obtained via `SaveSystem::getSaveDirectoryPath()`, which returns the platform-specific save directory (`~/.config/aitown/saves/` on Linux, `%APPDATA%\aitown\saves\` on Windows); see `architecture/game-design/save-system.md`.
  - **Enabled**: at least one valid, loadable save file exists. On activation: the loading-screen path is used (same as New Game start) to deserialise the save and rebuild terrain; upon completion, `GameState` transitions to `Gameplay`. The loading controller must call `UIManager::onGameLoaded()` after deserialization completes and before the first `UIManager::update()` tick (see `architecture/ui-ux/ui-manager.md` § `onGameLoaded()`).
- **New Game screen**:
  - Mode: Sandbox (V1 only; Scenario grayed out with "Post-launch" label)
  - Difficulty: Easy ($1M start) / Normal ($500K start) / Hard ($200K start) — radio buttons
  - Options: Disaster toggle (checkbox, default off for Easy/Normal, forced off in V1 — post-V1 scope)
  - Map size: **Small** (128×128 tiles) / **Medium** (512×512 tiles — default) / **Large** (1024×1024 tiles) — radio buttons. Mutually exclusive. Medium is the default selection. All three options are enabled in V1.
  - Map seed: text input (default: random); "Randomize" button generates a new random seed
  - **Start City** button: validates seed, generates terrain (shows loading spinner), then enters gameplay
  - **Back button** (bottom-left of screen): returns to Main Menu with no confirmation dialog (no simulation state has been created yet). **Escape key** in the New Game screen (and all other pre-gameplay screens) is mapped to this Back action, navigating to the Main Menu. This Escape mapping applies only to pre-gameplay screens; during gameplay Escape opens the Pause Menu as defined in the Hotkey Scheme.
- **Seed validation**: Before terrain generation begins, the map seed input is validated. Only integer values (positive, zero, or negative) are accepted. If the field is empty, a random seed is used (same as pressing Randomize). If the value is non-numeric or out of the valid `uint64_t` range, the **Start City** button remains disabled and an inline error label appears beneath the seed field: "Invalid seed — enter a whole number or leave blank for random." The error clears when the input is corrected.
- **Loading screen**: Full-screen progress bar during terrain generation. Label: "Generating terrain..." with percentage. Minimum display time: 0.5 s (prevents flash on fast hardware). **Cancel affordance**: A **Cancel** button is shown in the lower-right of the loading screen. Pressing Cancel or Escape during loading aborts terrain generation and returns to the New Game screen with all previous settings intact (difficulty, seed, options preserved). The abort signal is checked at generation checkpoint intervals; generation cannot be cancelled mid-tile-write (ensures no partial terrain state). **Cancel button disabled when abort is not possible**: When terrain generation passes its final checkpoint (abort is no longer checkable), the Cancel button must be immediately **disabled (grayed out) and its label changed to "Finalizing..."** — this transition happens the moment the final abort checkpoint passes, not when the minimum display time elapses. A visible-but-silently-ignored Cancel button is a UX defect. The 0.5 s minimum display time keeps the loading screen visible after generation completes, but the Cancel button is disabled during this window. Players who see the grayed "Finalizing..." label understand the transition is imminent.
- **Quit to Main Menu**: Available in Pause Menu. Performs unsaved-changes check (see Save System). On confirm, destroys simulation state, returns to Main Menu.
- **Difficulty is set at New Game creation only**. The Settings > Gameplay tab displays difficulty as read-only information during play (see `settings-pause-menu.md — Gameplay tab`). Players cannot change difficulty mid-game.
- **Keyboard navigation — Main Menu**: All four buttons (New Game, Load Game, Settings, Quit) are Tab-navigable in document order (top to bottom). Default keyboard focus on launch: "New Game" button. Arrow Up/Down also navigate between buttons. Enter activates the focused button. Load Game when grayed out (no saves or corrupted saves) is skipped in the Tab order. Visual focus ring: 2 px accent-color border (matching modal dialog focus ring spec).
- **Keyboard navigation — New Game screen**: Tab order follows document flow: Mode selector → Difficulty radio buttons → Map size radio buttons → Disaster checkbox → Map seed field → Randomize button → Start City button → Back button. Arrow keys cycle within radio button groups (Difficulty: Easy/Normal/Hard; Map size: Small/Medium/Large). Enter activates buttons. Grayed-out controls (Scenario mode, Disaster toggle in V1) are skipped in Tab order. Escape on the New Game screen is equivalent to clicking Back (same as all pre-gameplay screens per the Escape key spec above).

## MainMenuPanel → UIManager Communication

`MainMenuPanel` communicates user actions (Start City, Settings) to `UIManager` via a polling pattern — MainMenuPanel does NOT hold a `UIManager*` pointer and does NOT call UIManager methods directly. This avoids circular dependencies (UIManager owns MainMenuPanel).

**Mechanism**: When the player clicks "Start City" or "Settings", `MainMenuPanel::onEvent()` sets an internal flag (`m_startGameRequested` or `m_settingsRequested`) and returns `true`. Each frame, `UIManager::update()` polls these flags:

```cpp
// In UIManager::update(), when m_state == GameState::MainMenu:
if (m_mainMenu->consumeStartGameRequest()) {
    transitionToGameplay(GameMode::Sandbox);
    return;
}
if (m_mainMenu->consumeSettingsRequest()) {
    showSettings();
}
```

`consumeStartGameRequest()` and `consumeSettingsRequest()` are consume-once methods: they return `true` exactly once per flag-set (reading clears the flag). This one-frame polling delay is invisible to the player. The pattern ensures communication is unidirectional (panels set flags, UIManager polls and acts) with no back-pointers from panels to UIManager.
