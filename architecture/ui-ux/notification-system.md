# Notification System

- **Persistent indicators**: Budget in red when negative; debt indicator; power/water coverage warnings as persistent HUD icons
- **Transient toasts**: Displayed in the right-rail (top:64, right:12, 300 px wide; see Right-Rail Layout table below); CRITICAL band: up to `kNotifMaxVisible` (3) simultaneously; Normal band: up to `kNotifMaxVisible` (3) simultaneously (reduced when CRITICAL band is occupied — see priority separation below). **Auto-dismiss (5 seconds) applies only to Normal queue toasts.** CRITICAL queue toasts do NOT auto-dismiss — they require explicit player dismissal (click, Enter, or Delete).
- **Priority separation**: Toasts are separated into two queues with **dedicated vertical screen bands** to prevent overlap:
  - **CRITICAL queue** (fire, bankruptcy, power/water outage, deficit-streak warnings): displayed in the **right-rail** (top:64, right:12) as **80 px cards** (title + body + dismiss affordance + padding fit within 80 px; long body text truncated with ellipsis at 80 characters; see Right-Rail Layout table below), up to `kNotifMaxVisible` (3) visible simultaneously (rest remain queued); never silently dropped; each CRITICAL toast must be explicitly dismissed by the player (click, Enter, or Delete — Delete added as alternative to avoid interrupting mouse work). CRITICALs fill slot 0 first, up to all `kNotifMaxVisible` slots if no Normal entries are pending; further CRITICAL toasts queue behind them.
  - **CRITICAL toast auto-pause**: The simulation is automatically paused (equivalent to pressing Space/Pause) when the CRITICAL notification queue transitions from empty to non-empty. **Auto-resume requires explicit player action**: the simulation does NOT automatically resume when the CRITICAL queue is cleared (all CRITICAL toasts dismissed). The player must explicitly unpause (press Space or click the Pause button) after dismissing CRITICAL toasts. This prevents unexpected simulation restarts after the player pauses manually and then dismisses CRITICAL toasts. The `NotificationManager` calls `setPaused(true)` when the first CRITICAL toast arrives; it does NOT call `setPaused(false)` when the last CRITICAL toast is dismissed — the player is responsible for unpausing. During auto-pause triggered by a CRITICAL toast, the speed selector remains accessible (player can change speed setting for when the simulation resumes). A "Paused — [event name]" indicator appears in the Time Controls area to distinguish CRITICAL-toast-pause from player-initiated pause; after CRITICAL toast dismissal the indicator changes to the standard "Paused" label. **Priority rule**: If a blocking modal is simultaneously active, the modal takes precedence — the speed selector is grayed out regardless of CRITICAL toast state. The CRITICAL-toast auto-pause rule applies only when no blocking modal is present.

  **Speed selector state matrix** (to prevent ambiguity when states combine):

  | State                                   | Simulation                 | Speed selector                                    |
  | --------------------------------------- | -------------------------- | ------------------------------------------------- |
  | No modal, no CRITICAL toast             | Running                    | Enabled                                           |
  | CRITICAL toast only (no modal)          | Auto-paused                | **Enabled** — player can pre-set speed for resume |
  | Modal only (no CRITICAL toast)          | Paused                     | **Disabled** (grayed out)                         |
  | Modal + CRITICAL toast                  | Paused                     | **Disabled** (modal wins)                         |
  | Modal dismissed, CRITICAL toast remains | Auto-paused (re-evaluated) | **Enabled** — back to CRITICAL-toast-only rule    |

  The "modal dismissed with queued CRITICAL toast" row is the non-obvious transition: after `closeModal()`, CRITICAL toasts become visible and auto-pause is re-evaluated, but the speed selector transitions back to **ENABLED** because the modal is gone. The player must manually unpause to resume simulation (auto-resume is never performed by `NotificationManager`).

  **Auto-resume disambiguation — `dismissCriticalToast()` vs. `closeModal()`**: These two operations have distinct resume semantics and must not be conflated:
  - `NotificationManager::dismissCriticalToast(UIElementHandle)` — called when the player clicks the dismiss button, presses Enter, or presses Delete on a CRITICAL toast. This does NOT auto-resume the simulation. The simulation stays paused regardless of whether the CRITICAL queue becomes empty after dismissal. The player must explicitly unpause (Space or Pause button). This is the authoritative "no auto-resume" rule for CRITICAL toasts.
  - `UIManager::closeModal()` — called when a blocking modal dialog (forced loan, game-over) is dismissed. `closeModal()` calls `m_sim->setPaused(false)` ONLY if the modal itself set `m_didPauseSim = true` when it opened (i.e., the modal was the entity that called `setPaused(true)`). If the simulation was already paused before the modal opened (e.g., by user-pressed Pause key or by a CRITICAL-toast auto-pause), the modal sets `m_didPauseSim = false` on open and `closeModal()` does NOT call `setPaused(false)`. After `closeModal()`, the CRITICAL toast system independently manages its own auto-pause state based on queued toasts; however, `closeModal()` itself never calls `setPaused(false)` based on CRITICAL-queue state — the two systems are orthogonal. In no code path does `NotificationManager` or `UIManager` automatically call `setPaused(false)` without either the `m_didPauseSim` flag being set or an explicit player action.

  Summary: "auto-resume on closeModal() when CRITICAL queue empty" is NOT a specified behavior. `closeModal()` only undoes the pause it personally initiated. The simulation only resumes through the `m_didPauseSim` unwind path or when the player explicitly unpauses via the Pause button or Space key.
  - **Simultaneous CRITICAL toast + blocking modal behavior**: When a blocking modal becomes active, **ALL CRITICAL toasts — both currently visible and any new arrivals — are hidden**. Pre-existing visible CRITICAL toasts are returned to the front of the CRITICAL queue and their on-screen elements removed. New CRITICAL toasts arriving while a modal is active are also held in the queue without being displayed. After the modal is dismissed, all queued CRITICAL toasts become visible (up to `kNotifMaxVisible` (3) simultaneously) and CRITICAL-toast auto-pause is re-evaluated. This ensures the modal has complete visual focus with no competing blocking UI layers. **Rationale for hiding pre-existing toasts**: showing pre-existing CRITICAL toasts on top of the modal scrim creates two simultaneous blocking UI layers (modal + toast), forcing the player to decide which to address first. The modal always takes full precedence. After modal dismissal the player immediately sees any outstanding CRITICAL alerts.

    **Same-frame race condition — modal and CRITICAL toast activate together**: If a blocking modal and a CRITICAL toast both become active on the same frame (e.g., a forced-loan modal and a bankruptcy CRITICAL toast are enqueued in the same `update()` tick), the following rules apply in order:

    **Explicit ordering rule**: `UIManager::showModal()` MUST call `m_notifications->setModalActive(true)` BEFORE calling `m_modal->open()` (or `m_modal->setPaused()`). This ensures `NotificationManager`'s `m_modalActive` flag is set before any auto-pause logic inside `ModalDialog::open()` can run. Reversing this order — calling `m_modal->open()` first — creates a window where `m_modalActive == false` while `ModalDialog` is already executing its open-path logic, allowing a CRITICAL toast's auto-pause call to fire when it should be suppressed, and producing an erroneous double-pause or incorrect `m_didPauseSim` state.
    1. The modal's auto-pause tracking takes precedence. If the simulation was running when the frame is processed, the modal's open path sets `m_didPauseSim = true` and calls `m_sim->setPaused(true)`. Before opening the modal, `UIManager::showModal()` (or the equivalent modal-open path) calls `m_notifications->setModalActive(true)`, so `NotificationManager` has `m_modalActive == true` by the time any CRITICAL toast auto-pause logic runs. The CRITICAL toast's own auto-pause call (`NotificationManager` transitioning from empty to non-empty queue) is **suppressed** because `m_modalActive == true` — `NotificationManager` checks its local `m_modalActive` flag before calling `setPaused(true)` and skips the call when the flag is set. This avoids double-pause calls and ensures the `m_didPauseSim` flag on the modal accurately reflects which subsystem initiated the pause. No UIManager reference is held by `NotificationManager`; the modal state is pushed in one direction only (UIManager → NotificationManager).
    2. The CRITICAL toast is held in the queue without being displayed (standard modal-hiding rule). No auto-pause side-effect fires from `NotificationManager` while the modal is active.
    3. After modal dismissal (`UIManager::closeModal()`), `closeModal()` calls `m_sim->setPaused(false)` only if `m_didPauseSim == true` (i.e., the modal was the entity that initiated the pause). Then `NotificationManager` re-evaluates: if the CRITICAL queue is non-empty **and** the simulation is not already paused (i.e., `m_sim->isPaused()` returns `false`), `NotificationManager` calls `m_sim->setPaused(true)`. If the simulation is already paused (e.g., the player manually paused before the modal appeared), `NotificationManager` does **not** call `setPaused(true)` — auto-pause is a no-op when the simulation is already paused.

    **Summary of the post-dismissal re-evaluation condition**: `closeModal()` completes its unwind first (calling `m_sim->setPaused(false)` only if `m_didPauseSim == true`), then calls `m_notifications->setModalActive(false)`. Then `NotificationManager` re-evaluates **synchronously** (inline within the `setModalActive(false)` call — **not** deferred to the next `update()` tick per `testability-architecture.md` § test 8 contract): if `m_criticalQueue` is non-empty AND `m_modalActive == false` AND `!m_sim->isPaused()`, it calls `m_sim->setPaused(true)`. This ensures CRITICAL toasts that were blocked by a modal still trigger auto-pause once the modal is gone, without double-pausing an already-paused simulation. No `onModalDismissed()` callback exists — modal state is pushed via `setModalActive()` and re-evaluation is triggered synchronously by `setModalActive(false)`, not by the next `update()` tick. `NotificationManager` holds no reference to `UIManager`.

  - **CRITICAL toast keyboard navigation**: The visible CRITICAL toasts (up to `kNotifMaxVisible`) are Tab-navigable. The first (oldest) CRITICAL toast receives keyboard focus automatically when it becomes visible. Tab cycles through subsequent visible CRITICAL toasts. Enter or Delete dismisses the currently focused CRITICAL toast. The focus ring uses the same 2px accent-color border as modal buttons. CRITICAL toast keyboard focus is established AFTER any blocking modal is dismissed (modals hold keyboard focus exclusively while active — see Input Arbitration Priority 1).
  - **Normal queue**: FIFO, max depth 10; auto-dismiss after 5 seconds (default; individual toasts may specify a shorter custom timeout — e.g., the QueryPanel Escape feedback toast uses 1.5 s per `query-inspector-panel.md`; `postNormal` accepts an optional `float timeoutSeconds = 5.0f` parameter); dismissable by click. Normal visible capacity = `kNotifMaxVisible − K` where K is the number of currently visible CRITICAL toasts: max 3 visible when 0 CRITICAL toasts are visible; max 2 visible when 1 CRITICAL toast is visible; max 1 visible when 2 CRITICAL toasts are visible; max 0 visible when 3 CRITICAL toasts are visible. Toasts beyond depth 10 are **logged to an in-game notification log** rather than silently discarded.
  - **Toast height**: All cards (CRITICAL and Normal) are **80 px** per card, matching the right-rail card height. Long body text is truncated with ellipsis at 80 characters. Max 3 cards visible simultaneously across both queues. `NotificationManager` must enforce the 80 px card height at element creation time.
    - Long notification messages that would overflow the toast body are stored in full in the notification log (bell icon) but displayed truncated with "…" in the toast. The `NotificationManager` must NOT allow toast body elements to exceed the 80 px card height; if text wraps beyond the allowed height, truncate and append "…".
  - **Z-order vs. resource bar**: Notification cards render above the resource bar (higher Z-order). The right-rail begins at top:64, below the resource bar bottom edge (y:56). The toast layer MUST be assigned a higher Z-order than the resource bar layer so cards are not occluded.
  - **Layout note — grace period indicator**: The grace period indicator occupies y:60–92. The right-rail begins at top:64, so the first card (80 px) occupies y:64–144 on the right side of the screen. These elements may overlap in the right portion of the screen; the toast layer Z-order ensures cards render above the grace period indicator when both are present.

