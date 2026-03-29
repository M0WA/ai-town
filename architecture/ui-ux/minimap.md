# Minimap

- **Dimensions**: 200×200 px in virtual 1920×1080 coordinate space (scaled via UIScaler)
- **Rendered content**: Top-down zone color coding (R=green, C=blue, I=yellow), road network (grey lines), camera viewport rectangle (white outline). **Viewport indicator size constraints**: The white viewport rectangle has a **minimum size of 8×8 px** (to remain visible when the camera is zoomed far out and the viewport maps to a large world area) and a **maximum size of 190×190 px** (clamped to keep the indicator within the 200×200 px minimap boundaries with 5 px margin on each side). These clamp values are applied to the computed viewport rectangle before rendering; the camera's actual world frustum is unaffected.
- **Interaction**: Click-to-pan camera to clicked minimap position
- **Overlay toggle**: An extensible icon-button row on the minimap border; **one button per overlay type** (radio behavior — only one overlay active at a time; clicking active overlay deactivates it). For V1 with Service Coverage and Traffic Congestion overlays, the row has two icon buttons plus the implicit "off" state. This architecture scales to additional overlays (demand heat map, etc.) without UX redesign.
  - **Toggle button position**: The overlay toggle row is anchored to the **top edge of the minimap**. The minimap occupies virtual bounds x: 1720–1920 px, y: 880–1080 px (bottom-right corner). The toggle row sits at virtual x: 1720–1752 px, y: 848–880 px (32×32 px icon button, 8 px gap above minimap top edge at y=880). Additional overlay buttons extend leftward from x:1720 (each 32×32 px with 4 px gap between buttons). **Up to 4 overlay toggle buttons are supported; each button is 32 px wide with 4 px gap; the leftmost button's left edge is no further left than x: 1576 (= 1720 − 4 × (32+4)). The full overlay toggle row occupies x: 1576–1752, y: 848–880 px. The input-arbitration widget footprint for the minimap widget includes this full toggle row extent (x: 1576–1920, y: 848–1080), not only the 200×200 px render area.**
  - Active button state: filled icon with accent color border. Inactive: outline icon, no border. States defined in UI sprite sheet.
  - When an overlay is active: a **label strip** (16 px tall) appears **immediately above the toggle row** at virtual y: 832–848 px (overlay name, left-aligned to x:1720), and a **legend panel** (200×100 px) is anchored **above the label strip** at virtual x: 1720–1920, y: **732–832 px** (100 px tall, positioned immediately above the label strip at y:832). The legend panel spans the full minimap width (x: 1720–1920) and is positioned immediately below the minimap render area's chrome stack (above the label strip). This placement keeps the legend clear of the minimap (y:880–1080), toggle row (y:848–880), and label strip (y:832–848). **Do NOT anchor the legend inside the minimap bounds** (y:880–1080) — this causes visual overlap with the city map tiles. The legend panel dynamically updates to show data for whichever overlay is currently active (Service Coverage or Traffic Congestion), displaying category colors with text labels (8×8 px color swatch and text label per category).
  - **Service Coverage overlay**: Covered tiles receive a colour tint according to the active
    service layer. Authoritative hex values (used for both the minimap tile tint and the legend
    swatches):

    | Service | Colour | Hex |
    |---|---|---|
    | Fire Station | Red | `#C0392B` |
    | Police Station | Blue | `#2E4482` |
    | Power Plant | Yellow | `#F1C40F` |
    | Water Tower | Cyan | `#1ABC9C` |

    Overlay data is rendered into the minimap texture at budget-tick cadence (not per-frame).

    **Colorblind mode** (required per `architecture/ui-ux/resolution-ui-scaling.md`
    §Colorblind Accessibility): when colorblind mode is active, each covered-tile colour
    must also include a distinct geometric pattern overlay so the service type is
    distinguishable by pattern alone, independent of colour:

    | Service | Pattern |
    |---|---|
    | Fire Station | Diagonal hatching at 45° |
    | Police Station | Horizontal lines |
    | Power Plant | Dotted overlay |
    | Water Tower | Cross-hatch |

    Patterns are rendered at 50% opacity on top of the tint colour. Pattern pixel pitch:
    4 px between lines/dots at the minimap tile resolution. Both tint colour and pattern
    are applied simultaneously in colorblind mode — the tint is not suppressed.

    **Cross-hatch pattern reuse note**: The cross-hatch pattern (Water Tower) is also used
    for the Industrial demand bar colorblind mode in `hud-layout.md` and
    `resolution-ui-scaling.md`. This reuse is intentional — the two contexts (minimap service
    coverage overlay vs. HUD demand bars) are visually distinct and do not appear
    simultaneously in a way that could cause confusion.

  - **Traffic Congestion overlay**: Road segments are coloured by speed band relative to the
    free-flow speed of that segment. Authoritative hex values (used for both the minimap road
    colouring and the legend swatches):

    | Speed band | Condition | Hex |
    |---|---|---|
    | ≥ 40 % of free-flow speed | Green (free-flowing) | `#27AE60` |
    | 31 – 39 % of free-flow speed | Orange (mild congestion) | `#E67E22` |
    | ≤ 30 % of free-flow speed | Red (moderate–heavy congestion) | `#E74C3C` |

    Overlay data is rendered into the minimap texture at budget-tick cadence (not per-frame).
    Unroaded tiles are not coloured.
