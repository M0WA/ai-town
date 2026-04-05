## Phase 11p: Fully-Specced Minimap — Zone Coding, Road Network, Viewport Projection, Overlay Toggles & Tests

**Status: PENDING**

### Goal

Complete the minimap implementation to match the full specification in
`architecture/ui-ux/minimap.md`. Phase 9b delivered only a dark placeholder rectangle;
this phase adds zone color coding, road network rendering, correct camera viewport
projection, a two-button radio toggle row, the label strip, a properly-sized legend
panel, click-to-pan wiring, and budget-tick cadence — plus the
65 new test cases that verify every gap.

---

### Spec Updates

The following architecture spec files were updated as part of the squad review
(already applied — commit `fbe9a0f`):

- [x] **SU-01** `architecture/ui-ux/minimap.md` — Add a **Phase boundary** note at the end
  of the Colorblind mode subsection stating that colorblind pattern rendering for the minimap
  Service Coverage overlay is implemented in Phase 12 (not Phase 11p); Phase 11p delivers
  only the base tint colour per service type; `setColorblindMode(bool)` and pattern draw
  logic are Phase 12 deliverables.

- [x] **SU-02** `architecture/ui-ux/ui-manager.md` — Add method 22 documentation to the
  IUIBackend method contract section: `fillColoredRect(int x, int y, int w, int h, int r,
  int g, int b, int a)` — transient filled-rectangle draw call in virtual 1920×1080
  coordinate space; no persistent `UIElementHandle` created; must be called inside a frame
  render pass; used by `Minimap::draw()` for tile colour overlays without element leakage;
  `IrrlichtUIBackend` implements via `IVideoDriver::draw2DRectangle`.

- [x] **SU-03** `architecture/ui-ux/camera-controls.md` — Add Phase 11p extension section
  after the "Phase 1 gamedesign-lookandfeel sign-off" documenting the three new `CameraState`
  fields added by MM-05: `targetX{0.f}`, `targetZ{0.f}`, `zoomDistance{200.f}` — their
  semantics, who populates them (MM-06 `CameraController::getCameraState()`), and who reads
  them (MM-21 `Minimap::draw()` viewport projection).

- [x] **SU-04** `architecture/ui-ux/minimap.md` — Update the Phase 9b section to replace the
  deferred `CameraController::panTo()` reference with `CameraController::setTarget(float worldX,
  float worldZ)` (MM-31), aligning the spec with the actual API used in the Phase 11p plan.

---

### Deliverables

---

#### 1. IUIBackend API — `fillColoredRect` (method 22)

**Files:** `src/interfaces/IUIBackend.h`, `src/rendering/IrrlichtUIBackend.h`,
`src/rendering/IrrlichtUIBackend.cpp`, `tests/ui/MockUIBackend.h`,
any `StubUIBackend` in smoke tests

- [ ] **MM-01** `src/interfaces/IUIBackend.h`: Add pure-virtual method 22
  `virtual void fillColoredRect(int x, int y, int w, int h, int r, int g, int b, int a) = 0`
  with a doc comment: "Transient filled-rectangle draw call in virtual 1920×1080 coordinate
  space; no persistent `UIElementHandle` created; must be called inside a frame render pass
  (between `beginScene`/`endScene`); r, g, b, a each in [0, 255]."
  (ref: `architecture/ui-ux/minimap.md` — zone tile rendering, colored-rectangle approach)

- [ ] **MM-02** `src/rendering/IrrlichtUIBackend.h` + `IrrlichtUIBackend.cpp`: Implement
  `fillColoredRect` by applying the `UIScaler` transform to (x, y, w, h) to produce a
  screen-space `irr::core::recti`, then calling
  `m_driver->draw2DRectangle(irr::video::SColor(a, r, g, b), screenRect)`.
  (ref: `architecture/ui-ux/minimap.md` — colored-rectangle tile approach)

