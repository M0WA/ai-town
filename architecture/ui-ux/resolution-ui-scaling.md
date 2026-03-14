# Resolution & UI Scaling

- **Minimum supported resolution**: 1280×720
- **Target resolution**: 1920×1080
- All UI elements authored in virtual 1920×1080 coordinate space; scaled at runtime via `UIScaler` (input) and `IrrlichtUIBackend` (element creation) before passing to `IGUIEnvironment`
- **Scaling mode**: Uniform scale with letterbox/pillarbox (preserve aspect ratio); black bars on sides or top/bottom as needed
- **Mouse input un-projection**: Mouse coordinates from `SEvent` must be transformed back to virtual space accounting for letterbox/pillarbox offset:

  ```text
  virtual_x = (actual_x - letterbox_offset_x) × (1920.0 / viewport_width)
  virtual_y = (actual_y - letterbox_offset_y) × (1080.0 / viewport_height)
  ```

  where `letterbox_offset_x/y` are the pixel offsets of the active viewport within the window (non-zero when black bars are present). **Omitting the offset displaces all UI clicks when the window is not native 1920×1080.** `UIScaler` must expose `getViewportRect()` returning the active viewport dimensions and offset, and all input handlers must call it before un-projecting.

  **`VirtualPoint` is a nested type inside `UIScaler`** (not at namespace scope):

  ```cpp
  class UIScaler {
  public:
      struct VirtualPoint { int x; int y; };
      VirtualPoint unproject(int physicalX, int physicalY) const;
      Rect getViewportRect() const;
      // ... other methods
  };
  ```

  Callers use `UIScaler::VirtualPoint`. Placing `VirtualPoint` at namespace scope creates an ODR violation when both `UIScaler.h` and consumer translation units define `VirtualPoint` independently.

  Canonical C++ method signatures for `UIScaler`:

  ```cpp
  // Returns the active viewport rectangle in physical pixels (accounts for letterbox).
  // Call before un-projecting any input coordinate.
  Rect getViewportRect() const;

  // Transforms a physical-pixel input coordinate to virtual 1920×1080 space.
  // Must be applied exactly once at the entry point of the input chain.
  // Returns a UIScaler::VirtualPoint nested struct { int x; int y; }.
  // OUTPUT CLAMPING: if the input screen coordinates fall outside the active viewport
  // (e.g. mouse moved into a letterbox/pillarbox black bar, or outside the window),
  // the returned VirtualPoint is clamped to [0, virtualW-1] × [0, virtualH-1] — i.e.
  // x is clamped to [0, 1919] and y is clamped to [0, 1079].
  // The maximum valid virtual X is 1919 (not 1920); the maximum valid virtual Y is
  // 1079 (not 1080). Coordinates equal to virtualW or virtualH are out-of-bounds and
  // MUST NOT be returned by unproject() — they would produce an off-by-one index into
  // texture atlases and layout tables sized for [0, virtualW-1] × [0, virtualH-1].
  // unproject() NEVER returns negative virtual coordinates or coordinates at or above
  // the virtual resolution. Callers must not add their own clamp on top of this.
  struct VirtualPoint { int x; int y; };  // nested inside UIScaler — callers use UIScaler::VirtualPoint
  VirtualPoint unproject(int physicalX, int physicalY) const;
  ```

  **Note**: These signatures are locked at Phase 0 (stub header only — no logic). Phase 1 fills in the full `UIScaler` implementation and delivers all 5 named `UIScaler` unit tests. Phase 3 VERIFYs the Phase 1 deliverables are present and adds one compile-only stub test case — it does NOT add or change implementation logic. Teams implementing `CameraController::OnInputEvent()` and `UIManager` input dispatch MUST use exactly these signatures to avoid integration breakage.

  **Phase 1 exit criterion for UIScaler clamping**: The Phase 1 implementation MUST include unit tests verifying BOTH clamp boundaries:
  - **Lower-bound clamp test**: a physical coordinate that maps below virtual (0, 0) (e.g. mouse in the letterbox black bar above the viewport) must return `VirtualPoint{0, 0}`, never a negative value.
  - **Upper-bound clamp test**: a physical coordinate that maps to or beyond virtual (1920, 1080) (e.g. mouse in the pillarbox or below the viewport) must return `VirtualPoint{1919, 1079}`, never a value of 1920 or 1080. This test is mandatory alongside the lower-bound test — an implementation that clamps to `[0, virtualW]` (inclusive 1920) rather than `[0, virtualW-1]` (inclusive 1919) will pass the lower-bound test but silently produce out-of-bounds atlas indices on the high side.

