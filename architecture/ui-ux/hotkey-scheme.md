# Hotkey Scheme (default bindings)

| Key | Action |
|---|---|
| Z | Zone tool |
| R | Road tool |
| U | Utilities tool |
| D | Demolish tool |
| I | Inspector / Query tool |
| T | Toggle Tax Rate Panel |
| B | Toggle Notification Log |
| Space | Pause / unpause |
| + / = | Increase simulation speed |
| - | Decrease simulation speed |
| Escape | Open pause menu (gameplay); Back/Cancel (pre-gameplay screens) |
| Ctrl+Z | Undo last destructive action |
| Ctrl+S | Manual save (opens save-slot dialog) |

Camera pan (Arrow keys) and Undo (Ctrl+Z) are additional bindings not shown in the single-key table; see Camera Controls and Undo System sections respectively. T and B are rebindable in Settings > Controls with standard conflict detection. **WASD camera pan preset**: Settings > Controls includes a "WASD" preset button that atomically rebinds PanUp=W, PanDown=S, PanLeft=A, PanRight=D and simultaneously moves Demolish from D to X — applied as a single atomic operation to avoid partial-rebind asymmetric states. **Before applying the preset, a confirmation preview modal is shown** — see `modal-dialog-system.md — WASD camera preset confirmation modal` for the full spec including modal body text, button labels, atomic rebinding behavior, and Tab order. Individual key rebinding remains available for custom setups.

- **Undo (Ctrl+Z)**: Modifier chord. Processed by `UIManager::onEvent()` at Priority 5 (HUD controls tier) in the input arbitration chain. The `IEventReceiver` translates the raw `SEvent` chord into an `InputEvent` and forwards it to `UIManager::onEvent()` — it does NOT intercept Ctrl+Z before the priority chain. Ctrl+Z is blocked at Priority 1 when any blocking modal is active. In V1, Ctrl+Z is **not rebindable** (chord handling requires separate key-mapping infrastructure). It appears in the rebinding UI as a non-rebindable informational row: "Ctrl+Z — Undo (not rebindable in V1)". The JSON config format for chord bindings: `"Ctrl+KeyZ"` (modifier prefix + SDL2-style key name). Conflict detection does not apply to non-rebindable chords.
- **Save (Ctrl+S)**: Modifier chord. Opens the save-slot dialog (same as Settings > Save). Processed by `IEventReceiver` before other handlers, immediately after Ctrl+Z in priority. In V1, Ctrl+S is **not rebindable** — same rationale as Ctrl+Z. Appears in the rebinding UI as a non-rebindable informational row: "Ctrl+S — Save (not rebindable in V1)". Ctrl+S is only active during gameplay (not on the Main Menu or New Game screen). The unsaved-changes dot in the HUD shows the tooltip "Press Ctrl+S to save" — this tooltip must reference the correct chord even if the binding changes post-V1.
- **Q/E are reserved** for future use and must not be bound to camera or tools. In the rebinding UI, Q and E are displayed as **grayed-out rows labeled "Reserved for future camera controls — unavailable"** — not as bindable slots. Binding them silently (even via direct file edit) must be ignored on load with a warning logged. **Q and E cannot be used as swap targets**: if a player types Q or E in any rebinding input field, immediately display "This key is reserved and cannot be assigned" in red beneath the input field, and do not proceed to the conflict detection flow. Q/E rows in the rebinding table must show a tooltip on hover explaining why they are unavailable.

## Visual Design — Glass City

The hotkey rebinding UI in Settings > Controls uses the Glass City button tile and text colours:

- **Rebinding table row labels**: `#EBF4F6` near-white for bindable actions;
  `#4A7FA5` mid-blue for reserved / non-rebindable rows (Q, E, Ctrl+Z, Ctrl+S)
- **Currently bound key chip**: `rgba(255, 255, 255, 0.08)` background, 1 px
  `rgba(255, 255, 255, 0.18)` border (inactive tile)
- **Key chip during capture (listening for input)**: `rgba(0, 201, 200, 0.22)` teal wash,
  2 px `rgba(0, 201, 200, 0.75)` border — signals "this slot is active / recording"
- **Conflict warning text** ("Key already used by: [Action Name]"): `#F04E37` red
- **Reserved key warning text** ("This key is reserved and cannot be assigned"): `#F04E37` red
- **Swap / Cancel conflict resolution buttons**: Glass City button tile (inactive default)

- Bindings stored in a `KeyBindings` config struct loaded at startup; rebinding UI required in V1 settings panel
- **Config file format**: JSON; path: `~/.config/aitown/keybindings.json` (Linux), `%APPDATA%\aitown\keybindings.json` (Windows); key names use SDL2-style string identifiers (e.g. `"Space"`, `"KeyZ"`). Default camera pan bindings: `"PanUp": "ArrowUp"`, `"PanDown": "ArrowDown"`, `"PanLeft": "ArrowLeft"`, `"PanRight": "ArrowRight"`
- **Conflict detection**: When a player selects a new key for any action, the rebinding UI immediately checks all other bound actions. If a conflict is found, display inline text "Key already used by: [Action Name]" with two choices: **Swap bindings** (exchange the two actions' keys) or **Cancel** (revert to previous key). `keybindings.json` is saved only after a conflict-free state is confirmed — a conflicting config is never written to disk.