- [ ] **MM-03** `tests/ui/MockUIBackend.h`: Add `MOCK_METHOD(void, fillColoredRect,
  (int, int, int, int, int, int, int, int), (override))` stub.
  (ref: `architecture/testing/testability-architecture.md` — `IUIBackend` mock policy)

- [ ] **MM-04** Any `StubUIBackend` used in smoke tests (e.g., `ui_smoke_test.cpp`): Add a
  no-op `void fillColoredRect(int, int, int, int, int, int, int, int) override {}` override
  so the smoke-test build is not broken by the new pure-virtual method.
  (ref: `architecture/testing/testability-architecture.md`)

---

#### 2. CameraState extension

**Files:** `src/interfaces/camera_state.h`, `src/ui/CameraController.cpp`

- [ ] **MM-05** `src/interfaces/camera_state.h`: Add three new fields to the `CameraState`
  struct: `float targetX{0.f}`, `float targetZ{0.f}`, `float zoomDistance{200.f}`.
  (ref: `architecture/ui-ux/minimap.md` — viewport rectangle projection)

- [ ] **MM-06** `src/ui/CameraController.cpp`: Populate the three new fields in
  `getCameraState()` from `m_targetX`, `m_targetZ`, and `m_zoomDistance` respectively,
  so the minimap can compute the correct viewport indicator rectangle.
  (ref: `architecture/ui-ux/minimap.md` — viewport indicator size constraints)

---

#### 3. Minimap header — new public methods and members

**File:** `src/ui/Minimap.h`

- [ ] **MM-07** `src/ui/Minimap.h`: Fix the header comment that describes Industrial zone
  colour as "orange" — it must read "yellow" to match `architecture/ui-ux/minimap.md`
  (I=yellow, `#F39C12`).

- [ ] **MM-08** `src/ui/Minimap.h`: Add public method
  `void setCameraState(const CameraState& state)` — stores the latest camera state so
  `draw()` can project the viewport rectangle each frame.
  (ref: `architecture/ui-ux/minimap.md` — camera viewport rectangle)

- [ ] **MM-09** `src/ui/Minimap.h`: Add public method
  `void setPanCallback(std::function<void(float, float)> cb)` — registers the
  world-coordinate pan callback invoked when the player clicks the minimap.
  (ref: `architecture/ui-ux/minimap.md` — click-to-pan)

- [ ] **MM-10** `src/ui/Minimap.h`: Add public method
  `UIRect getWidgetFootprint() const` — returns the **dynamic** input-arbitration footprint:
  `{1576, m_overlayActive ? 732 : 848, 344, m_overlayActive ? 348 : 232}`.
  When no overlay is active: x:1576–1920, y:848–1080 (h:232). When any overlay is active:
  x:1576–1920, y:732–1080 (h:348), extending upward to cover the legend panel (y:732–832)
  and label strip (y:832–848). Distinct from `getBounds()` which returns only the 200×200
  render area. UIManager calls this each frame to keep arbitration bounds in sync.
  (ref: `architecture/ui-ux/minimap.md` — input-arbitration widget footprint, dynamic bounds)

- [ ] **MM-11** `src/ui/Minimap.h`: Replace `UIElementHandle m_toggleBtn` with two named
  handles: `UIElementHandle m_toggleBtnSvc{kInvalidUIElement}` (Service Coverage toggle,
  x:1720, y:848, 32×32) and `UIElementHandle m_toggleBtnTfc{kInvalidUIElement}` (Traffic
  Congestion toggle, x:1684, y:848, 32×32, 4 px gap leftward from Svc button).
  (ref: `architecture/ui-ux/minimap.md` — overlay toggle row, radio behavior)
  **Note**: Phase 11p creates text-label buttons ('Svc' / 'Tfc') as placeholders. Icon sprite implementation using `kSpriteOverlayXxx` constants (`setElementImage` wiring) is a **Phase 12** deliverable per `architecture/asset-standards/2d-texture-standards.md` §Phase 10 Sign-Off — UI Sprite Sheet.

