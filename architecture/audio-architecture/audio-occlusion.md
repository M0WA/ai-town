# Audio Occlusion (V1)

- **Model**: Simplified two-state occlusion via `AL_EXT_EFX` (ships with OpenAL Soft)
- **Raycast frequency**: At most **1 raycast per source per 6 frames** (10 Hz at 60 FPS); only for sources within **100 m** of the listener; **max 8 raycasts per frame total**
- Raycasts performed against simplified collision-only scene layer (building `_col.obj` meshes + terrain; not full visual geometry)
- **Per-source EFX filter allocation** (mandatory): Allocate **one `AL_FILTER_LOWPASS` EFX filter object per source** in the SFX pool at initialization time. Bind each source's dedicated filter at pool construction:
  ```cpp
  // Required guard — check ALC_EXT_EFX and load entry points before any EFX call:
  if (!alcIsExtensionPresent(m_device, "ALC_EXT_EFX")) {
      logWarning("ALC_EXT_EFX not available — occlusion disabled");
      m_efxAvailable = false;
  } else {
      // Store EFX function pointers as AudioSystem members (not local vars) so the audio thread
      // can call them without re-querying (alGetProcAddress is not guaranteed thread-safe):
      m_fnGenFilters    = reinterpret_cast<LPALGENFILTERS>(alGetProcAddress("alGenFilters"));
      m_fnFilteri       = reinterpret_cast<LPALFILTERI>(alGetProcAddress("alFilteri"));
      m_fnFilterf       = reinterpret_cast<LPALFILTERF>(alGetProcAddress("alFilterf"));
      m_fnDeleteFilters = reinterpret_cast<LPALDELETEFILTERS>(alGetProcAddress("alDeleteFilters"));
      if (!m_fnGenFilters || !m_fnFilteri || !m_fnFilterf || !m_fnDeleteFilters) {
          logWarning("EFX entry points unavailable — occlusion disabled");
          m_efxAvailable = false;
      } else {
          m_efxAvailable = true;
          // Per-source EFX filter allocation (evictable SFX sources only;
          // stinger and stream sources are not positional and need no occlusion filter):
          // kEvictableSFXCount=55 in V1; iterates sources[0..54] only.
          for (int i = 0; i < kEvictableSFXCount; ++i) {
              // ... existing loop body using m_fnGenFilters, m_fnFilteri, m_fnFilterf ...
          }
      }
  }
  ```
  All occlusion code-paths must check `m_efxAvailable` before touching any EFX object.

  Full pool construction loop (executed only inside the `m_efxAvailable = true` branch above):
  ```cpp
  // kEvictableSFXCount=55 in V1; iterates sources[0..54] only.
  // m_efxAllocationAttempted is set to true before the first iteration and is
  // used to guard the shutdown loop regardless of whether allocation completed fully.
  m_efxAllocationAttempted = true;
  for (int i = 0; i < kEvictableSFXCount; ++i) {
      m_fnGenFilters(1, &m_occlusionFilter[i]);
      // CRITICAL: alGenFilters may fail (returns AL_FILTER_NULL = 0) if EFX resources
      // are exhausted. Check immediately after generation — calling alFilteri/alFilterf
      // on AL_FILTER_NULL produces an AL_INVALID_NAME error and leaves the filter
      // in an undefined state. Disable EFX and break if ANY filter fails:
      if (m_occlusionFilter[i] == AL_FILTER_NULL) {
          logWarning("EFX filter allocation failed at index " + std::to_string(i) + " — occlusion disabled");
          m_efxAvailable = false;
          break;  // do not attempt further filter allocations; cleanup handles partial arrays
      }
      m_fnFilteri(m_occlusionFilter[i], AL_FILTER_TYPE, AL_FILTER_LOWPASS);
      // CRITICAL: if m_fnFilteri or m_fnFilterf fails after m_fnGenFilters has already
      // returned a valid (non-null) filter ID, the just-allocated filter must be deleted
      // before disabling EFX and breaking. Without this cleanup the shutdown loop (guarded
      // by m_efxAllocationAttempted == true) will attempt to delete the filter, but there
      // is no guarantee the filter is in a usable state if parameter setup failed.
      // The safe pattern is: detect failure via alGetError() after each EFX call, and on
      // any failure call m_fnDeleteFilters(1, &m_occlusionFilter[i]), set
      // m_occlusionFilter[i] = AL_FILTER_NULL, set m_efxAvailable = false, and break.
      // Example:
      //   alClearError();
      //   m_fnFilteri(m_occlusionFilter[i], AL_FILTER_TYPE, AL_FILTER_LOWPASS);
      //   if (alGetError() != AL_NO_ERROR) {
      //       m_fnDeleteFilters(1, &m_occlusionFilter[i]);
      //       m_occlusionFilter[i] = AL_FILTER_NULL;
      //       m_efxAvailable = false;
      //       break;
      //   }
      m_fnFilterf(m_occlusionFilter[i], AL_LOWPASS_GAIN, 1.0f);      // fully open default
      m_fnFilterf(m_occlusionFilter[i], AL_LOWPASS_GAINHF, 1.0f);
      alSourcei(m_sources[i], AL_DIRECT_FILTER, m_occlusionFilter[i]);
      m_occlusionGainCurrent[i] = 1.0f;                                // per-source smoothed gain state
      m_occlusionGainTarget[i].store(1.0f, std::memory_order_relaxed); // target: 1.0 = open, 0.1 = fully occluded; store() required — m_occlusionGainTarget is std::atomic<float>
  }
  ```
  Do NOT use a single shared filter for multiple sources — modifying a shared filter's parameters affects all sources bound to it simultaneously.

  **Shutdown loop guard flags — two separate booleans required**:

  - `m_efxAllocationAttempted` (`bool`, default `false`): set to `true` immediately before the filter allocation loop begins (before the first `m_fnGenFilters` call). The `~AudioSystem()` destructor runs the filter cleanup loop when `m_efxAllocationAttempted == true`, regardless of whether allocation completed successfully. This prevents a leak where partial allocation succeeded (some filters have valid non-null IDs) but `m_efxAvailable` was set to `false` mid-loop — meaning the shutdown guard `m_efxAvailable == true` would be skipped and those valid filters would never be deleted.
  - `m_efxAvailable` (`bool`, default `false`): set to `true` only after ALL `kEvictableSFXCount` filters have been successfully allocated and configured. Runtime occlusion code paths (occlusion gain update, `onSourceRecycled()`, state-change HF writes) check `m_efxAvailable` before touching any EFX object.

  The cleanup loop in `~AudioSystem()` must use `m_efxAllocationAttempted` as its guard, and must skip `AL_FILTER_NULL` entries (which represent slots where `alGenFilters` returned null or were cleaned up mid-loop). After deleting each filter, reset the ID to `AL_FILTER_NULL` (consistent with the authoritative shutdown sequence in `audio-thread-shutdown.md`):
  ```cpp
  // In ~AudioSystem(), after m_audioThread.join():
  if (m_efxAllocationAttempted) {
      for (int i = 0; i < kEvictableSFXCount; ++i) {
          if (m_occlusionFilter[i] != AL_FILTER_NULL) {
              m_fnDeleteFilters(1, &m_occlusionFilter[i]);
              m_occlusionFilter[i] = AL_FILTER_NULL;
          }
      }
  }
  ```

  **Thread-ordering requirement**: ALL of the above (EFX extension check → function pointer loading → filter allocation loop) MUST execute in the `AudioSystem` constructor body BEFORE `m_audioThread = std::thread(...)` is launched. The EFX function pointers and filter objects are stored as `AudioSystem` members; writing them after thread launch would create a data race (main thread writes, audio thread reads without synchronization). The `std::thread` constructor acts as a synchronization barrier ONLY for memory written BEFORE the thread is launched — not for writes that occur concurrently after launch.

