# 3D Spatial Audio

- **Distance model**: `AL_INVERSE_DISTANCE_CLAMPED`

| Category | Reference distance | Max distance | Rolloff factor |
|---|---|---|---|
| Traffic / vehicles | 10 m | 150 m | 1.0 |
| Ambient crowd | 20 m | 200 m | 0.8 |
| Construction / industry | 15 m | 120 m | 1.2 |
| Zone ambient loops | 30 m | 300 m | 0.6 |
| UI / notification sounds | N/A (AL_SOURCE_RELATIVE) | N/A | 0.0 (non-positional) |
| Service events (fire, police) | 15 m | 150 m | 1.5 |

Rolloff factors are set per source at creation time via `alSourcef(src, AL_ROLLOFF_FACTOR, value)`. Higher rolloff = faster attenuation. `AL_SOURCE_RELATIVE` sources (music, ambient beds, UI) must have rolloff = 0.0 and position (0,0,0) to remain non-positional regardless of listener position. **Zone ambient loop cull alignment**: Zone ambient loops are culled by `AudioSystem` when the source is beyond **300 m** from the listener (matching the 300 m max distance in the table above). This matches the dynamic-soundscape.md cull distance. Sources at max distance produce inaudible output; the cull prevents pool exhaustion from distant inactive sources.

**Gameplay notification sounds (UI / notification category)**: UI toast SFX, budget alerts, milestone notification sounds, and all other HUD-driven audio events are **non-positional (2D)**. Their source setup is mandatory:

```cpp
// Required for all UI / notification SFX sources immediately after acquireSFXSource():
alSource3f(src, AL_POSITION,      0.f, 0.f, 0.f);
alSourcei (src, AL_SOURCE_RELATIVE, AL_TRUE);
alSourcef (src, AL_ROLLOFF_FACTOR,  0.0f);
```

These three calls ensure the source is completely unaffected by listener position or orientation — the sound plays at full gain regardless of where the camera is. Notification sounds are **NOT submitted to the EFX occlusion filter**: `alSourcei(src, AL_DIRECT_FILTER, AL_FILTER_NULL)` must be called for these sources if they are acquired from the evictable SFX pool (which pre-binds the EFX lowpass filter at construction). EFX occlusion filters apply only to **positional environmental sounds** — specifically vehicle engine sources and zone ambient loop sources. Applying occlusion filtering to UI sounds would cause them to be muffled when the camera is inside a building, which is incorrect behaviour.

**Vehicle source Doppler policy**: Vehicle engine sources must have `AL_VELOCITY = (0, 0, 0)` — Doppler pitch shift is explicitly disabled. Speed-dependent pitch uses `AL_PITCH` modulation only. See `dynamic-soundscape.md — Vehicle Engine Audio` for the full rationale, `AL_PITCH` range, and required implementation call order.

- **CameraState forward/up vector computation**: `CameraController::getCameraState()` must derive the `forward` and `up` fields from the Irrlicht camera node's own computed vectors when a camera node exists. The implementer must NOT manually compute these from raw pitch/yaw angles in the live path — Irrlicht already accounts for the node's full orientation in `getTarget()` and `getUpVector()`. Using world `(0, 1, 0)` as `up` unconditionally is incorrect when camera pitch != 0 and will produce wrong HRTF spatialization.

  **Null-camera guard (MANDATORY)**: `getCameraState()` MUST guard against `camera == nullptr` before calling any Irrlicht getter. When `camera` is null (the unit-test seam), the implementation must return internal state without touching any Irrlicht API. Calling `camera->getAbsolutePosition()` or any other Irrlicht getter on a null pointer causes a null-pointer dereference that crashes both production and test builds. The null path is only exercised by unit tests; production always has a live camera node.

  The `pitchYawToForward()` helper computes the forward unit vector from stored pitch/yaw angles in Irrlicht's left-handed coordinate system. It is used only in the null path (unit-test seam) to return a geometrically consistent forward vector without touching Irrlicht.

  The required implementation is:

  ```cpp
  CameraState CameraController::getCameraState() const {
      CameraState state;
      if (camera) {
          state.position = toVec3(camera->getAbsolutePosition());
          state.forward  = toVec3((camera->getTarget() -
                                   camera->getAbsolutePosition()).normalize());
          state.up       = toVec3(camera->getUpVector());
      } else {
          // Null camera (unit-test seam): return internal state without touching Irrlicht
          state.position = m_position;
          state.forward  = pitchYawToForward(m_pitch, m_yaw);
          state.up       = vec3{0.f, 1.f, 0.f};  // world-up approximation for tests only
      }
      return state;
  }
  ```

  Note that `getAbsolutePosition()`, `getTarget()`, and `getUpVector()` return Irrlicht `core::vector3df` values (uppercase `.X/.Y/.Z`). The `toVec3()` helper converts from `core::vector3df` to the AI Town `vec3` struct (lowercase `.x/.y/.z`) — this conversion must be applied to every value read from Irrlicht before assigning to a `CameraState` field. These computed `forward` and `up` vectors are consumed by `AudioSystem::syncListenerToCamera()` to set `AL_ORIENTATION` — see below.

