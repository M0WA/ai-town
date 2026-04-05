# Camera Controls

- **Pan**: Middle-mouse-button drag
- **Keyboard pan**: Arrow keys (Up/Down/Left/Right) are the default binding. Up = pan forward/north, Down = pan backward/south, Left = pan left/west, Right = pan right/east. Pan speed scales with zoom level (faster when zoomed out). Keyboard pan is listed in the KeyBindings config and appears in the rebinding UI. Default keyboard pan: Arrow keys (Up/Down/Left/Right). WASD is available as an alternative rebinding in Settings > Controls, but creates a conflict with Demolish (D) unless that hotkey is rebound first.

## Pan Speed Specification

Pan speed scales with current zoom distance using the following formula:

```text
panSpeed = kBasePanSpeed * (currentZoomDistance / kDefaultZoomDistance)
```

Named constants (values chosen at implementation time):

- `kBasePanSpeed` — world units per second at `kDefaultZoomDistance`
- `kDefaultZoomDistance` — default camera distance from target
- `kMinZoomDistance` — minimum allowed zoom (clamps pan speed to minimum)
- `kMaxZoomDistance` — maximum allowed zoom (clamps pan speed to maximum)

Arrow-key pan uses the same zoom-scale formula as MMB drag pan, multiplied by a separate `kKeyboardPanRate` constant (allows Arrow-key pan to be tuned independently from mouse drag pan speed). Arrow-key pan uses **continuous key-held state**: `OnInputEvent(KeyDown)` sets a boolean flag (e.g., `m_panLeft`); `OnInputEvent(KeyUp)` clears it; `update(dt)` applies `panSpeed * dt` while the flag is set. This approach is robust against OS key-repeat configuration differences (containers, VMs, X11 settings) and provides frame-rate-independent smooth panning.

The mouse-sensitivity slider (Phase 8 Settings panel) applies as a user-controlled multiplier on top of the zoom-scaled base rate: `effectivePanSpeed = panSpeed * sensitivityMultiplier`. This design allows Phase 1 to implement the correct zoom-scaled formula without requiring refactoring when Phase 8 adds the sensitivity slider. **The mouse-sensitivity slider applies to mouse-driven pan inputs ONLY** — specifically MMB drag and edge-scroll. Arrow-key pan is controlled exclusively by `kKeyboardPanRate` and is NOT affected by the sensitivity slider; the slider must not be applied to the keyboard pan code path.