- [ ] **MM-12** `src/ui/Minimap.h`: Add private member `UIElementHandle m_labelStrip{kInvalidUIElement}`
  for the 16 px tall label strip at virtual y:832–848 px (overlay name text, left-aligned
  to x:1720, text colour `#EBF4F6`).
  (ref: `architecture/ui-ux/minimap.md` — label strip)

- [ ] **MM-13** `src/ui/Minimap.h`: Add private members `CameraState m_cameraState{}` and
  `std::function<void(float, float)> m_panCallback{}`.

---

#### 4. Minimap implementation — `draw()` rewrite and all missing rendering

**File:** `src/ui/Minimap.cpp`

- [ ] **MM-15** `src/ui/Minimap.cpp` — constructor: Replace the single `m_toggleBtn` button
  creation with two button creations:
  `m_toggleBtnSvc` at virtual x:1720, y:848, 32×32 ("Svc") and
  `m_toggleBtnTfc` at virtual x:1684, y:848, 32×32 ("Tfc"). Register both with the
  input-arbitration system.
  (ref: `architecture/ui-ux/minimap.md` — overlay toggle row)

- [ ] **MM-16** `src/ui/Minimap.cpp` — constructor: Create `m_labelStrip` as a static-text
  element at virtual x:1720, y:832, w:200, h:16 with text colour `#EBF4F6`; start hidden.
  (ref: `architecture/ui-ux/minimap.md` — label strip)

- [ ] **MM-17** `src/ui/Minimap.cpp` — constructor: Resize the legend panel `m_legendPanel`
  to 200×100 px at virtual x:1720, y:732 (was 80×100 at wrong position); background
  `rgba(13, 27, 42, 209)` (`setElementBackground(handle, 13, 27, 42, 209)`); start hidden.
  (ref: `architecture/ui-ux/minimap.md` — legend panel 200×100, Glass City background)

- [ ] **MM-18** `src/ui/Minimap.cpp` — constructor: Update `m_mapBg` background colour from
  `rgba(20, 20, 20, 230)` to Glass City navy `rgba(13, 27, 42, 217)`
  (`setElementBackground(handle, 13, 27, 42, 217)`).
  (ref: `architecture/ui-ux/minimap.md §Visual Design — Glass City`)

- [ ] **MM-19** `src/ui/Minimap.cpp` — `draw()`: Zone color coding via `fillColoredRect`.
  Iterate all `getMapTilesX()` × `getMapTilesZ()` tiles using `ICitySimulation::queryTile()`.
  Map zone type to authoritative colours: Residential `#27AE60` (green), Commercial `#2980B9`
  (blue), Industrial `#F39C12` (yellow). Scale each tile to its pixel footprint in the
  200×200 render area. Emit one `fillColoredRect` call per non-empty zone tile. Do NOT call
  `addStaticText` or `addButton` inside `draw()`.
  (ref: `architecture/ui-ux/minimap.md` — zone color coding)

- [ ] **MM-20** `src/ui/Minimap.cpp` — `draw()`: Road network rendering via `fillColoredRect`.
  For each tile where `queryTile().isRoad == true`, fill the corresponding minimap pixel
  rect with road grey `#7F8C8D`. Non-road tiles are skipped.
  (ref: `architecture/ui-ux/minimap.md` — road network grey lines)