- **`getBounds()` return value semantics**: The `Minimap::getBounds()` method returns `Rect` (the struct defined in `IUIBackend.h` — `struct Rect { int x{0}, y{0}, w{0}, h{0}; }`) representing the bounding rectangle of the minimap **render area only** — the 200×200 px tile (virtual bounds x: 1720–1920 px, y: 880–1080 px). Returning `Rect` rather than `irr::core::rect<irr::s32>` keeps Irrlicht headers out of `src/ui/` translation units. It explicitly excludes the toggle row (y: 848–880 px), the label strip (y: 832–848 px when an overlay is active), and the legend overlay panel (y: 732–832 px when an overlay is active). Tests that call `getBounds()` and check its dimensions MUST compare against the 200×200 px render area, not the full minimap widget footprint including chrome. Using the full widget bounds in tests will produce incorrect hit-test and overlap results because the chrome elements can be toggled independently of the render area.

- **Scrim input behavior during blocking modals**: When a blocking modal (`ModalDialog`) is active, the full-screen scrim `IGUIElement` (50% opacity fill rect) **must consume left-mouse click events and right-click context events** that would otherwise reach HUD elements behind it (minimap, toolbar, undo button, resource bar). The scrim is not merely a visual overlay — it must be an event-consuming element at Priority 1 of the input arbitration chain. Without this, left-clicks and right-clicks on the minimap (and other HUD elements) pass through the scrim while the modal is visible, allowing accidental tool activations (zone placement, road placement) during a blocking modal. **Camera pass-through (mandatory)**: The following input events must NOT be consumed by the scrim — they pass through directly to `CameraController` per input-arbitration.md Priority 1: scroll-wheel zoom, middle-mouse-button drag (pan), and right-mouse-button drag (rotate/pan). These camera interactions are non-destructive and provide useful spatial context while the player reads the modal. Only left-click and right-click context events (which could trigger tool activations or HUD interactions) are consumed.

## Visual Design — Glass City

### Minimap Background

The minimap area uses the Glass City deep-navy panel background:

- **Map background panel** (`m_mapBg`): `rgba(13, 27, 42, 0.85)` — 8 px corner radius on
  the inward edges (top-left, top-right, bottom-left); the right and bottom edges are flush
  with the screen border and use 0 px radius.

  > **Implementation note**: The Phase 9b background was set via
  > `setElementBackground(handle, 20, 20, 20, 230)` (near-black RGBA). This is superseded
  > by the Glass City navy `rgba(13, 27, 42, ...)` which corresponds to
  > `setElementBackground(handle, 13, 27, 42, 217)` (alpha 217 ≈ 0.85 × 255).
  > Update this call when implementing Phase 11 minimap rendering.

- **Legend panel** (`m_legendPanel`): `rgba(13, 27, 42, 0.82)` —
  `setElementBackground(handle, 13, 27, 42, 209)`.

### Overlay Toggle Button States

The overlay toggle icon buttons on the minimap border follow the Glass City icon state spec:

| State | Style |
|---|---|
| Inactive | Outlined 2 px stroke icon at 65% opacity, no border |
| Active | Filled solid icon at 100% opacity, 2 px teal `rgba(0, 201, 200, 0.75)` border + baked glow |

This extends and replaces the earlier description "Active button state: filled icon with
accent color border. Inactive: outline icon, no border."

