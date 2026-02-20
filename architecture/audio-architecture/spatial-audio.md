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

- **Listener sync**: `AudioSystem::syncListenerToCamera(camera)` called **once per frame** after Irrlicht updates the camera. This function must update ALL three listener attributes — omitting any one breaks HRTF spatialization:

  **Note**: These code examples use Irrlicht's `vector3df` member naming convention (`.X`, `.Y`, `.Z` uppercase). The AI Town `vec3` struct defined in `src/interfaces/vec3.h` uses **lowercase** `x`, `y`, `z` fields. Phase 4 `AudioSystem::syncListenerToCamera(const CameraState& cam)` must use `cam.position.x` (lowercase), `cam.forward.y` (lowercase), etc. — NOT `cam.position.X`. These are different types: Irrlicht's `core::vector3df` uses uppercase `.X/.Y/.Z`; AI Town's `vec3` uses lowercase `.x/.y/.z`.

  ```cpp
  void AudioSystem::syncListenerToCamera(const CameraParams& cam) {
      // Position
      alListener3f(AL_POSITION, cam.position.X, cam.position.Y, cam.position.Z);
      // Velocity (set to zero — we do not model Doppler for camera movement)
      alListener3f(AL_VELOCITY, 0.f, 0.f, 0.f);
      // Orientation: forward vector followed by up vector (6-float array)
      ALfloat orientation[6] = {
          cam.forward.X, cam.forward.Y, cam.forward.Z,   // "at" vector
          cam.up.X,      cam.up.Y,      cam.up.Z          // "up" vector
      };
      alListenerfv(AL_ORIENTATION, orientation);
  }
  ```

  **`AL_ORIENTATION` is required for HRTF**: Without updating `AL_ORIENTATION` each frame, HRTF uses the listener's initial orientation (default: facing −Z, up +Y) for all spatialization, producing incorrect head-related transfer function results when the camera rotates. HRTF breaks silently — there is no OpenAL error; sounds simply appear from wrong directions. This is most noticeable with the "Enable HRTF" setting active (see HRTF Initialization). `AL_VELOCITY` is set to zero because Doppler effect is not modelled for camera movement (only for vehicle sources where appropriate).

  **Thread safety**: `syncListenerToCamera()` calls `alListener3f` and `alListenerfv` from the **main thread**. When `ALC_EXT_thread_local_context` is active:
  - `alcMakeContextCurrent(m_context)` in the `AudioSystem` constructor establishes a **process-wide default context** visible to the main thread.
  - `alcSetThreadContext(m_context)` on the audio thread establishes a **thread-local context override** for the audio thread only.

  OpenAL Soft guarantees that a thread-local context on the audio thread does NOT invalidate the process-wide context on the main thread. Each thread sees its own current context independently: the audio thread uses the thread-local context; the main thread uses the process-wide context. Therefore `syncListenerToCamera()` is safe to call from the main thread provided `alcMakeContextCurrent(m_context)` was called (in the `AudioSystem` constructor) before the audio thread is launched.

  **Verification requirement**: During the Phase 4 `ALC_EXT_thread_local_context` spike, confirm this guarantee holds on the OpenAL Soft version targeted for Linux and Windows CI. Log the OpenAL Soft version string at `AudioSystem` construction. If any platform shows that `alcSetThreadContext` on the audio thread displaces the main-thread context, escalate to the audio architecture review board before shipping.
