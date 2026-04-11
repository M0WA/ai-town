# Minimap

- **Dimensions**: 200×200 px in virtual 1920×1080 coordinate space (scaled via UIScaler)
- **Rendered content**: Top-down zone color coding (R=green, C=blue, I=yellow), road network (grey lines), camera viewport rectangle (white outline). **Viewport indicator size constraints**: The white viewport rectangle has a **minimum size of 8×8 px** (to remain visible when the camera is zoomed far out and the viewport maps to a large world area) and a **maximum size of 190×190 px** (clamped to keep the indicator within the 200×200 px minimap boundaries with 5 px margin on each side). These clamp values are applied to the computed viewport rectangle before rendering; the camera's actual world frustum is unaffected.

  **Coordinate mapping** (Phase 11q7): The minimap is camera-centred and camera-following.

  - **Camera target = minimap centre**: `(targetX, targetZ)` always maps to pixel
    `(kMapX + 100, kMapY + 100)`. Tiles are expressed relative to the camera target
    and rotated by `yaw_rad` so the camera's forward direction points toward the
    top of the minimap.
  - **World extents**: `worldW = getMapTilesX() × kTileSize` (metres); `worldD = getMapTilesZ() × kTileSize` (metres). `kTileSize = 10.0f` m/tile is a compile-time constant local to `Minimap.cpp`.
  - **Scale**: `scaleX = kMapW / worldW` px/m; `scaleZ = kMapH / worldD` px/m.
  - **Per-tile pixel position**:

    ```text
    relX = wx - targetX;  relZ = wz - targetZ          (world-space offset from target)
    rotX = relX * cos(yaw_rad) - relZ * sin(yaw_rad)    (rotate world so cam-fwd = up)
    rotZ = relX * sin(yaw_rad) + relZ * cos(yaw_rad)
    px   = kMapX + 100 + rotX * scaleX
    py   = kMapY + 100 - rotZ * scaleZ
    ```

    Tiles outside `[kMapX, kMapX+kMapW) × [kMapY, kMapY+kMapH)` are culled.
  - **North indicator**: A small "N" marker rendered at the minimap border at angle
    `yaw_rad` from the top: pixel `(kMapX + 100 − 90·sin(yaw_rad), kMapY + 100 − 90·cos(yaw_rad))`.
    This gives the player an absolute bearing reference as the camera rotates.
  - **Viewport indicator**: Always centred at `(kMapX+100, kMapY+100)` — no translation
    needed since camera target is always the minimap centre. **Side length**:
    `side = 200 × (zoomDistance / CameraController::kMaxZoomDistance)`, clamped to [8, 190] px.
    Draw as four transient `fillColoredRect` white strips (1–2 px thick) in `drawOverlay()`,
    after zone/road tile colours: top strip, bottom strip, left strip, right strip —
    all at `rgba(255, 255, 255, 200)`. `m_viewportRect` is no longer a persistent GUI
    element as of Phase 11p; the viewport indicator is fully transient.
  - **Click-to-pan**: Offset from minimap centre converted to metres, then rotated
    by `+yaw_rad` to recover world-space offset from camera target:

    ```text
    offX      = (clickX - (kMapX+100)) / scaleX
    offZ      = ((kMapY+100) - clickY) / scaleZ
    worldOffX =  offX * cos(yaw_rad) + offZ * sin(yaw_rad)
    worldOffZ = -offX * sin(yaw_rad) + offZ * cos(yaw_rad)
    panTo(targetX + worldOffX, targetZ + worldOffZ)
    ```

  - `CameraController::kMaxZoomDistance` must be a `public static constexpr float` on
    `CameraController`; Minimap accesses it via the class name (no instance required).
- **Interaction**: Click-to-pan camera to clicked minimap position
- **Overlay toggle**: An extensible icon-button row on the minimap border; **one button per overlay type** (radio behavior — only one overlay active at a time; clicking active overlay deactivates it). For V1 with Service Coverage and Traffic Congestion overlays, the row has two icon buttons plus the implicit "off" state. This architecture scales to additional overlays (demand heat map, etc.) without UX redesign.
  - **Toggle button position**: The overlay toggle row is anchored to the **top edge of the minimap**. The minimap occupies virtual bounds x: 1720–1920 px, y: 880–1080 px (bottom-right corner). The **full overlay toggle row** spans virtual x: 1576–1752 px, y: 848–880 px (supporting up to 4 overlay buttons). The rightmost button (first, e.g. Service Coverage) sits at x: 1720–1752 px; additional overlay buttons extend leftward from x:1720 (each 32×32 px with 4 px gap between buttons). **Up to 4 overlay toggle buttons are supported; each button is 32 px wide with 4 px gap; the leftmost button's left edge is no further left than x: 1576 (= 1720 − 4 × (32+4)). The full overlay toggle row occupies x: 1576–1752, y: 848–880 px. The input-arbitration widget footprint for the minimap widget is **dynamic**: when no overlay is active it spans (x: 1576–1920, y: 848–1080); when any overlay is active it expands upward to (x: 1576–1920, y: 732–1080) to cover the legend panel (y:732–832) and label strip (y:832–848). `Minimap::getWidgetFootprint()` returns the correct current footprint (computed from `m_overlayActive`); UIManager must call `getWidgetFootprint()` each frame to keep arbitration bounds in sync with overlay state. The `kMinimapWidgetTop` (y:848, no overlay) and `kMinimapWidgetTopOverlayActive` (y:732, overlay active) constants in `ui_constants.h` encode these two Y values.**
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

    **Multi-service overlap**: When a tile is covered by more than one service type,
    `drawOverlay()` processes the cached `getServiceCoverage()` snapshot in a fixed ascending
    draw-priority order — Fire Station first, then Police Station, then Power Plant, then
    Water Tower — so the last-drawn color wins via painter's algorithm (Water Tower cyan wins
    over all others; Power Plant yellow wins over Fire and Police; Police blue wins over Fire).
    Before iterating, sort or bucket the snapshot entries into this order; duplicate
    `(tileX, tileZ)` entries are permitted since painter's algorithm handles them correctly.
    This produces deterministic, design-intentional display regardless of building placement
    order or simulation iteration order.

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

    **Phase boundary**: Colorblind pattern rendering for the minimap Service Coverage overlay
    is implemented in **Phase 12** (polish and QA pass), not Phase 11p. Phase 11p delivers
    only the base tint colour per service type. The `setColorblindMode(bool)` method and
    pattern draw logic are Phase 12 deliverables.

  - **Traffic Congestion overlay**: Road segments are coloured by speed band relative to the
    free-flow speed of that segment. Authoritative hex values (used for both the minimap road
    colouring and the legend swatches):

    | Speed band | Condition | Hex |
    |---|---|---|
    | ≥ 40 % of free-flow speed | Green (free-flowing) | `#27AE60` |
    | > 30 % and < 40 % of free-flow speed | Orange (mild congestion) | `#E67E22` |
    | ≤ 30 % of free-flow speed | Red (moderate–heavy congestion) | `#E74C3C` |

    Overlay data is rendered into the minimap texture at budget-tick cadence (not per-frame).
    Unroaded tiles are not coloured.
