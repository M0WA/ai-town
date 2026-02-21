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

Arrow-key pan uses the same zoom-scale formula as MMB drag pan, multiplied by a separate `kKeyboardPanRate` constant (allows Arrow-key pan to be tuned independently from mouse drag pan speed).

The mouse-sensitivity slider (Phase 8 Settings panel) applies as a user-controlled multiplier on top of the zoom-scaled base rate: `effectivePanSpeed = panSpeed * sensitivityMultiplier`. This design allows Phase 1 to implement the correct zoom-scaled formula without requiring refactoring when Phase 8 adds the sensitivity slider. **The mouse-sensitivity slider applies to mouse-driven pan inputs ONLY** — specifically MMB drag and edge-scroll. Arrow-key pan is controlled exclusively by `kKeyboardPanRate` and is NOT affected by the sensitivity slider; the slider must not be applied to the keyboard pan code path.

- **Zoom**: Scroll wheel. Scroll wheel controls zoom distance ONLY — it does NOT control camera pitch angle.
- **Rotate / Pitch**: Right-mouse-button (RMB) drag only (**not** Q/E — those are reserved for future camera controls). Camera pitch is controlled by RMB vertical drag — NOT by scroll wheel. Pitch clamp test cases must inject vertical RMB drag events (`MouseButtonDown button=1` + `MouseMove` with `physY` varying), not `MouseWheel` events.
- **Edge scrolling**: In scope for V1, configurable on/off in Settings > Controls. **Default state by window mode**: Edge scrolling is **ON by default in exclusive fullscreen mode** and **OFF by default in windowed mode**. Edge scrolling is automatically disabled when the application loses OS focus (e.g., Alt+Tab) — `CameraController` must check an `m_appHasFocus` flag set by the window focus event before processing edge-scroll input. This prevents unwanted panning when clicking on other windows at screen edges. Edge-scroll activation band: 20 px from each screen edge in virtual 1920×1080 space (i.e., mouse position x < 20 triggers left-scroll; x > 1900 triggers right-scroll; y < 20 triggers forward/north-scroll; y > 1060 triggers backward/south-scroll). This threshold is used in `CameraController` unit tests.
- **CameraController constructor contract**: The `CameraController` constructor must accept a `bool startInFullscreen` parameter (or equivalent `WindowMode` enum) that sets the initial `m_edgeScrollEnabled` state per the edge-scroll default rules above. The `IrrlichtRenderer` or platform layer passes the initial window mode at construction.
- **`isEdgeScrollEnabled()` accessor**: `CameraController` must expose `bool isEdgeScrollEnabled() const` as a public method that returns the current value of `m_edgeScrollEnabled`. This accessor is required by the unit test `CameraController_EdgeScroll_EnabledByDefaultInFullscreen` (see `architecture/testing/testability-architecture.md`) to assert the constructor's initial state without performing any input injection.
- **`setEdgeScrollEnabled()` setter**: `CameraController` MUST expose `void setEdgeScrollEnabled(bool enabled)` as a public method that updates `m_edgeScrollEnabled`. Called by the Settings panel (Settings > Controls > Edge scroll toggle) at runtime. Unlike `m_appHasFocus = false` (which suppresses edge-scroll at the processing point without changing the stored preference), `setEdgeScrollEnabled()` persists the player's explicit on/off choice. Required at Phase 1 to lock the interface before Phase 8 Settings panel wiring.
- **Pitch clamp boundary semantics**: The pitch clamp range [−70°, −20°] is inclusive at both bounds: after clamping, `pitch` may equal exactly −70° or exactly −20°. Unit tests must assert equality at the boundary values using `std::clamp(pitch, -70.0f, -20.0f)` semantics (not strictly-less-than comparisons).
- **Drag-delta coordinate space**: Mouse drag-delta calculations in `CameraController` MUST use physical pixel coordinates (the raw screen-space coordinate difference BEFORE `UIScaler::unproject()` is applied), not virtual-space coordinates. Applying `unproject()` to drag deltas would scale camera sensitivity with viewport scaling, producing incorrect pan/rotate speed at non-native resolutions (e.g. 1.5x faster pan at 1280×720 vs 1920×1080).
- A dedicated `CameraController` class wraps Irrlicht's `ICameraSceneNode` and receives input events from `IEventReceiver`
- **Camera input exception during blocking modals**: MMB drag, RMB drag, and scroll-wheel zoom pass through to `CameraController` regardless of modal state. See [`input-arbitration.md`](input-arbitration.md) for the authoritative priority chain and camera pass-through rationale.
