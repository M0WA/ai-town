# Camera Controls

- **Pan**: Middle-mouse-button drag
- **Keyboard pan**: Arrow keys (Up/Down/Left/Right) are the default binding. Up = pan forward/north, Down = pan backward/south, Left = pan left/west, Right = pan right/east. Pan speed scales with zoom level (faster when zoomed out). Keyboard pan is listed in the KeyBindings config and appears in the rebinding UI. Default keyboard pan: Arrow keys (Up/Down/Left/Right). WASD is available as an alternative rebinding in Settings > Controls, but creates a conflict with Demolish (D) unless that hotkey is rebound first.
- **Zoom**: Scroll wheel
- **Rotate**: Right-mouse-button drag only (**not** Q/E — those are reserved for future camera controls)
- **Edge scrolling**: In scope for V1, configurable on/off in Settings > Controls. **Default state by window mode**: Edge scrolling is **ON by default in exclusive fullscreen mode** and **OFF by default in windowed mode**. Edge scrolling is automatically disabled when the application loses OS focus (e.g., Alt+Tab) — `CameraController` must check an `m_appHasFocus` flag set by the window focus event before processing edge-scroll input. This prevents unwanted panning when clicking on other windows at screen edges.
- **Pitch clamp boundary semantics**: The pitch clamp range [−70°, −20°] is inclusive at both bounds: after clamping, `pitch` may equal exactly −70° or exactly −20°. Unit tests must assert equality at the boundary values using `std::clamp(pitch, -70.0f, -20.0f)` semantics (not strictly-less-than comparisons).
- A dedicated `CameraController` class wraps Irrlicht's `ICameraSceneNode` and receives input events from `IEventReceiver`
- **Camera input exception during blocking modals**: MMB drag, RMB drag, and scroll-wheel zoom pass through to `CameraController` regardless of modal state. See [`input-arbitration.md`](input-arbitration.md) for the authoritative priority chain and camera pass-through rationale.
