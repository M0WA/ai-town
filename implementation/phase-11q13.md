## Phase 11q13: HUD Visual Rework — Glacier Glass + Silver Chrome

**Status: TODO**

**Prerequisite**: phase-11q12 merged.

### Goal

`hud-layout.md` has been updated on this branch to replace the old "Glass City deep-navy"
visual theme with a new **Glacier Glass + Silver Chrome** design. The reference mockup is
`hud-option-a-mercury.html` at the repo root. This phase implements all spec changes that
are now inconsistent with the existing C++ HUD code:

1. Panel background `setElementBackground(...)` values updated to the new ultra-low-opacity
   dark-navy formula for all panels — minimap panels (existing call sites) AND the five HUD
   panels (top bar, toolbar, Zone, Utilities, Grace Period Indicator) which receive new
   `UIElementHandle` background members (see Deliverable 1).
2. Chrome bottom divider strip (3 px near-white specular bar at the base of the top bar).
3. Notification cards repositioned from top-center transient-toast area to a **permanent
   right-side vertical rail** (`right:12px, top:64px`), with full glass-card anatomy
   (3 px left severity strip | icon | body | dismiss button). Queue contracts
   (auto-pause on first CRITICAL, CRITICAL/Normal priority separation, 10-deep Normal FIFO,
   modal suppression) are **preserved unchanged**.
4. Minimap overlay toggle buttons (Map / Traffic / Service) added below the minimap with
   mutual-exclusivity logic, dot indicators, legend panel wiring, hover state, and backend
   `ICitySimulation::setMinimapOverlay()` dispatch.
5. Text colour palette updated throughout `HUD.cpp` / `NotificationManager.cpp`:
   primary labels `#D0E8F8`, secondary `rgba(180,210,240,0.58)`, positive `#70E898`,
   deficit `#FF7870`, rating pill `#8ECAFF`, active border `rgba(0,200,255,0.76)` cyan
   (replaces old teal `rgba(0,201,200,0.75)`), bell icon `rgba(180,210,240,0.68)`.
6. Red flashing budget indicator ARGB updated from `0x80F04E37` → `0x80FF7870`.
7. Sprite constant additions to `hud_sprite_ids.h` for minimap overlay toggles.
8. Companion spec fix: update `architecture/ui-ux/notification-system.md` to match the new
   right-rail card model delivered here.
9. Unit tests for minimap toggle mutual exclusivity, chrome strip element creation,
   and notification right-rail positioning.

> **Scope policy**: Nothing in this phase is deferred to a later phase or to a post-V1
> backlog except **`backdrop-filter: blur(...)`** which is deferred to **phase-11q14**
> (RTT + GLSL Kawase blur — High effort; keeping current semi-transparent solid fill
> for now). `border-radius`, `letter-spacing`, and animated hover transitions ARE
> implemented in this phase via custom rendering approaches (see Deliverables 13–15).
> Any other V1 feature that cannot be completed within this phase's scope must be
> split into a separate phase rather than silently omitted. UI code
> (HUD, NotificationManager, Minimap, UIManager) **may be refactored** as needed to
> accommodate the new Glacier Glass design cleanly — refactors that reduce complexity or
> remove legacy code are in scope even if not explicitly listed as deliverables.

---

### Deliverables

#### 1. `src/ui/Minimap.cpp` — panel background updates (audited)

**Scope note**: This deliverable updates Glacier Glass `SColor` values for all panel backgrounds: the two explicit `setElementBackground()` call sites in `src/ui/Minimap.cpp` listed below, plus new `UIElementHandle` panel-background members added to `HUD.h`/`HUD.cpp` for the five HUD panel regions (top bar, left toolbar, Zone sub-panel, Utilities sub-panel, Grace Period Indicator). The INDEX.md '7 panels' reference is to the spec design table listing 7 panels — not to 7 code-level handle implementations.

Update the following existing `setElementBackground` call sites to Glacier Glass + Silver
Chrome values:

| File                 | Element handle  | Old call `setElementBackground(h, r, g, b, a)`         | New call                                             |
| -------------------- | --------------- | ------------------------------------------------------ | ---------------------------------------------------- |
| `src/ui/Minimap.cpp` | `m_mapBg`       | `setElementBackground(m_mapBg, 13, 27, 42, 217)`       | `setElementBackground(m_mapBg, 4, 12, 28, 66)`       |
| `src/ui/Minimap.cpp` | `m_legendPanel` | `setElementBackground(m_legendPanel, 13, 27, 42, 209)` | `setElementBackground(m_legendPanel, 4, 12, 28, 66)` |

- [ ] Update the two call sites in `src/ui/Minimap.cpp` as tabled above.
- [ ] **2px chrome top rim on minimap panel**: Draw a 2px horizontal chrome rim at the top
      edge of the minimap panel (`kMapY = 864`) via
      `fillColoredRect(kMMBtnMapX, kMapY, kMMBtnRight - kMMBtnMapX, 2, 230, 242, 252, 242)`
      (decomposed from `kChromeRimColor`). Called from
      `UIManager::drawOverlays()` post-drawAll.
- [ ] Notification card backgrounds are handled in Deliverable 3 (`NotificationManager.cpp`).
- [ ] Irrlicht does not natively support `backdrop-filter`; frosted-glass blur is deferred
      to Phase 11q14. These flat `setElementBackground` values
      are the canonical in-engine approximation. Do NOT add any additional rendering path.
- [ ] **Add HUD panel-background elements for the 5 HUD panels**: In `HUD.cpp` constructor,
      after querying the backend for available screen area, add `UIElementHandle` members to
      `HUD.h` (private, default `kInvalidUIElement`) and create corresponding `addStaticText`
      elements with `setElementBackground` set to `kGlacierPanelBg` (SColor decomposed:
      a=66, r=4, g=12, b=28) for each panel per the bounds in `architecture/ui-ux/hud-layout.md`:

      | Panel | Handle member | Virtual bounds (x, y, w, h) |
      |---|---|---|
      | Top bar | `m_topBarBg` | per `hud-layout.md §Top Bar` |
      | Left toolbar | `m_toolbarBg` | per `hud-layout.md §Left Toolbar` |
      | Zone sub-panel | `m_zonePanelBg` | per `hud-layout.md §Zone Sub-Panel` |
      | Utilities sub-panel | `m_utilsPanelBg` | per `hud-layout.md §Utilities Sub-Panel` |
      | Grace Period Indicator | `m_gracePeriodBg` | per `hud-layout.md §Grace Period Indicator` |

      > **Note**: Deliverable 13 replaces these `setElementBackground` handles with
      > `drawNineSlice` calls and removes `m_topBarBg` etc. Implement D1 first to
      > establish layout; D13 then upgrades to rounded-corner rendering.

      Add `constexpr uint32_t kGlacierPanelBg = 0x42040C1Cu;` to `src/ui/ui_constants.h`
      (SColor(66, 4, 12, 28) = 0x42040C1C). Each background element must be created BEFORE
      the panel's child controls so it renders behind them. Visibility of `m_zonePanelBg` and
      `m_utilsPanelBg` follows the active toolbar mode: `setElementVisible(h, visible)` toggled
      by the existing toolbar mode switch logic in `HUD.cpp`.

      ```cpp
      // kGlacierPanelBg: unified Glacier Glass panel background for subordinate panels.
      // SColor(66, 4, 12, 28) = ARGB 0x42040C1C. Alpha=66 ≈ 0.259.
      // NOTE: The HTML mockup top bar uses rgba(4,12,30,0.30) (alpha≈77, B=30) which is slightly
      // more opaque. For V1, kGlacierPanelBg is applied uniformly to all five HUD panels and the
      // minimap panel for visual consistency. The top bar's extra opacity is a design-intent note
      // for future refinement but is not a V1 deliverable.
      ```

#### 2. `src/ui/HUD.cpp` — chrome bottom divider strip

Add a new member element `m_chromeStrip` (type `UIElementHandle`, default
`kInvalidUIElement`) to `HUD`. In the `HUD` constructor, after all top-bar elements are
created:

    // Chrome bottom divider strip — 3 px near-white specular bar at base of top bar.
    // Persistent IGUIStaticText element; background is set once via setElementBackground,
    // so no per-frame draw call is needed.
    // Virtual bounds: full width x=0, y=53, w=1920, h=3 (bottom 3 px of the 56 px top bar).
    // Colour decomposed from kChromeStripColor (0xF2E6F2FCu) — #E6F2FC, alpha≈95%
    m_chromeStrip = m_backend->addStaticText("", 0, 53, 1920, 3);
    m_backend->setElementBackground(m_chromeStrip,
        (kChromeStripColor >> 16) & 0xFF,  // r = 230
        (kChromeStripColor >>  8) & 0xFF,  // g = 242
         kChromeStripColor        & 0xFF,  // b = 252
        (kChromeStripColor >> 24) & 0xFF); // a = 242

- [ ] The chrome strip element is created as a persistent `IGUIStaticText`. Its background
      is set once at construction via `setElementBackground(handle, r, g, b, a)` — no
      per-frame draw call is needed. If `setElementBackground` is unavailable for any
      reason, use `m_backend->fillColoredRect(0, 53, 1920, 3, 230, 242, 252, 242)` in
      `UIManager::drawOverlays()` (a **post-drawAll** step, step 10b) — NOT in `HUD::draw()`
      (step 9, which runs before drawAll and would be overdrawn by GUI elements).
- [ ] The chrome strip has no text and no border. Do not call `setElementText` on it.
- [ ] Add `UIElementHandle m_chromeStrip{kInvalidUIElement};` to `HUD.h` private members.

#### 3. `src/ui/NotificationManager.cpp` — right-side permanent rail

The old spec placed notification toasts top-center (transient). The new spec places glass
cards as a **permanent right-side rail**: `right:12px, top:64px`, 292 px wide, stacking
downward, max 3 visible. **Queue contracts (auto-pause on first CRITICAL, CRITICAL/Normal
priority separation, modal suppression, 10-deep Normal FIFO with log-fallback) are
preserved unchanged** — only the visual layout and card anatomy change.

**Virtual coordinate mapping:**

- Rail right edge: 1920 − 12 = 1908
- Card width: 292 px → card left edge: 1908 − 292 = 1616
- Rail top: 64 (just below the 56 px top bar + 8 px gap)
- Card height: 80 px for both CRITICAL and Normal cards (unified height for alignment)
- Gap between cards: 7 px
- Per-card stack spacing: 87 px (80 card + 7 gap)
- Card N top: 64 + N × 87

**Card anatomy** (per `hud-layout.md`):

Each card is laid out as a flat glass panel (`setElementBackground(h, 4, 12, 30, 71)`)
with the following child elements:

**V1 engine limitation — card entry animation**: The HTML mockup specifies a slide-fade
entrance animation (`@keyframes notif-in`: slide from x:+24px, fade from opacity:0, duration 350ms
with spring curve). Irrlicht `IUIBackend` has no native CSS animation support. In V1, cards appear
instantaneously at their target position and full opacity. A fallback alpha ramp (via `setElementAlpha`
over 3 frames ≈ 50ms) is acceptable but not required for V1. This is an Irrlicht engine constraint,
not a deferral — no follow-up phase is required for the animation baseline.

**Flat-rectangle rendering note (border-radius):** `border-radius: 10px` is specified
in the CSS mockup spec. Irrlicht `IGUIStaticText` does **not** support native corner
radius rendering. Phase 11q13 implements rounded corners via **9-slice sprite textures**
(see Deliverable 13). A pre-authored corner-quarter PNG asset is stretched via
`draw2DImage` for each corner/edge region. All card child-element coordinates (icon,
title, message, timestamp, dismiss button) assume the 9-slice panel background and
include appropriate padding to avoid clipping at rounded corners.

- [ ] Card panel handle MUST be created via `addStaticText`:

      ```cpp
      // Colour decomposed from kNotifCardBg (0x47040C1Eu) — SColor(71, 4, 12, 30)
      slot.hPanel = m_backend->addStaticText("", kNotifCardLeft, cardTop, 292, 80);
      m_backend->setElementBackground(slot.hPanel,
          (kNotifCardBg >> 16) & 0xFF,  // r = 4
          (kNotifCardBg >>  8) & 0xFF,  // g = 12
           kNotifCardBg        & 0xFF,  // b = 30
          (kNotifCardBg >> 24) & 0xFF); // a = 71
      ```

      Do NOT use `addButton` for the card panel — `setElementBackground` is a no-op on
      non-`IGUIStaticText` elements per `architecture/ui-ux/ui-manager.md` method 18.

1. **Left severity strip** — 3 px wide, full card height. Drawn as a filled rect in
   `NotificationManager::drawOverlay()` using `m_backend->fillColoredRect(...)` with
   the severity colour decomposed from the named `kNotifStrip*` constants in
   `ui_constants.h` (no inline RGBA literals in `NotificationManager.cpp`):
   - CRITICAL: decompose `kNotifStripCritical` (0xCCC8281Eu) → `fillColoredRect(cardLeft, cardTop, 3, 80, 200, 40, 30, 204)` — red (`#C8281E`)
   - WARNING: decompose `kNotifStripWarning` (0xCCC88214u) → `fillColoredRect(cardLeft, cardTop, 3, 80, 200, 130, 20, 204)` — amber
   - INFO: decompose `kNotifStripInfo` (0xCC2878DCu) → `fillColoredRect(cardLeft, cardTop, 3, 80, 40, 120, 220, 204)` — teal-cyan

   Call-site pattern MUST use the decomposition helper (or inline shift extraction)
   against `kNotifStripCritical` / `kNotifStripWarning` / `kNotifStripInfo` — do NOT
   pass raw numeric literals as the (r,g,b,a) arguments. No `addStaticText` element is
   used for the strip — drawn directly. `drawOverlay()` is called from a **post-drawAll**
   step (UIManager step 10b) so strips render above card background IGUIElements. Do NOT
   call from step 9.

- [ ] **2px chrome top rim per card**: For each occupied slot, draw a 2px horizontal
      chrome rim at the top edge of the card via `fillColoredRect(cardLeft, cardTop, 292, 2, r, g, b, a)`.
      Use constant `kChromeRimColor` (same as Deliverable 2 `kChromeStripColor` = `0xF2E6F2FC`):
      decomposed as `(r=230, g=242, b=252, a=242)`. This rim is drawn in `drawOverlay()` immediately
      after the severity strip, in the same post-drawAll step (UIManager step 10b).
      Add `constexpr uint32_t kChromeRimColor = kChromeStripColor;` or alias the existing constant.

2. **Icon** — 20 px sprite-backed element at x:cardLeft+11, y:cardTop+10, w:20 h:20
   (top-aligned with the title text). `refreshVisibleSlots()` uses a **two-way dispatch**
   in V1 — no `NotifSeverity` enum or severity field on `NormalToast` is required:
   - CRITICAL cards → `kSpriteNotifCritical` (sprite ID 324, row 10 col 4)
   - Normal cards → `kSpriteNotifInfo` (sprite ID 326, row 10 col 6)
     `kSpriteNotifWarning` (ID 325, row 10 col 5) is reserved as a constant but **not used
     in V1** — WARNING is a post-V1 severity tier.
     Icon width: 20 px (x:cardLeft+11 → x:cardLeft+31). When the sprite sheet is absent
     (`m_spriteTextureReady == false`), the icon element renders as an empty sprite — no
     text fallback, because the severity strip (Deliverable 3 §1) already provides a
     severity signal. See
     `architecture/asset-standards/2d-texture-standards.md § Cell Assignment Table` and
     `architecture/ui-ux/notification-system.md § Card anatomy`.

   `hIcon` MUST be created via `m_backend->addButton("", cardLeft+11, cardTop+10, 20, 20)`
   (empty label `IGUIButton`) so `setElementImage(hIcon, sprite)` successfully attaches the
   sprite via `IGUIButton::setImage`. Do NOT create `hIcon` via `addStaticText` — the
   `IrrlichtUIBackend::setElementImage` implementation silently no-ops on non-button elements.

3. **Body** — three `IGUIStaticText` elements:
   - Title: x:cardLeft+38, y:cardTop+10, w:cardRight−cardLeft−82, h:18px. The width
     accounts for the dismiss button (left edge x:1884) with a 20 px safe margin
     (title right edge at x:1864, dismiss left edge at x:1884). Text color `#E2F2FF`
     via `setElementTextColor(h, 226, 242, 255)`.
   - Message: immediately below title (y:cardTop+30), same x/w, h:28px (2 lines).
     Text color `rgba(155,192,228,0.82)` via `setElementTextColorA(h, 155, 192, 228, 209)`.
   - Timestamp + severity label: y:cardTop+60, h:14px, monospace font via
     `m_backend->setElementMonoFont(handle)`. Text color via
     `setElementTextColorA(h, 130, 170, 210, 140)`.

4. **Dismiss button** — `addButton("", cardRight−24, cardTop+8, 16, 16)` (x:1884, y:72). The
   button label is intentionally **empty** — the visible "✕" glyph is rendered by a child
   `IGUIStaticText` overlay (`hDismissLabel`), NOT by `IGUIButton` text. Passing a non-empty
   label to `addButton` and then calling `setElementTextColorA(hDismiss, 180, 210, 240, 97)`
   would route through `IGUISkin::setColor(EGDC_BUTTON_TEXT, SColor(97,180,210,240))` — a
   skin-GLOBAL call that sets ALL `IGUIButton` text (speed controls, toolbar, minimap toggles)
   to 38% opacity on every `refreshVisibleSlots()` call.

   After `addButton(...)`, create the dismiss label overlay:

        hDismissLabel = m_backend->addStaticText("✕", cardRight−24, cardTop+8, 16, 16);
        m_backend->setElementTextColorA(hDismissLabel, 180, 210, 240, 97);

   `setOverrideColor(SColor(97, 180, 210, 240))` applies correctly on `IGUIStaticText`
   (alpha-bearing `setOverrideColor` is NOT available on `IGUIButton` in vcpkg Irrlicht).
   Alpha=97 produces the spec-canonical `rgba(180,210,240,0.38)` dismiss glyph tint without
   affecting any other button's text colour. `hDismissLabel` is a visual-only element — it has
   no click handler. The `hDismiss` button handle is the click target; `hDismissLabel` is the
   visible glyph. Both handles are owned by `CardSlot` (see hDismissLabel in §CardSlot member
   additions below). On click, `hDismiss` dispatches `NotificationManager::dismissCard(index)`.

- [ ] **Remove legacy `refreshCriticalVisibility()` and `refreshNormalVisibility()` methods**:
      Phase 11q13 replaces these with the new `refreshVisibleSlots()` method. Remove: - The `refreshCriticalVisibility()` declaration in `NotificationManager.h` (line ~113) - The `refreshNormalVisibility()` declaration in `NotificationManager.h` (line ~117) - Their implementations in `NotificationManager.cpp` - The `UIElementHandle handle{kInvalidUIElement}` field from `CriticalToast` struct
      (element handles are now owned by `CardSlot m_slots[]`, not by queue entries) - The `UIElementHandle handle{kInvalidUIElement}` field from `NormalToast` struct
      (same reason)
      Replace all 5 call sites of `refreshCriticalVisibility()`/`refreshNormalVisibility()`
      (in `postCritical`, `postNormal`, `dismissCriticalToast`, `setModalActive`, and `update`)
      with calls to the new `refreshVisibleSlots()`.
      Without this removal, both the old top-center placement logic AND the new right-rail
      logic will run simultaneously, doubling element creation and invalidating test assertions.
- [ ] Remove the old top-center placement logic. The `NotificationManager` no longer creates
      a stacking area at the top of the screen.
- [ ] `refreshVisibleSlots()` calls `m_backend->setElementImage(slot.hIcon, sprite)` using
      a **two-way dispatch** in V1 (no `NotifSeverity` enum; no severity field on
      `NormalToast`): CRITICAL card → `kSpriteNotifCritical` (ID 324); Normal card →
      `kSpriteNotifInfo` (ID 326). `kSpriteNotifWarning` (ID 325) is reserved but not used
      in V1. Constants are added to `hud_sprite_ids.h` in Deliverable 5 and the sprite
      cells are added to `architecture/asset-standards/2d-texture-standards.md` row 10
      cols 4–6 in the Cell Assignment Table.
- [ ] `postCritical()` and `postNormal()` push to their respective queues, then call
      `refreshVisibleSlots()` which re-populates the `CardSlot[3]` presentation array from
      the queue tops (CRITICAL cards first, then Normal) and sets each card's Y to
      `kNotifRailTop + slotIndex * (kNotifCardH + kNotifCardGap)`.
      Add constants to `src/ui/ui_constants.h`:
      `cpp
    constexpr int kNotifCardLeft   = 1616;
    constexpr int kNotifCardRight  = 1908;
    constexpr int kNotifRailTop    = 64;
    constexpr int kNotifCardGap    = 7;
    constexpr int kNotifCardH      = 80;  // unified height for CRITICAL + Normal
    constexpr int kNotifMaxVisible = 3;
    // Derived card anatomy constants — referenced by setElementRect test assertions
    // on card child elements to prevent silent drift if the anatomy changes.
    constexpr int kNotifCardIconLeft    = kNotifCardLeft + 11;             // 1627
    constexpr int kNotifCardTextLeft    = kNotifCardLeft + 38;             // 1654
    constexpr int kNotifCardTextWidth   = kNotifCardRight - kNotifCardLeft - 82; // 210
    constexpr int kNotifCardDismissLeft = kNotifCardRight - 24;            // 1884
    `

      Test assertions for `setElementRect` on card child elements (see
      `NotifRail_DismissCard_ShiftsRemainingUp` and
      `NotifRail_DismissCard_UsesSetElementRect_NotRecreate` in Deliverable 11)
      MUST reference these constants (`kNotifCardIconLeft`, `kNotifCardTextLeft`,
      `kNotifCardTextWidth`, `kNotifCardDismissLeft`) — NOT hardcoded literals
      (1627, 1654, 210, 1884) — to prevent silent drift if the card anatomy
      changes.

- [ ] **Slot ordering rule**: Within each severity group, `refreshVisibleSlots()` assigns
      the FIFO-front entry (oldest, dequeue-first) to the lowest slot index (0 for
      CRITICALs, filling upward). Newer entries fill higher slot indices. Within the
      3-slot array: CRITICALs fill slots 0…(K−1) in age order (oldest=0), Normals fill
      slots K…2 in age order. This rule MUST be applied consistently so test assertions
      about slot indices are unambiguous.