## UIScaler Constructor

The canonical constructor signature for `UIScaler` is:

```cpp
UIScaler(int virtualW, int virtualH, int viewportW, int viewportH, int offsetX, int offsetY);
```

- All six parameters are captured at construction time. `UIScaler` MUST NOT read from a live `IVideoDriver` — doing so would couple the object to a display connection and make it impossible to instantiate in headless unit tests.
- **Viewport resize**: After window resize, `UIScaler`'s cached `m_viewportW` and `m_viewportH` become stale. `UIScaler` exposes `void setViewportSize(int viewportW, int viewportH)` to update these cached dimensions. The main loop MUST call `uiScaler.setViewportSize(uiBackend.getScreenWidth(), uiBackend.getScreenHeight())` each frame (before event processing) so that `unproject()` always uses the current physical viewport size. Without this update, mouse coordinate unprojection after resize produces incorrect virtual coordinates, making buttons unclickable despite being visually positioned correctly (IrrlichtUIBackend dynamically queries the driver's screen size for element positioning, but UIScaler would still use stale construction-time dimensions for input unprojection).
- This constructor is the testability seam used by `tests/ui/ui_scaler_test.cpp` (see `architecture/testing/testability-architecture.md` UIScaler tests section). Tests construct `UIScaler(1920, 1080, 1280, 720, 0, 90)` directly to validate coordinate projection and letterbox offset math without a display.

## Rect Type Ownership

`getViewportRect()` returns a `Rect` value. The `Rect` struct is defined **exclusively** in `src/ui/IUIBackend.h`:

```cpp
struct Rect { int x{0}, y{0}, w{0}, h{0}; };
```

- `UIScaler.h` MUST `#include "src/ui/IUIBackend.h"` to obtain the `Rect` type for `getViewportRect()`.
- No other header may define or forward-declare `struct Rect`. Duplicate definitions across translation units risk ODR (One Definition Rule) violations and linker errors that are difficult to diagnose.
- **Acknowledged transitive include coupling**: Including `IUIBackend.h` in `UIScaler.h` creates a transitive dependency: any translation unit that includes `UIScaler.h` (directly or through `CameraController.h` or the platform adapter) will also see all 17 `IUIBackend` virtual method declarations. This coupling is **intentional and accepted** — `Rect` is defined in `IUIBackend.h` by design (co-location with the interface that returns it), and `UIScaler.h` is a UI-domain header legitimately coupled to the UI backend. An alternative — extracting `Rect` into a minimal `src/ui/rect.h` — was evaluated and rejected to avoid header proliferation for a single 4-field struct. Test targets (`ui_tests`) that include `UIScaler.h` must link `aitown_ui` (which provides `IUIBackend` via the `aitown_render`→`IrrlichtUIBackend` linkage chain) — this is already satisfied by the existing `target_link_libraries(ui_tests ...)` setup. If this transitive dependency ever becomes a build-time bottleneck (e.g., slow incremental compilation), the `rect.h` extraction is the clean upgrade path.

- **Mouse un-projection ownership**: `UIScaler::unproject()` MUST be applied exactly once, at the entry point of the input chain. The platform event receiver (in `src/platform/`) applies the transform before handing the resulting `InputEvent` to `UIManager::onEvent()`. Panels receive virtual-space coordinates and must not call `UIScaler` again. Panel unit tests inject pre-projected virtual-space coordinates directly, bypassing the platform receiver entirely.

