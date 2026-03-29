# Notification System

- **Persistent indicators**: Budget in red when negative; debt indicator; power/water coverage warnings as persistent HUD icons
- **Transient toasts**: Displayed top-center; CRITICAL band: up to 2 simultaneously; Normal band: up to 3 simultaneously (reduced when CRITICAL band is occupied — see priority separation below). **Auto-dismiss (5 seconds) applies only to Normal queue toasts.** CRITICAL queue toasts do NOT auto-dismiss — they require explicit player dismissal (click, Enter, or Delete).
- **Priority separation**: Toasts are separated into two queues with **dedicated vertical screen bands** to prevent overlap:
  - **CRITICAL queue** (fire, bankruptcy, power/water outage, deficit-streak warnings): displayed in a **reserved top band at virtual y: 20–116 px** (fixed 48 px per item — **CRITICAL toasts are fixed-height at 48 px each**; 2 toasts × 48 px = 96 px content fits exactly within the 96 px band height. Title + single-line body + dismiss affordance + padding all fit at 48 px; see toast height constraints below), max 2 visible simultaneously (rest remain queued); never silently dropped; each CRITICAL toast must be explicitly dismissed by the player (click, Enter, or Delete — Delete added as alternative to avoid interrupting mouse work). Maximum 2 CRITICAL toasts visible at once; further CRITICAL toasts queue behind them.
  - **CRITICAL toast auto-pause**: The simulation is automatically paused (equivalent to pressing Space/Pause) when the CRITICAL notification queue transitions from empty to non-empty. **Auto-resume requires explicit player action**: the simulation does NOT automatically resume when the CRITICAL queue is cleared (all CRITICAL toasts dismissed). The player must explicitly unpause (press Space or click the Pause button) after dismissing CRITICAL toasts. This prevents unexpected simulation restarts after the player pauses manually and then dismisses CRITICAL toasts. The `NotificationManager` calls `setPaused(true)` when the first CRITICAL toast arrives; it does NOT call `setPaused(false)` when the last CRITICAL toast is dismissed — the player is responsible for unpausing. During auto-pause triggered by a CRITICAL toast, the speed selector remains accessible (player can change speed setting for when the simulation resumes). A "Paused — [event name]" indicator appears in the Time Controls area to distinguish CRITICAL-toast-pause from player-initiated pause; after CRITICAL toast dismissal the indicator changes to the standard "Paused" label. **Priority rule**: If a blocking modal is simultaneously active, the modal takes precedence — the speed selector is grayed out regardless of CRITICAL toast state. The CRITICAL-toast auto-pause rule applies only when no blocking modal is present.

  **Speed selector state matrix** (to prevent ambiguity when states combine):

  | State | Simulation | Speed selector |
  |---|---|---|
  | No modal, no CRITICAL toast | Running | Enabled |
  | CRITICAL toast only (no modal) | Auto-paused | **Enabled** — player can pre-set speed for resume |
  | Modal only (no CRITICAL toast) | Paused | **Disabled** (grayed out) |
  | Modal + CRITICAL toast | Paused | **Disabled** (modal wins) |
  | Modal dismissed, CRITICAL toast remains | Auto-paused (re-evaluated) | **Enabled** — back to CRITICAL-toast-only rule |

  The "modal dismissed with queued CRITICAL toast" row is the non-obvious transition: after `closeModal()`, CRITICAL toasts become visible and auto-pause is re-evaluated, but the speed selector transitions back to **ENABLED** because the modal is gone. The player must manually unpause to resume simulation (auto-resume is never performed by `NotificationManager`).

  **Auto-resume disambiguation — `dismissCriticalToast()` vs. `closeModal()`**: These two operations have distinct resume semantics and must not be conflated:

  - `NotificationManager::dismissCriticalToast(UIElementHandle)` — called when the player clicks the dismiss button, presses Enter, or presses Delete on a CRITICAL toast. This does NOT auto-resume the simulation. The simulation stays paused regardless of whether the CRITICAL queue becomes empty after dismissal. The player must explicitly unpause (Space or Pause button). This is the authoritative "no auto-resume" rule for CRITICAL toasts.
  - `UIManager::closeModal()` — called when a blocking modal dialog (forced loan, game-over) is dismissed. `closeModal()` calls `m_sim->setPaused(false)` ONLY if the modal itself set `m_didPauseSim = true` when it opened (i.e., the modal was the entity that called `setPaused(true)`). If the simulation was already paused before the modal opened (e.g., by user-pressed Pause key or by a CRITICAL-toast auto-pause), the modal sets `m_didPauseSim = false` on open and `closeModal()` does NOT call `setPaused(false)`. After `closeModal()`, the CRITICAL toast system independently manages its own auto-pause state based on queued toasts; however, `closeModal()` itself never calls `setPaused(false)` based on CRITICAL-queue state — the two systems are orthogonal. In no code path does `NotificationManager` or `UIManager` automatically call `setPaused(false)` without either the `m_didPauseSim` flag being set or an explicit player action.

  Summary: "auto-resume on closeModal() when CRITICAL queue empty" is NOT a specified behavior. `closeModal()` only undoes the pause it personally initiated. The simulation only resumes through the `m_didPauseSim` unwind path or when the player explicitly unpauses via the Pause button or Space key.

  - **Simultaneous CRITICAL toast + blocking modal behavior**: When a blocking modal becomes active, **ALL CRITICAL toasts — both currently visible and any new arrivals — are hidden**. Pre-existing visible CRITICAL toasts are returned to the front of the CRITICAL queue and their on-screen elements removed. New CRITICAL toasts arriving while a modal is active are also held in the queue without being displayed. After the modal is dismissed, all queued CRITICAL toasts become visible (up to the 2-simultaneous limit) and CRITICAL-toast auto-pause is re-evaluated. This ensures the modal has complete visual focus with no competing blocking UI layers. **Rationale for hiding pre-existing toasts**: showing pre-existing CRITICAL toasts on top of the modal scrim creates two simultaneous blocking UI layers (modal + toast), forcing the player to decide which to address first. The modal always takes full precedence. After modal dismissal the player immediately sees any outstanding CRITICAL alerts.

    **Same-frame race condition — modal and CRITICAL toast activate together**: If a blocking modal and a CRITICAL toast both become active on the same frame (e.g., a forced-loan modal and a bankruptcy CRITICAL toast are enqueued in the same `update()` tick), the following rules apply in order:

    **Explicit ordering rule**: `UIManager::showModal()` MUST call `m_notifications->setModalActive(true)` BEFORE calling `m_modal->open()` (or `m_modal->setPaused()`). This ensures `NotificationManager`'s `m_modalActive` flag is set before any auto-pause logic inside `ModalDialog::open()` can run. Reversing this order — calling `m_modal->open()` first — creates a window where `m_modalActive == false` while `ModalDialog` is already executing its open-path logic, allowing a CRITICAL toast's auto-pause call to fire when it should be suppressed, and producing an erroneous double-pause or incorrect `m_didPauseSim` state.

    1. The modal's auto-pause tracking takes precedence. If the simulation was running when the frame is processed, the modal's open path sets `m_didPauseSim = true` and calls `m_sim->setPaused(true)`. Before opening the modal, `UIManager::showModal()` (or the equivalent modal-open path) calls `m_notifications->setModalActive(true)`, so `NotificationManager` has `m_modalActive == true` by the time any CRITICAL toast auto-pause logic runs. The CRITICAL toast's own auto-pause call (`NotificationManager` transitioning from empty to non-empty queue) is **suppressed** because `m_modalActive == true` — `NotificationManager` checks its local `m_modalActive` flag before calling `setPaused(true)` and skips the call when the flag is set. This avoids double-pause calls and ensures the `m_didPauseSim` flag on the modal accurately reflects which subsystem initiated the pause. No UIManager reference is held by `NotificationManager`; the modal state is pushed in one direction only (UIManager → NotificationManager).
    2. The CRITICAL toast is held in the queue without being displayed (standard modal-hiding rule). No auto-pause side-effect fires from `NotificationManager` while the modal is active.
    3. After modal dismissal (`UIManager::closeModal()`), `closeModal()` calls `m_sim->setPaused(false)` only if `m_didPauseSim == true` (i.e., the modal was the entity that initiated the pause). Then `NotificationManager` re-evaluates: if the CRITICAL queue is non-empty **and** the simulation is not already paused (i.e., `m_sim->isPaused()` returns `false`), `NotificationManager` calls `m_sim->setPaused(true)`. If the simulation is already paused (e.g., the player manually paused before the modal appeared), `NotificationManager` does **not** call `setPaused(true)` — auto-pause is a no-op when the simulation is already paused.

    **Summary of the post-dismissal re-evaluation condition**: `closeModal()` completes its unwind first (calling `m_sim->setPaused(false)` only if `m_didPauseSim == true`), then calls `m_notifications->setModalActive(false)`. Then `NotificationManager` re-evaluates **synchronously** (inline within the `setModalActive(false)` call — **not** deferred to the next `update()` tick per `testability-architecture.md` § test 8 contract): if `m_criticalQueue` is non-empty AND `m_modalActive == false` AND `!m_sim->isPaused()`, it calls `m_sim->setPaused(true)`. This ensures CRITICAL toasts that were blocked by a modal still trigger auto-pause once the modal is gone, without double-pausing an already-paused simulation. No `onModalDismissed()` callback exists — modal state is pushed via `setModalActive()` and re-evaluation is triggered synchronously by `setModalActive(false)`, not by the next `update()` tick. `NotificationManager` holds no reference to `UIManager`.
  - **CRITICAL toast keyboard navigation**: The two visible CRITICAL toasts are Tab-navigable. The first (oldest) CRITICAL toast receives keyboard focus automatically when it becomes visible. Tab moves focus to the second CRITICAL toast if visible. Enter or Delete dismisses the currently focused CRITICAL toast. The focus ring uses the same 2px accent-color border as modal buttons. CRITICAL toast keyboard focus is established AFTER any blocking modal is dismissed (modals hold keyboard focus exclusively while active — see Input Arbitration Priority 1).
  - **Normal queue**: FIFO, max depth 10; auto-dismiss after 5 seconds (default; individual toasts may specify a shorter custom timeout — e.g., the QueryPanel Escape feedback toast uses 1.5 s per `query-inspector-panel.md`; `postNormal` accepts an optional `float timeoutSeconds = 5.0f` parameter); dismissable by click. Max 3 visible simultaneously when no CRITICAL toasts are visible; max 2 visible simultaneously when 1 CRITICAL toast is visible; max 1 visible when 2 CRITICAL toasts are visible. **Start position**: Normal queue toasts always start at virtual y = **130 px** (immediately below the CRITICAL reserved band at y:20–116, with a 14 px visual separation gap). This position is fixed regardless of how many CRITICAL toasts are currently visible — the CRITICAL band is a fixed reserved region at y:20–116 that never overlaps y:130+, so Normal toasts do not need to shift down based on CRITICAL count. The visible-count reduction (3/2/1) above provides sufficient density adaptation. Toasts beyond depth 10 are **logged to an in-game notification log** rather than silently discarded.
  - **Toast height constraints** (required to enforce the 320 px cap):
    - CRITICAL toast height: **fixed 48 px** per toast (title line + single-line body + dismiss affordance + padding; long body text truncated with ellipsis at 80 characters to prevent wrapping). **Fixed at 48 px, not variable** — 2 × 48 px = 96 px fits exactly in the 96 px CRITICAL band (y:20–116 px). Do NOT allow CRITICAL toasts to grow beyond 48 px; a variable max of 80 px would overflow the band (2 × 80 = 160 px > 96 px).
    - Normal toast height: min 40 px, max **63 px** per toast (single-line body; long text truncated with ellipsis at 80 characters).
    - **320 px budget verification**: CRITICAL band = 2 × 48 px (fixed) = 96 px from y:20 → y:116. Normal band starts at y:130 (14 px visual gap below CRITICAL band). 3 Normal toasts × 63 px = 189 px; total from top = 130 + 189 = 319 px ≤ 320 px. ✓ `NotificationManager` must enforce the 63 px Normal toast height cap and the 48 px CRITICAL toast fixed height at element creation time.
    - Long notification messages that would overflow the toast body are stored in full in the notification log (bell icon) but displayed truncated with "…" in the toast. The `NotificationManager` must NOT allow toast body elements to exceed the max height; if text wraps beyond the allowed height, truncate and append "…".
  - Combined maximum height of both bands must not exceed 320 px (virtual) to prevent the stack from reaching center screen.
  - **Z-order vs. resource bar**: CRITICAL toasts render above the resource bar (higher Z-order). The resource bar occupies y:0–56; CRITICAL toasts begin at y:20 and therefore overlap the resource bar visually. The toast layer MUST be assigned a higher Z-order than the resource bar layer so toasts are not occluded.
  - **Layout constraints — grace period and Normal toast gap**: The grace period indicator occupies y:60–92. Normal toasts begin at y:130. The 38 px gap (y:92–130) ensures these two elements never overlap. This constraint must be preserved if either element's position changes.