- **Occlusion gain floor**: `AL_LOWPASS_GAIN = 0.1f` (**−20 dB**) when a source is fully occluded. A gain floor of 0.25f (−12 dB) is too mild — at −12 dB the source remains clearly audible through solid building walls, losing the spatial cue that the sound is behind an obstruction. A floor of 0.1f (−20 dB) provides a perceptible but not complete attenuation; this is consistent with real-world acoustic transmission loss through light masonry at urban frequencies. (**Do not use a floor of 0.0f — complete silence from a nearby source is unnatural and breaks the spatial illusion.**)

- **Pool slot recycle — mandatory occlusion state reset**: When a pool slot is recycled (re-acquired for a new sound), `m_occlusionGainCurrent[i]` and `m_occlusionGainTarget[i]` must be reset to `1.0f` immediately, and the filter must be applied without waiting for the smoothing step. Without this, a slot that was fully occluded (`cur = 0.1f`) when recycled will play the new sound at 0.1f gain for up to 200 ms — a clearly audible muffled artifact at the start of every sound assigned to a previously-occluded slot:
  ```cpp
  // Required in AudioSourcePool (or AudioSystem) on slot recycle:
  // THREAD SAFETY: onSourceRecycled() is called from the MAIN THREAD (at SFX pool
  // acquisition time). The audio thread calls updateOcclusion() every 10 ms, which
  // also reads/writes m_occlusionFilter[] via m_fnFilterf and alSourcei.
  // These concurrent accesses constitute a data race (UB) without synchronization.
  // REQUIRED: acquire m_streamMutex (or a dedicated m_occlusionMutex) before any
  // EFX filter writes in onSourceRecycled(), and hold the same mutex in
  // updateOcclusion() during filter write operations.
  void onSourceRecycled(int i) {
      // THREAD SAFETY: onSourceRecycled() is called from the MAIN THREAD (at SFX pool
      // acquisition time). m_occlusionMutex must be acquired before any EFX filter writes
      // to prevent data races with the audio thread's updateOcclusion().
      std::lock_guard<std::mutex> lk(m_occlusionMutex);
      m_occlusionGainCurrent[i] = 1.0f;
      m_occlusionGainTarget[i].store(1.0f, std::memory_order_relaxed);
      if (m_efxAvailable) {
          m_fnFilterf(m_occlusionFilter[i], AL_LOWPASS_GAIN, 1.0f);
          alCheckError("alFilterf(AL_LOWPASS_GAIN) in onSourceRecycled");
          m_fnFilterf(m_occlusionFilter[i], AL_LOWPASS_GAINHF, 1.0f);
          alCheckError("alFilterf(AL_LOWPASS_GAINHF) in onSourceRecycled");
          alSourcei(m_sources[i], AL_DIRECT_FILTER, m_occlusionFilter[i]);
          alCheckError("alSourcei(AL_DIRECT_FILTER) in onSourceRecycled");
      }
  }
  ```
  This reset must occur before the new caller begins playing on the slot. `m_occlusionMutex` is a `std::mutex` member of `AudioSystem` (declared alongside `m_streamMutex`). The audio thread's `updateOcclusion()` must acquire `m_occlusionMutex` for the section of code that writes to `m_occlusionFilter[]` via EFX calls — not for the entire 10 ms wake cycle (holding the mutex across the full wake would block main-thread SFX pool operations for 10 ms). The `m_occlusionGainTarget[i]` field remains `std::atomic<float>` (written by main thread's raycast pass, read by audio thread's updateOcclusion) — the atomic type handles the gain-target reads without the mutex. Only the EFX filter writes (alSourcei, m_fnFilterf) require mutex protection.