## IrrlichtUIBackend Coordinate Scaling

`IrrlichtUIBackend` is the coordinate translation layer between the virtual 1920×1080 design space used by all panels and the physical screen pixel space used by Irrlicht's `IGUIEnvironment`. Two scaling transforms are applied:

- **Element creation** (`addStaticText`, `addButton`): virtual coordinates passed by panels are scaled to physical screen pixels before calling `m_guiEnv->addStaticText()` / `m_guiEnv->addButton()`. The scaling factors are `screenW / virtualW` and `screenH / virtualH`, computed from the driver's screen dimensions and the fixed virtual canvas size (1920×1080). The original virtual coordinates are stored alongside the Irrlicht element pointer in an `ElementInfo` struct for later retrieval.
- **Element rect query** (`getElementRect`): returns the **stored virtual rect** captured at element creation time. This avoids a physical→virtual round-trip conversion that would produce incorrect coordinates after a window resize (the Irrlicht element's physical position reflects the screen size at creation time, not the current screen size).

### Stored Virtual Rect Pattern

`IrrlichtUIBackend` tracks each element using an `ElementInfo` struct:

```cpp
struct ElementInfo {
    irr::gui::IGUIElement* element{nullptr};
    Rect virtualRect{};  // captured at addStaticText/addButton time
};
std::unordered_map<UIElementHandle, ElementInfo> m_elementMap;
```

`getElementRect()` returns `it->second.virtualRect` directly — no coordinate conversion needed at query time. This eliminates the resize-dependent round-trip bug where dividing stale physical positions by the new screen size produces wrong virtual coordinates.

### Viewport Resize Handling (`handleViewportResize()`)

`IrrlichtUIBackend` exposes a concrete method (not on the `IUIBackend` interface):

```cpp
void handleViewportResize();
```

Called once per frame from the main loop (after `uiScaler.setViewportSize()` and before event processing). Detects screen size changes by comparing the current driver screen dimensions against cached `m_lastScreenW` / `m_lastScreenH`. When a resize is detected, iterates all elements in `m_elementMap` and repositions each Irrlicht `IGUIElement` via `setRelativePosition()` using the stored virtual rect scaled to the new physical screen dimensions. This ensures that:

1. Irrlicht elements visually reposition to match the virtual layout at the new resolution.
2. `getElementRect()` continues to return the correct (unchanged) virtual coordinates.
3. Hit tests remain correct because both mouse coordinates (via `UIScaler::unproject()` with updated viewport) and element rects (stored virtual) use the same virtual coordinate space.

Without `handleViewportResize()`, elements remain at their old physical pixel positions after a resize. `getElementRect()` would still return correct virtual coordinates (stored, not computed), but the elements would be visually mispositioned — appearing at the wrong physical location on screen.

### Coordinate Pipeline Summary

The complete resize-safe coordinate pipeline:

1. **Per frame**: `uiScaler.setViewportSize(screenW, screenH)` updates mouse unprojection.
2. **Per frame**: `uiBackend.handleViewportResize()` repositions Irrlicht elements if screen size changed.
3. **On click**: `UIScaler::unproject(physX, physY)` → virtual mouse coordinates.
4. **On hit test**: `getElementRect(handle)` → stored virtual rect (no conversion).
5. **Comparison**: virtual mouse vs virtual rect — always in the same coordinate space.

This ensures that:

1. Panels work exclusively in virtual 1920×1080 space — no panel code touches physical coordinates.
2. Hit tests in panel `onEvent()` handlers compare virtual mouse coordinates (from `UIScaler::unproject()`) against virtual element rects (from `getElementRect()`), so the coordinate spaces always match regardless of resize.
3. Elements are positioned correctly on screen at any resolution — a button at virtual (780, 320) is centered on a 1280×720 window (physical (520, 213)) rather than placed at physical pixel 780.

Without this scaling, elements are positioned at virtual-space values interpreted as physical pixels (e.g., a button intended for virtual x=780 appears at physical pixel 780 on a 1280-wide window, offset from center). Mouse un-projection converts physical clicks to virtual coordinates that do not match the misplaced element positions, making buttons unclickable.

- No hardcoded pixel offsets anywhere in UI code

## Bitmap Font Physical Size

Irrlicht bitmap fonts render at their **physical pixel size** — they do not scale with the virtual
coordinate system. A font generated at 18 px physical renders 18-pixel-tall glyphs on screen
regardless of virtual coordinate transformations. This means the font asset must be authored for
the **physical window size** (minimum 1280×720), not for the virtual 1920×1080 canvas.

**Correct font sizes**:

- `assets/fonts/hud_font.xml` — DejaVu Sans, **18 px physical**
- `assets/fonts/hud_mono_font.xml` — DejaVu Sans Mono, **18 px physical**

Both fonts are generated by `tools/generate_hud_font.py`. Re-run that script to regenerate
`hud_font.xml + hud_font_0.png` and `hud_mono_font.xml + hud_mono_font_0.png` whenever the
font size or typeface changes.

**Why 11 px was wrong**: The previous 11 px font was unreadably small at any normal viewing
distance. The spec rationale ("11 px fits 5-char labels in 37 px buttons") optimised for
toolbar button width rather than legibility — a false trade-off, since toolbar buttons show
icons not text labels. 18 px is the minimum for comfortable HUD readability.

**Rule**: When choosing a bitmap font size, prioritise legibility at 1280×720 physical.
Toolbar buttons display icons; text labels appear in panels, toasts, and the HUD bar where
space is not as constrained.

## Bitmap Font Baseline Alignment

Irrlicht bitmap fonts are NOT auto-baseline-aligned — all glyph rects are drawn with their
**top edge at the same screen Y**. For mixed uppercase/lowercase text to appear on a shared
visual baseline, every glyph rect in the atlas PNG must be authored with a consistent
baseline position within each cell.

**Required layout** (enforced by `tools/generate_hud_font.py`):

- Cell height = `ascent + descent` from PIL `getmetrics()` (22 px for 18 px DejaVu Sans:
  ascent = 17, descent = 5).
- Every glyph rect has the same height (`cell_h = 22`).
- Glyphs are drawn at `(draw_x, row_y)` in the PNG — PIL places the tallest ascender at
  `row_y + 3` and the visual baseline at `row_y + 17`.
- When Irrlicht renders all rects with their tops at screen Y, every character's visual
  baseline falls at `screen_Y + 17` (= `screen_Y + ascent`). ✓

**Pitfall — top-aligned generation**: If glyphs are cropped to their tight bounding box and
packed flush to the cell top, uppercase and lowercase characters both start at screen Y but
have different visual heights, making lowercase letters appear to float at the top of the
text line instead of sitting on the shared baseline. This was the root cause of the
"irregular / vertically misaligned" text in the old 11 px font.

## Typography

- **Minimum body font size**: 14 px virtual (1920×1080 space). At 1280×720 (scale factor ≈ 0.667), this renders to approximately 9 px physical — the minimum for compact panels. Prefer **16 px virtual** for all interactive labels.
- **Minimum label font size in compact panels** (Query/Inspector Panel, Tax Rate Panel, Notification toasts): **13 px virtual**, never smaller. These panels display numeric data that must be legible under time pressure.
- **Hard physical floor**: No UI text may render below **11 px physical pixels** at the minimum supported resolution (1280×720). `UIScaler` must clamp text scale so that font sizes never fall below this floor, even if a window is resized below 1280×720.
- **Typeface requirements**: Numeric readouts (tax rates, treasury balance, population counts, percentages) must use a **monospace typeface** (prevents layout shift as digits change). Labels (zone types, panel titles, button text) must use a **sans-serif typeface** for legibility at small sizes.
- **These rules apply to all UI elements**: HUD resource bar, demand pressure bars (must supplement color with zone-type letters R/C/I), toolbar tooltip text, Query Panel fields, Tax Rate Panel rows, notification toasts, modal dialog body text, and minimap legend labels.

## Visual Design — Glass City: Canonical Colour Palette

The following values are the **locked canonical palette** for all UI panels, HUD elements,
modals, and overlays in AI Town. Every file in `architecture/ui-ux/` that specifies colours
MUST reference or conform to these values. Do not introduce other colour values for the
categories below without updating this table first.

### Panel Backgrounds

| Element | Value | Usage |
|---|---|---|
| Resource/budget bar | `rgba(13, 27, 42, 0.88)` | Full-width top bar; 0px corner radius |
| All other panels | `rgba(13, 27, 42, 0.78–0.88)` | Toolbar, sub-panels, inspector, minimap bg, modals |

Panel backgrounds use deep navy — not milky white. The alpha range `0.78–0.88` allows
individual panels to tune opacity; the resource bar is fixed at the upper end (`0.88`) for
legibility against the sky.

### Corner Radius

- Inner panel edges: **8 px** corner radius (virtual 1920×1080 space)
- Outer edges that are flush with the screen border (e.g. left edge of toolbar): **0 px** —
  no radius on the screen-adjacent edge; radius only on the inward-facing edge.
- Resource bar: **0 px** radius on all edges.

### Accent and State Colours

| Token | Hex | Usage |
|---|---|---|
| Accent / teal | `#00C9C8` | Active borders, focus rings, active-state glow |
| Active border | `rgba(0, 201, 200, 0.75)` | 2 px border on active buttons and icon cells |
| Amber / values | `#F0B429` | All HUD numeric values: treasury, population, demand bars, date |
| Amber / undo warning | `#F0B429` | Undo countdown amber state |
| Near-white / labels | `#EBF4F6` | Primary label text |
| Mid-blue / sub-labels | `#4A7FA5` | Secondary / sub-labels, less prominent text |
| Error / deficit red | `#F04E37` | Deficit indicators, error states |
| Warning amber | `#E8960C` | Warning states (distinct from value amber `#F0B429`) |

### Button Tile States

All toolbar, sub-panel, modal, and settings buttons use this three-state tile spec:

| State | Background | Border |
|---|---|---|
| Inactive | `rgba(255, 255, 255, 0.08)` | 1 px `rgba(255, 255, 255, 0.18)` |
| Hover | `rgba(255, 255, 255, 0.15)` | 1 px `rgba(255, 255, 255, 0.35)` |
| Active | `rgba(0, 201, 200, 0.22)` teal wash | 2 px `rgba(0, 201, 200, 0.75)` + 4 px baked glow |

"Baked glow" on the active state means the glow is pre-authored into the sprite cell
(see `architecture/asset-standards/2d-texture-standards.md` UI Sprite Sheet Art Style
— Glass City), not a runtime blur. The `IUIBackend` interface does not expose a blur
operation; the glow must be part of the active-state icon art.

### Icon States

| State | Style | Opacity | Border/Glow |
|---|---|---|---|
| Inactive | **Outlined — 2 px stroke only**, no fill | 65% | None |
| Hover | **2 px outlined stroke** | 85% | 1 px `rgba(255,255,255,0.35)` |
| Active | **Filled solid icon** | 100% | 2 px teal border + baked glow |

Icons "gain weight" on selection: the visual transition from outlined-stroke to filled-solid
communicates mode activation without relying solely on colour or border changes. All three
states are separate sprite cells in `hud_sprites_ui.png` — the stroke-only cell at 65%
opacity is the inactive variant, the stroke-only cell at 85% with a white border is the
hover variant, and the filled cell with teal border and baked glow is the active variant.

#### Hover State Switching — Implementation (Phase 10c)

Hover state switching is implemented entirely inside `IrrlichtUIBackend` using Irrlicht GUI
events — no new methods are added to the `IUIBackend` interface.

- **`EGET_ELEMENT_HOVERED`**: When this event fires on a toolbar button handle,
  `IrrlichtUIBackend` calls `IGUIButton::setImage()` with the rect for the corresponding
  `kSpriteXxxHover` cell from `hud_sprites_ui.png`.
- **`EGET_ELEMENT_LEFT`**: When this event fires, `IrrlichtUIBackend` calls
  `IGUIButton::setImage()` with the rect for the inactive `kSpriteXxx` cell, restoring the
  default inactive appearance.

**`kSpriteXxxHover` naming convention**: Every icon that has an inactive/active pair gains a
paired hover constant in `src/ui/hud_sprite_ids.h` following the same prefix pattern as the
existing inactive/active pairs. Representative examples by group:

- **Toolbar tool-mode icons**: `kSpriteToolZoneHover`, `kSpriteToolRoadHover`,
  `kSpriteToolUtilitiesHover`, `kSpriteToolDemolishHover`, `kSpriteToolQueryHover`
- **Zone sub-panel icons**: `kSpriteZoneResLowHover`, `kSpriteZoneComLowHover`,
  `kSpriteZoneIndLowHover` (and the corresponding Med/High variants)
- **Utilities sub-panel icons**: `kSpriteUtilPowerHover`, `kSpriteUtilWaterHover`,
  `kSpriteUtilFireHover`, `kSpriteUtilPoliceHover`

The hover sprite cell is always the cell immediately adjacent to the inactive cell within the
same row of `hud_sprites_ui.png` — its exact column is determined by the sprite sheet layout
documented in `architecture/asset-standards/2d-texture-standards.md`.

**No `IUIBackend` interface changes**: `setElementImage`, `setElementAlpha`, and the existing
`IUIBackend` virtual method set are sufficient. The hover swap is a pure internal detail of
`IrrlichtUIBackend` and is invisible to panels, `UIManager`, and tests that stub
`IUIBackend`.

### Button Tile Corner Radius

Button tiles (free-floating cell elements within panels) use **8 px corner radius** on all four corners.
This is distinct from the panel container corner radius rule (8 px inner edge / 0 px flush-screen-edge).
Button tiles are independent interactive cells and receive uniform 8 px radius regardless of position within the panel.

### Superseded Values

The following values from earlier specs are **superseded** by the Glass City palette and
must not be used for new work:

| Old value | Category | Replacement |
|---|---|---|
| Milky/white frosted background | Panel background | `rgba(13, 27, 42, 0.78–0.88)` |
| `rgba(0, 200, 220, 35)` active tint | Active state signal | Teal wash `rgba(0, 201, 200, 0.22)` + 2 px border |
| Weak cyan-teal accent | Accent colour | `#00C9C8` |
| White or default for numeric values | Value colour | `#F0B429` amber |

> **Implementation note**: The superseded "Frosted Glass" sprite sheet art style section in
> `architecture/asset-standards/2d-texture-standards.md` documents the Phase 10 signed-off
> sprite sheet. That section is now extended (not replaced) by the Glass City spec in the
> same file. New icon authoring follows Glass City; the signed-off Phase 10 sheet is a
> historical record of what was delivered before Glass City was adopted.

## Colorblind Accessibility

- **Colorblind mode toggle**: A "Colorblind Mode" toggle is located in **Settings > Graphics tab, Accessibility subsection** — see [`settings-pause-menu.md`](settings-pause-menu.md) for the canonical tab structure definition. The toggle MUST NOT appear in any other tab or panel. It switches all color-coded UI to a colorblind-safe alternative encoding.
- **Minimap zone palette** (colorblind mode): Replace Residential=green / Commercial=blue / Industrial=orange with a pattern-supplemented palette: each zone type uses a distinct hatching or cross-hatch pattern overlay in addition to color, ensuring deuteranopia and tritanopia users can distinguish zone types by pattern alone.
- **Demand pressure bars** (R/C/I in HUD): Must always display zone-type letter labels (R / C / I) adjacent to or inside each bar — color is supplemental, not the sole encoding. This applies in all modes (colorblind and standard).

  **Demand pressure bar hatching patterns (colorblind mode)** — When colorblind mode is active, each demand bar column MUST display a hatching overlay so that zone type is distinguishable by pattern alone, independent of color and the clamped letter label:
  - **Residential (R)**: diagonal hatching at 45°
  - **Commercial (C)**: horizontal lines
  - **Industrial (I)**: cross-hatch
- **Service Coverage overlay on minimap**: In colorblind mode, use distinct geometric pattern overlays (e.g., diagonal hatching for fire, horizontal lines for police, dotted for power, cross-hatch for water) in addition to tint colors.
- **Zone placement preview/cursor tint**: In colorblind mode, zone type cursors must include a zone-type label overlay (small "R", "C", or "I" text) so players can confirm zone type without relying on color.
- **3D zone colour overlay** (the `setZoneOverlay` semi-transparent quad layer introduced in Phase 9b):
  In standard mode, the zone overlay uses: Residential = `0x6000FF00` (green, alpha 0x60), Commercial = `0x600000FF` (blue, alpha 0x60), Industrial = `0x60FFFF00` (yellow, alpha 0x60).
  In colorblind mode, the zone overlay MUST switch to a colorblind-safe palette: Residential = `0x602020FF` (blue-violet, alpha 0x60), Commercial = `0x60FF8000` (orange, alpha 0x60), Industrial = `0x60FF00FF` (magenta, alpha 0x60). These three hues are distinguishable under deuteranopia and protanopia. Additionally, each zone type's overlay quad MUST include a zone-type letter stamp — the `IrrlichtRenderer::setZoneOverlay()` implementation, when colorblind mode is active, renders a small 'R', 'C', or 'I' sprite centered on each zoned tile (drawn as a separate pass at Y + 0.15f, using a white-on-transparent glyph texture from the UI sprite sheet). This dual-encoding (color + letter) satisfies WCAG 2.1 SC 1.4.1. `UIManager` passes the colorblind-safe ARGB values to `setZoneOverlay` when colorblind mode is on; `IrrlichtRenderer` reads a `m_colorblindMode` boolean (set via a new `setColorblindMode(bool)` method on `IrrlichtRenderer`, called by `UIManager` when the Settings colorblind toggle changes) to know whether to render the glyph pass. The colorblind ARGB values for the overlay map entries are computed by `UIManager` when updating `m_overlayMap` — `UIManager` queries `m_settings->isColorblindMode()` (or a cached `m_colorblindMode` bool updated on settings change) to select the correct ARGB per zone type before inserting into `m_overlayMap`.
- **Tile hover highlight** (the `setTileHoverHighlight` per-tool ARGB introduced in Phase 9b):
  In standard mode, hover highlight colours are: Zone = `0x80FF00FF` (magenta), Road = `0x8000FFFF` (cyan), Utilities = `0x80FF8000` (orange), Demolish = `0x80FF0000` (red), Query = `0x80FFFFFF` (white). All five hues are distinguishable in standard mode.
  In colorblind mode, the hover highlight MUST additionally render a tool-type icon or letter in the highlight quad to supplement color: Zone shows a small 'Z' glyph, Road shows 'R', Utilities shows 'U', Demolish shows 'X'. Query retains the white highlight (white is colorblind-safe). The glyph is rendered as a sprite from the UI sprite sheet, centered on the tile, using the same Y + 0.05f offset as the hover quad. The actual hover highlight ARGB colours do not change in colorblind mode for the hover highlight (unlike the zone overlay) — the colours are bright and high-contrast enough that deuteranopia does not cause confusion between them. The glyph is additive supplemental encoding. **Implementation scope**: The hover highlight colorblind glyph is deferred to Phase 12 (colorblind QA pass), consistent with the Phase 8 colorblind delivery schedule. Phase 9b delivers only the standard-mode hover ARGB colours. The architecture commitment is made here so Phase 12 has a concrete spec.
- **Reference standard**: WCAG 2.1 Success Criterion 1.4.1 (Use of Color) — color must not be the sole means of conveying information.
