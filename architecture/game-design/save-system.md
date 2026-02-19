# Save System

- **Auto-save triggers**: Every **120 real seconds** OR every **5 budget ticks**, whichever comes first — and when the player opens the Pause Menu (before displaying it). The time-based trigger ensures players at 1× speed do not lose more than 2 minutes of progress regardless of tick rate. Additionally, auto-save triggers **immediately when the forced loan dialog becomes active** (before the modal is shown), ensuring pre-crisis city state is always saved and "Load Last Save" in the game-over modal reflects a meaningful restore point.
- **Save slots**: 1 auto-save slot (overwritten silently) + 3 manual save slots (player-named or auto-named with timestamp)
- **Save format**: JSON for V1 (human-readable, debuggable); binary serialization is post-V1 optimization
- **Save file location**: `~/.config/aitown/saves/` on Linux; `%APPDATA%\aitown\saves\` on Windows (consistent with `keybindings.json` path convention)
- **Manual save**: The Pause Menu > Save opens a slot selection dialog showing 3 slots with their timestamps. Saving to an occupied slot shows a brief confirmation "Overwrite [slot name]? Yes / Cancel."
- **Quit to Desktop safety**: Selecting Quit to Desktop checks if unsaved progress exists since the last save. If so, shows a blocking modal: "You have unsaved progress. Save and Quit / Quit Without Saving / Cancel." This modal follows the standard ModalDialog system and blocks scene input.
- **Load Last Save (Game Over)**: Loads the most recent save across all slots (auto-save or manual), sorted by timestamp. If no save exists, the "Load Last Save" button is grayed out with tooltip "No save file found."
- **Save in Scenario mode**: Scenario mode saves include scenario state (win condition progress, starting conditions). Scenario saves are stored in a dedicated `scenarios/` subdirectory.
