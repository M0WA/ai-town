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

- **Element creation** (`addStaticText`, `addButton`): virtual coordinates passed by panels are scaled to physical screen pixels before calling `m_guiEnv->addStaticText()` / `m_guiEnv->addButton()`. The scaling factors are `screenW / virtualW` and `screenH / virtualH`, computed from the driver's screen dimensions and the fixed virtual canvas size (1920×1080).
- **Element rect query** (`getElementRect`): Irrlicht stores element positions in physical screen pixels. `getElementRect()` scales these back to virtual coordinates before returning a `Rect` to the caller.

This ensures that:

1. Panels work exclusively in virtual 1920×1080 space — no panel code touches physical coordinates.
2. Hit tests in panel `onEvent()` handlers compare virtual mouse coordinates (from `UIScaler::unproject()`) against virtual element rects (from `getElementRect()`), so the coordinate spaces always match.
3. Elements are positioned correctly on screen at any resolution — a button at virtual (780, 320) is centered on a 1280×720 window (physical (520, 213)) rather than placed at physical pixel 780.

Without this scaling, elements are positioned at virtual-space values interpreted as physical pixels (e.g., a button intended for virtual x=780 appears at physical pixel 780 on a 1280-wide window, offset from center). Mouse un-projection converts physical clicks to virtual coordinates that do not match the misplaced element positions, making buttons unclickable.

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

  **Demand pressure bar hatching patterns (colorblind mode)** — When colorblind mode is active, each demand bar column MUST display a hatching overlay so that zone type is distinguishable by pattern alone, independent of color and the clamped letter label:
  - **Residential (R)**: diagonal hatching at 45°
  - **Commercial (C)**: horizontal lines
  - **Industrial (I)**: cross-hatch
- **Service Coverage overlay on minimap**: In colorblind mode, use distinct geometric pattern overlays (e.g., diagonal hatching for fire, horizontal lines for police, dotted for power, cross-hatch for water) in addition to tint colors.
- **Zone placement preview/cursor tint**: In colorblind mode, zone type cursors must include a zone-type label overlay (small "R", "C", or "I" text) so players can confirm zone type without relying on color.
- **Reference standard**: WCAG 2.1 Success Criterion 1.4.1 (Use of Color) — color must not be the sole means of conveying information.