- **`getBounds()` return value semantics**: The `Minimap::getBounds()` method returns `UIRect` (the struct defined in `IUIBackend.h` — `struct UIRect { int x{0}, y{0}, w{0}, h{0}; }`) representing the bounding rectangle of the minimap **render area only** — the 200×200 px tile (virtual bounds x: 1720–1920 px, y: 880–1080 px). Returning `UIRect` rather than `irr::core::rect<irr::s32>` keeps Irrlicht headers out of `src/ui/` translation units. It explicitly excludes the toggle row (y: 848–880 px), the label strip (y: 832–848 px when an overlay is active), and the legend overlay panel (y: 732–832 px when an overlay is active). Tests that call `getBounds()` and check its dimensions MUST compare against the 200×200 px render area, not the full minimap widget footprint including chrome. Using the full widget bounds in tests will produce incorrect hit-test and overlap results because the chrome elements can be toggled independently of the render area.

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

**Phase boundary**: Full Glass City icon visual states (filled vs. outlined icons,
2 px teal `rgba(0, 201, 200, 0.75)` border, baked glow) are implemented in **Phase 12**
when `setElementImage` wiring and `kSpriteOverlayXxx` constants are added (see
`architecture/asset-standards/2d-texture-standards.md` §Phase 10 Sign-Off — UI Sprite
Sheet). Phase 11p delivers opacity-based button states using text-label placeholders
('Svc' / 'Tfc'): active button at 100% opacity (`setElementAlpha(handle, 1.0f)`), inactive button
at 65% opacity (`setElementAlpha(handle, 0.65f)`). The inactive sprite cell opacity (65%) is baked
into the sprite sheet artwork; Phase 12 does NOT rely on `setElementAlpha` for inactive
icon rendering.

### Legend and Label Text

- **Overlay label strip text**: `#EBF4F6` near-white, left-aligned
- **Legend category names**: `#EBF4F6` near-white
- **Colour swatches** (8×8 px): use the authoritative hex values defined in the overlay
  entries above (Service Coverage: `#C0392B`, `#2E4482`, `#F1C40F`, `#1ABC9C`;
  Traffic Congestion: `#27AE60`, `#E67E22`, `#E74C3C`). These are data-encoding
  colours, not UI chrome, and are unchanged by Glass City.

### Zone Colors and Road Network

Authoritative hex values for the base minimap tile rendering (implemented in Phase 11p):

| Layer | Colour | Hex |
|---|---|---|
| Residential zone | Green | `#27AE60` |
| Commercial zone | Blue | `#2980B9` |
| Industrial zone | Yellow | `#F39C12` |
| Road network | Grey | `#7F8C8D` |

These values are used in `Minimap::drawOverlay()` for the per-tile `fillColoredRect` calls and in
`minimap_overlay_test.cpp` for colour-assertion tests. `drawOverlay()` is a separate method
invoked after `guiEnv->drawAll()` to ensure tile colours and the viewport outline render on
top of the GUI background panel (see `irrlicht-device-lifecycle.md` render sequence). Unzoned tiles are not coloured (no
`fillColoredRect` call). Note that `#27AE60` also appears in the Traffic Congestion overlay
free-flow colour — the values are coincidentally identical but represent independent design
decisions (zone presence vs. traffic speed).

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
  the actual `CameraController::setTarget(float worldX, float worldZ)` call is wired in
  **Phase 11** (MM-31) when minimap coordinates are mapped to world coordinates.
- **Service coverage overlay**: **Phase 11**.
- **Overlay toggle button functional**: the toggle button is shown in Phase 9b but clicking it
  only flips `m_overlayActive`; no visual change results until the overlay rendering is
  implemented in Phase 11.