- **Notification log**: Accessible via a bell/log icon in the HUD. See [Notification Log Panel](#notification-log-panel) below for full specification.

## Visual Design — Glacier Glass

### Right-Rail Layout

Notification cards are displayed in a **permanent right-rail** (not top-center). Layout
constants:

| Property          | Value                                                              |
| ----------------- | ------------------------------------------------------------------ |
| Position          | `top: 64`, `right: 12` (virtual 1920×1080 space)                  |
| Rail width        | 300 px                                                             |
| Card width        | 292 px                                                             |
| Card height       | 80 px                                                              |
| Card gap          | 7 px                                                               |
| Max visible slots | 3                                                                  |

### Toast Backgrounds

| Toast type     | Background                                                                  |
| -------------- | --------------------------------------------------------------------------- |
| CRITICAL toast | `kNotifCardBg` = `SColor(71,4,12,30)` = `0x47040C1E` + 3 px left severity strip, colour `kNotifStripCritical` = `#C8281E` |
| Normal / INFO toast           | `kNotifCardBg` = `SColor(71,4,12,30)` = `0x47040C1E` + 3 px left severity strip, colour `kNotifStripInfo` = `#2878DC` (`0xCC2878DCu`, teal-cyan) |
| WARNING toast (reserved, post-V1) | `kNotifCardBg` = `SColor(71,4,12,30)` = `0x47040C1E` + 3 px left severity strip, colour `kNotifStripWarning` (amber, reserved — colour TBD / `0xCCC88214u`) |

Each card has a **chrome top rim strip**: 2 px `fillColoredRect` at the top edge of the
card, colour `#E6F2FC` (Silver Chrome rim, alpha ≈ 95%). This applies to both CRITICAL and
Normal cards.

The left severity strip provides an at-a-glance severity signal independent of the title
text. All three severity tiers receive a 3 px left severity strip drawn by
`NotificationManager::drawOverlay()` each frame:

- CRITICAL: `kNotifStripCritical` = `#C8281E` (deep red)
- WARNING: `kNotifStripWarning` (reserved; colour TBD for future warning toasts)
- Normal/INFO: `kNotifStripInfo` = `#2878DC` (`0xCC2878DCu`, teal-cyan)

Corner radius: **10 px** on all edges of each toast card.

**V1 rounded corners**: V1 implements rounded corners via 9-slice sprite rendering using the `panel_corner_10px.png` asset (Deliverable 13 of Phase 11q13). The 10 px corner radius is achieved by slicing the notification card background into nine regions and stretching only the center and edge strips, preserving the curved corners at any card size.

### Toast Text

| Content                                           | Colour                           |
| ------------------------------------------------- | -------------------------------- |
| CRITICAL toast title                              | `#E2F2FF` near-white (Glacier Glass)       |
| CRITICAL toast body                               | `rgba(155,192,228,0.82)` muted blue        |
| Normal toast title                                | `#E2F2FF` near-white (Glacier Glass)       |
| Normal toast body                                 | `rgba(155,192,228,0.82)` muted blue        |
| Dismiss affordance label (click / Enter / Delete) | `rgba(180,210,240,0.38)` dim blue          |

### Notification Log Panel Background

The log panel uses the Glacier Glass deep-navy style:

- `setElementBackground(handle, 13, 27, 42, 217)` (alpha 217 ≈ 0.85 × 255)

### CRITICAL Toast Row Priority Badge

In the notification log panel, CRITICAL entries use the Glacier Glass deficit colour:

- Priority badge: `#FF7870` red background with `#D0E8F8` near-white text
- Normal entries: no badge; text in `#D0E8F8`

### Notification Bell Icon

The notification bell icon follows the Glacier Glass icon state spec:

- **Inactive (no unread)**: outlined 2 px stroke bell at 65% opacity
- **Active / unread badge present**: filled solid bell at 100% opacity, 2 px cyan
  `rgba(0, 200, 255, 0.76)` = `kActiveButtonBorderColor` border + baked glow
- **Unread count badge**: `#FF7870` red circular badge with `#D0E8F8` near-white numeral

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
  by `toggleLog()`. This produces the Glacier Glass deep-navy fill (r=13, g=27, b=42) at approximately
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
- `void dismissCard(int slotIndex)` — player-dismissal entry point. Called by `UIManager::onEvent()` when
  the player clicks the dismiss button, presses Enter, or presses Delete on a visible card slot. Routes to
  `dismissCriticalToast` when the addressed slot holds a CRITICAL card; otherwise removes the Normal toast
  from the slot and calls `refreshVisibleSlots()`.
- `void dismissCriticalToast(UIElementHandle handle)` — internal delegate; called by `dismissCard` when
  slotIndex points to a CRITICAL card; NOT the public entry point for player-dismissal.
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

**Full constructor signature (Phase 10 / Phase 11q13)**:

> `IHUDBadgeNotifier*` (`m_badgeNotifier`) is a **Phase 11q13 addition** — it does NOT exist
> in the Phase 10 constructor. Before Phase 11q13, the constructor is 4-parameter:
> `(IUIBackend*, ICitySimulation*, IClock*, IAudioSystem*)`. Phase 11q13 adds the fifth
> parameter; existing callers may pass `nullptr` to retain pre-Phase-11q13 behavior.

```cpp
NotificationManager(IUIBackend* backend, ICitySimulation* sim, IClock* clock, IAudioSystem* audio, IHUDBadgeNotifier* badgeNotifier)
```

All five parameters are stored as non-owning pointers. `m_audio` is the `IAudioSystem*` added in
Phase 10. Before Phase 10, `nullptr` is passed; every audio call site is guarded by `if (m_audio)`.
`m_badgeNotifier` is the `IHUDBadgeNotifier*` added in Phase 10; every badge call site is guarded by
`if (m_badgeNotifier)`.

`IAudioSystem` is forward-declared in `NotificationManager.h` — the full header is included only in
`NotificationManager.cpp`. This keeps the `src/ui/` translation units free of audio headers.

**Phase 10 audio call sites in NotificationManager**:

`UI_TOAST` fires from **`refreshVisibleSlots()`**, NOT from `postCritical()` or `postNormal()`.
Neither enqueue method calls `playSound` directly. The unified fire-on-display contract is:

- Audio fires inside `refreshVisibleSlots()` at the moment a card's slot transitions from
  not-visible to visible (queued → displayed), for both CRITICAL and Normal cards.
- A `bool soundFired{false}` member on each toast struct (`CriticalToast` / `NormalToast`)
  provides edge-detection. `soundFired` is set to `true` unconditionally inside
  `refreshVisibleSlots()` before the 150 ms rate-limit gate — a card that is rate-limited
  will not retry the sound on a subsequent `refreshVisibleSlots()` call.
- The audio call is guarded by the 150 ms rate-limit: `playSound` fires only if
  `m_clock->nowSeconds() - m_lastToastSoundTime >= 0.150`; `m_lastToastSoundTime` is updated
  only on the fire path, not on the skip path.
- The explicit call signature (including priority) is:
  `m_audio->playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)` — cross-reference:
  `source-pool.md §SoundPriority Enum` ("UI sounds: HIGH") and
  `v1-audio-asset-manifest.md` for the canonical SoundId assignment.
- All UI_TOAST calls are non-positional (`AL_SOURCE_RELATIVE = AL_TRUE`) with EFX bypass
  (`AL_DIRECT_FILTER = AL_FILTER_NULL`), consistent with all UI SFX.

`UI_TOAST` = `SoundId 23` (`ui_toast.wav`). Priority: `SoundPriority::HIGH`. Volume: `1.0f`.

**CRITICAL toast audio fire-on-display (not fire-on-enqueue)**: UI_TOAST fires inside
`refreshVisibleSlots()` at the moment a CRITICAL card's slot transitions from not-visible
to visible — same as for Normal cards. Audio does NOT fire on enqueue. A `bool soundFired`
flag on `CriticalToast` prevents re-fire on subsequent `refreshVisibleSlots()` calls.
If the CRITICAL is queued but no slot is available, audio fires when the slot opens.

**NormalToast soundFired**: `NormalToast` carries the same `bool soundFired{false}` member
and applies the identical fire-on-display contract. Sound fires in `refreshVisibleSlots()`
on the first visibility transition (queued → visible), with the 150 ms rate-limit via
`m_lastToastSoundTime`. Re-enqueueing a Normal toast after it has been auto-dismissed
creates a new `NormalToast` instance with `soundFired` reset to `false`, so the sound fires
correctly on its next display.

**150 ms rate-limit on UI_TOAST audio** (prevents startle from rapid cascading failures):
When `refreshVisibleSlots()` fires `playSound(UI_TOAST, ...)` for a newly-visible card
(CRITICAL or Normal), it only fires if `m_clock->nowSeconds() - m_lastToastSoundTime >= 0.150`.
`m_lastToastSoundTime` is updated only inside the `if` branch (not on the skip path).
The `m_lastToastSoundTime{-1.0e30}` far-past sentinel ensures the first card always plays.
This is implemented via `IClock` injection (`m_clock`) for test determinism.
The `soundFired` flag is set to `true` regardless of whether the rate-limit gate suppressed the `playSound` call — a card that was rate-limited when its slot became visible will not retry the sound on a subsequent `refreshVisibleSlots()` call.

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
directly must pass the full five-argument form. Tests that do not exercise toast audio may pass
`nullptr` for `IAudioSystem*`. Tests that do not exercise badge updates may pass `nullptr` for
`IHUDBadgeNotifier*`. Tests that verify `ui_toast` SFX behaviour must inject
`NiceMock<MockAudioSystem>` (from `tests/simulation/mock_audio_system.h`) as the fourth argument.

Cross-references:

- `architecture/testing/testability-architecture.md` — `ICitySimulation` definition (extends
  `ISimulationPauser`), `MockCitySimulation`, `MockSimulationPauser`, and `MockAudioSystem` source
  locations.
- `src/interfaces/ISimulationPauser.h` — minimal `setPaused(bool)` interface.
- `src/interfaces/ICitySimulation.h` — full simulation interface including `getConsecutiveDeficitMonths()`.
- `architecture/ui-ux/hud-layout.md` — Phase 10 Audio Wiring section (`ui_toast` call site spec).