- **Per-source gain smoothing** (prevents audible binary snap on occlusion state change): Instead of writing the target gain directly to the EFX filter each frame, maintain per-source smoothed gain state. Each audio thread wake (10 ms), update the current gain toward the target. **Dirty-check optimization**: only call `m_fnFilterf` and `alSourcei` when the gain actually changed — skipping unchanged sources reduces EFX driver call volume (55 sources × 2 EFX calls × 100 wakes/s = 11,000 calls/s without the check):
  ```cpp
  // Per-source smoothing step (called in updateOcclusion() each audio thread wake):
  constexpr float kOcclusionGainStep = 0.05f;  // max change per 10 ms wake ≈ 200 ms full transition
  // kEvictableSFXCount=55 in V1; iterates sources[0..54] only.
  for (int i = 0; i < kEvictableSFXCount; ++i) {
      float& cur = m_occlusionGainCurrent[i];
      float  tgt = m_occlusionGainTarget[i].load(std::memory_order_relaxed);
      bool changed = false;
      if (std::abs(cur - tgt) > kOcclusionGainStep) {
          cur += (tgt > cur) ? kOcclusionGainStep : -kOcclusionGainStep;
          changed = true;
      } else if (cur != tgt) {
          cur = tgt;
          changed = true;
      }
      if (changed) {  // only update driver state when gain actually changed
          m_fnFilterf(m_occlusionFilter[i], AL_LOWPASS_GAIN, cur);
          // Note: AL_LOWPASS_GAINHF is updated on state-change (see below), not per-step.
          // The broadband gain ramp dominates percept; HF cut is applied once at state transition.
          alSourcei(m_sources[i], AL_DIRECT_FILTER, m_occlusionFilter[i]);
      }
  }
  ```
  On occlusion state change for source i:
  - **Occluded**: `m_occlusionGainTarget[i].store(0.1f, std::memory_order_relaxed)`; also immediately apply `AL_LOWPASS_GAINHF = 0.3f` (−10 dB HF cut, simulating high-frequency absorption through walls). The GAINHF cut is applied at state-change (not smoothed), since the broadband gain ramp over 200 ms already masks the transition:
    ```cpp
    // On transition → occluded (main-thread raycast pass):
    m_occlusionGainTarget[i].store(0.1f, std::memory_order_relaxed);  // store() required — std::atomic<float>
    if (m_efxAvailable) {
        m_fnFilterf(m_occlusionFilter[i], AL_LOWPASS_GAINHF, 0.3f);
        alSourcei(m_sources[i], AL_DIRECT_FILTER, m_occlusionFilter[i]);
    }
    ```
  - **Unoccluded**: `m_occlusionGainTarget[i].store(1.0f, std::memory_order_relaxed)`; also immediately restore `AL_LOWPASS_GAINHF = 1.0f` (full HF response):
    ```cpp
    // On transition → unoccluded (main-thread raycast pass):
    m_occlusionGainTarget[i].store(1.0f, std::memory_order_relaxed);  // store() required — std::atomic<float>
    if (m_efxAvailable) {
        m_fnFilterf(m_occlusionFilter[i], AL_LOWPASS_GAINHF, 1.0f);
        alSourcei(m_sources[i], AL_DIRECT_FILTER, m_occlusionFilter[i]);
    }
    ```
  The smoothing step applies the broadband gain change gradually over ~200 ms, eliminating the audible click from a binary state snap. The GAINHF cut/restore is instantaneous on state-change — perceptually the gain ramp masks the HF change onset.