- **Notification log**: Accessible via a bell/log icon in the HUD. See [Notification Log Panel](#notification-log-panel) below for full specification.

## Visual Design — Glass City

### Toast Backgrounds

| Toast type | Background |
|---|---|
| CRITICAL toast | `rgba(13, 27, 42, 0.88)` with a 2 px left accent stripe `#F04E37` red |
| Normal toast | `rgba(13, 27, 42, 0.82)` |

The left accent stripe on CRITICAL toasts is a 2 px vertical bar on the left edge of the
toast element, authored as a separate narrow element (or part of the toast background
element) using the `#F04E37` error red. This provides an at-a-glance severity signal
independent of the title text.

Corner radius: **8 px** on all edges of each toast.

### Toast Text

| Content | Colour |
|---|---|
| CRITICAL toast title | `#EBF4F6` near-white |
| CRITICAL toast body | `#EBF4F6` near-white |
| Normal toast title | `#EBF4F6` near-white |
| Normal toast body | `#EBF4F6` near-white |
| Dismiss affordance label (click / Enter / Delete) | `#4A7FA5` mid-blue |

### Notification Log Panel Background

The log panel uses the Glass City deep-navy style:

- `setElementBackground(handle, 13, 27, 42, 217)` (alpha 217 ≈ 0.85 × 255)

### CRITICAL Toast Row Priority Badge

In the notification log panel, CRITICAL entries use the Glass City error colour:

- Priority badge: `#F04E37` red background with `#EBF4F6` near-white text
- Normal entries: no badge; text in `#EBF4F6`

### Notification Bell Icon

The notification bell icon follows the Glass City icon state spec:

- **Inactive (no unread)**: outlined 2 px stroke bell at 65% opacity
- **Active / unread badge present**: filled solid bell at 100% opacity, 2 px teal
  `rgba(0, 201, 200, 0.75)` border + baked glow
- **Unread count badge**: `#F04E37` red circular badge with `#EBF4F6` white numeral

## Notification Log Panel

The notification log panel is a scrollable history overlay toggled by the bell icon or the **B** key.

- **Panel dimensions**: 400×500 px (virtual/scaled); content area width is 388 px when the scrollbar is visible (12 px scrollbar on the right edge)
- **Anchor**: the top-right corner of the log panel aligns to the bottom-right corner of the bell icon. Bell icon virtual bounds: x:1820–1868, y:8–56. Log panel virtual bounds: x:1468–1868, y:56–556 (400×500 px). Do NOT use a bottom-left-to-bottom-left anchor — that formula yields a top-of-panel at y = 56 − 500 = −444, rendering entirely above the viewport.
- **Z-order**: rendered above all HUD elements; when a blocking modal is active, the log panel is covered by the modal scrim (the log does NOT render above the scrim)
- **Toggle**: **B** key or bell icon click; opening the panel resets the unread-count badge to 0
- **Content**: Shows the last 50 notifications (CRITICAL and Normal combined), most-recent entry at the top. Each row displays:
  - Timestamp: elapsed time since the event (e.g., "2m 34s ago")
  - Priority badge: CRITICAL entries use a red badge; Normal entries use the default (no special color)
  - Message text: full message (not truncated, unlike toast display)
- **Scroll**: Mouse wheel scrolls the list. A 12 px vertical scrollbar is rendered on the right inner edge of the log panel (right of content, left of panel boundary). Virtual bounds of scrollbar track: x:1856–1868 px, y:56–556 px (500 px height). Content area width is reduced from 400 px to **388 px** to accommodate the scrollbar. **Thumb dimensions**: `thumbH = max(20 px, floor(visibleRows / totalRows × trackH))`; `thumbY = trackTop + floor(scrollOffset / max(1, totalRows − visibleRows) × (trackH − thumbH))`. **Track colour**: `rgba(255, 255, 255, 0.08)` (same as inactive button fill). **Thumb colour**: `rgba(255, 255, 255, 0.25)` at rest; `rgba(255, 255, 255, 0.40)` on thumb hover (detected via `IGUIElement::isPointInside()`). The scrollbar is hidden (`setElementVisible(false)`) when `totalRows ≤ visibleRows` (all entries fit without scrolling). Most-recent notification is at the top of the list.
- **Session persistence**: the log persists for the duration of the play session; it is NOT cleared on save or load within the same session
- **Dismiss on outside click**: clicking anywhere outside the panel bounds closes the log panel. Outside clicks do not consume scroll-wheel or middle-mouse-button events — those pass through to the camera/3D view
- **Panel background**: The log panel has a dark semi-opaque background applied via
  `setElementBackground(handle, 13, 27, 42, 217)` immediately after the panel element is created
  by `toggleLog()`. This produces the Glass City deep-navy fill (r=13, g=27, b=42) at approximately
  85% opacity (a=217), ensuring log entries are legible against any terrain or city view behind the panel.

  **`setElementBackground` parameter order**: the signature is `(handle, r, g, b, a)` — alpha is
  the **LAST** parameter. Passing `(handle, 217, 13, 27, 42)` would set r=217 (bright tint) and
  a=42 (16% opacity — near-transparent), making the background nearly invisible. The correct call is
  `(handle, 13, 27, 42, 217)`.
- Implementation: `NotificationManager` class creates/manages `IGUIStaticText` or custom `IGUIElement` overlays

## NotificationManager API

The following methods are required on the `NotificationManager` class in addition to toast-enqueueing and
dismiss methods documented elsewhere in this spec:

- `bool hasCriticalToastVisible() const` — returns `true` if any CRITICAL-severity toast is currently
  visible in the toast stack (i.e., at least one CRITICAL toast has been displayed and not yet dismissed by
  the player). Used by `UIManager::onEvent()` Priority 2 guard ("no CRITICAL toast is visible" condition)
  to decide whether click/Enter/Delete events must be routed to `NotificationManager` before any other
  handler. Phase 3 stub: returns `false`.
- `void dismissCriticalToast(UIElementHandle handle)` — production API for player-dismissal of a specific
  CRITICAL toast (not a test backdoor). Called when the player clicks the dismiss button, presses Enter, or
  presses Delete on the focused CRITICAL toast.
- `void setModalActive(bool active)` — called by `UIManager` to push modal state into `NotificationManager`.
  `UIManager` calls `setModalActive(true)` when opening any blocking modal (in the modal-open path, before
  any auto-pause logic runs in `NotificationManager`). `UIManager` calls `setModalActive(false)` from
  `closeModal()` after the modal's own unwind completes. `NotificationManager` stores this state in a
  private member `bool m_modalActive{false}` and checks it before calling `m_sim->setPaused(true)` — if
  `m_modalActive == true`, the auto-pause call is suppressed. Re-evaluation of the auto-pause gate occurs
  synchronously inline within the `setModalActive(false)` call — not deferred to the next `update()` tick
  (per `testability-architecture.md` § test 8 contract and the same-frame race condition section above).
  `NotificationManager` holds **no
  reference to `UIManager`** — the dependency is one-directional (UIManager pushes into
  NotificationManager). Phase 3 stub: no-op setter body; `m_modalActive` stored but only checked in
  Phase 8 auto-pause implementation.

## NotificationManager Constructor

**Full constructor signature (Phase 10)**:

```cpp
NotificationManager(IUIBackend* backend, ICitySimulation* sim, IClock* clock, IAudioSystem* audio)
```

All four parameters are stored as non-owning pointers. `m_audio` is the `IAudioSystem*` added in
Phase 10. Before Phase 10, `nullptr` is passed; every audio call site is guarded by `if (m_audio)`.

`IAudioSystem` is forward-declared in `NotificationManager.h` — the full header is included only in
`NotificationManager.cpp`. This keeps the `src/ui/` translation units free of audio headers.

**Phase 10 audio call sites in NotificationManager**:

- `postCritical()`: calls `m_audio->playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)` immediately
  after the toast element is made visible (`m_backend->setElementVisible(handle, true)`). Fires
  once per toast display — not once per enqueue. If the toast is queued but not yet visible (because
  the max simultaneous limit is reached), the sound does NOT fire until the toast actually appears.
- `postNormal()`: same call site pattern as `postCritical()`.
- Both calls are non-positional (`AL_SOURCE_RELATIVE = AL_TRUE`) with EFX bypass
  (`AL_DIRECT_FILTER = AL_FILTER_NULL`), consistent with all UI SFX.

`UI_TOAST` = `SoundId 23` (`ui_toast.wav`).

## Testing Note: Mock Selection for NotificationManager Unit Tests

The `NotificationManager` constructor takes `ICitySimulation*` (not `ISimulationPauser*`). The sole reason
for this is the `m_sim->setPaused(true)` call that fires when the first CRITICAL toast arrives. `NotificationManager`
does NOT call `getConsecutiveDeficitMonths()` internally — that method is polled exclusively by
`UIManager::update()`, which is the authoritative polling bridge for deficit-month-based toast dispatch
(GD-H3). The `ICitySimulation*` parameter exists only for the auto-pause call. If a future refactor splits
concerns, `NotificationManager` could accept `ISimulationPauser*` instead — but since `UIManager` already
holds `ICitySimulation*` and no cast is required, `ICitySimulation*` is used.

Therefore, Phase 8 auto-pause unit tests for `NotificationManager` must use
`NiceMock<MockCitySimulation>` (which implements both `ICitySimulation` and `ISimulationPauser` via
inheritance), NOT `MockSimulationPauser` (which only implements `ISimulationPauser` and cannot be passed as
`ICitySimulation*`).

The `MockSimulationPauser` class is available for tests of other components that take `ISimulationPauser*`
directly (e.g. components that only need pause/resume control and have no dependency on simulation-state
query methods).

**Phase 10 test update**: All existing `NotificationManager` test fixtures that construct the class
directly must pass a fourth `IAudioSystem*` argument. Tests that do not exercise toast audio may pass
`nullptr`. Tests that verify `ui_toast` SFX behaviour must inject `NiceMock<MockAudioSystem>`
(from `tests/simulation/mock_audio_system.h`) as the fourth argument.

Cross-references:

- `architecture/testing/testability-architecture.md` — `ICitySimulation` definition (extends
  `ISimulationPauser`), `MockCitySimulation`, `MockSimulationPauser`, and `MockAudioSystem` source
  locations.
- `src/interfaces/ISimulationPauser.h` — minimal `setPaused(bool)` interface.
- `src/interfaces/ICitySimulation.h` — full simulation interface including `getConsecutiveDeficitMonths()`.
- `architecture/ui-ux/hud-layout.md` — Phase 10 Audio Wiring section (`ui_toast` call site spec).
