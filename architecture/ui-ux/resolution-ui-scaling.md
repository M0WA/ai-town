# Resolution & UI Scaling

- **Minimum supported resolution**: 1280×720
- **Target resolution**: 1920×1080
- All UI elements authored in virtual 1920×1080 coordinate space; scaled at runtime via `UIScaler` before passing to `IGUIEnvironment`
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
  // the returned VirtualPoint is clamped to [0, virtualW] × [0, virtualH] — i.e.
  // x is clamped to [0, 1920] and y is clamped to [0, 1080].
  // unproject() NEVER returns negative virtual coordinates or coordinates exceeding
  // the virtual resolution. Callers must not add their own clamp on top of this.
  struct VirtualPoint { int x; int y; };  // nested inside UIScaler — callers use UIScaler::VirtualPoint
  VirtualPoint unproject(int physicalX, int physicalY) const;
  ```

  **Note**: These signatures are locked at Phase 0 (stub implementations; Phase 1 fills in logic). Phase 1 parallel teams implementing `CameraController::OnInputEvent()` and `UIManager` input dispatch MUST use exactly these signatures to avoid integration breakage.

- **Mouse un-projection ownership**: `UIScaler::unproject()` MUST be applied exactly once, at the entry point of the input chain. The platform event receiver (in `src/platform/`) applies the transform before handing the resulting `InputEvent` to `UIManager::onEvent()`. Panels receive virtual-space coordinates and must not call `UIScaler` again. Panel unit tests inject pre-projected virtual-space coordinates directly, bypassing the platform receiver entirely.
- No hardcoded pixel offsets anywhere in UI code

## Typography

- **Minimum body font size**: 14 px virtual (1920×1080 space). At 1280×720 (scale factor ≈ 0.667), this renders to approximately 9 px physical — the minimum for compact panels. Prefer **16 px virtual** for all interactive labels.
- **Minimum label font size in compact panels** (Query/Inspector Panel, Tax Rate Panel, Notification toasts): **13 px virtual**, never smaller. These panels display numeric data that must be legible under time pressure.
- **Hard physical floor**: No UI text may render below **11 px physical pixels** at the minimum supported resolution (1280×720). `UIScaler` must clamp text scale so that font sizes never fall below this floor, even if a window is resized below 1280×720.
- **Typeface requirements**: Numeric readouts (tax rates, treasury balance, population counts, percentages) must use a **monospace typeface** (prevents layout shift as digits change). Labels (zone types, panel titles, button text) must use a **sans-serif typeface** for legibility at small sizes.
- **These rules apply to all UI elements**: HUD resource bar, demand pressure bars (must supplement color with zone-type letters R/C/I), toolbar tooltip text, Query Panel fields, Tax Rate Panel rows, notification toasts, modal dialog body text, and minimap legend labels.

## Colorblind Accessibility

- **Colorblind mode toggle**: A "Colorblind Mode" toggle is located in **Settings > Graphics tab, Accessibility subsection** — see [`settings-pause-menu.md`](settings-pause-menu.md) for the canonical tab structure definition. The toggle MUST NOT appear in any other tab or panel. It switches all color-coded UI to a colorblind-safe alternative encoding.
- **Minimap zone palette** (colorblind mode): Replace Residential=green / Commercial=blue / Industrial=orange with a pattern-supplemented palette: each zone type uses a distinct hatching or cross-hatch pattern overlay in addition to color, ensuring deuteranopia and tritanopia users can distinguish zone types by pattern alone.
- **Demand pressure bars** (R/C/I in HUD): Must always display zone-type letter labels (R / C / I) adjacent to or inside each bar — color is supplemental, not the sole encoding. This applies in all modes (colorblind and standard).
- **Service Coverage overlay on minimap**: In colorblind mode, use distinct geometric pattern overlays (e.g., diagonal hatching for fire, horizontal lines for police, dotted for power, cross-hatch for water) in addition to tint colors.
- **Zone placement preview/cursor tint**: In colorblind mode, zone type cursors must include a zone-type label overlay (small "R", "C", or "I" text) so players can confirm zone type without relying on color.
- **Reference standard**: WCAG 2.1 Success Criterion 1.4.1 (Use of Color) — color must not be the sole means of conveying information.