### Legend and Label Text

- **Overlay label strip text**: `#EBF4F6` near-white, left-aligned
- **Legend category names**: `#EBF4F6` near-white
- **Colour swatches** (8×8 px): use the authoritative hex values defined in the overlay
  entries above (Service Coverage: `#C0392B`, `#2E4482`, `#F1C40F`, `#1ABC9C`;
  Traffic Congestion: `#27AE60`, `#E67E22`, `#E74C3C`). These are data-encoding
  colours, not UI chrome, and are unchanged by Glass City.

## Minimap Lifecycle — Show/Hide on State Transitions

The `Minimap` constructor calls `hide()` internally as its last step, so the minimap starts
hidden. `UIManager` is responsible for calling `m_minimap->show()` and `m_minimap->hide()` at
the correct state transition points.

**Required show/hide calls in `UIManager`** (all implemented as of Phase 9b):

| Transition | Required call | Status |
|---|---|---|
| `transitionToGameplay(mode)` | `m_minimap->show()` | Implemented |
| `transitionToMainMenu()` | `m_minimap->hide()` | Implemented |
| `transitionToGameOver()` | `m_minimap->hide()` | Implemented |

## Phase 9b Minimum Viable Minimap

### What the player sees in Phase 9b

The minimap is **visually present** during gameplay as a dark rectangle in the bottom-right
corner. This is not a full implementation — zone coding, road network lines, and the camera
viewport rectangle are Phase 11+ features. The Phase 9b bar is deliberately low: the panel
exists and is non-transparent so the player can see it as a distinct HUD element.

**Phase 9b visual output (implemented)**:

- **Dark background panel** — the `m_mapBg` static-text element has a filled dark background
  (RGBA 20, 20, 20, 230) set via `IUIBackend::setElementBackground()`. This makes the minimap
  area a visible dark rectangle instead of being transparent.
- **Viewport indicator** — `m_viewportRect` has a semi-transparent white fill (255, 255, 255,
  64) to indicate the approximate camera view area within the minimap panel.
- **Legend panel** — `m_legendPanel` has a dark fill (20, 20, 20, 210) so legend text is
  legible when the service coverage overlay is active.
- **Toggle button** — the "Svc" button at virtual x:1720, y:848–880 is rendered by Irrlicht's
  default button skin.

### IUIBackend method 18: setElementBackground

The fix required adding `setElementBackground(handle, r, g, b, a)` as method 18 to
`IUIBackend`. This method enables `fillBackground` on an `IGUIStaticText` element and sets its
fill color. Implementation in `IrrlichtUIBackend` casts to `IGUIStaticText*` (guard on
`getType() == EGUIET_STATIC_TEXT`) and calls `setBackgroundColor()` + `setDrawBackground(true)`.
`MockUIBackend` has the corresponding `MOCK_METHOD` stub. `StubUIBackend` in `ui_smoke_test.cpp`
has a no-op override.

Channels r, g, b, a are each in `[0, 255]`. Irrlicht `SColor` constructor order is
`(a, r, g, b)`.

### What is NOT required in Phase 9b

- **Zone color coding** (R=green, C=blue, I=yellow): requires `ICitySimulation::queryTile()`
  iteration over all tiles and pixel-level drawing. This is a **Phase 11** deliverable.
- **Road network lines**: requires iterating road tiles and drawing line primitives. **Phase 11**.
- **Camera viewport rectangle**: requires projecting the camera frustum to minimap coordinates.
  **Phase 11**.
- **Render texture / off-screen camera**: the spec does NOT require a render texture for the
  minimap at any phase. The minimap is drawn by iterating tile data and filling colored
  rectangles via the UI backend — it is a 2D data visualization, not a 3D camera view. A
  render texture approach would require a second Irrlicht camera and off-screen render target,
  which is out of scope for V1. The colored-rectangle approach (tile data → pixel grid) is the
  correct implementation for all V1 phases.
- **Click-to-pan camera**: the `onEvent()` stub in `Minimap.cpp` already consumes the click;
  the actual `CameraController::panTo()` call is wired in **Phase 11** when minimap coordinates
  are mapped to world coordinates.
- **Service coverage overlay**: **Phase 11**.
- **Overlay toggle button functional**: the toggle button is shown in Phase 9b but clicking it
  only flips `m_overlayActive`; no visual change results until the overlay rendering is
  implemented in Phase 11.