- **GAINHF occluded target**: `0.3f` (−10 dB). This simulates the acoustic effect of high-frequency absorption through building walls (walls pass low frequencies more readily than high frequencies). A value of 1.0f (no HF cut) produces audibly incorrect muffling — the source gets quieter but retains its full brightness, which is unnatural for occluded sounds. A value of 0.1f (same as gain floor) over-attenuates HF and can cause listener fatigue in dense urban scenes. `0.3f` is the V1 calibrated value — adjust only in coordination with audio design review.

### Source Recycle Requirement

Any operation that returns a pool source index to the evictable SFX pool MUST call `onSourceRecycled(i)` before marking the slot as available. This applies to ALL release paths without exception:

- **Audio LOD cull**: Zone ambient loops culled at > 300 m; vehicle engine sources culled at > 150 m — both call `onSourceRecycled(i)` (for each source index in a vehicle pair) before the slot is returned to the pool.
- **Explicit `stopSound(handle)`**: The source stop sequence calls `onSourceRecycled(i)` before marking the slot free.
- **Pool eviction**: When `acquireSFXSource()` evicts a lower-priority source to satisfy a new request, `onSourceRecycled(i)` is called on the evicted index before the slot is reassigned.

`onSourceRecycled(i)` resets `m_occlusionGainCurrent[i]` and `m_occlusionGainTarget[i]` to `1.0f` and immediately applies the fully-open filter state. Without this, a source slot that was fully occluded (`m_occlusionGainCurrent[i] = 0.1f`) at the time of recycle will play the next sound assigned to that slot at 0.1f gain for up to 200 ms — a clearly audible muffled artifact at every sound start on a previously-occluded slot. Failure to call `onSourceRecycled()` on the LOD cull path specifically leaves stale occlusion state that persists into the next occupant of that slot.

**Implementation note**: `onSourceRecycled(i)` is called from the main thread (at pool acquisition/eviction time). It acquires `m_occlusionMutex` internally (see the occlusion state reset section above). The caller must NOT already be holding `m_occlusionMutex` when calling `onSourceRecycled(i)` — doing so would deadlock.

- Full EFX reverb zones (interiors, tunnels) are post-V1 scope
