# AI Town UI/UX Architecture Review

**Reviewer**: Senior UI/UX Designer
**Date**: 2026-03-29
**Scope**: All files under `architecture/ui-ux/`, cross-referenced with `architecture/game-design/` and `architecture/graphics-architecture/irrlicht-device-lifecycle.md`

---

## Table of Contents

1. [File-by-File Findings](#file-by-file-findings)
2. [Cross-File Issues](#cross-file-issues)
3. [Missing Specs](#missing-specs)
4. [Summary Table](#summary-table)

---

## File-by-File Findings

---

### `camera-controls.md`

**CC-1** [GAP] MEDIUM
The spec states that `kKeyboardPanRate` is a named constant that allows Arrow-key pan to be tuned independently, but it does not appear in the `ui_constants.h` constant listing in `ui-manager.md`. The constant is described textually but never formally listed in the toolbar-constants block. If Arrow-key pan speed is tunable at implementation time and the constant lives in `CameraController.h`, the spec should state that explicitly. Currently there is no canonical home declared for `kKeyboardPanRate`, `kBasePanSpeed`, `kDefaultZoomDistance`, `kMinZoomDistance`, or `kMaxZoomDistance`.
**Proposed resolution**: Add a section to `camera-controls.md` explicitly stating these five constants are `constexpr` in `src/ui/CameraController.h` (not in `ui_constants.h`), and list their purpose alongside the constraint that they are tunable at implementation time with no prescribed values.

---

**CC-2** [GAP] MEDIUM
The mouse-sensitivity slider is described as applying to "MMB drag and edge-scroll" pan inputs only, and explicitly not to Arrow-key pan. However, there is no spec for whether the sensitivity slider also applies to RMB drag (camera rotate/pitch). RMB drag uses physical pixel delta by design (drag-delta coordinate space section), but the sensitivity multiplier relationship to RMB rotate speed is never stated.
**Proposed resolution**: Add a sentence explicitly stating whether `sensitivityMultiplier` applies to RMB drag pan/rotate or whether RMB rotate speed has a separate constant.

---

**CC-3** [GAP] LOW
The spec defines edge-scroll activation as a 20 px band in virtual 1920×1080 space but does not specify what happens when the application window is not 16:9 (e.g., ultrawide 21:9). The UIScaler letterbox/pillarbox model means that mouse coordinates in the black bars are clamped to virtual edge values, which could cause spurious edge-scrolling in the bars. There is no explicit statement that edge-scroll should be suppressed when the mouse is in the letterbox/pillarbox region.
**Proposed resolution**: Add a note that edge-scroll fires only when the physical mouse coordinate is within the active viewport (not in a letterbox/pillarbox bar), cross-referenced with `UIScaler::getViewportRect()`.

---

**CC-4** [GAP] LOW
The spec describes the `CameraController` constructor as accepting a `bool startInFullscreen` parameter but does not specify the full constructor signature (parameters, whether `UIScaler*` or another type is passed). `resolution-ui-scaling.md` defines `UIScaler` construction but never references `CameraController`'s dependency on it for edge-scroll threshold evaluation.
**Proposed resolution**: Add the full `CameraController` constructor signature with all parameters so it can be locked before implementation.

---

### `finances-panel.md`

**FP-1** [GAP] MEDIUM
The spec says the panel is "horizontally centered" at 360×520 px below the resource bar, but no explicit `y` anchor is given for the panel's top edge. "Below the resource bar" is ambiguous — the resource bar occupies y: 0–56 px; does the panel start at y: 56, y: 64, or some other value? This is needed to verify that the panel does not overlap the left toolbar (which starts at x: 8) and that its bottom edge (y_top + 520) does not exceed virtual 1080 px.
**Proposed resolution**: Add explicit virtual-space bounding box coordinates: `panel_x = (1920 - 360) / 2 = 780`, `panel_y = 64` (or chosen value), `w = 360`, `h = 520`.

---

**FP-2** [GAP] LOW
The "key-repeat cap" of ±5 percentage points per continuous hold is defined for the +/- buttons, but the interaction between the cap and direct text entry is not specified. If a user holds + to reach the cap (+5 ppt), then immediately uses direct entry to type a new value, is the cap reset? The spec only mentions "releasing and re-pressing resets the cap" for the hold path; it is silent on whether direct-entry clears the hold-cap state.
**Proposed resolution**: Add a sentence clarifying that activating the direct-entry text field unconditionally resets the hold-cap accumulator.

---

**FP-3** [INCONSISTENCY] LOW
The Budget Section color for surplus is specified as `SColor(255, 128, 200, 80)` = `#80C850` (green). The Glass City canonical color palette in `resolution-ui-scaling.md` does not include this green — the palette defines `#F04E37` (error red), `#F0B429` (amber), `#EBF4F6` (near-white), `#4A7FA5` (mid-blue), `#E8960C` (warning amber), and `#00C9C8` (teal). A surplus-green is not in the locked palette table. This could cause inconsistency when other panels need to display a positive/surplus state.
**Proposed resolution**: Add `#80C850` surplus-green to the Glass City canonical palette table in `resolution-ui-scaling.md`, or substitute it with an existing palette token and document the substitution in `finances-panel.md`.

---

**FP-4** [GAP] LOW
The spec says the Finances Panel receives `IAudioSystem*` in its constructor, matching the HUD pattern. However, it does not specify whether `FinancesPanel` forward-declares `IAudioSystem` in its header (keeping UI headers free of audio headers, matching the `NotificationManager` pattern). This is a code-structure concern with direct UX testing implications (the `MockAudioSystem` injection pattern for panel tests).
**Proposed resolution**: Add an explicit note mirroring the `NotificationManager.h` pattern: `IAudioSystem` is forward-declared in `FinancesPanel.h`; the full include is in `FinancesPanel.cpp` only.

---

### `hotkey-scheme.md`

**HS-1** [PROBLEM] HIGH
The Pause/Unpause key (Space) and the speed controls (+/= and -) are listed as "system-reserved" and do not appear in the rebinding table. However, `input-arbitration.md` does not include Space in the Escape routing rules section, and there is no explicit specification for what priority in the 6-level arbitration chain intercepts Space. If a blocking modal is active, can the player still press Space? The modal section of `input-arbitration.md` says "All keyboard events... ARE consumed by the modal" — but the `notification-system.md` CRITICAL-toast auto-pause section explicitly says the speed selector remains enabled during CRITICAL-toast pause (not modal pause), implying Space must reach `UIManager` during CRITICAL-toast state. The exact arbitration path for Space is never stated.
**Proposed resolution**: Add Space to the `input-arbitration.md` Escape routing rules section, specifying at which priority level it is processed and whether it is consumed by blocking modals.

---

**HS-2** [GAP] MEDIUM
The hotkey table lists `+ / =` for speed increase and `-` for speed decrease. These are specified as "system-reserved" and not rebindable, but the spec never addresses the numpad variants (Numpad+ and Numpad-). On keyboards where the main `+` key requires Shift, players may expect numpad speed controls to work. The spec is silent.
**Proposed resolution**: Either explicitly include or exclude numpad variants in the non-rebindable speed control spec.

---

**HS-3** [GAP] LOW
The conflict detection flow describes two resolution choices (Swap / Cancel) but does not specify what happens when the player attempts to swap a rebindable action with a non-rebindable informational row (e.g., if Ctrl+Z or Ctrl+S somehow appears as a conflict target). The conflict detection spec says "If a conflict is found" but Ctrl+Z and Ctrl+S use a chord format (`"Ctrl+KeyZ"`) while single-key actions use bare names — it is unclear whether the chord validator correctly excludes modifier chords from appearing as single-key conflicts.
**Proposed resolution**: Add a sentence to the conflict detection spec explicitly stating that Ctrl-chord bindings (Ctrl+Z, Ctrl+S) are never flagged as conflicts with single-key bindings and cannot be swap targets.

---

**HS-4** [GAP] LOW
The WASD preset confirmation modal spec (cross-referenced to `modal-dialog-system.md`) is thorough, but `hotkey-scheme.md` itself does not specify what happens if the player has already manually rebound some of the WASD keys before clicking the preset button. For example, if the player has already rebound W to Zone and D to Road, the modal says "current binding of each affected key" — but those custom bindings would be overwritten by the preset. The modal body text ("Any custom bindings for W/A/S/D will be overwritten") covers this, but the `hotkey-scheme.md` atomic rebinding description does not restate this behavior, creating a potential implementation gap.
**Proposed resolution**: Cross-reference the atomic rebinding note in `hotkey-scheme.md` to confirm that the preset applies regardless of current W/A/S/D bindings.

---

### `hud-layout.md`

**HL-1** [GAP] HIGH
The Density Unlock Preview Tooltip is mentioned in `economy-model.md` ("HUD shows a preview: when monthly revenue is within 10% of an unlock threshold, a projected 'After Unlock' estimated monthly expense change is shown in the resource bar tooltip") but there is no corresponding spec in `hud-layout.md`. The HUD layout file describes the resource bar content (treasury, debt, city rating, population, date) but contains no definition of the density unlock progress indicator element, its virtual bounds, the 10% proximity trigger, or the tooltip content format. The `economy-model.md` `getNextUnlockThreshold()` section cross-references `hud-layout.md §Density Unlock Preview Tooltip` — but that section does not exist.
**Proposed resolution**: Add a `Density Unlock Progress Indicator` and `Density Unlock Preview Tooltip` subsection to `hud-layout.md` with virtual bounds, trigger condition (≥10% proximity), tooltip content (threshold value, current revenue, projected expense change), and the sentinel-suppression rule when `getNextUnlockThreshold()` returns `-1.0f`.

---

**HL-2** [GAP] HIGH
The resource bar (y: 0–56 px) contains treasury, debt indicator, City Rating, population, and in-game date. The `economy-model.md` states wages are a "visible budget line item in the HUD resource bar". However, `hud-layout.md` does not include wages as a resource bar element. This is a direct contradiction: the economy spec promises a HUD element that the HUD layout spec does not define. Whether wages appear in the resource bar or only in the Finances Panel Budget Section is unresolved.
**Proposed resolution**: Clarify in `hud-layout.md` whether wages appear as a resource bar element (and if so, add virtual bounds and update logic) or only in the Finances Panel Budget Section (and if so, update `economy-model.md` to remove the "HUD resource bar" reference).

---

**HL-3** [GAP] MEDIUM
The Utilities sub-panel in `hud-layout.md` specifies a 4×1 layout (`width = (64×4)+(4×3) = 268 px`) but the `ui_constants.h` block in `ui-manager.md` shows `kUtilSubPanelWidth = 196` and `kUtilSubBtnW = 96`, with a comment "2×2 button grid: 2 columns × 2 rows". This is a direct numerical contradiction: `hud-layout.md` says 4 buttons in a single horizontal row; `ui-manager.md` says a 2×2 grid of 96×48 px buttons. The total widths differ (268 vs. 196). One of these is wrong.
**Proposed resolution**: Determine the canonical layout (single-row 4 buttons at 64 px each, or 2×2 grid of 96×48 px buttons) and update whichever file is incorrect. This is a CRITICAL implementation conflict that will produce a broken sub-panel.

---

**HL-4** [INCONSISTENCY] MEDIUM
The Utilities sub-panel visibility is anchored at y:64 (same as Zone sub-panel) per `hud-layout.md`. The `ui-manager.md` `kUtilSubPanelTop = 176` (aligned with Utilities button row at y:176). These are contradictory top-y values (64 vs. 176). The comment in `ui-manager.md` says "aligned with Utilities button row" which makes functional sense, but the `hud-layout.md` text says the Utilities sub-panel shares "the same top anchor (y:64) as the Zone sub-panel."
**Proposed resolution**: Determine the correct top anchor and update both files. The `y:176` value in `ui-manager.md` (aligned with the Utilities toolbar button) is almost certainly more correct ergonomically. Update `hud-layout.md` to match.

---

**HL-5** [GAP] MEDIUM
The in-game date/time display is listed as a resource bar element but the spec gives no format for it (e.g., "Month 3, Year 1", "Year 1 Month 3", "Day 42"). The spec also does not specify which `ICitySimulation` accessor provides date/time data, how many characters the element needs, or whether it uses the monospace font.
**Proposed resolution**: Add a `Date/Time Display` subsection specifying: format string, data source accessor (e.g., `getSimulatedDate()` or derived from tick count), character budget, monospace font requirement.

---

**HL-6** [GAP] LOW
The demand pressure bar spec says `getDemandPressurePct(ZoneType)` returns `float` in `[0.0, 1.0]` and the bar must multiply by 100.0f. It also warns about inverse semantics with `QueryResult::demandPressurePct`. However, the spec does not define what "0% demand pressure" means visually — is an empty bar good (no unmet demand) or bad (no demand at all)? The visual meaning (full bar = high unmet demand, i.e., opportunity to zone) vs. (full bar = high demand satisfaction, i.e., city is doing well) is ambiguous from the bar design alone.
**Proposed resolution**: Add a legend label below the bars (e.g., "Unmet demand" / "Low" → "High") with a brief tooltip that explains the direction: "Higher bar = more demand for this zone type." This is both a spec gap and a UX clarity issue.

---

**HL-7** [GAP] LOW
The `IUIBackend::setMouseCursor()` method is described as "not part of the V1 19-method `IUIBackend` interface" and cursor shape changes are deferred to Phase 12. However, the method is described as a future addition. When Phase 12 adds it, it will become method 22 (since the interface already has 21 methods). There is no placeholder or stub in the `IUIBackend` method contract in `ui-manager.md` for it. If Phase 12 implementers add it without updating the method count comment, `ui-manager.md` will become stale.
**Proposed resolution**: Add a numbered comment (method 22, reserved / Phase 12) to the `IUIBackend` interface listing in `ui-manager.md` so the contract stays auditable.

---

### `input-arbitration.md`

**IA-1** [GAP] HIGH
The Priority 7 rules include a detailed "Demolition Tool" section and a "Road tool — straight-line drag-select" section, but there is no specification for what happens during drag-selection when the cursor leaves the terrain entirely (ray-cast returns false mid-drag). The Zone tool spec says "If the LMB is released while the hover tile is invalid (m_hoveredTileX == -1, e.g., ray missed all terrain), no tiles are filled and the anchor is cleared silently." The Road tool has no equivalent mid-drag miss rule.
**Proposed resolution**: Add an explicit "ray-cast miss during drag" paragraph to the Road tool section stating behavior when `pickTerrainTile()` fails mid-drag (e.g., keep last valid anchor, do not extend preview, or clear preview).

---

**IA-2** [GAP] MEDIUM
The Priority 5 dispatch table lists toolbar y-ranges for Zone (64–112), Road (120–168), Utilities (176–224), Demolish (232–280), Query (288–336), Undo (608–656). These six entries account for y ranges up to 656. The text above the table says input carve-out runs to y:784 to cover demand bars (y:664–748) and active tool indicator (y:752–784). However, the dispatch table has no entries for clicks in the demand bar or active tool indicator regions. Clicks in y:657–784 would fall through the dispatch table with no handler — are they simply consumed by the carve-out (preventing accidental terrain interaction) without any action, or is this a gap?
**Proposed resolution**: Add a note to the dispatch table clarifying that clicks within the carve-out but not matching any button y-range are silently consumed (no action, but event is not passed to Priority 7).

---

**IA-3** [GAP] MEDIUM
The "Escape during keybinding capture" section specifies that pressing Escape while a chip is in Capturing state cancels capture AND closes Settings in the same event frame. However, it does not specify what happens if the player is in the Controls tab and has other unsaved keybinding changes (not the currently capturing chip) — do those pending changes get discarded along with the capture cancellation, or does the usual "Cancel reverts all pending keybinding changes" behavior still apply via the normal Settings Cancel path?
**Proposed resolution**: Clarify that the escape-during-capture path follows the same "Cancel reverts all pending changes" semantics as the normal Escape-closes-Settings path.

---

**IA-4** [GAP] LOW
The "RMB drag suppression in non-gameplay states" section guards `EMIE_RMOUSE_PRESSED_DOWN` with `UIManager::isGameplayOrPaused()`. However, this method is not declared anywhere in the `ui-manager.md` `UIManager` class structure listing. It is referenced here but has no formal spec.
**Proposed resolution**: Add `bool isGameplayOrPaused() const` to the UIManager public API section in `ui-manager.md` with its contract: returns `true` when `m_state == GameState::Gameplay || m_state == GameState::Paused`.

---

**IA-5** [DUPLICATE] LOW
The Finances Panel dismiss behavior (outside clicks not consumed) is described both in `input-arbitration.md` Priority 4 and in `finances-panel.md` (Dismiss click event consumption section). Both descriptions are accurate and consistent, but the `finances-panel.md` section is considerably more detailed. The duplication creates a maintenance risk if either is updated without updating the other.
**Proposed resolution**: In `input-arbitration.md` Priority 4, replace the local explanation with a cross-reference sentence: "Dismiss click event consumption: see `finances-panel.md — Dismiss click event consumption` for the full spec." Keep the authoritative text only in `finances-panel.md`.

---

**IA-6** [PROBLEM] LOW
The QueryPanel dismiss spec (Priority 3) specifies that the toolbar carve-out exception allows toolbar clicks to close the inspector AND activate the new tool in a single click. The implementation contract explicitly states "call `m_inspector->hide()`, set `m_inspectorOpen = false`, fall through to Priority 5." However, the dispatch table at Priority 5 uses a y-range hit test to identify the button. When the inspector-dismiss path falls through from Priority 3 to Priority 5, the original event's y coordinate is used for the Priority 5 hit test — but Priority 3 only fires the close sequence when the click is within the toolbar carve-out (x: 8–72). If a player clicks in x: 8–72 but in a y-range not matching any button (e.g., y: 344–607, which is between Query's bottom at y:336 and the undo button at y:608), the inspector closes but no tool is activated. This gap (y:337–607 within the toolbar carve-out) produces a silent click that only dismisses the inspector.
**Proposed resolution**: Document this as intentional behavior: clicks in the toolbar carve-out between buttons (y:337–607) close the inspector but do not activate any tool. Add this as an explicit note in the Priority 3 dispatch block.

---

### `main-menu-new-game-flow.md`

**MM-1** [GAP] MEDIUM
The Main Menu keyboard navigation spec says "Default keyboard focus on launch: 'New Game' button." But the spec does not address what happens if the player launches with a gamepad or other non-keyboard/mouse input device. This is low priority for V1 (desktop-only) but the Main Menu spec should at minimum note that non-keyboard/mouse input is out of scope for V1.
**Proposed resolution**: Add a V1 scope note: "Only keyboard and mouse input are supported in V1. Gamepad/controller support is post-V1."

---

**MM-2** [GAP] MEDIUM
The New Game screen has a "Disaster toggle (checkbox, default off for Easy/Normal, forced off in V1)". The spec says this control is "grayed out in V1" but does not specify via `setElementEnabled()` or `setElementVisible()`. Based on the pattern for the Scenario mode radio button (which is "grayed out with 'Post-launch' label"), the intent is to show the control as disabled. However, the spec for the Disaster toggle does not specify a label suffix (like "Post-launch") or a tooltip explaining why it is disabled. The `settings-pause-menu.md` Gameplay tab specifies the Disaster toggle there uses `setElementEnabled(..., false)` with "Post-launch" label suffix — the New Game screen variant should mirror this.
**Proposed resolution**: Specify that the New Game screen Disaster toggle uses `setElementEnabled(handle, false)` and displays a "(Post-launch)" suffix, matching the pattern in `settings-pause-menu.md`.

---

**MM-3** [GAP] LOW
The `MainMenuPanel → UIManager Communication` section specifies `consumeStartGameRequest()` and `consumeSettingsRequest()` polling flags, but it does not specify a `consumeLoadGameRequest()` polling flag for the Load Game button. The flow says "On activation: the loading-screen path is used (same as New Game start)" but the communication mechanism (polling flag from `MainMenuPanel` to `UIManager`) is not defined for load game, only for new game and settings.
**Proposed resolution**: Add `consumeLoadGameRequest()` to the MainMenuPanel polling contract table with the same consume-once semantics.

---

**MM-4** [GAP] LOW
The loading screen "Cancel" button is specified as "shown in the lower-right" but no virtual bounds are given. During the loading screen, normal HUD layout is not active. The exact position and size of the Cancel button, progress bar, and "Generating terrain..." label are unspecified. These need virtual coordinates for implementation.
**Proposed resolution**: Add virtual coordinate specs for the loading screen progress bar (e.g., centered, width 640 px, y: 490–530 px), status label (centered above bar), and Cancel button (x: 1740–1900, y: 980–1040).

---

### `minimap.md`

**MI-1** [GAP] MEDIUM
The minimap spec defines the `getBounds()` return value as the 200×200 px render area (x: 1720–1920, y: 880–1080) and explicitly excludes the toggle row, label strip, and legend panel. However, the click-to-pan implementation ("Click-to-pan camera to clicked minimap position") maps minimap pixel coordinates to world positions — this transformation formula is never specified. Phase 11 defers the actual camera pan wiring, but even the coordinate mapping formula (minimap pixel → world tile coordinate) is absent.
**Proposed resolution**: Add a "Coordinate Mapping" subsection specifying: `worldX = (minimapClickX - minimapLeft) / minimapW * mapTilesX`, `worldZ = (minimapClickY - minimapTop) / minimapH * mapTilesZ`, where minimap bounds are the 200×200 render area.

---

**MI-2** [GAP] MEDIUM
The minimap Traffic Congestion overlay is specified for V1 but `traffic-system.md` is not yet referenced by any UI spec. The three speed bands (≥40%, 31–39%, ≤30% of free-flow speed) are defined in `minimap.md` but the data source (`ICitySimulation` method that returns per-road-segment speed ratios) is not specified. The minimap spec says "Overlay data is rendered into the minimap texture at budget-tick cadence" but does not define which accessor provides the data.
**Proposed resolution**: Add a cross-reference to the traffic system spec and specify the `ICitySimulation` accessor used to query road segment speed ratios (e.g., `getRoadSegmentSpeedRatio(tileX, tileZ)` returning float in [0, 1]).

---

**MI-3** [INCONSISTENCY] LOW
The minimap spec says the colorblind pattern for Water Tower coverage is "Cross-hatch", and the `resolution-ui-scaling.md` Service Coverage overlay colorblind patterns also say Water Tower = "Cross-hatch". However, the demand pressure bar colorblind patterns in `hud-layout.md` / `resolution-ui-scaling.md` assign "cross-hatch" to Industrial (I), while Water Tower uses cross-hatch for the service overlay. This creates a cross-context pattern collision: players in colorblind mode will see "cross-hatch" meaning both Industrial demand and Water Tower coverage depending on context, with no label differentiation. While this may be acceptable given the different visual contexts (toolbar bar vs. minimap tile), it is worth noting.
**Proposed resolution**: Document the cross-context pattern reuse as intentional and note that context (minimap vs. demand bars) is sufficient disambiguation, OR assign distinct patterns. At minimum, add a note acknowledging the overlap.

---

### `modal-dialog-system.md`

**MD-1** [GAP] HIGH
The "Unsaved changes" modal is referenced in `settings-pause-menu.md` (Quit to Desktop / Quit to Main Menu flows) but its full spec is not defined in `modal-dialog-system.md`. The settings file says it has title "Unsaved Progress", body "You have unsaved changes.", three buttons (Save and Quit / Quit Without Saving / Cancel), and is dismissible via Escape (Cancel). But this modal is not listed in the Modal sizes table or the Visual Design section of `modal-dialog-system.md`, and has no Tab order specification. Specifically, which button gets default Tab focus (least destructive = Cancel) is not stated.
**Proposed resolution**: Add a full `Unsaved Changes Confirmation` modal entry to `modal-dialog-system.md`, including size (Small 480×240 px), Tab order (default focus: Cancel), Escape behavior (Cancel), and button layout.

---

**MD-2** [GAP] MEDIUM
The "Save Failed" modal triggered on manual save failure (Ctrl+S) is described in `settings-pause-menu.md` as "a blocking error modal (not a silent failure): title 'Save Failed', body '[reason]', buttons: 'Retry' / 'Cancel'." This modal is not specified in `modal-dialog-system.md`. There is no size, Tab order, or Escape behavior defined. The modal is described as "dismissable (not forced)" in `settings-pause-menu.md` but the full ModalDialog spec for it is absent.
**Proposed resolution**: Add a `Save Failed Error Modal` section to `modal-dialog-system.md` with size, Tab order (default focus: Retry — the safer/most common recovery action), Escape behavior (Cancel), and confirm that Escape closes without retrying.

---

**MD-3** [GAP] MEDIUM
The forced loan dialog's "last-resort deadlock prevention" path overrides the debt cap with inline text: "Debt cap overridden — emergency credit issued to prevent soft-lock." This scenario (bonds exhausted + all rates at max + no demolishable building) is the only path where the "Back — Accept original loan terms" button force-issues a loan. However, there is no specification for how large this forced loan is when the debt cap is overridden. The regular forced loan formula caps at `3 × max(monthly_revenue, $1,000) − outstanding_debt` but that formula would return 0 or negative when the cap is exhausted. The spec says "force-issues the loan regardless of the outstanding debt cap" but gives no loan amount for the override case.
**Proposed resolution**: Specify a loan amount for the debt-cap-override path, e.g., the standard `monthly_shortfall × 3` formula without the cap constraint, or a fixed emergency amount. Cross-reference `economy-model.md`.

---

**MD-4** [INCONSISTENCY] LOW
The forced loan dialog lists the WASD preset modal as `Small (480×240 px)` and the demolish confirmation as `Small`. The Modal sizes table at the top of the file says "Small = 480×240 px; Medium = 560×320 px; Large = 640×400 px." The demolish confirmation modal body includes "Demolish [N] tiles? You can press Ctrl+Z to undo." and a "Do not ask again" checkbox. At 480×240 px, fitting a title, body text, checkbox, and two buttons is tight. There is no explicit content layout or font-size guidance for the Small modal to verify content fits without scrolling (the rule says "Use the smallest size that fits all required content without scrolling").
**Proposed resolution**: Add a content layout sketch or character-budget note to each modal size to verify the "no scrolling" rule.

---

### `notification-system.md`

**NS-1** [GAP] MEDIUM
The notification log panel spec says "Most-recent notification is at the top of the list" and "Shows the last 50 notifications." But there is no specification for what happens to notifications beyond 50 — are they permanently dropped, archived to a file, or pushed out of the visible list but retained in memory? The "Session persistence" note says the log "is NOT cleared on save or load within the same session" but does not clarify the 50-entry cap semantics (drop oldest vs. ring buffer vs. persistent scroll archive).
**Proposed resolution**: Explicitly state that entries beyond 50 are dropped from the in-memory log (oldest entry is removed when a 51st notification arrives), and this is permanent — they cannot be recovered within the session.

---

**NS-2** [GAP] MEDIUM
The CRITICAL toast keyboard navigation spec says "The first (oldest) CRITICAL toast receives keyboard focus automatically when it becomes visible." But the spec does not address what happens to keyboard focus when the first CRITICAL toast is dismissed — does focus move to the second CRITICAL toast automatically, or does it return to the previously focused element (e.g., a toolbar button)? The "two visible CRITICAL toasts are Tab-navigable" clause implies Tab can move between them, but auto-focus-shift on dismiss is unspecified.
**Proposed resolution**: Add a focus-transfer rule: "When a CRITICAL toast is dismissed, if another CRITICAL toast is visible, keyboard focus moves to it automatically. If no CRITICAL toast remains, focus returns to the previously focused toolbar element or defaults to the first toolbar button."

---

**NS-3** [GAP] LOW
The toast height enforcement note says `NotificationManager` must enforce the 63 px Normal toast height cap "at element creation time." But the actual mechanism for enforcing this via `IUIBackend` is not specified — there is no `setElementHeight()` or `setElementMaxHeight()` method in the IUIBackend interface. If text wraps, the `IGUIStaticText` element grows unless explicitly constrained. How the 63 px cap is enforced (e.g., by passing `h=63` to `addStaticText()` and relying on Irrlicht clipping, or by pre-truncating text) is unspecified.
**Proposed resolution**: Specify the enforcement mechanism: the element is created with `addStaticText(text, x, y, w, 63)` (h=63 px cap applied at creation), relying on Irrlicht's `IGUIStaticText` to clip overflow. Text is pre-truncated to 80 characters before element creation to prevent wrapped-text height growth.

---

**NS-4** [DUPLICATE] LOW
The `dismissCriticalToast(UIElementHandle)` API is described in both the `NotificationManager API` section of `notification-system.md` and in the `CLAUDE.md` "Notes for AI Assistants" section ("`NotificationManager::dismissCriticalToast(UIElementHandle)` is the production API for player-dismissal of CRITICAL toasts"). This is a minor redundancy that increases maintenance surface.
**Proposed resolution**: This is acceptable as `CLAUDE.md` is an overview doc. No action required.

---

### `query-inspector-panel.md`

**QI-1** [GAP] MEDIUM
The panel has 8 rows with `kLineH=33` and 16 px padding, totaling 8×33+16 = 280 px height. This matches `kPanelH=280`. However, the "Updated N seconds ago" label is specified at the bottom of the panel. With 8 data rows already filling the panel, it is unclear whether this label occupies one of the 8 rows or is a 9th row that causes height overflow. A 9th row would require `kPanelH` to be 313 px. If the "Updated" label is inside the 280 px height, one of the 8 rows must be that label, leaving only 7 for data — which may not be enough for the most data-rich tile type (zone tile: 5 fields; service building: 3 fields; road: 3+ fields).
**Proposed resolution**: Either explicitly state that the "Updated" label is one of the 8 counted rows (with a numbered field layout table), or increase `kPanelH` to accommodate it as an extra row, and update the layout constant table accordingly.

---

**QI-2** [GAP] MEDIUM
The Panel has no specified close affordance (e.g., an [X] button in the corner). Dismissal is via pressing I, clicking outside, or pressing Escape. For users who rely solely on mouse input, there is no visible close button on the panel itself — the dismiss mechanism is invisible to them unless they already know the hotkey. This is a usability gap for discoverability.
**Proposed resolution**: Add a close affordance to the panel spec: a small [X] button or close icon at the top-right corner of the panel (within the 340×280 px bounds), with the Glass City focus/hover border spec already defined for it in the visual design section.

---

**QI-3** [GAP] LOW
The data refresh policy distinguishes budget/economy data (every ~120 frames) and traffic data (every 10 frames). However, the policy uses draw-frame count as a proxy for budget-tick cadence. This means the refresh interval is sensitive to actual FPS — at 30 FPS, 120 frames ≈ 4 seconds; at 60 FPS, 120 frames ≈ 2 seconds. The spec acknowledges "approximately 2 s at 60 FPS" but does not specify whether the intent is time-based or frame-based. If frame-based, 30 FPS users get stale data for longer.
**Proposed resolution**: Clarify whether the refresh should be time-based (use `IClock` and `realDeltaSeconds` to accumulate elapsed time toward a 2 s threshold) or frame-count-based as currently specified. Time-based is more robust.

---

**QI-4** [PROBLEM] LOW
The third-fallback edge-snap formula uses `edge_x = cursor_x <= 960 ? 1920 − 340 : 0`. This means when the cursor is exactly at x=960 (horizontal center), the panel snaps to the right edge. However, when the cursor is in the right half (x > 960), the panel snaps to the left edge (x = 0). For a cursor in the right half with a tile near center-screen, snapping to x=0 places the panel far from the tile AND the cursor — the player's eyes must travel the full width of the screen. The snap direction logic appears inverted for ergonomic comfort.
**Proposed resolution**: Review the third-fallback direction: `cursor_x <= 960 → snap right (1920-340)` means cursor in left half → panel at right edge, cursor in right half → panel at left edge. This is the furthest possible panel from the cursor in both cases. Consider whether the intent is nearest edge (right if cursor is right of center) or furthest edge. If the goal is to maximize panel/tile distance, the current logic is correct but should be explicitly justified.

---

### `resolution-ui-scaling.md`

**RS-1** [GAP] MEDIUM
The Colorblind Accessibility section specifies that Zone placement preview/cursor tints must include a zone-type label overlay ('R', 'C', 'I') in colorblind mode. However, the spec for colorblind support for the density-tier-based fixed-color overlay system (Phase 11m and beyond) says "Full colorblind support for the density-tier-based fixed-color overlay system is deferred to a post-V1 colorblind QA pass." This creates a V1 delivery gap: the zone placement cursor in colorblind mode must show zone-type letters, but the zone overlay (the persistent unbuilt-tile color overlay) in colorblind mode does NOT show letters in V1. This inconsistency means colorblind users see zone letters on the hover cursor but not on already-placed unbuilt tiles.
**Proposed resolution**: Document this explicitly as a known V1 limitation: "In V1 (through Phase 12), zone-type letter labels appear on the placement cursor (hover) in colorblind mode but NOT on placed-tile overlays. Full per-tile label rendering is post-V1 scope."

---

**RS-2** [GAP] LOW
The colorblind mode toggle spec says it is "located in Settings > Graphics tab, Accessibility subsection" and "the toggle MUST NOT appear in any other tab or panel." However, there is no spec for the Accessibility subsection's visual layout within the Graphics tab — how is it separated from the Resolution/Vsync/MSAA controls? Is it a labeled section header? A horizontal rule? This affects whether the subsection is discoverable by users who need it.
**Proposed resolution**: Add a brief layout note: "The Accessibility subsection is separated from the main Graphics controls by a 1 px horizontal rule and a 'Accessibility' section header label in `#4A7FA5` mid-blue."

---

**RS-3** [GAP] LOW
The `UIScaler::setViewportSize()` spec says "The main loop MUST call `uiScaler.setViewportSize()` each frame." However, `irrlicht-device-lifecycle.md` describes an 11-step per-frame sequence and does not include `uiScaler.setViewportSize()` as one of the steps. The two specs are inconsistent about when this call happens relative to other frame steps.
**Proposed resolution**: Add `uiScaler.setViewportSize(screenW, screenH)` as an explicit step in the per-frame sequence in `irrlicht-device-lifecycle.md`, positioned before event processing (step 1: poll events), consistent with the `resolution-ui-scaling.md` specification.

---

### `settings-pause-menu.md`

**SP-1** [GAP] HIGH
The Settings panel keyboard navigation spec states "Default focused tab on open: the previously active tab, or Graphics on first open." But the spec does not define where tab state is persisted — is it a `UIManager` member? A `SettingsPanel` member? A settings config file field? If the player exits Settings and re-enters later in the same session, "previously active tab" implies in-memory state. If the game is restarted, "first open" implies `Graphics`. This persistence contract needs to be explicit.
**Proposed resolution**: Add a note that last-active-tab is stored in `SettingsPanel::m_activeTab` (session-only, not persisted to config file). The tab state resets to Graphics on each application launch.

---

**SP-2** [GAP] MEDIUM
The auto-save triggers are described in two places: `settings-pause-menu.md` (every 120 real seconds) and `save-system.md` (every 120 real seconds OR every 5 budget ticks, whichever comes first; also on pause-menu open and on forced loan dialog activation). The `settings-pause-menu.md` description is incomplete — it only mentions the 120-second trigger and omits the 5-tick trigger and the pause-menu-open trigger. This creates a documentation discrepancy where a user reading only the settings spec would not know about the richer auto-save behavior.
**Proposed resolution**: Either update `settings-pause-menu.md` auto-save description to match the full spec in `save-system.md`, or replace it with a cross-reference: "See `architecture/game-design/save-system.md` for the full auto-save trigger list."

---

**SP-3** [GAP] MEDIUM
The Graphics tab has a "Confirm display change?" modal with a 10-second countdown. This is a modal dialog but it is not listed in `modal-dialog-system.md`. Its size, keyboard navigation, Tab order, and Escape behavior are not specified there. The settings spec describes it only briefly ("modal with 10-second countdown").
**Proposed resolution**: Add a `Display Change Confirmation Modal` section to `modal-dialog-system.md` with size (Small 480×240 px is appropriate), Tab order, Escape behavior (auto-revert immediately, same as countdown expiry), and countdown rendering spec (amber `#E8960C` text color matches the Glass City visual design table in `settings-pause-menu.md`).

---

**SP-4** [GAP] MEDIUM
The Pause Menu spec says "Quit to Main Menu" and "Quit to Desktop" both check `m_hasUnsavedChanges`. The resulting modal has three buttons: Save and Quit / Quit Without Saving / Cancel. However, `settings-pause-menu.md` does not specify the keyboard navigation for this modal (which button gets default Tab focus). Based on the global modal rule in `modal-dialog-system.md` (default focus = least destructive = Cancel), the focus should be Cancel. This is not restated for this specific modal.
**Proposed resolution**: Add a note confirming default Tab focus is Cancel (least destructive action per the global modal rule), consistent with the game-over modal and WASD confirmation modal specs.

---

**SP-5** [GAP] LOW
The post-V1 manual save slot picker (3 slots with timestamps) is described with "saving to an occupied slot shows 'Overwrite [slot name]? Yes / Cancel'" confirmation. This is a UI modal flow that will need a full spec when implemented. The current V1 spec does not define the slot picker's virtual dimensions, content layout, or keyboard navigation. While this is post-V1, a placeholder reference in `modal-dialog-system.md` would prevent it from being forgotten.
**Proposed resolution**: Add a "(Post-V1) Save Slot Picker" placeholder entry to `modal-dialog-system.md`.

---

### `ui-manager.md`

**UM-1** [GAP] MEDIUM
The `UIManager` class structure lists `m_finances` (FinancesPanel) as a private member, but there is no corresponding `m_newGamePending` or `m_gameSessionActive` member visible in the private section. Both are referenced in the `transitionToMainMenu()` comment ("Does NOT reset m_gameSessionActive — keeps it true so the next handleNewGameRequest() takes the subsequent-game path (sets m_newGamePending=true)"). These members are not documented in the class structure block, making the flow hard to follow for implementors.
**Proposed resolution**: Add `m_gameSessionActive`, `m_newGamePending`, and `m_pendingQuit` (referenced in `save-system.md`) to the UIManager private members section with brief contracts.

---

**UM-2** [GAP] LOW
The `UIManager::draw()` doc comment says "Render all GUI panels — call AFTER sceneManager->drawAll() and BEFORE endScene()." This is accurate but incomplete relative to the irrlicht-device-lifecycle.md spec which adds "and BEFORE guiEnvironment->drawAll()". The ordering distinction (UIManager::draw() before guiEnvironment->drawAll()) is architecturally critical (UIManager::draw sets visibility state; guiEnvironment::drawAll renders it) but the doc comment in `ui-manager.md` does not state the `guiEnvironment->drawAll()` relationship.
**Proposed resolution**: Update the `draw()` doc comment to: "Call AFTER sceneManager->drawAll() and BEFORE guiEnvironment->drawAll() and endScene()."

---

**UM-3** [DUPLICATE] LOW
The `kOverlayArgbResidential`, `kOverlayArgbCommercial`, `kOverlayArgbIndustrial` constants (and their colorblind variants) are defined in both the `ui-manager.md` Toolbar Carve-Out Constants block (the `ui_constants.h` example) AND in the `hud-layout.md` Zone Colour Overlay section. Both files repeat the same hex values. Any value update requires editing two spec files.
**Proposed resolution**: Make `ui-manager.md` the single canonical definition of the `ui_constants.h` block, and have `hud-layout.md` cross-reference it rather than repeat the hex values.

---

---

## Cross-File Issues

---

**CF-1** [INCONSISTENCY] CRITICAL
**Files**: `hud-layout.md` (Utilities Sub-Panel), `ui-manager.md` (Toolbar Carve-Out Constants)
The Utilities sub-panel layout is contradicted between the two files:
- `hud-layout.md`: 4×1 single-row grid, 4 buttons at 64×40 px, total width = (64×4)+(4×3) = 268 px, top = y:64.
- `ui-manager.md`: 2×2 button grid, each button 96×48 px, total width = 196 px, top = y:176.

The virtual bounds (x:80–348 vs. x:80–276), height (40 px vs. 100 px), button count (4×1 vs. 2×2), and top-y anchor (y:64 vs. y:176) all differ. This cannot be resolved by implementation; one spec must be wrong.
**Proposed resolution**: Resolve the canonical layout. The 2×2 arrangement at y:176 in `ui-manager.md` is more ergonomic (aligns with Utilities toolbar button) but the constants and text must match exactly. Update whichever file is incorrect. This must be fixed before Phase 9 implementation.

---

**CF-2** [INCONSISTENCY] HIGH
**Files**: `hud-layout.md` (demand bar y:664–748), `ui-manager.md` (Toolbar Carve-Out Constants note says demand bars at y:664–744)
The demand bars top and bottom differ by 4 px across the two files: `hud-layout.md` says "y: 664–748 px" and the commentary in `hud-layout.md` also says "y:664–744 px" in an inline layout note within the same document. This is an internal self-contradiction in `hud-layout.md` itself (the heading says y:664–748, the inline note says y:664–744), and `ui-manager.md`'s text says "demand bars (y:664–744)".
**Proposed resolution**: Pick one value. The element height of 56 px for the colored bar columns (y:692–748 per the detailed description) makes 748 the correct bottom. Update all references to use y:664–748.

---

**CF-3** [INCONSISTENCY] HIGH
**Files**: `hud-layout.md` (resource bar y: 0–56 px), `input-arbitration.md` (Priority 3 toolbar carve-out: y: 64–784), `notification-system.md` (CRITICAL band y: 20–116)
The CRITICAL toast band starts at y:20, which is within the resource bar (y: 0–56). No spec defines what happens when a CRITICAL toast overlaps the resource bar visually. The toast Z-order versus resource bar Z-order is unspecified. The notification system spec says CRITICAL toasts are in a "reserved top band" but does not address the visual overlap with the always-present resource bar.
**Proposed resolution**: Add a Z-order note to `notification-system.md` stating that CRITICAL toasts render above the resource bar (higher Z-order) and that the resource bar content at y:20–56 may be partially obscured by CRITICAL toast elements. Alternatively, adjust the CRITICAL band to start below the resource bar at y:64 if 44 px per toast is sufficient.

---

**CF-4** [INCONSISTENCY] MEDIUM
**Files**: `hud-layout.md` (unsaved changes dot at x: 1796–1812), `notification-system.md` (bell icon at x: 1820–1868)
The unsaved changes indicator (x: 1796–1812) and the notification bell icon (x: 1820–1868) are correctly non-overlapping per `hud-layout.md` ("8 px gap before the bell's x:1820 left edge"). However, the time controls block (x: 1600–1796) per the HUD layout description ends at x:1796, meaning the unsaved changes dot (1796–1812) overlaps the right edge of the time controls area by 0 px (they share the edge at 1796). The dot BEGINS at x:1796 which equals the time controls block's right edge. This is an exact edge-share with no visual gap between the speed selector buttons and the dot. Whether this zero-gap is intentional or a rounding artifact is unspecified.
**Proposed resolution**: Verify the time controls right boundary. If x:1796 is the last pixel of the time controls, the dot at x:1796 starts on that boundary — add at least an 8 px gap: set dot to x:1804–1820 and shift the bell accordingly, or verify the 8 px gap is preserved.

---

**CF-5** [INCONSISTENCY] MEDIUM
**Files**: `finances-panel.md`, `settings-pause-menu.md`
The unsaved-changes tracking spec in `settings-pause-menu.md` says `m_hasUnsavedChanges` is set to `true` by "tax rate change" among other actions. `finances-panel.md` confirms "tax changes... DO set `UIManager::m_hasUnsavedChanges = true`." However, `economy-model.md` says tax rates are intentionally NOT reset on New Game start (they persist across `CitySimulation::reset()` calls). This creates a scenario where: player adjusts tax rates in Session 1, saves and quits, relaunches, starts a New Game (resumes at old tax rates), immediately quits without placing anything — `m_hasUnsavedChanges` is `false` because no explicit tax-rate-change event fired in Session 2. The player's inherited tax rates are never saved unless they trigger a save action. The spec for what constitutes a "change" in the context of inherited cross-session tax rates is unspecified.
**Proposed resolution**: Add a note to `settings-pause-menu.md` clarifying that `m_hasUnsavedChanges` is only set by in-session tax rate changes (i.e., player explicitly presses +/- or direct-enters a value). Inherited cross-session tax rates that have not been explicitly changed in the current session do NOT set the flag.

---

**CF-6** [INCONSISTENCY] MEDIUM
**Files**: `input-arbitration.md` (Priority 3 minimap carve-out), `minimap.md` (widget footprint)
`input-arbitration.md` Priority 3 says the full minimap widget footprint for the carve-out uses constants `kMinimapWidgetTopOverlayActive` and `kMinimapWidgetTop` from `ui_constants.h`. However, these constants are not listed anywhere in the `ui-manager.md` Toolbar Carve-Out Constants block, which is the authoritative listing of `ui_constants.h` contents. The constants are referenced but never defined in any spec file with their numeric values.
**Proposed resolution**: Add `kMinimapWidgetTop`, `kMinimapWidgetTopOverlayActive`, `kMinimapLeft = 1576`, `kMinimapRight = 1920`, `kMinimapBottom = 1080` to the `ui_constants.h` block in `ui-manager.md` with their computed values.

---

**CF-7** [INCONSISTENCY] LOW
**Files**: `notification-system.md` (Normal toast start at y:130), `hud-layout.md` (resource bar y: 0–56, grace period indicator y: 60–92)
Normal toasts start at y:130 (fixed, regardless of CRITICAL count). The grace period indicator occupies y: 60–92 (directly below the resource bar). A Normal toast starting at y:130 and ending at y:193 (one 63 px toast) does not overlap the grace period indicator at y:60–92. However, during the early-game window when both a Normal toast and the grace period indicator are visible simultaneously, they occupy y:60–193. The spec does not verify that this is acceptable and does not mention the visual stacking of these two elements.
**Proposed resolution**: Add a note confirming that the grace period indicator (y:60–92) and Normal toasts (starting y:130) have a 38 px gap and do not overlap. Mark this as a verified layout constraint.

---

**CF-8** [DUPLICATE] LOW
**Files**: `input-arbitration.md`, `hud-layout.md`
The Zone Rectangular Selection interaction sequence (press anchor, drag preview, release fill) is described in both `hud-layout.md` (Zone Rectangular Selection section) and `input-arbitration.md` Priority 7 (Zone tool — rectangular drag-select). The two descriptions are largely consistent but the `input-arbitration.md` version is more complete (includes the `freeTiles`/`blockedTiles` partition and `setTilePlacementPreview` calls). The `hud-layout.md` version describes the same sequence without the preview color detail.
**Proposed resolution**: In `hud-layout.md`, replace the full interaction sequence with a cross-reference: "See `input-arbitration.md` Priority 7 for the authoritative Zone tool rectangular drag-select interaction spec." Keep only the "V1 Option B" hover-only preview note in `hud-layout.md` if it is not redundant.

---

---

## Missing Specs

---

**MISSING-1** [MISSING] HIGH
**Loading Screen UI Spec**
The loading screen is referenced throughout multiple files (main-menu-new-game-flow.md, modal-dialog-system.md game-over section, game-over-flow.md "Load Last Save" path, save-system.md) but has no dedicated spec file. Required content: virtual coordinate layout of progress bar, status label, and Cancel button; font and color choices (Glass City palette); progress data source; minimum display time handling (0.5 s floor); Cancel button state transitions; loading screen → gameplay transition signal (`onGameLoaded()` call timing).
**Proposed resolution**: Create `architecture/ui-ux/loading-screen.md` with a full spec.

---

**MISSING-2** [MISSING] HIGH
**HUD Resource Bar Full Element Layout**
The resource bar (y: 0–56 px, full width) contains multiple elements (treasury balance, debt indicator, City Rating label, population count, date/time display, and the density unlock progress indicator from `economy-model.md`). No spec defines the horizontal position of each element within the bar (x ranges, alignment, order from left to right, separators between fields). The only visual spec is the Glass City background colour. With 6+ elements in a 1920 px wide bar, the layout needs to be fully specified.
**Proposed resolution**: Add a `Resource Bar Element Layout` section to `hud-layout.md` with per-element virtual x ranges, text alignment, separator positions, and the element ordering (e.g., [Treasury | Debt] [Population] [Date] [City Rating] [Density Unlock Progress] [Grace period]).

---

**MISSING-3** [MISSING] MEDIUM
**City Rating Display Spec**
The resource bar includes a "City Rating label (`getCityRating()` → `CityRatingTier` display name)" but no spec defines what `CityRatingTier` values look like in the HUD (e.g., "Bronze City", "Silver City", "Gold City", or "Rating: B+"). The color of the rating label (presumably `#F0B429` amber for values or `#EBF4F6` near-white for labels) is not specified. The stinger_milestone event in `ui-manager.md` fires on upward `CityRatingTier` transitions — but the tier names themselves are not documented in any UI spec file.
**Proposed resolution**: Add a `City Rating Display` section to `hud-layout.md` or `hud-layout.md` referencing `architecture/game-design/game-progression-modes.md` for tier names, with the display format and color.

---

**MISSING-4** [MISSING] MEDIUM
**Outstanding Debt Indicator Full Spec**
The resource bar mentions an "outstanding debt indicator (`getOutstandingDebt()` — hidden when debt is zero)" but provides no visual spec: no virtual bounds, no color (is it `#F04E37` red? amber?), no format (e.g., "Debt: $12,500" or just a red badge with the amount), no tooltip content. This element is also referenced in `hud-layout.md`'s notification system cross-reference and in the loan/economy model but never fully specified as a HUD element.
**Proposed resolution**: Add a `Debt Indicator` subsection to `hud-layout.md` with format, color (`#F04E37` red, consistent with deficit indicator), virtual bounds, tooltip, and hide/show conditions.

---

**MISSING-5** [MISSING] MEDIUM
**"Paused — [event name]" Indicator in Time Controls**
`notification-system.md` specifies: "A 'Paused — [event name]' indicator appears in the Time Controls area to distinguish CRITICAL-toast-pause from player-initiated pause; after CRITICAL toast dismissal the indicator changes to the standard 'Paused' label." The Time Controls area (x: 1600–1796, y: 8–56) has the four speed buttons (Pause/1×/3×/10×). There is no spec for where this additional text indicator appears within the Time Controls area, what color it uses, or what the "standard 'Paused' label" looks like. Whether it is a 5th element in the speed controls row or an overlay on the Pause button is unspecified.
**Proposed resolution**: Add a `Pause State Indicator` section to `hud-layout.md` Time Controls specifying: the indicator's virtual bounds, text format ("Paused — [event]" vs. "Paused"), color (`#E8960C` warning amber for CRITICAL-toast pause; `#EBF4F6` near-white for player-initiated pause), and how it coexists with the four speed buttons.

---

**MISSING-6** [MISSING] LOW
**Post-V1 Scenario Mode New Game Screen**
The New Game screen has a Scenario mode radio button grayed out with "Post-launch" label. No spec describes what the Scenario mode new game flow would look like post-V1 (scenario selection, difficulty presets, scenario-specific options). While this is intentionally deferred, a placeholder stub in `main-menu-new-game-flow.md` would prevent the flow from being designed inconsistently in a future phase.
**Proposed resolution**: Add a brief "(Post-launch) Scenario Mode New Game" stub section to `main-menu-new-game-flow.md` noting it is out of scope for V1 and will require a dedicated spec.

---

**MISSING-7** [MISSING] LOW
**Notification Bell Unread Count Badge Spec**
`notification-system.md` specifies the unread count badge has "`#F04E37` red circular badge with `#EBF4F6` white numeral." But the badge dimensions, positioning relative to the bell icon (top-right corner? bottom-right?), font size, and behavior when count exceeds 9 (single digit → "9+" or "99+"?) are unspecified.
**Proposed resolution**: Add a `Bell Icon Unread Badge` subsection with: badge diameter (16 px), position (top-right of bell icon, partially overlapping), font size (minimum 9 px virtual), overflow behavior ("9+" when count > 9).

---

---

## Summary Table

| ID | File | Type | Severity | Short Description |
|---|---|---|---|---|
| HL-1 | `hud-layout.md` | GAP | HIGH | Density Unlock Progress Indicator section missing — referenced by `economy-model.md` |
| HL-2 | `hud-layout.md` | GAP | HIGH | Wages not in HUD resource bar spec but `economy-model.md` says it is |
| CF-1 | `hud-layout.md` / `ui-manager.md` | INCONSISTENCY | CRITICAL | Utilities sub-panel layout contradicted (4×1 @64px vs 2×2 @96px, width 268 vs 196, y:64 vs y:176) |
| CF-2 | `hud-layout.md` (internal) | INCONSISTENCY | HIGH | Demand bar bottom y contradicted within same file (748 vs 744) |
| CF-3 | `hud-layout.md` / `notification-system.md` | INCONSISTENCY | HIGH | CRITICAL toast band starts at y:20, within resource bar (y:0–56); Z-order unspecified |
| HS-1 | `hotkey-scheme.md` | PROBLEM | HIGH | Space key arbitration path through 6-level priority chain never specified |
| MD-1 | `modal-dialog-system.md` | GAP | HIGH | Unsaved Changes modal spec missing (referenced from settings but not defined in modal spec) |
| MISSING-1 | — | MISSING | HIGH | No loading-screen UI spec file |
| MISSING-2 | — | MISSING | HIGH | Resource bar element layout (horizontal positions) never fully specified |
| SP-1 | `settings-pause-menu.md` | GAP | HIGH | Settings tab persistence (session vs. config-file) not specified |
| HL-3 | `hud-layout.md` | GAP | MEDIUM | Utilities sub-panel width 268 vs. 196 in ui-manager.md constants |
| HL-4 | `hud-layout.md` / `ui-manager.md` | INCONSISTENCY | MEDIUM | Utilities sub-panel top-y: 64 vs. 176 across files |
| HL-5 | `hud-layout.md` | GAP | MEDIUM | In-game date/time display format, accessor, and monospace requirement unspecified |
| IA-1 | `input-arbitration.md` | GAP | HIGH | Road tool mid-drag ray-cast miss behavior unspecified |
| IA-2 | `input-arbitration.md` | GAP | MEDIUM | Toolbar carve-out y:657–783 click behavior (between buttons) never stated |
| IA-3 | `input-arbitration.md` | GAP | MEDIUM | Escape-during-capture interaction with other pending keybinding changes unspecified |
| CF-4 | `hud-layout.md` / `notification-system.md` | INCONSISTENCY | MEDIUM | Unsaved dot and time controls share x:1796 edge — zero-gap ambiguity |
| CF-5 | `finances-panel.md` / `settings-pause-menu.md` | INCONSISTENCY | MEDIUM | m_hasUnsavedChanges and cross-session inherited tax rates: when flag is set unspecified |
| CF-6 | `input-arbitration.md` / `ui-manager.md` | INCONSISTENCY | MEDIUM | kMinimapWidgetTop / kMinimapWidgetTopOverlayActive referenced but never defined with values |
| FP-1 | `finances-panel.md` | GAP | MEDIUM | Finances panel y anchor not specified (only "below resource bar") |
| HS-2 | `hotkey-scheme.md` | GAP | MEDIUM | Numpad +/- speed keys not addressed |
| IA-4 | `input-arbitration.md` | GAP | LOW | `UIManager::isGameplayOrPaused()` referenced but not declared in UIManager class spec |
| MD-2 | `modal-dialog-system.md` | GAP | MEDIUM | Save Failed error modal not defined in modal spec |
| MD-3 | `modal-dialog-system.md` | GAP | MEDIUM | Debt-cap-override loan amount for last-resort deadlock path unspecified |
| MI-1 | `minimap.md` | GAP | MEDIUM | Click-to-pan coordinate mapping formula (minimap px → world tile) not specified |
| MI-2 | `minimap.md` | GAP | MEDIUM | Traffic overlay data source accessor (`ICitySimulation` method) not specified |
| MM-3 | `main-menu-new-game-flow.md` | GAP | LOW | consumeLoadGameRequest() polling flag not defined |
| MM-4 | `main-menu-new-game-flow.md` | GAP | LOW | Loading screen element virtual bounds not specified |
| MISSING-3 | — | MISSING | MEDIUM | City Rating display format and color not specified in any UI file |
| MISSING-4 | — | MISSING | MEDIUM | Outstanding debt indicator HUD spec missing |
| MISSING-5 | — | MISSING | MEDIUM | "Paused — [event]" text indicator in Time Controls area unspecified |
| NS-1 | `notification-system.md` | GAP | MEDIUM | Notification log cap behavior (>50 entries) not specified |
| NS-2 | `notification-system.md` | GAP | MEDIUM | CRITICAL toast keyboard focus transfer on dismiss not specified |
| QI-1 | `query-inspector-panel.md` | GAP | MEDIUM | "Updated N seconds ago" label vs. 8-row height budget unresolved |
| QI-2 | `query-inspector-panel.md` | GAP | MEDIUM | No visible close affordance (X button) on inspector panel |
| SP-2 | `settings-pause-menu.md` | GAP | MEDIUM | Auto-save description incomplete vs. save-system.md |
| SP-3 | `settings-pause-menu.md` | GAP | MEDIUM | Display Change Confirmation modal not defined in modal-dialog-system.md |
| SP-4 | `settings-pause-menu.md` | GAP | MEDIUM | Quit confirmation modal Tab order/default focus not stated |
| CC-1 | `camera-controls.md` | GAP | MEDIUM | Named constants (kBasePanSpeed etc.) have no declared canonical home |
| CC-2 | `camera-controls.md` | GAP | MEDIUM | Sensitivity slider applicability to RMB rotate not stated |
| FP-3 | `finances-panel.md` | INCONSISTENCY | LOW | Surplus green #80C850 not in Glass City canonical palette table |
| HS-3 | `hotkey-scheme.md` | GAP | LOW | Chord vs. single-key conflict exclusion rule not specified |
| IA-5 | `input-arbitration.md` | DUPLICATE | LOW | Finances Panel dismiss behavior described in both input-arbitration.md and finances-panel.md |
| IA-6 | `input-arbitration.md` | PROBLEM | LOW | Inspector dismiss + toolbar carve-out gap at y:337–607 produces silent clicks |
| MI-3 | `minimap.md` | INCONSISTENCY | LOW | Cross-hatch pattern used for both Water Tower (minimap) and Industrial (demand bars) in colorblind mode |
| MISSING-6 | — | MISSING | LOW | No post-V1 Scenario mode new game flow placeholder |
| MISSING-7 | — | MISSING | LOW | Bell icon unread count badge dimensions and overflow not specified |
| MM-2 | `main-menu-new-game-flow.md` | GAP | MEDIUM | Disaster toggle disabled-state pattern not specified (no "Post-launch" label suffix) |
| NS-3 | `notification-system.md` | GAP | LOW | 63 px height enforcement mechanism via IUIBackend not specified |
| QI-3 | `query-inspector-panel.md` | GAP | LOW | Data refresh frame-count vs. time-based policy ambiguous; FPS-sensitive |
| QI-4 | `query-inspector-panel.md` | PROBLEM | LOW | Third-fallback edge-snap direction unintuitive (panel placed furthest from cursor) |
| RS-1 | `resolution-ui-scaling.md` | GAP | MEDIUM | V1 colorblind gap: hover cursor shows zone letters but placed tile overlays do not |
| RS-2 | `resolution-ui-scaling.md` | GAP | LOW | Accessibility subsection layout within Graphics tab not specified |
| RS-3 | `resolution-ui-scaling.md` / `irrlicht-device-lifecycle.md` | INCONSISTENCY | LOW | setViewportSize() not in the 11-step frame loop spec |
| CF-7 | `notification-system.md` / `hud-layout.md` | INCONSISTENCY | LOW | Normal toast / grace period indicator visual stacking not verified |
| CF-8 | `hud-layout.md` / `input-arbitration.md` | DUPLICATE | LOW | Zone rectangular selection described in both files |
| CC-3 | `camera-controls.md` | GAP | LOW | Edge-scroll and letterbox/pillarbox black bars — spurious scroll in bars not addressed |
| CC-4 | `camera-controls.md` | GAP | LOW | Full CameraController constructor signature not declared |
| FP-2 | `finances-panel.md` | GAP | LOW | Key-repeat cap interaction with direct-entry activation unspecified |
| FP-4 | `finances-panel.md` | GAP | LOW | IAudioSystem forward-declaration pattern not specified for FinancesPanel.h |
| HL-6 | `hud-layout.md` | GAP | LOW | Demand bar visual meaning (direction: full=high unmet demand) not labeled |
| HL-7 | `hud-layout.md` | GAP | LOW | setMouseCursor() IUIBackend method 22 placeholder absent from interface listing |
| HS-4 | `hotkey-scheme.md` | GAP | LOW | WASD preset with pre-existing custom W/A/S/D bindings behavior not cross-referenced |
| MD-4 | `modal-dialog-system.md` | INCONSISTENCY | LOW | Small (480×240) modal content fit for demolish confirmation (title+body+checkbox+2 buttons) not verified |
| MM-1 | `main-menu-new-game-flow.md` | GAP | MEDIUM | Non-keyboard/mouse input not explicitly scoped out |
| NS-4 | `notification-system.md` | DUPLICATE | LOW | dismissCriticalToast API described in both notification-system.md and CLAUDE.md |
| SP-5 | `settings-pause-menu.md` | GAP | LOW | Post-V1 save slot picker has no placeholder in modal-dialog-system.md |
| UM-1 | `ui-manager.md` | GAP | MEDIUM | m_gameSessionActive, m_newGamePending, m_pendingQuit not in UIManager class structure |
| UM-2 | `ui-manager.md` | GAP | LOW | draw() doc comment missing guiEnvironment->drawAll() ordering relationship |
| UM-3 | `ui-manager.md` | DUPLICATE | LOW | Zone overlay ARGB constants defined in both ui-manager.md and hud-layout.md |