- **Listener sync**: `AudioSystem::syncListenerToCamera(camera)` called **once per frame** after Irrlicht updates the camera. This function must update ALL three listener attributes — omitting any one breaks HRTF spatialization:

  **Note**: The AI Town `vec3` struct defined in `src/interfaces/vec3.h` uses **lowercase** `x`, `y`, `z` fields. `AudioSystem::syncListenerToCamera` takes a `const CameraState& cam` where each positional/directional member is an AI Town `vec3` — use `cam.position.x` (lowercase), `cam.forward.y` (lowercase), etc. This is distinct from Irrlicht's `core::vector3df`, which uses uppercase `.X/.Y/.Z`. Never mix the two.

  ```cpp
  void AudioSystem::syncListenerToCamera(const CameraState& cam) {
      // Position
      alListener3f(AL_POSITION, cam.position.x, cam.position.y, cam.position.z);
      // Velocity (set to zero — we do not model Doppler for camera movement)
      alListener3f(AL_VELOCITY, 0.f, 0.f, 0.f);
      // Orientation: forward vector followed by up vector (6-float array)
      ALfloat orientation[6] = {
          cam.forward.x, cam.forward.y, cam.forward.z,   // "at" vector
          cam.up.x,      cam.up.y,      cam.up.z          // "up" vector
      };
      alListenerfv(AL_ORIENTATION, orientation);
  }
  ```

  **`AL_ORIENTATION` is required for HRTF**: Without updating `AL_ORIENTATION` each frame, HRTF uses the listener's initial orientation (default: facing −Z, up +Y) for all spatialization, producing incorrect head-related transfer function results when the camera rotates. HRTF breaks silently — there is no OpenAL error; sounds simply appear from wrong directions. This is most noticeable with the "Enable HRTF" setting active (see HRTF Initialization). `AL_VELOCITY` is set to zero because Doppler effect is not modelled for camera movement (only for vehicle sources where appropriate).

  **Thread safety**: `syncListenerToCamera()` calls `alListener3f` and `alListenerfv` from the **main thread**. This is safe because of the following invariants, which are enforced by the `AudioSystem` constructor sequence:

  - `alcMakeContextCurrent(m_context)` in the `AudioSystem` constructor (Step 1) establishes a **process-wide default context** on the main thread. This binding is **permanent for the application lifetime** — `alcMakeContextCurrent(nullptr)` is NEVER called after this point, and no code path in `AudioSystem` may call it. Calling `alcMakeContextCurrent(nullptr)` after construction would clear the main-thread context and make `syncListenerToCamera()` operate on a null context (AL calls would silently no-op or crash).

    **Scope clarification**: "After this point" refers to application runtime only — while the game loop is executing and `syncListenerToCamera()` is being called. The `AudioSystem` destructor teardown sequence (`audio-thread-shutdown.md` Step 3.5) correctly calls `alcMakeContextCurrent(m_context)` to re-bind the context before AL resource cleanup, and Step 6 calls `alcMakeContextCurrent(nullptr)` as part of ordered teardown. These destructor calls are correct and mandatory. The prohibition applies only to runtime calls that would clear the main-thread context while `syncListenerToCamera()` is in use.

  - `alcSetThreadContext(m_context)` called inside the audio thread at startup establishes a **thread-local context override** for the audio thread only. This call is strictly per-thread: it affects only the calling thread's view of the current context and does NOT displace or modify the process-wide context held by the main thread.

  OpenAL Soft guarantees that a thread-local context on the audio thread does NOT invalidate the process-wide context on the main thread. Each thread sees its own current context independently: the audio thread uses its thread-local context; the main thread uses the process-wide context set at construction. Therefore `syncListenerToCamera()` is safe to call from the main thread at any point after `AudioSystem` construction — the process-wide context is always current on the main thread.

  **Verification requirement**: During the Phase 7 `ALC_EXT_thread_local_context` spike, confirm this guarantee holds on the OpenAL Soft version targeted for Linux and Windows CI. Log the OpenAL Soft version string at `AudioSystem` construction. If any platform shows that `alcSetThreadContext` on the audio thread displaces the main-thread context, escalate to the audio architecture review board before shipping.
