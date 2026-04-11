# Modal Dialog System

- A `ModalDialog` component owned by `UIManager`; blocks scene input while active
- **Modal sizes** (virtual 1920×1080 space): Small = 480×240 px; Medium = 560×320 px; Large = 640×400 px. Use the smallest size that fits all required content without scrolling. The forced loan dialog uses **Large (640×400 px minimum)**; unsaved-changes confirmation uses Small; game-over modal uses Medium.
- **Background scrim**: A full-screen semi-transparent black overlay (50% opacity) is drawn above the 3D scene and above all HUD/panel elements, but below the ModalDialog content itself, when any blocking modal is active. The scrim is rendered as a single solid `IGUIElement` fill rect covering the full virtual 1920×1080 area. The scrim dims the city background to focus player attention on the modal, without hiding it entirely.
- Structure: title, body text, up to four action buttons (primary + secondary + tertiary + cancel/back)
- **Keyboard navigation** (required for desktop accessibility): All modal buttons are Tab-navigable in document order. The default-focused button is the least destructive action (Cancel/Decline). Enter activates the focused button. Escape activates the Cancel/safe-exit button where the modal is dismissible (consumed by the modal focus layer where it is not). Visual focus ring: 2px accent-color border on the focused button. This is a V1 requirement on all desktop platforms.
- **Forced loan dialog** (V1): Blocking modal (Large, 640×400 px) — **two-screen structure**:
  - **Screen 1 — Accept / Decline** (shown first): Title: "Budget Crisis — Loan Required". Body shows:
    - Single summary line: "Revenue: $X/month | Deficit: $Y/month"
    - Loan terms block: amount, interest rate (5%/year), estimated repayment per tick ($/month)
    - Two buttons: **Accept** (primary) and **Decline** (opens Screen 2).
    - A collapsible "Show details ▼" section (collapsed by default) expands to show: name/type/tile of last-placed building with "Highlight on map" button; projected tax rates per zone type after a ×1.10 raise. This keeps Screen 1 scannable while preserving the detailed context for players who want it.
  - **Screen 2 — Budget Action Required** (shown after Decline): Title: "Budget Action Required". Full alternative-action listing (Options 1–3 + Back button). All detailed context visible here:
    - Current monthly revenue and current monthly deficit (side-by-side)
    - Last-placed building: name, type, tile coordinates + "Highlight on map" button
    - Projected tax rates per zone type after raise, formatted as e.g. "Residential: 8% → 8.8%"
  - **Accept** (Screen 1): loan amount credited to treasury immediately; dialog dismissed; toast confirmation shown
  - **Decline**: dialog transitions to a "Budget Action Required" sub-screen listing available alternative actions. **The forced loan dialog cannot be fully dismissed without resolving the budget crisis** — neither Escape nor clicking outside the modal will close it until the player accepts the loan or chooses an alternative. Escape key is **consumed** by the ModalDialog focus layer (see Input Arbitration) and must not open the pause menu while any blocking modal is active.
    - **Option 1 — Raise tax rates**: Raise all zone tax rates by a **multiplicative factor of ×1.10** (each rate is multiplied by 1.10, then capped at the 25% ceiling). Example: Residential at 8% becomes 8% × 1.10 = 8.8%. A rate at 23% becomes 23% × 1.10 = 25.3%, capped to 25%. Button shows projected rates per zone type (formatted as e.g. "Residential: 8% → 8.8%"). If projected surplus after raise is still negative, display inline warning: "This will not resolve your deficit — further action may be required next budget cycle." If all zone types are already at the 25% ceiling, this button is grayed out with tooltip "All tax rates are already at maximum."
    - **Option 2 — Demolish last-placed building**: Shows name, type, and tile coordinates. If no "last-placed building" exists in undo history (e.g., deficit caused by organic decline, not placement), this button is grayed out with tooltip "No recent building to demolish." If the last player action was a road (not a building), the button targets the most recently placed road segment.
    - **Option 3 — Emergency municipal bond** (escape valve): One-time loan at **5%/year (same rate as forced loans — the bond's distinguishing cost is the doubled principal and 24-tick repayment period)** — see Economy Model for bond terms; the rate must match `economy-model.md` exactly), available a limited number of times per game total — **Easy: 3 uses, Normal: 2 uses, Hard: 1 use** (Hard mode is most constrained; Easy gets more to soften learning curve). The button label includes a remaining-uses indicator: "Emergency Bond (N remaining)" where N decrements after each use. When N = 0, the button is grayed out with tooltip "Emergency bonds exhausted." Shows terms: amount, 5%/year rate, repayment schedule. This option ensures the player always has at least one viable exit from the modal in early game, preventing soft-lock in structurally insolvent cities in Sandbox mode.
    - **Back / Accept original loan**: A fourth button labeled "Back — Accept original loan terms" is always available at the bottom of the Budget Action Required sub-screen. Clicking it returns to the main forced loan Accept/Decline screen showing the original standard loan terms (5%/year). This button is always enabled and cannot be grayed out. **Tab order on this sub-screen**: default Tab focus is on **Option 1 (Raise tax rates)** — the least destructive available action — consistent with the global modal rule (line above: "The default-focused button is the least destructive action"). The Back button is Tab-last (accessible via Shift+Tab or Tab through all three options). This prevents accidentally accepting the original loan by pressing Enter twice in rapid succession after declining Screen 1; the player must explicitly Tab to Back or click it.
    - Player must pick one available option. All three action buttons plus the Back button display with their current availability state before the player takes any action. **Last-resort deadlock prevention**: If all three action options are simultaneously grayed out (bonds exhausted + all tax rates at 25% + no demolishable building — possible in Sandbox mode after prolonged organic decline), the "Back — Accept original loan terms" button force-issues the loan regardless of the outstanding debt cap. This overrides the cap as a true last resort and is the only path out of the modal in this scenario. A warning is displayed inline: "Debt cap overridden — emergency credit issued to prevent soft-lock."
  - **Auto-pause**: The simulation **must be automatically paused** when the forced loan dialog (or any blocking modal) becomes active. The simulation must not continue running behind a blocking modal. This prevents the cascade scenario at high simulation speeds (10×) where upkeep costs compound while the player interacts with the modal. The speed selector is grayed out while any blocking modal is active. **Resume condition**: The simulation remains paused until the blocking modal is fully dismissed, but ONLY if the modal was the entity that initiated the pause. The `ModalDialog` tracks whether it called `setPaused(true)` on open via a private boolean flag `m_didPauseSim`. On open: if the simulation is not already paused, the modal calls `m_sim->setPaused(true)` and sets `m_didPauseSim = true`; if the simulation is already paused (e.g., by user-pressed Pause key or by a CRITICAL-toast auto-pause), the modal does NOT call `setPaused(true)` and sets `m_didPauseSim = false`. On `closeModal()`: if `m_didPauseSim` is `true`, the modal calls `m_sim->setPaused(false)` and clears the flag; if `m_didPauseSim` is `false`, `closeModal()` does NOT call `setPaused(false)` — this prevents accidentally resuming a simulation that was paused for other reasons. **No auto-resume based on CRITICAL-queue state**: `closeModal()` never unconditionally calls `setPaused(false)` based on whether the CRITICAL toast queue is empty; the CRITICAL toast system manages its own pause state independently of `closeModal()`.

  **Session-ending modal dismiss (game-over modal)**: When a game-over modal is dismissed via "Load Last Save" or "Return to Main Menu", `closeModal()` is still called and the normal `m_didPauseSim` resume path executes (calling `setPaused(false)` if applicable). This brief resume call is harmless: the loading controller or main-menu transition that follows immediately destroys the current simulation session. `setPaused(false)` on a simulation that is about to be replaced has no observable effect. The caller (UIManager game-over handler) does NOT need to suppress the `setPaused(false)` call for game-over dismissals — the simulation teardown that follows renders the pause state irrelevant.

- **Demolish confirmation modal** (V1, Small 480×240 px, dismissible): fires after the
  player completes a demolish drag-select and releases the mouse button, when "Confirm
  before demolish" is enabled in Settings > Gameplay. Title: "Confirm Demolish". Body:
  "Confirm Demolish? [N] tile(s) will be demolished. You can press Ctrl+Z to undo." —
  where N is the pre-counted number of occupied tiles (`isZoned || isRoad ||
  serviceType != None`) in the selected rectangle. The modal is suppressed entirely if
  N = 0 (empty selection, nothing to demolish). Two buttons: **Yes** (primary) and
  **Cancel** (safe-exit, default Tab focus per global modal rule). Keyboard: Tab
  navigates between buttons; Enter activates the focused button; Escape activates
  Cancel. On **Yes**: `UIManager::updateModalDialogState()` iterates the stored
  rectangle and calls `ICitySimulation::demolishTile()` for each occupied tile, then
  updates the zone overlay and sets unsaved-changes. On **Cancel**: highlight and
  selection state are cleared; no tiles are demolished. The "Confirm before demolish"
  toggle in Settings > Gameplay controls whether this modal fires; when OFF, occupied
  tiles are demolished immediately on mouse-up without any dialog.

- **WASD camera preset confirmation modal** (triggered from Settings > Controls when player clicks the "WASD" preset button): Small modal (480×240 px). Title: "Apply WASD Preset?". Body: "This will change: W=Pan Up, A=Pan Left, S=Pan Down, D=Pan Right (Demolish moved to X). Any custom bindings for W/A/S/D will be overwritten." The modal also displays the current binding of each affected key (W, A, S, D) so the player understands what will be replaced before committing. Two buttons: **Apply** (primary) and **Cancel** (safe-exit). Escape activates Cancel. **Keyboard Tab order**: default focus on **Cancel** (least destructive per global modal rule). Tab moves to Apply. On Apply: atomically rebind PanUp=W, PanDown=S, PanLeft=A, PanRight=D and move Demolish from D to X; write `keybindings.json`; close modal. On Cancel: no change, close modal. This modal is dismissible (Escape closes it via Cancel action). **Note**: Q and E are independently reserved for future camera controls (displayed as non-rebindable in the Controls tab regardless of preset); this is a separate constraint unrelated to the WASD preset itself.

## Visual Design — Glass City

All modal dialogs use the Glass City panel style regardless of dialog type.

### Modal Background

- **Dialog background**: `rgba(13, 27, 42, 0.88)` deep navy, **8 px corner radius** on all
  four edges. The dialog is not flush with any screen edge so all corners receive the radius.
- **Scrim** (full-screen behind dialog): solid `rgba(0, 0, 0, 0.50)` — unchanged from the
  existing 50% opacity black overlay spec. The scrim colour is not part of the Glass City
  panel palette; it is a viewport dimming layer.

### Modal Text

| Content | Colour |
|---|---|
| Title | `#EBF4F6` near-white |
| Body text | `#EBF4F6` near-white |
| Numeric values in body (loan amount, interest rate, monthly figures) | `#F0B429` amber |
| Inline warning text ("This will not resolve your deficit…") | `#E8960C` warning amber |
| Inline error / overridden-cap warning | `#F04E37` red |
| Secondary detail text (subheadings, field names) | `#4A7FA5` mid-blue |

### Modal Buttons

All modal action buttons (primary, secondary, tertiary, cancel/back) use the Glass City
button tile:

- **Default / unfocused**: `rgba(255, 255, 255, 0.08)` fill, 1 px `rgba(255, 255, 255, 0.18)` border
- **Keyboard focus / hover**: `rgba(255, 255, 255, 0.15)` fill, 1 px `rgba(255, 255, 255, 0.35)` border
- **Primary action (when indicated)**: `rgba(0, 201, 200, 0.22)` teal wash, 2 px
  `rgba(0, 201, 200, 0.75)` teal border + 4 px baked glow

The keyboard focus ring specified in the existing spec ("2px accent-color border on the
focused button") is satisfied by the Active tile state above — the 2 px teal border IS the
focus ring. No additional focus ring element is needed.

Grayed-out (disabled) buttons use `setElementEnabled(..., false)` and render at reduced
opacity; the teal border is absent on disabled buttons.

## Element Repositioning

`ModalDialog` elements (background, title, body, buttons) are created once at construction with placeholder coordinates and repositioned on each `openModal()` call using `IUIBackend::setElementRect(handle, x, y, w, h)` (method 20). This avoids destroying and recreating handles — which would invalidate test expectations and listener registrations — and allows the dialog to be correctly centred at any screen resolution.

**Positioning sequence** (called from `setDialogRect()` + each `layout*()` method):

1. `setDialogRect(w, h)` computes the centred origin `(dialogX, dialogY)` from the virtual canvas size and stores it as `m_dialogX/Y/W/H`.
2. `setElementRect(m_dialogBg, m_dialogX, m_dialogY, m_dialogW, m_dialogH)` repositions the background panel.
3. Each `layout*()` method calls `setElementRect()` on the title label, body label, and each button relative to `(m_dialogX, m_dialogY)`.

`IrrlichtUIBackend::setElementRect()` updates the stored `virtualRect` (so `handleViewportResize()` continues to scale correctly at non-native resolutions) and immediately calls `IGUIElement::setRelativePosition()` at the current physical resolution.

- **Game-over modal** (Scenario mode only, Medium 560×320 px, non-dismissible): Title: "City Bankrupt". Body: displays current total debt and number of consecutive months in deficit. Two buttons: **Load Last Save** (primary, less destructive) and **Return to Main Menu**. Keyboard nav: all buttons Tab-navigable; default focus on **Load Last Save** (least destructive action per global modal rule). Escape is **consumed** by the modal — it does not open the pause menu (the game has ended; there is no gameplay to resume). Neither Escape nor clicking outside the modal closes it without button activation — this is non-dismissible. On **Load Last Save**: the modal is dismissed, the current simulation state is destroyed, and the save file is loaded using the **same loading-screen path as a New Game start** — a loading spinner is shown while `TerrainSystem::flushPendingRebuilds()` runs and the save data is deserialised; upon completion, `GameState` transitions to `Gameplay`. The loading controller must call `UIManager::onGameLoaded()` after deserialization completes and before the first `UIManager::update()` tick (see `architecture/ui-ux/ui-manager.md` § `onGameLoaded()`). This is NOT a direct synchronous `GameOver → Gameplay` transition; the loading screen is required because terrain chunks must be rebuilt from the saved seed, which is a non-trivial operation that must not block the render thread without user feedback. On **Return to Main Menu**: clean simulation shutdown, then transition to main menu. Both paths perform a clean simulation shutdown before transitioning — no abrupt exit. The game-over modal auto-pauses the simulation on appearance (same as all blocking modals) and the auto-pause is never released — the session ends after this modal.