- [ ] `refreshVisibleSlots()` fills up to `kNotifMaxVisible` (3) slots from the queue tops
      (CRITICAL-first, then Normal). There are **two distinct displacement/eviction
      operations** with different semantics — these MUST NOT be conflated:

      **(a) Slot-overflow displacement (log-append on collapse)** — triggered when a
      CRITICAL arrives and all Normal slots are occupied (or otherwise: when a slot is
      needed for a higher-priority card and the Normal slots are full). In this case
      the **oldest** visible Normal card — defined as the Normal card with the
      **lowest slot index** among the currently occupied Normal slots (which, by the
      Deliverable 3 §Slot ordering rule "FIFO-front → slot 0", is always the
      earliest-posted / FIFO-front Normal) — is auto-collapsed. This matches the
      canonical `architecture/ui-ux/notification-system.md` (lines 4 and 41):
      auto-collapsed Normal cards **"are removed from the visible rail but remain in
      the notification log panel"** — they go to the log, they do NOT re-appear on
      the rail. Specifically:
      - The oldest Normal's seven handles (`hPanel`, `hIcon`, `hTitle`, `hMsg`,
        `hTime`, `hDismiss`, `hDismissLabel`) are **freed via `removeElement`** and its entry is
        **permanently removed from the visible presentation layer** (its
        `CardSlot` entry is cleared; the card is NOT re-queued at the head of
        `m_normalQueue`; handles are NOT retained for re-visibility).
      - The card's data (title, message, timestamp, severity) is appended to the
        notification log ring buffer via
        `NotificationLog::append(title, message, timestamp, false)` per
        `notification-system.md §Notification Log Panel`.
      - The bell icon unread-count badge increments by 1 via
        `if (m_badge) m_badge->incrementNotificationBadge()` (the injected
        `IHUDBadgeNotifier*` — see IHUDBadgeNotifier injection section below).
      - Unlike queue-capacity eviction (§(b) 11th-Normal), this is **NOT** logged as
        a warning — it is a normal slot-management operation. No `LOG_WARN` fires.
      - After collapse, the CRITICAL occupies slot 0; the remaining Normals shift
        to fill slots 1–3 in their existing age order (consistent with the "FIFO-front
        → slot 0" rule within the Normal group — the next-oldest Normal moves to the
        lowest available Normal slot).
      - The shifted (still-visible) Normals keep their seven handles and are
        repositioned via `setElementRect` (no `addStaticText` / `addButton` calls
        during the reshuffle — hence the `addStaticText.Times(0)` /
        `addButton.Times(0)` expectations on the reshuffle portion of the test).

      This is the collapse-and-log-append path exercised by
      `NotifRail_FifthNormal_CollapsesOldestNormal_NotCritical` and
      `NotifRail_OldestNormalCollapsed_AppendsToLog` (the latter explicitly
      verifies log-append and badge increment on collapse). The collapsed Normal
      card's handles are freed at collapse time; the card lives on in the
      notification log but never re-appears on the rail.

      **(b) Queue-capacity eviction** — triggered when `m_normalQueue.size() > 10` (i.e.
      the 11th Normal card is enqueued by `postNormal()`, exceeding the 10-deep FIFO
      capacity). In this case the **oldest** Normal in `m_normalQueue` (front of queue)
      is **permanently removed** (log-fallback eviction): the queue entry is erased from
      `m_normalQueue`. If the evicted entry had visible handles (i.e. it occupied a
      `CardSlot`), call `removeElement` on all seven handles to release them from the
      backend before erasing the queue entry. Then:
      - Append the evicted card's data to the notification log via
        `NotificationLog::append(evictedTitle, evictedMessage, evictedTimestamp, false)`
        per `notification-system.md §Overflow Policy` ("Toasts beyond depth 10 are logged to
        an in-game notification log rather than silently discarded") — unlike §(a), do NOT
        call `incrementNotificationBadge()` for a §(b) eviction.
      - Emit `LOG_WARN("NotificationManager: Normal queue full (10), oldest card evicted: %s", title)`.
      This is the eviction path exercised by `NotifRail_EvictedEleventhNormal_UIToastNeverFires`
      (which verifies the evicted entry is gone from the queue and the `ui_toast` audio never
      fires because the evicted card never became visible) and
      `NotifRail_EvictedEleventhNormal_AppendsToLog` (which verifies
      `notifications_->getLog().getEntryCount()` increases after the eviction).

      **CRITICAL-full rule (unchanged)**: If ALL visible cards are CRITICAL and a new
      CRITICAL arrives, the new CRITICAL **queues behind the visible ones** — no card is
      displaced or evicted. The new CRITICAL card becomes visible when a CRITICAL is
      dismissed. This preserves "CRITICAL cards are never collapsed while Normal cards
      remain visible" and "Further CRITICAL toasts beyond 4 queue behind visible cards"
      from `notification-system.md §Normal queue rule`.

**Cognitive Complexity constraint**: `refreshVisibleSlots()` MUST be implemented as a
coordination function only. Extract the following helpers to stay within CC≤25:

- `_assignCriticalSlots()` — populates CRITICAL card entries from `m_criticalQueue`
- `_assignNormalSlots(int firstSlot)` — populates Normal card entries from `m_normalQueue`
- `_collapseOldestNormal()` — performs slot-overflow displacement (log-append + badge)
- `_repositionCard(CardSlot& slot, int newTop)` — calls setElementRect on all 7 handles
Run `python3 tools/cognitive_complexity.py --only-violations src/ui/NotificationManager.cpp`
and resolve all violations before marking this phase done. Add the CC check as an exit criterion.

- [ ] `drawOverlay()` iterates visible cards and draws the 3 px left severity strip
      for each via `m_backend->fillColoredRect(...)`. The strip is NOT a child element.
      Called from `UIManager::drawOverlays()` **post-drawAll** (step 10b).
- [ ] `NotificationManager::drawOverlay()` is declared **`public`** (not `private` /
      `protected`) so that `UIManager::drawOverlays()` can invoke it and the
      `UIManagerDrawOrder_DrawOverlays_CalledAfterDrawAll` test can observe the resulting
      `fillColoredRect` call on the injected `MockUIBackend`.
- [ ] `dismissCard(int slotIndex)` is the unified dismiss entry point for player-initiated card dismissal (dismiss button click dispatched from `UIManager::onEvent()` via `MouseButtonDown` coordinate hit-testing). `NotificationManager` owns `std::unordered_map<UIElementHandle, int> m_dismissButtonToSlot` as a **private member** (built/rebuilt inside `refreshVisibleSlots()`) and exposes `int findSlotIndexForDismissButton(UIElementHandle h) const` returning the slot index or -1 if not found. `UIManager::onEvent()` calls `m_notifications->findSlotIndexForDismissButton(caller)` to resolve the slot, then calls `m_notifications->dismissCard(slotIndex)`. For CRITICAL cards, `dismissCard` delegates to the existing `dismissCriticalToast(m_slots[slotIndex].hPanel)` path (preserving the no-auto-resume contract). For Normal cards, `dismissCard` removes the Normal card's elements, removes the corresponding entry from `m_normalQueue`, and calls `refreshVisibleSlots()`. Add `dismissCard(int slotIndex)` and `findSlotIndexForDismissButton(UIElementHandle h) const` to `architecture/ui-ux/notification-system.md §NotificationManager API` as part of Deliverable 10 (companion spec fix).

- [ ] **`dismissCriticalToast(UIElementHandle)` dispatch path (post-Fix 4)**: The `handle` field has been removed from `CriticalToast` (per the "remove legacy ..." bullet above) — CRITICAL queue entries no longer carry a handle, since handles are owned per-card by the `CardSlot` currently holding the card. Because the slot ordering rule (Deliverable 3 §Slot ordering rule) guarantees that **slot 0 is always the FIFO-front CRITICAL** whenever any CRITICAL is visible, `dismissCriticalToast(UIElementHandle)` does NOT need a reverse `handle → queue-entry` lookup map. The production dispatch path is:

      ```text
      dismissCriticalToast(UIElementHandle) ← called only via dismissCard(0) delegation
      dismissCard(0)                        → pops m_criticalQueue.front() + handles setPaused/cleanup
      ```

      Concretely: when `dismissCard(slotIndex)` is called and the card at `m_slots[slotIndex]` is CRITICAL, the implementation calls `dismissCriticalToast(m_slots[slotIndex].hPanel)` (passing the slot's panel handle as the identity argument). Inside `dismissCriticalToast`, the implementation pops `m_criticalQueue.front()` directly — it does NOT search the queue for a matching handle, because the slot-ordering invariant ensures the front of the queue is always the card being dismissed when `slotIndex` corresponds to a CRITICAL. For non-slot-0 CRITICAL dismissals (e.g. keyboard-focused dismiss of a middle CRITICAL), the caller MUST first use `std::rotate` to move the targeted entry to `m_criticalQueue.front()`, then call `dismissCriticalToast(m_slots[slotIndex].hPanel)` — this ensures the no-auto-resume contract is always enforced through `dismissCriticalToast` regardless of which slot index is dismissed. The key invariant is: **no `handle → queue-entry` reverse map is required**, and the only `UIElementHandle` argument to `dismissCriticalToast` is the slot's panel handle (used as a sanity / identity token, not as a lookup key).

- [ ] `NotificationManager.h` private members: the
      `std::deque<CriticalToast> m_criticalQueue`,
      `std::deque<NormalToast> m_normalQueue`,
      `m_hasPausedForCritical`, `m_focusedCriticalIndex`, and `m_modalActive` members are
      **retained unchanged** — all auto-pause, priority separation, modal suppression, and
      10-deep Normal FIFO semantics are preserved.
      Add a **presentation-layer** `CardSlot m_slots[kNotifMaxVisible]` array alongside
      the existing members. `CardSlot` holds
      `UIElementHandle hPanel, hIcon, hTitle, hMsg, hTime, hDismiss, hDismissLabel` — the GUI
      element handles for one visible card. `hDismissLabel` is the child `IGUIStaticText` overlay
      that renders the "✕" glyph (alpha-tinted via `setElementTextColorA`); `hDismiss` is the
      transparent click-target `IGUIButton` (empty label). The array is populated from the queue tops in
      `refreshVisibleSlots()`, called after every `postCritical()`, `postNormal()`,
      `dismissCard()`, and `setModalActive()`. An `int m_visibleCount{0}` member tracks
      the current number of populated slots (0..kNotifMaxVisible).
      Also add `double m_lastToastSoundTime{-1.0e30}` as a new private member.
      The far-past sentinel ensures the first `postCritical()` always passes the gate
      (since any real `nowSeconds()` value minus `-1.0e30` is enormous).
      See §Existing queue invariants preserved below for the 150 ms rate-limit contract.

      **IHUDBadgeNotifier injection**: Add a fifth nullable constructor parameter
      `IHUDBadgeNotifier* badgeNotifier = nullptr` to `NotificationManager`. Store as
      `m_badge` private member (`IHUDBadgeNotifier* m_badge{nullptr}`). When slot-overflow
      displacement (path (a)) collapses a Normal card to the log, call
      `if (m_badge) m_badge->incrementNotificationBadge();`.
      Existing call sites that do not pass a `badgeNotifier` receive `nullptr` and continue
      to work unchanged. `src/interfaces/IHUDBadgeNotifier.h` is the new interface header
      (single pure-virtual method `virtual void incrementNotificationBadge() = 0`).
      `UIManager` is the thin adapter that owns the `IHUDBadgeNotifier` contract; it passes `this` to `NotificationManager` and delegates `incrementNotificationBadge()` to `m_hud` (null-guarded — `NotificationManager` is constructed before `HUD`). The production wiring is `NotificationManager(..., this)` in the `UIManager` constructor.
      Tests inject a `MockHUDBadgeNotifier` (see `tests/ui/MockHUDBadgeNotifier.h`).

      **NotificationLog testability interface**:
      `NotificationManager` owns a `NotificationLog m_log;` private member. The class
      interface is:

      ```cpp
      // src/ui/NotificationLog.h — public interface
      class NotificationLog {
      public:
          void append(const std::string& title, const std::string& msg,
                      const std::string& timestamp, bool isCritical);
          int  getEntryCount() const;  // test accessor
          // Optional: const LogEntry& getEntry(int index) const;
      };
      ```

      Tests access the log via `NotificationManager::getLog() const { return m_log; }`
      public accessor. Add `src/ui/NotificationLog.h` and `src/ui/NotificationLog.cpp`
      as new files in this phase (see Files Changed table). Add a note to
      `architecture/testing/testability-architecture.md` §NotificationManager testability
      documenting the `NotificationLog` public interface (append, getEntryCount, getLog
      accessor on NotificationManager).

      **`m_focusedCriticalIndex` semantics and keyboard dismiss update**: `m_focusedCriticalIndex` now
      indexes the CRITICAL-visible `CardSlot[]` entries (0 = the topmost visible CRITICAL card;
      -1 = no CRITICAL card is currently visible). The existing Priority 2 keyboard dismiss
      handler (Enter/Delete on a focused CRITICAL toast, per
      `architecture/ui-ux/input-arbitration.md §Priority 2` and
      `architecture/ui-ux/notification-system.md §CRITICAL toast keyboard navigation`) MUST be
      updated in this phase.

      **NEW: `NotificationManager::onInput(const InputEvent& ev)` public method**:
      Expose the keyboard-dispatch logic as a public method on `NotificationManager` so
      it can be driven directly from tests (the `NotificationRailTest` fixture has no
      `UIManager` member). Method signature:

      ```cpp
      // In NotificationManager.h (public section):
      void onInput(const InputEvent& ev);
      ```

      The method handles the Priority 2 keyboard dismiss path internally: on
      `InputEvent::Type::KeyDown` with `keyCode == SDLK_RETURN (13)` or
      `keyCode == SDLK_DELETE (127)`, if `m_focusedCriticalIndex >= 0` it calls
      `this->dismissCard(m_focusedCriticalIndex)`. Tab-cycling of
      `m_focusedCriticalIndex` across visible CRITICALs is handled inside `onInput()`
      as well. Guard all branches with `if (m_focusedCriticalIndex >= 0)` to avoid
      dispatching when no CRITICAL is visible.

      **Priority 2 dual-guard (in `UIManager::onEvent()`, NOT inside `onInput()`)**: Per
      `architecture/ui-ux/ui-manager.md §Event Dispatch Priority 2`, the guard:

      ```cpp
      bool criticalVisible = m_notifications->hasCriticalToastVisible();
      bool modalActive     = m_modal && m_modal->isActive();
      if (criticalVisible && !modalActive) {
          m_notifications->onInput(ev);
      }
      ```

      lives in `UIManager::onEvent()`. `NotificationManager::onInput()` is called ONLY when the guard passes.
      `onInput()` does NOT replicate the guard — it trusts UIManager's precondition. This preserves the
      architectural layering: UIManager is the sole dispatcher; NotificationManager is the handler.

      **`UIManager` wiring**: `UIManager::onEvent()` (the outer input router) delegates
      the keyboard dispatch to `NotificationManager` via a single line:

      ```cpp
      // Inside UIManager::onEvent(const InputEvent& ev):
      m_notificationManager->onInput(ev);   // Priority 2 keyboard dismiss path
      ```

      The old inline `dismissCriticalToast(queueEntry.handle)` call is **removed** from
      `UIManager::onEvent()` — all keyboard dismiss routing now lives inside
      `NotificationManager::onInput()`. `dismissCriticalToast(UIElementHandle)` remains
      callable via the slot handle (`m_slots[slotIndex].hPanel`) for the internal
      CRITICAL delegation path inside `dismissCard()`, preserving the no-auto-resume
      contract.

      Add `UIManager.cpp`, `UIManager.h`, `NotificationManager.cpp`, and
      `NotificationManager.h` to Files Changed if not already listed for this keyboard
      routing update (see Files Changed table: `NotificationManager.h` gets the new
      public `onInput()` declaration; `NotificationManager.cpp` gets the implementation;
      `UIManager.cpp` gets the one-line delegation call).

      **`m_focusedCriticalIndex` maintenance after `dismissCard()`**: When `dismissCard(slotIndex)`
      is called for a CRITICAL card, update `m_focusedCriticalIndex` according to these rules
      (applied after `refreshVisibleSlots()` has reshuffled the slots):
      - If `slotIndex == m_focusedCriticalIndex`: reset to 0 if at least one CRITICAL card
        remains visible after refresh, else reset to -1 (no visible CRITICAL).
      - If `slotIndex < m_focusedCriticalIndex`: decrement `m_focusedCriticalIndex` by 1 so
        the focus ring follows the shifted card (the card that was at index N is now at N−1).
      - If `slotIndex > m_focusedCriticalIndex`: no change (focused card did not shift).
      For Normal card dismissals, `m_focusedCriticalIndex` is not affected.

      **Initial focus assignment (0→1 transition)**: `refreshVisibleSlots()` is
      responsible for setting `m_focusedCriticalIndex` when the number of visible
      CRITICAL cards transitions between zero and non-zero:
      - When the count of visible CRITICAL cards transitions from **0 to ≥1** (i.e. the
        first CRITICAL card becomes visible), `refreshVisibleSlots()` sets
        `m_focusedCriticalIndex = 0` so the first (oldest, topmost) CRITICAL card
        receives keyboard focus automatically.
      - When the count of visible CRITICAL cards transitions from **≥1 back to 0** (all
        CRITICALs dismissed or otherwise removed), `refreshVisibleSlots()` resets
        `m_focusedCriticalIndex = -1`.
      This implements the
      `architecture/ui-ux/notification-system.md §CRITICAL toast keyboard navigation`
      rule: "The first (oldest) CRITICAL toast receives keyboard focus automatically
      when it becomes visible." The maintenance rules listed above handle all
      mid-list dismiss transitions; the initial-focus rule handles the two boundary
      transitions (0↔1+). Together they fully specify `m_focusedCriticalIndex`
      semantics across all CRITICAL visibility changes.

- [ ] **Card-element handle lifecycle — per-card model (required for `NotifRail_DismissCard_UsesSetElementRect_NotRecreate` and `NotifRail_FifthNormal_CollapsesOldestNormal_NotCritical` tests)**: Handles are owned **per card**, not per slot. The seven handles of a card (`hPanel`, `hIcon`, `hTitle`, `hMsg`, `hTime`, `hDismiss`, `hDismissLabel`) are: 1. **Allocated once per card** — when the card first becomes visible, `refreshVisibleSlots()` calls `addStaticText` / `addButton` to create the seven handles for that card and stores them in the `CardSlot` entry currently assigned to the card. 2. **Follow the card through slot shifts** — when a slot reshuffle occurs (e.g. a CRITICAL arrives and Normals shift down, or a card is dismissed and lower cards shift up), the same seven handles are moved to their new `CardSlot` position by calling `setElementRect` with the new slot's coordinates (`hDismissLabel` must be repositioned alongside `hDismiss`). `setElementText`, `setElementImage` may also be called to refresh content, but `addStaticText` / `addButton` are **NOT** called during reshuffling — no handle is destroyed or recreated. 3. **Freed (via `removeElement`) when the card is dismissed or evicted** — the seven handles are explicitly removed from the backend (and from `m_dismissButtonToSlot`) at dismiss / eviction time, before `refreshVisibleSlots()` repositions the surviving cards.

      Operationally, the seven handles travel with the card itself: each already-visible queue entry owns a set of seven handles that were allocated when it first became visible. `CardSlot m_slots[kNotifMaxVisible]` is the presentation-layer view of which card currently occupies each slot; during `refreshVisibleSlots()`, the seven handles of a reshuffled card are carried to its new slot (the `CardSlot` entries are re-pointed / re-populated to reference the moved card's handles) and `setElementRect` is issued to relocate the on-screen rectangles. Only a queue entry that is transitioning from not-visible to visible allocates a fresh set of seven handles. This per-card model is what the `.Times(0)` `addStaticText` and `addButton` assertions in `NotifRail_DismissCard_UsesSetElementRect_NotRecreate` and `NotifRail_FifthNormal_CollapsesOldestNormal_NotCritical` lock in: no new handle creation during any reshuffle involving already-visible cards.

      **Existing queue invariants preserved**:
      - `postCritical()` enqueues to `m_criticalQueue` but does **NOT** immediately fire
        audio. Audio fires inside `refreshVisibleSlots()` at the moment the CRITICAL card's
        slot transitions from not-visible to visible — same mechanism as Normal cards. A
        `bool soundFired{false}` field added to `CriticalToast` prevents re-fire on
        subsequent `refreshVisibleSlots()` calls. The 150 ms rate-limit
        (`m_lastToastSoundTime`) applies to both CRITICAL and Normal audio fires from
        `refreshVisibleSlots()`. This aligns with
        `architecture/ui-ux/notification-system.md §fire-on-display contract`.
      - **150 ms rate-limit on audio (prevents startle from rapid cascading
        failures)**: Add `double m_lastToastSoundTime{-1.0e30}` as a new
        `NotificationManager` private member alongside the existing queue members.
        The far-past sentinel ensures the first visible-slot transition always passes the gate.
        `refreshVisibleSlots()` calls `m_audio->playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)`
        for a newly-visible card (CRITICAL or Normal) ONLY IF
        `m_clock->nowSeconds() - m_lastToastSoundTime >= 0.150`.
        Updates `m_lastToastSoundTime = m_clock->nowSeconds()` **ONLY inside the
        branch that fires `playSound`** (i.e. inside the `if` block — on the skip path
        where the rate-limit fires, the timestamp MUST NOT be updated, otherwise each
        skipped call would advance the gate window and suppress legitimate next-fires).
      - `postNormal()` enqueues to `m_normalQueue`; `m_audio->playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)` fires from `refreshVisibleSlots()` when a Normal card's slot transitions from not-visible to visible (deferred visibility-transition rule). The 11th Normal that evicts the oldest Normal (log-fallback) does NOT trigger `ui_toast` because the evicted card never becomes visible.
      - **Edge-detection (prevents re-fire on every `refreshVisibleSlots()` call)**: Add a
        `bool soundFired{false}` field to both `CriticalToast` and `NormalToast` (the queue
        entry structs in `NotificationManager.h`). When `refreshVisibleSlots()` populates a
        slot for any card (CRITICAL or Normal) and the queue entry's `soundFired == false`,
        set `soundFired = true` **unconditionally and immediately — before and outside the
        rate-limit gate** — then attempt `playSound` through the rate-limit check. The correct
        pseudo-code structure is:
        ```
        if (!slot.soundFired) {
            slot.soundFired = true;  // set UNCONDITIONALLY before the rate-limit gate
            if (m_clock->nowSeconds() - m_lastToastSoundTime >= 0.150) {
                if (m_audio) m_audio->playSound(UI_TOAST, SoundPriority::HIGH, 1.0f);
                m_lastToastSoundTime = m_clock->nowSeconds();
            }
            // no else branch — soundFired stays true even if rate-limited
        }
        ```
        A rate-limited card will NOT retry audio on subsequent `refreshVisibleSlots()` calls
        because `soundFired` is already `true` regardless of whether `playSound` was called.
        On subsequent `refreshVisibleSlots()` calls (triggered by other posts/dismisses) the
        already-visible card's queue entry has `soundFired == true`, so the entire
        `if (!slot.soundFired)` block is skipped. Both CRITICAL and Normal cards use the same
        `soundFired` edge-detection path in `refreshVisibleSlots()`.
      - Auto-pause fires (call `m_sim->setPaused(true)`) when
        `m_criticalQueue` transitions from empty to non-empty AND `m_modalActive == false`
        AND `!m_sim->isPaused()` (do not re-pause an already-paused simulation — per
        `notification-system.md §Same-frame race condition`).
      - `setModalActive(true/false)` suppresses display of CRITICAL cards when modal is up.
        With the new shared 3-slot rail, Normal cards also share the same visual rail and
        must be hidden during modal. On `setModalActive(true)`, iterate all occupied
        `CardSlot` entries and call `setElementVisible` on all 7 handles (hPanel, hIcon,
        hTitle, hMsg, hTime, hDismiss, hDismissLabel) setting each to `false`. Also clear
        `m_dismissButtonToSlot` after hiding so stale handle lookups return -1. On
        `setModalActive(false)`, call `refreshVisibleSlots()` which re-shows the
        appropriate children for each occupied slot and rebuilds `m_dismissButtonToSlot`.
      - Normal cards in `m_normalQueue` are shown after CRITICAL slots are filled.
      - 10-deep Normal queue: oldest Normal card is evicted (with log-fallback) when the
        11th Normal is posted.

      **Normal card 5-second auto-dismiss timer (preserved in `update()`)**: `NotificationManager::update(double nowSeconds)` retains its timer-expiry loop from the previous implementation. Each visible Normal card's elapsed time (measured from the card's `postedAt` timestamp against `nowSeconds`) is checked against `timeoutSeconds` (default 5.0 s). Expired entries are removed from `m_normalQueue` (and their seven element handles freed via `removeElement`). `refreshVisibleSlots()` is called after any evictions so the vacated slots are filled from the remaining queue. CRITICAL cards are **never** auto-dismissed by the timer — `update()` only expires Normal cards. The `ManualClock` test accessor enables deterministic timer tests (see `NotifRail_NormalCard_AutoDismiss_AfterFiveSeconds` in Deliverable 11).

- [ ] **Audio priority correction**: This phase corrects the UI_TOAST `playSound`
      priority from `SoundPriority::NORMAL` (in existing `src/ui/NotificationManager.cpp`)
      to `SoundPriority::HIGH` per `source-pool.md §Source Allocation` and
      `notification-system.md`. Grep for `UI_TOAST, SoundPriority::NORMAL` in both `src/`
      and `tests/` before closing this phase — zero matches expected after implementation.
- [ ] **UI_CLICK audio priority correction**: This phase also corrects the UI_CLICK
      `playSound` priority to `SoundPriority::HIGH` per `source-pool.md §Source Allocation`
      ("UI sounds: HIGH"). Run `grep -rn 'UI_CLICK, SoundPriority::NORMAL' src/ tests/`
      before closing this phase — zero matches expected after implementation.
- [ ] **UI_MENU_OPEN audio priority correction**: This phase also corrects the UI_MENU_OPEN
      `playSound` priority to `SoundPriority::HIGH` per `source-pool.md §Source Allocation`
      ("UI sounds: HIGH"). Change all `playSound(UI_MENU_OPEN, SoundPriority::NORMAL, ...)`
      call sites in `src/` and `tests/` to `SoundPriority::HIGH`. Run
      `grep -rn 'UI_MENU_OPEN, SoundPriority::NORMAL' src/ tests/`
      before closing this phase — zero matches expected after implementation.
- [ ] **UI_MENU_CLOSE audio priority correction**: This phase also corrects the UI_MENU_CLOSE
      `playSound` priority to `SoundPriority::HIGH` per `source-pool.md §Source Allocation`
      ("UI sounds: HIGH"). Change all `playSound(UI_MENU_CLOSE, SoundPriority::NORMAL, ...)`
      call sites in `src/` and `tests/` to `SoundPriority::HIGH`. Run
      `grep -rn 'UI_MENU_CLOSE, SoundPriority::NORMAL' src/ tests/`
      before closing this phase — zero matches expected after implementation.
- [ ] **nullptr guard on `m_audio`**: `refreshVisibleSlots()` MUST guard every
      `m_audio->playSound(...)` call site with `if (m_audio)` per
      `notification-system.md`. Tests that
      do not exercise audio may inject `nullptr` for the audio system; these must not
      crash during slot transitions or on `postCritical()` / `postNormal()`.
- [ ] For alpha-bearing text colours (message body, timestamp, dismiss label overlay), use `IUIBackend::setElementTextColorA(handle, r, g, b, a)` — method 23, **added in this phase** (see Deliverable 6 implementation steps and `architecture/ui-ux/ui-manager.md` § IUIBackend Method Contract). The dismiss label is a child `IGUIStaticText` (`hDismissLabel`) — do NOT call `setElementTextColorA(hDismiss, ...)` on the button handle directly; doing so routes through the skin-global `EGDC_BUTTON_TEXT` path and overwrites ALL button text opacity. Fully-opaque text colours (title `#E2F2FF`) use the existing `setElementTextColor(handle, r, g, b)` (method 21).
- [ ] Update `NotificationManager` card background via `setElementBackground(h, r, g, b, a)`
      with (r,g,b,a) decomposed from `kNotifCardBg` (0x47040C1Eu) — i.e. the call site
      MUST reference `kNotifCardBg` (not inline literals `4, 12, 30, 71`). Glacier Glass
      notification card ARGB 0x47040C1E.
- [ ] Wire `NotificationManager::drawOverlay()` into `UIManager::drawOverlays()` —
      expand the existing `UIManager::drawMinimapOverlay()` sequence into
      `UIManager::drawOverlays()` which calls both the minimap overlay and the
      notification severity strips, each after `drawAll`.

#### 4. `src/ui/HUD.cpp` — minimap overlay toggle buttons

Add three `UIElementHandle` members to `HUD.h`:

    UIElementHandle m_btnMinimapMap{kInvalidUIElement};
    UIElementHandle m_btnMinimapTraffic{kInvalidUIElement};
    UIElementHandle m_btnMinimapService{kInvalidUIElement};

In the `HUD` constructor, immediately after the minimap element is created:

    // Minimap overlay toggle buttons (virtual coords per hud-layout.md)
    // kMMBtnTop = 1080 - 16 - 200 - 4 - 22 = 838; kMMBtnBottom = 860
    // 3 buttons × 64 px wide + 2 × 4 px gaps = 200 px total (matches minimap width)
    // Aligned above the minimap at x:1720–1920 (minimap right edge = 1920, matching minimap render area).
    // Left edges: Map=1720, Traffic=1788, Service=1856
    m_btnMinimapMap     = m_backend->addButton("MAP",     1720, 838, 64, 22);
    m_btnMinimapTraffic = m_backend->addButton("TRAFFIC", 1788, 838, 64, 22);
    m_btnMinimapService = m_backend->addButton("SERVICE", 1856, 838, 64, 22);

    // Default: Map view is active at game start
    m_backend->setElementImage(m_btnMinimapMap,     kSpriteMinimapMapActive);
    m_backend->setElementImage(m_btnMinimapTraffic, kSpriteMinimapTrafficInactive);
    m_backend->setElementImage(m_btnMinimapService, kSpriteMinimapServiceInactive);

**V1 appearance (Phase 11q13)**: Buttons are plain `IGUIButton` elements. Apply
active/inactive state via:

- **Fill colours are drawn per-frame** in `UIManager::drawOverlays()` via
  `m_backend->fillColoredRect(x, y, w, h, r, g, b, a)` (method 22 on `IUIBackend`) —
  **not** via `setElementBackground(btn, ...)`. Active fill `rgba(4,20,56,92)` i.e.
  `(r=4, g=20, b=56, a=92)`; inactive fill `rgba(4,12,28,71)` i.e. `(r=4, g=12, b=28, a=71)`.
  The `drawOverlays()` loop iterates the three button virtual rects from `ui_constants.h`
  (Map: x=1720, Traffic=1788, Service=1856; y=838, w=64, h=22) and draws the appropriate
  active/inactive fill **before** calling the minimap and notification overlay draws.
  No `setElementBackground(btn, ...)` calls are made on the toggle buttons —
  `setElementBackground` is a no-op on `IGUIButton` elements per `ui-manager.md` method 18.
  The `fillColoredRect` approach is the sole authoritative draw path for toggle button
  backgrounds in Phase 11q13 (per `architecture/ui-ux/hud-layout.md §Phase 11q13 V1
appearance subset` item (a)).
- [ ] **2px chrome top rim per toggle button**: After drawing the fill colour, draw a 2px
      horizontal chrome rim at the top edge of each toggle button via
      `fillColoredRect(btnX, 838, 64, 2, 230, 242, 252, 242)` for each of the three buttons
      (Map x=1720, Traffic x=1788, Service x=1856). Use `kChromeRimColor` / `kChromeStripColor`
      constant from `ui_constants.h`. Draw order: fill first, then rim (so rim visually overlays fill).
- `m_backend->setElementImage(btn, kSprite...)` for the active/inactive sprite-swap.

The HUD constructor MUST NOT call `setElementBackground(m_btnMinimapMap, ...)` /
`setElementBackground(m_btnMinimapTraffic, ...)` / `setElementBackground(m_btnMinimapService, ...)`.
These calls are replaced by the per-frame `fillColoredRect` path in `UIManager::drawOverlays()`
described above.

Text colour distinction IS delivered in Phase 11q13 via the `IGUISkin::setColor(EGDC_BUTTON_TEXT, SColor(255, r, g, b))` skin-global approach, called **once during HUD init** (in the `HUD` constructor, after all buttons are created — not per-button, not per-click). **Note**: `IGUIButton::setOverrideColor()` does NOT exist in the vcpkg Irrlicht port — `IGUISkin::setColor(EGDC_BUTTON_TEXT, ...)` is the sole V1 mechanism for button text colour. This is a global setting affecting **all** `IGUIButton` elements (minimap toggle buttons, speed buttons, toolbar buttons, and dismiss buttons). In the `HUD` constructor, after all buttons are created, call:

    // Set button text colour once globally during HUD init.
    // Affects all IGUIButton elements: minimap toggles, speed buttons, toolbar buttons.
    // Per-button text colour is NOT available in V1 without child IGUIStaticText overlays.
    // HUD has IUIBackend* m_backend — NOT IGUIEnvironment*; use backend dispatch which
    // routes through IrrlichtUIBackend → m_guiEnv->getSkin()->setColor(EGDC_BUTTON_TEXT, ...).
    m_backend->setElementTextColor(m_btnMinimapMap, 180, 210, 240);

Do **NOT** call `setElementTextColor` / `setElementTextColorA` on individual button handles in `onMinimapOverlayClick` — those calls route through the `IrrlichtUIBackend` skin-global path and change all button text simultaneously, not just the clicked button. Update `architecture/ui-ux/ui-manager.md §method 21/23` to document this constraint.

The minimap toggle active-state border uses a distinct constant `kMinimapToggleActiveBorder`
defined in `src/ui/ui_constants.h`:

    constexpr uint32_t kMinimapToggleActiveBorder = 0xA500C8F0u;  // SColor(165, 0, 200, 240) — rgba(0,200,240,0.65)

This differs from `kActiveButtonBorderColor` (0xC200C8FFu = rgba(0,200,255,0.76)) used by speed buttons.
The intentional distinction: speed buttons use a bright cyan for playback-mode affordance; minimap toggles
use a softer teal for overlay-mode affordance.
Add `kMinimapToggleActiveBorder` to the ui_constants.h deliverable (see Deliverable 7).

Backdrop-filter, glass highlight, border glow, and box-shadow are CSS properties not
supported by Irrlicht — these are V1 engine limitations, not deliverable items. Dot
indicators and toggle typography ARE delivered in this phase (see checklist items below).

Add `onMinimapOverlayClick(UIElementHandle clicked)` as a public method in `HUD`:

    void HUD::onMinimapOverlayClick(UIElementHandle clicked) {
        m_backend->setElementImage(m_btnMinimapMap,
            (clicked == m_btnMinimapMap)
                ? kSpriteMinimapMapActive : kSpriteMinimapMapInactive);
        m_backend->setElementImage(m_btnMinimapTraffic,
            (clicked == m_btnMinimapTraffic)
                ? kSpriteMinimapTrafficActive : kSpriteMinimapTrafficInactive);
        m_backend->setElementImage(m_btnMinimapService,
            (clicked == m_btnMinimapService)
                ? kSpriteMinimapServiceActive : kSpriteMinimapServiceInactive);
    }

Add three public getters to `HUD` so `UIManager::onEvent()` can route button clicks:

    // Returns the minimap overlay button handles for UIManager dispatch routing.
    UIElementHandle getBtnMinimapMap() const { return m_btnMinimapMap; }
    UIElementHandle getBtnMinimapTraffic() const { return m_btnMinimapTraffic; }
    UIElementHandle getBtnMinimapService() const { return m_btnMinimapService; }

Wire click dispatch in `UIManager::onEvent()` (HUD itself has no `onEvent()` method;
UIManager owns click routing — analogous to the existing speed-selector dispatch). When a
`MouseButtonDown` event fires (InputEvent::Type::MouseButtonDown), use
`m_backend->getElementRect(h)` hit-testing against the button handles to determine
which button was clicked. Check against `m_hud->getBtnMinimapMap()`, `getBtnMinimapTraffic()`,
and `getBtnMinimapService()` handle rects, then call
`m_hud->onMinimapOverlayClick(matchedHandle)`.
`onMinimapOverlayClick(UIElementHandle clicked)` is declared public so `UIManager::onEvent()`
can call it directly and test code can dispatch it without a `UIManager` fixture member
(consistent with the white-box dispatch tests in Deliverable 8).

- [ ] **UI_CLICK audio for minimap toggle buttons**: After calling
      `m_hud->onMinimapOverlayClick(matchedHandle)` in `UIManager::onEvent()`, call:
      `if (m_audio) m_audio->playSound(UI_CLICK, SoundPriority::HIGH, 1.0f);`
      consistent with `architecture/ui-ux/hud-layout.md §ui_click` which mandates
      UI_CLICK audio for all `addButton()` elements processed in UIManager's dispatch
      path.

- [ ] **`src/ui/Minimap.h` render-area shift (Phase 11q13 upward adjustment)**:
      Update `kMapY` in `src/ui/Minimap.h` from **880** to **864**; `kMapH` stays 200
      (computed bottom = `kMapY + kMapH` = 864 + 200 = **1064**). This 16 px upward shift
      accommodates the new 22 px toggle row at y:838–860 plus a 4 px gap. Also update any
      inline comment referencing the old 880/1080 values in `src/ui/Minimap.cpp` to reflect
      the new y:864–1064 range. (`kMapH` does NOT change.) Add `src/ui/Minimap.h` to the
      Files Changed table with the change description:
      "Update `kMapY` in `src/ui/Minimap.h` from 880 to 864 (kMapH stays 200, computed
      bottom = 1064); update inline comments in `src/ui/Minimap.cpp` to reflect new
      y:864–1064 range."

- [ ] **4 px gap between toggle row and minimap render area (intentional, no backing
      element)**: The 4 px gap at y:860–864 (between the toggle row bottom at y=860 and
      the minimap render area starting at y=864) shows the scene background behind the
      HUD. This is intentional — no additional backing element is added for the gap itself.
      The gap matches the vertical rhythm of the minimap panel design. Add a unified
      full-height glass panel (`IGUIStaticText`) behind the minimap widget (toggle row +
      render area): `addStaticText("", 1720, 838, 200, 242)` with
      `setElementBackground(h, 4, 12, 28, 66)` (decomposed from `kGlacierPanelBg`). This
      panel element must be created BEFORE the toggle buttons so it renders behind them.
      Store the handle in `UIElementHandle m_minimapBg{kInvalidUIElement}` in `HUD.h`.
- [ ] **Remove legacy Svc/Tfc toggle hit-tests from `src/ui/Minimap.cpp::onEvent()`**: The existing two-icon Svc/Tfc radio hit-test blocks in `Minimap::onEvent()` (the `if (mx >= 1720 && ... && my >= 848 && my <= 880)` and `if (mx >= 1684 && ... && my <= 880)` branches that flip `m_overlayActive`) overlap the new toggle button row (y:838–860) and will cause double-dispatch (both `onMinimapOverlayClick` AND the legacy `m_overlayActive` toggle fire for the same click). Remove both legacy hit-test branches from `Minimap::onEvent()`.
- [ ] **Legend panel visibility wiring**: In `onMinimapOverlayClick(UIElementHandle clicked)`,
      call `m_minimap->setOverlayActive(clicked != m_btnMinimapMap)` (or equivalent setter)
      so `Minimap::m_overlayActive` reflects the current mode. `Minimap::m_overlayActive`
      controls visibility of `m_legendPanel` — when `true`, `m_legendPanel` is visible
      (shown via `setElementVisible(m_legendPanel, true)`) and the widget footprint expands
      to y:722 (kMinimapWidgetTopOverlayActive = 722); when `false`, `m_legendPanel` is
      hidden and the footprint contracts to y:838 (kMinimapWidgetTop). Add
      `void setOverlayActive(bool active)` as a public method on `Minimap` if not already
      present. Add `src/ui/Minimap.h` and `src/ui/Minimap.cpp` legend-panel-visibility
      note to Files Changed if not present.
- [ ] **Dot indicators and toggle button typography**: The 5 px diameter filled-circle dot
      indicators (Map = `#A8C8F0` steel blue, Traffic = `#FF7050` coral, Service = `#50D890`
      teal-green) specified in `architecture/ui-ux/hud-layout.md § Minimap Overlay Toggles —
    Toggle Appearance` are drawn per-frame in `UIManager::drawOverlays()` via
      `m_backend->fillColoredRect(...)` alongside the toggle button background fills. Each dot
      is 5×5 px, vertically centred in the 22 px button height (y_center = kMMBtnTop + 11,
      dot_top = y_center − 2 = 847), positioned 6 px from the button left edge: - Map dot: `fillColoredRect(kMMBtnMapX + 6, 847, 5, 5, 168, 200, 240, 255)` - Traffic dot: `fillColoredRect(kMMBtnTrafficX + 6, 847, 5, 5, 255, 112, 80, 255)` - Service dot: `fillColoredRect(kMMBtnServiceX + 6, 847, 5, 5, 80, 216, 144, 255)`
      Add constants `kDotMap`, `kDotTraffic`, `kDotService` (packed ARGB `uint32_t`) to
      `ui_constants.h` and decompose them at the call site — no inline literals. Uppercase
      label text ("MAP", "TRAFFIC", "SERVICE") is used for toggle button labels (passed to
      `addButton()`). Letter-spacing is not supported by Irrlicht (V1 engine limitation).
- [ ] **Backend minimap render layer switching**: `onMinimapOverlayClick(UIElementHandle clicked)`
      calls `setElementImage` for the sprite-swap AND dispatches to `ICitySimulation` to switch
      the active minimap overlay. Dispatch via a new `ICitySimulation` method (or the nearest
      equivalent per the existing interface in `src/interfaces/ICitySimulation.h`):
      `cpp
    // In onMinimapOverlayClick:
    if (clicked == m_btnMinimapMap)      m_sim->setMinimapOverlay(MinimapOverlay::Map);
    else if (clicked == m_btnMinimapTraffic) m_sim->setMinimapOverlay(MinimapOverlay::Traffic);
    else if (clicked == m_btnMinimapService) m_sim->setMinimapOverlay(MinimapOverlay::Service);
    `
      Add `enum class MinimapOverlay { Map, Traffic, Service };` and
      `virtual void setMinimapOverlay(MinimapOverlay overlay) = 0;` to
      `src/interfaces/ICitySimulation.h`. Implement (as no-op stub returning immediately) in
      the concrete `CitySimulation` class if full tile-colour overlay switching is not yet
      implemented; the interface contract is what matters for V1 testability. Update
      `MockCitySimulation` with `MOCK_METHOD(void, setMinimapOverlay, (MinimapOverlay), (override));`.
- [ ] Text label fallback: uppercase labels `"MAP"`, `"TRAFFIC"`, `"SERVICE"` are passed to
      `addButton()`. Irrlicht has no `text-transform` equivalent — only the literal string
      passed to `addButton()` is rendered. Do NOT use title-case (`"Map"`, `"Traffic"`,
      `"Service"`). When `m_spriteTextureReady == false` (sprite sheet absent), Irrlicht
      renders the uppercase text as the visible fallback. Do NOT pass empty strings for
      these buttons.
- [ ] Constants: add `kMMBtnTop = 838` and `kMMBtnBottom = 860` to
      `src/ui/ui_constants.h`. Also add button-width/gap and x-position constants:
      `cpp
    constexpr int kMMBtnTop         = 838;
    constexpr int kMMBtnBottom      = 860;
    constexpr int kMMBtnHeight      = 22;    // 860 - 838
    constexpr int kMMBtnWidth       = 64;    // each button 64 px wide
    constexpr int kMMBtnGap         = 4;     // gap between buttons
    constexpr int kMMBtnMapX        = 1720;  // left edge of Map button
    constexpr int kMMBtnTrafficX    = 1788;  // left edge of Traffic button
    constexpr int kMMBtnServiceX    = 1856;  // left edge of Service button
    constexpr int kMMBtnRight       = 1920;  // minimap right edge (matches minimap render area)
    `

      **Cross-spec consistency note**: `kMMBtnTop = 838` is referenced in **five places** that must stay in sync: (1) this deliverable's code block; (2) `architecture/ui-ux/notification-system.md §Layout constraints` which uses `kMMBtnTop` to derive the y:822 card-bottom clearance limit (= 838 − 16); (3) `architecture/ui-ux/minimap.md §Toggle button position` which documents the toggle row at y:838–860; (4) `architecture/ui-ux/ui-manager.md §Toolbar Carve-Out Constants` which documents `kMinimapWidgetTop = 838` (= kMMBtnTop) and `kMinimapWidgetTopOverlayActive = 722` (carve-out expands to legend panel top when overlay is active); (5) `architecture/ui-ux/input-arbitration.md §Priority 3 Minimap carve-out exception` which uses `kMinimapWidgetTop` / `kMinimapWidgetTopOverlayActive` to gate minimap click dispatch. If `kMMBtnTop` is ever changed, all five locations must be updated atomically.

- [ ] **`UIManager::drawOverlays()` sub-ordering (step 10b)**: The `drawOverlays()`
      method MUST execute its internal draw steps in the following fixed order:

      1. **HUD active-button wash + 2px cyan border** — the per-frame
         `fillColoredRect` calls emitted by `HUD::drawOverlays()` for the active speed button
         (pause/×1/×2/×4) and the active toolbar button (Zone/Road/Utilities/Demolish/Query).
         These use `kActiveButtonWashColor` (wash) + `kActiveButtonBorderColor` (border).
      2. **Minimap toggle button background fills + active border** — `fillColoredRect` for Map, Traffic,
         and Service (active fill `rgba(4,20,56,92)` OR inactive fill `rgba(4,12,28,71)`
         for each, per Deliverable 4). All three buttons draw their fill in this step, regardless
         of which is active. After the background fill for the active button, draw a 2 px active
         border via four `fillColoredRect` calls (top, bottom, left, right edges) using
         `kMinimapToggleActiveBorder` (0xA500C8F0, decomposed: a=165, r=0, g=200, b=240). No
         border is drawn for inactive buttons. Draw order within this step: fill all three buttons
         first, then draw the active button border on top.
      3. **`Minimap::drawOverlay()`** — tile colour overlays + viewport outline.
      4. **Minimap panel chrome rim** — draw the 2 px horizontal chrome rim at the top edge of
         the minimap panel via `fillColoredRect(kMMBtnMapX, kMapY, kMMBtnRight - kMMBtnMapX, 2, 230, 242, 252, 242)`
         (decomposed from `kChromeRimColor`). This step MUST come after `Minimap::drawOverlay()`
         so the rim renders on top of the tile colour fills (which start at `y = kMapY = 864`
         and would otherwise overdraw a rim placed before step 3).
      5. **`NotificationManager::drawOverlay()`** — notification card severity strips
         (3 px left strip per visible card, colour from `kNotifStripCritical` /
         `kNotifStripWarning` / `kNotifStripInfo`).

      **V1 non-overlap note**: the HUD active-button region (x:8–72, for toolbar and
      speed buttons along the top bar / left rail) does not overlap the notification
      card region (x:1616–1908). The draw order is nonetheless **locked by spec** to
      prevent regressions if layouts shift in future phases and to keep rendering
      deterministic for the `UIManagerDrawOrderTest` ordering assertions.

      See `architecture/graphics-architecture/irrlicht-device-lifecycle.md §Per-Frame
      Loop` step 10b for the authoritative sub-ordering.

#### 5. `src/ui/hud_sprite_ids.h` — minimap overlay toggle sprite IDs

Add nine new sprite handle constants for the minimap toggle buttons (active/inactive/hover
× Map/Traffic/Service). The current maximum assigned sprite ID in `hud_sprite_ids.h` is
**ID 323** (`kSpriteUndoIcon` in row 10). The next free contiguous block of 9 starts at
ID 324 in row 10 (cols 4-12) but to preserve a clean "dedicated row" structure, allocate
row 11 (cols 0-8 → IDs 352-360). Row 11 is currently unused (rows 11-15 are reserved per
existing comment at line 109). The ID assignment matches the canonical spec layout in
`architecture/asset-standards/2d-texture-standards.md` row 11.

These are placeholder values that resolve to no-op when the sprite sheet is absent
(`m_spriteTextureReady == false`):

    // Row 10 cols 4–6 — Notification card severity icons (IDs 324–326)
    constexpr uint32_t kSpriteNotifCritical          = 324;
    constexpr uint32_t kSpriteNotifWarning           = 325;
    constexpr uint32_t kSpriteNotifInfo              = 326;

    // Row 11 — Minimap overlay toggle icons (col 0-8 → IDs 352-360; pixel artwork painted by 2D texture artist as a Phase 11q13 deliverable)
    constexpr uint32_t kSpriteMinimapMapActive       = 352;
    constexpr uint32_t kSpriteMinimapMapInactive     = 353;
    constexpr uint32_t kSpriteMinimapMapHover        = 354;
    constexpr uint32_t kSpriteMinimapTrafficActive   = 355;
    constexpr uint32_t kSpriteMinimapTrafficInactive = 356;
    constexpr uint32_t kSpriteMinimapTrafficHover    = 357;
    constexpr uint32_t kSpriteMinimapServiceActive   = 358;
    constexpr uint32_t kSpriteMinimapServiceInactive = 359;
    constexpr uint32_t kSpriteMinimapServiceHover    = 360;

- [ ] **Notification severity icons (row 10 cols 4–6, IDs 324–326)**:
      `kSpriteNotifCritical` (ID 324), `kSpriteNotifWarning` (ID 325), and
      `kSpriteNotifInfo` (ID 326) are added to `hud_sprite_ids.h` alongside the minimap
      toggle constants. In V1, `refreshVisibleSlots()` uses only IDs 324 and 326:
      CRITICAL cards → `kSpriteNotifCritical` (ID 324); Normal cards →
      `kSpriteNotifInfo` (ID 326). `kSpriteNotifWarning` (ID 325) is **reserved**
      (constant declared, sprite cell allocated) but **not dispatched in V1** — the WARNING
      severity tier is post-V1. No `NotifSeverity` enum or severity field on `NormalToast`
      is introduced by this phase. Per `architecture/ui-ux/notification-system.md` and
      `architecture/asset-standards/2d-texture-standards.md § Cell Assignment Table` row 10.

- [ ] **Row 10 notification severity icon artwork** (2D texture artist deliverable):
      - `kSpriteNotifCritical` (col 4, ID 324): 20×20 red circle (#CC2828) with white `!`
        committed to `assets/textures/ui/hud_sprites_ui.png`
      - `kSpriteNotifInfo` (col 6, ID 326): 20×20 teal circle (#28789C) with white `i`
        committed to same file
      - `kSpriteNotifWarning` (col 5, ID 325): reserved cell; leave transparent in V1
        (V1 engine constraint — WARNING severity is post-V1)
      Artwork must be committed to `assets/textures/ui/hud_sprites_ui.png`
      (NOT the DDS file — see V1 asset delivery rule below).

- [ ] Row 11 cells (`kSpriteMinimapMapActive` through `kSpriteMinimapServiceHover`) are
      reserved — 9 constants (active/inactive/hover × 3), row 11
      cols 0-8 → IDs 352-360. Pixel artwork for row 11 cells (IDs 352–360) IS a deliverable
      in this phase — painted by the 2D texture artist and committed to
      `assets/textures/ui/hud_sprites_ui.png`. The button text labels are the
      visible fallback when the sprite sheet is absent.

**V1 asset delivery rule**: Sprite artwork is committed as a PNG source file
(`assets/textures/ui/hud_sprites_ui.png`). The DDS file is generated from it and
must NOT be git-tracked (the CI `_validate-assets.yml` step hard-fails on tracked DDS files).

**Row 11 minimap overlay toggle icon design specification**:
State convention: Active = full opacity; Inactive = same glyph at 40% opacity / desaturated; Hover = active glyph + 1px white border glow.
Dot palette: Map `#A8C8F0`, Traffic `#FF7050`, Service `#50D890`.

- col 0 (`kSpriteMinimapMapActive`):      Map/compass icon in #A8C8F0, full opacity, 64×64 px
- col 1 (`kSpriteMinimapMapInactive`):    Same glyph, rgba(168,200,240,0.40) — desaturated
- col 2 (`kSpriteMinimapMapHover`):       Map icon #A8C8F0 + 1px white pixel border
- col 3 (`kSpriteMinimapTrafficActive`):  Road/traffic arrow icon in #FF7050, full opacity
- col 4 (`kSpriteMinimapTrafficInactive`): Same glyph, desaturated/40% opacity
- col 5 (`kSpriteMinimapTrafficHover`):   Traffic icon + 1px white border
- col 6 (`kSpriteMinimapServiceActive`):  Cross/service icon in #50D890, full opacity
- col 7 (`kSpriteMinimapServiceInactive`): Same glyph, desaturated/40% opacity
- col 8 (`kSpriteMinimapServiceHover`):   Service icon + 1px white border

All 9 cells must be authored and committed to `assets/textures/ui/hud_sprites_ui.png`
in this phase. **Hover state wiring IS implemented
      in this phase**: hover state is derived from `MouseMove` events (InputEvent::Type::MouseMove) by
      checking whether `event.x`/`event.y` falls within `getElementRect(btn)` bounds,
      consistent with the existing `NotificationManager` mouse-coordinate dispatch pattern.
      `UIManager::onEvent()` dispatches hover entry/exit to a new
      `HUD::onMinimapOverlayHover(UIElementHandle hovered)` method that calls
      `setElementImage(btn, kSpriteMinimapMapHover/kSpriteMinimapTrafficHover/kSpriteMinimapServiceHover)`
      for the hovered button and restores the active/inactive sprite for all others when
      hover ends. The hover sprites render as no-op until the sprite cells are painted, but
      the wiring code must be present and covered by tests.

- [ ] **Sprite ID audit before assigning**: Current maximum _assigned-constant_ ID in `hud_sprite_ids.h` is `kSpriteUndoIcon = 323` (row 10, col 3). However, rows 8 and 9 already have **painted cells** in `hud_sprites_ui.png` that await Phase 12 constant assignment: Service Coverage overlay cells (~IDs 256–261) and Traffic overlay cells (~IDs 288–293). These painted-but-unassigned cells mean the effective highest-ID painted cell exceeds 323. Row 11 IDs 352–360 are unambiguously free and remain the correct assignment for these nine constants. Phase 12 constant-block assignments for rows 8 and 9 MUST be verified to not conflict with row 11. If this audit changes before merge, adjust the nine constants accordingly.

#### 6. `src/ui/HUD.cpp` — text colour palette update

Replace all hardcoded colour literals that correspond to the old Glass City palette with
the new Glacier Glass + Silver Chrome values. Call sites use either
`IUIBackend::setElementTextColor(handle, r, g, b)` (method 21, fully opaque) or
`IUIBackend::setElementTextColorA(handle, r, g, b, a)` (method 23, alpha-bearing — added
in this phase; see Deliverable 5e and `architecture/ui-ux/ui-manager.md`):

| HUD element                                        | Old colour               | New colour (method 21 or 23 per alpha presence)                        |
| -------------------------------------------------- | ------------------------ | ---------------------------------------------------------------------- |
| Segment label text (Treasury, Debt, etc.)          | `#EBF4F6`                | `(h, 208, 232, 248, 255)` — `#D0E8F8`                                  |
| Segment sub-labels ("TREASURY", "POPULATION" caps) | `#4A7FA5`                | `setElementTextColorA(h, 180, 210, 240, 148)` — rgba(180,210,240,0.58) |
| Treasury surplus value                             | `#EBF4F6`                | `(h, 112, 232, 152, 255)` — `#70E898`                                  |
| Treasury deficit value                             | `#F04E37`                | `(h, 255, 120, 112, 255)` — `#FF7870`                                  |
| Rating pill text                                   | unchanged `#8ECAFF`      | `(h, 142, 202, 255, 255)` — see VERIFY step below                      |
| Active speed button text                           | `#6ADEFF` (teal variant) | `(h, 106, 222, 255, 255)` — keep same hex                              |
| Bell icon colour                                   | unspecified              | `setElementTextColorA(h, 180, 210, 240, 173)` — rgba(180,210,240,0.68) |
| Pending rate change text                           | `#E8960C`                | `(h, 235, 180, 60, 224)` — rgba(235,180,60,0.88)                       |

- [ ] **Add `setElementTextColorA` to `IUIBackend`** — this method does NOT yet exist. Add it
      as part of this phase:
      1. In `src/interfaces/IUIBackend.h`: add `virtual void setElementTextColorA(UIElementHandle handle, int r, int g, int b, int a) = 0;`
         as method 23 (after `setElementTextColor`). Update the IUIBackend method-count comment
         from 22 to 24 (method 24 = `drawNineSlice`, added in Deliverable 13).
      2. In `src/rendering/IrrlichtUIBackend.h`: add the override declaration.
      3. In `src/rendering/IrrlichtUIBackend.cpp`: implement — cast handle to `IGUIStaticText*`; call `setOverrideColor(SColor(a, r, g, b))` + `enableOverrideColor(true)`. For `IGUIButton` elements, the text colour is set via the global skin path: `m_guiEnv->getSkin()->setColor(EGDC_BUTTON_TEXT, SColor(a,r,g,b))` — see INVESTIGATION RESULT below for the rationale.
      4. In `tests/ui/MockUIBackend.h`: add `MOCK_METHOD(void, setElementTextColorA, (UIElementHandle, int, int, int, int), (override));`
      5. In `architecture/ui-ux/ui-manager.md`: update method count from 22→24 and add method 23 + 24 contracts.
      Add `src/interfaces/IUIBackend.h`, `src/rendering/IrrlichtUIBackend.h`, `src/rendering/IrrlichtUIBackend.cpp`,
      `tests/ui/MockUIBackend.h`, and `architecture/ui-ux/ui-manager.md` to the Files Changed table
      as non-VERIFY entries (if not already listed for substantive changes).
- [ ] **INVESTIGATION RESULT — `IrrlichtUIBackend::setElementTextColor` and
      `setElementTextColorA` for `IGUIButton` elements**: `IGUIButton::setOverrideColor()`
      does NOT exist in the vcpkg Irrlicht version (confirmed by inspection of
      `build/vcpkg_installed/x64-linux/include/irrlicht/IGUIButton.h`). Therefore:
      For `IGUIButton` elements, text colour is set via the global skin:
      `m_guiEnv->getSkin()->setColor(EGDC_BUTTON_TEXT, SColor(a, r, g, b))`.
      Note: this is a global setting affecting all buttons. For per-button text colour,
      a child `IGUIStaticText` overlay element (transparent background) is the V1 alternative.
      V1 implementation uses the skin-global path for active/inactive text colour on toolbar
      and toggle buttons. Update `architecture/ui-ux/ui-manager.md §method 21/23` to
      document this constraint and the skin-global dispatch path.
      The existing `IGUIStaticText` dispatch path in `setElementTextColor` (method 21)
      is unchanged — only the button-element path is affected.
- [ ] **VERIFY: `setElementTextColor` existing call-site audit (behaviour change to existing method)**:
      Phase 11q13 updates `IrrlichtUIBackend::setElementTextColor` to use the global skin
      path for button elements (since `IGUIButton::setOverrideColor()` does not exist in
      vcpkg Irrlicht — see INVESTIGATION RESULT above). Before completing Deliverable 6:
      run `grep -rn 'setElementTextColor(' src/` **excluding `src/rendering/`
      and excluding `tests/`** to enumerate all existing call sites in production UI code
      (HUD, NotificationManager, Minimap, etc.). For each enumerated call site:
      1. Confirm the handle passed is a **static text element** (created via `addStaticText`),
         NOT a button handle (created via `addButton`).
      2. If any existing call site passes a **button handle** AND the skin-global text colour
         dispatch behaviour is NOT desired at that site, add an explicit type guard at the
         call site or use a child `IGUIStaticText` overlay instead.
      3. Document the full grep result (file:line + handle origin) in the PR description
         for reviewer sign-off. The PR MUST include this audit before Deliverable 6 is
         marked complete.
- [ ] **VERIFY: rating pill handle `m_ratingLabel` is `IGUIStaticText`**:
      The rating pill handle `m_ratingLabel` must be audited to confirm element type.
      Read `src/ui/HUD.cpp` and locate the creation site for `m_ratingLabel`. - If the handle is created via `addStaticText(...)`, it is an `IGUIStaticText`
      element and `setElementTextColor(m_ratingLabel, 142, 202, 255)` invokes the
      existing static-text path (no behavioural change). Proceed with the call. - If the handle is created via `addButton(...)`, it is an `IGUIButton` element
      and the Phase 11q13 skin-global dispatch path applies
      (see INVESTIGATION RESULT above: `setElementTextColor` for button elements uses
      `m_guiEnv->getSkin()->setColor(EGDC_BUTTON_TEXT, SColor(255, r, g, b))`). In that case, the call is
      `setElementTextColor(m_ratingLabel, 142, 202, 255)` and the implementer MUST
      document in the PR description whether the new button-dispatch behaviour is
      correct for this use case. If it is NOT correct (e.g. the pill is a button
      for click affordance but should render text like static text, and the
      override-colour behaviour breaks the rating pill rendering), REPLACE the
      element with an `IGUIStaticText` version (either migrate the rating display
      to `addStaticText(...)` or introduce a static-text overlay on top of the
      button). Do not leave the rating pill in an ambiguous state.
      Document the grep/inspection result (file:line and element type) in the PR
      description for reviewer sign-off.

- [ ] **VERIFY: `MockUIBackend.h` MOCK_METHOD count equals 24**:
      `grep -c "MOCK_METHOD" tests/ui/MockUIBackend.h` returns exactly **24**. If it
      returns **22**, both `setElementTextColorA` and `drawNineSlice` mock entries are missing.
      If it returns **23**, the `drawNineSlice` mock entry (method 24, Deliverable 13) is missing — add it in the
      same commit. `MOCK_METHOD(void, setElementTextColorA, (UIElementHandle handle, int r, int g, int b, int a), (override));`
      The count of 24 accounts for all `IUIBackend` virtual methods including
      `setElementTextColorA` (method 23) and `drawNineSlice` (method 24) added in this phase.
- [ ] Fully-opaque colour rows (alpha = 255) use `IUIBackend::setElementTextColor(handle, r, g, b)` (method 21). Alpha-bearing rows (sub-labels, bell icon) use `IUIBackend::setElementTextColorA(handle, r, g, b, a)` (method 23 — **added in this phase**; see implementation steps in this deliverable).
- [ ] Red flashing budget indicator ARGB: update the constant `kDeficitPulseArgb` (or
      equivalent inline literal) in `HUD.cpp`/`ui_constants.h` from `0x80F04E37u` to
      `0x80FF7870u`. This affects the `setElementAlpha()` call path in `HUD::update()` for
      consecutive deficit months — the element's base colour is now `#FF7870` (coral-red).

      **Scope note**: `kDeficitPulseArgb` is used **ONLY** by the standard
      consecutive-deficit pulse animation in the resource/budget bar (treasury text
      pulsing red when budget goes negative). The Month-2 bankruptcy countdown
      indicator uses a **separate code path and a separate ARGB constant** — see
      `architecture/game-design/game-over-flow.md`. These are distinct visual states
      and must remain visually distinguishable. Do NOT reuse `kDeficitPulseArgb` for
      the bankruptcy countdown path; `kBankruptcyCountdownArgb = 0xFFFF3830u` is
      introduced in Deliverable 7 (`ui_constants.h`) as a separate constant and MUST
      remain distinct from `kDeficitPulseArgb` (enforced by the
      `static_assert(kBankruptcyCountdownArgb != kDeficitPulseArgb, ...)` exit criterion).

- [ ] **Active button wash + border — per-frame draw in `HUD::drawOverlays()`**: **Active UI fill draws MUST be in `HUD::drawOverlays()`, not `HUD::draw()`**: `HUD::draw()` is
      called before `guiEnvironment->drawAll()` and any `fillColoredRect` calls there are overdrawn
      by the GUI layer. All active-button washes and border fills must be placed in `HUD::drawOverlays()`,
      which is called by `UIManager::drawOverlays()` in the post-drawAll step (UIManager step 10b).
      Add `void drawOverlays()` as a public method declaration to `HUD.h` (in the `src/ui/HUD.h` Files
      Changed row). Add `HUD::drawOverlays()` implementation to `src/ui/HUD.cpp`.

      Active speed button and active toolbar button each receive (a) a `rgba(0,200,255,0.16)` cyan wash fill and (b) a 2 px `rgba(0,200,255,0.76)` cyan border drawn per frame in `HUD::drawOverlays()`. No new `IUIBackend` method is needed — simulate the 2 px border as four `fillColoredRect` calls around the button rect `(x, y, w, h)`:
      `cpp
    // Cyan wash fill — draw first (behind border)
    // Decompose kActiveButtonWashColor (0x2900C8FFu): a=41, r=0, g=200, b=255
    m_backend->fillColoredRect(x, y, w, h, 0, 200, 255, 41);
    // 2 px border — draw after wash (on top)
    // Decompose kActiveButtonBorderColor (0xC200C8FFu): a=194, r=0, g=200, b=255
    m_backend->fillColoredRect(x,     y,     w, 2, 0, 200, 255, 194); // top
    m_backend->fillColoredRect(x,     y+h-2, w, 2, 0, 200, 255, 194); // bottom
    m_backend->fillColoredRect(x,     y,     2, h, 0, 200, 255, 194); // left
    m_backend->fillColoredRect(x+w-2, y,     2, h, 0, 200, 255, 194); // right
    `
      Use `kActiveButtonWashColor` and `kActiveButtonBorderColor` for decomposition — no inline `rgba` literals. Applies to: the active speed button (whichever of ⏸/1×/3×/10× is currently selected) and the active toolbar button (whichever of Zone/Road/Utilities/Demolish/Query is currently active). Draw calls are ordered: wash first, then border, so the border overlays the wash cleanly.

#### 7. `src/ui/ui_constants.h` — consolidate new colour constants

`ui_constants.h` must remain Irrlicht-free (it is included by targets that MUST NOT link
Irrlicht). Irrlicht's `SColor` has no `constexpr` constructor, so all palette values are
declared as **packed ARGB `constexpr uint32_t`** in the `0xAARRGGBB` format, matching the
existing `kOverlayArgb*` and `kHoverArgb*` constants already in this file. Call sites
decompose to `r=(v>>16)&0xFF, g=(v>>8)&0xFF, b=v&0xFF, a=v>>24` when calling
`setElementBackground(h,r,g,b,a)` or `setElementTextColor(h,r,g,b)` (method 21, 3-arg opaque) / `setElementTextColorA(h,r,g,b,a)` (method 23, alpha-bearing).

- [ ] Update EXISTING constants in `src/ui/ui_constants.h`: - `kMinimapWidgetTop` from the old value to **838** (matching `kMMBtnTop`, Phase 11q13 upward shift) - `kMinimapWidgetTopOverlayActive` from the old value to **722** (= top of legend panel at y:722 when overlay is active — the carve-out expands to include the legend panel when an overlay toggle is active; see legend panel wiring below) - `kMinimapWidgetLeft` from the old value to **1720** (= `kMMBtnMapX`, aligning the carve-out left edge with the toggle-button row / minimap render area) for V1. Add an inline comment: `// kMinimapWidgetLeft = 1720 (= kMMBtnMapX) in V1`
      Add compile-time lock after the constant declarations:
      `static_assert(kMinimapWidgetTop == kMMBtnTop, "kMinimapWidgetTop must equal kMMBtnTop");`
      `static_assert(kMinimapWidgetTopOverlayActive == 722, "overlay-active carve-out must include legend panel top y=722");`
      `static_assert(kMinimapWidgetLeft == kMMBtnMapX, "kMinimapWidgetLeft must equal kMMBtnMapX in V1");`
      `static_assert(kMinimapRight == kMMBtnRight, "kMinimapRight must alias kMMBtnRight (same minimap right edge x=1920)");`
- [ ] **Update `Minimap::getWidgetFootprint()` in `src/ui/Minimap.cpp`** to use named constants instead of hard-coded literals. The method currently returns `UIRect{1576, m_overlayActive ? 732 : 848, 344, m_overlayActive ? 348 : 232}`. Replace with constants from `src/ui/ui_constants.h`:
      ```cpp
      return UIRect{kMinimapWidgetLeft,
                    m_overlayActive ? kMinimapWidgetTopOverlayActive : kMinimapWidgetTop,
                    kMMBtnRight - kMinimapWidgetLeft,
                    1080 - (m_overlayActive ? kMinimapWidgetTopOverlayActive : kMinimapWidgetTop)};
      ```
      `kMinimapWidgetLeft` MUST be updated to 1720 (see preceding bullet) so `getWidgetFootprint()` and the `UIManager::onEvent()` carve-out left boundary agree. This ensures `UIManager::onEvent()` carve-out always tracks the constants rather than independent hard-coded literals.

Add the following named constants so no inline literals for the new palette appear in
`HUD.cpp` or `NotificationManager.cpp`:

    // ── Glacier Glass + Silver Chrome palette (ARGB: 0xAARRGGBB) ──────────────
    constexpr uint32_t kColorLabelPrimary       = 0xFFD0E8F8u; // #D0E8F8 cool pale blue-white
    constexpr uint32_t kColorLabelSecondary     = 0x94B4D2F0u; // rgba(180,210,240,0.58)
    constexpr uint32_t kColorValueAmber         = 0xFFF0B429u; // #F0B429 warm amber — unchanged
    constexpr uint32_t kColorValuePositive      = 0xFF70E898u; // #70E898 glacier green
    constexpr uint32_t kColorValueNegative      = 0xFFFF7870u; // #FF7870 coral-red
    constexpr uint32_t kColorRatingPill         = 0xFF8ECAFFu; // #8ECAFF sky blue
    constexpr uint32_t kColorCyanActive         = 0xFF6ADEFFu; // #6ADEFF cyan glow text
    constexpr uint32_t kColorBellInactive       = 0xADB4D2F0u; // rgba(180,210,240,0.68)
    constexpr uint32_t kColorPendingRate        = 0xE0EBB43Cu; // rgba(235,180,60,0.88)
    constexpr uint32_t kActiveButtonWashColor   = 0x2900C8FFu; // rgba(0,200,255,0.16) — cyan wash fill for active speed/toolbar buttons
    constexpr uint32_t kActiveButtonBorderColor = 0xC200C8FFu; // rgba(0,200,255,0.76) — 2 px border for active speed/toolbar buttons; drawn via 4 fillColoredRect calls
    constexpr uint32_t kDeficitPulseArgb        = 0x80FF7870u; // pulsing overlay coral-red
    constexpr uint32_t kBankruptcyCountdownArgb = 0xFFFF3830u; // fully-opaque vivid red — Month-2 bankruptcy countdown (game-over-flow.md)
    // Notification severity strip colours (ARGB) drawn via fillColoredRect
    constexpr uint32_t kNotifStripCritical      = 0xCCC8281Eu; // approx #C8281E red
    constexpr uint32_t kNotifStripWarning       = 0xCCC88214u; // approx amber
    constexpr uint32_t kNotifStripInfo          = 0xCC2878DCu; // approx teal-cyan
    // Notification card background (ARGB) — SColor(71, 4, 12, 30)
    constexpr uint32_t kNotifCardBg             = 0x47040C1Eu;
    // Chrome bottom divider strip (ARGB) — #E6F2FC, alpha≈95%
    constexpr uint32_t kChromeStripColor        = 0xF2E6F2FCu;
    // Chrome rim — alias for kChromeStripColor; used for 2px top-edge chrome rim on minimap panel and notification cards
    constexpr uint32_t kChromeRimColor          = kChromeStripColor;
    // ── Minimap widget full-footprint edge constants ─────────────────────────
    // Used by the input-arbitration carve-out (Priority 3 Minimap exception) and by
    // `Minimap::getWidgetFootprint()` so the carve-out right/bottom edges track a
    // named constant rather than a hard-coded literal.
    constexpr int kMinimapRight  = 1920;  // == kMMBtnRight (aliases the right edge)
    constexpr int kMinimapBottom = 1080;  // full-height widget footprint bottom

**Alias note**: `kMMBtnRight = kMinimapRight` — these are aliases for the same edge
value (the minimap right edge at x=1920). Future code should prefer
`kMinimapRight` for input-arbitration carve-out (right/bottom edges of the minimap
widget footprint) and `kMMBtnRight` for toggle-row button layout (the right edge
of the Service button at x=1856+64=1920). If these ever diverge, the static_assert
at the bottom of `ui_constants.h` will fail and force reconciliation.

- [ ] Palette constants are **packed ARGB uint32_t** (0xAARRGGBB format, matching existing
      `kOverlayArgb*`/`kHoverArgb*` constants in this file). Call sites decompose to
      `r=(v>>16)&0xFF, g=(v>>8)&0xFF, b=v&0xFF, a=v>>24` when calling
      `setElementBackground` / `setElementTextColor`. Do **not** include Irrlicht headers
      from this file.
- [ ] Replace all occurrences of the old inline literal colours in `HUD.cpp` and
      `NotificationManager.cpp` with these named constants.
- [ ] Do not add constants for values that do not change (e.g. `kColorValueAmber` remains
      the same hex; keep the existing constant rather than duplicating it).
- [ ] **Anti-literal gate (strict)**: grep for `setElementBackground(` and
      `fillColoredRect(` calls in `src/ui/HUD.cpp` and `src/ui/NotificationManager.cpp`
      must return **zero** occurrences that pass raw numeric RGBA literals as arguments.
      Every such call site MUST decompose one of the named palette constants
      (`kChromeStripColor`, `kNotifCardBg`, `kNotifStripCritical`, `kNotifStripWarning`,
      `kNotifStripInfo`, `kActiveButtonWashColor`, `kActiveButtonBorderColor`, etc.)
      via `(v>>16)&0xFF, (v>>8)&0xFF, v&0xFF, v>>24` or an equivalent helper. The gate
      is verified by a reviewer-run `grep -E 'setElementBackground\([^,]+, *[0-9]+, *[0-9]+, *[0-9]+, *[0-9]+\)' src/ui/HUD.cpp src/ui/NotificationManager.cpp`
      and `grep -E 'fillColoredRect\([^,]+, *[^,]+, *[^,]+, *[^,]+, *[0-9]+, *[0-9]+, *[0-9]+, *[0-9]+\)' src/ui/HUD.cpp src/ui/NotificationManager.cpp`
      — both must return zero matches. This is a mandatory exit criterion (see Exit Criteria).
- [ ] **Named-constant enforcement for `kBankruptcyCountdownArgb`**: grep `src/ui/HUD.cpp` for inline literal `0xFFFF3830` and `0xFF3830` and replace all occurrences with `kBankruptcyCountdownArgb` (ref: `architecture/game-design/game-over-flow.md`). Zero occurrences of either literal may remain in `src/ui/HUD.cpp` after this deliverable is complete.

#### 8. Tests — `tests/ui/HUDGlacierGlassTest.cpp` (new file)

- [ ] `HUDGlacierGlass_ChromeStrip_ElementCreatedAtY53`:
      Construct `HUD` with `MockUIBackend`. Verify that
      `addStaticText("", 0, 53, 1920, 3)` is called during construction (5 integer params,
      NOT `recti`) using `EXPECT_CALL` or inspecting the `MockUIBackend` element registry.
      Also verify `setElementBackground(h, 230, 242, 252, 242)` is called with the
      returned handle.

- [ ] `HUDGlacierGlass_MinimapOverlay_MapActiveByDefault`:
      Construct `HUD` with `MockUIBackend`. Verify that
      `setElementImage(m_btnMinimapMap, kSpriteMinimapMapActive)` is called during
      construction, and `setElementImage(m_btnMinimapTraffic, kSpriteMinimapTrafficInactive)`
      and `setElementImage(m_btnMinimapService, kSpriteMinimapServiceInactive)` are also
      called. Also verify the three `addButton` calls pass x=1720/1788/1856, y=838, w=64,
      h=22.

- [ ] `HUDGlacierGlass_MinimapOverlay_ClickTraffic_DeactivatesMap`:
      Construct `HUD`. Call
      `m_hud->onMinimapOverlayClick(m_hud->getBtnMinimapTraffic())` directly (white-box
      dispatch test — `UIManager` routing is covered by existing `UIManagerDrawOrderTest`).
      Verify that `setElementImage(m_btnMinimapMap, kSpriteMinimapMapInactive)` is called,
      `setElementImage(m_btnMinimapTraffic, kSpriteMinimapTrafficActive)` is called, and
      `setElementImage(m_btnMinimapService, kSpriteMinimapServiceInactive)` is called.

- [ ] `HUDGlacierGlass_MinimapOverlay_ClickService_DeactivatesMapAndTraffic`:
      Same pattern for Service button click: call
      `m_hud->onMinimapOverlayClick(m_hud->getBtnMinimapService())` directly.

- [ ] `HUDGlacierGlass_MinimapOverlay_ClickMap_AfterTraffic_ReactivatesMap`:
      Call `onMinimapOverlayClick` with the Traffic handle, then with the Map handle.
      Verify Map becomes active and Traffic reverts to inactive.

- [ ] **Test 6**: `HUDGlacierGlass_MinimapOverlay_ClickTraffic_DispatchesToSimulation`:

      ```cpp
      TEST_F(HUDGlacierGlassTest, HUDGlacierGlass_MinimapOverlay_ClickTraffic_DispatchesToSimulation) {
          EXPECT_CALL(sim_, setMinimapOverlay(MinimapOverlay::Traffic)).Times(1);
          hud_->onMinimapOverlayClick(hud_->getBtnMinimapTraffic());
      }
      ```

      Similarly add assertions for Map (`MinimapOverlay::Map`) and Service
      (`MinimapOverlay::Service`) — either as separate tests or parameterized.

- [ ] **Test 7**: `HUDGlacierGlass_MinimapOverlay_HoverTraffic_SetsHoverSprite`:

      ```cpp
      TEST_F(HUDGlacierGlassTest, HUDGlacierGlass_MinimapOverlay_HoverTraffic_SetsHoverSprite) {
          EXPECT_CALL(backend_, setElementImage(hud_->getBtnMinimapTraffic(), kSpriteMinimapTrafficHover)).Times(1);
          EXPECT_CALL(backend_, setElementImage(hud_->getBtnMinimapMap(), kSpriteMinimapMapInactive)).Times(1);
          EXPECT_CALL(backend_, setElementImage(hud_->getBtnMinimapService(), kSpriteMinimapServiceInactive)).Times(1);
          hud_->onMinimapOverlayHover(hud_->getBtnMinimapTraffic());
      }
      ```

**Test fixture contract** (required — do not omit):

- Mock policy: `NiceMock<MockUIBackend>` — `HUD`/`NotificationManager` constructors issue many `addStaticText`/`addButton` calls that `StrictMock` would reject.
- `#include "tests/simulation/MockAudioSystem.h"` is required. Verify that
  `target_include_directories(ui_tests PRIVATE tests/simulation/)` is already set in
  `CMakeLists.txt` from Phase 11m — if not, add it in the same commit.
  - [ ] Verify `target_include_directories(ui_tests PRIVATE tests/simulation/)` is
        present in `CMakeLists.txt` (grep or open the file).
- Full constructor: `HUD(backend_, audio_, sim_, clock_)` with `StrictMock<MockAudioSystem> audio_`, `NiceMock<MockCitySimulation> sim_`, `ManualClock clock_`; `NotificationManager(backend_, sim_, clock_, audio_)`.
  Mock policy exception: `audio_` uses `StrictMock<MockAudioSystem>` (not `NiceMock`) because
  audio side-effects (`playSound`) must be explicitly expected — silent swallowing hides bugs.
  Note: `NotificationManager`'s constructor parameter order `(IUIBackend*, ICitySimulation*, IClock*, IAudioSystem*)` is a **legacy deviation** from the canonical order `(IUIBackend*, IAudioSystem*, ICitySimulation*, IClock*)` documented in `architecture/testing/testability-architecture.md §Canonical UI class constructor parameter order`. This deviation is intentional and is explicitly documented in that spec section as an exception. Do not reorder without updating all call sites in `src/` and `tests/`.
- **Member declaration order** (critical for destruction order — C++ destroys in reverse
  declaration order): declare mock members in the fixture class in this order so that
  `audio_` is destroyed before `backend_` and before any `sim_` / `clock_` references
  held by the class-under-test:
  1. `NiceMock<MockUIBackend> backend_;`
  2. `NiceMock<MockCitySimulation> sim_;`
  3. `ManualClock clock_;`
  4. `StrictMock<MockAudioSystem> audio_;` — **declared AFTER `backend_`** so that in C++
     reverse destruction order (last declared → first destroyed) it is destroyed **BEFORE**
     `backend_`, satisfying the "audio mock destroyed before backend mock" invariant. Note:
     "declared after = destroyed before" is the correct C++ rule.
  5. `StrictMock<MockHUDBadgeNotifier> badge_;` — fifth member; injected as the fifth
     nullable constructor parameter of `NotificationManager`. Include
     `#include "tests/ui/MockHUDBadgeNotifier.h"` in the fixture.
     **StrictMock policy**: `badge_` is a single-method interface with no construction-time
     calls. Using `StrictMock` catches unexpected badge increments that `NiceMock` would
     silently swallow. Every test body MUST include an explicit
     `EXPECT_CALL(badge_, incrementNotificationBadge())` with the correct `.Times(N)`:
     use `.Times(0)` in tests that do not involve slot-overflow displacement (§(a) collapse);
     use `.Times(N)` where N collapses are expected. Tests that already declare a badge
     `EXPECT_CALL` (e.g. `NotifRail_OldestNormalCollapsed_AppendsToLog`,
     `NotifRail_EvictedEleventhNormal_AppendsToLog`) are unaffected — their existing
     declarations remain correct.
  6. `std::unique_ptr<NotificationManager> notifications_;`
  7. `std::unique_ptr<HUD> hud_;`

  **Note — no `log_` fixture member**: `NotificationManager` has no `ILogger*` injection
  seam; eviction vs. collapse is distinguished via `NotificationLog::getEntryCount()` and
  badge increment counts. Do NOT declare a `log_` mock member in this fixture — it is
  never declared and there is no logging seam to inject.

- `void SetUp() override`: **Construction order is critical** — the ON_CALL handle-capture
  lambdas must be installed first, then `hud_` constructed, then `allocatedHandles_` cleared,
  then `notifications_` constructed:

      // 1. ON_CALL lambdas already installed (see handle capture pattern above)
      // 2. Construct HUD — HUD-constructor addStaticText/addButton calls are captured but
      //    must be discarded before notification indices are used
      hud_ = std::make_unique<HUD>(&backend_, &audio_, &sim_, &clock_);
      // 3. MUST clear before constructing NotificationManager so allocatedHandles_[0]
      //    maps to the first notification card handle, not a HUD element
      allocatedHandles_.clear();
      // 4. Construct NotificationManager — only notification allocations captured from here
      notifications_ = std::make_unique<NotificationManager>(&backend_, &sim_, &clock_, &audio_, &badge_);

  (`StrictMock` will verify all badge expectations at destruction — no explicit reset needed in TearDown).
- `void TearDown() override`: explicit reset order (most important for multi-mock
  teardown):
  1. `hud_.reset();` — destroy `HUD` first (it may hold handles owned by `notifications_`
     indirectly via backend registry; destroying it first avoids post-destruction
     callbacks into `notifications_`).
  2. `notifications_.reset();` — destroy `NotificationManager` next (this releases any
     `m_audio->playSound(...)` call paths before `audio_` is destroyed).
  3. Mocks auto-destruct in reverse declaration order after `TearDown()` returns
     (GoogleMock `VerifyAndClearExpectations` runs at mock destruction).
- Add `.Times(1)` qualifier to EXPECT_CALLs that should fire exactly once.
- **Handle capture pattern** (required for `NotifRail_DismissCard_ShiftsRemainingUp`,
  `NotifRail_DismissCard_UsesSetElementRect_NotRecreate`,
  `NotifRail_FifthNormal_CollapsesOldestNormal_NotCritical`,
  `NotifRail_ModalActive_HidesAllVisibleCards`,
  `NotifRail_FindSlotIndexForDismissButton_ReturnsCorrectIndex_UnknownReturnsMinusOne`,
  `NotifRail_EnterDismissesFocusedCritical`): many `NotificationRailTest` cases need to
  reference `UIElementHandle` values returned from `addStaticText` / `addButton` during
  card construction (e.g. `postNormal()` / `postCritical()` calls) so that subsequent
  assertions on `setElementRect`, `setElementVisible`, etc. can use the captured
  handles. Use a monotonic counter `ON_CALL` pattern in `SetUp()` (or a fixture helper):

      ```cpp
      // Member in the fixture class:
      UIElementHandle nextHandle_ = 100;
      std::vector<UIElementHandle> allocatedHandles_;

      // REQUIRED construction order in SetUp() — MUST follow this exact sequence:
      //   1. Install ON_CALL lambdas (before any construction, so all allocations are captured)
      //   2. Construct hud_ (HUD constructor emits addStaticText/addButton calls that
      //      pollute allocatedHandles_ — these must be discarded before notification handles)
      //   3. allocatedHandles_.clear()  ← CRITICAL: discard all HUD-constructor entries so
      //      that allocatedHandles_[0] maps to the first NotificationManager handle, not a HUD element
      //   4. Construct notifications_ (only notification handle allocations from here)
      //
      // In SetUp() (after backend_ is constructed):
      ON_CALL(backend_, addStaticText(_, _, _, _, _))
          .WillByDefault([this](auto...) {
              UIElementHandle h = nextHandle_++;
              allocatedHandles_.push_back(h);
              return h;
          });
      ON_CALL(backend_, addButton(_, _, _, _, _))
          .WillByDefault([this](auto...) {
              UIElementHandle h = nextHandle_++;
              allocatedHandles_.push_back(h);
              return h;
          });
      ```

  **Allocation order per card** (fixed — tests reference handles by position in the
  sequence): per the card anatomy in Deliverable 3, each card allocates seven handles in
  this order:
  1. `hPanel` ← `addStaticText` (card panel — Deliverable 3 §Card anatomy — panel
     before children)
  2. `hIcon` ← `addButton` (empty-label icon button so `setElementImage` works)
  3. `hTitle` ← `addStaticText`
  4. `hMsg` ← `addStaticText`
  5. `hTime` ← `addStaticText` (monospace timestamp)
  6. `hDismiss` ← `addButton` (empty-label click-target dismiss button)
  7. `hDismissLabel` ← `addStaticText` ("✕" dismiss glyph overlay — Deliverable 3 §Dismiss
     button; child `IGUIStaticText` avoids skin-global `EGDC_BUTTON_TEXT` side-effect)

  So for the Nth card constructed, `allocatedHandles_[7*N + 0]` is `slotN.hPanel`,
  `allocatedHandles_[7*N + 1]` is `slotN.hIcon`, …, `allocatedHandles_[7*N + 5]` is
  `slotN.hDismiss`, and `allocatedHandles_[7*N + 6]` is `slotN.hDismissLabel`. Tests may
  alias these as local variables for readability
  (e.g. `auto slot1_hDismiss = allocatedHandles_[7*1 + 5]; auto slot1_hDismissLabel = allocatedHandles_[7*1 + 6];`). The
  `.WillByDefault(...)` installs a default action; any `EXPECT_CALL(..., addStaticText(...))`
  / `addButton(...)` that a specific test adds (e.g. to assert `.Times(0)` during a
  reshuffle) overrides for that expectation without invalidating the capture lambda.

- [ ] Register the new test file in `CMakeLists.txt`:
      `target_sources(ui_tests PRIVATE tests/ui/HUDGlacierGlassTest.cpp)`.
      The `ui_tests` target already links `aitown_ui` and has the correct include paths.
      (10 tests total: 5 original + Test 6 sim_ dispatch + Test 7 hover sprite-swap + Test 8 drawNineSlice + Test 9 hover-alpha-lerp + Test 10 hover-exit)

#### 9. `architecture/ui-ux/resolution-ui-scaling.md` — VERIFY only (already updated in earlier phase)

The Glacier Glass + Silver Chrome palette rename and panel-colour table update described in earlier drafts of this deliverable were completed by a prior phase. Verify:

- [ ] `architecture/ui-ux/resolution-ui-scaling.md` section heading reads `## Visual Design — Glacier Glass + Silver Chrome: Canonical Colour Palette` — no rename needed.
- [ ] The panel-colour table (rows for Top bar, Left toolbar, Zone sub-panel, Utilities sub-panel, Notification cards, Minimap, Grace period indicator) lists Glacier Glass `SColor(*, 4, 12, *)` values — no table update needed.
- [ ] The Superseded Values section lists the old Glass City palette values as superseded — no change needed.

No edits to `resolution-ui-scaling.md` are expected. Update the Files Changed row for this file to `(VERIFY only — no change expected)`.

#### 10. `architecture/ui-ux/notification-system.md` — update to right-side permanent rail

Update the following sections to match the new right-rail card model (matching
`hud-layout.md`). This is the spec-fix companion to the code changes in Deliverable 3 —
the queue behaviour contracts (auto-pause, priority separation, modal suppression, FIFO)
are preserved; only the visual/layout sections change:

- [ ] **Transient toasts / layout position**: Update from top-center to right-side permanent
      rail (right:12px, top:64px, 292 px wide, max 3 visible). Cards persist until dismissed.
- [ ] **Toast height constraints**: 80 px for both CRITICAL and Normal cards (unified height
      for alignment). Previous 48 px CRITICAL / 40-63 px Normal values superseded.
- [ ] **Priority separation**: CRITICAL cards still appear at top of rail and trigger auto-pause
      on first arrival. The separate y:20-116 / y:130 band layout is replaced by unified rail
      position (card N top = 64 + N×87 where 87 = 80 card + 7 gap). CRITICAL cards display
      at positions 0-N, Normal cards fill remaining slots.
- [ ] **Visual Design — Glass City → Glacier Glass + Silver Chrome**: Card background
      SColor(71, 4, 12, 30); 3 px (not 2 px) left severity strip per CRITICAL/WARNING/INFO;
      title #E2F2FF; body rgba(155,192,228,0.82); timestamp SColor(140,130,170,210); dismiss
      rgba(180,210,240,0.38); bell icon rgba(180,210,240,0.68).
- [ ] **Queue model preserved**: CRITICAL and Normal queues (std::deque) are retained. The
      CardSlot[3] array is presentation-only — a view over the queue top 3. Auto-pause on first
      CRITICAL, no double-pause, modal suppression, 10-deep Normal FIFO with log-fallback: all
      retained unchanged.
- [ ] **CRITICAL queue max-visible limit**: Update the CRITICAL queue max-visible limit: remove
      the legacy per-severity cap of 2 and replace with `kNotifMaxVisible` (3) — CRITICALs fill
      slots 0…(K−1) where K ≤ kNotifMaxVisible; the allocation table gains the row
      "3 CRITICAL visible → 0 Normal visible". Note: with this cap raised to 3, the
      `NotifRail_PostCritical_BurstThree_AudioRateLimit_ExactlyTwoFires` test is no longer in
      conflict — all 3 CRITICALs can be simultaneously visible in the rail, making the test's
      assumption (all 3 land in visible slots) valid.
- [ ] **Replace stale content in `architecture/ui-ux/notification-system.md §Phase 10 audio call sites`** with the Phase 11q13 unified fire-on-display contract: remove the stale description of `postCritical()`/`postNormal()` calling `playSound` directly (the old "CRITICAL fires on enqueue, Normal fires on visibility transition" split-contract sentence must be deleted, not merely supplemented). Replace it with the unified fire-on-display contract: both CRITICAL and Normal
      cards fire `playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)` from
      `refreshVisibleSlots()` when the card's slot transitions from not-visible to visible
      (rate-limited to 150 ms minimum interval via `IClock*` / `m_lastToastSoundTime`);
      edge-detection via `soundFired` flag on both `CriticalToast` and `NormalToast`
      prevents re-fire. The old "CRITICAL fires on enqueue, Normal fires on
      visibility transition" split-contract sentence is **removed** — replaced in its entirety
      by the unified fire-on-display contract.
- [ ] Update `architecture/ui-ux/hud-layout.md §ui_toast` call site note to reference the
      unified fire-on-display contract (both CRITICAL and Normal fire on visibility
      transition in `refreshVisibleSlots()`, rate-limited to 150 ms via
      `m_lastToastSoundTime`).
- [ ] Update `§NotificationManager Constructor` to show the full five-parameter signature:
      `NotificationManager(IUIBackend*, ICitySimulation*, IClock*, IAudioSystem*, IHUDBadgeNotifier* badgeNotifier = nullptr)`
      and document the `m_badge` nullable injection contract (null = badge disabled).
      **Also update the section heading** from `"Full constructor signature (Phase 10)"` to
      `"Full constructor signature (Phase 10 / Phase 11q13)"` — the `IHUDBadgeNotifier*`
      parameter is a Phase 11q13 addition; the heading must not attribute it to Phase 10
      (which only added `IAudioSystem*`).
- [ ] Add `void onInput(const InputEvent& ev)` to `§NotificationManager API` with its
      contract: handles `KeyDown` Enter/Delete dismiss and Tab-cycle across visible CRITICAL
      cards; called only when UIManager's Priority 2 guard
      (`hasCriticalToastVisible() && !modal`) passes.
- [ ] Update `§NotificationManager API` — revise `dismissCriticalToast(UIElementHandle)`
      description from 'production API for player-dismissal' to 'internal delegation called
      by `dismissCard()` for CRITICAL cards; not invoked directly by UIManager or
      player-input handlers after Phase 11q13'.
- [ ] Update `architecture/ui-ux/minimap.md §Overlay Toggle Buttons`: replace old 32×32 px
      toggle row at y:848–880 with new **64×22 px buttons** at y:838–860 (x:1720 Map,
      x:1788 Traffic, x:1856 Service, 4 px gap). Add active border constant
      `kMinimapToggleActiveBorder = 0xA500C8F0`. Add dot-indicator colours: Map #A8C8F0,
      Traffic #FF7050, Service #50D890. Update render area to y:864–1064
      (kMapY=864, kMapH=200).
- [ ] Add `void dismissCard(int slotIndex)` to `§NotificationManager API` with contract:
      unified dismiss entry point for player click, Enter, and Delete; delegates to
      `dismissCriticalToast()` for CRITICAL slots, removes queue entry and calls
      `refreshVisibleSlots()` for Normal slots.
- [ ] Add `int findSlotIndexForDismissButton(UIElementHandle h) const` to `§NotificationManager API`
      with contract: returns slot index [0..kNotifMaxVisible) whose `hDismiss` matches h,
      or -1 if not found.
- [ ] Update minimap.md §Visual Design — Glacier Glass §Minimap Background: replace old panel background values (rgba(13,27,42,0.85) / setElementBackground(h,13,27,42,217) for m_mapBg and rgba(13,27,42,0.82) / setElementBackground(h,13,27,42,209) for m_legendPanel) with kGlacierPanelBg SColor(66,4,12,28) values. Note old values as superseded.
- [ ] Explicitly update notification log panel background in notification-system.md to Glacier Glass values: setElementBackground(h, 4, 12, 28, 66) (kGlacierPanelBg) — consistent with all other panels.

#### 11. Tests — `tests/ui/NotificationRailTest.cpp` (new file)

- [ ] `NotifRail_PostNormal_FirstCardAtRailTop`: `postNormal(...)` creates card elements
      with the panel's y-position at `kNotifRailTop` (64). Verify via
      `EXPECT_CALL(backend_, addStaticText(_, 1616, 64, 292, 80))` (or equivalent element
      registration check).
- [ ] `NotifRail_PostNormal_SecondCardBelowFirst`: Second `postNormal(...)` creates panel
      at y = 64 + (80 + 7) = 151.
- [ ] `NotifRail_DismissCard_ShiftsRemainingUp`: After `postNormal` × 2, `dismissCard(0)`
      repositions card-1's **all seven** element handles (hPanel, hIcon, hTitle, hMsg,
      hTime, hDismiss, hDismissLabel) to slot-0 y-coordinates (not just the panel). Assert `setElementRect`
      is called once per handle with the correct rect (values derived from Deliverable 3
      card anatomy: cardLeft=1616, cardRight=1908, cardTop=64 for slot 0): - `setElementRect(slot1.hPanel,        kNotifCardLeft,        64,  292, 80)` — panel at rail-top - `setElementRect(slot1.hIcon,         kNotifCardIconLeft,    74,  20,  20)` — icon (kNotifCardIconLeft, cardTop+10) - `setElementRect(slot1.hTitle,        kNotifCardTextLeft,    74,  kNotifCardTextWidth, 18)` — title (kNotifCardTextLeft, cardTop+10, w=kNotifCardTextWidth, h=18) - `setElementRect(slot1.hMsg,          kNotifCardTextLeft,    94,  kNotifCardTextWidth, 28)` — message (cardTop+30, h=28) - `setElementRect(slot1.hTime,         kNotifCardTextLeft,    124, kNotifCardTextWidth, 14)` — timestamp (cardTop+60, h=14) - `setElementRect(slot1.hDismiss,      kNotifCardDismissLeft, 72,  16,  16)` — dismiss click-target (kNotifCardDismissLeft, cardTop+8, 16×16) - `setElementRect(slot1.hDismissLabel, kNotifCardDismissLeft, 72,  16,  16)` — dismiss glyph overlay (same rect as hDismiss)
      All seven `.Times(1)`. This catches a
      buggy implementation that moves only the panel and leaves the 6 child elements at
      old Y coordinates.
- [ ] `NotifRail_FifthNormal_CollapsesOldestNormal_NotCritical`: Post 3 Normal cards via
      `postNormal()` × 3 (fills all 3 slots: Normal0–Normal2, handles allocated at slots 0–2).
      // After the 3 posts: Normal0=slot0, Normal1=slot1, Normal2=slot2
      // (FIFO-front → slot 0 per Deliverable 3 ordering rule; Normal0 is the oldest).

      **Test structure — two phases split by `Mock::VerifyAndClearExpectations`:** The
      CRITICAL-arrival step requires two distinct handle-allocation scopes. Structure
      the test as follows:

      **Phase 1 — CRITICAL post allocates 7 fresh handles (then collapse + reshuffle):**
      Call `Mock::VerifyAndClearExpectations(&backend_)` to clear the allocation
      expectations from the 3-Normal setup. Then set expectations that ALLOW the
      fresh allocations for the new CRITICAL card (which is transitioning from
      not-visible to visible for the first time, so it needs 7 new handles):
        `EXPECT_CALL(backend_, addStaticText(_, _, _, _, _)).Times(5);  // CRITICAL: hPanel + hTitle + hMsg + hTime + hDismissLabel`
        `EXPECT_CALL(backend_, addButton(_, _, _, _, _)).Times(2);      // CRITICAL: hIcon + hDismiss`
        `EXPECT_CALL(badge_, incrementNotificationBadge()).Times(1);    // Normal0 collapse triggers badge increment`
      Post 1 CRITICAL via `postCritical()` × 1. Under the log-append-on-collapse rule
      (Deliverable 3 §(a)), the **oldest visible Normal** — Normal0, currently at slot 0 —
      is auto-collapsed: Normal0's seven handles are freed via `removeElement(...)`
      (seven calls), Normal0's queue entry is permanently removed from the visible
      presentation layer, and its data is appended to the notification log. The
      CRITICAL then occupies slot 0 (using its 7 freshly allocated handles). Normal1,
      Normal2 remain visible and shift to fill slots 1, 2 respectively
      (reusing their existing handles via `setElementRect` — no new allocations for them).
      // After reshuffle: CRITICAL=slot0, Normal1=slot1 (now the oldest *visible* Normal),
      // Normal2=slot2. (Normal0 is collapsed / not visible.)
      Call `Mock::VerifyAndClearExpectations(&backend_)` to verify the Phase 1
      expectations hold: exactly 5 `addStaticText` and 2 `addButton` calls were made
      during `postCritical()` (all for the new CRITICAL card's fresh handles; none for
      the reshuffled Normals).

      **Phase 2 — 4th Normal post + Normal shift (NO new allocations):** Now set:
        `EXPECT_CALL(backend_, addStaticText(_, _, _, _, _)).Times(0);`
        `EXPECT_CALL(backend_, addButton(_, _, _, _, _)).Times(0);`
      Then post a 4th Normal via `postNormal(...)` (Normal3). Under
      log-append-on-collapse: Normal1 is now the oldest visible Normal (it occupies
      the lowest Normal slot = slot 1) and is therefore the one that collapses to make
      room for Normal3. Verify `EXPECT_CALL(backend_, removeElement(_)).Times(7)` fires
      for Normal1's seven handles (hPanel, hIcon, hTitle, hMsg, hTime, hDismiss,
      hDismissLabel) — `setElementVisible(false)` is NOT an acceptable substitute; all
      disposal paths use `removeElement` × 7 per the card lifecycle mandate. Normal1's
      data is appended to the log. After this second reshuffle the visible state is:
      CRITICAL=slot0, Normal2=slot1, Normal3=slot2 (Normal2 shifts up one slot;
      Normal3 occupies the vacated slot 2).

      **NOTE on Phase 2 `Times(0)`:** Because Normal3 is entering visibility for the
      first time, it ALSO needs 7 fresh handles. This contradicts `Times(0)` unless
      we are careful. Concretely: the collapse of Normal1 frees 7 handles via
      `removeElement`; Normal3 then allocates 7 fresh handles. To assert that the
      Normal2 shift uses `setElementRect` and does NOT recreate its handle,
      the test MUST instead use the **sequenced pattern** below:

        - Before posting Normal3: `Mock::VerifyAndClearExpectations(&backend_)`.
        - Set
          `EXPECT_CALL(backend_, addStaticText(_, _, _, _, _)).Times(5);  // Normal3 fresh: hPanel+hTitle+hMsg+hTime+hDismissLabel`
          `EXPECT_CALL(backend_, addButton(_, _, _, _, _)).Times(2);      // Normal3 fresh: hIcon+hDismiss`
          `EXPECT_CALL(badge_, incrementNotificationBadge()).Times(1);    // Normal1 collapse triggers badge increment`
          `EXPECT_CALL(backend_, removeElement(_)).Times(7);             // Normal1 collapse: all 7 handles freed (setElementVisible(false) is NOT a substitute)`
          `EXPECT_CALL(backend_, setElementRect(slot2.hPanel, ...)).Times(1); // Normal2 shifts slot2→slot1`
          (plus all 5 child rects + dismiss label rect for the shifted card — 7 `setElementRect` calls total
          for the one shifted Normal: 1 card × 7 handles.)
        - Post Normal3 via `postNormal(...)`.
        - `Mock::VerifyAndClearExpectations(&backend_)` to verify.

      This locks the "CRITICAL cards are never collapsed while Normal cards remain
      visible" invariant from `notification-system.md §Normal queue rule` and the
      log-append-on-collapse rule from `notification-system.md` lines 4 and 41, AND
      the handle-reuse-on-shift invariant (only cards entering visibility for the
      first time allocate new handles; already-visible shifted cards are repositioned
      via `setElementRect` only).

- [ ] `NotifRail_DrawOverlay_EmitsFillColoredRect_ForCriticalStrip`: After a
      `postCritical(...)`, calling `NotificationManager::drawOverlay()` emits
      `EXPECT_CALL(backend_, fillColoredRect(1616, 64, 3, 80, _, _, _, _))` (3 px wide
      strip at card left edge).
- [ ] `NotifRail_CardBg_UsesGlacierGlassColor`: After `postNormal(...)`, the card
      background element is created with `setElementBackground(h, 4, 12, 30, 71)`.
      Explicitly assert `addStaticText("", 1616, 64, 292, 80)` is called **before**
      `setElementBackground(h, 4, 12, 30, 71)` (via `InSequence seq` ordered
      `EXPECT_CALL` blocks) — locks the Deliverable 3 requirement that the card panel
      handle is created via `addStaticText` (not `addButton`), since
      `setElementBackground` is a silent no-op on non-`IGUIStaticText` elements.
- [ ] `NotifRail_AutoPause_OnFirstCritical`: Existing contract preserved —
      `postCritical(...)` calls `m_sim->setPaused(true)` (assert
      `EXPECT_CALL(sim_, setPaused(true)).Times(1)`) when CRITICAL queue transitions from
      empty to non-empty AND `m_modalActive == false` AND `!m_sim->isPaused()`. Author this
      as an explicit test case aligned with the canonical
      `NotificationSystem_AutoPause_OnFirstCriticalToast` test in
      `architecture/testing/testability-architecture.md`.
- [ ] `NotifRail_ThirdCard_BottomAboveMinimapToggles`: Post three Normal cards via `postNormal(...)`. Verify that the bottom y of the third card panel = `kNotifRailTop + 2 × (kNotifCardH + kNotifCardGap) + kNotifCardH = 64 + 2×87 + 80 = 318` and confirm 318 < `kMMBtnTop − 16` (= 822). This locks the layout-integrity invariant from `notification-system.md` that rail cards never overlap the minimap toggle row introduced in the same phase.

- [ ] `NotifRail_NormalCard_AutoDismiss_AfterFiveSeconds`: Post one Normal card via
      `postNormal(...)`. Verify the card is visible (slot 0 populated). Advance
      `clock_` by 5.1 seconds via `clock_.advance(5.1)`. Call
      `notifications_->update(clock_.nowSeconds())`. Verify:
      (a) `EXPECT_CALL(backend_, removeElement(_)).Times(7)` — all seven handles
          (hPanel, hIcon, hTitle, hMsg, hTime, hDismiss, hDismissLabel) are freed via
          `removeElement`. `setElementVisible(false)` is NOT an acceptable substitute;
          all disposal paths — including timer-expired auto-dismiss — must use
          `removeElement` on all seven handles per Deliverable 3's card lifecycle mandate.
      (b) `notifications_->getNormalQueueSize() == 0` confirms the Normal queue is
          empty after expiry. (`getLog().getEntryCount()` must NOT be used here —
          auto-dismissed cards are never appended to the log, so that check is
          trivially 0 regardless of whether auto-dismiss actually ran.)
      Locks the `update()` timer-expiry loop contract: Normal cards are auto-dismissed
      after `timeoutSeconds` (default 5.0 s) using `ManualClock`-measured elapsed time.

- [ ] `NotifRail_UIToast_TwoCriticalsThenNormal_NormalFiresAfterDismiss`:
      Post 2 CRITICAL cards and 1 Normal card with `clock_.advance(0.150)` between each
      call so each slot transition fires audio (fire-on-display with 150 ms rate-limit):
      `     postCritical(...);       // t = 0.000s — slot0 visible, fires (1)
    clock_.advance(0.150);  // t = 0.150s
    postCritical(...);       // slot1 visible, fires (2) — 0.150 >= 0.150
    clock_.advance(0.150);  // t = 0.300s
    postNormal(...);         // slot2 visible (Normal fills remaining slot), fires (3)
    `
      All 3 slots are now occupied (2 CRITICAL + 1 Normal). Then call `postNormal(...)`.
      Verify the second Normal card's `playSound` is **NOT** called during `postNormal()`
      (Normal cannot become visible while all 3 slots are occupied).
      Then call `clock_.advance(0.150); // t = 0.450s, gate satisfied` so that at least
      150 ms has elapsed since `m_lastToastSoundTime` (set at t=0.300s when the first Normal
      became visible), ensuring the rate-limit gate is satisfied before the queued Normal
      card's visibility transition fires audio. Then `dismissCard(0)` on the oldest CRITICAL (slot 0 — FIFO-front per Deliverable 3 ordering rule). Verify
      `playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)` fires exactly once additional
      time (for the queued Normal card's visibility transition as it enters the vacated slot).
      Total expected calls: 4 (2 from CRITICAL visibility transitions + 1 from first Normal
      visibility transition + 1 from queued Normal visibility transition after dismiss).
      `EXPECT_CALL(audio_, playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)).Times(4)`.

- [ ] `NotifRail_EvictedEleventhNormal_UIToastNeverFires`: Post 2 CRITICALs (fills slots 0–1, leaving slot 2 available for Normals)
      via 2× `postCritical(...)` with `clock_.advance(0.150)` between calls (fires
      `playSound` 2 times for the CRITICAL visibility transitions), then post 1 Normal to
      fill slot 2 (fires `playSound` 1 more time — 3 initial fires total). Then call
      `Mock::VerifyAndClearExpectations(&audio_)` to clear the initial audio expectations.
      Set `EXPECT_CALL(audio_, playSound(_, _, _)).Times(0)`. Then post 11 Normal cards via
      `postNormal(...)` (no new slot becomes visible since all 3 are occupied by 2 CRITICAL + 1 Normal);
      the 11th enqueued Normal evicts the oldest queued Normal via log-fallback. Verify zero `playSound`
      calls occur for all 11 `postNormal()` invocations.

- [ ] `NotifRail_OldestNormalCollapsed_AppendsToLog`: Post 3 Normal cards via
      `postNormal(...)` × 3 (fills all 3 slots: Normal0–Normal2 at slots 0–2, per
      FIFO-front → slot 0 rule; Normal0 is the oldest visible Normal). Then post
      1 CRITICAL via `postCritical(...)`. The oldest visible Normal (Normal0 at slot 0)
      is auto-collapsed per Deliverable 3 §(a). Verify:
      (a) The card data for Normal0 (title, message, timestamp, severity) was appended
      to the notification log via `NotificationLog::append(...)` — assert via a
      test accessor `NotificationLog::getEntryCount()` returning 1 after the collapse
      (or equivalent observable on the log panel under test).
      (b) The bell icon unread-count badge incremented by 1 — assert via
      `EXPECT_CALL(badge_, incrementNotificationBadge()).Times(1)` where `badge_` is a
      `MockHUDBadgeNotifier` injected as the fifth constructor parameter of
      `NotificationManager` (see IHUDBadgeNotifier injection section in Deliverable 3).
      Do NOT use a HUD mock for this assertion; the injection seam is `IHUDBadgeNotifier*`.
      (c) Normal0's seven handles are freed via `removeElement(...)` (seven calls). Assert
      `EXPECT_CALL(backend_, removeElement(_)).Times(7)` scoped to the collapse step.
      Locks the log-append semantics from `notification-system.md` lines 4 and 41.

- [ ] `NotifRail_EvictedEleventhNormal_AppendsToLog`: Post 2 CRITICALs (fills slots 0–1, leaving slot 2 available for Normals)
      via 2× `postCritical(...)` with `clock_.advance(0.150)` between calls, plus
      1 Normal via `postNormal(...)` to fill slot 2 (all 3 slots now occupied; no further
      Normal slot becomes visible). Post 10 Normal cards via `postNormal(...)` × 10
      (fills `m_normalQueue` to its 10-deep capacity; none become visible). Then post an
      11th Normal via `postNormal(...)`. Per Deliverable 3 §(b), the 11th Normal evicts
      the oldest Normal (front of `m_normalQueue`) via queue-capacity eviction. Verify:
      (a) The evicted card's data (title, message, timestamp, severity) was appended to
      the notification log via `NotificationLog::append(...)` — assert via the log
      entry-count or tail observer. The log now contains the evicted Normal's data.
      (b) Assert `notifications_->getLog().getEntryCount()` increased by 1 for the evicted
      entry (both §(a) collapse and §(b) eviction append to NotificationLog; the observable
      distinction is the badge: §(a) calls `incrementNotificationBadge()`, §(b) does NOT —
      this replaces the undecidable LOG_WARN check).
      (c) The bell icon badge increment does **NOT** fire — assert via
      `EXPECT_CALL(badge_, incrementNotificationBadge()).Times(0)`.
      Badge increments fire ONLY on §(a) slot-overflow displacement (a Normal card is
      collapsed to make room on the visible rail), NOT on §(b) queue-capacity eviction
      (which silently removes the oldest entry from the backing queue without any
      visible-rail displacement).
      Locks the queue-capacity-eviction-appends-to-log contract from `notification-system.md`.

- [ ] `NotifRail_RefreshVisibleSlots_AlreadyVisibleCard_DoesNotRefireUIToast`:
      Set up `EXPECT_CALL(audio_, playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)).Times(2)`
      **before** any posts. Then post one Normal card via `postNormal(...)` (card 1
      becomes visible in slot 0 → fires once). Then post a SECOND Normal card via
      `postNormal(...)` (card 2 becomes visible in slot 1 → fires for card 2 only; card
      1's queue entry has `soundFired == true` so no re-fire). Then call
      `dismissCard(0)` which triggers `refreshVisibleSlots()` and shifts card 2 into
      slot 0. Verify total `playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)` call count
      = 2 (one per card, NOT re-fired on the dismiss-triggered `refreshVisibleSlots()`).
      Locks the `soundFired` edge-detection invariant from Fix 3.

- [ ] `NotifRail_PostCritical_BurstThree_AudioRateLimit_ExactlyTwoFires`:
      Using `ManualClock`, post 3 CRITICALs via `postCritical(...)` at the following
      schedule (advance the `ManualClock` between calls using `clock_.advance(seconds)`).
      With `kNotifMaxVisible = 3`, all 3 cards land in visible slots (rail not full until
      after the 3rd post), so all 3 trigger the fire-on-display path — but the rate-limit
      gate suppresses the 2nd fire: - t = 0.000s: `postCritical(...)` — slot 0 visible →
      soundFired=true; gate: 0.000 - (-1e30) is huge ≥ 0.150 → `playSound` fires → fire 1.
      `m_lastToastSoundTime = 0.000`. - `clock_.advance(0.050)` → t = 0.050s - t = 0.050s:
      `postCritical(...)` — slot 1 visible → soundFired=true; gate: 0.050 - 0.000 = 0.050
      < 0.150 → rate-limited, `playSound` suppressed. Timestamp NOT updated. - `clock_.advance(0.100)`
      → t = 0.150s - t = 0.150s: `postCritical(...)` — slot 2 visible → soundFired=true;
      gate: 0.150 - 0.000 = 0.150 >= 0.150 → `playSound` fires → fire 2.
      `m_lastToastSoundTime = 0.150`.
      Expected total fires: 2 (at t = 0.000s and t = 0.150s).
      `EXPECT_CALL(audio_, playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)).Times(2)`.

- [ ] `NotifRail_PostCriticalDuringModalActive_UIToastNotFiredWhileModalActive`:
      Call `setModalActive(true)`, then `postCritical(...)`. Verify
      `playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)` is **NOT** called during
      `postCritical()` itself — audio fires on display, and while modal is active the
      CRITICAL card is not displayed. `EXPECT_CALL(audio_, playSound(_, _, _)).Times(0)`
      set up before `postCritical()`. Then call
      `setModalActive(false)`. Verify `playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)`
      fires **exactly once** as the CRITICAL card becomes visible (fire-on-display
      contract — `refreshVisibleSlots()` is called from `setModalActive(false)` and the
      CRITICAL slot transitions from not-visible to visible).
      `EXPECT_CALL(audio_, playSound(UI_TOAST, SoundPriority::HIGH, 1.0f)).Times(1)`
      set up before `setModalActive(false)`. Total fires = 1.

- [ ] `NotifRail_DismissCard_UsesSetElementRect_NotRecreate`:
      After `postNormal()` × 2 (setup; both slots pre-allocated), call
      `Mock::VerifyAndClearExpectations(&backend_)` to clear setup allocations. Then set: - `EXPECT_CALL(backend_, addStaticText(_, _, _, _, _)).Times(0)` - `EXPECT_CALL(backend_, addButton(_, _, _, _, _)).Times(0)` - `setElementRect` expectations for the seven handles of slot-1 repositioning to slot-0
      (same rects as `NotifRail_DismissCard_ShiftsRemainingUp`): - `setElementRect(slot1.hPanel,        kNotifCardLeft,        64,  292, 80)` - `setElementRect(slot1.hIcon,         kNotifCardIconLeft,    74,  20,  20)` - `setElementRect(slot1.hTitle,        kNotifCardTextLeft,    74,  kNotifCardTextWidth, 18)` — title w=kNotifCardTextWidth, h=18 - `setElementRect(slot1.hMsg,          kNotifCardTextLeft,    94,  kNotifCardTextWidth, 28)` — message h=28 - `setElementRect(slot1.hTime,         kNotifCardTextLeft,    124, kNotifCardTextWidth, 14)` — timestamp h=14 - `setElementRect(slot1.hDismiss,      kNotifCardDismissLeft, 72,  16,  16)` — dismiss click-target 16×16 - `setElementRect(slot1.hDismissLabel, kNotifCardDismissLeft, 72,  16,  16)` — dismiss glyph overlay 16×16
      Call `dismissCard(0)`. Verify (a) all seven `setElementRect` calls fire, (b)
      `addStaticText` not called, (c) `addButton` not called. The
      `VerifyAndClearExpectations` ensures assertions cover only the dismiss operation,
      not prior setup allocations. This locks the `setElementRect`-based reposition
      implementation mandated in Deliverable 3, preventing an equivalent but
      behaviourally different destroy/recreate path for any of the 5 child elements.

- [ ] `UIManagerDrawOrder_DrawOverlays_CalledAfterDrawAll` (**goes in `tests/ui/ui_manager_draw_order_test.cpp`**, NOT in `NotificationRailTest.cpp`):
      The existing `UIManagerDrawOrderTest` fixture's `UIManager` instance owns a real
      `NotificationManager` (not a sentinel stub). `NotificationManager::drawOverlay()`
      MUST be declared `public` (not `private` / `protected`) so the
      `UIManager::drawOverlays()` post-drawAll step can invoke it and the test can
      observe the resulting `fillColoredRect` call via the injected `MockUIBackend`.
      Test body: 1. Post a CRITICAL toast through the UIManager's owned `NotificationManager`
      (e.g. `uiManager.getNotificationManager().postCritical(...)`). 2. Call `uiManager->draw();` then `uiManager->drawOverlays();`. 3. Assert
      `EXPECT_CALL(backend_, fillColoredRect(1616, 64, 3, 80, 200, 40, 30, 204))
       .Times(AtLeast(1))` fires after the two calls — this confirms
      `drawOverlays()` was invoked and ran `NotificationManager::drawOverlay()`
      with the exact Deliverable 3 CRITICAL strip colour `SColor(204, 200, 40, 30)`.
      Note: `IUIBackend` does NOT declare `drawAll()` as a virtual method — `MockUIBackend`
      has no `MOCK_METHOD` for it, so `EXPECT_CALL(backend_, drawAll())` would not compile.
      The ordering guarantee (drawOverlays after drawAll) is enforced architecturally in
      `IrrlichtRenderer::drawScene()`, not verifiable through `MockUIBackend` since
      `drawAll()` calls `IGUIEnvironment::drawAll()` on the real Irrlicht layer, not on
      `IUIBackend`. This test verifies that `drawOverlays()` produces the expected
      `fillColoredRect` output.
      **Prerequisite**: The `NotificationManager& getNotificationManager()` accessor in
      `UIManager` is listed in the Files Changed table (`src/ui/UIManager.h`). Use this
      accessor in the test: `uiManager.getNotificationManager().postCritical(...)`.
      **Sentinel coexistence decision**: The Phase 3 `kNotificationSentinel = 0xDEAD0105u`
      sentinel call `setElementVisible(kNotifSentinel, true)` in the old
      `NotificationManager::draw()` stub is **removed** when the full `drawOverlay()`
      implementation replaces the stub. The existing `UIManagerDrawOrderTest` (in
      `tests/ui/ui_manager_draw_order_test.cpp`) MUST be updated: replace any
      `EXPECT_CALL(backend_, setElementVisible(kNotifSentinel, ...))` assertion with
      `EXPECT_CALL(backend_, fillColoredRect(1616, 64, 3, 80, 200, 40, 30, 204))` (the
      real CRITICAL severity strip output). Confirm method name in `UIManager` is `drawOverlays()`
      (not the old `drawMinimapOverlay()`).

      **`UIManagerDrawOrder_ModalActive` update**: The `UIManagerDrawOrder_ModalActive` test
      in `tests/ui/ui_manager_draw_order_test.cpp` (per
      `architecture/testing/testability-architecture.md`) also asserts
      `EXPECT_CALL(backend_, setElementVisible(handles::kNotificationSentinel, true))`.
      When Phase 11q13 removes the sentinel, this line must also be removed from
      `UIManagerDrawOrder_ModalActive`. In modal state, all notification cards are hidden —
      `NotificationManager::drawOverlay()` emits no `fillColoredRect` — so there is no
      ordering signal to assert for the notification manager in the modal draw-order test.
      Remove the `kNotificationSentinel` expectation from `UIManagerDrawOrder_ModalActive`
      and update the comment to:
      `// NotificationManager: modal hides all cards, drawOverlay emits nothing, no ordering assertion (Phase 11q13+)`.
      The scrim and modal ordering assertions (`kScrimSentinel`, `kModalSentinel`) remain.
      Add `tests/ui/ui_manager_draw_order_test.cpp` to the Files Changed table covering both
      `DrawOrder_BackToFront_MatchesSpec` AND `UIManagerDrawOrder_ModalActive`.

- [ ] `HUDGlacierGlass_SetElementTextColorA_BodyText_UsesSpecPalette`:
      Construct `NotificationManager`. Call `postNormal("msg", "Title")`.
      Verify `setElementTextColorA(hMsgHandle, 155, 192, 228, 209)` is called for the message
      body text element (rgba(155,192,228,0.82) decomposed: a=round(0.82×255)=209).

- [ ] `HUDGlacierGlass_SetElementTextColorA_Timestamp_UsesSpecPalette`:
      After `postNormal(...)`, verify `setElementTextColorA(hTimeHandle, 130, 170, 210, 140)`
      is called for the timestamp element (SColor(140,130,170,210): a=140, r=130, g=170, b=210).

- [ ] `NotifRail_ModalActive_HidesAllVisibleCards`:
      Post 1 CRITICAL and 1 Normal card. Verify both card panels are visible (slot 0 and slot 1).
      Call `setModalActive(true)`. Verify `setElementVisible(slot0.hPanel, false)` and
      `setElementVisible(slot1.hPanel, false)` are called. Also verify
      `setElementVisible(slot0.hDismiss, false)` and `setElementVisible(slot1.hDismiss, false)`
      are called during `setModalActive(true)` (locks the dismiss button visibility — prevents
      stale dismiss-button clicks through the scrim). Call `setModalActive(false)`.
      Because `refreshVisibleSlots()` is a **private** method and cannot be observed through
      `MockUIBackend`, the post-modal-release assertion MUST use **observable side-effects**
      only: after `setModalActive(false)`, expect `setElementVisible(slot.hPanel, true)` for
      each occupied slot's panel handle (slot0.hPanel and slot1.hPanel) and for each CRITICAL
      slot's dismiss button handle (slot0.hDismiss in this setup). Concretely, set up
      expectations BEFORE the `setModalActive(false)` call:
      `EXPECT_CALL(backend_, setElementVisible(slot0.hPanel, true)).Times(1);`
      `EXPECT_CALL(backend_, setElementVisible(slot1.hPanel, true)).Times(1);`
      `EXPECT_CALL(backend_, setElementVisible(slot0.hDismiss, true)).Times(1);`
      `EXPECT_CALL(backend_, setElementVisible(slot1.hDismiss, true)).Times(1);`
      Alternatively, for a less brittle assertion:
      `EXPECT_CALL(backend_, setElementVisible(_, true)).Times(AtLeast(2));` — at least one
      `(..., true)` call per occupied slot's panel handle. Do NOT assert on
      `refreshVisibleSlots()` being called — private methods are not observable through the
      mock backend. The observable side-effects (visibility restored) are the contract.
      Update the test count in the CMakeLists registration comment from 20 to 25 tests.

- [ ] `NotifRail_FindSlotIndexForDismissButton_ReturnsCorrectIndex_UnknownReturnsMinusOne`:
      Post 2 Normal cards. Capture `slot0.hDismiss` and `slot1.hDismiss` by observing the
      `addButton(...)` call sequence during setup (use `EXPECT_CALL` with `SaveArg<0>` on
      the second `addButton` call per card — the dismiss button is always the last
      `addButton` call for each card, returning a distinct `UIElementHandle`). Call
      `findSlotIndexForDismissButton(slot0.hDismiss)` — assert returns 0. Call
      `findSlotIndexForDismissButton(slot1.hDismiss)` — assert returns 1. Call
      `findSlotIndexForDismissButton(UIElementHandle{9999u})` (unknown handle) — assert
      returns -1. Locks the `m_dismissButtonToSlot` lookup contract.

- [ ] `NotifRail_DismissCard_CriticalDelegatesToDismissCriticalToast_NoAutoResume`:
      Post 1 CRITICAL card. Verify `setPaused(true)` was called during `postCritical()`.
      Call `dismissCard(0)`. Verify `setPaused(false)` is **NOT** called during
      `dismissCard(0)` — `EXPECT_CALL(sim_, setPaused(false)).Times(0)`. Locks the
      no-auto-resume contract from `architecture/testing/testability-architecture.md
    §CriticalToast_OnLastDismiss_NoAutoResume`: dismissing a CRITICAL card never
      auto-resumes the simulation.

- [ ] `NotifRail_EnterDismissesFocusedCritical`:
      Post 1 CRITICAL card. The `NotificationRailTest` fixture has NO `UIManager`
      member — the test dispatches the keyboard event directly to the
      `NotificationManager` under test via the new public `onInput(const InputEvent&)`
      method (see Deliverable 3 §`NotificationManager::onInput()` public method below).
      Call
      `notifications_->onInput(InputEvent{InputEvent::Type::KeyDown, 0, 0, 0, 0, 0, 0.f, 13})`.
      // keyCode 13 = SDL2 SDLK_RETURN (Enter key)
      Verify `EXPECT_CALL(backend_, removeElement(_)).Times(7)` (the card is freed on
      keyboard dismiss (removeElement × 7 per lifecycle mandate) — Priority 2 path handled inside `NotificationManager::onInput()`).
      Locks the keyboard dismiss routing update specified in the `m_focusedCriticalIndex`
      semantics bullet above: keyboard dismiss routes via `dismissCard(m_focusedCriticalIndex)`.

- [ ] `NotifRail_DismissMiddleCritical_FocusFollowsShift`:
      Post 3 CRITICAL cards (with `clock_.advance(0.150)` between each). The 3 CRITICALs
      occupy slots 0, 1, 2 // 3 CRITICALs at slots 0 (oldest), 1, 2 per FIFO-front=slot-0 rule
      (see Deliverable 3 slot ordering rule). Dispatch a Tab `InputEvent` directly to the
      `NotificationManager` under test (the fixture has no `UIManager` member — use the
      new public `onInput(const InputEvent&)` method):

      ```cpp
      notifications_->onInput(InputEvent{InputEvent::Type::KeyDown, 0, 0, 0, 0, 0, 0.f, 9}); // keyCode 9 = SDLK_TAB
      // m_focusedCriticalIndex is now 1 (Tab advances focus from 0 to 1)
      ASSERT_EQ(notifications_->getFocusedCriticalIndex(), 1);
      ```

      Then call `dismissCard(0)` (mouse-dismiss on the top card, which is NOT the
      focused card). After `refreshVisibleSlots()` reshuffles: old slot-1 card is now
      at slot-0, old slot-2 card is at slot-1. Assert
      `notifications_->getFocusedCriticalIndex() == 0` (decremented by 1 because
      dismissed slotIndex=0 < focusedIndex=1). Locks the `m_focusedCriticalIndex`
      maintenance rule: dismiss of a card above the focused card decrements focus by
      1 so the focus ring follows the shifted card. Expose `m_focusedCriticalIndex`
      via a `int getFocusedCriticalIndex() const` test accessor declared in
      `NotificationManager.h` (listed in the Files Changed table for
      `src/ui/NotificationManager.h` alongside the other new public methods).

- [ ] `NotifRail_EnterDismissesFocusedNonSlot0Critical_RotatesAndDismissesCorrectCard`:
      Verifies that after removing the "OR — simpler" bypass, the
      `std::rotate`-then-`dismissCriticalToast` path correctly handles Tab+Enter on a
      non-slot-0 focused CRITICAL — dismissing the correct card, not the slot-0 card,
      while the no-auto-resume contract (`setPaused(false)` never called) holds.
      - Post 2 CRITICAL cards with `clock_.advance(0.150)` between them (so both audio
        calls fire; `Times(2)` for `playSound`).
      - Tab once (`notifications_->onInput(InputEvent{..., keyCode=9})`) to set
        `m_focusedCriticalIndex = 1`.
      - `EXPECT_CALL(sim_, setPaused(false)).Times(0)` — no-auto-resume must hold.
      - `EXPECT_CALL(backend_, removeElement(_)).Times(7)` — slot 1 card's 7 handles
        disposed on dismiss.
      - Slot-0 handles must NOT be freed — add explicit `Times(0)` guards before
        the Enter `onInput` call to detect any implementation that erroneously frees
        both slot-0 and slot-1 handles (NiceMock does not catch over-saturation of
        the `Times(7)` expectation, so these per-handle guards are required):
        ```cpp
        // Slot-0 handles must NOT be freed — assert card survives
        for (int k = 0; k < 7; ++k) {
            EXPECT_CALL(backend_, removeElement(allocatedHandles_[0*7 + k])).Times(0);
        }
        ```
      - Call `notifications_->onInput(InputEvent{..., keyCode=13})` (Enter) to dismiss
        the focused (slot-1) card.
      - Assert slot 0 card remains visible: its panel handle (from
        `allocatedHandles_[0*7+0]`) is still live — `setElementVisible` was not called
        false on it, and no `removeElement` was called for its 7 handles beyond the
        `Times(7)` already expected for the dismissed card.

**Test fixture contract**: same mock policy, member declaration order, `TearDown()` reset
sequence, and constructor signatures as Deliverable 8 — see that section for the full
contract. Both `HUDGlacierGlassTest.cpp` and `NotificationRailTest.cpp` share the same
fixture structure.

- [ ] Register in CMakeLists.txt:
      `target_sources(ui_tests PRIVATE tests/ui/NotificationRailTest.cpp)` (registers all 25 tests: 23 NotifRail\_\* + 2 HUDGlacierGlass\_SetElementTextColorA\_\*).
      Note: `UIManagerDrawOrder_DrawOverlays_CalledAfterDrawAll` is NOT counted here — it goes in `tests/ui/ui_manager_draw_order_test.cpp` (see Fix 3 in plan).

Also update `tests/ui/notification_system_test.cpp`: update coordinate-based assertions
referencing old top-center y:20/y:130 positions to the new rail coordinates (y:64, y:151,
etc). Enumerate the specific test methods that need coordinate updates during
implementation (any test with literal y:20, y:130, or top-center x-range assertions).

#### 12. Font baking — Barlow Condensed + Share Tech Mono

This phase requires the HUD bitmap fonts to be baked from the correct typefaces.

- [ ] Verify `assets/fonts/src/BarlowCondensed-Light.ttf` through
      `assets/fonts/src/BarlowCondensed-Bold.ttf` exist; if not, add them from Google
      Fonts under OFL license.
- [ ] Verify `assets/fonts/src/ShareTechMono-Regular.ttf` exists; if not, add it from
      Google Fonts under OFL license.
- [ ] Run `python3 tools/generate_bitmap_fonts.py` to regenerate all three tier-specific
      font pairs (6 font files):
      `assets/fonts/hud_font_720.xml`, `hud_font_1080.xml`, `hud_font_1440.xml`,
      `assets/fonts/hud_mono_font_720.xml`, `hud_mono_font_1080.xml`,
      `hud_mono_font_1440.xml`
- [ ] After regeneration, each XML file must contain the typeface name
      "Barlow Condensed" or "Share Tech Mono" in its glyph metadata.
- [ ] Verify `LICENSE.md` contains Barlow Condensed and Share Tech Mono entries
      under "SIL Open Font License 1.1"; add if missing.

#### 13. Rounded corners — 9-slice sprite rendering

Irrlicht `IGUIStaticText`/`IGUIButton` do not support `border-radius`. This deliverable
implements rounded corners via pre-baked 9-slice corner textures composited with
`driver->draw2DImage()`.

- [ ] Author `assets/textures/ui/panel_corners_10px.png` — a 128×128 RGBA PNG containing
      all four pre-flipped quarter-circle corners (10 px radius, anti-aliased alpha) in a
      2×2 grid layout: top-left corner at (0,0)–(64,64), top-right at (64,0)–(128,64),
      bottom-left at (0,64)–(64,128), bottom-right at (64,64)–(128,128). Each quadrant is
      a distinct pre-rotated variant — Irrlicht `draw2DImage` does **not** flip source rects,
      so all four orientations must be pre-baked. Authoritative asset; committed to repo.
      This PNG is loaded at runtime via `IVideoDriver::getTexture()` — permitted under the
      small UI overlay PNG exception (≤128×128 RGBA8) added to `2d-texture-standards.md`.
- [ ] Add `drawNineSlice(int x, int y, int w, int h, int cornerRadius, int r, int g, int b, int a)` to `IUIBackend`:
      1. In `src/interfaces/IUIBackend.h`: add `virtual void drawNineSlice(int x, int y, int w, int h, int cornerRadius, int r, int g, int b, int a) = 0;` as method 24. Update method-count comment from 23 to 24.
      2. In `src/rendering/IrrlichtUIBackend.h`: add override declaration.
      3. In `src/rendering/IrrlichtUIBackend.cpp`: implement — 9 `draw2DImage` calls
         (4 corners via `panel_corners_10px.png` 2×2 grid — TL at 0,0–64,64; TR at 64,0–128,64; BL at 0,64–64,128; BR at 64,64–128,128 — no source-rect flips needed, 4 edge strips,
         1 centre fill via `draw2DRectangle`). Uses `useAlphaChannel = true` for corners.
      4. In `tests/ui/MockUIBackend.h`: add `MOCK_METHOD` entry (method 24).
- [ ] Replace `setElementBackground` panel-background calls for all 7 panels (top bar,
      toolbar, Zone, Utilities, Grace Period, minimap, notification cards) with
      `drawNineSlice` calls in the `drawOverlays()` post-drawAll pass. Panel backgrounds
      become draw-time renders, not element properties. Remove the `IGUIStaticText`
      background panel handles (`m_topBarBg`, etc.) — they are superseded by `drawNineSlice`.
- [ ] Add unit test `HUDGlacierGlass_DrawNineSlice_Called_ForEachPanel`: verify
      `drawNineSlice` is called at least once per panel region in `drawOverlays()`.

#### 14. Letter spacing — bitmap font glyph widening

Irrlicht `IGUIFont::draw()` has no letter-spacing parameter. This deliverable widens
the glyph advance values in the bitmap font `.xml` files to match the mockup spacing.

- [ ] In `tools/generate_bitmap_fonts.py`: add a `--letter-spacing` parameter (integer,
      default 0) that inflates each glyph `rect` width by the specified amount (added to
      the right edge of each glyph rect). This widens the advance without changing the
      glyph image.
- [ ] Determine the target letter-spacing value from `hud-option-a-mercury.html` CSS
      (inspect `letter-spacing` property on `.top-labels`, `.mm-ov`, and panel text
      elements). Convert from CSS `em`/`px` to bitmap-font pixel units at each tier
      (720/1080/1440).
- [ ] Re-run `python3 tools/generate_bitmap_fonts.py --letter-spacing <N>` to regenerate
      all 6 font files with the new spacing.
- [ ] Visual verification: screenshot comparison of HUD text with and without spacing
      shows visible inter-character gaps matching the HTML mockup. No automated test
      required — this is a data-only change with no code path impact.

#### 15. Animated hover transitions — alpha-lerp overlay

The HTML mockup uses CSS `transition` for smooth hover effects on buttons. This
deliverable replaces the discrete sprite-swap hover with a smooth alpha-animated
overlay.

- [ ] Add per-button hover state tracking to `HUD.h`:

          struct ButtonHoverState {
              UIElementHandle overlay{kInvalidUIElement};
              float           t{0.0f};        // 0.0 = idle, 1.0 = fully hovered
              bool            hovering{false};
          };

- [ ] For each button that needs hover transitions (minimap toggles, toolbar buttons,
      speed buttons): create a companion `IGUIStaticText` overlay element positioned
      exactly over the button, with `setElementBackground` set to the hover highlight
      colour and initial alpha 0. Set the overlay as a **child sub-element** of the
      button (`IGUIElement::addChild()` + `setSubElement(true)`) so it does not intercept
      click events.
- [ ] In `HUD::update(float dt)` (called each frame before `drawAll()`):
      For each `ButtonHoverState`: if `hovering`, lerp `t` toward 1.0; else lerp toward
      0.0. Rate: `t += dt / kHoverTransitionDuration` (constant, e.g. 0.15 seconds).
      Call `m_backend->setElementAlpha(overlay, (int)(t * targetAlpha))`.
- [ ] In `HUD::onEvent()` mouse-move handler: hit-test each button rect; update
      `hovering` flag. This replaces the existing discrete sprite-swap for hover state.
      The active/inactive sprite-swap on click is preserved — the hover overlay is
      additive on top of the current sprite.
- [ ] Add `constexpr float kHoverTransitionDuration = 0.15f;` to `src/ui/ui_constants.h`.
- [ ] Add unit test `HUDGlacierGlass_HoverTransition_AlphaLerps`: post a simulated
      mouse-enter event, advance `ManualClock` by `kHoverTransitionDuration / 2`, call
      `hud.update(dt)`, verify `setElementAlpha` was called with an intermediate alpha
      value (not 0 and not full target alpha). Advance again to full duration, verify
      `setElementAlpha` was called with target alpha.
- [ ] Add unit test `HUDGlacierGlass_HoverExit_AlphaReturnsToZero`: post mouse-enter,
      advance to full hover, post mouse-exit, advance by `kHoverTransitionDuration`,
      verify `setElementAlpha` was called with 0.

---

### Files Changed

| File                                                                            | Change                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| ------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/ui/Minimap.cpp`                                                            | `setElementBackground` values updated for `m_mapBg` (SColor(66, 4, 12, 28)) and `m_legendPanel` (SColor(66, 4, 12, 28)) — Glacier Glass low-opacity dark navy. Render-area top: update `kMapY` in `src/ui/Minimap.h` from 880 to 864 (`kMapH` stays 200, computed bottom = kMapY + kMapH = 1064); update any inline comment in `src/ui/Minimap.cpp` that references the old 880/1080 values to reflect the new y:864–1064 range. Remove legacy Svc/Tfc hit-test branches from `onEvent()` that overlap new y:838–860 toggle row. Update `getWidgetFootprint()` to use named constants from `src/ui/ui_constants.h` instead of hard-coded literals (see Deliverable 7).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `src/ui/Minimap.h`                                                              | Update `kMapY` from 880 to 864 (16 px upward shift to accommodate toggle row at y:838–860 + 4 px gap); `kMapH` stays 200 (computed bottom = 864 + 200 = 1064). Add `void setOverlayActive(bool active)` public method if not already present; ensure `m_legendPanel` visibility is toggled by this method (visible when `m_overlayActive == true`, hidden when `false`).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| `src/ui/HUD.cpp`                                                                | `m_chromeStrip` element added (addStaticText + setElementBackground); `m_btnMinimapMap/Traffic/Service` added with sprite defaults; `onMinimapOverlayClick()` wired; getters added; text colour calls updated to new palette via `setElementTextColor` (fully-opaque) and `setElementTextColorA` (alpha-bearing, method 23); `kDeficitPulseArgb` literal updated `0x80F04E37` → `0x80FF7870`; active border colour updated teal → cyan; implement `void HUD::incrementNotificationBadge()` — increments the internal `m_notifBell` badge counter (e.g. `m_notifBell` unread-count integer member or equivalent UIElement badge value), updating the badge display on the notification bell icon                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `src/ui/HUD.h`                                                                  | `UIElementHandle m_chromeStrip`, `m_btnMinimapMap`, `m_btnMinimapTraffic`, `m_btnMinimapService` added; `m_topBarBg{kInvalidUIElement}` (Deliverable 1), `m_toolbarBg{kInvalidUIElement}` (Deliverable 1), `m_zonePanelBg{kInvalidUIElement}` (Deliverable 1), `m_utilsPanelBg{kInvalidUIElement}` (Deliverable 1), `m_gracePeriodBg{kInvalidUIElement}` (Deliverable 1), `m_minimapBg{kInvalidUIElement}` (Deliverable 4) added; `void drawOverlays()` (public) added (FIX J); `onMinimapOverlayClick(UIElementHandle)` declared **(public)**; `onMinimapOverlayHover(UIElementHandle)` declared **(public)**; `getBtnMinimapMap/Traffic/Service()` declared (public inline); declare `void incrementNotificationBadge();` as a public method (increments the `m_notifBell` badge counter — called by `UIManager::incrementNotificationBadge()` which delegates to `m_hud->incrementNotificationBadge()`)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| `src/ui/NotificationManager.cpp`                                                | Notification cards repositioned to right-side rail (`x:1616–1908, top:64`); `CardSlot` struct introduced; `postCritical()`/`postNormal()`/`dismissCard()` call `refreshVisibleSlots()` to populate presentation array from queue tops; `drawOverlay()` renders 3 px left severity strips via `m_backend->fillColoredRect(...)` from post-drawAll step; card background via `setElementBackground(h, 4, 12, 30, 71)`; alpha-bearing text colours (message body, timestamp, dismiss button) use `setElementTextColorA` (method 23); existing queue contracts (auto-pause, priority separation, modal suppression, 10-deep Normal FIFO) preserved                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| `src/ui/NotificationManager.h`                                                  | Remove `refreshCriticalVisibility()` and `refreshNormalVisibility()` private method declarations; remove `UIElementHandle handle` field from `CriticalToast` and `NormalToast` structs; add public `void drawOverlay()` method declaration; add private `void refreshVisibleSlots()` method declaration; add `CardSlot m_slots[kNotifMaxVisible]` + `int m_visibleCount{0}` + `double m_lastToastSoundTime{-1.0e30}` private members; add `IHUDBadgeNotifier* m_badge{nullptr}` private member; add fifth nullable constructor parameter `IHUDBadgeNotifier* badgeNotifier = nullptr`; add `std::unordered_map<UIElementHandle, int> m_dismissButtonToSlot` private member; add `NotificationLog m_log` private member (owned by `NotificationManager`; ring-buffer log of collapsed/evicted card data); add public `const NotificationLog& getLog() const` accessor (returns `m_log` for test assertions); add `bool soundFired{false}` field to `CriticalToast` struct (edge-detection for fire-on-display — prevents re-fire on subsequent `refreshVisibleSlots()` calls); add `bool soundFired{false}` field to `NormalToast` struct (same edge-detection purpose); add public `int findSlotIndexForDismissButton(UIElementHandle) const` accessor; add public `void dismissCard(int slotIndex)`; **add public `void onInput(const InputEvent& ev)`** (Priority 2 keyboard dismiss path — called by `UIManager::onEvent()` via `m_notificationManager->onInput(ev)` and callable directly from tests without a `UIManager` fixture member); **add public `int getFocusedCriticalIndex() const`** test accessor (exposes `m_focusedCriticalIndex` for the `NotifRail_DismissMiddleCritical_FocusFollowsShift` test); **add public `int getNormalQueueSize() const`** test accessor (returns `(int)m_normalQueue.size()` — used by `NotifRail_NormalCard_AutoDismiss_AfterFiveSeconds` assertion (b) to verify the Normal queue is empty after auto-dismiss; log-based checks are not valid here because auto-dismissed cards are never appended to the log)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| `src/ui/UIManager.cpp`                                                          | Expand `drawMinimapOverlay()` → `drawOverlays()` post-drawAll (step 10b) which also calls `m_notifications->drawOverlay()`; add minimap toggle button click routing in `onEvent()` that dispatches to `m_hud->onMinimapOverlayClick(...)` via the new HUD getters; add dismiss-button routing via `m_notifications->findSlotIndexForDismissButton(caller)` → `m_notifications->dismissCard(slotIndex)`; **add single-line keyboard delegation `m_notificationManager->onInput(ev)` in `onEvent()` — the old inline Priority 2 `dismissCriticalToast(queueEntry.handle)` call is removed and all keyboard dismiss routing now lives inside `NotificationManager::onInput()`**; implement `void UIManager::incrementNotificationBadge() { if (m_hud) m_hud->incrementNotificationBadge(); }` (null-safe — NotificationManager is constructed before HUD)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| `src/ui/UIManager.h`                                                            | Add `IHUDBadgeNotifier` as a base class: `class UIManager : public IHUDBadgeNotifier`; add `void incrementNotificationBadge() override;` to UIManager's public declarations; add `#include "src/interfaces/IHUDBadgeNotifier.h"` to required includes; add `NotificationManager& getNotificationManager()` public accessor; rename `drawMinimapOverlay()` → `drawOverlays()` declaration                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| `src/interfaces/IUIBackend.h`                                                   | Add `virtual void setElementTextColorA(UIElementHandle handle, int r, int g, int b, int a) = 0;` as method 23; add `virtual void drawNineSlice(int x, int y, int w, int h, int cornerRadius, int r, int g, int b, int a) = 0;` as method 24 (Deliverable 13); update method-count comment from 22 to 24 |
| `src/rendering/IrrlichtUIBackend.h`                                             | Add override declaration for `setElementTextColorA`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| `src/rendering/IrrlichtUIBackend.cpp`                                           | Add `setElementTextColorA` implementation — cast handle to `IGUIStaticText*`, call `setOverrideColor(SColor(a,r,g,b))` + `enableOverrideColor(true)` for static text elements. **Note**: `IGUIButton::setOverrideColor()` does NOT exist in vcpkg Irrlicht; for button text colour, use the global skin path `m_guiEnv->getSkin()->setColor(EGDC_BUTTON_TEXT, SColor(a,r,g,b))` — documented in `architecture/ui-ux/ui-manager.md §method 21/23`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| `tests/ui/MockUIBackend.h`                                                      | Add `MOCK_METHOD(void, setElementTextColorA, (UIElementHandle handle, int r, int g, int b, int a), (override));` as method 23; add `MOCK_METHOD` for `drawNineSlice` as method 24 (Deliverable 13); update method-count to 24 |
| `src/interfaces/ICitySimulation.h`                                              | Add `enum class MinimapOverlay { Map, Traffic, Service };` and `virtual void setMinimapOverlay(MinimapOverlay overlay) = 0;`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| `tests/ui/MockCitySimulation.h`                                                 | Add `MOCK_METHOD(void, setMinimapOverlay, (MinimapOverlay), (override));`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| `src/interfaces/IHUDBadgeNotifier.h`                                            | **NEW** — lightweight interface with single `virtual void incrementNotificationBadge() = 0` method; injected into `NotificationManager` as fifth nullable constructor parameter (see Deliverable 3 IHUDBadgeNotifier injection section)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| `tests/ui/MockHUDBadgeNotifier.h`                                               | **NEW** — `MOCK_METHOD(void, incrementNotificationBadge, (), (override));`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| `src/ui/hud_sprite_ids.h`                                                       | Add `kSpriteNotifCritical` (ID 324), `kSpriteNotifWarning` (ID 325), `kSpriteNotifInfo` (ID 326) — row 10 cols 4–6; add `kSpriteMinimapMapActive` … `kSpriteMinimapServiceHover` (9 constants — active/inactive/hover × 3, row 11 cols 0-8 → IDs 352–360)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| `src/ui/ui_constants.h`                                                         | Add Glacier Glass palette constants as **packed ARGB `uint32_t`** (0xAARRGGBB format): `kColorLabelPrimary`, `kColorLabelSecondary`, `kColorValueAmber`, `kColorValuePositive`, `kColorValueNegative`, `kColorRatingPill`, `kColorCyanActive`, `kColorBellInactive`, `kColorPendingRate`, `kActiveButtonBorderColor`, `kDeficitPulseArgb`, **`kBankruptcyCountdownArgb` (0xFFFF3830u — fully-opaque vivid red, Month-2 bankruptcy countdown per `game-over-flow.md`)**, `kNotifStripCritical`, `kNotifStripWarning`, `kNotifStripInfo`, `kNotifCardBg`, `kChromeStripColor`, **`kChromeRimColor = kChromeStripColor`** (alias for 2px top-edge chrome rim on minimap panel and notification cards), `kGlacierPanelBg` (SColor(66,4,12,28) for HUD panel backgrounds), `kMinimapToggleActiveBorder` (0xA500C8F0 — SColor(165,0,200,240), 2 px active border for the active minimap toggle button drawn via four `fillColoredRect` calls in `drawOverlays()` step 10b), `kDotMap`, `kDotTraffic`, `kDotService` (packed ARGB for minimap toggle dot indicators); add layout constants `kNotifCardLeft`, `kNotifCardRight`, `kNotifRailTop`, `kNotifCardGap`, `kNotifCardH`, `kNotifMaxVisible`, `kNotifCardIconLeft`, `kNotifCardTextLeft`, `kNotifCardTextWidth`, `kNotifCardDismissLeft`, `kMMBtnTop`, `kMMBtnBottom`, `kMMBtnHeight`, `kMMBtnWidth`, `kMMBtnGap`, `kMMBtnMapX`, `kMMBtnTrafficX`, `kMMBtnServiceX`, `kMMBtnRight`, **`kMinimapRight`** (= `kMMBtnRight` = 1920, alias for minimap right edge used by input-arbitration carve-out), **`kMinimapBottom`** (= 1080, full-height widget footprint bottom). Update existing constants: `kMinimapWidgetTop` → 838, `kMinimapWidgetTopOverlayActive` → **722** (legend panel top — carve-out expands when overlay active), **`kMinimapWidgetLeft` → 1720** (= `kMMBtnMapX` in V1) with inline comment `// kMinimapWidgetLeft = 1720 (= kMMBtnMapX) in V1`. Add `static_assert`s: `kMinimapWidgetTop == kMMBtnTop`, `kMinimapWidgetTopOverlayActive == 722`, `kMinimapWidgetLeft == kMMBtnMapX`, `kMinimapRight == kMMBtnRight`. **No Irrlicht include added.** |
| `tests/ui/HUDGlacierGlassTest.cpp`                                              | **NEW** — 10 unit tests for chrome strip, minimap toggle mutual exclusivity, toggle state after sequential clicks, sim_ dispatch for Map/Traffic/Service, hover sprite-swap, `HUDGlacierGlass_DrawNineSlice_Called_ForEachPanel` (Deliverable 13), `HUDGlacierGlass_HoverTransition_AlphaLerps` (Deliverable 15), and `HUDGlacierGlass_HoverExit_AlphaReturnsToZero` (Deliverable 15)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| `tests/ui/NotificationRailTest.cpp`                                             | **NEW** — 25 unit tests (23 NotifRail*\* + 2 HUDGlacierGlass_SetElementTextColorA*\*): right-rail card positioning, dismiss-shift (all 7 handles repositioned), Normal-first collapse (CRITICAL preserved), collapse-appends-to-log + badge increment, 11th-Normal eviction appends to log + LOG_WARN, severity strip fillColoredRect, card background (addStaticText-before-setElementBackground ordering), auto-pause contract preserved, layout-integrity invariant (fourth card bottom < kMMBtnTop − 16), **Normal card 5-second auto-dismiss timer (`NotifRail_NormalCard_AutoDismiss_AfterFiveSeconds`)**; audio rules: CRITICAL audio fires on display (not on enqueue); `NotifRail_PostCriticalDuringModalActive_UIToastNotFiredWhileModalActive` verifies audio is suppressed during `postCritical()` and fires exactly once on `setModalActive(false)` when the card becomes visible; CRITICAL 150 ms rate-limit burst test, Normal deferred-fire contract (queued-while-full, evicted-11th-never-fires, already-visible-no-refire via `soundFired` flag); setElementRect reposition lock (addStaticText.Times(0) + addButton.Times(0)); setElementTextColorA palette assertions; modal suppression hides all visible cards; findSlotIndexForDismissButton lookup contract; CRITICAL no-auto-resume on dismissCard; keyboard dismiss via m_focusedCriticalIndex; focus-follows-shift on middle-card dismiss; **`NotifRail_EnterDismissesFocusedNonSlot0Critical_RotatesAndDismissesCorrectCard`** (Tab+Enter on slot-1 CRITICAL dismisses slot-1 not slot-0; slot-0 card remains live; no-auto-resume holds). Note: `UIManagerDrawOrder_DrawOverlays_CalledAfterDrawAll` goes in `ui_manager_draw_order_test.cpp`, not here.                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| `tests/ui/notification_system_test.cpp`                                         | Update coordinate-based assertions from old top-center y:20/y:130 positions to the new right-rail positions (y:64, y:151, …); **also** update existing `EXPECT_CALL(audio_, playSound(UI_TOAST, SoundPriority::NORMAL, 1.0f))` assertions to `SoundPriority::HIGH` throughout this file (aligns with `source-pool.md` / `notification-system.md` — see Deliverable 3 audio priority correction bullet)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `tests/ui/ui_manager_draw_order_test.cpp`                                       | Update `DrawOrder_BackToFront_MatchesSpec` (replace kNotificationSentinel `setElementVisible` expectation with `fillColoredRect(1616, 64, 3, 80, 200, 40, 30, 204)`); in `DrawOrder_BackToFront_MatchesSpec`, after the `Minimap::drawOverlay()` step 3 `InSequence` assertion and before the notification strip assertion (step 5), add the step 4 chrome rim assertion: `EXPECT_CALL(backend_, fillColoredRect(kMMBtnMapX, kMapY, kMMBtnRight - kMMBtnMapX, 2, 230, 242, 252, 242)).Times(AtLeast(1));` — this locks that the chrome rim fires after `Minimap::drawOverlay()` and before the notification severity strip, matching the Deliverable 4 draw-order spec; update `UIManagerDrawOrder_ModalActive` (remove kNotificationSentinel expectation — modal hides all cards, `drawOverlay()` emits nothing, no ordering assertion; update comment to `// NotificationManager: modal hides all cards, drawOverlay emits nothing, no ordering assertion (Phase 11q13+)`; scrim and modal sentinel assertions remain); add new test `UIManagerDrawOrder_DrawOverlays_CalledAfterDrawAll`: post a CRITICAL toast, call `uiManager.drawOverlays()`, assert `fillColoredRect(1616, 64, 3, 80, 200, 40, 30, 204)` fires at least once                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| `CMakeLists.txt`                                                                | `target_sources(aitown_ui PRIVATE src/ui/NotificationLog.cpp)` (adds production source to the `aitown_ui` library target — required to avoid linker errors); `target_sources(ui_tests PRIVATE tests/ui/HUDGlacierGlassTest.cpp tests/ui/NotificationRailTest.cpp)` |
| `architecture/ui-ux/resolution-ui-scaling.md`                                   | VERIFY: section heading reads `## Visual Design — Glacier Glass + Silver Chrome: Canonical Colour Palette` (renamed from `## Visual Design — Glass City: Canonical Colour Palette`); panel-colour table rows list Glacier Glass `SColor(*, 4, 12, *)` values; Superseded Values section lists old Glass City palette. No edits expected — already updated in earlier phase.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| `architecture/ui-ux/notification-system.md`                                     | Update Transient toasts / Priority separation / Toast height / Visual Design sections to Glacier Glass right-rail model; queue contracts (auto-pause, priority separation, 10-deep Normal FIFO) unchanged; update §Phase 10 audio call sites to document unified fire-on-display contract (both CRITICAL and Normal fire from `refreshVisibleSlots()` on not-visible → visible transition, rate-limited 150 ms via `m_lastToastSoundTime`; `soundFired` edge-detect flag on both `CriticalToast` and `NormalToast`); **explicitly state that `soundFired` is set to `true` before the rate-limit check — a rate-limited card will not retry audio on subsequent `refreshVisibleSlots()` calls**; add `void dismissCard(int slotIndex)` to `§NotificationManager API` with contract: unified dismiss entry point for player click, Enter, and Delete; delegates to `dismissCriticalToast()` for CRITICAL slots, removes queue entry and calls `refreshVisibleSlots()` for Normal slots; add `int findSlotIndexForDismissButton(UIElementHandle h) const` to `§NotificationManager API` with contract: returns slot index [0..kNotifMaxVisible) whose `hDismiss` matches h, or -1 if not found |
| `architecture/ui-ux/hud-layout.md`                                              | Rename section heading `## Visual Design — Glass City` → `## Visual Design — Glacier Glass + Silver Chrome`; update HUD colour palette table to Glacier Glass values (`kGlacierPanelBg`, `kNotifCardBg`, `kActiveButtonBorderColor`, etc.); update notification area description from "top-center for transient toasts" to "permanent right-side rail (x:1616–1908, top:64)"; update toolbar icon state active border colour from teal `rgba(0,201,200,0.75)` to cyan `kActiveButtonBorderColor`; update button tile active wash from teal to `kActiveButtonWashColor` cyan; update §Text Colours in HUD to Glacier Glass palette; add §Minimap Overlay Toggles section (three-button row, fill colours, dot colours, chrome rim, fillColoredRect path); add §Phase 11q13 V1 appearance subset section; update §ui_toast call site note to reference unified fire-on-display contract (both CRITICAL and Normal fire on visibility transition in `refreshVisibleSlots()`, rate-limited 150 ms)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `architecture/audio-architecture/production-briefs/wav-sfx-production-brief.md` | `ui_toast.wav` priority updated NORMAL → HIGH; `ui_click.wav` priority updated NORMAL → HIGH; `ui_menu_open.wav` priority updated NORMAL → HIGH (if listed); `ui_menu_close.wav` priority updated NORMAL → HIGH (if listed); trigger description for `ui_toast.wav` updated to reflect Phase 11q13 unified fire-on-display contract (both CRITICAL and Normal fire on visibility transition in `refreshVisibleSlots()`)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| `architecture/asset-standards/2d-texture-standards.md`                          | Add row 10 cols 4–6 assignments (IDs 324–326) for `kSpriteNotifCritical` / `kSpriteNotifWarning` / `kSpriteNotifInfo` to the Cell Assignment Table (notification card severity icons); add row 11 cols 0–8 assignments (IDs 352–360) for minimap overlay toggle active/inactive/hover sprites (Map, Traffic, Service); rename section heading from 'UI Sprite Sheet Art Style — Glass City' to 'UI Sprite Sheet Art Style — Glacier Glass + Silver Chrome'; update Panel Colour Context: authors must verify icons against `kGlacierPanelBg` = SColor(66,4,12,28) = approx rgba(4,12,28,0.26). At this low opacity the city scene is visible through the panel; verify icon contrast over a mid-luminance scene background, not just a dark swatch.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| `architecture/ui-ux/minimap.md`                                                 | Update minimap render-area bounds to y:864–1064, supersede old 2-icon Svc/Tfc toggle row with the new 3-button Map/Traffic/Service row at y:838–860, update kMinimapWidgetTop to 838, update label strip (y:822–838) and legend panel (y:722–822) coordinates                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| `architecture/ui-ux/ui-manager.md`                                              | Rename `drawMinimapOverlay()` → `drawOverlays()` in class declaration and internal-view pseudocode; update docstring to include notification severity strips; update `kMinimapWidgetTop = 838` and `kMinimapWidgetTopOverlayActive = 722` in §Toolbar Carve-Out Constants (overlay-active carve-out expands to include legend panel at y:722); **update `kMinimapWidgetLeft` to 1720** (= `kMMBtnMapX`) as part of this phase with inline comment `// kMinimapWidgetLeft = 1720 (= kMMBtnMapX) in V1` — ensures the spec stays in sync with the `src/ui/ui_constants.h` implementation update in Deliverable 7; Update §Panel construction order item 1 constructor call from `new NotificationManager(m_backend, m_sim, m_clock, m_audio)` to `new NotificationManager(m_backend, m_sim, m_clock, m_audio, this)` and update the label from 'Phase 10 constructor call' to 'Phase 11q13 constructor call' to match the canonical 5-arg signature in `notification-system.md §NotificationManager Constructor`; **update the class declaration from `class UIManager {` to `class UIManager : public IHUDBadgeNotifier {`**; add `void incrementNotificationBadge() override;` to the spec's method list for `UIManager`; add `#include "IHUDBadgeNotifier.h"` to the required includes listed in the spec                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| `architecture/ui-ux/input-arbitration.md`                                       | Update Priority 3 Minimap carve-out exception: toggle row y:838–860 (matches `kMinimapWidgetTop = 838`); `kMinimapWidgetTopOverlayActive = 722` — carve-out expands to legend panel top (y:722) when overlay is active                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `src/rendering/IrrlichtRenderer.cpp`                                            | Update call site `m_uiManager->drawMinimapOverlay()` → `m_uiManager->drawOverlays()` in `IrrlichtRenderer::drawScene()` (one-line rename to match the renamed UIManager method)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| `architecture/graphics-architecture/irrlicht-device-lifecycle.md`               | Replace `uiManager->drawMinimapOverlay()` with `uiManager->drawOverlays()` in the Per-Frame Loop code block. Update the comment to note that `drawOverlays()` now handles minimap overlay draws, notification severity strips, and active-button washes (all post-`drawAll()` filled rects).|
| `architecture/testing/testability-architecture.md`                              | Update §NotificationManager testability to document `dismissCard(int slotIndex)` as the new production dispatch entry point for player-driven toast dismissal (click, Enter, Delete) and reclassify `dismissCriticalToast(UIElementHandle)` as an internal delegation target called by `dismissCard()` for CRITICAL cards (preserves the no-auto-resume contract). Update the named test reference `CriticalToast_OnLastDismiss_NoAutoResume` to note its Phase 11q13 equivalent: `NotifRail_DismissCard_CriticalDelegatesToDismissCriticalToast_NoAutoResume` (same invariant: dismissing the last CRITICAL card never calls `setPaused(false)`). Add `NotificationManager` constructor order `(IUIBackend*, ICitySimulation*, IClock*, IAudioSystem*)` as a documented legacy exception to the §Canonical UI class constructor parameter order rule. Add `NotificationLog` public interface spec: `append(title, msg, timestamp, bool isCritical)`, `getEntryCount()`, and `getLog()` accessor on `NotificationManager`. **Update the inline `IUIBackend` class code block**: append `virtual void setElementTextColorA(UIElementHandle handle, int r, int g, int b, int a) = 0;` as method 23 in the block. **Update the `MockUIBackend` method-count sentence** from "22 `MOCK_METHOD` entries" to "23 `MOCK_METHOD` entries". **Update the `NotificationManager` constructor signature** in §NotificationManager testability from the 4-parameter form `NotificationManager(IUIBackend*, ICitySimulation*, IClock*, IAudioSystem*)` to the 5-parameter form `NotificationManager(IUIBackend*, ICitySimulation*, IClock*, IAudioSystem*, IHUDBadgeNotifier*)`, and update the "CRITICAL — Constructor parameter types are fixed" prose to reflect Phase 11q13 as the new canonical signature (the fifth nullable `IHUDBadgeNotifier*` parameter is the Phase 11q13 addition; all existing 4-arg call sites pass `nullptr` implicitly via the default argument). |
| `src/ui/NotificationLog.h`                                                      | **NEW** — public notification log interface: `void append(const std::string& title, const std::string& msg, const std::string& timestamp, bool isCritical)`, `int getEntryCount() const` |
| `src/ui/NotificationLog.cpp`                                                    | **NEW** — `NotificationLog` implementation |
| `tools/validate_assets.py`                                                      | Add `check_33()` function verifying presence and non-emptiness of row 10 cols 4–6 (IDs 324–326) and row 11 cols 0–8 (IDs 352–360) in `hud_sprites_ui.png` |
| `.github/workflows/_validate-assets.yml`                                        | Append `check_33` to the `for check in` list in the 'Verify required checks present' step, and update the header comment from `check_32` to `check_33`. |
| `architecture/ci-cd/github-actions-workflow.md`                                 | Document **check_33**: (a) append a Phase 11q13 phasing-summary bullet after the Phase 11q12 bullet, describing check_33 as the sprite pixel-presence gate for notification severity icon IDs 324–326 (row 10 cols 4–6) and minimap overlay toggle icon IDs 352–360 (row 11 cols 0–8) in `assets/textures/ui/hud_sprites_ui.png`; (b) include the guard step YAML block for check_33 in that bullet, consistent with checks 25–32: `- name: Guard — check_33 present in validate_assets.py` / `  run: grep -q "check_33" tools/validate_assets.py \|\| { echo "FAIL: check_33 not found in validate_assets.py — Phase 11q13 sprite presence gate missing"; exit 1; }`. The phasing bullet must state that check_33 hard-fails the CI asset-validation job if any of the listed sprite cells are absent or empty. |
| `assets/textures/ui/hud_sprites_ui.png`                                         | Add row 10 cols 4–6 notification severity icon artwork (IDs 324–326) and row 11 cols 0–8 minimap toggle sprites (IDs 352–360) |
| `assets/fonts/src/BarlowCondensed-*.ttf`                                        | Add source font files from Google Fonts (OFL license) if not already present |
| `assets/fonts/src/ShareTechMono-Regular.ttf`                                    | Add source font file from Google Fonts (OFL license) if not already present |
| `LICENSE.md`                                                                    | Add Barlow Condensed + Share Tech Mono entries under SIL OFL 1.1 (Deliverable 12) |
| `assets/fonts/hud_font_720.xml`                                                 | Regenerated from Barlow Condensed via `tools/generate_bitmap_fonts.py` |
| `assets/fonts/hud_font_1080.xml`                                                | Regenerated from Barlow Condensed |
| `assets/fonts/hud_font_1440.xml`                                                | Regenerated from Barlow Condensed |
| `assets/fonts/hud_mono_font_720.xml`                                            | Regenerated from Share Tech Mono via `tools/generate_bitmap_fonts.py` |
| `assets/fonts/hud_mono_font_1080.xml`                                           | Regenerated from Share Tech Mono |
| `assets/fonts/hud_mono_font_1440.xml`                                           | Regenerated from Share Tech Mono |
| `assets/textures/ui/panel_corners_10px.png`                                     | **NEW** — 128×128 RGBA PNG with 4 pre-flipped quarter-circle corners in 2×2 grid (10 px radius, anti-aliased alpha) for 9-slice panel rendering (Deliverable 13) |
| `src/interfaces/IUIBackend.h`                                                   | Add `drawNineSlice` as method 24; update method-count comment 23→24 (Deliverable 13) |
| `src/rendering/IrrlichtUIBackend.cpp`                                           | Add `drawNineSlice` implementation — 9 `draw2DImage` calls (4 corners, 4 edges, 1 centre fill) (Deliverable 13); add `setElementTextColorA` implementation (Deliverable 6) |
| `tests/ui/MockUIBackend.h`                                                      | Add `MOCK_METHOD` for `drawNineSlice` (method 24); update method-count comment 23→24 (Deliverable 13) |
| `tools/generate_bitmap_fonts.py`                                                | Add `--letter-spacing` parameter that inflates glyph `rect` width in generated `.xml` font files (Deliverable 14) |

---

### Lines of Code Estimate

~120 lines changed in `HUD.cpp` (colour constants + chrome strip + minimap toggle wiring +
getters);
~80 lines changed in `NotificationManager.cpp` (right-rail reposition + CardSlot + card
anatomy + drawOverlay);
~40 lines changed in `UIManager.cpp` (drawOverlays expansion + onEvent routing);
~30 lines new in `HUD.h` / `NotificationManager.h` (new members + CardSlot struct);
~50 lines new in `hud_sprite_ids.h` + `ui_constants.h` (constants);
~170 lines new in `HUDGlacierGlassTest.cpp` (10 tests: 5 original + Test 6 sim_ dispatch + Test 7 hover + Test 8 drawNineSlice + Test 9 hover-alpha-lerp + Test 10 hover-exit);
~460 lines new in `NotificationRailTest.cpp` (23 tests);
~10 lines changed in `notification_system_test.cpp` (coordinate updates);
~15 lines changed in `resolution-ui-scaling.md`;
~40 lines changed in `notification-system.md`.

---

### Exit Criteria

- [ ] `HUD` constructs without crash; `m_chromeStrip`, `m_btnMinimapMap`,
      `m_btnMinimapTraffic`, `m_btnMinimapService` are all valid handles after construction.
- [ ] Clicking a minimap toggle button deactivates the other two and activates the clicked
      one (verified by the 5 `HUDGlacierGlass_*` unit tests passing).
- [ ] All 10 `HUDGlacierGlassTest` tests pass under `ctest --test-dir build -LE "integration|requires-opengl"` (includes Test 6 sim_ dispatch, Test 7 hover sprite-swap, Test 8 `HUDGlacierGlass_DrawNineSlice_Called_ForEachPanel` from Deliverable 13, Test 9 `HUDGlacierGlass_HoverTransition_AlphaLerps` and Test 10 `HUDGlacierGlass_HoverExit_AlphaReturnsToZero` from Deliverable 15).
- [ ] All 25 `NotificationRailTest` tests pass under `ctest --test-dir build -LE "integration|requires-opengl"`.
- [ ] Third notification rail card bottom edge (y = 318) is < kMMBtnTop − 16 (= 822); cards never overlap the minimap toggle row (verified by `NotifRail_ThirdCard_BottomAboveMinimapToggles` test).
- [ ] Notification cards appear at `x:1616–1908, top:64` when posted (no cards appear in
      the old top-center region). Verify via `MockUIBackend` element registry inspection
      in `NotificationRailTest`.
- [ ] Existing auto-pause / priority-separation / 10-deep FIFO / modal-suppression tests
      in `notification_system_test.cpp` continue to pass after coordinate updates.
- [ ] The chrome bottom divider strip element exists at virtual `x=0, y=53, w=1920, h=3`
      with `setElementBackground(h, 230, 242, 252, 242)` (verified by unit test or element
      registry assertion).
- [ ] `setElementBackground` values in `src/ui/Minimap.cpp` match the Glacier Glass +
      Silver Chrome values (inspect source — no purely visual automated test required).
- [ ] `grep -r "UI_TOAST, SoundPriority::NORMAL"` in `src/` and `tests/` returns zero
      matches after implementation (priority corrected to `SoundPriority::HIGH` per
      `source-pool.md` / `notification-system.md`).
- [ ] `grep -rn "UI_CLICK, SoundPriority::NORMAL"` in `src/` and `tests/` returns zero
      matches after implementation (priority corrected to `SoundPriority::HIGH` per
      `source-pool.md §Source Allocation` "UI sounds: HIGH" rule).
- [ ] `grep -rn "UI_MENU_OPEN, SoundPriority::NORMAL" src/ tests/` returns zero matches
      after implementation (priority corrected to `SoundPriority::HIGH` per
      `source-pool.md §Source Allocation` "UI sounds: HIGH" rule).
- [ ] `grep -rn "UI_MENU_CLOSE, SoundPriority::NORMAL" src/ tests/` returns zero matches
      after implementation (priority corrected to `SoundPriority::HIGH` per
      `source-pool.md §Source Allocation` "UI sounds: HIGH" rule).
- [ ] `kDeficitPulseArgb == 0x80FF7870u` (grep or compile-time `static_assert`).
- [ ] `static_assert(kBankruptcyCountdownArgb != kDeficitPulseArgb, "bankruptcy countdown and standard deficit pulse must be distinct ARGB values")` — compile-time assertion confirming the bankruptcy countdown indicator (separate ARGB constant per `architecture/game-design/game-over-flow.md`) stays visually distinct from `kDeficitPulseArgb`.
- [ ] `grep -rn '0xFFFF3830\|0xFF3830' src/ui/HUD.cpp` returns zero matches — all Month-2 bankruptcy countdown indicator call sites use the named constant `kBankruptcyCountdownArgb`.
- [ ] `kActiveButtonWashColor == 0x2900C8FFu` and `kActiveButtonBorderColor == 0xC200C8FFu` are both declared in `ui_constants.h` and consumed in `HUD.cpp` for the active speed button and active toolbar button (wash fill + four-sided 2 px border per button). Grep confirms no `rgba(0,200,255` or `rgba(0,201,200` inline literals for active button styling remain in source.
- [ ] **Anti-literal gate (named-constant decomposition required)**: grep for literal `setElementBackground(` / `fillColoredRect(` calls with raw numeric arguments in `src/ui/HUD.cpp` and `src/ui/NotificationManager.cpp` confirms zero occurrences — all use named constant decomposition (from `kChromeStripColor`, `kNotifCardBg`, `kNotifStripCritical`, `kNotifStripWarning`, `kNotifStripInfo`, `kActiveButtonWashColor`, `kActiveButtonBorderColor`, etc.) via `(v>>16)&0xFF, (v>>8)&0xFF, v&0xFF, v>>24` or an equivalent helper.
- [ ] `ui_constants.h` contains no `#include <irr*>` directive and no `irr::video::SColor`
      type references (grep confirms).
- [ ] `grep -c "MOCK_METHOD" tests/ui/MockUIBackend.h` returns **24** (covers all
      `IUIBackend` virtual methods including `setElementTextColorA` and `drawNineSlice`).
- [ ] `hud_sprite_ids.h` `static_assert(kSpriteNotifCritical == 324, ...)`
- [ ] `hud_sprite_ids.h` `static_assert(kSpriteNotifWarning == 325, ...)`
- [ ] `hud_sprite_ids.h` `static_assert(kSpriteNotifInfo == 326, ...)`
- [ ] `hud_sprite_ids.h` `static_assert(kSpriteMinimapMapActive == 352, ...)`
- [ ] `hud_sprite_ids.h` `static_assert(kSpriteMinimapMapInactive == 353, ...)`
- [ ] `hud_sprite_ids.h` `static_assert(kSpriteMinimapMapHover == 354, ...)`
- [ ] `hud_sprite_ids.h` `static_assert(kSpriteMinimapTrafficActive == 355, ...)`
- [ ] `hud_sprite_ids.h` `static_assert(kSpriteMinimapTrafficInactive == 356, ...)`
- [ ] `hud_sprite_ids.h` `static_assert(kSpriteMinimapTrafficHover == 357, ...)`
- [ ] `hud_sprite_ids.h` `static_assert(kSpriteMinimapServiceActive == 358, ...)`
- [ ] `hud_sprite_ids.h` `static_assert(kSpriteMinimapServiceInactive == 359, ...)`
- [ ] `hud_sprite_ids.h` `static_assert(kSpriteMinimapServiceHover == 360, ...)`
- [ ] `kMinimapWidgetTop == 838` (grep or static_assert confirms)
- [ ] `kMinimapWidgetTopOverlayActive == 722` (grep or static_assert confirms — carve-out expands to legend panel top when overlay is active)
- [ ] `kMinimapWidgetLeft == 1720` (= `kMMBtnMapX`; grep or static_assert confirms; V1 value)
- [ ] `Minimap render area top y = 864` — `static_assert(kMapY == 864)` or grep `src/ui/Minimap.h` confirms `kMapY == 864`.
- [ ] `python3 tools/cognitive_complexity.py --only-violations src/ui/NotificationManager.cpp` returns zero violations (all functions ≤ CC 25).
- [ ] `assets/fonts/hud_font_1080.xml` and `assets/fonts/hud_mono_font_1080.xml` are present
      and their embedded glyph metadata confirms the correct typeface name
      (`grep -i "BarlowCondensed" assets/fonts/hud_font_1080.xml` returns at least one match;
      `grep -i "ShareTechMono" assets/fonts/hud_mono_font_1080.xml` returns at least one match).
- [ ] `tools/validate_assets.py check_33` passes: row 10 cols 4–6 (IDs 324–326) and row 11
      cols 0–8 (IDs 352–360) are present and non-empty in `assets/textures/ui/hud_sprites_ui.png`.
- [ ] Build succeeds on Linux (`make build`).
- [ ] All existing tests pass (`make test`).
- [ ] **Rounded corners (Deliverable 13)**: `assets/textures/ui/panel_corners_10px.png` exists
      and is a 128×128 RGBA PNG (2×2 grid of pre-flipped corners). `drawNineSlice` method exists in `IUIBackend.h` (method 24).
      `HUDGlacierGlass_DrawNineSlice_Called_ForEachPanel` test passes.
- [ ] **Letter spacing (Deliverable 14)**: `tools/generate_bitmap_fonts.py --help` shows
      `--letter-spacing` parameter. All 6 `.xml` font files have glyph widths wider than
      the default (diff confirms inflated `rect` values).
- [ ] **Hover transitions (Deliverable 15)**: `kHoverTransitionDuration` is declared in
      `ui_constants.h`. `HUDGlacierGlass_HoverTransition_AlphaLerps` and
      `HUDGlacierGlass_HoverExit_AlphaReturnsToZero` tests pass.
- [ ] `grep -c "MOCK_METHOD" tests/ui/MockUIBackend.h` returns **24** (includes
      `drawNineSlice` as method 24).