- [ ] **MM-21** `src/ui/Minimap.cpp` — `draw()`: Camera viewport rectangle projection.
  Use the authoritative formula from `architecture/ui-ux/minimap.md` §Viewport projection
  formula:
  - `kTileSize = 10.0f` — declare as a `static constexpr float` local to `Minimap.cpp`
    (not shared with IrrlichtRenderer; the two usages are independent).
  - World extents: `worldW = m_sim->getMapTilesX() * kTileSize`;
    `worldD = m_sim->getMapTilesZ() * kTileSize`.
  - Centre pixel: `cx = (m_cameraState.targetX / worldW) * 200`;
    `cz = (m_cameraState.targetZ / worldD) * 200` (clamp each to [0, 200]).
  - Side: `side = 200.f * (m_cameraState.zoomDistance / CameraController::kMaxZoomDistance)`,
    clamped to [8, 190] px.
  - Draw centred at `(cx, cz)`, clamp rect to [0, 200] render area.
  - Call `m_backend->setElementRect(m_viewportRect, rectX, rectZ, side, side)` each frame —
    no new element is created.
  (ref: `architecture/ui-ux/minimap.md` — Viewport projection formula, size constraints 8–190 px)

- [ ] **MM-22** `src/ui/Minimap.cpp` — `draw()`: Budget-tick cadence. Cache the
  `queryTile()` iteration results (zone grid snapshot and road grid snapshot) in member
  arrays. Re-query only when `consumeBudgetTicks()` returns a non-zero count (> 0). Per-frame `draw()`
  uses the cached snapshot; no `queryTile()` calls are made on non-tick frames.
  (ref: `architecture/ui-ux/minimap.md` — "Overlay data is rendered into the minimap texture
  at budget-tick cadence (not per-frame)")

- [ ] **MM-23** `src/ui/Minimap.cpp` — `draw()`: Legend content — dynamic switching.
  **Element lifecycle rule**: Legend `addStaticText` elements are created **once** (in
  the constructor or on first overlay activation), never inside `draw()` — this satisfies
  the MM-42 zero-element-creation contract. `draw()` only calls `setElementText()`,
  `setElementVisible()`, and `fillColoredRect()` on pre-existing elements.

  When Service Coverage overlay is active, render four rows using 8×8 px `fillColoredRect`
  swatches: Fire Station `#C0392B`, Police Station `#2E4482`, Power Plant `#F1C40F`,
  Water Tower `#1ABC9C`. When Traffic Congestion overlay is active, render three rows:
  Free-flowing `#27AE60`, Mild congestion `#E67E22`, Heavy congestion `#E74C3C`. Legend
  text labels use colour `#EBF4F6`; update via `setElementText()` in `draw()` when overlay
  changes (not by creating new elements).
  (ref: `architecture/ui-ux/minimap.md` — legend panel, authoritative hex values)

- [ ] **MM-25** `src/ui/Minimap.cpp` — `onEvent()`: Click-to-pan. Map click pixel coordinates
  within the minimap render area to world coordinates and invoke `m_panCallback(worldX, worldZ)`.
  If no callback is registered, the click is consumed but no-op. Clicks outside the minimap
  render area (x:1720–1920, y:880–1080) do not fire the callback.
  (ref: `architecture/ui-ux/minimap.md` — click-to-pan)

- [ ] **MM-26** `src/ui/Minimap.cpp` — `onEvent()`: Two-button radio toggle. Hit-test both
  `m_toggleBtnSvc` (x:1720–1752, y:848–880) and `m_toggleBtnTfc` (x:1684–1716, y:848–880).
  Radio behavior: clicking the currently active button deactivates it (no overlay);
  clicking the inactive button activates it and deactivates the other. On toggle, show or
  hide `m_labelStrip` and `m_legendPanel` appropriately, and update button visual states
  (active: filled solid icon, 100% opacity, 2 px teal `rgba(0,201,200,0.75)` border + glow;
  inactive: outlined 2 px stroke icon, 65% opacity, no border) per
  `architecture/ui-ux/minimap.md §Visual Design — Glass City`.
  (ref: `architecture/ui-ux/minimap.md` — overlay toggle, button states)

- [ ] **MM-27** `src/ui/Minimap.cpp` — `setCameraState()` implementation: Store the supplied
  `CameraState` in `m_cameraState` for use during the next `draw()` call.

- [ ] **MM-28** `src/ui/Minimap.cpp` — `getWidgetFootprint()` implementation: Return the
  dynamic footprint `UIRect{1576, m_overlayActive ? 732 : 848, 344, m_overlayActive ? 348 : 232}`.
  No overlay active → `{1576, 848, 344, 232}`; overlay active → `{1576, 732, 344, 348}`.
  (ref: `architecture/ui-ux/minimap.md` — dynamic input-arbitration widget footprint)

- [ ] **MM-29a** `src/ui/ui_constants.h` — Add two named constants for the minimap widget
  input-arbitration Y bounds:
  `constexpr int kMinimapWidgetTop = 848;` (no overlay active)
  `constexpr int kMinimapWidgetTopOverlayActive = 732;` (any overlay active)
  These constants are referenced by MM-29 and MM-10 and must exist before either is compiled.
  (ref: `architecture/ui-ux/minimap.md` — dynamic widget footprint, toggle button position)

- [ ] **MM-29** `src/ui/UIManager.cpp` — input arbitration: In `UIManager`'s draw/update loop,
  call `m_minimap->getWidgetFootprint()` each frame and cache the result; use this dynamic
  rect (not a hard-coded constant) when evaluating whether a click falls inside the minimap
  widget. When no overlay is active the footprint is `{1576,848,344,232}`; when any overlay
  is active the footprint expands to `{1576,732,344,348}` so the legend panel and label strip
  block click-through to city tiles. Use `kMinimapWidgetTop`/`kMinimapWidgetTopOverlayActive`
  from `ui_constants.h` (added in MM-29a); do NOT hard-code raw numbers in UIManager.
  (ref: `architecture/ui-ux/input-arbitration.md` — minimap carve-out with dynamic bounds)

---

#### 5. UIManager wiring

**File:** `src/ui/UIManager.cpp`

- [ ] **MM-30** `src/ui/UIManager.cpp` — `draw()`: Call `m_minimap->setCameraState(camState)`
  each frame, passing the `CameraState` obtained from `m_cameraController->getCameraState()`,
  so the minimap always has current camera position and zoom for viewport rectangle projection.
  (ref: `architecture/ui-ux/minimap.md` — camera viewport rectangle)

- [ ] **MM-31** `src/ui/UIManager.cpp` — initialization: Call `m_minimap->setPanCallback(...)`,
  passing a lambda that calls `m_cameraController->setTarget(worldX, worldZ)` — the
  `CameraController::setTarget(float, float)` method moves the camera look-at to the specified
  world XZ coordinates. Confirm `setTarget(float, float)` is public in
  `src/ui/CameraController.h` before implementing. So minimap clicks pan the camera to the
  corresponding world position.
  (ref: `architecture/ui-ux/minimap.md` — click-to-pan)

---

#### 6. Tests — 65 new cases in 10 groups

**CMakeLists.txt registration** (required before tests are discoverable by CTest):

- [ ] **MM-32** Root `CMakeLists.txt`: Register the test file by extending the existing
  `ui_tests` target via `target_sources(ui_tests PRIVATE tests/ui/minimap_overlay_test.cpp)`.
  Do NOT call `add_executable(ui_tests ...)` again — use `target_sources` per framework.md
  §Phase 4+ target extension policy. **No additional `aitown_add_tests()` call is needed**:
  the existing `aitown_add_tests(ui_tests LABEL "unit" TIMEOUT 300)` call already registers
  the entire `ui_tests` target with the `unit` CTest label; adding sources via `target_sources`
  automatically makes the 65 new cases discoverable by CTest under the `unit` label in all CI
  jobs (`build-linux`, `build-windows`, `coverage-linux`).
  (ref: `architecture/testing/framework.md` — `target_sources` policy)

**File:** `tests/ui/minimap_overlay_test.cpp` (extended via `target_sources`)

**Mock strategy**: Groups 1–9 use `NiceMock<MockUIBackend>` and `NiceMock<MockCitySimulation>`
(standard policy for UI overlay interaction tests per `architecture/testing/testability-architecture.md`
§Mock Policy). Group 10 (element leak regression, MM-42) uses `StrictMock<MockUIBackend>` in a
separate fixture class `MinimapElementLeakTest` to enforce `Times(0)` call-count contracts.

- [ ] **MM-33** `tests/ui/minimap_overlay_test.cpp` — Group 1: Zone color coding (8 tests).
  Stub `ICitySimulation::queryTile()` to return each of the four zone types (Residential,
  Commercial, Industrial, None) and verify that `draw()` emits `fillColoredRect` calls with
  the correct authoritative hex colours (`#27AE60`, `#2980B9`, `#F39C12`) and skips empty
  tiles. Include tests for mixed-zone maps.
  (ref: `architecture/ui-ux/minimap.md` — zone color coding, authoritative hex values)

- [ ] **MM-34** `tests/ui/minimap_overlay_test.cpp` — Group 2: Road network rendering (4 tests).
  Verify that road tiles produce `fillColoredRect` calls with grey `#7F8C8D`; non-road
  tiles do not; a map with no roads produces zero road-colour calls; a map with only roads
  produces no zone-colour calls.
  (ref: `architecture/ui-ux/minimap.md` — road network grey lines)

- [ ] **MM-35** `tests/ui/minimap_overlay_test.cpp` — Group 3: Camera viewport rectangle
  (5 tests). Verify `setElementRect` is called on `m_viewportRect` with the correct computed
  pixel rect for a range of (targetX, targetZ, zoomDistance) values; verify the 8 px minimum
  clamp; verify the 190 px maximum clamp; verify no crash when `setCameraState()` has never
  been called.
  (ref: `architecture/ui-ux/minimap.md` — viewport indicator size constraints)

- [ ] **MM-36** `tests/ui/minimap_overlay_test.cpp` — Group 4: Click-to-pan (6 tests).
  Verify that clicking inside the 200×200 render area fires the pan callback with correct
  world coordinates proportional to click position; verify clicking outside does not fire
  the callback; verify no crash when no callback is registered; verify boundary pixels.
  (ref: `architecture/ui-ux/minimap.md` — click-to-pan)

- [ ] **MM-37** `tests/ui/minimap_overlay_test.cpp` — Group 5: Overlay toggle radio behavior
  (5 tests). Verify two buttons exist; clicking the inactive Svc button activates it and
  deactivates Tfc; clicking the active Svc button deactivates it (no active overlay); only
  one overlay can be active at a time; clicking inactive Tfc button activates it and shows
  Traffic Congestion legend.
  (ref: `architecture/ui-ux/minimap.md` — radio behavior)

- [ ] **MM-38** `tests/ui/minimap_overlay_test.cpp` — Group 6: Label strip (7 tests). Verify
  `m_labelStrip` is hidden when no overlay is active; visible when Svc overlay is active;
  visible when Tfc overlay is active; positioned at virtual y:832, h:16, x:1720; text is set
  to the overlay name; text colour is `#EBF4F6`; strip is hidden again after overlay is
  deactivated.
  (ref: `architecture/ui-ux/minimap.md` — label strip)

- [ ] **MM-39** `tests/ui/minimap_overlay_test.cpp` — Group 7: Legend panel (13 tests).
  Verify legend panel dimensions are 200×100 at virtual x:1720, y:732; background alpha is
  209 (`rgba(13,27,42,209)`); Service Coverage legend shows four rows with authoritative hex
  swatches `#C0392B`, `#2E4482`, `#F1C40F`, `#1ABC9C`; Traffic Congestion legend shows three
  rows with `#27AE60`, `#E67E22`, `#E74C3C`; legend dynamically switches content when active
  overlay changes; legend is hidden when no overlay is active; legend text colour is `#EBF4F6`.
  (ref: `architecture/ui-ux/minimap.md` — legend panel, authoritative hex values, Glass City)

- [ ] **MM-40** `tests/ui/minimap_overlay_test.cpp` — Group 8: Budget-tick cadence (4 tests).
  Verify `ICitySimulation::queryTile()` is called exactly once (for the full tile grid) per
  budget tick, not per frame; verify that calling `draw()` multiple times between ticks does
  not re-query; verify that a budget tick occurring between two `draw()` calls triggers a
  re-query on the second `draw()`; verify the cached snapshot is used for rendering in
  non-tick frames.
  (ref: `architecture/ui-ux/minimap.md` — budget-tick cadence)

- [ ] **MM-41** `tests/ui/minimap_overlay_test.cpp` — Group 9: Input footprint (8 tests).
  Verify `getBounds()` always returns `Rect{1720, 880, 200, 200}` (render area only);
  verify `getWidgetFootprint()` returns `UIRect{1576, 848, 344, 232}` when no overlay active;
  verify `getWidgetFootprint()` returns `UIRect{1576, 732, 344, 348}` when overlay is active
  (toggle a service coverage overlay on, then call getWidgetFootprint() — y drops to 732,
  height grows to 348); verify the two `getBounds()`/`getWidgetFootprint()` methods return
  distinct values in both overlay states; verify toggle button hit-test regions lie within the
  base (no-overlay) footprint; verify the label strip and legend panel Y coords lie within the
  overlay-active footprint.
  (ref: `architecture/ui-ux/minimap.md` — `getBounds()` semantics, dynamic widget footprint)

- [ ] **MM-42** `tests/ui/minimap_overlay_test.cpp` — Group 10: Element leak regression
  (5 tests). Use a **separate fixture** `MinimapElementLeakTest : public ::testing::Test`
  with `StrictMock<MockUIBackend>` (not NiceMock) to enforce `Times(0)` call-count contracts.
  Set `EXPECT_CALL(mockBackend, addStaticText(...)).Times(0)` and
  `EXPECT_CALL(mockBackend, addButton(...)).Times(0)` before calling `draw()`; verify that
  one `draw()` call, ten consecutive `draw()` calls, and a `draw()` after a budget tick each
  produce zero element-creation calls; verify that `fillColoredRect` is called (not zero)
  during these same `draw()` invocations so the test is not vacuous.
  (ref: `architecture/ui-ux/minimap.md` — colored-rectangle approach, not element-per-tile)

---

### Exit Criteria

- [ ] `make build` completes without errors or warnings on both Linux and Windows.
- [ ] `CMakeLists.txt` extended via `target_sources(ui_tests PRIVATE tests/ui/minimap_overlay_test.cpp)`
  (MM-32); CTest discovers all 65 new cases under the `unit` label.
- [ ] All 65 new test cases in `tests/ui/minimap_overlay_test.cpp` (Groups 1–10) pass under
  `make test`.
- [ ] All pre-existing unit, integration, and OpenGL tests continue to pass unchanged —
  zero regressions introduced.
- [ ] The element-leak regression tests (Group 10, MM-42) run in CI using `StrictMock<MockUIBackend>`;
  `addStaticText` and `addButton` are never called inside `draw()`.
- [ ] `IUIBackend::fillColoredRect` (MM-01) is verified present in `MockUIBackend` with a
  `MOCK_METHOD` stub and in `IrrlichtUIBackend` with a real `draw2DRectangle` implementation.
- [ ] `CameraState` has `targetX`, `targetZ`, `zoomDistance` fields (MM-05) and
  `CameraController::getCameraState()` populates them (MM-06).
- [ ] `src/ui/ui_constants.h` contains `kMinimapWidgetTop = 848` and
  `kMinimapWidgetTopOverlayActive = 732` (MM-29a).
- [ ] `Minimap::getWidgetFootprint()` returns `UIRect{1576,848,344,232}` when no overlay
  active and `UIRect{1576,732,344,348}` when overlay active (MM-10, MM-28);
  `Minimap::getBounds()` continues to return `Rect{1720, 880, 200, 200}` unchanged.
- [ ] Legend panel geometry is 200×100 at virtual x:1720, y:732 (not 80×100 at the former
  position) — verified by Group 7 tests (MM-39).
- [ ] Budget-tick cadence is enforced: `queryTile()` is called at most once per budget tick,
  not once per frame — verified by Group 8 tests (MM-40).
- [x] `architecture/ui-ux/minimap.md` Colorblind mode deferral note added (SU-01) — already applied commit `fbe9a0f`.
- [x] `architecture/ui-ux/ui-manager.md` IUIBackend method 22 `fillColoredRect` documented (SU-02) — already applied commit `fbe9a0f`.
- [x] `architecture/ui-ux/camera-controls.md` Phase 11p extension section added (SU-03) — already applied commit `fbe9a0f`.
- [x] `architecture/ui-ux/minimap.md` Phase 9b deferred API updated from `panTo()` to
  `setTarget()` (SU-04) — already applied commit `fbe9a0f`.
- [ ] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'`
  reports zero errors.

---

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | `fillColoredRect` in `IrrlichtUIBackend`; `CameraState` extension; `CameraController::getCameraState()` update; `Minimap.cpp` full rewrite (zone, road, viewport, budget-tick cache, legend, radio toggle, click-to-pan); `UIManager.cpp` wiring |
| `gamedesign-ux` | Review minimap visual output against `architecture/ui-ux/minimap.md` (Glass City colors, button states, label strip position, legend panel geometry) |
| `test-dev-cpp` | Author all 65 test cases across the 10 groups in `minimap_overlay_test.cpp`; add `MOCK_METHOD` stub in `MockUIBackend`; verify no-op override in `StubUIBackend` |

---

### Dependencies

- Requires Phase 11o complete (source clean-up applied; `IUIBackend` method numbering up to
  method 21 established; `UITestFixtureBase` and `MockUIBackend.h` in their clean state).
- `ICitySimulation::queryTile()`, `getMapTilesX()`, `getMapTilesZ()` must be present on the
  interface (confirmed present from Phase 6 onward).
- `consumeBudgetTicks()` tick-count mechanism must be accessible from `Minimap` (confirmed
  present from Phase 9b onward).
- `CameraController::kMaxZoomDistance` must be accessible as a public `constexpr` so
  `Minimap.cpp` can compute the viewport side length (confirm or expose before implementing
  MM-21).

---

### Risks & Spikes

- **RISK**: `StubUIBackend` in `ui_smoke_test.cpp` may have other call sites that indirectly
  require `fillColoredRect` to be non-abstract before the phase is fully implemented,
  breaking the smoke-test build immediately on adding the pure-virtual method. **Spike**: Add
  the no-op `StubUIBackend` override (MM-04) in the same commit as the `IUIBackend` pure
  virtual declaration to keep the build green throughout the phase.

- **RISK**: `CameraController::kMaxZoomDistance` may not be `public` or may be named
  differently. **Spike**: `grep -r kMaxZoomDistance src/` before implementing MM-21 to
  confirm the constant name and visibility; expose or rename before authoring the viewport
  formula.

- **RISK**: The budget-tick tick-count mechanism (`consumeBudgetTicks()`) may return the tick
  delta rather than a non-zero tick count, requiring the minimap to compare against zero. **Spike**: Read
  `src/ui/Minimap.h` and `ICitySimulation.h` to confirm the exact return type and semantics
  before implementing MM-22.

- **RISK**: The two-button radio row (MM-15) creates a second `addButton` call in the
  constructor; if the input-arbitration system registers buttons by index rather than handle,
  the new second button may collide with an existing handle. **Spike**: Review how
  `m_toggleBtn` was registered in Phase 9b to confirm handle-based (not index-based)
  arbitration before renaming to `m_toggleBtnSvc`/`m_toggleBtnTfc`.