- **Zoom**: Scroll wheel. Scroll wheel controls zoom distance ONLY — it does NOT control camera pitch angle.
- **Rotate / Pitch**: Right-mouse-button (RMB) drag only (**not** Q/E — those are reserved for future camera controls). Camera pitch is controlled by RMB vertical drag — NOT by scroll wheel. Pitch clamp test cases must inject vertical RMB drag events (`MouseButtonDown button=1` + `MouseMove` with `physY` varying), not `MouseWheel` events. **RMB drag always starts immediately on mouse down** — `EventReceiver` sets `m_rmbDragActive = true` on every `EMIE_RMOUSE_PRESSED_DOWN` without calling `UIManager`. A short RMB press with no movement is a click that cancels the active placement tool (handled in `UIManager::onEvent()` on `MouseButtonUp button=1` when `m_rmbMoved == false`); if movement occurs during the press, only the camera drag fires and the tool is NOT cancelled. This design lets the player pan the camera freely while a placement tool is active — the tool is only cancelled by a deliberate short right-click.
- **Edge scrolling**: In scope for V1, configurable on/off in Settings > Controls. **Default state by window mode**: Edge scrolling is **ON by default in exclusive fullscreen mode** and **OFF by default in windowed mode**. Edge scrolling is automatically disabled when the application loses OS focus (e.g., Alt+Tab) — `CameraController` must check an `m_appHasFocus` flag set by the window focus event before processing edge-scroll input. This prevents unwanted panning when clicking on other windows at screen edges. Edge-scroll activation band: 20 px from each screen edge in virtual 1920×1080 space (i.e., mouse position x < 20 triggers left-scroll; x > 1900 triggers right-scroll; y < 20 triggers forward/north-scroll; y > 1060 triggers backward/south-scroll). This threshold is used in `CameraController` unit tests.
- **CameraController constructor contract**: The `CameraController` constructor must accept a `bool startInFullscreen` parameter (or equivalent `WindowMode` enum) that sets the initial `m_edgeScrollEnabled` state per the edge-scroll default rules above. The `IrrlichtRenderer` or platform layer passes the initial window mode at construction.
- **`isEdgeScrollEnabled()` accessor**: `CameraController` must expose `bool isEdgeScrollEnabled() const` as a public method that returns the current value of `m_edgeScrollEnabled`. This accessor is required by the unit test `CameraController_EdgeScroll_EnabledByDefaultInFullscreen` (see `architecture/testing/testability-architecture.md`) to assert the constructor's initial state without performing any input injection.
- **`setEdgeScrollEnabled()` setter**: `CameraController` MUST expose `void setEdgeScrollEnabled(bool enabled)` as a public method that updates `m_edgeScrollEnabled`. Called by the Settings panel (Settings > Controls > Edge scroll toggle) at runtime. Unlike `m_appHasFocus = false` (which suppresses edge-scroll at the processing point without changing the stored preference), `setEdgeScrollEnabled()` persists the player's explicit on/off choice. Required at Phase 1 to lock the interface before Phase 8 Settings panel wiring.
- **Pitch clamp boundary semantics**: The pitch clamp range [−70°, −20°] is inclusive at both bounds: after clamping, `pitch` may equal exactly −70° or exactly −20°. Unit tests must assert equality at the boundary values using `std::clamp(pitch, -70.0f, -20.0f)` semantics (not strictly-less-than comparisons).
- **Pitch/yaw → Cartesian forward-vector formula (null-camera path)**: When `camera == nullptr` (the unit-test seam), `CameraController::getCameraState()` MUST derive the `forward` vector from `m_pitch` and `m_yaw` using the following formula, expressed in Irrlicht's **left-handed, Y-up** coordinate system where yaw = 0 corresponds to looking toward +Z:

  ```cpp
  const float pitch_rad = m_pitch * (M_PI / 180.0f);
  const float yaw_rad   = m_yaw   * (M_PI / 180.0f);
  state.forward.x = std::cos(pitch_rad) * std::sin(yaw_rad);
  state.forward.y = std::sin(pitch_rad);   // negative at pitch in [-70°, -20°] — camera points downward
  state.forward.z = std::cos(pitch_rad) * std::cos(yaw_rad);
  // World-up for null-camera path:
  state.up = vec3{0.0f, 1.0f, 0.0f};      // approved test-seam world-up
  ```

  **Correctness verification**: At pitch = −45°, `forward.y = sin(−45°) ≈ −0.707` (negative — camera pointing downward into terrain). At yaw = 0, `forward.z = cos(−45°) ≈ 0.707` (positive — pointing toward +Z, Irrlicht's default forward). These expected values are used in the pitch-sign-convention exit criterion. A formula yielding `forward.y > 0` at pitch = −45° is a sign-convention error. The `state.up = (0, 1, 0)` world-up in the null-camera path is the explicitly **approved** test-seam behavior; only the live-camera path (`camera != nullptr`) must use `camera->getUpVector()`.
- **Drag-delta coordinate space**: Mouse drag-delta calculations in `CameraController` MUST use physical pixel coordinates (the raw screen-space coordinate difference BEFORE `UIScaler::unproject()` is applied), not virtual-space coordinates. Applying `unproject()` to drag deltas would scale camera sensitivity with viewport scaling, producing incorrect pan/rotate speed at non-native resolutions (e.g. 1.5x faster pan at 1280×720 vs 1920×1080).
- A dedicated `CameraController` class wraps Irrlicht's `ICameraSceneNode` and receives input events from `IEventReceiver`
- **Camera input exception during blocking modals**: MMB drag, RMB drag, and scroll-wheel zoom pass through to `CameraController` regardless of modal state. See [`input-arbitration.md`](input-arbitration.md) for the authoritative priority chain and camera pass-through rationale.

---

## Phase 1 gamedesign-lookandfeel sign-off

**Date**: 2026-02-21
**Agent**: gamedesign-lookandfeel

### Pitch sign convention verified

Code inspection of `src/ui/CameraController.cpp`, function `getCameraState()`, null-camera path (lines 229-265) confirms:

- `state.forward.y = std::sin(pitch_rad)` is the formula used (line 247).
- At pitch = -45 degrees, `pitch_rad = -45 * pi/180 = -0.7854 rad`, giving `sin(-0.7854) = -0.707` (negative). This means `forward.y < 0` at the standard operating pitch, confirming the camera points downward into terrain as required.
- The inline comment at line 247 explicitly reads: `// negative for pitch in [-70°, -20°] — correct`.
- A formula yielding `forward.y > 0` at pitch = -45 degrees would be a sign-convention error; this implementation is correct.

### camera_state.h field layout verified

Code inspection of `src/interfaces/camera_state.h` confirms the `CameraState` struct contains exactly:

- `vec3 position` — camera world position
- `vec3 forward` — normalised forward direction vector
- `vec3 up` — camera up vector
- `float pitch{0.0f}` — test-seam spherical coordinate (degrees)
- `float yaw{0.0f}` — test-seam spherical coordinate (degrees)

The three `vec3` fields match the locked canonical definition established in Phase 0. The `pitch` and `float yaw` fields are the newly added test-seam fields required for null-camera unit tests. Field layout is compliant with spec.

## Phase 11p Extension: CameraState Fields for Minimap Viewport Projection

`MM-05` (Phase 11p) adds three new fields to the `CameraState` struct in
`src/interfaces/camera_state.h` for minimap viewport rectangle projection:

| Field | Type | Default | Semantics |
|---|---|---|---|
| `targetX` | `float` | `0.f` | World X coordinate of the camera look-at target |
| `targetZ` | `float` | `0.f` | World Z coordinate of the camera look-at target |
| `zoomDistance` | `float` | `200.f` | Distance from the camera to its look-at target (proxy for zoom level) |

`CameraController::getCameraState()` (`MM-06`) populates all three fields each frame.
`Minimap::drawOverlay()` (`MM-21`) reads them to project the camera viewport rectangle onto the
200×200 minimap area. The three fields are read-only from Minimap's perspective.

### CameraController::setTarget (Minimap Click-to-Pan)

`MM-31` (Phase 11p) adds a public method for minimap click-to-pan:

```cpp
void CameraController::setTarget(float worldX, float worldZ);
```

**Semantics**: Moves the camera look-at target to the specified world XZ coordinates.
Updates `m_targetX` and `m_targetZ` to the provided values. Does **not** alter pitch,
yaw, or zoom distance -- only the horizontal position of the look-at point changes.
The camera position is recomputed from the new target using the existing pitch, yaw,
and zoom distance on the next `update()` call.

**Caller**: `UIManager`'s minimap pan callback invokes `setTarget()` when the player
clicks on the minimap surface. The minimap converts the click position from minimap-local
pixel coordinates to world XZ via the inverse of the projection used in
`Minimap::drawOverlay()`, then passes the result to `CameraController::setTarget()`.
